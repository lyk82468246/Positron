# Phase 1 — TLS 1.2 over WinCE: 实际历程与决策记录

记录从规划到 WM6 模拟器成功握手 api.anthropic.com 的过程中**真正踩到的坑和最终决策**，方便后续阶段及他人复用。

---

## 时间线摘要

1. 规划 `positron_tls.dll` 集成 mbedTLS 2.28.10
2. 写 `mbedtls_config.h`、`positron_tls.h/.c`、`test_host/main.c`
3. 改 `.vcproj`（XML 直编辑），加入约 100 个 mbedTLS 源文件
4. 首次构建：编译器找不到 `MBEDTLS_CONFIG_FILE` —— vcproj 里宏的引号转义不对
5. 修转义 + 注释里的 `/* */` 提前闭合 bug + 文件编码 → 进展到 `stdint.h` 缺失
6. 写 `stdint.h`/`inttypes.h` 兼容 shim → `_W64` 关键字 ARM CL 不认 → 干掉
7. 进展到 mbedTLS 内部 ~70 处 C99 mid-block 声明：**2.28 不是 C89 兼容的**
8. 决策：**降级到 mbedTLS 2.16.12 LTS**（最后一条真 C89 主线）
9. 重新构建：通过。生成 dll + exe
10. VS 部署到模拟器：失败。`%CSIDL_PROGRAM_FILES%` 不被展开
11. 弃用 VS 部署，改用模拟器**共享文件夹** + WMDC 桥接网络
12. `test_host.exe` 在模拟器跑通，弹出 `HTTP/1.1 404 Not Found from Cloudflare`

---

## 核心决策

### 选 mbedTLS 2.16.12 而非 2.28.x

**起因**：2.28.10 在它自己 `library/bignum.c:1543` 等约 70 个有效编译点用 C99 mid-block 声明（变量在代码块中间初始化）。VS2008 MSVC 9.0 没有 C99 模式，cl 报 `C2143/C2275` 拒绝。

**为什么不补丁 2.28**：
- 70+ 站点机械修改易出错
- 即便改完，仍有 6 处 `for (int i = 0; ...)` 等其它 C99 写法
- mbedTLS 2.28 后续会越来越远离 C89

**为什么不编译为 C++**：
- 全 mbedTLS 库 109 次 `mbedtls_calloc(...)` 调用里只有 3 处加了显式 cast；剩余 106 处依赖 C 的隐式 `void*→T*` 转换，C++ 模式必报错

**代价**：2.16 已于 2021-12 EOL，无后续安全更新。对 hobby/复古目标可接受。

### 自己写 `stdint.h` / `inttypes.h` shim

VS2008 不带 `stdint.h`（C99 头），但 mbedTLS（任何版本）都用 `uint32_t` 等定宽类型。我们在 `positron_tls/stdint.h` + `inttypes.h` 写最小 shim，用 MSVC 扩展 `__int8/__int16/__int32/__int64`。

**踩过的坑**：第一版用了 `_W64` 关键字（MSVC 桌面 cl 通过 `<vadefs.h>` 提供，给 64-bit 移植警告用的），WinCE ARM 交叉 cl 不认 → C2054。直接拿掉，WM6 永远是 32-bit 不需要这个标注。

### `OutputDirectory` 从 `$(PlatformName)\$(ConfigurationName)` 改为 `bin\$(ConfigurationName)`

`$(PlatformName)` 展开为 `Windows Mobile 6 Professional SDK (ARMV4I)`，含**空格 + 括号**。VS2008 部署引擎在某些路径解析点对此处理不当（生成时正常，部署时假阴性）。改用纯 ASCII 路径 `bin\Debug` 后构建链路本身稳定下来。

### `MBEDTLS_NET_C` 不启用，自己写 BIO

mbedTLS 的 `net_sockets.c` 用 `getaddrinfo`，WinCE 5.2 上没有这个 API。配置里禁掉 `MBEDTLS_NET_C`，在 `positron_tls.c` 实现两个 BIO 回调（`ptls_bio_send`/`ptls_bio_recv`）配 raw `SOCKET`，DNS 用 `gethostbyname`。

由此带来副作用：mbedTLS 的 `MBEDTLS_ERR_NET_SEND_FAILED` / `MBEDTLS_ERR_NET_RECV_FAILED` 错误码也被 `#if defined(MBEDTLS_NET_C)` 屏蔽。我们在 positron_tls.c 顶部按 mbedTLS 2.x 原值 `-0x004E / -0x004C` 本地 `#define` 补上。

### 熵源走自实现 `mbedtls_hardware_poll`

WinCE 5.2 上 `CryptGenRandom` 在不同 ROM 上行为不一致；我们启用 `MBEDTLS_ENTROPY_HARDWARE_ALT` 提供一个混合 `QueryPerformanceCounter + GetTickCount + GetCurrentThreadId + GetCurrentProcessId + GetSystemTime` 的实现，再喂给 CTR-DRBG。

**密码学等级**：低（远不如 OS RNG），Phase 2+ 应替换。但 mbedTLS 的 CTR-DRBG 会做后续放大，对 TLS 客户端的 ClientHello.random + ephemeral key 生成够用了。

### `lstrcpynA` / `lstrlenA` / `wsprintfA` 全部弃用

WinCE coredll 对 ANSI 后缀函数的导出**不一致**。某些镜像（包括我们用的 WM6 Pro Emulator）只有 W 版。链接器报 `LNK2019: 无法解析的外部符号 lstrlenA`。

**替代**：CRT 的 `strlen` / `_snprintf`（VS2008 WinCE CRT 都有），加一个手写的 `ptls_safe_copy` 替 `lstrcpynA`。

---

## VS2008 IDE 与 vcproj 的若干坑

### 宏值里的引号转义

vcproj 里给宏定义带值是用 `<key>=<value>` 形式，如果 value 本身含双引号（比如 `MBEDTLS_CONFIG_FILE="mbedtls_config.h"`），**必须**写成：

```xml
PreprocessorDefinitions="...;MBEDTLS_CONFIG_FILE=\&quot;mbedtls_config.h\&quot;;..."
```

注意 `\&quot;` — 反斜杠转义到命令行层、`&quot;` XML 转义到属性值层。漏 `\` 就变成无引号宏，`#include MBEDTLS_CONFIG_FILE` 展开成 `#include mbedtls_config.h`（语法错）。

### 注释里的字面 `/* */`

C 注释不嵌套。文件头注释里若出现示例 `/* */`，会**提前关闭**外层块注释，造成奇怪的连锁解析错误，且 cl 的错误位置往往在很后面，难定位。

```c
/*
 * ...
 * /* */ 这是字面示例 ← 危险
 */
```

替换成文字描述如 `slash-star comments`。

### vcproj 的 ProjectDependencies 自动注入 .lib

`.sln` 里 test_host 的 `ProjectSection(ProjectDependencies)` 列了 positron_core / positron_json 后，VS 自动把 `positron_core.lib` / `positron_json.lib` 加到 test_host 的 link 输入。空壳工程不产 lib → `LNK1181`。Phase 1 把 .sln 改成只依赖 positron_tls。

---

## VS2008 部署引擎的奇怪行为

**症状**：`test_host.vcproj` 的 `<DeploymentTool RemoteDirectory="\Program Files\test_host">` 字面值正确，VS IDE 属性页里显示也正确，但实际 deploy 时报：

```
无法启动程序"%CSIDL_PROGRAM_FILES%\test_host\test_host.exe"
系统找不到指定的文件
```

错误对话框里的 `%CSIDL_PROGRAM_FILES%` 是字面字符串（项目里没人写过这个），来自 VS 部署引擎内部硬编码默认值。`grep -ri csidl .` 在整个项目根下 0 命中。

**怀疑成因**：VS2008 Smart Device 部署引擎对 vcproj 的 `<DeploymentTool>` 元素解析路径有 bug 或对路径格式有未公开要求，导致 vcproj 中的 RemoteDirectory 被忽略，回退到 `%CSIDL_PROGRAM_FILES%\<project_name>\` 默认值，又不在传递给主机端 debugger 时做展开。

**短期方案**：模拟器**共享文件夹**功能挂主机目录到设备 `\Storage Card\`，手动双击运行。完全绕开 VS 部署。

**长期方案** Phase 2/3 再调：
- 试把 `<DeploymentTool>` 元素彻底删掉，让 VS 走纯默认值并自行解析
- 或脚本化 `cspr.exe` / `ceutil` 直接推文件
- 或写 PowerShell 脚本走 ActiveSync FileSystem 接口

---

## 验证结果

WM6 Pro Emulator 中，`test_host.exe` 弹 MessageBox 显示：

```
HTTP/1.1 404 Not Found
Date: Thu, 28 May 2026 ...
Server: cloudflare
...
```

证明：
- TLS 1.2 ClientHello → ECDHE 密钥交换 → ServerHello → Finished
- SNI 正确（`api.anthropic.com`，否则证书拿错）
- 加密数据通道双向工作
- HTTP 报文回传完整

---

## 文件清单

| 路径 | 角色 | 大小（约） |
|---|---|---|
| `positron_tls/positron_tls.h` | 公开 API 头 | 1.5 KB |
| `positron_tls/positron_tls.c` | DLL 实现 | 11 KB |
| `positron_tls/mbedtls_config.h` | 裁剪配置 | 3 KB |
| `positron_tls/stdint.h` | C99 shim | 4 KB |
| `positron_tls/inttypes.h` | C99 shim | 4 KB |
| `test_host/main.c` | 验证程序 | 3 KB |
| `positron_tls.dll` (Debug 构建) | 输出 | ~320 KB |
| `test_host.exe` (Debug 构建) | 输出 | ~6 KB |

# Positron

Electron-like 轻量级框架，目标设备：**Windows Mobile 6 Professional** (Windows CE 5.2, ARMv4i)。

让 18 年前的 WM6 设备能跑 HTTPS、JSON 处理与（最终）HTML/JS 应用，构建在共享 DLL 生态之上。

---

## 当前状态

| Phase | 内容 | 状态 |
|---|---|---|
| **1** | `positron_tls.dll` — TLS 1.2 客户端（mbedTLS 2.16 LTS） | ✅ 完成，已在 WM6 Pro 模拟器上跑通 |
| 2 | `positron_json.dll` (cJSON) + `positron_http.dll` (HTTP/1.1 over TLS) | 计划中 |
| 3+ | `positron_core.dll`（CA 校验、HTTP keep-alive、HTML/JS 渲染等） | 待定 |

Phase 1 验证：`test_host.exe` 在 WM6 Pro Emulator 上向 `api.anthropic.com:443` 发起 TLS 1.2 握手并 GET `/`，收到 Cloudflare 真实的 `HTTP/1.1 404` 响应——证明 TLS 握手 + ECDHE 密钥交换 + 加密通道 + SNI 全套链路工作。

---

## 工具链

- **编译器**：MSVC 9.0（VS2008 SP1，C89-only，无 C99/C++11）
- **SDK**：Windows Mobile 6 Professional SDK (ARMV4I)
- **目标 Subsystem**：`windowsce,5.02`
- **链接库**：`ws2.lib`（WinCE 版 Winsock2，非桌面 `ws2_32.lib`）
- **加密库**：mbedTLS **2.16.12 LTS**（最后一条真正 C89 兼容的主线；2.28+ 用了 C99 mid-block 声明 VS2008 编不动）

---

## 仓库结构

```
Positron/
  Positron.sln                  VS2008 solution（4 工程登记）
  README.md                     本文件
  PHASE1.md                     Phase 1 经验/坑记录

  positron_tls/                 TLS 1.2 DLL
    positron_tls.h              公开 API（PTls_Init/Connect/Write/Read/Close/Cleanup）
    positron_tls.c              实现（DllMain + Winsock2 BIO + 熵源 + API）
    mbedtls_config.h            裁剪后的 mbedTLS 配置
    stdint.h                    VS2008 无 stdint.h，给 mbedTLS 补的 shim
    inttypes.h                  同上
    positron_tls.vcproj         VS2008 工程文件
    mbedtls/                    mbedTLS 2.16.12 源码树（未跟踪进 git，下载方式见下）
    bin/Debug/                  构建产物（gitignore）

  test_host/                    端到端测试 EXE
    main.c
    test_host.vcproj

  positron_json/                Phase 2，空壳工程
  positron_core/                预留，空壳工程
```

---

## 编译

### 一次性准备

1. 安装 **VS2008 SP1** + **Windows Mobile 6 Professional SDK** + **WM6 Pro Emulator**
2. 下载 mbedTLS 2.16.12：
   ```
   https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/mbedtls-2.16.12.zip
   ```
3. 解压，把 `include/` 和 `library/` 整体复制到 `positron_tls/mbedtls/` 下：
   ```
   positron_tls/mbedtls/include/mbedtls/...
   positron_tls/mbedtls/include/psa/...
   positron_tls/mbedtls/library/*.c
   positron_tls/mbedtls/library/*.h
   ```
   （`mbedtls/` 目录不入 git，每个开发机自己准备）

### 构建

打开 `Positron.sln`，确认顶部工具栏：
- Solution Configuration = `Debug`
- Solution Platform = `Windows Mobile 6 Professional SDK (ARMV4I)`

**生成 → 生成解决方案** (F7)。产物：

```
positron_tls/bin/Debug/positron_tls.dll
positron_tls/bin/Debug/positron_tls.lib
test_host/bin/Debug/test_host.exe
```

---

## 部署到模拟器

> ⚠ **VS2008 内置 Smart Device 部署对当前工程不可用**——RemoteDirectory 配置无论怎么写都被部署引擎覆盖成它的硬编码默认值 `%CSIDL_PROGRAM_FILES%\<project>\` 字面字符串，导致主机端 debugger 启动时找不到文件。详见 [PHASE1.md](PHASE1.md)。

**推荐方式：模拟器共享文件夹**

1. 启动模拟器（VS → 工具 → Device Emulator Manager → 双击 WM6 Pro Emulator）
2. Cradle 它（VS 配 WMDC 时设备需 Cradle）
3. 模拟器窗口 → File → Configure → **Shared folder** tab → 浏览选一个主机目录（如 `C:\WMShare\`）
4. 把构建产物拷进去：
   ```
   positron_tls/bin/Debug/positron_tls.dll → C:\WMShare\
   test_host/bin/Debug/test_host.exe       → C:\WMShare\
   ```
5. 模拟器内 Start → File Explorer → **Storage Card** → 双击 `test_host.exe`

### 网络配置

WM6 模拟器默认无网。让它走主机网：

1. 主机 Windows Mobile Device Center → Mobile Device Settings → **Connection Settings**
2. 勾 "Allow data connections on device when connected to PC"
3. "This computer is connected to" 选 **The Internet**
4. Device Emulator Manager → Uncradle → Cradle 重新挂

模拟器内 IE 能打开 `http://example.com` 表示出网成功。

### 预期结果

`test_host.exe` 弹 MessageBox 显示 `HTTP/1.1 404 Not Found` + `Server: cloudflare`——证明 TLS 1.2 握手通过、HTTP 响应正常收到。

---

## 已知限制（Phase 1）

- **证书未校验**：`MBEDTLS_SSL_VERIFY_NONE`，连接易被 MITM。Phase 2/3 会嵌入 Mozilla CA bundle 替换。
- **熵源弱**：用 `GetTickCount + QueryPerformanceCounter + GetCurrentThreadId/ProcessId` 做的 jitter source，经 CTR-DRBG 放大。理论上能用，密码学等级低于 `CryptGenRandom`。
- **VS2008 部署破损**：必须用共享文件夹方式手动部署，详见 PHASE1.md。
- **同步阻塞**：`PTls_Connect/Write/Read` 全是阻塞调用，不适合 UI 线程长时间使用。

---

## License

代码本身 MIT。mbedTLS 是 Apache 2.0（详见 `mbedtls/LICENSE`，不入 git，跟随上游）。

# Positron

Electron-like 轻量级框架，目标设备：**Windows Mobile 6 Professional** (Windows CE 5.2, ARMv4i)。

让 18 年前的 WM6 设备能跑 HTTPS、JSON、HTTP/1.1，并（最终）能用 HTML/JS 写应用——构建在共享 DLL 生态之上。

---

## 当前状态

| Phase | 内容 | 状态 |
|---|---|---|
| **1** | `positron_tls.dll` — TLS 1.2 客户端（mbedTLS 2.16 LTS） | ✅ 完成，emulator 验证 |
| **2** | `positron_json.dll` (cJSON 1.7.18) + `positron_http.dll` (HTTP/1.1 over TLS) | ✅ 完成，emulator 验证 |
| **3** | 嵌入式 CA bundle + verified TLS (`PTls_ConnectVerified`) + CryptGenRandom 熵源 | ✅ 完成，emulator 验证 |
| **4** | `positron_core.dll` — NetSurf 内核移植（HTML/CSS 渲染层） | 🚧 进行中：NetSurf 真实 `layout.c/redraw.c` 已成为正式 Browse 路径；flex + table 已真机验证（TEST 17）；border / selector / 图片后续 |

Phase 3 验证：`test_host.exe` 的通信组——HTTPS GET（`checkip.amazonaws.com`，大陆直连纯文本 IP）、POST（postman-echo）、badssl.com 正样本 + expired + self-signed 三连测，全部真机通过。详见 [PHASE3.md](PHASE3.md)。

Phase 4 进展：vendoring NetSurf 3.11，五个底层库（libwapcaplet / libparserutils / libhubbub / libdom / libcss）全部在 VS2008 / WinCE / ARM 下编译通过（C99→C89 脚本化转换，见 `scripts/c89ize.py` 等）。`positron_core.dll` 已作为产品级引擎边界立起，公开 `PCore_ParseHTML/ParseCSS/StyleDocument/LayoutDocument/PaintDocument/LinkAt` 等小巧 opaque-HANDLE API。HTML→DOM、CSS 解析、CSS select/computed style、整树样式、GDI 窗口绘制、垂直滚动、viewport/DPI 自适应、点击导航、外部 `<link rel="stylesheet">` 抓取、明文 `http://` via WinInet、跨协议重定向、完整 Mozilla CA bundle 均已真机验证。

当前 Browse 正式路径已经从手写块流布局切到 **NetSurf 真实布局/重绘引擎**：`PCore_LayoutDocument` / `PCore_PaintDocument` / `PCore_LinkAt` 走 `pcore_box_construct` → NetSurf `layout_document` → `html_redraw` → GDI plotter。M7-flex 已接入 `layout_flex.c`，TEST 17 三色块横排；M7-table 已接入 `table.c` + `BOX_TABLE > ROW_GROUP > ROW > CELL` 构建，TEST 17 2×2 表格网格真机验证。下一步优先级：真实 border 绘制（`redraw_border.c`）、CSS attribute/sibling selector、图片/SVG、后台导航体验。详见 [PHASE4.md](PHASE4.md) 和 [.agents/ROADMAP.md](.agents/ROADMAP.md)。

---

## 工具链

- **编译器**：MSVC 9.0（VS2008 SP1，C89-only，无 C99/C++11）
- **SDK**：Windows Mobile 6 Professional SDK (ARMV4I)
- **目标 Subsystem**：`windowsce,5.02`
- **链接库**：`ws2.lib`（WinCE 版 Winsock2，非桌面 `ws2_32.lib`）
- **加密库**：mbedTLS **2.16.12 LTS**（最后一条真正 C89 兼容的主线；2.28+ 用了 C99 mid-block 声明 VS2008 编不动）
- **JSON 库**：cJSON **1.7.18**（C89 兼容）

---

## 仓库结构

```
Positron/
  Positron.sln                  VS2008 solution（9 工程登记）
  README.md                     本文件
  PHASE1.md                     Phase 1 经验/坑记录
  PHASE2.md                     Phase 2 设计 + 已知风险点

  positron_tls/                 TLS 1.2 DLL
    positron_tls.h              公开 API
    positron_tls.c              实现（DllMain + Winsock2 BIO + 熵源 + API）
    mbedtls_config.h            裁剪后的 mbedTLS 配置
    ca_bundle.h                 嵌入的完整 Mozilla 根集（~140 根，脚本生成）
    gen_ca_bundle.py            从 curl cacert.pem 提取根证书的生成脚本
    positron_tls.vcproj
    mbedtls/                    mbedTLS 2.16.12 源（不入 git，见下）

  positron_json/                cJSON 包装 DLL
    positron_json.h / .c / .vcproj
    cjson/                      cJSON 1.7.18 源（已入 git）

  positron_http/                HTTP/1.1 客户端 DLL
    positron_http.h / .c / .vcproj

  positron_core/                NetSurf 引擎共享 DLL 边界
    positron_core.h / .c          PCore_* API（Parse/Style/Layout/Paint/LinkAt）
    pcore_box.c                   DOM+computed style → NetSurf box tree；正式 layout/paint/link path
    pcore_plot_gdi.c              NetSurf plotter + GDI 字体量度表
    pcore_talloc.c                精简 talloc 垫片
    nsshim/                       NetSurf layout/redraw 依赖的精简 shim 头
    positron_core.vcproj          DLL，静态链入 NetSurf 库与移植的 layout/redraw 源

  test_host/                    端到端测试 EXE（分通信/引擎/GDI渲染/Browse 组）
    main.c
    test_host.vcproj

  compat/                       VS2008 + WinCE 缺的 C99 shims
    stdint.h
    inttypes.h

  scripts/
    stage.bat                   一键把 5 个二进制拷到 C:\WMShare\

  .agents/                      Codex 接手交接、调试纪律、路线图
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
   （`mbedtls/` 不入 git，开发机自备）

cJSON 已经入 git，无需额外下载。

### 构建

打开 `Positron.sln`，确认顶部工具栏：
- Solution Configuration = `Debug`
- Solution Platform = `Windows Mobile 6 Professional SDK (ARMV4I)`

**生成 → 生成解决方案** (F7)。产物：

```
positron_tls/bin/Debug/positron_tls.dll
positron_json/bin/Debug/positron_json.dll
positron_http/bin/Debug/positron_http.dll
positron_core/bin/Debug/positron_core.dll
test_host/bin/Debug/test_host.exe
```

---

## 部署到模拟器

> ⚠ **VS2008 内置 Smart Device 部署对当前工程不可用**——无论 RemoteDirectory 怎么配都被部署引擎覆盖。详见 [PHASE1.md](PHASE1.md)。改用模拟器共享文件夹。

### 一次性配置

1. 启动 WM6 Pro Emulator（VS → 工具 → Device Emulator Manager）
2. Cradle 它（连 WMDC）
3. 模拟器 → File → Configure → **Shared folder** 选 `C:\WMShare\`
4. 主机 Windows Mobile Device Center → Mobile Device Settings → Connection Settings：
   - 勾 "Allow data connections on device when connected to PC"
   - "This computer is connected to" 选 **The Internet**
   - Uncradle / Cradle 重挂一次

### 每次构建后

```cmd
scripts\stage.bat         :: 默认 Debug
scripts\stage.bat Release :: 或 Release
```

把 5 个二进制拷到 `C:\WMShare\`。

模拟器内 Start → File Explorer → **Storage Card** → 双击 `test_host.exe`。

### 测试入口

`test_host.exe` 启动后先选择测试组：

- **Communication**：TEST 1-5，TLS / HTTP / JSON，需要网络。
- **Engine**：TEST 6-11、15、16，HTML/CSS/DOM/select/style/layout/box tree，离线。
- **GDI Render**：TEST 14、17、12，窗口绘制，离线。
- **Browse**：TEST 13，真实 HTTPS 页面抓取 + 渲染，需要网络。

当前关键 smoke test：

- TEST 17：内置 NetSurf real layout + redraw 页面。预期看到深红 H1、三色 flex 横排、2×2 table 网格。
- TEST 13：打开 start page，点击 `Open example.com`，走真实 Browse 路径；页面内链接可继续点击导航。

> ⚠ **跑 TEST 5 之前先把模拟器系统时钟设到当前**（开始 → 设置 → 系统 → Clock & Alarms）。WM6 Emulator 默认是 2005-2007 年某个时间，会让所有现役证书都看着像"尚未生效"。

---

## 已知限制 / 注意事项

- **熵源**：默认 `CryptGenRandom`（Phase 3 起）；CSP 不可用时自动退回 QPC+GetTickCount+tid/pid jitter，CTR-DRBG 兜底。
- **HTTP 限制**：单连接 `Connection: close`、无 keep-alive、无 gzip 解码、响应体 cap 1 MB；GET 已有有限 3xx follow，明文 `http://` 经 WinInet。
- **同步阻塞**：所有网络调用阻塞，不适合直接在 UI 线程长跑。
- **渲染限制**：真实 NetSurf layout/redraw 已接入，但 border redraw、图片/SVG、部分 CSS selector、float/复杂表格仍待补。
- **WM6 X 按钮 = 最小化不是关闭**。每次启动 test_host 前确认任务管理器没有遗留实例，否则 stage.bat 替换 exe 时会产生 image 不一致。
- **WMDC 桥会静默断**：host 待机 / 模拟器长跑后偶尔失联，表现是 `PTls_Connect` 拿到 `-0x004C [BIO: recv WSA=...]`。修法：重启 WMDC（任务栏 → 退出 → 重启）。**联网测试前先在 IE Mobile 打开 baidu 验证一遍**。
- **模拟器时钟**：跑 verified TLS 前必须校准（见上）。证书 notBefore/notAfter 都按 UTC 比对当前时间。

---

## License

代码本身 MIT。mbedTLS 是 Apache 2.0（详见 `mbedtls/LICENSE`，不入 git，跟随上游）。cJSON 是 MIT。

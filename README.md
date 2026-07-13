# Positron

面向 **Windows Mobile 6 Professional**（Windows CE 5.2, ARMv4i）的现代基础设施与应用运行时。

Positron 一方面提供可被任意 WM 程序独立调用的现代 DLL 集合，包括 TLS、HTTP、JSON、渲染核心以及后续图片等能力；另一方面在这些基础设施上建设自带浏览器内核和 Electron-like 应用运行时。当前主线已经进入 HTML/CSS 真实渲染：NetSurf 3.11 的解析、样式、layout/redraw、GDI 绘制和点击导航都在 `positron_core.dll` 后面跑通；JavaScript 是长期必须实现的目标，但尚不是当前可用能力。

公共 DLL 是正式产品，不只是 `test_host.exe` 或浏览器的内部依赖。架构与 ABI 原则见 [.agents/ARCHITECTURE.md](.agents/ARCHITECTURE.md)。

---

## 当前状态

| Phase | 内容 | 状态 |
|---|---|---|
| **1** | `positron_tls.dll` — TLS 1.2 客户端（mbedTLS 2.16 LTS） | ✅ 完成，WM6 Emulator 验证 |
| **2** | `positron_json.dll` (cJSON 1.7.18) + `positron_http.dll` (HTTP/1.1：HTTPS via mbedTLS，明文 HTTP via WinInet) | ✅ 完成，WM6 Emulator 验证 |
| **3** | 嵌入式 CA bundle + verified TLS (`PTls_ConnectVerified`) + CryptGenRandom 熵源 | ✅ 完成，WM6 Emulator 验证 |
| **4** | `positron_core.dll` — NetSurf 内核移植（HTML/CSS 渲染层） | 🚧 正式 Browse 路径已走 NetSurf `layout.c/redraw.c`；flex、table、border、selector、缓存图片链与 CSS 背景图已真机验证，窄屏复杂布局仍待补 |
| **5** | `positron_image.dll` — 可复用图片基础设施 | 🚧 SVG parse/draw/cache/fallback、网络 fixture、复合 fill-rule、CSS 背景图、原生 GDI text 与连续线性渐变已由 TEST25-32/13 真机验证；TEST33 渐变坐标/变换待设备验收 |

Phase 3 验证：`test_host.exe` 的通信组——HTTPS GET（`checkip.amazonaws.com`，大陆直连纯文本 IP）、POST（postman-echo）、badssl.com 正样本 + expired + self-signed 三连测，全部真机通过。详见 [PHASE3.md](PHASE3.md)。

Phase 4 进展：vendoring NetSurf 3.11，五个底层库（libwapcaplet / libparserutils / libhubbub / libdom / libcss）全部在 VS2008 / WinCE / ARM 下编译通过（C99→C89 脚本化转换，见 `scripts/c89ize.py` 等）。`positron_core.dll` 已作为产品级引擎边界立起，公开 `PCore_ParseHTML/ParseCSS/StyleDocumentEx/LayoutDocument/PaintDocument/LinkAt` 等小巧 opaque-HANDLE API。HTML→DOM、CSS 解析、CSS select/computed style、整树样式、外部 `<link rel="stylesheet">` 抓取、GDI 窗口绘制、垂直滚动、viewport/DPI 自适应、点击命中与导航、HTTPS verified fetch、明文 `http://` via WinInet、跨协议重定向、完整 Mozilla CA bundle 均已真机验证。

当前 Browse 正式路径已经从早期手写块流布局切到 **NetSurf 真实布局/重绘引擎**：`PCore_LayoutDocument` / `PCore_PaintDocument` / `PCore_LinkAt` 走 `pcore_box_construct` → NetSurf `layout_document` → `html_redraw` → GDI plotter。M7-flex/table、M5f border、CSS attribute/sibling selector 与 `:link` / `:lang()` 已由 TEST 9/17 真机验证。TEST 11 的 margin collapse 与 `padding-top:1px` 阻断折叠成对断言已于 2026-07-10 真机通过。`<img>` alt fallback 已由 TEST 17 验证；TEST 18 的文档级资源缓存与 URL 去重、TEST 20 的 BMP/PNG/JPEG/GIF 缓存 replaced box/`content_redraw`/`plot_bitmap` 绘制均已真机通过。TEST 21 已验证运行时 viewport/DPI 及整数像素 MQ4 `(width <= Npx)` / `(width < Npx)` 的 320/300/299px 边界。TEST13 已确认 `white-space:normal/nowrap` 的源码换行被正确折叠、词间距正常；TEST15 又确认 `<pre>` 换行仍保留。TEST 22 已验证反向 flex 的 25px leading padding；IANA 正文不再左裁切，但窄屏页脚/导航仍有真实版式缺口，因此 TEST13 整体仍未验收通过。Browse host 在布局前使用同一 HTTP 获取器填充 `<img>` 缓存，失败仍保留 alt/src 回退。SVG parse/draw/cache/fallback 已由 TEST25-28 真机通过，TEST13 的 HTTPS HTML + 相对 SVG 网络 fixture 也已显示正确。详见 [PHASE4.md](PHASE4.md)、[.agents/ROADMAP.md](.agents/ROADMAP.md) 和 [.agents/KNOWN_LIMITATIONS.md](.agents/KNOWN_LIMITATIONS.md)。

当前可用能力：TLS/HTTP/JSON 通信栈；HTML/CSS/DOM 解析；CSS select + computed style；整树样式；外链 CSS；NetSurf real layout/redraw；GDI plotter；滚动、viewport/DPI 自适应、点击链接导航；flex、常见 table、border、CSS attribute/sibling/static-pseudo selector、`<img>` alt fallback 与 `<img src>` 资源发现/fetch。WM Imaging 的 BMP/PNG/JPEG/GIF 与缓存 `<img>` 链已真机验证。`positron_image.dll` 公共 C ABI 已接通 Expat、libdom XML、libsvgtiny 与 NanoSVG rasterizer；TEST25-27 已依次确认 SVG parse、抗锯齿 retained draw 和缓存 replaced-box 绘制。

最新设备反馈（2026-07-13）：TEST29 确认默认 nonzero 实心和 inherited evenodd 孔洞；TEST30 确认 CSS `background-image` 定位、平铺与复用；TEST31 确认 WM GDI 文字样式、anchor 与 path/text 顺序。TEST32 首轮暴露 libsvgtiny 三角展开的密集接缝，改为结构化 stop/坐标映射 -> NanoSVG 单路径连续填充后，真机截图确认红紫蓝色带平滑、白字正确且自动 seam/jump guard 通过。TEST33 已构建，用三块图验证 objectBoundingBox 斜向、userSpaceOnUse 水平及 gradientTransform 旋转后的竖向线性渐变，待设备验收。

当前明确缺口：位图四格式与 SVG 网络/缓存/fallback/fill-rule 链已经闭环。CSS 单一背景图现复用 document cache 与 NetSurf `box->background -> redraw -> plot_bitmap`，TEST30 已真机通过；暂不包含 `background-size`、多层背景或资源异步事务。SVG text 已接入 libsvgtiny 元数据与 WM GDI 字体后端并由 TEST31 真机通过；当前只覆盖基础 UTF-8 文本、fill、字号、通用字体族、粗斜体、anchor 和常见变换。TEST32 已真机证明连续线性渐变/文本的缓存正式链；TEST33 的坐标系/变换矩阵待设备，径向渐变、spread method 和 stop opacity 仍未实现。CSS 动态状态伪类、float、复杂 table、forms/widgets 仍不完整；JavaScript 尚未实现但属于长期必做目标。

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
  Positron.sln                  VS2008 solution（含 NetSurf 静态库与产品 DLL）
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

  positron_expat/               Expat 2.8.2 静态库及 WM/VS2008 适配
  positron_libsvgtiny/          NetSurf libsvgtiny 静态库
  positron_image/               可供任意 WM 程序调用的图片 DLL（SVG API 起点）

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

命令行构建（agent 和日常开发的首选入口）：

```cmd
scripts\build.bat                 :: 默认 Debug 增量 Build
scripts\build.bat Debug rebuild   :: Debug 全量 Rebuild
scripts\build.bat Debug build     :: Debug 增量 Build
scripts\build.bat Release rebuild :: Release 全量 Rebuild
scripts\build.bat Debug clean     :: 清理 Debug
```

脚本调用 VS2008 的 `Common7\IDE\devenv.com`，按解决方案中的工程依赖构建
`Debug|Windows Mobile 6 Professional SDK (ARMV4I)`，并将完整输出写入
`vs2008-build.log`。ARM 编译器本体位于 `VC\ce\bin\x86_arm\cl.exe`，但不应绕过
`.sln` 直接逐文件调用它，否则必须手工复制 SDK include/lib、宏、链接参数和工程顺序。

也可以打开 `Positron.sln`，确认顶部工具栏：
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
scripts\stage.bat Debug C:\WMShare\Positron-next :: 旧进程锁文件时隔离 staging
```

把 5 个二进制拷到 `C:\WMShare\`。

模拟器内 Start → File Explorer → **Storage Card** → 双击 `test_host.exe`。

### 测试入口

`test_host.exe` 启动后先选择测试组：

快速复测可在 `test_host.exe` 同目录放置 `test_host.ini`：

```ini
# 支持逗号、空格、范围，以及特殊编号 7b
tests=31,32
```

读取到有效配置后，程序先提示是否只运行列出的 TEST；选 **Yes** 直接按编号升序执行，选 **No** 完整保留下面原有的 All/四组选择流程。文件缺失时直接走旧流程；文件存在但无效时提示并忽略。TEST23 已撤回，配置中出现 23 会被拒绝。`scripts\stage.bat` 会复制仓库内默认的 `test_host\test_host.ini`，当前默认只选最新 TEST33；可在共享目录直接修改后重新启动。

- **Communication**：TEST 1-5，TLS / HTTP / JSON，需要网络。
- **Engine**：TEST 6-11、15、16、18、21、22、24、25，HTML/CSS/DOM/select/style/layout/box tree/image resource cache、responsive media viewport、row-reverse flex padding、cached CSS restyle 与 SVG parse，离线。2026-07-12 已由用户真机确认整组通过。TEST23 float 最小样例已因真实 Browse 回归撤回。
- **GDI Render**：TEST 12、14、17、19、20、26-33，覆盖 WM Imaging、SVG path/cache/fallback/fill-rule、CSS background-image、原生 SVG text、缓存 SVG gradient/text 与线性渐变坐标，离线；TEST26-32 已真机通过，TEST33 待设备。
- **Browse**：TEST 13，真实页面抓取 + 渲染，需要网络；HTTPS 走 mbedTLS verified，明文 HTTP 走 WinInet。

当前关键 smoke test：

- TEST 17：内置 NetSurf real layout + redraw 页面。预期看到深红 H1 及红色下边框、带边框的三色 flex 横排、带可见单元格边框的 2×2 table。
- TEST 13：打开 start page，点击 `Open example.com`，走真实 Browse 路径；页面内链接可继续点击导航，有限 3xx 与 http/https 切换已覆盖。

> ⚠ **跑 TEST 5 之前先把模拟器系统时钟设到当前**（开始 → 设置 → 系统 → Clock & Alarms）。WM6 Emulator 默认是 2005-2007 年某个时间，会让所有现役证书都看着像"尚未生效"。

---

## 已知限制 / 注意事项

- **熵源**：默认 `CryptGenRandom`（Phase 3 起）；CSP 不可用时自动退回 QPC+GetTickCount+tid/pid jitter，CTR-DRBG 兜底。
- **HTTP 限制**：单连接 `Connection: close`、无 keep-alive、无 gzip 解码、响应体 cap 1 MB；GET 已有有限 3xx follow，明文 `http://` 经 WinInet。
- **导航卡顿**：主文档 GET 已移到 worker，旧页在等待网络时可滚动，父窗口 common-control 进度条可见；HTML parse、外部 CSS/图片 fetch、style、layout 仍在 UI 提交阶段执行，大页面返回后仍可能短暂卡顿。真实进度、失败分支复测和完整资源事务仍待后续。
- **渲染限制**：TEST25-32 与 TEST13 fixture 已确认 SVG parse/draw/cache/fallback/fill-rule/网络链、CSS 单背景图、基础 SVG text 及连续线性渐变。TEST33 正在验证线性渐变坐标/变换；复杂 SVG text、径向渐变、background-size 和多层背景仍未完成。TEST23 浮动实现已因 Browse 回归撤回。完整范围见 [.agents/KNOWN_LIMITATIONS.md](.agents/KNOWN_LIMITATIONS.md)。
- **WM6 X 按钮 = 最小化不是关闭**。每次启动 test_host 前确认任务管理器没有遗留实例，否则 stage.bat 替换 exe 时会产生 image 不一致。
- **WMDC 桥会静默断**：host 待机 / 模拟器长跑后偶尔失联，表现是 `PTls_Connect` 拿到 `-0x004C [BIO: recv WSA=...]`。修法：重启 WMDC（任务栏 → 退出 → 重启）。**联网测试前先在 IE Mobile 打开 baidu 验证一遍**。
- **模拟器时钟**：跑 verified TLS 前必须校准（见上）。证书 notBefore/notAfter 都按 UTC 比对当前时间。

---

## License

代码本身 MIT。mbedTLS 是 Apache 2.0（详见 `mbedtls/LICENSE`，不入 git，跟随上游）。cJSON 是 MIT。

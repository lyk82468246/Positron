# Positron Current Handoff

更新时间：2026-07-07  
当前分支：`main`  
当前最新提交：`db97b95 Phase 4 (M7-table): port table.c + build BOX_TABLE - tables render on device`

## 项目目标

Positron 是面向 Windows Mobile 6 Professional / WinCE 5.02 / ARMV4I 的 Electron-like 轻量框架。核心原则是“给 WM6 打补丁”，不是重造系统：

- 现代 TLS 是 WM6 缺口，所以用 `positron_tls.dll` + mbedTLS 2.16.12。
- 现代 HTML/CSS 渲染是 IE Mobile 缺口，所以移植 NetSurf 3.11。
- WM6 已有且够用的能力优先复用：明文 HTTP 用 WinInet，绘图用 GDI，后续图片应优先考虑 WM Imaging API。

## 当前真实状态

Phase 1-3 已完成：

- `positron_tls.dll`：TLS 1.2，CA bundle，hostname/chain verify，`CryptGenRandom` 熵源。
- `positron_json.dll`：cJSON 包装。
- `positron_http.dll`：HTTP/HTTPS GET/POST，HTTPS 走 mbedTLS verified，明文 `http://` 走 WinInet，支持有限重定向。

Phase 4 当前已到 M7-table：

- NetSurf 底层库已在 VS2008 / WinCE / ARMV4I / C89-only 下编译通过：
  `positron_netsurf`、`positron_hubbub`、`positron_libdom`、`positron_libcss`。
- `positron_core.dll` 是正式引擎边界，公开 `PCore_*` opaque-HANDLE API。
- Browse 正式路径已经切到 NetSurf 真实引擎：
  `PCore_LayoutDocument` / `PCore_PaintDocument` / `PCore_LinkAt` 走 `pcore_box_construct` -> NetSurf `layout_document` -> `html_redraw` -> GDI plotter。
- 旧的手写 block/inline layout + paint 已退休。
- `layout_flex.c` 已移植并真机验证，TEST 17 三色块横排。
- `table.c` 已移植，`pcore_construct_table` 生成 `BOX_TABLE > ROW_GROUP > ROW > CELL`，TEST 17 2x2 table 网格真机验证。

## 关键文件

- `positron_core/pcore_box.c`  
  DOM + computed style -> NetSurf `struct box`；含 flex/table 构建；正式 layout/paint/link hit-test。

- `positron_core/pcore_select.c`  
  libcss select handler、UA CSS、整树 computed style、外部 `<link rel=stylesheet>` 抓取入口。

- `positron_core/pcore_plot_gdi.c`  
  NetSurf plotter table + GDI 字体测量表。

- `positron_core/pcore_talloc.c`  
  精简 talloc 垫片。

- `positron_core/nsshim/`  
  拦截 NetSurf 头文件的 shim 层，支撑 `layout.c` / `redraw.c` / `layout_flex.c` / `table.c` 编译。

- `positron_core/pcore_layout_stubs.c`  
  仅保留未移植/未产生路径的链接桩。注意注释可能落后，看到 stub 前先确认真实源码和 vcproj。

- `test_host/main.c`  
  设备端唯一可靠测试 UI。没有 stdout，所有结果靠 MessageBox/window。

## 构建与运行

工具链：

- Visual Studio 2008 SP1
- Windows Mobile 6 Professional SDK
- ARMV4I
- C89-only，不能写 C99/C++11 风格代码

构建：

1. 打开 `Positron.sln`
2. 选择 `Debug | Windows Mobile 6 Professional SDK (ARMV4I)`
3. Rebuild whole Solution，尤其是改了静态库或 vendored NetSurf 源码时。

部署：

```cmd
scripts\stage.bat
```

复制到 `C:\WMShare\`，然后在模拟器/设备里从 `\Storage Card\test_host.exe` 启动。不要依赖 VS Smart Device Deploy。

## test_host 分组

启动时可选择：

- Communication：TEST 1-5，TLS/HTTP/JSON，需要网络。
- Engine：TEST 6-11、15、16，解析/选择/样式/layout/box tree，离线。
- GDI Render：TEST 14、17、12，离线窗口渲染。
- Browse：TEST 13，真实页面抓取 + 渲染，需要网络。

M7 当前最关键验证：

- TEST 17：内置 NetSurf real layout + redraw 页面。预期：深红 H1，三色 flex 横排，2x2 table 网格。
- TEST 13：Start page -> Open example.com -> 点击页面链接，走正式 Browse 路径。

## 当前限制 / 下一步

优先候选：

1. M5f：移植/接入 `redraw_border.c`，让 CSS border 真正绘制。当前 `pcore_layout_stubs.c` 里 border redraw 仍是 no-op。
2. 图片/SVG：`plot_bitmap` 仍是 stub，SVG logo 等不会显示。方向应优先看 WM Imaging API / NetSurf bitmap 接口，而不是从零写解码器。
3. CSS selector fidelity：`pcore_select.c` 里 attribute selectors 和 adjacent/general sibling selectors 仍是 stub；真实网页保真度会受影响。
4. table rowspan：当前 `pcore_construct_table` 对 rowspan 跨行占用是简化版，常见无 rowspan 表正常。
5. 文档同步：`README.md` / `PHASE4.md` 落后于 `main`，可单独做一次 docs refresh。

## 开发纪律

- 面向用户回复使用简体中文。
- 默认先查环境，再改代码。WMDC、僵尸 `test_host`、共享目录、旧二进制非常容易造成假故障。
- 改 TEST 时必须同步所有 MessageBox 文案、分组范围、最终 summary。
- 改 vendored NetSurf 源码时保持最小差异，C89 化要谨慎；`c89ize.py` 不能处理所有情况，特别是 designated initializers / static aggregate initializers。
- 不要引入 IE Mobile ActiveX 作为渲染层；渲染层方向是 OSS browser kernel port。


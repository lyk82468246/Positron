# Positron Current Handoff

更新时间：2026-07-10
当前分支：`main`  
当前最新提交：请以 `git log --oneline -5` 为准；Codex 接手后已刷新文档，接入 M5f border、CSS selector 补强、`<img>` fallback/fetch、文档级图片字节缓存与 WM Imaging 原生图片适配层，并把 TEST 11 扩展为 margin-collapse 正反样例。

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

Phase 4 当前已越过 M7-table，并进入 M5f border + selector 验证：

- NetSurf 底层库已在 VS2008 / WinCE / ARMV4I / C89-only 下编译通过：
  `positron_netsurf`、`positron_hubbub`、`positron_libdom`、`positron_libcss`。
- `positron_core.dll` 是正式引擎边界，公开 `PCore_*` opaque-HANDLE API。
- Browse 正式路径已经切到 NetSurf 真实引擎：
  `PCore_LayoutDocument` / `PCore_PaintDocument` / `PCore_LinkAt` 走 `pcore_box_construct` -> NetSurf `layout_document` -> `html_redraw` -> GDI plotter。
- 旧的手写 block/inline layout + paint 已退休。
- `layout_flex.c` 已移植并真机验证，TEST 17 三色块横排。
- `table.c` 已移植，`pcore_construct_table` 生成 `BOX_TABLE > ROW_GROUP > ROW > CELL`，TEST 17 2x2 table 网格真机验证。
- `redraw_border.c` 已接入源码和 `positron_core.vcproj`；`pcore_layout_stubs.c` 中 border no-op 已移除。2026-07-08 根据真实 VS2008 错误补齐 include 后，2026-07-10 已成功复编；TEST 17 真机可见 H1、flex、table/cell 边框并通过。
- `pcore_select.c` 已实现 CSS attribute selectors、adjacent/general sibling selectors、`:link` 与 `:lang()`；TEST 9 已于 2026-07-10 真机通过。
- TEST 11 原有 `body.y=8` / `p.y=24` 是旧手写布局器预期；NetSurf 折叠结果为 `body.y=p.y=16`。当前源码新增 `padding-top:1px` 阻断组，必须同时得到 `(16,16)` 与 `(8,25)` 才通过；2026-07-10 用户真机截图已确认 TEST 11 OK。
- TEST 18 的两个 `<img src>` 资源发现/fetch 已于 2026-07-10 真机通过；2026-07-11 已确认 document user-data 字节缓存与 URL 去重，二次扫描 fetch calls 保持 2。
- `pcore_wmimage.cpp` 是刻意新增的 C++ 小适配层：项目主体仍按 C89 编译，只有该文件 include WM6 SDK 的 C++ `imaging.h`，通过 `IImagingFactory::CreateImageFromBuffer` / `IImage::Draw` 暴露 C ABI 的 `PCore_ImageInfoFromMemory` / `PCore_DrawImageFromMemory`。内存 PNG 首次真机反馈为 decode fail；TEST 19 现改为 2x2 BMP 基线并输出 HRESULT 阶段码。第二次真机反馈为 `stage=2 hr=0x80070057`，已按 WM6 SDK `winx.h` 的约定把 COM init 改为 `COINIT_MULTITHREADED`；2026-07-10 BMP 基线已真机通过。2026-07-11 新增缓存 `<img>` 最小链：`box->object -> content_redraw -> plot_bitmap -> IImage::Draw`，TEST 20 已真机验证。

## 关键文件

- `positron_core/pcore_box.c`  
  DOM + computed style -> NetSurf `struct box`；含 flex/table 构建；正式 layout/paint/link hit-test。

- `positron_core/pcore_select.c`  
  libcss select handler、UA CSS、整树 computed style、外部 `<link rel=stylesheet>` 抓取入口；attribute/sibling/static-pseudo selector 源码已接入，动态状态伪类仍为 no-match。

- `positron_core/pcore_plot_gdi.c`  
  NetSurf plotter table + GDI 字体测量表。

- `positron_core/pcore_talloc.c`  
  精简 talloc 垫片。

- `positron_core/nsshim/`  
  拦截 NetSurf 头文件的 shim 层，支撑 `layout.c` / `redraw.c` / `layout_flex.c` / `table.c` 编译。

- `positron_core/pcore_layout_stubs.c`  
  仅保留未移植/未产生路径的链接桩。注意注释可能落后，看到 stub 前先确认真实源码和 vcproj。

- `test_host/main.c`  
  设备端唯一可靠测试 UI。没有 stdout，所有结果靠 MessageBox/window。TEST 19 是 WM Imaging 原生内存 BMP 解码/绘制基线；TEST 20 才验证缓存 `<img>` 已接入布局树和重绘链。

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
- Engine：TEST 6-11、15、16、18，解析/选择/样式/layout/box tree/image resource cache，离线。
- GDI Render：TEST 14、19、17、12，离线窗口渲染与 WM Imaging 原生图片绘制。
- Browse：TEST 13，真实页面抓取 + 渲染，需要网络。

当前最关键验证：

- TEST 17：内置 NetSurf real layout + redraw 页面。预期：深红 H1 和红色下边框、带边框的三色 flex 横排、2x2 table 可见 cell 边框。
- TEST 13：Start page -> Open example.com -> 点击页面链接，走正式 Browse 路径。

## 当前限制 / 下一步

优先候选：

1. ENGINE 回归：TEST 11 已由真机截图确认；新版 TEST 18 应显示 first/second 都为 `2/2` 且 fetch calls 为 2。
2. 图片/SVG：TEST 20 已确认 96x72 有边框的 2x2 BMP（red/green + blue/yellow）显示且无 fallback text；Browse host 已在 layout 前接入同一 fetch 流程。下一步是 PNG/JPEG/GIF 格式覆盖；当前 SVG logo/PNG/JPEG 仍不会通过 `<img>` 真实显示。
3. table rowspan：当前 `pcore_construct_table` 对 rowspan 跨行占用是简化版，常见无 rowspan 表正常。
4. 2026-07-11 TEST 13 的 IANA 截图显示左侧裁切：先验证新 TEST 21，再重跑 TEST 13。根因和修复在 `pcore_select.c` 的 `css_media.width/height`；若仍有越界，再检查具体 CSS 特性，而不是先改绘制 clip。

## 开发纪律

- 面向用户回复使用简体中文。
- 默认先查环境，再改代码。WMDC、僵尸 `test_host`、共享目录、旧二进制非常容易造成假故障。
- 改 TEST 时必须同步所有 MessageBox 文案、分组范围、最终 summary。
- 改 vendored NetSurf 源码时保持最小差异，C89 化要谨慎；`c89ize.py` 不能处理所有情况，特别是 designated initializers / static aggregate initializers。
- 如果新接入 NetSurf content-handler `.c` 后从 `html/private.h` 爆出 `dom_document` / `dom_node` / `bool` 之类连锁语法错，优先检查该 `.c` 的 include 区是否对齐 `layout.c` / `redraw.c`，不要先改 `private.h` 或让 `c89ize.py` 硬处理。
- 不要引入 IE Mobile ActiveX 作为渲染层；渲染层方向是 OSS browser kernel port。

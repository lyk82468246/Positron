# Positron Current Handoff

更新时间：2026-07-15
当前分支：`main`  
当前最新提交：请以 `git log --oneline -5` 为准；Codex 接手后已刷新文档，接入 M5f border、CSS selector 补强、`<img>` fallback/fetch、文档级图片字节缓存与 WM Imaging 原生图片适配层，并把 TEST 11 扩展为 margin-collapse 正反样例。

> **接手前先读**：`main` 产品源码已完整回退到 `9c5c7c7`/next37，且重编后的 core/host/http 文件尺寸与用户确认正常的 next37 一致。next37 后实验保存在远端 `codex/post-next37-experiments`，不得直接合回。冻结项、失败时间线和后续门槛见 `ROLLBACK_NEXT37.md`。

## 项目目标

Positron 是面向 Windows Mobile 6 Professional / WinCE 5.02 / ARMV4I 的现代基础设施集合，并在其上建设浏览器内核和 Electron-like 应用运行时。公共 DLL 必须能被其他 WM 程序独立调用，不能只按 test_host 或浏览器内部模块设计。完整分层见 `ARCHITECTURE.md`。核心原则是“给 WM6 打补丁”，不是重造系统：

- 现代 TLS 是 WM6 缺口，所以用 `positron_tls.dll` + mbedTLS 2.16.12。
- 现代 HTML/CSS 渲染是 IE Mobile 缺口，所以移植 NetSurf 3.11。
- WM6 已有且够用的能力优先复用：明文 HTTP 用 WinInet，绘图用 GDI，后续图片应优先考虑 WM Imaging API。
- WM6 缺少的成熟能力优先联网检索并移植许可证兼容的开源实现；只有平台胶水和 ABI 包装才优先自写。

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
- `layout_flex.c` 已移植并真机验证，TEST 17 三色块横排。2026-07-11 的 TEST 22 进一步确认 `row-reverse` 配合 25px leading padding 时主内容不会被推到 viewport 左侧；这只是该回归子例，不代表完整 flex 兼容。
- `table.c` 已移植，`pcore_construct_table` 生成 `BOX_TABLE > ROW_GROUP > ROW > CELL`，TEST 17 2x2 table 网格真机验证。
- `redraw_border.c` 已接入源码和 `positron_core.vcproj`；`pcore_layout_stubs.c` 中 border no-op 已移除。2026-07-08 根据真实 VS2008 错误补齐 include 后，2026-07-10 已成功复编；TEST 17 真机可见 H1、flex、table/cell 边框并通过。
- `pcore_select.c` 已实现 CSS attribute selectors、adjacent/general sibling selectors、`:link` 与 `:lang()`；TEST 9 已于 2026-07-10 真机通过。
- TEST 11 原有 `body.y=8` / `p.y=24` 是旧手写布局器预期；NetSurf 折叠结果为 `body.y=p.y=16`。当前源码新增 `padding-top:1px` 阻断组，必须同时得到 `(16,16)` 与 `(8,25)` 才通过；2026-07-10 用户真机截图已确认 TEST 11 OK。
- TEST 18 的两个 `<img src>` 资源发现/fetch 已于 2026-07-10 真机通过；2026-07-11 已确认 document user-data 字节缓存与 URL 去重，二次扫描 fetch calls 保持 2。
- `pcore_wmimage.cpp` 是刻意新增的 C++ 小适配层，主体仍为 C89。TEST19 的 BMP/PNG/JPEG/GIF 可见绘制已确认；TEST20 小点回归也已通过 `g_render_sheet` 修复，四格式缓存绘制与资源计数 4/4 已由设备确认。
- TEST 21 已确认 `css_media.width/height` 采用实际 client viewport；2026-07-11 用户又确认整数像素 MQ4 `(width <= Npx)` / `(width < Npx)` 的 320/300/299px 边界通过。随后 TEST13 方框在空白折叠修复后消失且词间距正常；补齐 NetSurf 上游 `<pre>` UA 默认后，TEST15 已确认 `normal_ws=ok pre_lf=kept`。页脚/导航拥挤仍未解决。TEST24 的滚动比例断言及真实 TEST13 横竖屏同区域保持均已确认。

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

1. 首选运行 `scripts\build.bat`；默认执行 `Debug` 增量 `Build`。改工程依赖、生成规则或需要干净基线时显式运行 `scripts\build.bat Debug rebuild`。
2. 可用 `scripts\build.bat Debug build` 做增量构建，或用第二参数 `clean` 清理。`scripts\stage.bat` 也会在复制前自动调用同配置的增量 Build；构建失败时不复制，避免新 EXE 搭配旧 DLL。
3. 脚本调用 `Common7\IDE\devenv.com`，不是直接调用 `VC\ce\bin\x86_arm\cl.exe`；前者负责 `.sln` 工程依赖和完整 WM6 平台设置。
4. GUI 等价操作是打开 `Positron.sln`，选择 `Debug | Windows Mobile 6 Professional SDK (ARMV4I)` 后 Rebuild whole Solution。

2026-07-11 已由 Codex 在本机通过该脚本完整重建：9 个工程成功、0 个失败；随后增量构建也成功并报告 9 个工程均为最新。根目录 `vs2008-build.log` 保存最近一次调用的输出（已忽略，不入 git）。

部署：

```cmd
scripts\stage.bat
```

复制到 `C:\WMShare\`，然后在模拟器/设备里从 `\Storage Card\test_host.exe` 启动。不要依赖 VS Smart Device Deploy。
若旧 `test_host.exe` 锁住默认目录，可先真正关闭旧实例，再运行 `scripts\stage.bat Debug C:\WMShare\Positron-next` 隔离新二进制；不要把不同构建的 EXE/DLL 混在同一运行目录。

## test_host 分组

启动时可选择：

- 快速配置：`test_host.exe` 同目录的 `test_host.ini` 使用 `tests=13,17,41,42,46,47,48`、`tests=1-5 7b` 一类语法。读取成功后 Yes 只跑这些编号，No 回到原四组路由；缺失/无效不会静默改变测试范围。TEST23 不可选。`stage.bat` 会复制仓库默认配置，当前为 `tests=13,17,41,42,46,47,48,49,50`。四组路由的第 3 组提示已压缩为能力类别，避免 WM6 小屏 MessageBox 被长清单撑坏。

- Communication：TEST 1-5，TLS/HTTP/JSON，需要网络。
- Engine：TEST 6-11、15、16、18、21、22、24、25、38、40-45，解析/选择/样式/layout/box tree/image resource cache、responsive media viewport、reverse flex、cached CSS restyle、SVG parse、受约束的 `:root` token、数值型 OKLCH/可求值 calc、grid-overflow 隔离、overflow scrollbar、分阶段资源事务、失败回滚与 CSS import tree，离线。TEST40-45 已真机确认。TEST23 浮动最小样例已因真实 Browse 回归撤回，不运行。
- GDI Render：TEST 12、14、17、19、20、26-37、39、46-50，离线窗口渲染、WM Imaging 位图、SVG path/cache/fallback/fill-rule、CSS background-image、原生 GDI text、线性/径向渐变、继承/透明 stop、缓存复用、IANA token 间距、table span/匿名归一化、列表 marker/counter/image 与随包静态字体 fallback 正式 redraw；TEST48/49 已验收，TEST50/next61 待设备验收。
- Browse：TEST 13，真实页面抓取 + 渲染，需要网络。

当前最关键验证：

- TEST 17：内置 NetSurf real layout + redraw 页面。预期：深红 H1 和红色下边框、带边框的三色 flex 横排、2x2 table 可见 cell 边框。
- TEST 13：Start page -> Open example.com -> 点击页面链接，走正式 Browse 路径。

## 当前限制 / 下一步

优先候选：

1. 2026-07-11 用户真机确认 ENGINE 原整组至 TEST24 通过；2026-07-12 单独确认 TEST25 SVG parse。后续修改引擎路径时必须重跑当前整组。
2. TEST23 的浮动构盒最小复现虽通过，但真实 Browse 严重回归，已撤回。最新 TEST13 已作为当前 IANA 可读基线通过，但这只表示本轮未发现回归，不代表现代 CSS 或任意真实页完整兼容。后续 float 必须对照上游 box construction/normalisation，而不是基于该简化测试继续扩展。
3. `WM_SIZE` 从 document-owned 外链 CSS 缓存 restyle + layout，且使用 cache-only callback。TEST24 与真实 Browse 旋转均已确认。
4. 主文档、外链 CSS、CSS `@import`、`<img>` 和 CSS 背景 GET 已组成分阶段 worker 事务；DOM/style/layout 仍只在 UI。2026-07-14 设备已确认 TEST3 的真实单响应正文进度、TEST43 资源事务、TEST44 主文档失败回滚和 TEST13 IANA Browse 基线。UI 提交现由一次性 WM timer 拆成 parse/style/image-discovery/layout 四段，单个 NetSurf 调用仍可能卡顿。宿主暂存预算为 64 URL/2 MiB，不是 core ABI 限制；整页聚合进度、网页字体和脚本仍待处理。
5. TEST30-37 已于 2026-07-13 真机通过：CSS 背景、基础 text、连续线性/径向渐变及坐标、继承/透明 stop、循环保护、径向 SVG 文档缓存以及 `<img>`/CSS 背景单次 fetch 复用均成立；复杂 shaping、`textPath`、逐字定位、任意 shear、径向焦点与 spread method 尚未实现。
6. 当前 IANA 线上 CSS `iana_website.80c103cc08b6.css` 使用 custom properties、媒体查询范围语法、`oklch()`、`calc()`、grid/gap 与 `:has()`。整数像素媒体范围、同表顶层 `:root` token、TEST40 的数值型 OKLCH/可求值 calc、TEST41 的 `/numbers` 宽度隔离及 TEST42 的 NetSurf overflow scrollbar 均已真机确认；不要扩大表述为完整 MQ4、custom-properties、CSS Color/Values、Grid、触摸惯性或 overlay scrollbar 支持。
7. 图片/SVG：TEST20 四格式缓存 `<img>`、TEST25-37 的 SVG 正式链，以及 TEST13 网络相对 SVG 链均已真机确认。公共 `positron_image.dll` 已覆盖 retained WM 位图/SVG、旧 core 转发、PNG/JPEG/BMP/GIF 编码与静态 libjpeg-turbo 4:4:4。next51 已确认 ABI 1.4 启动前 BMP/GIF 编码、签名、回读检查和六项视觉均正常，但标题栏 X 仍按 WM 约定 Smart Minimize，说明仅处理 `WM_CLOSE` 不足。next52 改用 `SHDoneButton(SHDB_SHOW)` 的原生标题栏 OK，并处理 `WM_COMMAND/IDOK`；用户已确认进程真退出且可再次启动。按用户要求不增加左右软键。复杂文本、径向焦点、spread method、background-size 和多层背景仍是显式缺口。
8. 测试节奏：默认按能力批次积累多个相关实现、自动断言和直绘/正式链回归，再用一个 `test_host.ini` 一次交付多个 TEST。除编译错误、高风险回归定位或设备专有故障外，不应为每个微小改动单独要求用户验收。
9. `PCore_StyleDocumentEx2` 是旧样式 ABI 的兼容扩展：接收绝对 document URL 和宿主 URL resolver；core 用 libcss 原生 pending/register 机制处理最多 16 层导入，失败导入注册空表。WM 宿主使用 `InternetCombineUrlA`，旋转只读 document CSS cache。TEST45 已真机确认嵌套、缺失回退、URL 规范化和缓存重选。
10. 表格构盒已移植 NetSurf 3.11 的 span occupancy；next53 已确认 TEST46 的有限/自动 rowspan、colspan、row-group 边界、像素与正式 redraw。当时尚缺的匿名包装与空 cell 已进入下方 next56 批次；完整 border-collapse 仍未完成。
11. next54 的宿主纵条和 auto-height 横条空间有效，但第二次整树 layout 误改 fixed-height overflow 几何，导致 TEST42 原右箭头点击位置失败；右箭头图形还相对 16px 控件向下偏 2px。该包不能作为新基线。
12. next55 在二次 layout 前屏蔽 fixed-height `overflow:auto` 的首轮横向 extent，只让 auto-height 容器获得额外空间；右箭头改用与左箭头对称的 `area.y0` 基准。用户已确认 TEST41/42、短页纵条与色块页均正常；冻结的 TEST13 导航链未改。
13. next56 按 NetSurf 3.11 规则补 table/row-group/row/cell 匿名盒和短行空 cell。用户已确认 TEST47 红/白、绿/蓝两行及同批其余测试正常。
14. next57 移植 NetSurf 列表 marker 构造并恢复 LI DOM user-data 映射。TEST48 自动校验 disc/circle/square、十进制 `start/value/reversed` 及 marker 几何；`PCore_ListMarker` 是只读诊断 API。
15. next58 引入 Noto OFL 来源的静态 Positron Symbols/Emoji 子集。next59 追加官方 hinted Noto Sans Symbols Basic 子集，用生成的精确 cmap 覆盖表互补两套 symbol face；设备确认箭头不再 tofu、marker 和五个 emoji 均可见且视觉稍有改善。`ANTIALIASED_QUALITY` 最终效果仍取决于 OEM GDI；不要宣称网页 `@font-face`、复杂 emoji shaping 或彩色字体支持。
16. next60 用生成的 `positron_format_list_style.c` 替换 decimal-only stub，算法与 47 种样式来自仓库内原版 libcss。`scripts/port_list_style_vs2008.py` 负责指定初始化器和 UTF-8 字面量的可重复 C89/ASCII 转换；`c89ize.py` 已修复 aggregate 字段被错误重排的规则。`list-style-image` 复用 document image cache，只有 computed list-item 才发现资源，解码失败保留类型 marker。next60 首次设备 TEST50 的 `found=4 fetched=2` 来自打包事故：Debug `pcore_select.obj` 早于最终源码，而 staging 仍复制了旧 core DLL；Release 与仓库源码已有 list-item gate。next61 必须用自动增量构建后的 Debug 包复测，不得放宽 TEST50 断言；也不要把它扩大为自定义 counter-style、所有语言字体或 `list-style-position:inside`。

## 开发纪律

- 面向用户回复使用简体中文。
- 默认先查环境，再改代码。WMDC、僵尸 `test_host`、共享目录、旧二进制非常容易造成假故障。
- 改 TEST 时必须同步所有 MessageBox 文案、分组范围、最终 summary。
- 改 vendored NetSurf 源码时保持最小差异，C89 化要谨慎；`c89ize.py` 不能处理所有情况，特别是 designated initializers / static aggregate initializers。
- 如果新接入 NetSurf content-handler `.c` 后从 `html/private.h` 爆出 `dom_document` / `dom_node` / `bool` 之类连锁语法错，优先检查该 `.c` 的 include 区是否对齐 `layout.c` / `redraw.c`，不要先改 `private.h` 或让 `c89ize.py` 硬处理。
- 不要引入 IE Mobile ActiveX 作为渲染层；渲染层方向是 OSS browser kernel port。

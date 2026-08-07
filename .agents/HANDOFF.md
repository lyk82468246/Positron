# Positron Current Handoff

更新时间：2026-08-07
当前分支：`main`  
当前设备基线：next134 已在 `screen=240x320 dpi=96` 设备日志中通过 TEST13/20/27/43/44/56/58-77/80-99；next135 新增 TEST100-104 的 `minlength`/`maxlength` 表单约束，next136 隔离 TEST59 的 CSS 参考上下文，next137 修正非整数 DPI 的 `libcss` 设备像素换算，均已完成 ARMV4I 构建与 staging，设备验收待进行。next123/124/125/126/127/134/135 的 `positron_script.dll` 与表单扩展仍不接入冻结的浏览器 JS 路径。next123 以来 Browse 宿主使用物理像素/CSS 视口分离，并按设备报告的 DPI 换算；96 DPI 只是某次设备日志，不是产品固定值。next114 建立外部 `<script src>` 的 transport-agnostic 发现/抓取/document 缓存 ABI，但没有执行 JavaScript。next115 与 next116 的 float 候选均已因 TEST79 失败和 TEST13 视觉回归否决，代码与默认配置已恢复 next114；Float 方向暂挂。`pattern`、类型/范围 validity、`:visited/:target/:indeterminate`、专用事件数据、完整 HTML activation 和浏览器 JS binding 仍未实现；真实触屏与视觉仍待累计人工检查。**next78 仍是已撤回的失败实验，不得使用**。

失败/暂挂总索引见 [`FAILED_EXPERIMENTS.md`](./FAILED_EXPERIMENTS.md)。接手时先查该索引，再查本文件的当前基线；不要只依据某个旧包里的自动 `OK`。

**状态更正（next132，2026-08-07）**：next131 在 `screen=320x320 dpi=128` 下的 TEST13
三段导航均完成，但 TEST20 动态 DPI 断言实际得到 `48x48`，期望 `64x64` device px，
因此不能把 next131 记为该设备的全通过。next132 已将设备视口决定和单位上下文快照
提前到正式构盒之前，并在 TEST20/27 的样式完成后重新绑定同一设备视口；VS2008
`Debug|Windows Mobile 6 Professional SDK (ARMV4I)` 增量构建已通过，待同设备复测。

**状态更正（next134，2026-08-07）**：next132 在 `screen=480x640 dpi=192` 下的
TEST13/20/27/43/44/56 均通过，但 TEST58 的离线几何段继承了前一个渲染测试留下的
设备视口待布局状态，得到 `article=320 rows=40/80/40`，而不是旧的 96 DPI 期望。
next133 曾用固定 230x260/96 DPI 隔离该段；因不符合项目的设备自适应原则，next134
已改为让 TEST58 直接读取运行时屏幕宽高和设备 DPI，并按 CSS 96 DPI 规范基准换算
物理断言；最终可见布局同样使用真实设备视口。ARMV4I 增量构建和 staging 已通过，
随后在 `screen=240x320 dpi=96` 设备日志中确认通过。该修复不放宽断言，也不修改布局引擎。

**next135（2026-08-07）**：表单校验新增 `PCORE_VALIDITY_TOO_SHORT` 与
`PCORE_VALIDITY_TOO_LONG`。text/password/textarea 控件读取有效的 HTML 非负整数
`minlength`/`maxlength`，按 UTF-8 字符数生成约束结果；required、disabled、readonly、
提交阻断和首个无效控件几何保持既有语义，坏属性保守忽略。TEST100-104 覆盖静态
边界、动态 native EDIT 更新、textarea、豁免项和首个长度错误；ARMV4I 增量构建与
`C:\WMShare\Positron-next135` staging 已通过，设备 testbench 尚未验收。

**next136（2026-08-07）**：设备日志在 `screen=480x640 dpi=192` 下于 TEST59 停止：
`width=224 main=(50,50) 124x77; expect x=25 w=174`。该测试是显式 CSS 像素几何
夹具，却继承了前一个设备-backed render 的 192 DPI 单位上下文；next136 在每个
离线 pass 前安装 `PCore_SetViewport(width,240,96)`，结束后恢复运行时设备视口。
这不是 core flex 回归，也没有放宽断言；ARMV4I 增量构建与
`C:\WMShare\Positron-next136` staging 已通过，设备复测待进行。

**next137（2026-08-07）**：next136 包在 `screen=320x320 dpi=128` 下记录为
`TEST13 PASS -> TEST20 FAIL: first box=48x48; expect 64x64 device px`。这次不是
DLL 混装或 TEST20 断言隔离：`libcss/src/select/unit.c` 的通用
`css_unit_len2device_px` 先把每 CSS 单位的 `1.333` 比例截成 `1`，再乘长度，导致
所有非整数 DPI 比例的尺寸丢失。next137 保留分数比例，完成整段长度换算后才按最终
设备像素取整；没有改成 48、没有固定 DPI，也没有修改 TEST20 断言。ARMV4I 增量
构建成功（libcss 仅保留既有 3 条 fpmath 警告），包为
`C:\WMShare\Positron-next137`，设备复测待进行。

2026-08-07 的 next126 设备日志记录为 `screen=320x320 dpi=128`：TEST13 的
`example.com`、IANA Example Domains、IANA Reserved Domains 三段导航均完成，随后
TEST20 停止。TEST20 的失败是断言隔离错误：它走显式 CSS 视口的离线缓存图片路径，
却按设备 DPI 计算 `48px` 的期望物理尺寸；实际盒为 `48x48`。next127 将该测试固定
到 96 DPI CSS 视口并修正失败诊断字段；这不等于高 DPI Browse 已验收，下一轮仍需
在不同分辨率/DPI 下记录日志并人工检查 TEST13。

设备验收记录要求：每次人工批次至少保留 `test_host.log` 开头的
`screen=宽x高 dpi=值`，并尽量轮换一个纵横方向或分辨率、一个 DPI 档位。自动断言
只证明对应代码路径和资源计数，不替代高 DPI 下的 Browse 版式、滚动、链接和旋转
截图检查。

2026-08-07 的 next127 设备日志记录为 `screen=240x320 dpi=96`：TEST13 的三段
导航、TEST20/27、ENGINE/表单回归以及 TEST80-96 均通过。TEST97 停止原因是测试
要求错误文本包含大写 `JSON`，而 Duktape 实际返回 `SyntaxError: invalid json ...`；
这不是 JSON 注入或上下文恢复失败。next128 只放宽该测试对引擎错误文本大小写的
耦合，仍要求 `PSCRIPT_ERROR_JSON` 和非空诊断，设备结果待补。

next128 的设备日志随后在 `screen=240x240 dpi=96` 下完成 `TESTBENCH PASS`，覆盖
TEST13、TEST20、TEST27、TEST43-99。next129 将 TEST20 从临时的 96 DPI 显式 CSS
视口改回真实 `PCore_SetDeviceViewport` 路径：48 CSS px 的期望值按当前设备 DPI
换算为物理像素；96 只保留为 CSS 规范的参考基准，不再作为设备 DPI 强制值。

next129 在 `screen=480x640 dpi=192` 下确认 TEST13 与 TEST20 的动态换算通过；
TEST27 暴露同一类旧断言，要求 `120x60` CSS SVG 盒却直接比较设备像素，实际为
`240x120`。next130 已让 TEST27 在 style/layout 前安装设备视口，按 DPI 检查盒尺寸，
并按物理坐标采样 SVG 色块；非 96 DPI 的设备回归待继续验证。

next130 随后的日志为 `screen=480x480 dpi=192`：TEST13/20/27/43/44 通过，TEST56
报告 `70/70/70` 与 `sum=210`。这是离线 TEST56 几何段继承 192 DPI 后把 105 CSS px
换算为 210 设备 px，并非 table 算法回归。next131 已把该段显式设为 96 DPI CSS
契约，同时让 TEST56 可见渲染段使用设备视口；58-99 的设备结果待补。

next137 继续保留 TEST20/27 的严格物理尺寸断言，不固定设备 DPI；设备复测时仍要先
记录日志头部的 screen/DPI，并确认运行目录中的 core/libcss 版本来自同一个 staging 包，
再判断是否是设备加载旧 DLL。

> **接手前先读**：导航路径以用户确认正常的 `9c5c7c7`/next37 为冻结起点，此后 `main` 已继续叠加图片、字体、列表和表格能力。next37 后那组失败的导航实验保存在远端 `codex/post-next37-experiments`，不得直接合回；这不表示当前整个仓库仍停在 next37。冻结项、失败时间线和后续门槛见 `ROLLBACK_NEXT37.md`。

## 项目目标

Positron 是面向 Windows Mobile 6 Professional / WinCE 5.02 / ARMV4I 的现代基础设施集合，并在其上建设浏览器内核和 Electron-like 应用运行时。公共 DLL 必须能被其他 WM 程序独立调用，不能只按 test_host 或浏览器内部模块设计。完整分层见 `ARCHITECTURE.md`。核心原则是“给 WM6 打补丁”，不是重造系统：

- 现代 TLS 是 WM6 缺口，所以用 `positron_tls.dll` + mbedTLS 2.16.12。
- 现代 HTML/CSS 渲染是 IE Mobile 缺口，所以移植 NetSurf 3.11。
- WM6 已有且够用的能力优先复用：明文 HTTP 用 WinInet，绘图用 GDI，后续图片应优先考虑 WM Imaging API。
- WM6 缺少的成熟能力优先联网检索并移植许可证兼容的开源实现；只有平台胶水和 ABI 包装才优先自写。

源码依赖现已自包含：NetSurf、mbedTLS、cJSON、Expat、libjpeg-turbo、NanoSVG、Duktape 与 Noto 字体都固定版本并随仓库提供。新环境只需另行安装不可再分发的 VS2008 SP1、WM6 Professional SDK 与模拟器；运行 `python scripts\audit_repo.py` 可检查 15 个工程引用、版本和许可证。第三方边界见根目录 `THIRD_PARTY.md`。

## 当前真实状态

Phase 1-3 已完成：

- `positron_tls.dll`：TLS 1.2，CA bundle，hostname/chain verify，`CryptGenRandom` 熵源。
- `positron_json.dll`：cJSON 包装。
- `positron_http.dll`：HTTP/HTTPS GET/POST，HTTPS 走 mbedTLS verified，明文 `http://` 走 WinInet，支持有限重定向。

Phase 4 已越过 M7-table 和 M5f border/selector，并推进到 TEST57 的百分比 table-row 高度验收：

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
  libcss select handler、UA CSS、整树 computed style、外部 `<link rel=stylesheet>` 抓取入口；attribute/sibling/static-pseudo selector 已接入，next109 又让 live `:checked/:enabled/:disabled` 与宿主馈送的 `:focus/:active` 参与正式重样式。

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

- 快速配置：`test_host.exe` 同目录的 `test_host.ini` 当前恢复为 next114 浏览器基线，并追加独立脚本 DLL 的 TEST80-99 与 next135 的 TEST100-104，使用 `tests=13,20,27,43,44,56,58-77,80-104`；next134 已由设备确认 TEST84-99，通过 next135 的 TEST100-104 仍待设备验收。TEST79/float 候选已撤回，next121 的 TEST83 已由设备确认通过。自动日志会在开头写入 screen/DPI；若 TEST20 仍显示 48 CSS px 被换算成异常物理尺寸，先记录设备指标，不要放宽断言。也支持 `tests=1-5 7b` 一类语法。`auto=1` 时不弹 Yes/No/OK，窗口首帧后自动关闭，TEST13 自动跑 example.com → IANA Example Domains → Reserved Domains，并把每个原始结果和逐页遥测覆盖写入同目录 `test_host.log`；`auto=0` 保留 Yes/No 与原四组路由。自动首帧冒烟不替代新视觉能力的人工截图验收。缺失/无效配置不会静默改变测试范围，TEST23/78/79 不可选。

- Communication：TEST 1-5，TLS/HTTP/JSON，需要网络。
- Engine：TEST 6-11、15、16、18、21、22、24、25、38、40-45、59-61、74-77，解析/选择/样式/layout/box tree/image resource cache、responsive media viewport、reverse flex、cached CSS restyle、SVG parse、受约束的 `:root` token、数值型 OKLCH/可求值 calc、grid/overflow min-content 隔离、overflow scrollbar、分阶段资源事务、失败回滚、CSS import tree、selector node-data restyle、具名 NetSurf option 默认、DOM Event 传播/取消、基础 relative/absolute positioning、动态 `:hover` 与脚本资源发现/缓存 ABI，离线。TEST40-45、59、60、74-77 已真机确认；next78 扩展测试及其 core 行为已经撤回。TEST23/79 浮动候选均因真实 Browse/设备回归撤回，不运行。
- GDI Render：TEST 12、14、17、19、20、26-37、39、46-58、62-73，离线窗口渲染、WM Imaging 位图、SVG path/cache/fallback/fill-rule、CSS background-image、原生 GDI text、线性/径向渐变、同文档及重叠文档缓存复用、table/list、HTML inline author CSS、普通表单、multipart/file、WM multiple select、首批 required 验证与动态表单伪类；TEST65-73 已验收。
- Browse：TEST 13，真实页面抓取 + 渲染，需要网络。

当前最关键验证：

- TEST 17：内置 NetSurf real layout + redraw 页面。预期：深红 H1 和红色下边框、带边框的三色 flex 横排、2x2 table 可见 cell 边框。
- TEST 56：显式 table height 分配。预期：105px 三行等高且文字依次 top/middle/bottom；70px 两行等高且橙色 rowspan 文字在底部；页面无多余纵向滚动条。
- TEST57：第一张 80px 表应约为 20/40/20px，第二张 50px 超约束表应为 25/25px；next73 已连同 TEST55/56 一起通过。
- TEST59：分别在 224px 和 320px viewport 建立无 Grid `overflow:auto` 宽表格夹具，必须保持 reversed-flex main 的 `x=25,width=viewport-50`。next78 的同 DOM 旋转/scrollbar 诊断版本已经撤回。
- TEST60：同一 DOM 先按 224×320、再按 400×240 重做 style/layout；IANA 同型 `.dtable` 的首个 `<th>` 必须保留 18px/10px inset，并与第二个同文字表头保持同一粗体宽度。它同时覆盖 `thead th`、`:first-child` 和后续 `tbody > tr:first-child > th` 选择器。
- TEST61：正式 NetSurf layout 中，同一串文本的 `1px` 与 `8.5pt` 必须测得相同宽度，证明 `font_min_size=85` 生效；`12pt` 控制组必须更宽。JavaScript 策略继续为 false。
- TEST80：不初始化 `positron_core`，直接加载独立 `positron_script.dll`，验证 Duktape 求值、持久上下文、错误恢复、内存计数和执行计数；不代表浏览器 DOM/window/network binding 已实现。设备日志已确认通过。
- TEST81：不初始化 `positron_core`，在独立脚本上下文中验证 50 ms 执行预算能打断无限循环、超过 `PSCRIPT_MAX_SOURCE_BYTES` 的源码被拒绝，以及拒绝/超时后仍能求值 `42`；这是 timeout/source-boundary/recovery 断言，不是完整内存配额或浏览器 JS 验收。next119 设备日志已确认通过。
- TEST82：不初始化 `positron_core`，以 `PScript_CreateEx` 建立 512 KiB Duktape heap 上限，执行短生命周期数组压力，要求返回 `PSCRIPT_ERROR_MEMORY_LIMIT`、峰值不超过上限，并在失败后求值 `42`；这是 runtime heap 边界，不是浏览器 JS 或模块生命周期验收。next120 设备日志已确认通过，峰值为 496184/524288。
- TEST83：不初始化 `positron_core`，以 `PScript_EvaluateModule` 验证 CommonJS 风格模块一次执行缓存、`require()`、失败条目回滚、`PScript_ClearModules` 和清空后的重新加载；没有 URL、文件、网络、DOM 或 window 解析。next121 ARMV4I Debug/Release 构建、staging 与设备日志均已确认通过。
- TEST84：不初始化 `positron_core`，以 `PScript_SetModuleSourceProvider`/`PScript_LoadModule` 验证宿主按名提供根模块和 `require()` 依赖、缓存命中不重复回调、provider 失败、执行失败回滚、buffer 释放和清空后的重新取源；没有 URL、文件、网络、DOM 或 window 解析。next134 的 `screen=240x320 dpi=96` 设备日志已确认通过。
- TEST85-89：不初始化 `positron_core`，以 ABI 1.4 的 primitive global setter、JSON getter、JSON-array function call、跨调用状态、错误恢复、非法全局名与 255 字节结果上限做五项断言；没有 URL、文件、网络、DOM 或 window 解析。next134 的 `screen=240x320 dpi=96` 设备日志已确认通过。
- TEST90-94：不初始化 `positron_core`，以 ABI 1.5 的同步 JSON 宿主回调注册、compact JSON 参数/返回值、失败恢复、同名替换/注销和固定 16 槽上限做五项断言；回调不能重入或异步持有上下文，结果最多 255 字节有效载荷；没有 URL、文件、网络、DOM 或 window 解析。next134 的 `screen=240x320 dpi=96` 设备日志已确认通过。
- TEST95-99：不初始化 `positron_core`，以 ABI 1.6 的 `PScript_SetGlobalJson` 注入 object/array/string/number/boolean/null，覆盖跨调用 mutation、malformed JSON 恢复、64 KiB 输入上限原值保留和类型替换；没有 URL、文件、网络、DOM 或 window 解析。next134 的 `screen=240x320 dpi=96` 设备日志已确认通过。
- TEST62：四个离屏探针确认 checkbox/radio 均采用 1em 几何，最终 gadget 的 checked 状态为 0/1，选中状态增加像素暗度，hidden input 不生成 box；它是静态 redraw 基线。
- TEST64：按盒树坐标执行 checkbox 切换、disabled 点击、同表单同名 radio 互斥、跨组/跨表单隔离和已选项幂等，再从 240×320 重排到 320×240 并复核 DOM 状态；next93 自动设备日志已通过。
- TEST13 next86 遥测：关闭 Browse 窗口后，既有 OK 框显示最后一次导航的 total/network/max-UI、parse/style/images/layout/paint、资源 queued/ok/fail、worker rounds、document/cache bytes 和 budget-rejected。style/images 是多轮累计，max-UI 才是单次消息循环最长阻塞。
- next87 在同一 OK 框追加 core layout 的 box/first/settle/final/other 与 settling pass。`PCore_GetLayoutStats` 只复制每个 document 最近一次布局统计；未改变构盒、两轮布局判定、几何或重绘。设备已确认两类真实页面的构盒均约 500ms；该结论只确定下一步细分方向，不代表卡顿已经优化。
- next88 新增独立 `PCoreBoxStats`/`PCore_GetBoxStats`，避免扩展 next87 已公开结构的大小。tree/backgrounds 互不重叠；tree 内 style/text/image/anonymous/table-normalise 互不重叠，`other` 为剩余 DOM 遍历、分支与分配时间。逐调用 `GetTickCount` 有轻微诊断开销，比较分布优先于比较 next87 的绝对毫秒。
- next88 设备数据已把两页热点缩到单张图片创建（518/474ms）。next89 用现有 `positron_image.dll` 做 XML-like 字节 SVG-first，避免先让 WM Imaging 失败；同一 document 的二次 layout 借用 image cache retained handle。TEST20/27 已通过 4/4、1/1 reuse，TEST27 也通过首次 markup-first。它不提供跨导航或跨线程句柄共享。
- TEST13 关闭后依次显示两个短框：`overview` 与 `box detail`。后者的 `image reuse/markup-first` 用于区分重排复用和首次 SVG 分派；两个概念不能相互代替。
- TEST13 显示的是导航完成快照；后续旋转布局不会回写 `g_nav_last_stats`。不要用旋转后弹窗的 reuse 值判断旋转路径，复用门禁在 TEST20/27。
- next90 将 `positron_image` ABI minor 提到 1.5，新增按 SVG handle 查询 total/setup/parse/raster；core 用独立 `PCoreImageDecodeStats` 汇总，不扩大 `PCoreBoxStats`。TEST27 与 TEST13 只读显示该数据，不改变创建、layout 或 redraw。
- next91 在 next90 只读候选上增加可选无人值守 testbench；不新增旁路测试实现，仍调用原 TEST 函数、公共 WndProc 和导航事务。失败保持 fail-fast，并以非零进程返回值及 `test_host.log` 的 `TESTBENCH FAIL` 收尾。
- next92 只共享同时存活文档的 SVG：键包含 URL、长度和两种 32 位内容哈希，document cache 各持一份引用，最后一个引用释放时立即销毁句柄。它不缓存空闲对象、不跨线程，也不改变位图所有权。TEST63 覆盖第二文档复用、首文档释放及后续像素绘制。
- next93 的 form 激活入口使用 document CSS px，与 `PCore_LinkAt` 相同；宿主先处理 overflow scrollbar，再处理 form control，最后才处理链接/空白关闭。控件状态同步回 libdom，因而 `WM_SIZE` 重建盒树后仍保留。next94 接通单行 text/password；next97 复用同一枚举、销毁、滚动、旋转和 `EN_CHANGE` 路径接入 textarea。next98 接入单选 `COMBOBOX`；next104 在同一生命周期中为 multiple 创建原生 `LISTBOX`，用 `LB_GETSEL/LB_SETSEL` 同步每项并回滚 disabled option。不要重写 radio 分组或既有输入同步逻辑。
- TEST 13：Start page -> Open example.com -> 点击页面链接，走正式 Browse 路径。

## 当前限制 / 下一步

优先候选：

> 2026-08-04：先解决“有没有”，再解决已有小范围“好不好”。next111/TEST75 已完成并验收 basic relative/absolute positioning，next113/TEST76 又完成并验收基础动态 `:hover`，next114/TEST77 的脚本资源接口已通过设备门禁；next115/116 的 TEST79 和真实 TEST13 均失败，Float 方向暂挂并恢复 next114。下一批评估显式 JavaScript 开关、基础 Grid 或背景尺寸等高价值缺口，继续每批保留 TEST13 深链/旋转门禁。高级约束验证与专用事件数据随后扩展。已有 NetSurf Duktape backend 的 JavaScript 最小纵切保持中期。首屏 SVG 冷解析、抗锯齿、渐变高级参数、视觉微调和全面性能优化后置，除非它们造成崩溃、数据错误或阻塞基本操作。

1. 2026-07-11 用户真机确认 ENGINE 原整组至 TEST24 通过；2026-07-12 单独确认 TEST25 SVG parse。后续修改引擎路径时必须重跑当前整组。
2. TEST23 的浮动构盒最小复现虽通过，但真实 Browse 严重回归，已撤回。next115/116 的 TEST79 和 TEST13 也均失败，float 代码、配置和 ENGINE 接入已恢复到 next114。TEST13 起始页正常不等于所有 IANA 子页正常；若未来重启 float，必须先完成上游 box construction/normalisation 的完整方案，并通过深层导航和旋转门禁。2026-07-24 `/domains/reserved` 已再次证明这一点；next80 已让 TEST60 与真实页横竖屏全部通过；next78 仍因扩大回归和系统异常保持撤回。
3. `WM_SIZE` 从 document-owned 外链 CSS 缓存 restyle + layout，且使用 cache-only callback。TEST24 与真实 Browse 旋转均已确认。
4. 主文档、外链 CSS、CSS `@import`、`<img>` 和 CSS 背景 GET 已组成分阶段 worker 事务；DOM/style/layout 仍只在 UI。2026-07-14 设备已确认 TEST3 的真实单响应正文进度、TEST43 资源事务、TEST44 主文档失败回滚和 TEST13 IANA Browse 基线。UI 提交现由一次性 WM timer 拆成 parse/style/image-discovery/layout 四段，单个 NetSurf 调用仍可能卡顿。宿主暂存预算为 64 URL/2 MiB，不是 core ABI 限制；整页聚合进度、网页字体和脚本仍待处理。
5. TEST30-37 已于 2026-07-13 真机通过：CSS 背景、基础 text、连续线性/径向渐变及坐标、继承/透明 stop、循环保护、径向 SVG 文档缓存以及 `<img>`/CSS 背景单次 fetch 复用均成立；复杂 shaping、`textPath`、逐字定位、任意 shear、径向焦点与 spread method 尚未实现。
6. 当前 IANA 线上 CSS `iana_website.80c103cc08b6.css` 使用 custom properties、媒体查询范围语法、`oklch()`、`calc()`、grid/gap 与 `:has()`。整数像素媒体范围、同表顶层 `:root` token、TEST40 的数值型 OKLCH/可求值 calc、TEST41 的 `/numbers` 宽度隔离及 TEST42 的 NetSurf overflow scrollbar 均已真机确认；不要扩大表述为完整 MQ4、custom-properties、CSS Color/Values、Grid、触摸惯性或 overlay scrollbar 支持。
7. 图片/SVG：TEST20 四格式缓存 `<img>`、TEST25-37 的 SVG 正式链，以及 TEST13 网络相对 SVG 链均已真机确认。公共 `positron_image.dll` 已覆盖 retained WM 位图/SVG、旧 core 转发、PNG/JPEG/BMP/GIF 编码与静态 libjpeg-turbo 4:4:4。next51 已确认 ABI 1.4 启动前 BMP/GIF 编码、签名、回读检查和六项视觉均正常，但标题栏 X 仍按 WM 约定 Smart Minimize，说明仅处理 `WM_CLOSE` 不足。next52 改用 `SHDoneButton(SHDB_SHOW)` 的原生标题栏 OK，并处理 `WM_COMMAND/IDOK`；用户已确认进程真退出且可再次启动。按用户要求不增加左右软键。复杂文本、径向焦点、spread method、background-size 和多层背景仍是显式缺口。
8. 测试节奏：默认按能力批次积累多个相关实现、自动断言和直绘/正式链回归，再用一个 `test_host.ini` 一次交付多个 TEST。除编译错误、高风险回归定位或设备专有故障外，不应为每个微小改动单独要求用户验收。
9. `PCore_StyleDocumentEx2` 是旧样式 ABI 的兼容扩展：接收绝对 document URL 和宿主 URL resolver；core 用 libcss 原生 pending/register 机制处理最多 16 层导入，失败导入注册空表。WM 宿主使用 `InternetCombineUrlA`，旋转只读 document CSS cache。TEST45 已真机确认嵌套、缺失回退、URL 规范化和缓存重选。
10. 表格构盒已移植 NetSurf 3.11 的 span occupancy，next53 已确认 TEST46；next56 又确认匿名包装与空 cell。next64/TEST53 的基本 collapsed-border 冲突已验收；next65/TEST54 又确认 rowspan 实际终止 row 和 row-group 边界承接。next67/TEST55 已确认 cell baseline 与 separated-table `empty-cells:hide`；next68/TEST56 已确认显式 table height 向 row/cell 的比例分配。NetSurf 3.11 与 2026-04-28 官方最新源码仍未建模 column/colgroup border 来源，该项继续保留。
11. next54 的宿主纵条和 auto-height 横条空间有效，但第二次整树 layout 误改 fixed-height overflow 几何，导致 TEST42 原右箭头点击位置失败；右箭头图形还相对 16px 控件向下偏 2px。该包不能作为新基线。
12. next55 在二次 layout 前屏蔽 fixed-height `overflow:auto` 的首轮横向 extent，只让 auto-height 容器获得额外空间；右箭头改用与左箭头对称的 `area.y0` 基准。用户已确认 TEST41/42、短页纵条与色块页均正常；冻结的 TEST13 导航链未改。
13. next56 按 NetSurf 3.11 规则补 table/row-group/row/cell 匿名盒和短行空 cell。用户已确认 TEST47 红/白、绿/蓝两行及同批其余测试正常。
14. next57 移植 NetSurf 列表 marker 构造并恢复 LI DOM user-data 映射。TEST48 自动校验 disc/circle/square、十进制 `start/value/reversed` 及 marker 几何；`PCore_ListMarker` 是只读诊断 API。
15. next58 引入 Noto OFL 来源的静态 Positron Symbols/Emoji 子集。next59 追加官方 hinted Noto Sans Symbols Basic 子集，用生成的精确 cmap 覆盖表互补两套 symbol face；设备确认箭头不再 tofu、marker 和五个 emoji 均可见且视觉稍有改善。当前字体范围明确只包含符号与单色 emoji；不要继续加入普通语言/多语种字体，也不要宣称网页 `@font-face`、复杂 emoji shaping 或彩色字体支持。`ANTIALIASED_QUALITY` 最终效果仍取决于 OEM GDI。
16. next60 用生成的 `positron_format_list_style.c` 替换 decimal-only stub，算法与 47 种样式来自仓库内原版 libcss。`scripts/port_list_style_vs2008.py` 负责指定初始化器和 UTF-8 字面量的可重复 C89/ASCII 转换；`list-style-image` 复用 document image cache，只有 computed list-item 才发现资源，解码失败保留类型 marker。next60 首次设备 TEST50 的 `found=4 fetched=2` 来自旧 Debug core DLL 打包事故；`stage.bat` 增加自动增量 Build 门禁后，next61 已确认 TEST50 的计数、缓存 SVG marker 与失败回退全部通过。
17. next62 把 NetSurf 3.11 已计算但未参与 layout 的 `list-style-position:inside` 接到 inline-first 首行：marker 尺寸在 minmax 前准备，首行吸收 marker+4px，换行恢复内容起点，图片 marker 可抬高行高。TEST51 通过新增只读 `PCore_ListItemGeometry` 自动检查 outside/inside、`VIII.`、缓存 12x12 SVG 与悬挂换行；用户提供的横竖屏截图均符合预期。`c89ize.py` 同批增加注释前导声明、函数头和多行初始化声明规则，`scripts/test_c89ize.py` 的 4 个回归必须先通过。
18. next63 按 W3C inside marker 的首个 inline element 语义，在构盒时加入零宽匿名 inline run；block-first、空条目、嵌套列表和 block-first 图片 marker 因而都由原 NetSurf block/inline layout 计算高度与兄弟位置，不在 layout 后手改坐标。TEST52 的 III/IV/V/VI、空行、嵌套缩进和绿色图片 marker 已由横竖屏截图确认。float 邻接仍明确不支持，本批未触碰冻结的 TEST13 导航链。
19. next64 新增 `PCore_TableCellBorder` 只读诊断，不改变布局/重绘。TEST53 一次检查 collapsed model 的 wider、style priority、hidden、left/top tie、origin precedence 与 separate 对照；用户已放大核对纵横屏截图并确认符合预期。
20. next65 对 vendored NetSurf `table.c` 做最小修复：bottom used border 使用 rowspan 实际终止 row；非末尾 row group 不冒充 table bottom，组间边由下一组 top 冲突算法承接。TEST54 自动断言 finite/zero rowspan、colspan 和 row-group 四组场景；仅需验收 TEST54，TEST13 路径未改。
21. next66 用 NetSurf 现有 inline baseline 约定补 table-cell baseline，并按 Mozilla `ShouldPaintBordersAndBackgrounds`/可见内容判定实现 separated model 的 `empty-cells:hide`。`PCore_TableCellGeometry` 只读返回 cell 与首段文字几何；TEST55 同时检查 top/middle/bottom、baseline、rowspan 与三类空格像素。因 baseline 是 table-cell 初始值，本批默认配置保留 TEST13 深层导航复测，不能只看 TEST55。
22. next66 的 TEST55 真机原始像素为 `FFFFFF/00C300/C6C300`，证明三类绘制正确，但设备 compatible bitmap 将 CSS `#00c000/#00c0c0` 量化了 3-6 色阶，使桌面式精确 RGB 断言假失败。next67 只改 TEST55 为紧格通道容差，未改 core layout/redraw。TEST13 Further Reading 的圆点来自 IANA 页面真实 `<li>` 与 next57-63 已验收的 marker 支持，不是 next66 回归。
23. next67/TEST55 自动断言和可见语义已验收；截图显示测试页因四组固定高度只超出 WM 客户区十几像素，仍生成了纵向滚动条。next68 将 TEST55 压缩到约 240px 内容高并设定标题行高；core 新增的显式表高分配参考 Blink 比例分配/小数余量规则，用 NetSurf rowspan 活跃列累加 cell bottom padding。`PCore_TableRowGeometry` 仅供 TEST56 读取最终 row 几何。2026-07-16 设备截图确认 TEST55 完整显示且无多余纵条，TEST56 的等高行、三种垂直对齐和 rowspan bottom 均正确；同批 TEST13 长页滚动正常。
24. 2026-07-20 完成仓库自包含审计：mbedTLS 2.16.12 完整官方源树和许可证已纳入 Git，cJSON 1.7.18 补齐独立许可证及来源。根 `LICENSE` 只覆盖 Positron 自有代码；NetSurf 浏览器源码的 GPLv2 等边界见 `THIRD_PARTY.md`。`python scripts\audit_repo.py` 当前检查 14 个工程、598 个工程输入、Git 跟踪、版本与关键许可证。
25. next69 首次百分比 table-row 第二遍得到错误的 `20/30/30`。随后切换包时 TEST56 的异常来自 WM/CE 全局 DLL 复用；next72 同包已证明 TEST56 正常。next72 的 TEST57 `styles=0:0` 暴露 inline `style=` 没有进入选择，next73 改外部 CSS 后 TEST55/56/57 均通过。后续确认直接根因是自有 `pcore_style_subtree` 固定传空 inline sheet，而不是未参与正式构盒路径的 NetSurf `author_level_css` 分支。next74 接入 inline sheet 后，TEST56 行高保持正确但 `.distributed .top` 失配；next75 修复祖先/父节点回调的通配 qname `*` 处理，设备已确认未改断言的 TEST56 和新增后代 class 断言的 TEST58 均通过。百分比 cell/后代内容和 `col`/`colgroup` 模型仍未覆盖。
26. 2026-07-24 TEST13 起始页正常，但进入 IANA `/domains/reserved` 后正文左移；该页没有 TEST41 的 Grid，只在宽表格外声明 `.dtable-wrap { overflow:auto }`。next77 把 TEST41 的 Grid fallback 特例收敛为通用但受限的 min-content boundary，仅对横向可收缩 flex item 的 grid/overflow 后代生效，显式 min-width 保持。设备确认 TEST59 和同批回归通过，竖屏子页边距恢复；继续旋转为横屏后，首个 `Domain` 表头内容却左移约 18px 至 wrapper 裁剪边界，字体/样式观感随之异常。
27. **next78 已撤回**：每次 layout 后递归调用 `scrollbar_set(...,0)` 并非无副作用的状态清零。设备上 TEST13 横屏从单个 `Domain` 异常扩大为全部表格单元格异常，随后 TEST56 报 `rows=35/35/35 35/35 sum=105/70 off=2/10/19 va=0/2/3/3`，并触发系统级 `test_host.exe` 异常。实现、`PCore_NodeScrollOffset`、`PCore_TableCellTextStyle` 与扩展 TEST59 已全部删除；旧包已改名为 `C:\WMShare\Positron-next78-FAILED-DO-NOT-USE`。
28. next79 恢复候选保留 next77 已验收的 flex min-content 修复，只恢复旧版 TEST59。`pcore_box.c` 与 `positron_core.h` 已回到 next77 内容；ARMV4I 重建后的 `positron_core.dll` `.text` 段大小 1,308,160 字节，SHA-256 `756629E25B063856B2DC334560B3EAB8C28A043D1758973FADE791BCC912CFFA`，与 next77 逐字节一致。包位于 `C:\WMShare\Positron-next79`，默认配置先隔离运行 TEST56/59，再单独跑 TEST13。
29. next79 已由设备确认 TEST56/59 正常，TEST13 也准确回到仅横屏首个 `Domain` 异常。继续审查 libcss 发现 Positron 的 `set_libcss_node_data` 曾立即执行 `CSS_NODE_DELETED`；但 `css__get_parent_bloom` 会在回调返回后继续使用该数据中的 bloom，形成悬空指针。next80 随后按 NetSurf `select.c` 的模式把数据挂到 libdom user-data，并在每个新 selection context 开始前递归失效旧缓存；TEST60 对两次 viewport restyle 的首表头几何和字重代理宽度做自动断言。
30. 2026-07-25 设备确认 next80 的 TEST56/58/59/60 全部通过；真实 TEST13 `/domains/reserved` 截图中首个 `Domain` 在横竖屏均恢复正常 inset、字重和基线，其余表格内容与滚动保持正常。该 selector node-data 生命周期批次完成。
31. 2026-07-30 next104 在 next103 表单基线上接入 WM 原生 multiple LISTBOX。不要改成自绘菜单：`LBS_MULTIPLESEL` 允许手指逐项切换，`LBS_NOINTEGRALHEIGHT` 保证窗口高度不偏离 NetSurf border-box；宿主逐项比较 `LB_GETSEL` 与 Core 状态，disabled option 被拒绝时以 `LB_SETSEL` 回滚。TEST71 同时检查 disabled select、单选 COMBOBOX、原生重建、GET 重复字段和 reset。设备日志连同 TEST13 深链及 TEST20/27/43/44/56/58-70 全部 PASS。
32. 2026-07-30 next105 首次接入提交前 required 验证，但 TEST72 reset 后仅恢复 6 个 invalid。根因不是断言，而是 libdom 0.4.2 会把无初始 `value` 属性的 text/password 第一次运行时写值记为 `defaultValue`。next106 在 `PCore_TextInputSetValue` 写入前冻结解析时默认值；设备日志确认 required text/password/textarea/file、checkbox、同名 radio、single/multiple select、首个无效控件几何、提交/Enter 阻止、两种 bypass、multipart 与 reset 全部通过，同批 TEST13/20/27/43/44/56/58-71 无回归。
33. 2026-07-31 next109/TEST73 将 live `:checked/:enabled/:disabled` 和宿主命中状态 `:focus/:active` 接到 libcss callback；交互变化采用 posted/coalesced cache-only restyle，避免在原生控件通知栈内同步销毁重建控件。设备日志确认焦点、按压、checkbox/option、纵横屏保持与 reset，并且 TEST13 三段导航和 TEST20/27/43/44/56/58-72 全部通过。此批不包含 DOM Focus/Mouse 事件传播、取消/default-action；无 CSS 尺寸的空 text input 仍没有浏览器默认 intrinsic size。
34. 2026-07-31 next110/TEST74 复用 libdom EventTarget 建立公共 C ABI，修正 target 重复传播、`bubbles/cancelable` 未生效和 dispatch-only 状态残留；Browse 宿主在既有 click 默认动作前派发可取消事件。设备日志确认 TEST74 及 TEST13/20/27/43/44/56/58-73 全部 PASS。专用事件类型数据、完整 HTML activation 与 JS binding 仍是后续边界。
35. 2026-08-03 next111/TEST75 按 NetSurf `box_construct.c` 的 absolute inline 特例补齐 slim builder：普通 relative box 保持正常流并应用偏移，absolute/fixed block 进入既有正式定位路径，`display:inline` 脱流元素改为 `BOX_INLINE_BLOCK` 后由 `layout_position_absolute()` 消费。设备日志确认 TEST75 及 TEST13/20/27/43/44/56/58-74 全部 PASS。该批不代表 float、Grid 轨道、sticky 或所有复杂 containing-block 组合已经实现。
36. 2026-08-03 next113/TEST76 接通动态 `:hover`：core 保存 hover DOM 元素并在下一次 libcss 选择时匹配，WM6 宿主用 `WM_MOUSEMOVE` 加 250ms 定时器轮询离开窗口；`TrackMouseEvent` 等桌面 API 不可依赖。设备日志确认 TEST76 及 TEST13/20/27/43/44/56/58-75 全部 PASS。该批不代表 `:visited/:target/:indeterminate`、专用 MouseEvent 或 JavaScript 已实现。
37. 2026-08-03 next114/TEST77 建立脚本资源 ABI：core 扫描非空外部 `<script src>`，可经 `PCoreResolveUrlFn` 使用宿主 URL 解析，调用 `PCoreFetchFn/PCoreFreeFn`，将成功 body 按 document 生命周期去重缓存，并以 `PCore_GetScriptResourceCount/GetScriptResource` 提供只读枚举。TEST77 覆盖相对/root-relative/absolute URL、重复引用、第二次 cache-only 扫描和 inline script 不执行；ARMV4I 增量构建和设备 `TESTBENCH PASS` 均已确认。该批不解释 `type`、不执行 JS，也不接入 TEST13 的网络事务。
38. 2026-08-04 next115 提交普通非替换 float 候选，但设备 TEST79 得到零宽 inline probe，TEST13 截图出现导航扁平化；该候选否决，不得作为设备基线。
39. 2026-08-04 next116 收窄为显式 block-level float，仍产生真实 TEST13 导航/正文排版回归，设备 TEST79 最终失败；代码、配置和 ENGINE 接入已撤回，next114 恢复为设备基线，Float 方向暂挂。
40. 2026-08-04 next118 接入独立 `positron_script.dll`：复用仓库内 NetSurf Duktape 2.7.0 单文件源，新增稳定 C ABI、DLL 自有堆/字符串所有权、持久上下文、预算、错误恢复和内存/执行计数；TEST80 不初始化 `positron_core`，只验证外部程序调用边界。VS2008 ARMV4I Debug 增量构建和设备 TEST80 已通过；timeout/source-size/recovery 边界与浏览器 inline/external JavaScript、DOM/window/fetch binding 仍关闭或待后续批次。
41. 2026-08-04 next119 为独立脚本 DLL 增加 TEST81：用短预算验证无限循环可中止、64 KiB 源码长度上限拒绝和上下文恢复。ARMV4I Debug 增量构建、staging 与设备日志均已通过；该批不添加完整内存配额，也不接入浏览器 JS。
42. 2026-08-05 next120 为独立脚本 DLL 增加 `PScript_CreateEx`、512 KiB runtime heap limit、peak memory telemetry 和 TEST82；ARMV4I Debug 增量构建、staging 与设备日志均已通过，浏览器 JS 仍关闭。
43. 2026-08-05 next121 为独立脚本 DLL 增加 CommonJS 风格模块 ABI：按名一次执行、缓存 exports、`require()` 读取已加载模块，失败删除半成品，`PScript_ClearModules` 显式清空。TEST83 已加入默认配置；ARMV4I Debug/Release 构建、staging 与设备日志均已通过，next121 提升为当前设备基线。

## 开发纪律

- 面向用户回复使用简体中文。
- 默认先查环境，再改代码。WMDC、僵尸 `test_host`、共享目录、旧二进制非常容易造成假故障。
- 构建只允许通过 `scripts\build.bat` / `scripts\stage.bat` 的 WM6 ARMV4I 配置。禁止调用 `VC\bin\dumpbin.exe`、桌面 `link.exe` 或其他 x86 VC 工具；VS2008 `dumpbin.exe` 会内部启动桌面 `link.exe /dump` 并因缺少 `mspdb80.dll` 弹系统错误。PE section 检查使用 PowerShell/.NET 直接读文件。
- 改 TEST 时必须同步所有 MessageBox 文案、分组范围、最终 summary。
- 改 vendored NetSurf 源码时保持最小差异，C89 化要谨慎；先运行 `python scripts/test_c89ize.py`，再对目标源运行脚本并要求显式 `0 change(s)` 或审阅每项改写。`c89ize.py` 仍不能处理所有 designated initializers / static aggregate initializers。
- 如果新接入 NetSurf content-handler `.c` 后从 `html/private.h` 爆出 `dom_document` / `dom_node` / `bool` 之类连锁语法错，优先检查该 `.c` 的 include 区是否对齐 `layout.c` / `redraw.c`，不要先改 `private.h` 或让 `c89ize.py` 硬处理。
- 不要引入 IE Mobile ActiveX 作为渲染层；渲染层方向是 OSS browser kernel port。

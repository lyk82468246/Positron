# Positron Current Handoff

更新时间：2026-07-30
当前分支：`main`  
当前设备基线：next106，配置的 TEST13/20/27/43/44/56/58-72 已由无人值守设备日志确认全部 PASS。next104 已完成 WM 原生 multiple select；next106 又接通首批 `required/valueMissing` 约束验证、首个无效控件定位、提交/Enter 阻止、`novalidate/formnovalidate` 旁路、multipart 与 reset。`pattern`、类型语法、数值/长度范围、custom validity、`invalid` 事件、验证气泡和完整事件系统仍未实现；真实触屏与视觉仍待累计人工检查。**next78 仍是已撤回的失败实验，不得使用**。

> **接手前先读**：导航路径以用户确认正常的 `9c5c7c7`/next37 为冻结起点，此后 `main` 已继续叠加图片、字体、列表和表格能力。next37 后那组失败的导航实验保存在远端 `codex/post-next37-experiments`，不得直接合回；这不表示当前整个仓库仍停在 next37。冻结项、失败时间线和后续门槛见 `ROLLBACK_NEXT37.md`。

## 项目目标

Positron 是面向 Windows Mobile 6 Professional / WinCE 5.02 / ARMV4I 的现代基础设施集合，并在其上建设浏览器内核和 Electron-like 应用运行时。公共 DLL 必须能被其他 WM 程序独立调用，不能只按 test_host 或浏览器内部模块设计。完整分层见 `ARCHITECTURE.md`。核心原则是“给 WM6 打补丁”，不是重造系统：

- 现代 TLS 是 WM6 缺口，所以用 `positron_tls.dll` + mbedTLS 2.16.12。
- 现代 HTML/CSS 渲染是 IE Mobile 缺口，所以移植 NetSurf 3.11。
- WM6 已有且够用的能力优先复用：明文 HTTP 用 WinInet，绘图用 GDI，后续图片应优先考虑 WM Imaging API。
- WM6 缺少的成熟能力优先联网检索并移植许可证兼容的开源实现；只有平台胶水和 ABI 包装才优先自写。

源码依赖现已自包含：NetSurf、mbedTLS、cJSON、Expat、libjpeg-turbo、NanoSVG 与 Noto 字体都固定版本并随仓库提供。新环境只需另行安装不可再分发的 VS2008 SP1、WM6 Professional SDK 与模拟器；运行 `python scripts\audit_repo.py` 可检查 14 个工程引用、版本和许可证。第三方边界见根目录 `THIRD_PARTY.md`。

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

- 快速配置：`test_host.exe` 同目录的 `test_host.ini` 当前基线使用 `tests=13,20,27,43,44,56,58-72`。也支持 `tests=1-5 7b` 一类语法。`auto=1` 时不弹 Yes/No/OK，窗口首帧后自动关闭，TEST13 自动跑 example.com → IANA Example Domains → Reserved Domains，并把每个原始结果和逐页遥测覆盖写入同目录 `test_host.log`；`auto=0` 保留 Yes/No 与原四组路由。自动首帧冒烟不替代新视觉能力的人工截图验收。缺失/无效配置不会静默改变测试范围，TEST23 不可选。

- Communication：TEST 1-5，TLS/HTTP/JSON，需要网络。
- Engine：TEST 6-11、15、16、18、21、22、24、25、38、40-45、59-61，解析/选择/样式/layout/box tree/image resource cache、responsive media viewport、reverse flex、cached CSS restyle、SVG parse、受约束的 `:root` token、数值型 OKLCH/可求值 calc、grid/overflow min-content 隔离、overflow scrollbar、分阶段资源事务、失败回滚、CSS import tree、selector node-data restyle 与具名 NetSurf option 默认，离线。TEST40-45、59、60 已真机确认；next78 扩展测试及其 core 行为已经撤回。TEST23 浮动最小样例已因真实 Browse 回归撤回，不运行。
- GDI Render：TEST 12、14、17、19、20、26-37、39、46-58、62-72，离线窗口渲染、WM Imaging 位图、SVG path/cache/fallback/fill-rule、CSS background-image、原生 GDI text、线性/径向渐变、同文档及重叠文档缓存复用、table/list、HTML inline author CSS、普通表单、multipart/file、WM multiple select 与首批 required 验证；TEST65-72 已验收。
- Browse：TEST 13，真实页面抓取 + 渲染，需要网络。

当前最关键验证：

- TEST 17：内置 NetSurf real layout + redraw 页面。预期：深红 H1 和红色下边框、带边框的三色 flex 横排、2x2 table 可见 cell 边框。
- TEST 56：显式 table height 分配。预期：105px 三行等高且文字依次 top/middle/bottom；70px 两行等高且橙色 rowspan 文字在底部；页面无多余纵向滚动条。
- TEST57：第一张 80px 表应约为 20/40/20px，第二张 50px 超约束表应为 25/25px；next73 已连同 TEST55/56 一起通过。
- TEST59：分别在 224px 和 320px viewport 建立无 Grid `overflow:auto` 宽表格夹具，必须保持 reversed-flex main 的 `x=25,width=viewport-50`。next78 的同 DOM 旋转/scrollbar 诊断版本已经撤回。
- TEST60：同一 DOM 先按 224×320、再按 400×240 重做 style/layout；IANA 同型 `.dtable` 的首个 `<th>` 必须保留 18px/10px inset，并与第二个同文字表头保持同一粗体宽度。它同时覆盖 `thead th`、`:first-child` 和后续 `tbody > tr:first-child > th` 选择器。
- TEST61：正式 NetSurf layout 中，同一串文本的 `1px` 与 `8.5pt` 必须测得相同宽度，证明 `font_min_size=85` 生效；`12pt` 控制组必须更宽。JavaScript 策略继续为 false。
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

> 2026-07-30 优先级调整：先解决“有没有”，再解决已有小范围“好不好”。普通表单、multipart/file、WM 多选列表与首批 `required/valueMissing` 已推进到 next106 / TEST72 设备自动化基线。下一步依次补事件状态和重大布局缺口；其后再扩展高级约束验证。已有 NetSurf Duktape backend 的 JavaScript 最小纵切提前到中期。首屏 SVG 冷解析、抗锯齿、渐变高级参数、视觉微调和全面性能优化后置，除非它们造成崩溃、数据错误或阻塞基本操作。

1. 2026-07-11 用户真机确认 ENGINE 原整组至 TEST24 通过；2026-07-12 单独确认 TEST25 SVG parse。后续修改引擎路径时必须重跑当前整组。
2. TEST23 的浮动构盒最小复现虽通过，但真实 Browse 严重回归，已撤回。TEST13 起始页正常不等于所有 IANA 子页正常；2026-07-24 `/domains/reserved` 已再次证明必须走深层链接和旋转验收。next80 已让 TEST60 与真实页横竖屏全部通过；next78 仍因扩大回归和系统异常保持撤回。后续 float 仍必须对照上游 box construction/normalisation。
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

## 开发纪律

- 面向用户回复使用简体中文。
- 默认先查环境，再改代码。WMDC、僵尸 `test_host`、共享目录、旧二进制非常容易造成假故障。
- 构建只允许通过 `scripts\build.bat` / `scripts\stage.bat` 的 WM6 ARMV4I 配置。禁止调用 `VC\bin\dumpbin.exe`、桌面 `link.exe` 或其他 x86 VC 工具；VS2008 `dumpbin.exe` 会内部启动桌面 `link.exe /dump` 并因缺少 `mspdb80.dll` 弹系统错误。PE section 检查使用 PowerShell/.NET 直接读文件。
- 改 TEST 时必须同步所有 MessageBox 文案、分组范围、最终 summary。
- 改 vendored NetSurf 源码时保持最小差异，C89 化要谨慎；先运行 `python scripts/test_c89ize.py`，再对目标源运行脚本并要求显式 `0 change(s)` 或审阅每项改写。`c89ize.py` 仍不能处理所有 designated initializers / static aggregate initializers。
- 如果新接入 NetSurf content-handler `.c` 后从 `html/private.h` 爆出 `dom_document` / `dom_node` / `bool` 之类连锁语法错，优先检查该 `.c` 的 include 区是否对齐 `layout.c` / `redraw.c`，不要先改 `private.h` 或让 `c89ize.py` 硬处理。
- 不要引入 IE Mobile ActiveX 作为渲染层；渲染层方向是 OSS browser kernel port。

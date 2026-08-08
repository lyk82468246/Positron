# WM6 / Positron Debugging Notes

## 先查环境，再查代码

这个项目里大量“故障”最终都是环境问题。用户已经多次纠正过：不要一听失败就直接改源码。

先检查：

- WMDC 是否 still connected。
- WMDC Connection Settings 里是否允许设备联网，且 host 连接类型是 The Internet。
- 模拟器 IE Mobile 能否打开一个已知网站。
- WM6 的 X 按钮只是最小化，不是关闭；是否有旧 `test_host.exe` 僵尸进程。
- `scripts\stage.bat` 是否真的复制了新二进制到 `C:\WMShare`。该脚本现会先执行同配置增量 Build，构建失败不会开始复制。
- 快速复测时同时核对 EXE 同目录的 `test_host.ini`。当前 next154 已在 `screen=640x480 dpi=192` 通过 `tests=13,20,27,43,44,56,58-77,80-120`，日志记录 `TESTBENCH PASS`；next155 的 TEST121 失败包已由 next156 的 JSON UTF-8 修复候选替代，next156 尚待设备验收；TEST79/float 候选已撤回。配置也支持 `tests=1-5 7b` 一类范围；启动提示选择 No 会回到原四组路由。`stage.bat` 会覆盖 staging 目录中的该配置文件。
- 模拟器共享目录是否还挂载在 `\Storage Card`。
- 是否 Rebuild whole Solution，尤其是改了静态库或 vendored NetSurf 代码时。
- 首选用 `scripts\build.bat`；默认是 `Debug` 增量 Build，退出码和 `vs2008-build.log` 可供 agent 直接判定结果。改了工程依赖、生成规则或需要干净基线时运行 `scripts\build.bat Debug rebuild`。脚本使用 `devenv.com`，不要直接调用 ARM `cl.exe` 拼装整套工程。
- 2026-07-16：next60 首次 TEST50 得到 `found=4 fetched=2 matched=1 calls=3`。文件时间证明 staging 中 `positron_core.dll`/`pcore_select.obj` 为 22:04，而带 computed list-item gate 的 `pcore_select.c` 为 22:05；22:14 只补编了 Release，随后却打包 Debug，形成新 TEST50 EXE + 旧 core DLL。旧 DLL 会同时扫描继承 `list-style-image` 的 UL 与 LI，计数恰为 4/2/1/3。不要修改断言；重编 Debug 即包含仓库现有修复。为防复发，`stage.bat` 已改为先增量构建后复制。
- 同日 next61 首次通过新 staging 门禁：Debug 增量编译实际重编 `positron_format_list_style.c`、`pcore_select.c` 与 `main.c`，三个受影响项目 0 错误，仅有 9 条既有 `fpmath.h` C4244。`C:\WMShare\Positron-next61` 中 core DLL 与 test EXE 的 SHA-256 均和刚生成 Debug 产物一致；用户随后确认 TEST50 的 IV/z/aa/09、绿色图片 marker 与 circle fallback 全部通过。
- 2026-07-16：next62/TEST51 横竖屏确认 inside `VIII.` 与绿色图片 marker 共享首行，换行回到内容起点，outside 对照未回归。next63/TEST52 随后确认 block-first、空 marker 行、嵌套 VI 与 block-first 图片行在横竖屏均符合预期。next64/TEST53 已确认基本 collapsed-border 冲突；next65/TEST54 又确认 finite 红边、auto 紫边、colspan 青边和 row-group 橙边。next66/TEST55 首次真机读到 `FFFFFF/00C300/C6C300`：隐藏格确为白、show 格确为绿、filled 格确为青，失败来自 compatible bitmap 的 WM 色彩量化，不是 empty-cells 语义错误。next67 改为逐通道 8/12 色阶紧容差，仍会拒绝白/绿/青分类或通道错位。可见页应看到 top/middle/bottom 三档、Big 与 small 共基线、rowspan 底对齐，最后依次是白色空位、绿色强制 show 空格和青色有内容单元格。
- 2026-07-16：next67/TEST55 自动断言与可见语义均已通过；原页四组固定高度只超出 WM 客户区十几像素，仍使纵条 thumb 几乎填满轨道。next68 将 TEST55 压到约 240px 并显式设标题行高，设备确认页面完整显示且无多余纵条。TEST56 设备截图确认 105px 内红/绿/蓝三个等高行，文字依次 top/middle/bottom；下方 70px 表两行等高，橙色 rowspan 文字在底部，页面无纵向滚动条。同期 TEST13 长页面滚动正常。
- 2026-07-21：next69/TEST57 首次设备运行得到错误的 `20/30/30`；切换多个共享目录包时又出现 TEST56 失败，后来由 next72 同包 TEST56 通过及失败文本版本不符证明这是 WM/CE 全局 DLL 复用造成的 EXE/DLL 混搭，不能归因于普通表高算法。next72 的 TEST57 返回 `styles=0:0`，说明测试中的 inline `style=` 未参与选择；next73 改用外部 author stylesheet 后确认 TEST55/56/57 通过。2026-07-22 重新审阅 vcproj 与调用图后确认，正式路径不编译/调用 NetSurf `box_construct.c`，所以全 false `nsoption` 并非直接根因；实际是 `pcore_style_subtree` 向 `css_select_style` 固定传了 `NULL`。切包前仍必须彻底结束所有 `test_host` 进程。
- 2026-07-24：next74 接入 inline sheet 后，TEST56 报 `rows=35/35/35 35/35 sum=105/70 off=2/10/19 va=0/2/3/3`。行高、两表总高和后三个对齐值均正确，退化集中在 `.distributed .top`：移植层 `named_ancestor_node`/`named_parent_node` 把 class-only 复合选择器携带的 qname `*` 当成真实标签名。next75 统一改用已有的 universal-aware name matcher，并在 TEST58 加 `.scope .probe` 后代 class 断言；TEST56 原夹具和断言保持不变。设备随后确认 TEST56/58 均通过，TEST58 的 cascade 文本和 25/50/auto 可见布局符合预期；下一门禁是同二进制的 TEST13 全流程与完整冻结回归。
- 2026-08-03：next111/TEST75 只改 slim box builder 的定位构盒分支，按 NetSurf 的 absolute inline 规则把 `position:absolute/fixed;display:inline` 构造成 `BOX_INLINE_BLOCK`；普通 block absolute 与 relative 复用已移植的 `layout_position_absolute/relative`。设备日志确认 TEST75 与 TEST13/20/27/43/44/56/58-74 全部 PASS。若后续扩展 float，不能把这个局部定位修复误当作 float 构盒已经安全。
- 2026-08-03：next113/TEST76 首次失败为 `initial=0000FF hover=000000 clear=000000 box=0,0 0x20`。根因是 `PCore_InteractionSetAt` 的入口掩码漏掉新 `PCORE_INTERACTION_HOVER`，且测试直接读取普通 inline `<a>` 的零宽起始标记盒。修复入口校验，并让测试目标使用明确尺寸的 block link 后，ARMV4I 增量构建、staging 哈希和设备 `TESTBENCH PASS` 均通过。WM6 SDK 没有桌面 `TrackMouseEvent`/`WM_MOUSELEAVE` 依赖，宿主 hover 离开检测使用 250ms 定时器轮询；后续不要改回桌面 API。
- 2026-08-04：next115/TEST79 是新的受控 float 候选，不是 TEST23 的恢复；设备已证明它失败，不能继续使用。`pcore_box.c` 过宽地接管了 inline float，且 TEST79 查询到零宽 inline 起始盒；图片和表单控件排除并不能抵消真实 TEST13 的导航回归。next114 保持设备基线。
- 2026-08-04：next116 收窄为显式 `display:block` float 并改用固定尺寸 inline probe，但设备日志仍为 `TEST79 FAIL`，截图也显示 TEST13 导航/正文排版异常。自动 TEST13 的数值 OK 不能覆盖人工视觉门禁；next116 的源码、测试和配置已撤回，Float 方向暂挂。若重启，必须先设计完整 box construction/normalisation 方案。
- WM6 SDK 没有桌面 Win32 `ShowScrollBar` 的声明或导出；需要动态隐藏标准滚动条时，使用 `GetWindowLong/SetWindowLong(GWL_STYLE, WS_VSCROLL)`，再用 `SetWindowPos(..., SWP_FRAMECHANGED)` 重算非客户区。
- 设备上是否在跑旧的 VS Deploy 目录，例如 `\Program Files\test_host\`。

## 没有 console：交互用 MessageBox，批量用同目录日志

WinCE/WM6 上没有 stdout。`auto=0` 时 MessageBox 仍是现场调试器；`auto=1` 时相同 `show_info/show_error` 内容写入 EXE 同目录 `test_host.log`，不再创建需要人工确认的结果框。自动可视测试只保证公共窗口至少绘制一帧并正常退出，不能替代字体、抗锯齿、边距和复杂版式的人工判断。定位崩溃/卡死时：

- 用 `MessageBoxW`，不要用 `MessageBoxA`。
- 一律带 `MB_TOPMOST | MB_SETFOREGROUND`。
- 插入编号 stage probes，例如 `BUILD 7 / step 3`。
- 必须带 BUILD stamp。看不到新 stamp 就说明跑的是旧二进制，先修部署。
- 跨 DLL / 静态库边界可以在 DLL 对象里放非 static trace hook，再从 vendored 源码 extern 调它。

诊断完成后：

- 删除所有 probe。
- 只留下真实修复。
- 用 `git diff --stat` 确认没有诊断垃圾。

## C89 / VS2008 规则

- 不写 mid-block declarations。
- 不写 `for (int i = ...)`。
- 不写 designated initializers，除非后续有转换脚本处理。
- 谨慎使用 `scripts/c89ize.py`：它主要处理块中声明和 for 声明，不能包治 designated initializer / static aggregate initializer。先运行 `python scripts/test_c89ize.py`；当前 4 个回归覆盖注释后的合法声明组、函数头后的 mid-block 声明、多行初始化声明和 aggregate 字段不重排。
- 2026-07-16 复盘：旧规则把块首注释误当语句，曾把后面的合法声明组拆坏；又把函数定义头误当未结束声明，从而漏掉真正的 mid-block 声明；多行初始化声明结束行也可能导致紧随声明被误搬。修复后对 `layout.c`、`pcore_box.c`、`main.c` 重跑必须明确显示 `total: 0`，不能只凭 VS2008 恰好能编译判断脚本稳健。
- 新移植 NetSurf content-handler `.c` 时，把 include 列表先对齐已经编过的 `layout.c`，否则容易出现 `private.h` 类型未定义连锁错误。
- 2026-07-08 复盘：`redraw_border.c` 编译时报 `html/private.h` 里 `dom_document` / `dom_node` / `bool` 连锁语法错误，根因仍是单文件 include 前置依赖不足，不是 `c89ize.py` 应处理的问题。修法是像 `layout_flex.c` / `table.c` 一样补齐 `layout.c`/`redraw.c` 的 dom/css/content 前置 include，再跑 `c89ize.py` 确认 0 change。

## test_host 文案纪律

改 TEST 时，同步修改：

- 分组选择器里的 TEST 范围。
- 测试开始前 `show_info` 的预期画面说明。
- 失败 `show_error` 文案。
- 最终 success summary。

设备端 MessageBox 是用户看到的全部 UI，错误文案就是用户可见 bug。

## 引擎切换后先审计旧几何断言

2026-07-10 复盘：TEST 11 的 `body.y=8` / `p.y=24` 来自旧手写布局器，它把 body 的 8px margin 与首段 1em margin 分开处理。M6 改走 NetSurf `layout_document()` 后会执行父子 margin collapse，当前测试 DOM 中两者的报告原点都在 `y=16`。因此看到 `body=(8,16,224,304)`、`p=(8,16,224,20)` 时，不是联网失败，也不是布局器没有运行，而是旧测试预期落后于引擎语义。

ENGINE 的基础测试 6-10 仍 fail-fast；引擎初始化成功后，TEST 11/15/16/18 应各自运行并收集失败，避免一条几何断言遮住后续结果。修改测试范围时还要检查最终 success summary，不能在 `rc != 0` 时误报全部通过。

TEST 11 不能只接受设备当前坐标：同时保留默认折叠组和 `padding-top:1px` 阻断组。前者预期 `body.y=p.y=16`，后者预期 `body.y=8,p.y=25`；两组一起通过才证明 margin-collapse 路径和停止条件都工作。

2026-07-10：用户真机截图已确认 TEST 11 OK，显示 collapse 组 `body box = (8,16) 224x304`、`first <p> = (8,16) 224x20`；阻断组也在同一 TEST 11 内部断言。

图片路径分层记忆：TEST 18 只验证 `<img src>` 资源发现、fetch、document user-data 字节缓存和 URL 去重；TEST 19 只验证 WM Imaging API 能从内存 BMP 取尺寸并通过 `IImage::Draw` 画到 HDC；TEST 20 验证缓存字节变成 `box->object`，经 `content_redraw -> plot_bitmap` 真正绘制 `<img>`。用户于 2026-07-11 确认 TEST 18（first/second=2/2，fetch calls=2）与 TEST 20 均通过。

2026-07-13：增强 TEST26 会在打开可见窗口前，先离屏检查红绿内部像素和蓝色 cubic 的部分覆盖边缘像素；用户截图既出现平滑曲线又进入该窗口，因此自动断言与视觉结果都已通过。缓存 SVG 不另走旁路：TEST27 把 retained object 放进 typed image carrier，仍经 `box->object -> content_redraw -> plot_bitmap`，并在开窗前检查 fetch/free、`120x60` 布局及离屏红绿像素；用户截图确认同页显示且无 fallback，故正式链通过。TEST28 的设备截图确认损坏 XML 保留绿色 alt 文本且页面继续；TEST13 的网络 fixture 确认 HTTPS HTML、同目录相对 SVG、fetch/cache 与正式绘制全链通过。

同日审计 NetSurf 官方最新 libsvgtiny 提交 `7ede71b`：公开 shape 结构与解析状态仍不保存 SVG `fill-rule`。因此仓库只做最小兼容扩展，不另写解析器：解析 presentation attribute 和 inline style，依靠 parse state 值传递实现组继承，把 nonzero/evenodd 映射到 NanoSVG rasterizer。TEST29 在显示窗口前检查默认 nonzero 同向内环保持实心、attribute evenodd 与 style evenodd 形成孔洞；可见结果应为一个实心红方块、一个蓝框和一个绿框。

TEST29 首次设备结果仅在绿色环精确值失败：SVG `#00a000` 经 screen-compatible RGB565 bitmap 量化后，`GetPixel` 返回 `#00a200`；红/蓝环及两个白色孔洞均正确。这不是 fill-rule 失败。测试 fixture 改用 RGB565 可精确表示的纯绿 `#00ff00`，仍对六个位置做精确相等断言，不放宽孔洞或颜色判断。

2026-07-13：TEST32 首次设备截图证明缓存 SVG 渐变/文本正式链已经可见，但红到蓝色带中出现密集竖向亮缝。根因是 libsvgtiny 上游线性渐变实现把原路径三角剖分为许多纯色 shape，NanoSVG 分别抗锯齿时边界重复混合背景。修复不替换解析器：libsvgtiny 输出单一路径、stop 数组和“最终坐标到归一化渐变轴”的矩阵，`positron_image` 将其映射为 NanoSVG `NSVG_PAINT_LINEAR_GRADIENT` 一次填充。TEST32 现除左右/中点和白色文字外，还拒绝扫描线上的绿色亮缝及大幅邻色跳变。

同日复验：TEST32 新构建在设备上显示连续红紫蓝色带和居中白色 Positron，旧竖缝消失，意味着原有缓存/replaced-box/NetSurf redraw 断言与新增 seam/jump guard 同时通过。随后新增 TEST33，分别覆盖 objectBoundingBox 斜向轴、userSpaceOnUse 水平轴和 `gradientTransform` 旋转得到的竖向轴；设备窗口应依次看到斜向、水平、竖向三块平滑红蓝方块。

同日设备复验 TEST33：三块图依次显示平滑斜向、水平和竖向红蓝渐变，与九点颜色和 seam/jump guard 一致，线性渐变坐标矩阵闭环。随后审计 NetSurf 官方 libsvgtiny 当前提交 `073283b`，其 README/解析分派仍只列出和处理 linearGradient；仓库不手写新光栅算法，而是在原 DOM 桥中补 `radialGradient` 的 `cx/cy/r`、坐标系与变换，交给已 vendor 的 NanoSVG `NSVG_PAINT_RADIAL_GRADIENT`。

TEST34 已通过 C89 专家脚本及 VS2008 ARM 增量构建：离屏先验证 objectBoundingBox 椭圆、userSpaceOnUse 圆、gradientTransform 平移圆的中心/中段/边缘颜色与连续性，再打开可见窗口。NanoSVG 径向器自身明确未实现焦点 `fx/fy`，因此 TEST34 OK 也只代表中心径向基线；`fx/fy`、spreadMethod 和 stop-opacity 必须继续留在限制清单。

同日设备复验 TEST34：截图中第一块是随 70x90 objectBoundingBox 拉伸的竖向椭圆，第二块是 userSpaceOnUse 圆，第三块红心按 gradientTransform 向右平移；三块红紫蓝过渡连续且无接缝，故 TEST34 真机通过。随后新增 TEST35，不改渲染器，只把同类径向 SVG 送入现有 document image cache，验证 `cache -> replaced box -> content_redraw -> plot_bitmap`，并对 160x80 椭圆执行横纵采样和连续性扫描。

同日设备复验 TEST35：页面中的 160x80 replaced image 显示为连续横向红紫蓝椭圆，无 fallback，说明径向参数经 document cache、box object、content redraw 和 plot bitmap 完整保留。用户要求降低人工测试频率，后续改为能力批次交付。

首个批次 TEST36-37：libsvgtiny 对 objectBoundingBox 无单位数按 `0..1 * bbox` 解释，支持 SVG2 `href` 与 SVG1.1 `xlink:href`，引用深度限制为 16 防止循环耗尽栈；gradient stop 新增独立 opacity，并沿 `positron_image` 结构化桥映射到 NanoSVG stop alpha。TEST36 用四面板及循环引用白底降级做直绘自动断言；TEST37 要求同一继承半透明径向 SVG 被 `<img>` 与 CSS background 发现两次但只 fetch/free 一次，两处中心/中点/边缘像素一致。C89 脚本零改动，VS2008 ARM 增量构建 0 错误。

TEST36 首次设备结果为 `unit=1 inherit=0 alpha=1/1 cycle=1`：除 SVG1.1 XLink 继承外，本批次其余直绘断言均已通过。根因是 XML DOM 中 `xlink:href` 是带命名空间的属性，不能用 qualified name 字符串调用 `dom_element_get_attribute`；修复改用 libdom 正式 API `dom_element_get_attribute_ns`，传 `dom_namespaces[DOM_NAMESPACE_XLINK]` 与本地名 `href`。测试 fixture 和 inherit 断言未放宽。

同日设备复验 TEST36-37：四面板依次显示线性单位坐标、XLink 偏心径向继承、线性透明 stop 与径向透明 stop；缓存页的 `<img>` 和 CSS background 显示两块一致的绿色场+半透明红焦点，因此修复后的 inherit 断言及单次 fetch/free 正式链均成立。SVG 渐变阶段在此收束，`fx/fy` 与 spread method 继续保留为 NanoSVG 光栅器限制。

随后联网读取当前 IANA `iana_website.80c103cc08b6.css`：窄屏 `article.sidenav`、footer navigation/custodian/legalnotice 的关键 padding/margin 使用 `var(--space-*)`，libcss 会丢弃整条声明。审计 NetSurf 最新 libcss `104d87f` 仍找不到 `var()`/custom-properties 实现，因此没有可直接抄入的上游补丁。新增兼容层只收集同表顶层精确 `:root` token，结构化处理注释/字符串、嵌套 fallback 与循环，设 128 token、16 层递归和 8 倍输出上限；TEST38-39 分别覆盖 computed color 语义和 IANA 同款 240/320px 25px inset + 正式 redraw。float 未重新启用。

TEST38-39 批次经 `c89ize.py` 检查两个 C 文件均为 0 修改；VS2008 ARMV4I 增量构建 0 错误，`positron_core` 与 `test_host` 各仅保留 libcss `fpmath.h` 三条既有 C4244。设备包 `C:\WMShare\Positron-next30` 已由用户确认：TEST39 竖横屏均为等距 25px inset，新的 TEST13 中导航、正文和注册表列也恢复可读。

随后审计同一份 IANA CSS：共见 22 处 `oklch()`、15 处 `calc()`。Oklab 转换矩阵采用 Bjorn Ottosson 公开域/MIT 参考实现；没有引入不适配 VS2008 的完整现代颜色库。新 `pcore_css_values.c` 只转换数值型 OKLCH，并求值同单位且不依赖布局上下文的 calc；混合 `%/px` 原样保留。TEST40 将颜色、alpha、变量 calc 的 `+ - * /` 和混合单位保留合并为一次自动验收。多组路由第 3 组提示也已压缩，避免 WM6 MessageBox 长文本异常。

TEST40 最终批次经 `c89ize.py` 检查三个 C 文件均为 0 修改。首次增量构建重编 core/test_host，各只有 libcss `fpmath.h` 三条既有 C4244；修正 22:10 fixed alpha 量化后，最终增量构建只重编 `pcore_css_values.c` 并重链接 test_host，两个项目均为 0 错误、0 警告。设备包为 `C:\WMShare\Positron-next31`，默认配置 `tests=40`。

用户随后确认 TEST40 OK，TEST13 的 IANA logo/header、导航、标题和页脚表格配色/间距均继续改善；但从页脚进入 `/numbers` 后，`Number Resources` 主内容左缘再次越出 viewport。联网读取原始 HTML/CSS 确认该子页是 `article.sidenav` 反向 flex，`main` 内 `#rir-map { display:grid }`，并含 `.dtable-wrap { overflow:auto }` 宽表格。NetSurf 3.11 只解析 grid display 值而没有 grid layout，本项目也未附加 box overflow scrollbar；block 降级的表格 min-content 因而向上钳住 flex item，反向定位产生负 x。

TEST41 修复只在 flex item 树中实际检测到 grid/inline-grid 降级盒时跳过错误的 block min-content 钳制，普通 flex 保持原规则；inline-grid 归入 inline-block 降级。fixture 同时覆盖隐藏 sidenav、25px 动态 padding、one-track grid、overflow:auto 宽表格、224/320px geometry 与正式 redraw。三个 C 文件经 `c89ize.py` 检查均为 0 修改；VS2008 ARM 增量构建两个项目 0 错误，core 7 条与 test_host 3 条均为既有 fpmath/layout float-to-int 警告。设备结果仍待确认，且不得表述为完整 Grid/gap/横向 scrollbar。

2026-07-13 用户截图确认 TEST41 竖横屏均保持主内容左右 inset，宽表格未再把页面推到负 x。随后移植 NetSurf 3.11 `desktop/scrollbar.c`；`c89ize.py` 只将 6 处 `plot_style_t` designated initializer 转为位置初始化，再次运行 0 修改。恢复 `descendant_x1/y1` 溢出判定、创建/销毁和祖先 offset，WM DOWN/MOVE/UP 经 `PCore_OverflowPointer` 转给箭头与 thumb。TEST42 合并离屏右箭头 16px 坐标断言和可见拖动页。首次构建只暴露 `box_is_float` 是上游文件内宏而非外部函数，改为等价 box-type 宏后增量构建 core/test_host 0 错误；TEST42 真机仍待确认。

2026-07-13 用户确认 TEST42 功能正常，横向 scrollbar、箭头与 thumb 链闭环。性能目标不是原生外观：该控件继续使用 NetSurf retained scrollbar 经 GDI plotter 绘制，避免为页面内每个 overflow 建立 WM child HWND。随后新增 `PCore_OverflowDirtyRect`，DOWN/MOVE/UP 后 host 只 `InvalidateRect` 对应 overflow viewport（文档 y 减当前 page scroll），不再整窗失效；TEST42 自动断言脏矩形小于 240x320 页面。`c89ize.py` 两次均 0 修改，VS2008 ARM 增量构建 core/test_host 0 错误，仅有既有 `fpmath` 警告。

2026-07-15 next53 真机确认 TEST46 table span 与同批 TEST13/17/41/42 其余功能正常，但截图暴露两个独立占位问题：host 无条件创建 `WS_VSCROLL`，短文档也保留右侧非客户区；Positron 每次公开布局都重建 box tree，而 NetSurf auto-height overflow 原本依赖上一次 reflow 留下的 descendant bounds，因此首轮横条会覆盖末行。next54 改为按文档高度动态切换宿主 `WS_VSCROLL`，并只对首轮已出现 auto-height 横条的树追加一次 layout；普通页面仍是单次布局。TEST42 新增 auto-height 红/绿宽表格及末行像素 guard。首次尝试桌面 `ShowScrollBar` 在 WM6 SDK 链接时报未解析外部符号，改用 style + `SWP_FRAMECHANGED` 后 Debug/Release ARMV4I 增量构建均 0 错误。

next54 设备结果证明追加的 layout 虽修复 TEST41 类 auto-height 占位，却也让同一树中的 fixed-height overflow 读取首轮 descendant extent 并改变既有控件位置，TEST42 在原右箭头坐标失败为 `used=0/0/0`；不要通过移动测试点击坐标接受这项回归。next55 在第二轮前把 fixed-height auto-overflow 的 `descendant_x1` 临时收回 border edge，最终 bbox 仍会恢复供 redraw 判断，因此只让 auto-height 容器新增 padding。截图像素还确认上游右箭头用 `rect.y0` 后黑色区域位于控件第 7-13 行，向下偏 2px；改为与左箭头一致的 `area.y0` 后，TEST42 自动要求黑色区域上下界之和为 16。C89 检查 0 修改，双配置增量构建 0 错误。

next55 已由用户确认全部正常，可作为新的 overflow 基线。next56 转向 NetSurf 表格归一化：若 TEST47 失败，先看红/白、绿/蓝四点中的哪一点不符。红或绿缺失通常表示匿名 cell 未承载 block；白变成红表示短行空 cell 未生成；蓝缺失表示显式 table-cell 被错误并入匿名 cell。不要改 EXPECTED 掩盖构盒错误。匿名样式来自 `css_select_default_style` 与父样式 compose，必须随 talloc box tree 释放。冻结的 TEST13 导航链未改。

2026-07-15 用户确认 next56 的 TEST47 与同批其余测试正常。next57 的 TEST48 期望 marker 顺序为两个 disc、circle、square、`3.`、`7.`、`5.`、`4.`；失败时先检查 `PCore_ListMarker` 返回的实际序号/文本/几何，不得修改 EXPECTED 回避 LI user-data 或 ordered-list 计数错误。当时仍使用 decimal-only stub；该限制随后已由 next60/61 的 `port_list_style_vs2008.py` 与 TEST50 解除，见下方后续记录。

2026-07-15 next60 已落实上述后续：`port_list_style_vs2008.py` 只接受已知 `list_counter_style`/`numeric` 字段，未知、重复或不平衡初始化会直接失败；所有非 ASCII 字符只允许位于字符串中并转成三位八进制 UTF-8。首次生成暴露 `c89ize.py` 会先把 `struct name {` 误判成未结束声明，从而把 aggregate 字段重排；根因是 aggregate 判断晚于 `DECL_LIKE`，现已提前。若以后生成文件的 struct 字段顺序变化、出现 C4047/C2078，必须修脚本并重新生成，不能手改生成文件或忽略初始化警告。

next57 的 `c89ize.py` 对 `pcore_box.c`、`pcore_select.c`、`test_host/main.c` 均报告 0 修改；Debug/Release ARMV4I 增量构建均 0 错误，只保留 libcss `fpmath.h` 既有 C4244。包位于 `C:\WMShare\Positron-next57`，配置已核对为 `tests=13,17,41,42,46,47,48`。

2026-07-13 导航第二阶段把外链 CSS、`<img>` 和应用外链 CSS 后发现的背景 URL 纳入同一 request 的分轮 worker。pending request 持有未交换 document、去重 URL、attempted 状态与原始字节；UI 只在 worker 停止后执行 parse/discovery/style/layout，成功 swap 后 core 已复制资源，失败/关闭则统一释放 request/document/resource。`test_host` 临时预算为 64 URL/2 MiB，限制的是提交前峰值而非 core ABI。TEST43 离线覆盖显式 origin 的 relative/root/absolute URL、去重、成功副本和失败只尝试一次；`c89ize.py` 为 0 修改，VS2008 ARM 增量构建 0 错误、3 条既有 `fpmath` 警告。真机 TEST43 与 TEST13 资源等待仍待确认。

用户截图确认修正后的 TEST29 三个样本正确：红色 nonzero 实心，蓝/绿 evenodd 中心为白色。随后 CSS background-image 按 NetSurf 现有边界接入：样式后的资源扫描读取 computed URI 并复用 document cache；构盒后设置 `box->background`；`redraw.c` 继续负责 position/repeat/clip；GDI plotter 参照上游 Windows frontend 展开 BITMAPF_REPEAT_X/Y。TEST30 的同步 fetch 是当前导航资源阶段的既有取舍，不代表 background-size、多层背景或异步资源事务完成。

2026-07-12 扩展 TEST19 首次真机结果：BMP/PNG/JPEG/GIF 都通过尺寸探测和 Draw 返回，但 PNG/GIF fixture 本身是黑色/透明 1x1，缺少视觉证明；旧 BMP 数组还只有 68 字节而头声明 70，宽容解码后颜色错误。同期 TEST20 从 96x72 退成小点，且 H2/p 颜色丢失：根因是 render window 首次 `WM_SIZE` 用 NULL author sheet restyle，覆盖了调用方 `hSheet`。现改为四种标准编码器 2x2 fixture（BMP 70/PNG 77/JPEG 694/GIF 46 字节），并由 `g_render_sheet` 在窗口生命周期内保留调用方 stylesheet；导航换文档时清空。增量构建通过，待复测 TEST19/20。

复测确认 BMP/PNG/GIF 四象限和 TEST20 的 96x72、边框、标题/段落颜色恢复；JPEG 仍呈橄榄色，不能称为轻微偏色。桌面解码旧 2x2 JPEG 得到的本来就是橄榄色，根因是 fixture 尺寸小于合理 DCT 色块而非 WM codec。现换成 16x16、每象限 8x8、quality=100、4:4:4 的 305 字节 JPEG；桌面象限中心为 `(254,0,0)/(0,255,1)/(0,0,254)/(255,255,0)`，增量构建通过待设备复测 JPEG 行。

设备复测确认新 JPEG 与 BMP/PNG/GIF 视觉完全一致，WM Imaging 四格式直接解码/绘制闭环。随后 TEST20 扩为四个 48x48 缓存 `<img>`，资源回调按 URL 提供 BMP/PNG/JPEG/GIF，自动要求 found/fetched/calls/matched/frees 全为 4，并通过正式 `box->object -> content_redraw -> plot_bitmap` 链；增量构建通过待设备确认。

2026-07-10 真机反馈：首次 TEST 19 使用内存 PNG 时，`PCore_ImageInfoFromMemory` 失败并显示“could not decode”。后续处理：去掉手写 `_WIN32_DCOM` 避免重定义警告，增加 `PCore_ImageLastError(stage, hr)`，并把 TEST 19 改为 2x2 BMP 基线。第二次真机反馈为 `stage=2 hr=0x80070057`，即 COM init invalid argument；WM6 SDK `winx.h` 把 `CoInitialize(x)` 映射为 `CoInitializeEx(x, COINIT_MULTITHREADED)`，所以 `pcore_wmimage.cpp` 已改用 `COINIT_MULTITHREADED`。2026-07-10 用户确认 BMP 基线正常；后续若 WM Imaging 又失败，看 stage：2=COM init，3=CoCreate factory，4=CreateImageFromBuffer，5=GetImageInfo，6=Draw。

2026-07-10：旧 TEST 18 已真机得到 `found=2 fetched=2`。后续缓存版测试必须再扫描同一文档，并确认结果仍为 `2/2`、fetch callback 总调用数仍为 2；只看第二次也成功不足以证明去重。

2026-07-11：TEST 13 打开 IANA Reserved Domains 的截图暴露正文、导航和页脚向左裁切。根因不是页面固定宽度：`pcore_select.c` 仅设置 `css_media.type=screen`，却把 `css_media.width/height` 留为 0；libcss 的 `@media (min/max-width)` 直接比较这些字段，于是所有断点按 0px 选规则。修复是从 `PCore_SetViewport` 的 unit context 填充媒体宽高，并在 Browse 的 `StyleDocumentEx` 前设置实际 client viewport。TEST 21 在 320px/299px 分别断言 `min-width:300px`/`max-width:299px`；若又退回 0px 就会选错规则并失败。这里的 299/300/320 仅为测试边界，运行时宽高和 DPI 都从设备 client/HDC 动态取得。

同一页面的第二轮截图仍有约 25px 左侧裁切，但 TEST 21 已通过，所以继续检查真实 CSS：IANA 的窄屏 `article.sidenav` 是 `flex-direction:row-reverse`、左右 padding 25px，且侧栏 `display:none`。`layout_flex.c` 反向主轴起点把 content width 减去 opposite padding，导致唯一的 main item 左移一个 padding。修复为 `leading padding + content size`，TEST 22 断言 224px viewport 中 main 必须为 `x=25,width=174`。

2026-07-11：用户真机截图确认 TEST 22 OK，随后 IANA TEST 13 的 `Example Domains` 左缘不再裁切。不要把这条结论扩大成“页面版式完整”：同一组截图的页脚/导航仍有拥挤、局部错位与替代方框。后续必须先用 computed style + `PCore_NodeBox` 缩成最小复现，不能仅凭观感继续修改 clip 或硬编码页面尺寸。已验证范围与完成条件统一记录在 `KNOWN_LIMITATIONS.md`。

2026-07-11：当前 IANA CSS 确认大量使用 `(width <= 1000px)` / `(width < 1200px)`，libcss 3.11 不解析这类 MQ4 范围。`PCore_ParseCSS` 现只在字符串/注释之外改写整数 px 的 `width <=` 和 `width <`，严格小于按 WM6 整数 client px 转成 `max-width:N-1px`。首次实现使用 `wsprintfA`，VS2008 编译通过但 WinCE 链接报 LNK2019；已改为内部十进制格式化并加溢出保护，随后全解决方案 9/9 构建通过。不要因构建通过跳过扩展 TEST21 的设备边界验证。

同日用户截图确认扩展 TEST21 OK。TEST13 中 `a□number`、`maintained□for`、`are□provided`、`require□the` 与线上 HTML 的源码 LF 精确对应；`Homepage□` 是 SVG alt fallback 后的格式化换行。根因不是字体或网络，而是 `pcore_make_text_box` 未移植上游 `box_construct_text` 的 normal/nowrap 空白折叠。现已折叠 ASCII 空白并用 `box->space` 保存词间距，TEST15 同时要求 `normal_ws=ok` 与 `pre_lf=kept`，防止全局删除换行；设备结果待确认。

首次设备结果为 `normal_ws=ok pre_lf=FAIL`，同时 TEST13 方框已消失且词间距正常。这不是普通空白修复回归，而是最小 `PCORE_UA_CSS` 漏了 HTML 默认 `pre { white-space:pre }`，导致 `<pre>` computed style 仍为 normal。已按上游 `resources/default.css` 补齐 `font-family:monospace; white-space:pre; margin-bottom:1em`；不得删除 `pre_lf` 反例来让测试变绿。

补齐 UA 默认后的用户截图确认 TEST15 OK：`normal_ws=ok pre_lf=kept`。随后 `WM_SIZE` 的滚动恢复从“旧像素值裁剪”改为按旧/新可滚动范围保持比例；扩展 TEST24 要求 0%、50%、100% 分别映射到新范围的 0%、50%、100%，并继续要求 restyle 不联网。

2026-07-11 用户截图确认扩展 TEST24 OK；真实 TEST13 在竖屏 `Further Reading / Domain Names` 区域旋转到横屏后仍保持同一阅读位置，旋转响应式与阅读进度闭环。随后导航第一阶段改为 worker 主文档 GET + `WM_APP` 完成消息；若设备仍卡顿，先区分“主 GET 期间进度条是否停止”和“GET 完成后资源/style/layout 提交是否短暂停止”，两者属于不同阶段。

导航第一阶段首次设备反馈：loading 条持续移动，等待主 GET 时旧页可滚动且成功后正常换页；网络失败未测。滚动时 loading 条会被 `ScrollWindowEx` 复制并随页面下移，timer 也因每 100ms 调用一次文档绘制造成轻微卡顿。修复后滚动矩形排除顶部 5px，纯 loading invalid region 只清除/重画固定条带，不再调用 `PCore_PaintDocument`；待设备复测复制残影与流畅度。

第二次设备反馈仍可见多条 loading 残影。下一版不再由父窗口 `WM_PAINT` 绘制进度条，改用独立 `STATIC` 子窗口并给 render window 加 `WS_CLIPCHILDREN`；timer 只 `MoveWindow` 子窗口。若该版仍复现，按已知视觉缺陷挂起，不再阻塞资源异步化主线。

`STATIC` 子窗口在设备上完全不可见。复核 WM6 SDK 后改用正确的 Common Controls 路径：`InitCommonControlsEx(ICC_PROGRESS_CLASS)`、`PROGRESS_CLASS`、`PBM_SETRANGE/PBM_SETPOS`，并链接 `commctrl.lib`。控件位于 render client 的 `y=0`，不是系统任务栏坐标；高度来自 `SM_CYHSCROLL` 且至少 6px。若 `CreateWindowExW` 失败，窗口标题会显示 `Positron render - loading`，便于区分创建失败与绘制/遮挡。

旋转调试注意：旧实现只调用 `PCore_SetViewport` 和 `PCore_LayoutDocument`，曾导致跨 CSS 断点时媒体规则沿用旋转前结果。当前 `WM_SIZE` 已改为 `PCore_SetViewport` -> cache-only `PCore_StyleDocumentEx2` -> `PCore_LayoutDocument`，并传入当前绝对文档 URL 与 WM resolver；若再出现问题，应分别检查缓存 URL 命中、style 重选和 layout。

2026-07-11：用户截图显示最终汇总 `Tests passed`，明确列出 ENGINE 的 TEST 6-11、15、16、18、21、22 全部通过。它是离线 HTML parse/select/style/layout、media-query viewport、反向 flex、box tree 及图片资源发现/document cache 的回归证据；不覆盖网络 Browse 或 GDI Render 组。

2026-07-11：IANA 页脚的 HTML 是 table cell 内的 `<ul>`，其条目规则为 `display:inline; float:left`。曾向 `pcore_box.c` 加入仿 NetSurf 的 `BOX_FLOAT_LEFT/RIGHT` 匿名包装，TEST23 最小复现通过；但真实 Browse 截图随即出现全页严重错位和替代方框。结论：该最小测试没有覆盖 pcore 精简构盒与上游 normalisation/list-marker 等前后条件。实现已撤回，TEST23 不再运行；必须先恢复真实页面基线，再以端到端回归重新设计 float 移植。

同日线上 IANA CSS 的文件名与此前版本不同，并使用 CSS custom properties 和 `@media (width <= 1000px)`。这不是 TEST21 的 `min-width` / `max-width` 断言所覆盖的语法；定位真实页问题前先确认实际抓取的是哪一个 CSS 版本，不能把旧截图结论外推到新站点资源。

2026-07-24：TEST13 起始页正常，但进入 IANA `/domains/reserved` 后标题只剩右侧字符，说明不是统一 viewport padding 回归。重新抓取当前页面与 `iana_website.3c174467e53c.css` 后确认结构为 `article.sidenav > main`，宽表格只包在 `.dtable-wrap { overflow:auto }` 中，没有 `/numbers` 的 Grid wrapper。用 Chromium 240/320px viewport 也测得 main 为负 x，根因是宽表格的 min-content 继续钳住 reversed-flex item；这不是网络、资源事务或 inline style 路径。next77 将 TEST41 的 grid-only 标记改为 min-content boundary，只在 horizontal、`flex-shrink>0` 且后代实际含 grid/inline-grid 或 `overflow-x:auto/scroll` 时跳过隐式 `box->min_width`，显式 `min-width` 仍先行。TEST59 用无 Grid fixture 在 224/320px 自动断言 `main x=25`；必须同时复测 TEST41/42 和真实 TEST13 深层链接，不能只看 TEST59 MessageBox。

同日 next77 设备结果：TEST59 与同批回归通过，`/domains/reserved` 竖屏主内容 inset 正常；在表格可见时旋转为横屏后，第一列表头 `Domain` 左移约 18px 并贴住 wrapper 左侧裁剪边界，第二列表头仍呈 700 字重。这个现象会让首个英文词看起来像字体/样式异常，但不能归因于缺少小语种字体。next78 在每次新布局树最终 layout 后显式将内部 scrollbar offset 归零，并用 `PCore_NodeScrollOffset`、`PCore_TableCellTextStyle` 扩展 TEST59：同一 DOM 先 224x300 后 320x200，逐次断言 wrapper offset 0、首格 18px padding、文本相对 x=18 及字重 700。字体范围保持符号与单色 emoji，不为此引入普通语言字体。

next78 真机失败后，上段候选结论作废：递归 `scrollbar_set(...,0)` 会走 NetSurf scrollbar 回调，不是可在布局末尾安全执行的字段归零。设备表现为 TEST13 横屏全部表格单元格间距/样式异常，随后 TEST56 `va=0/2/3/3` 并触发系统级 `test_host.exe` 异常。该行为、两个诊断 API 和同 DOM TEST59 已全部撤回；恢复后的 ARM DLL `.text` 与 next77 完全一致。先隔离复测 TEST56/59，进程稳定后才进入 TEST13。

工具链禁令：不要用 `C:\Program Files (x86)\Microsoft Visual Studio 9.0\VC\bin\dumpbin.exe` 检查 ARM PE。它会内部启动桌面 `link.exe /dump`，在当前机器因 `mspdb80.dll` 缺失弹系统错误。构建只走 `scripts\build.bat` / `scripts\stage.bat` 的 `Debug|Windows Mobile 6 Professional SDK (ARMV4I)`；PE 元数据或 section hash 用 PowerShell/.NET 直接读取。

2026-07-25：next79 设备确认 TEST56/59 OK，真实 `/domains/reserved` 也准确回到“竖屏正常、横屏仅首个 `Domain` 丢 inset/字重观感”的 next77 基线。当前 IANA HTML 的首行是普通四个 `<th>`；`.dtable thead th` 统一给 `padding:10px 14px;font-weight:700`，`.dtable th:first-child` 再给 18px 左 padding，不存在只把第一个表头改坏的站点规则。

继续对照仓库 libcss `select.c` 后找到明确生命周期错误：`css__get_parent_bloom` 在父节点无缓存时创建 node-data，调用 `set_libcss_node_data` 后把其中 bloom 指针返回给后续选择；Positron 旧回调却在 `set` 内立刻用 `CSS_NODE_DELETED` 释放整块数据，因此返回的是悬空指针。next80 改为 NetSurf 同型的 libdom user-data 持有，并在每次新 StyleDocument 事务前递归失效上一轮缓存，既保住本轮父/兄弟选择数据，也不跨 stylesheet/media 复用。TEST60 用两个相同 `Domain` 表头比较首格 18px/10px inset、第二格 14px/10px inset 和文字宽度，并在同 DOM 纵横屏各执行一次。C89 脚本 0 修改，VS2008 `Debug|Windows Mobile 6 Professional SDK (ARMV4I)` 增量构建 0 错误；包位于 `C:\WMShare\Positron-next80`，默认 `tests=56,58-60`。

next80 设备验收完成：TEST56/58/59/60 全部通过；真实 TEST13 `/domains/reserved` 在横竖屏截图中首个 `Domain` 均与第二表头保持一致的 inset、字重和基线，其余单元格与 overflow 滚动正常。该问题关闭；后续若再出现“仅第一个复合选择器节点丢样式”，先查 node-data 失效边界，不再改 scrollbar 状态或字体 fallback。

next81 审计 `positron_core.vcproj` 的实际 ARM 输入后确认，shim 覆盖的上游源只读取 `font_min_size`、`core_select_menu`、`remove_backgrounds`。旧全零宏让两个布尔值碰巧正确，却把 NetSurf 3.11 的 `font_min_size=85` 错成 0。新配置为实际读取和近期开关逐项命名，并通过 token-paste 让未知名称直接编译失败；`enable_javascript=false` 保持长期目标尚未启用的边界。TEST61 比较同字串的 1px/8.5pt/12pt 布局宽度。`c89ize.py` 为 0 修改，VS2008 `Debug|Windows Mobile 6 Professional SDK (ARMV4I)` 构建 0 错误；因头文件变化重编 layout/redraw 后重新显示其既有 C4244/C4013 警告，不是本批新增诊断。

2026-07-25 设备验收 next81：默认 TEST56/58/59/60/61 全部运行，未发现问题。最低字号具名默认与既有 table、inline restyle、flex overflow、selector node-data 门禁可共同保留。

next82 首次 forms 构建的 12 个语法错误均来自包含顺序：`form_internal.h` 间接使用 `nserror`，而 `pcore_box.c` 当时尚未包含 `utils/errors.h`。按 NetSurf `layout.c` 的顺序调整后，VS2008 `Debug|Windows Mobile 6 Professional SDK (ARMV4I)` 增量构建通过，core 仅有 3 个既有 C4244，test_host 为 0 警告。不要把这类错误误归因于 form gadget 本身，也不要调用桌面 x86 工具链。

next82 设备 TEST62 报 `cb=18x18 ink=300/300 radio=18x18 ink=200/200`；next83 改用 NetSurf `forms.c` 同款 `dom_html_input_element_get_checked/get_disabled` 后仍报 `306/306`、`200/200`。复核 `redraw.c` 后确认旧门禁指标错误：checkbox/radio 的未选中底面本来就是非白浅灰，选中 tick/blob 只覆盖并加深已有像素，非白像素总数可以完全不变。next84 不削弱几何、hidden 或视觉门禁，而是新增 `PCore_NodeFormControlState` 直接验证最终 gadget 0/1 状态，并将视觉指标改为 RGB 暗度总量；这样同时覆盖 DOM -> box 状态传递和 redraw 结果。

next84 设备 TEST62 自动门禁通过，四种静态控件均正确可见。可视验收指出控件与后续文字偏近：NetSurf `default.css` 的 toggle 特例只有 `padding: 0 0.1em`，并保留通用 input 的 1px margin。next85 只追加 `margin-right: 0.2em`，保持动态字号/DPI 语义；不要改成固定 WM 像素。

next85 设备截图确认四种控件状态、文字间距与 hidden-input 行为基本符合预期，作为新的静态 forms 基线。该验收不包含点击切换、焦点、编辑、select 或提交。

next86 在 `pcore_navigation_request` 内加入纯宿主遥测，不改变 worker/commit/swap：每轮 worker 累加 network，UI 分别计 parse/style/image-discovery/layout/首帧并保留最大单次 slice；资源侧记录 queued/fetched/failed、document/cache bytes、轮次和 2 MiB 预算拒绝数。`pcore_navigation_finish` 在释放事务前复制最后结果，TEST13 关闭后的既有 OK 框显示它。计时使用 `GetTickCount` 的无符号差，允许自然回绕；不要把累计 style/images 当成单次冻结时长。

next86 设备 TEST13 遥测为 total=6435ms、network=5503ms、max UI=673ms，parse/style/images/layout/paint=11/182/6/673/36ms；queued/ok/fail=2/2/0、rounds=2、document/cache=10499/121111 bytes、budget-rejected=0，其余批量门禁均 OK。总加载时间约 85% 在 worker 网络，连续 UI 停顿则集中在 layout；下一步应细分 box construction、首轮 layout、可选 settling pass 和收尾，不要先改网络线程或资源预算。

next87 在 `PCore_LayoutDocument` 内仅用 `GetTickCount` 记录 box construction、首轮 NetSurf layout、overflow 检查及可选 settling 二次 layout、finalize 和 total，并通过只读 `PCore_GetLayoutStats` 交给 TEST13。`settling_pass=1` 表示本次确实执行了第二轮 layout；`other` 是 total 扣除四个已知阶段后的调度/计时余量。设备 IANA 起始页报告 `580=515+65+0ms, pass=0`；进入 Reserved 后最后一次导航报告 `662=495+124+43ms, pass=1`，其余门禁均 OK。构盒在两页都稳定占约 500ms，下一批先细分构盒，不得为了缩短数字跳过既有 settling pass、修改几何或放宽回归断言。

next88 候选不改递归构盒语义：统计对象沿现有 block/inline/flex/table 调用显式传递，不使用 DLL 全局 profiler；旧 `pcore_box_construct` 保留为无统计包装。`tree` 与 background attachment 分开计时，tree 内再累计 computed-style lookup、text copy/whitespace、retained image decode、anonymous style compose 和 table span/gap normalisation；表格补空格的匿名样式只归 table，避免双重计时。`tree-other` 是 tree 扣除五项热点的余量。逐节点 `GetTickCount` 会增加少量观测开销，因此 next88 重点比较热点比例和调用数，不直接把总毫秒与 next87 当作性能回归。

next88 设备数据已定位到单张图片创建：IANA 起始页 tree/image=`523/518ms`，Reserved 为 `481/474ms`，backgrounds 均为 0，tree-other 仅 `4/1ms`。旧路径对 SVG 先调用 WM Imaging，失败后才进入现有 `PImage_CreateSvgFromMemory`，因此这不是 DOM 遍历或表格归一化瓶颈。next89 按字节 BOM/空白后的首个 `<` 做保守 SVG-first 分派，并把成功或失败的 retained handle 状态挂在既有 document image cache；二次 layout 的轻量 carrier 借用 document-owned handle。设备确认 TEST20/27 的 4/4 与 1/1 retained reuse 自动断言及其余默认门禁全部通过。TEST13 首次页面 image 仍为 `469ms`，随后 Reserved 页面降到 `37ms`；首屏 SVG 冷启动仍是后续诊断项。句柄必须在创建线程随 document 释放；这不是跨文档、跨导航或跨线程缓存。

TEST13 遥测现拆成 `overview` 与 `box detail` 两个短 MessageBox。前者保留网络、提交阶段、资源和 layout breakdown，后者显示构盒热点、调用数及 image reuse/markup-first；不要再把所有字段塞回一个 WM 小屏弹窗。

TEST13 的两个框读取导航完成时复制的 `g_nav_last_stats`。导航成功后再发生的 `WM_SIZE` 旋转布局不会回写该快照，所以“旋转后退出仍显示 reuse=0/markup-first=1”不能用来否定或证明旋转复用；同 document 复用由 TEST20/27 的显式二次 layout 断言负责。

next90 候选只读细分首次 SVG 创建：`positron_image` ABI 1.5 在每个 retained SVG 对象内保存 total/setup/libsvgtiny parse/NanoSVG retained raster 四段 `GetTickCount`，由 `PImage_SvgGetCreateStats` 按句柄读取，不使用 DLL 全局 last-call 状态。core 将成功对象的统计随现有 document image cache 保存并通过独立 `PCore_GetImageDecodeStats` 汇总，避免扩展已提交的 `PCoreBoxStats`。TEST27 要求一次创建、二次布局 reuse 且三段之和不超过 total；TEST13 box detail 用同样行数替换旧 calls 行。该批只回答冷启动花在哪里，不预热、不跨 document 缓存，也不改解析/绘制结果。

next91 无人值守日志确认 TEST27、IANA Example、Reserved 的 SVG 创建几乎都耗在 `svgtiny_parse`，但同类 IANA 资源在同进程内仍有 37ms 与 593ms 的巨大波动。next92 没有重写 parser，而是参考 NetSurf `hlcache` 的条目/使用者分离：旧页和新页同时存活时，URL、长度和双哈希一致的 SVG 共享 retained handle；每个 document 只持引用，归零立即释放。设备 Reserved 页为 `reuse=1, creates=0, image=2ms`，TEST63 又在释放首 document 后检查第二 document 的红绿像素。若以后该测试失败，先查 document user-data 析构是否重复释放或提前清空共享条目，不要改成无界全局缓存。

next93 设备无人值守日志确认 TEST13/20/27/43/44/56/58-64 全部 PASS。`PCore_FormActivateAt` 直接更新盒树 gadget 并调用 libdom `set_checked`，因此同一 document 旋转重排后状态仍在；radio 分组使用 `dom_html_input_element_get_form` 返回的 form owner 与 `name`，不要退化成全 document 同名互斥。disabled 或已选 radio 会消费点击但返回零尺寸 dirty rect。TEST62 继续只负责静态几何/暗度/hidden 基线，TEST64 才负责交互语义。

next94 设备无人值守日志确认 TEST13/20/27/43/44/56/58-65 全部 PASS。TEST13 三次真实导航均 `completed=1`；TEST65 通过真实 WM `EDIT` 的同步 `EN_CHANGE` 探针验证 UTF-16 宿主文本已写回 core/libdom，同时覆盖 password、maxlength、read-only/disabled、非法 UTF-8 拒绝和纵横屏重排保持。后续 textarea 应复用这套控件生命周期、滚动/旋转定位与换页前销毁顺序。

next97 将 textarea 纳入 `PCore_TextInput*` 枚举，并用新增只读 `PCore_TextInputIsMultiline` 查询类型，保持 next94 已公开的 `PCoreTextInputInfo` 结构布局不变；宿主据此创建 `ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL` 的 WM 原生 `EDIT`。初值送入控件前把 DOM LF 展开为 CRLF，`EN_CHANGE` 回写时由 core 将 CRLF/CR 归一化为 LF。next95/96 的设备失败证明多行 `SetWindowTextW` 不是可靠通知探针；next97 通过 `EM_SETSEL` + `EM_REPLACESEL` 模拟实际替换编辑，在真实 `EN_CHANGE` 回调中验收，并在 `GetWindowTextW` 前用 `EM_FMTLINES(FALSE)` 排除软折行字符。设备日志确认 TEST13/20/27/43/44/56/58-66 全部 PASS，next97 已提升为基线。

next98 按 NetSurf `form_option` 链建立 select 状态：option 文本折叠 ASCII 空白，value/selected/disabled 同步 libdom；错误的单选多 selected 归一化为首项，multiple 在 core ABI 中允许独立选择。WM 宿主只为单选创建原生 `COMBOBOX`，复用换页销毁、滚动定位和旋转重建。TEST67 的 WM 探针用 `CB_SETCURSEL` 选择第三项，再调用真实 `CBN_SELCHANGE` 所复用的 `pcore_native_select_changed`；它是自动宿主桥断言，不是手指展开下拉、视觉位置或 WM 多选列表验收。2026-07-30 设备日志确认 TEST13/20/27/43/44/56/58-67 全部 PASS，next98 已提升为自动化基线。

next99 参考 NetSurf `box_special.c::box_input/box_button` 与 `form.c::form_dom_to_data/form_url_encode`：submit/reset/button 使用正式 gadget 和 CSS 盒；successful controls 直接从 libdom form collection 读取 live value/checked/selected，百分号编码 UTF-8 字节，空格转 `+`，只加入被点击的 submit。GET 替换旧 query，POST 请求对象复制 body 并由 worker 调 `PHttp_PostEx`；换页成功后的 CSS/图片仍走 GET。TEST68 只构造宿主请求而不访问网络，既避免依赖公网回显站，也不把 multipart/file 冒充已实现。2026-07-30 设备日志确认 TEST13/20/27/43/44/56/58-68 全部 PASS。

next100 候选沿用 libdom 已保存的 `defaultValue/defaultChecked/defaultSelected` 做 reset，并在恢复后走正常重排来重建 NetSurf gadget 和 WM 控件；不要通过修改 TEST69 期望值掩盖 live/default 状态混淆。单行 WM `EDIT` 的 Enter 由子类窗口发送宿主消息，textarea 保留换行；无 submit 按钮的 form 仍提交其余 successful controls。label 解析只负责把显式/包裹式标签映射到现有控件，自动探针验证消息和焦点桥，不替代人工触屏/视觉验收。

next100 首次设备日志中 TEST13 深链和 TEST20/27/43/44/56/58-68 全部 PASS，TEST69 唯一失败于 reset。根因在 vendored libdom 0.4.2 `html_text_area_element.c`：首次 `get_value()` 成功后误写 `default_value_set = true`，导致紧随其后的“若 default 尚未保存则复制初始 value”分支永远不可达，同时 `value_set` 仍为 false。next101 把该行恢复为语义上应有的 `value_set = true`，不改 TEST69 期望，并把失败日志细分为 query/textarea/checked/options/submission。设备日志确认 TEST13/20/27/43/44/56/58-69 全部 PASS，next101 已升级为基线。

next102/TEST70 增加 file gadget 与 multipart；该包的 TEST13/20/27/43/44/56/58-69 全部 PASS，TEST70 仅在 reset 后仍见已选文件。根因是 libdom 对无初始 `value` 属性的 input 第一次 `set_value()` 会同步填入 `defaultValue`；next103 不改测试，而是在 file reset 时强制清空显示值和原始路径。设备日志确认 TEST13/20/27/43/44/56/58-70 全部 PASS，next103 已升级为基线。宿主仍必须以显式 body 长度调用 `PHttp_PostEx`；不可对 multipart 使用 `strlen`，也不可把原始本地路径写进 wire body。当前实现把整个请求体缓存在内存、文件 MIME 固定为 `application/octet-stream`。TEST70 不会自动点击 `GetOpenFileNameEx` 或验证公网 POST。

next104/TEST71 为 multiple select 创建原生 `LISTBOX`，不要把它误改成 `COMBOBOX` 或 Core 自绘菜单。触屏切换采用 `LBS_MULTIPLESEL`，CSS 高度依靠 `LBS_NOINTEGRALHEIGHT` 保持；`pcore_native_select_changed` 必须逐项同步，并在 Core 对 disabled option 返回 2 时恢复该行原状态。TEST71 的程序化 `LB_SETSEL` 走同一同步函数并在销毁/重建后再次读取原生状态；它不替代真实手指和视觉验收。设备日志确认 TEST13/20/27/43/44/56/58-71 全部 PASS。

next105/TEST72 首次接入 required 验证后，reset 只恢复 6 个 invalid。不要通过放宽计数断言掩盖它：libdom 0.4.2 会把无初始 `value` 属性的 text/password 第一次运行时 `set_value()` 误记为 `defaultValue`。next106 在 `PCore_TextInputSetValue` 写入前先读取并重写解析时默认值，使后续运行时编辑只改变 live value。设备日志确认八类 required、首个无效控件几何、显式提交/Enter 阻止、`novalidate/formnovalidate`、multipart 和 reset 全部通过。

next107/108 的 TEST73 报 `stage=2` 时六个颜色其实全部正确；该阶段还包含控件几何查找。next108 的 `geom=D`（checkbox/button/reset 有效，text 缺失）证明根因是空 text input 没有 CSS 尺寸时 border-box 为 0，不是 `:checked` 或 libcss callback 失败。next109 给动态状态夹具中的 text input 声明显式 80x20 CSS 尺寸，保留全部颜色、命中、旋转和 reset 断言；设备日志确认 TEST73 及 TEST13/20/27/43/44/56/58-72 全部 PASS。以后诊断 `stage=20+mask` 时，mask 位依次是 checkbox=1、text=2、button=4、reset=8；默认 text input intrinsic size 仍是独立兼容缺口。

next110/TEST74 对照 NetSurf 官方 libdom HEAD `f69781e` 后确认，上游仍会把 event target 同时放入祖先数组再执行 AT_TARGET，导致目标 listener 重复三次；同一实现还会无条件冒泡、允许不可取消事件被取消，并残留 `in_dispatch`。本地修复后 TEST74 要求严格顺序 outer-capture、parent-capture、target-capture、target-bubble、parent-bubble、outer-bubble，同时检查 non-bubbling、两种 stop、listener remove 和坐标派发。设备日志确认 TEST74 与 TEST13/20/27/43/44/56/58-73 全部 PASS。若以后更新 libdom，不能只看能否编译，必须保留这组行为门禁。

2026-07-11：为旋转跨断点新增 document-owned 外链 CSS 原始字节缓存。首次 `StyleDocumentEx` 成功 fetch 后复制数据；后续 restyle 只从缓存重新解析，`WM_SIZE` 传入的 callback 永远失败，作为“禁止联网”的防线。缓存上限为 32 份、单份 256 KiB、每 document 合计 512 KiB。重样式替换 node user-data 时还会显式释放旧 `css_computed_style`，因为 libdom 替换 user-data 不调用旧析构回调。用户已确认 TEST24：320px 首次 fetch 选绿色，299px cache-only restyle 选蓝色，fetch/free 计数都保持 1；真实 Browse 旋转也已验收。2026-07-14 的 `StyleDocumentEx2` 将成功 `@import` 字节纳入同一缓存，TEST45 覆盖导入树 cache-only 重选。

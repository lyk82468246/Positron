# Positron

面向 **Windows Mobile 6 Professional**（Windows CE 5.2, ARMv4i）的现代基础设施与应用运行时。

Positron 一方面提供可被任意 WM 程序独立调用的现代 DLL 集合，包括 TLS、HTTP、JSON、图片与渲染核心等能力；另一方面在这些基础设施上建设自带浏览器内核和 Electron-like 应用运行时。当前主线已经进入 HTML/CSS 真实渲染：NetSurf 3.11 的解析、样式、layout/redraw、GDI 绘制和点击导航都在 `positron_core.dll` 后面跑通；JavaScript 是长期必须实现的目标，但尚不是当前可用能力。

公共 DLL 是正式产品，不只是 `test_host.exe` 或浏览器的内部依赖。架构与 ABI 原则见 [.agents/ARCHITECTURE.md](.agents/ARCHITECTURE.md)。

> **Browse 冻结基线（2026-07-15）**：导航产品路径曾完整恢复到 `9c5c7c7`/next37，并由 next44 确认 TEST13 从 start page 到 IANA 深层导航全流程正常。此后 `main` 已继续加入图片、字体、列表和表格能力，但没有重新合入 next38 之后失败的 stylesheet metadata、base URL、redirect origin 与 timeout 实验；这些历史保存在 `codex/post-next37-experiments`。详见 [.agents/ROLLBACK_NEXT37.md](.agents/ROLLBACK_NEXT37.md)。

---

## 当前状态

| Phase | 内容 | 状态 |
|---|---|---|
| **1** | `positron_tls.dll` — TLS 1.2 客户端（mbedTLS 2.16 LTS） | ✅ 完成，WM6 Emulator 验证 |
| **2** | `positron_json.dll` (cJSON 1.7.18) + `positron_http.dll` (HTTP/1.1：HTTPS via mbedTLS，明文 HTTP via WinInet) | ✅ 完成，WM6 Emulator 验证 |
| **3** | 嵌入式 CA bundle + verified TLS (`PTls_ConnectVerified`) + CryptGenRandom 熵源 | ✅ 完成，WM6 Emulator 验证 |
| **4** | `positron_core.dll` — NetSurf 内核移植（HTML/CSS 渲染层） | 🚧 正式 Browse 路径已走 NetSurf `layout.c/redraw.c`；flex、table、border、selector、缓存图片链、CSS 背景图与 NetSurf overflow scrollbar 已真机验证，窄屏复杂布局仍待补 |
| **5** | `positron_image.dll` — 可复用图片基础设施 | 🚧 retained 解码、SVG、PNG/JPEG/BMP/GIF 与原始像素入口均已真机闭环；当前 ABI 1.5 增加只读 SVG 创建阶段遥测，next52 原生标题栏 OK 真退出已真机确认 |

Phase 3 验证：`test_host.exe` 的通信组——HTTPS GET（`checkip.amazonaws.com`，大陆直连纯文本 IP）、POST（postman-echo）、badssl.com 正样本 + expired + self-signed 三连测，全部真机通过。详见 [PHASE3.md](PHASE3.md)。

Phase 4 进展：vendoring NetSurf 3.11，五个底层库（libwapcaplet / libparserutils / libhubbub / libdom / libcss）全部在 VS2008 / WinCE / ARM 下编译通过（C99→C89 脚本化转换，见 `scripts/c89ize.py` 等）。`positron_core.dll` 已作为产品级引擎边界立起，公开 `PCore_ParseHTML/ParseCSS/StyleDocumentEx/StyleDocumentEx2/LayoutDocument/PaintDocument/LinkAt/FormActivateAt` 等小巧 opaque-HANDLE API。HTML→DOM、CSS 解析、CSS select/computed style、整树样式、外部 `<link rel="stylesheet">` 抓取、GDI 窗口绘制、垂直滚动、viewport/DPI 自适应、点击命中与导航、checkbox/radio 基础交互、HTTPS verified fetch、明文 `http://` via WinInet、跨协议重定向、完整 Mozilla CA bundle 均已真机验证。next94 新增 transport/UI 无关的 `PCore_TextInputInfo/SetValue` 与 WM 原生单行 `EDIT` 宿主桥；next97 又在保持结构 ABI 不变的前提下通过 `PCore_TextInputIsMultiline` 接通 textarea 与 WM 多行 `EDIT`，TEST65/66 均已通过设备验收。`StyleDocumentEx2` 新增文档基准 URL 与宿主解析回调；CSS `@import` 使用 libcss 原生 pending/register API，WM 宿主用 `InternetCombineUrlA` 规范化相对 URL，TEST45 已真机通过。

当前 Browse 正式路径已经从早期手写块流布局切到 **NetSurf 真实布局/重绘引擎**：`PCore_LayoutDocument` / `PCore_PaintDocument` / `PCore_LinkAt` 走 `pcore_box_construct` → NetSurf `layout_document` → `html_redraw` → GDI plotter。M7-flex/table、M5f border、CSS attribute/sibling selector 与 `:link` / `:lang()` 已由 TEST 9/17 真机验证。TEST 11 的 margin collapse 与 `padding-top:1px` 阻断折叠成对断言已于 2026-07-10 真机通过。`<img>` alt fallback 已由 TEST 17 验证；TEST 18 的文档级资源缓存与 URL 去重、TEST 20 的 BMP/PNG/JPEG/GIF 缓存 replaced box/`content_redraw`/`plot_bitmap` 绘制均已真机通过。TEST 21 已验证运行时 viewport/DPI 及整数像素 MQ4 `(width <= Npx)` / `(width < Npx)` 的 320/300/299px 边界。TEST13 已确认 `white-space:normal/nowrap` 的源码换行被正确折叠、词间距正常；TEST15 又确认 `<pre>` 换行仍保留。TEST 22 已验证反向 flex 的 25px leading padding；TEST38-39 进一步关闭了 IANA 顶层根变量造成的窄屏间距问题，当前截图中的导航、正文和注册表列均已可读，但其他真实子页仍需持续观察。Browse host 在布局前使用同一 HTTP 获取器填充 `<img>` 缓存，失败仍保留 alt/src 回退。SVG parse/draw/cache/fallback 已由 TEST25-28 真机通过，TEST13 的 HTTPS HTML + 相对 SVG 网络 fixture 也已显示正确。详见 [PHASE4.md](PHASE4.md)、[.agents/ROADMAP.md](.agents/ROADMAP.md) 和 [.agents/KNOWN_LIMITATIONS.md](.agents/KNOWN_LIMITATIONS.md)。

当前可用能力：TLS/HTTP/JSON 通信栈；HTML/CSS/DOM 解析；CSS select + computed style；整树样式；外链 CSS；NetSurf real layout/redraw；GDI plotter；滚动、viewport/DPI 自适应、点击链接导航；flex、常见 table、border、CSS attribute/sibling/static-pseudo selector、`<img>` alt fallback 与 `<img src>` 资源发现/fetch。WM Imaging 的 BMP/PNG/JPEG/GIF 与缓存 `<img>` 链已真机验证。`positron_image.dll` 公共 C ABI 已接通 WM Imaging、Expat、libdom XML、libsvgtiny 与 NanoSVG rasterizer；`PImage_CreateBitmapFromMemory/BitmapGetInfo/DrawBitmap/FreeBitmap` retained 位图对象会复制输入字节，NetSurf 图片载体也复用同一解码对象。2026-07-15 的 TEST19/20 已确认四格式颜色、清空调用方缓冲后重复绘制、损坏输入拒绝、旧核心 ABI 转发与正式缓存链；TEST26/27 和 TEST13 同批无回归。

最新设备反馈（2026-07-16）：next44 的 TEST13 全流程确认 next37 恢复点可作为 Browse 冻结基线；next45 又确认公共位图 ABI 的 TEST19 四格式、TEST20 缓存图片、TEST26/27 SVG 与 TEST13 全流程均正常。next46 的 ABI 1.0 独立 WM 示例也已在横竖屏确认，SVG 曲线缩放后的平滑观感略逊于先前大图但可接受。next47 已确认 ABI 1.1 的 PNG/JPEG 内存编码、DLL 配套释放及重新解码闭环；next48 证明 WM `EncoderQuality=100` 仍无法修复小图色度串扰。next49 已确认静态 libjpeg-turbo 1.5.3 的显式 quality JPEG 为正确 4:4:4，行方向、红绿蓝黄颜色与 PNG 一致，大面积色带消失。Debug DLL 相比 next48 增加 243,712 字节（约 238 KiB），设备不需额外 JPEG DLL；额外 CPU 与约 `width*height*3` 的主要中间像素内存只发生在显式 JPEG 编码。next50 的六项截图又确认 ABI 1.3 的 padded BGR24、straight-alpha BGRA32、RGB/alpha PNG、JPEG 与 SVG 一致。next51 进一步证明 ABI 1.4 的 BMP/GIF 隐藏编码、签名与回读检查通过，但也证明标题栏 X 是 Shell Smart Minimize，不保证发送 `WM_CLOSE`。next52 改用 WM/Pocket PC 原生 `SHDoneButton(SHDB_SHOW)` 标题栏 OK，并在 `WM_COMMAND/IDOK` 销毁窗口；用户已确认点击 OK 后进程消失且可以正常再次启动。next53 又确认 TEST46 四行三列表格 span 颜色/位置正确，TEST13/17/41/42 其余功能正常。next54 虽让 TEST41 的 auto-height 横条获得独立空间并去掉短页无效纵条，却因第二次整树 layout 同时改变 fixed-height overflow 几何而令 TEST42 自动断言失败；next55 已限制二次 layout 只影响 auto-height 容器并修正右箭头坐标，用户现已确认 TEST41/42、短页纵条与色块页全部正常。next56 的 TEST47 红/白、绿/蓝两行及同批其他测试现已确认正常。next57 的 TEST48 已确认列表层级和有序计数语义；next58/59 的随包字体最终让基础箭头、marker 与五个单色 emoji 可见。next61 的 TEST50 已确认 IV/z/aa/09 计数、绿色缓存 SVG marker 与圆形失败回退全部通过。next62/TEST51 与 next63/TEST52 又依次确认 inline-first 和 block-first/空条目/嵌套/图片的 inside marker 流在横竖屏符合预期。next64/TEST53 的纵横屏截图现已确认 collapsed border 的宽度、样式、hidden、来源 tie 与 separate 对照均符合预期；next65/TEST54 又确认 finite/auto rowspan、colspan 与 row-group 四组终止边正确。上述改动均没有重启 next38 之后的导航实验。

当前明确缺口：位图四格式与 SVG 网络/缓存/fallback/fill-rule/基础渐变缓存链已经闭环，但径向焦点 `fx/fy` 与 spread method 仍是 NanoSVG 光栅器的显式 TODO。CSS Variables 兼容层只替换同一 stylesheet 顶层精确 `:root` token，不支持元素作用域、跨 stylesheet cascade 或 `@property`。现代值兼容只处理数值型 `oklch()` 到裁剪 sRGB，以及无需布局上下文即可完全求值的同单位 `calc()`；混合单位、`color-mix()` 和完整 CSS Color/Values 仍未支持。CSS Grid 目前只是保持文档顺序的单列 block 降级，TEST41 只防止 grid 内宽表格推走整个 flex 页面，不代表网格轨道或 gap 已实现。标准 NetSurf overflow scrollbar 已由 TEST42/next55 验收，但不包含触摸惯性或 overlay scrollbar。CSS `@import` 的嵌套解析、失败空表回退和文档缓存已由 TEST45 验收；它尚不代表跨源策略、完整缓存失效或整页资源进度已完成。有效表格的 span 占位、匿名 row/cell、collapsed-border 冲突、cell vertical alignment、`empty-cells`、显式 table height 与百分比 row 第二遍已由 TEST46/47、TEST53-57 真机确认；`col`/`colgroup` border 来源、百分比 cell/后代内容和跨行 baseline 仍未覆盖。正式 Positron 构盒走 `pcore_select.c` + `pcore_box.c`，并不调用 NetSurf `box_construct.c`；此前 HTML `style=` 缺失的直接原因是 `pcore_style_subtree` 向 `css_select_style` 固定传入空 inline sheet，而不是 `nsoption_bool(author_level_css)`。next75/TEST58 已确认 NetSurf 式 inline stylesheet 能通过 cascade、继承、`!important`、错误恢复、后代 class 选择及正式布局/重绘。next81 已将全零 `nsoption` shim 改为具名专家默认：当前实际读取的 `font_min_size=85`、`core_select_menu=false`、`remove_backgrounds=false` 对齐 NetSurf 3.11，JavaScript 继续显式关闭，未审计名称会直接编译失败；TEST56/58-61 已由设备确认无异常。列表 marker 的 47 种上游 counter formatter、缓存 `list-style-image`、失败回退和 inside 首行流已由 next61-63/TEST50-52 验收；float 邻接 marker 与自定义 `@counter-style` 仍未完成。next93/TEST64 已完成 checkbox/radio，next94/TEST65 已在设备确认单行 text/password；next97/TEST66 已完成并在设备确认 textarea；next98/TEST67 已确认 NetSurf select option 链、单选/多选 core 状态 ABI 与 WM 原生单选下拉；next99/TEST68 已确认 NetSurf button gadget、successful-controls 编码及 WM GET/URL-encoded POST。WM 多选列表、label 激活、multipart/file、reset 恢复、Enter 隐式提交和完整事件系统仍未实现。字体 fallback 的当前范围只包括符号和单色 emoji，不计划扩展普通语言/多语种字体。`background-size`、多层背景和脚本资源仍未实现。UI 提交已在 parse/style/image-discovery/layout 四个调用之间让出 WM 消息循环，单个不可重入调用仍可能短暂卡顿。复杂 SVG text、动态状态伪类、float 和其余 forms/widgets 仍不完整；JavaScript 尚未启用。路线采用“存在性优先”：下一主线补 multipart/reset/事件状态与重大布局缺口，随后利用仓库已有 NetSurf Duktape backend 做中期 JavaScript 最小纵切；首屏 SVG 性能、抗锯齿和高级视觉边角后置。

next59 的 TEST49 已确认四个箭头不再 tofu、marker 和五个单色 emoji 均可见，视觉比 next58 稍有改善。next60 首次 TEST50 显示 `found=4`，核查确认不是当前源码逻辑失败，而是 staging 在最后一次 Debug 增量编译后又修改了 `pcore_select.c`，最终错误地组合了新 `test_host.exe` 与旧 `positron_core.dll`。next61 重新增量编译后 TEST50 已通过；`stage.bat` 现会先执行同配置增量 Build，失败时不再复制任何产物。next62 的 TEST51 与 next63 的 TEST52 均已由横竖屏截图确认，inside 文本/图片 marker、悬挂换行、block-first、空条目和嵌套布局符合预期。

next58/59 的随包单色 symbol/emoji fallback 是宿主基础字体，不等于网页字体：`@font-face` 下载、复杂 emoji ZWJ/variation shaping、彩色字体仍未实现。`ANTIALIASED_QUALITY` 是向 WM GDI 提出的灰度抗锯齿请求，最终效果仍由设备/OEM 字体光栅器决定。

next66 的 TEST55 首次真机运行读到 `FFFFFF/00C300/C6C300`：隐藏格为白、强制 show 格为绿、filled 格为青，功能分类正确；失败是 WM compatible bitmap 的 3-6 色阶量化与过严精确 RGB 断言共同造成的假阴性。next67 只将 TEST55 改为逐通道紧容差，core layout/redraw 不动。TEST13 Further Reading 新出现的圆点来自 IANA 页面真实列表项与已验收的 marker 支持，不是表格对齐回归。

next67 的 TEST55 已通过自动断言，设备截图也确认 top/middle/bottom、大小字体共基线、rowspan 底对齐及白/绿/青 empty-cells 正确。截图另外暴露测试页的四组固定高度刚好超出 WM 客户区十几像素，产生几乎填满轨道的纵向滚动条。next68 已压缩 TEST55 的行高/间距并显式设定标题行高，没有用滚动条掩盖可见内容。

next68 已由设备验收：TEST55 在竖屏客户区内完整显示且不再产生多余纵向滚动条；TEST56 的 105px 三行表与 70px 两行表按预期等比分配行高，top/middle/bottom 和跨行单元格底对齐正确。同期 TEST13 长页面滚动与 IANA 页脚布局保持正常。

next69 首次百分比 row 第二遍得到错误的 `20/30/30`。随后在多个共享目录包之间切换时出现的 TEST56 异常，最终由失败文本版本不符及 next72 同包 TEST56 通过证明为 WM/CE 全局 DLL 复用导致的 EXE/DLL 混搭。next72 的 TEST57 `styles=0:0` 暴露 inline `style=` 未参与正式选择；next73 将夹具改为外部 author stylesheet 后，用户确认 TEST55/56/57 通过，第一张表约为 20/40/20，超约束表为 25/25。后续架构复核确认正式路径没有调用受 `author_level_css` 控制的 NetSurf `box_construct.c`，真正缺口是 `pcore_style_subtree` 固定传 `NULL` inline sheet。

next74 首次接入 inline sheet 后，设备在 TEST56 报告 `va=0/2/3/3`：三行与两行高度仍正确，但 `.distributed .top` 的通配祖先 class 复合选择丢失。TEST56 原夹具和断言未改；next75 修复选择器回调对通配 qname `*` 的祖先/父节点匹配，并在 TEST58 增加独立 `.scope .probe` 断言。2026-07-24 设备确认 TEST56 与 TEST58 均通过；TEST58 可见页的 cascade 文本和 25/50/auto 三行布局符合预期。随后 TEST13 起始页和其余回归正常，但 IANA `/domains/reserved` 等宽表格子页仍会把主内容推到负 x。next77 只允许“横向、可收缩 flex item 且后代含 Grid fallback 或 `overflow-x:auto/scroll`”越过隐式 min-content 钳制，设备确认 TEST59 与同批回归通过、竖屏子页边距恢复；然而同页旋转为横屏后，首个英文表头 `Domain` 内容左移约 18px。next78 尝试递归 `scrollbar_set(...,0)` 后把异常扩大到全部表格单元格，并令 TEST56 失败、触发系统级 `test_host.exe` 异常，因此已经完整撤回。next79 已恢复 next77 机器码，设备确认 TEST56/59 均通过，真实页也准确回到“仅横屏首个 `Domain` 异常”的原始状态。next80 修复了 libcss 节点数据被过早销毁而留下父 bloom 悬空指针的问题；新增 TEST60 在同一 DOM 纵横屏重选时自动检查首表头的 18px/10px inset 与粗体宽度。2026-07-25 设备确认 TEST56/58/59/60 全部通过，真实 TEST13 `/domains/reserved` 的首个 `Domain` 在横竖屏均恢复正常 padding、字重和基线。该问题与非拉丁字体覆盖无关，普通语言/多语种字体明确不在当前开发范围。

next85 已建立只读 checkbox/radio 的静态 forms 基线。next86（提交 `210611d`）又在不改变导航控制流的前提下加入宿主侧阶段遥测；设备 TEST13 实测 total/network/max-UI=6435/5503/673ms，parse/style/images/layout/paint=11/182/6/673/36ms，说明总时长主要消耗在网络，而最长连续 UI 停顿集中在 `PCore_LayoutDocument`。next87（提交 `3b2446c`）进一步把该调用拆成 box construction、首轮 layout、overflow settling/可选二次 layout 与 finalize 计时；IANA 起始页得到 `580=515+65+0ms`（无二次布局），进入 Reserved 子页后最后一次导航得到 `662=495+124+43ms`（发生二次布局）。next88 的设备数据将两页构盒进一步定位到单张图片创建：tree/image 分别为 `523/518ms` 与 `481/474ms`。next89（提交 `ef0cc06`）用既有 SVG API 优先处理 XML-like 字节，并在同一 document 内复用 retained handle。next90 的 image ABI 1.5 与独立 core 统计在 next91 自动设备日志中确认创建耗时几乎全在 `svgtiny_parse`。next92 按 NetSurf 的“缓存条目与使用者分离”模型，让同时存活且 URL、长度、双哈希一致的文档共享 SVG；引用归零立即释放，不形成常驻缓存。设备日志确认 Reserved 页 `image reuse=1`、`svg creates=0`，图片阶段由前页的 523ms 降至 2ms，新增 TEST63 也通过释放首文档后的像素绘制检查。

next93 将静态 forms 基线推进到第一段真实交互：宿主先于链接命中消费 checkbox/radio，core 按 NetSurf 上游语义同步 `selected` 与 libdom checked 状态；同一 form owner、同名 radio 互斥，不同组和不同表单隔离，disabled 控件不改变。TEST64 还要求局部 dirty rect 有效，并在 240×320 到 320×240 重排后逐项复核状态。设备无人值守日志确认 TEST13/20/27/43/44/56/58-64 全部 PASS。

next94 把单行 text/password 输入接到同一表单状态层：core 生成 NetSurf `GADGET_TEXTBOX/PASSWORD` 盒并提供 UTF-8 枚举/写值 ABI，`test_host` 在其 border-box 上放置 WM 原生 `EDIT` 子窗口，滚动时移动、旋转时重建并保留焦点，换页前先销毁旧控件。TEST65 自动验证 maxlength、read-only、disabled、非法 UTF-8、DOM 重排保持，并用真实 `EN_CHANGE` 探针检查宿主消息桥。设备无人值守日志确认 TEST13/20/27/43/44/56/58-65 全部 PASS，next94 已提升为设备基线。

next97 沿用同一状态桥与原生控件生命周期，把 `<textarea>` 接为 `GADGET_TEXTAREA` 和 WM 多行 `EDIT`。core 将 WM 的 CRLF/CR 统一为 DOM LF，宿主启用多行换行、纵向滚动与回车输入；TEST66 覆盖初值、readonly/disabled、非法 UTF-8、CRLF/LF 归一化、真实 `EN_CHANGE` 和纵横屏重排保持。next95/96 证明 WM 多行 EDIT 的程序化 `SetWindowTextW` 不能作为可靠通知探针；next97 改由 `EM_REPLACESEL` 模拟编辑，并在真实通知回调中验收。设备无人值守日志确认 TEST13/20/27/43/44/56/58-66 全部 PASS，next97 已提升为设备基线。

next98 按 NetSurf `box_select`/`form_option` 模型构造 `<select>`，option 文本折叠空白、value/selected/disabled 与 libdom 同步，最宽 option 参与正式 layout。新增 `PCore_SelectInfo/SelectOptionInfo/SelectSetOptionSelected` 独立 ABI，不改变 next94/97 的文本结构；WM 宿主以原生 `COMBOBOX` 覆盖单选 border-box，并复用滚动、换页销毁和旋转重建。2026-07-30 设备自动日志确认 TEST13/20/27/43/44/56/58-67 全部 PASS；TEST67 检查错误的多 selected 归一化、disabled policy、multiple core 状态、DOM 重排持久化与原生下拉桥。该 next98 基线当时尚不包含多选 WM 列表和提交；下述 next99 在此基础上继续推进。

next99 把按钮和基础提交接入正式路径：`input[type=submit/reset/button]` 与 `<button>` 使用 NetSurf gadget、CSS layout/redraw 和文档坐标命中；`PCore_FormSubmissionAt` 按上游 `form.c` successful-controls 规则生成 UTF-8 `application/x-www-form-urlencoded` 数据。WM 宿主把 GET 数据替换到 action query，把 URL-encoded POST 交给既有 `PHttp_PostEx`，旧页在 worker 完成前保持可见。TEST68 成批覆盖 disabled/无名/未选控件过滤、multiple select 重复字段、仅提交被点击按钮、GET/POST 请求所有权、超长目标拒绝及 multipart 明确拒绝。2026-07-30 设备自动日志确认 TEST13/20/27/43/44/56/58-68 全部 PASS，next99 已提升为设备基线。multipart/file upload、Enter 隐式提交、reset 恢复、label 激活和完整事件系统尚未包含。

---

## 工具链

- **编译器**：MSVC 9.0（VS2008 SP1，C89-only，无 C99/C++11）
- **SDK**：Windows Mobile 6 Professional SDK (ARMV4I)
- **目标 Subsystem**：`windowsce,5.02`
- **链接库**：`ws2.lib`（WinCE 版 Winsock2，非桌面 `ws2_32.lib`）
- **加密库**：mbedTLS **2.16.12**（历史 2.16 LTS 系列的 WM6/MSVC9 兼容固定版本，不表示当前仍受上游维护；尝试过的 2.28.10 含 MSVC9 无法直接编译的 C99 声明）。当前 verified 路径显式使用 `MBEDTLS_SSL_VERIFY_REQUIRED` 并调用 `mbedtls_ssl_set_hostname()`；迁移到仍受维护版本仍是安全性中期目标。
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
    mbedtls/                    完整 vendored mbedTLS 2.16.12 源与许可证

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

  assets/fonts/                 WM GDI 随包静态 symbols/mono emoji fallback 字体

  positron_expat/               Expat 2.8.2 静态库及 WM/VS2008 适配
  positron_libsvgtiny/          NetSurf libsvgtiny 静态库
  positron_image/               可供任意 WM 程序调用的图片 DLL（WM 位图 + SVG retained C ABI）

  samples/positron_image_demo/  仅依赖 positron_image.dll 的独立 WM C 示例

  test_host/                    端到端测试 EXE（分通信/引擎/GDI渲染/Browse 组）
    main.c
    test_host.vcproj

  compat/                       VS2008 + WinCE 缺的 C99 shims
    stdint.h
    inttypes.h

  scripts/
    stage.bat                   增量构建并把 6 个二进制拷到 C:\WMShare\
    stage_image_demo.bat        只打包图片 DLL 与独立示例

  .agents/                      Codex 接手交接、调试纪律、路线图
```

---

## 编译

### 一次性准备

1. 安装 **VS2008 SP1** + **Windows Mobile 6 Professional SDK** + **WM6 Pro Emulator**。
2. Clone 本仓库。mbedTLS、cJSON、NetSurf、Expat、libjpeg-turbo、NanoSVG 和字体源均已固定版本并随仓库提供，不需要额外下载源码。
3. 可先运行 `python scripts\audit_repo.py`，确认 14 个 VS2008 工程引用的源码和关键许可证都存在且已被 Git 跟踪。

VS2008、WM6 SDK、模拟器和设备镜像是外部专有工具链，不能随本仓库再分发。第三方版本和许可证清单见 [THIRD_PARTY.md](THIRD_PARTY.md)。

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
scripts\stage.bat         :: 默认先增量构建 Debug，再打包
scripts\stage.bat Release :: 或 Release
scripts\stage.bat Debug C:\WMShare\Positron-next :: 旧进程锁文件时隔离 staging
```

把 6 个二进制、测试配置及 `fonts` 子目录拷到 `C:\WMShare\`。

模拟器内 Start → File Explorer → **Storage Card** → 双击 `test_host.exe`。

### 测试入口

`test_host.exe` 启动后先选择测试组。部署时必须保留 EXE/DLL 同级的 `fonts` 子目录：

快速复测可在 `test_host.exe` 同目录放置 `test_host.ini`：

```ini
# 支持逗号、空格、范围，以及特殊编号 7b
auto=1
tests=13,20,27,43,44,56,58-66
```

`auto=1` 启用无人值守 testbench：不显示 Yes/No/OK，按编号升序运行，所有原始 INFO/ERROR 与 TEST13 每次导航遥测写入 EXE 同目录的 `test_host.log`（每次启动覆盖）。可视测试窗口至少完成一次 `WM_PAINT` 后正常关闭；TEST13 自动经过 example.com、IANA Example Domains 和 IANA Reserved Domains。自动模式验证已有断言、资源计数和首帧可绘制性，**不等价于人工检查字体、抗锯齿和版式观感**。设为 `auto=0` 时仍先提示是否只运行配置项；选 No 完整保留原 All/四组流程。文件缺失时直接走旧流程，文件存在但无效时提示并忽略。TEST23 已撤回，配置中出现 23 会被拒绝。`scripts\stage.bat` 会先调用同配置的 VS2008 增量 Build，再复制配置及三份静态 symbol/emoji fallback 字体；构建失败不会留下混合版本包。

测试交付默认按能力批次进行：先积累多项相关实现、自动像素/资源/安全断言和直绘/正式链两层回归，再请求一次设备验收。只有真实编译错误、高风险回归定位或设备特有故障才临时拆成单项包，避免每个微小改动都要求人工截图。

- **Communication**：TEST 1-5，TLS / HTTP / JSON，需要网络。
- **Engine**：TEST 6-11、15、16、18、21、22、24、25、38、40-45、59-61，HTML/CSS/DOM/select/style/layout/box tree/image resource cache、responsive media viewport、row-reverse flex padding、cached CSS restyle、SVG parse、受约束的 `:root` token、现代 CSS 值、grid/overflow min-content 隔离、overflow scrollbar、分阶段导航资源事务、主文档失败回滚、CSS import tree、libcss 节点缓存纵横屏重选与具名 NetSurf option 默认，离线。TEST40-45、59、60 已真机通过；next78 已撤回。TEST23 float 最小样例已因真实 Browse 回归撤回。
- **GDI Render**：TEST 12、14、17、19、20、26-37、39、46-58、62-66，覆盖 WM Imaging、SVG path/cache/fallback/fill-rule、CSS background-image、原生 GDI text、线性/径向渐变、继承/透明 stop、同文档及重叠文档缓存复用、IANA token 间距、table span/匿名归一化/collapsed border/cell alignment/height distribution、列表 marker/counter/image/inside flow、HTML inline author CSS、checkbox/radio 静态 redraw 与基础交互、WM 原生单行/多行输入桥、随包字体 fallback，离线；TEST48-58、62-66 已验收。
- **Browse**：TEST 13，真实页面抓取 + 渲染，需要网络；HTTPS 走 mbedTLS verified，明文 HTTP 走 WinInet。

当前关键 smoke test：

- TEST 17：内置 NetSurf real layout + redraw 页面。预期看到深红 H1 及红色下边框、带边框的三色 flex 横排、带可见单元格边框的 2×2 table。
- TEST 13：交互模式从 start page 点击链接；自动模式依次直达 example.com、IANA Example Domains 和 Reserved Domains。两者走相同的 fetch → parse → style/resources → layout → paint 导航事务。
- TEST 64：自动点击 checkbox/disabled/radio，验证同组互斥与跨表单隔离，并在纵横屏重排后复核 checked 状态。
- TEST 65：枚举 text/password border-box，验证原生 WM `EDIT` 的 `EN_CHANGE`、maxlength/read-only/disabled、UTF-8 DOM 同步和重排持久性。
- TEST 66：枚举 textarea border-box，验证原生 WM 多行 `EDIT`、CRLF/LF DOM 归一化、readonly/disabled 和重排持久性。

> ⚠ **跑 TEST 5 之前先把模拟器系统时钟设到当前**（开始 → 设置 → 系统 → Clock & Alarms）。WM6 Emulator 默认是 2005-2007 年某个时间，会让所有现役证书都看着像"尚未生效"。

---

## 已知限制 / 注意事项

- **熵源**：默认 `CryptGenRandom`（Phase 3 起）；CSP 不可用时自动退回 QPC+GetTickCount+tid/pid jitter，CTR-DRBG 兜底。
- **HTTP 限制**：单连接 `Connection: close`、无 keep-alive、无 gzip 解码、响应体 cap 1 MB；GET 已有有限 3xx follow，明文 `http://` 经 WinInet。
- **导航卡顿**：主文档、外链 CSS、CSS `@import`、`<img>` 与已计算 CSS 背景资源的 GET 已组成分阶段 worker 事务，旧页在网络等待时可滚动。`PHttp_GetEx/PostEx` 在请求线程报告已解码正文大小和可选 `Content-Length`；父窗口进度条对已知总长显示当前响应的真实百分比，对 chunked/无长度响应保持活动动画，TEST3/13 已真机确认。每个资源响应会开始自己的进度序列，所以这不是整页资源总字节百分比。HTML parse、style、图片 cache copy、layout 仍严格留在 UI 线程，但现通过一次性 WM timer 分成四个提交阶段，让触摸、旋转、绘制和进度控件可在阶段之间运行；单个 NetSurf 调用仍可能短暂卡顿。next86-91 的逐层遥测把主要 UI 热点定位到 `svgtiny_parse`。next92 在旧页与新页同时存活的事务窗口内复用内容一致的 SVG，Reserved 页确认 `image reuse=1`、`svg creates=0`，图片阶段为 2ms；首个页面的冷解析仍可能超过 500ms。TEST13 显示导航完成快照，后续旋转布局不会回写该框。TEST44 已确认主文档失败保留旧页与事务收尾，TEST45 已确认嵌套导入与失败回退。网页字体/脚本资源仍待后续。`test_host` 暂存最多 64 个 URL、合计 2 MiB 原始字节；这是可替换的宿主预算，不是 `positron_core` ABI 限制。
- **渲染限制**：TEST25-37 与 TEST13 fixture 已确认 SVG parse/draw/cache/fallback/fill-rule/网络链、CSS 单背景图、基础 SVG text、线性/径向渐变、继承/透明 stop 及缓存复用；复杂 SVG text、径向焦点、spread method、background-size 和多层背景仍未完成。TEST38-39 已确认受约束的 `:root` token，TEST40 已确认数值型 OKLCH/可求值 calc；两者都不代表完整 CSS Variables/Color/Values。TEST41 只验证 grid 单列降级不会把反向 flex 主内容推至负坐标；TEST42 验证的是 overflow scrollbar，不是完整 Grid。TEST46/47 与 TEST53-57 覆盖一批已验收的表格构盒、边框和行高子例；百分比 cell/后代或 column 模型仍未完成。TEST48-52 覆盖 47 种上游 counter formatter、图片 marker 及 inside 流，但仍不代表完整 CSS Lists/Counter Styles。TEST23 浮动实现已因 Browse 回归撤回。完整范围见 [.agents/KNOWN_LIMITATIONS.md](.agents/KNOWN_LIMITATIONS.md)。
- **WM6 X 按钮 = 最小化不是关闭**。每次启动 test_host 前确认任务管理器没有遗留实例，否则 stage.bat 替换 exe 时会产生 image 不一致。
- **WMDC 桥会静默断**：host 待机 / 模拟器长跑后偶尔失联，表现是 `PTls_Connect` 拿到 `-0x004C [BIO: recv WSA=...]`。修法：重启 WMDC（任务栏 → 退出 → 重启）。**联网测试前先在 IE Mobile 打开 baidu 验证一遍**。
- **模拟器时钟**：跑 verified TLS 前必须校准（见上）。证书 notBefore/notAfter 都按 UTC 比对当前时间。

---

## License

Positron 自有代码使用 [MIT License](LICENSE)。Vendored 源码保留各自许可证，不能被根许可证覆盖；尤其 `netsurf-all-3.11/netsurf/` 的浏览器源码是 GPLv2。完整组件、版本、路径和通知要求见 [THIRD_PARTY.md](THIRD_PARTY.md)。

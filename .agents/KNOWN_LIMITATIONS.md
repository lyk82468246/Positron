# 已验证基线与待消除限制

更新时间：2026-08-08

这份清单把“已经在设备上验证的最小链路”和“当前刻意保留的阶段性实现”分开记录。未被列为完成的项目不得在后续交接、README 或测试结论中表述为完整浏览器能力。

**当前状态更正（next132，2026-08-07）**：next131 的 `screen=320x320 dpi=128`
日志中 TEST13 三段导航完成，但 TEST20 返回 `48x48`，严格动态 DPI 期望为 `64x64`。
next132 已把设备视口决定和单位上下文快照提前到正式构盒之前，并在 TEST20/27 的
样式完成后重新绑定设备视口；ARMV4I 增量构建通过，设备复测仍待完成。不能用放宽
断言或固定 96 DPI 宣称修复。

**历史状态更正（next134，2026-08-07）**：next132 在 `480x640 dpi=192` 下于
TEST58 停止。失败值 `article=320 rows=40/80/40` 是该离线几何测试继承了前一个
设备-backed render 的待布局上下文；next133 的固定 `230x260 @ 96 DPI` 隔离方案已
撤销。next134 让 TEST58 直接使用运行时屏幕/DPI，并把固定 CSS 长度换算为物理断言；
ARMV4I 构建/staging 通过，随后在 `screen=240x320 dpi=96` 设备日志中通过；TEST58
的断言和布局实现均未放宽或改写。

**当前状态更正（next135，2026-08-07）**：next134 的 TEST13/20/27/43/44/56/58-77/80-99
已在 `screen=240x320 dpi=96` 设备日志中通过。next135 新增 text/password/textarea 的
`minlength`/`maxlength` validity 及 TEST100-104；ARMV4I 构建/staging 已通过，随后随
next142 默认批次在同一 `screen=240x320 dpi=96` 设备确认。next143 的受限 ASCII
`pattern` adapter 与 TEST105-109 已在 `screen=480x640 dpi=192` 默认批次确认；首轮
TEST108 暴露并修复了 `tiny-regex-c` 对字符类末尾字面量连字符的错误处理。该能力仍
不是完整 JavaScript RegExp；
email/url/number 类型约束、range/custom validity 和 `invalid` 事件仍未实现，不能把这批
表单检查表述为完整 HTML Constraint Validation。

**当前状态更正（next136，2026-08-07）**：`screen=480x640 dpi=192` 日志中 TEST13/20/27/43/44/56/58
通过后，TEST59 暴露离线 flex 几何夹具继承设备 DPI：固定 `25px` padding 被按 192 DPI
换算为 `50px`。next136 只在 TEST59 的显式 CSS 几何 pass 前安装 96 DPI 参考上下文，
再恢复运行时设备视口；ARMV4I 构建/staging 已通过，设备复测待补。该修复不改变
NetSurf flex/layout，也不把 96 DPI 设为产品默认。

**当前状态更正（next137，2026-08-07）**：next136 在 `screen=320x320 dpi=128` 下
先通过 TEST13，随后 TEST20 报 `first box=48x48; expect 64x64 device px`。根因在
vendored `libcss` 的 `css_unit_len2device_px`：它先把每 CSS 单位的设备比例取整，
使 `128/96=1.333` 退化为 1。next137 改为保留固定点分数比例，乘完整 CSS 长度后再
做最终正负向取整；这是通用设备像素修复，不是放宽断言或固定 96 DPI。ARMV4I
构建/staging 已通过；设备复测已确认 TEST13/20/27/43/44/56/58/59 通过，随后
TEST60 停止。

**当前状态更正（next138，2026-08-07）**：TEST60 的离线表格几何夹具仍把运行时
`128 DPI` 传给 `PCore_SetViewport`，所以固定 CSS padding 被正确换算成设备像素后，
断言看到 `24/19/19/24` 与 `13/13/7/7`，而不是预期的 CSS `18/14/14/18` 与
`10/10/5/5`。文本宽度仍相等，未显示 selector/restyle 回归。next138 只为该显式
CSS 像素探针安装 96 DPI 参考上下文，并在结束时恢复真实设备视口；ARMV4I
构建/staging 已通过，`C:\WMShare\Positron-next138` 的设备复测待补。该修复不把
96 DPI 变成产品默认，也不放宽 TEST60 断言。

**当前状态更正（next139，2026-08-07）**：next138 在 `screen=320x320 dpi=128` 下
通过 TEST60，随后 TEST63 报告 `shared SVG did not survive first document release`。
TEST63 同样是固定 CSS `240x120`/`120x60` 的离线夹具，却继承了设备-backed DPI；next139
只为该探针安装 96 DPI 参考上下文，清理时恢复真实设备视口，并把失败诊断拆成
`layout/node/box` 值。共享 SVG 的跨文档 create/reuse/free 断言没有放宽，ARMV4I
构建/staging 已通过，`C:\WMShare\Positron-next139` 的设备复测待补。

**历史状态更正（next140，2026-08-07，已替代）**：next139 在 `screen=480x640 dpi=192` 下
通过 TEST13/20/27/43/44/56/58/59/60/61，随后 TEST62 的离线 checkbox/radio probe
返回 `36x36`，正好是 `192/96=2` 的 CSS 尺寸换算。next140 只为四个静态 toggle
probe 与 hidden-input 检查安装固定 96 DPI；这不是可接受的动态 DPI 修复，已由 next141
替代。

**当前状态更正（next141，2026-08-07）**：TEST62 保留 `64x48 CSS px` 探针表面，使用
设备实际 DPI，并将原本 96-DPI 的 `14..24px` 控件几何范围按 `dpi/96` 等比换算；控件
状态、绘制和隐藏 input 断言未放宽。ARMV4I 构建与
`C:\WMShare\Positron-next141` staging 已通过，设备日志确认 TEST62 及 TEST63-74
通过，TEST75 停止。

**当前状态更正（next142，2026-08-07）**：TEST75 的定位夹具保留 CSS 尺寸，断言将
宽高、绝对偏移和 relative 偏移统一按实际设备 DPI 的 `dpi/96` 等比换算；定位构盒、
绘制和可见页面路径未改变。ARMV4I 构建与
`C:\WMShare\Positron-next142` staging 已通过；`screen=240x320 dpi=96` 设备日志确认
TEST13、20、27、43、44、56、58-77、80-104 全部通过并记录 `TESTBENCH PASS`。

## 已验证基线（不是完整功能声明）

### 高 DPI / 大分辨率视口边界（next134，非 96 DPI 仍需继续轮换验收）

NetSurf 的标准坐标约定是：CSS media/vw/vh 使用 CSS 像素视口，
`layout_document` 与 GDI 重绘使用设备像素。next122 的新模拟器日志首次暴露
宿主把两者混用：TEST20 的 `48px` 图像盒实际为 `96x96` device px，自动化在
TEST20 停止；这不是图像缓存或脚本 provider 的失败。next123 增加
`PCore_SetDeviceViewport`，Browse 导航、WM_SIZE 旋转和宿主启动路径统一经过
设备像素到 CSS 像素的换算；`PCore_SetViewport` 的显式 CSS 像素语义保留给
离线引擎测试。ARMV4I 构建/staging 已通过，必须在新分辨率模拟器重新跑自动
配置并人工检查 IANA、Example Domain、滚动、链接和旋转；在此之前不能宣称
高 DPI Browse 已验收。

2026-08-07 的 next126 日志为 `screen=320x320 dpi=128`：TEST13 的三段导航完成，
但 TEST20 在离线缓存图片断言处停止。该失败不是 Browse 回归，而是 TEST20 沿用了
显式 CSS 视口路径，却把 48 CSS px 按设备 DPI 放大后比较；next127 已将 TEST20
隔离到 96 DPI CSS 视口，并修正了失败信息中的字段顺序。下一轮设备批次应轮换
分辨率、横竖方向或 DPI，并保留日志头部的屏幕尺寸与 DPI；即使自动 testbench 全部
通过，也必须人工检查 TEST13 的排版、滚动、链接和旋转。

next127 随后的设备日志为 `screen=240x320 dpi=96`：TEST13、TEST20、TEST27、
TEST43-96 均通过，TEST97 因测试把 Duktape 的小写 `invalid json` 错误文本误当成
失败而停止。next128 已将 TEST97 改为验证 `PSCRIPT_ERROR_JSON` 与非空诊断，保留
非法 JSON 后继续求值 `42` 及 JSON `null` 的后续恢复断言；这不改变 DLL 的运行时
行为，也不等于高 DPI Browse 已验收。

next128 的新设备日志为 `screen=240x240 dpi=96`，TEST13/20/27、TEST43-99 全部
通过。next129 进一步撤销 TEST20 中临时的 `PCore_SetViewport(..., 96)`：该离线
图片测试现在也通过 `PCore_SetDeviceViewport` 读取实际 DPI，并以
`MulDiv(48, dpi, 96)` 检查物理盒尺寸。96 是 CSS 像素定义中的参考 DPI，不是设备
锁定值；非 96 DPI 的设备仍需用 next129 实测验证。

next129 的新设备日志为 `screen=480x640 dpi=192`：TEST13 与 TEST20 通过，TEST27
发现 SVG 测试仍把 `120x60` CSS 尺寸当作物理尺寸比较，实际设备盒为 `240x120`。
next130 已修正 TEST27 的设备视口安装、动态尺寸断言和离屏采样坐标；该批次尚未
在设备上重跑。

next130 在 `screen=480x480 dpi=192` 下确认 TEST27/TEST43/44 通过后，TEST56 的
离线表格几何段因继承物理 DPI得到 `105 CSS px -> 210 device px` 而误报失败。
next131 已将该离线段隔离为 96 DPI CSS 契约，并保留可见渲染段的真实设备 DPI；
这不改变 table layout 实现。

| 范围 | 已验证事实 | 不代表 |
|---|---|---|
| CSS 媒体查询与 token | TEST 21 已在设备确认运行时 viewport/DPI、旧式 min/max-width 及整数像素 MQ4 `width <=` / `width <`；TEST38-39 又确认同表顶层 `:root` token 语义与正式 redraw。 | 所有媒体特性、MQ4 范围、元素作用域或完整 custom properties 均已覆盖。 |
| 现代 CSS 值 | TEST40 已在设备确认：数值型 `oklch()` 转裁剪 sRGB，并求值无需布局上下文的同单位 `calc()` 四则运算。 | 完整 gamut mapping、`none`/复杂角度、`color-mix()`、混合单位 calc 或 CSS Color/Values 均已实现。 |
| 反向 flex 内边距 | TEST 22 已在设备上确认：224px viewport 下，`row-reverse`、左右 25px padding、隐藏侧栏时，主内容为 `x=25,width=174`。 | 完整 Flexbox 规范或任意真实站点的复杂 flex 均已兼容。 |
| 基础定位 | next111/TEST75 已在设备确认：relative box 保持正常流并应用 top/left 偏移，absolute block 使用 positioned parent，`display:inline` 的 absolute box 被 blockify 后进入 NetSurf 正式定位路径。 | 不代表 float、sticky、Grid/flex/table 中所有定位交互、复杂 containing-block 或完整 CSS Positioned Layout 已实现。 |
| 普通浮动候选 | next115/next116 曾把 `float:left/right` 受控构盒路径和 TEST79 加入源码，但都已撤回。next116 的设备 TEST79 失败，且真实 TEST13 截图仍有导航/正文排版回归。 | Float 当前不支持；不能作为真实 IANA 页脚或完整 CSS Floats 证据。重新实现前必须先完成完整 box construction/normalisation，并通过 TEST79、TEST13 深链和旋转门禁。 |
| 动态 `:hover` | next113/TEST76 已在设备确认：命中最近 DOM 元素后，样式重选会把链接从蓝色切为红色，清除状态后恢复；WM6 宿主用 `WM_MOUSEMOVE` 与 250ms 定时器轮询离开窗口。 | 不代表 `:visited`、`:target`、`:indeterminate`、专用 MouseEvent 数据、触屏 hover 语义或 JavaScript 绑定已实现。 |
| IANA 窄屏页 | TEST13 起始页和 `Example Domains` 已可读；TEST41 的竖横屏截图确认 `/numbers` grid 宽表格不再把主内容推到左边界外。next80 已修复 libcss 父 bloom 节点数据过早销毁；TEST56/58/59/60 与真实 `/domains/reserved` 横竖屏均已通过，首个 `Domain` 表头恢复正常。 | 任意 IANA 子页版式通过，或页面已达到现代浏览器还原度。 |
| 视觉容器与文本比例 | next117 人工复核确认主链路基本正常，但部分页面/测试存在容器或背景框偏小、文本量偏多导致的版式不协调。 | 尚未定位到单一 CSS 根因；不应通过放宽断言解决，也不代表核心解析/资源/导航失败。需要至少三个复现样例、computed style/box geometry 数据、针对性回归和竖横屏截图后才能关闭。 |
| 嵌套 overflow | NetSurf 3.11 scrollbar 已接入；TEST42 的离屏步进断言及真机箭头/thumb 交互通过，host 拖动只重绘对应 overflow viewport。next54 的 fixed-height 回归已在 next55 收窄，用户确认 auto-height 空间、箭头、短页纵条与色块页正常。 | 不代表惯性触摸、overlay scrollbar 或任意嵌套组合均已覆盖。 |
| table span/归一化/折叠边框 | NetSurf 3.11 span occupancy 与匿名 row/cell 已由 TEST46/47 验收。next64/TEST53 至 next68/TEST56 已覆盖常见 collapsed-border、cell alignment/empty-cells 与显式 table height；next73/TEST57 又确认 25/50/auto 百分比 row 分配及超约束缩放。 | 尚不覆盖任意 inline/float/form 畸形组合、caption/column 归一化、`col`/`colgroup` border 来源、百分比 cell/后代内容、跨行 baseline 或所有复杂表格边界。 |
| Forms/widgets | next85/93 至 next104/TEST71 已完成 checkbox/radio、文本、textarea、single/multiple select、button、GET/POST、reset/Enter/label 与 multipart/file；next106/TEST72 已确认首批 `required/valueMissing`；next109/TEST73 已确认动态表单伪类；next110/TEST74 已建立通用 DOM Event 传播/取消和宿主 click default-action 门；next135/TEST100-104 又加入 text/password/textarea 的 `minlength`/`maxlength`、UTF-8 字符计数、动态值更新和首个长度错误几何；next143/TEST105-109 加入受限 ASCII `pattern` mismatch、动态更新、坏属性豁免和 flags 组合，并已在 `screen=480x640 dpi=192` 设备通过。 | multipart 仍整体缓冲且 MIME 固定；尚无流式上传、上传进度、MIME 推断、multiple file、完整 JavaScript RegExp（groups/alternation/brace quantifier/Unicode/inverted class）、email/url/number 类型约束、range、custom validity、`invalid` 事件、验证气泡、专用 Mouse/Keyboard/Focus/Input 数据或完整 HTML activation。空且无 CSS 尺寸的 text input 也缺浏览器默认 intrinsic size。自动断言不等于真实手指、原生选择器或公网 POST 已人工验收。 |
| author-level inline CSS | 外部 author stylesheet 正常参与 libcss 选择；TEST57 使用外部类规则通过。next75/TEST58 已确认 NetSurf 式声明列表解析、libcss inline cascade、继承、后代 class 选择与正式布局/重绘。next81 已把全零 `nsoption` shim 改成具名默认，未知读取会编译失败；TEST56/58-61 已由设备确认。 | 正式构盒不调用 NetSurf `box_construct.c`；旧缺口是 `pcore_style_subtree` 固定给 `css_select_style` 传 `NULL`。具名 option 不能被误写成 inline CSS 的直接开关。 |
| Forms/widgets 状态边界 | next109 把宿主维护的 focus/active 节点与 live checked/selected/disabled 状态交给 libcss callback；next110 的通用 Event 已有 capture/target/bubble、取消和停止传播，next113 又加入独立的 hover 状态，现有 click 默认动作尊重取消结果。 | 通用 Event 尚无 MouseEvent/KeyboardEvent/FocusEvent/InputEvent 专用字段，也未覆盖 `:visited/:target/:indeterminate`、所有 HTML activation 细节或所有控件的浏览器默认 intrinsic size。 |
| 列表 marker | next57/59 已确认基础 marker 与字体；next61/TEST50 已确认 libcss 上游 47 种 counter formatter、document-cache `list-style-image` 与失败类型回退；next62/TEST51、next63/TEST52 已确认 inline-first 及 block-first/空条目/嵌套/图片的 `list-style-position:inside`。 | 不代表 float 邻接 marker、自定义 `@counter-style` 或完整 CSS Lists。普通语言字体不属于当前 marker 工作范围。 |
| 字体 fallback | next59 随包部署约 901 KiB 的三份静态 Positron Symbols/Emoji（来自 Noto OFL），精确 cmap 选择统一用于 GDI 测量、换行命中与绘制 run；设备确认箭头/marker/五个 emoji 可见且比 next58 稍平滑。当前范围明确只支持符号与单色 emoji fallback。 | 不计划在本阶段加入普通语言/多语种字体；也没有复杂 ZWJ/variation shaping、彩色 emoji、网页 `@font-face` 或字体下载。`ANTIALIASED_QUALITY` 最终效果仍依赖 OEM GDI。 |
| 图片 | TEST19/20 已确认公共 retained 位图 ABI 与 WM Imaging 四格式；TEST25-37/13 已确认当前 SVG 链。next89 已由 TEST20/27 确认同 document 二次布局复用；next92/TEST63 已确认两个同时存活且内容一致的 document 可共享 SVG，并在释放首文档后继续绘制。 | 复杂 SVG text、径向焦点/spread method、多层或可缩放 CSS 背景、空闲/持久缓存及跨线程图片句柄仍未完成。 |
| 外部脚本资源与独立 JS DLL | next114/TEST77 在 core 中建立了非空 script-src 扫描、宿主 URL resolver/fetch/free 回调、document 生命周期缓存、重复引用去重和只读枚举 ABI；next118 又以仓库内 Duktape 2.7.0 接入独立 `positron_script.dll`，提供 UTF-8 求值、持久上下文、错误恢复和内存/执行计数；ARMV4I 构建及 TEST80 设备 testbench 已通过；next119/TEST81 增加 timeout、64 KiB 源码长度拒绝和恢复断言，设备 testbench 已通过；next120/TEST82 增加 512 KiB runtime heap 配额和峰值断言，设备 testbench 已通过；next121/TEST83 已加入 CommonJS 风格模块一次执行缓存、`require()`、失败回滚和显式清空，ARMV4I 构建/staging 与设备 testbench 已通过；next122-126 的 TEST84-99 又加入 provider、global/JSON、native callback 与 structured setter，next134 的 `screen=240x320 dpi=96` 设备日志已确认通过。 | `positron_core` 不执行 inline/external JavaScript，`positron_script.dll` 也不自动提供 DOM/window/fetch/network binding；尚不解释 type，不把脚本抓取加入冻结的 TEST13 网络事务；总配额只约束 DLL 的 Duktape heap，不约束宿主进程其他内存；global/JSON bridge 只处理宿主显式提供的 primitive/JSON 值，结果缓冲最多 255 字节有效载荷，不能替代 DOM 或异步 bridge；模块 provider 是同步、宿主拥有的源码回调，不读取 URL 或文件，不提供异步依赖图、错误事件、CSP、跨源策略或持久 HTTP cache。 |
| ENGINE 离线回归 | 2026-07-11 用户确认原整组至 TEST24 通过；2026-07-12 又单独确认 TEST25 SVG parse。TEST23 的浮动实现已因真实 Browse 回归撤回。 | 网络 Browse、GDI Render 组，或未被这些测试覆盖的真实页面兼容性均已通过。 |
| 旋转尺寸 | `WM_SIZE` 以新 client 宽高从 document CSS 缓存 restyle + layout；TEST24 已确认跨断点重选、无联网及滚动比例，真实 TEST13 横竖屏也保持同一阅读区域。 | 所有媒体语法和任意样式资源均已覆盖。 |

### next125：独立脚本 JSON 宿主回调桥（待设备验收）

`PScript_RegisterGlobalJsonFunction` 通过固定 16 槽表把宿主同步回调暴露成 JavaScript global。参数是 compact JSON 数组，回调写回一个 JSON 值；回调不得重入/销毁上下文，也不能异步保存指针。DLL 只接受小于 256 字节的返回缓冲，失败、非法 JSON 和调用异常会回到既有 recoverable error 路径。TEST90-94 覆盖这条 ABI；next126 又提供 `PScript_SetGlobalJson`，用受保护 decode 注入结构化 JSON，输入最多 64 KiB，失败/超限保持原 global。TEST95-99 覆盖该 setter，但 Debug 构建通过不等于设备/第三方程序验收；它仍不提供 DOM、window、fetch、网络或浏览器 JS 开关。

## 真实页面观察到的未完成项

### IANA 根变量布局已改善，完整真实页仍继续观察

TEST38-39 真机确认根变量语义及 25px inset 后，新的 TEST13 截图中 IANA logo、导航、正文与注册表两列均未再裁切或重叠，custom-properties 导致的窄屏间距根因可以关闭。普通文本空白折叠也已由 TEST13/15 确认。当前结论仍不是任意 IANA 子页或任意真实站点都已完整还原。

- **可能范围**：剩余 flex/table/inline/字体或未实现 CSS 特性的组合；尚未把单一原因当作结论。
- **已撤回的一项**：IANA 页脚是 table cell 内 `display:inline; float:left` 列表。TEST23 曾在最小样例中确认两个浮动块同行及 `clear:both`，但将该构盒规则直接接入真实页面后，2026-07-11 Browse 截图出现严重错位和替代方框；实现已撤回。该测试不再参加 ENGINE 组，不能作为 float 支持证据。
- **next115 已否决**：设备日志得到 `float=(0,0 70x36)/(154,0 70x36) flow=(70,0 0x20)`，说明 TEST79 查询到的是零宽 inline 起始盒；同包 TEST13 截图还出现导航扁平化。next115 不得作为基线。
- **next116 已否决**：它只处理显式 block-level 的非替换 float，保留 inline/list-marker、flex、图片和表单的既有路径，但仍让真实 IANA 页面出现导航扁平化/正文排版回归；设备 `TEST79` 最终失败。实现和默认测试配置已撤回，next114 恢复为可靠基线。Float 方向暂挂，不能把离线几何候选当成真实页面支持。
- **当前站点版本风险**：2026-07-13 重新读取到 IANA 的 `iana_website.80c103cc08b6.css`；除已确认的 `var(--space-*)` 外还有 22 处 `oklch()`、15 处 `calc()`、`color-mix()`、grid/gap 与 `:has()`。新兼容模块只处理数值型 OKLCH 和可完全求值的同单位 calc；混合单位及其他现代能力仍会降级。
- **最新子页结论**：`/numbers` 使用 `display:grid`，其中 `.dtable-wrap { overflow:auto }` 包住宽表格，TEST41 已确认该路径。`/domains/reserved` 没有 Grid 包装层，但同样以 `.dtable-wrap { overflow:auto }` 包住宽表格。next77 已让 flex main 在竖屏保持正确 inset；旋转到横屏后，wrapper 本身位置正常，但第一格内容左移约 18px，恰好抵消作者的 `padding-left:18px`，导致 `Domain` 贴到 clip edge，视觉上连字体/样式也异常。非拉丁字符 tofu 与这个英文表头问题无关，并按项目范围保留。
- **失败实验**：next78 在 layout 末尾递归 `scrollbar_set(...,0)` 后，真实页横屏从首个 `Domain` 异常扩大为全部表格单元格异常，TEST56 随后失败并触发系统级异常。该实验及其诊断 API/扩展 TEST59 已撤回，旧包已改名为 `C:\WMShare\Positron-next78-FAILED-DO-NOT-USE`。
- **已验收边界**：next80 的 TEST56/58/59/60 与 TEST13 起始页、`Open example`、IANA `/domains/reserved` 横竖屏均通过。仍禁止恢复 next78 的全局 scrollbar 回调重置；这次闭环不等于所有 IANA 子页或完整现代 CSS 已实现。
- **完成条件**：在目标设备的竖屏和横屏下，主内容、页脚和导航均不裁切、不重叠，且没有明显错误图标/替代字符；结果需要新的真机截图确认。

### 旋转 responsive restyle 已完成当前验收

`WM_SIZE` 现调用 `PCore_SetViewport`、基准 URL 感知的 `PCore_StyleDocumentEx2` 和 `PCore_LayoutDocument`。外链 CSS 与成功导入首次导航时以原始字节缓存到 document，尺寸变化只从该缓存重选 `@media`，不重新联网。旧 `PCore_StyleDocumentEx` 保持兼容。TEST24 已于 2026-07-11 在设备确认 320px 到 299px 的旧接口外链 CSS 重选，fetch/free 都保持一次；TEST45 又确认导入树的 cache-only 重选。

- **当前取舍**：只缓存最多 32 份、单份不超过 256 KiB、每 document 合计不超过 512 KiB 的成功外链 CSS 原始字节；缓存未命中的样式在旋转时保持缺失，不能在 `WM_SIZE` 中重新联网。
- **设备结论**：TEST24 的 0/50/100% 比例断言通过；真实 TEST13 从竖屏 `Further Reading / Domain Names` 区域旋转到横屏后仍停留在同一区域。扩大 MQ4 语法或处理 custom properties 仍是独立兼容性工作。
- **完成条件**：旋转前后跨越 TEST 21 式断点时，computed style 与几何都切换正确，并恢复原滚动位置的合理比例。

### 导航 CSS/图片抓取已异步，最终提交仍在 UI

主文档之后的外链 CSS、CSS `@import`、`<img>` 和应用样式后发现的背景 URL 现也分轮交给同一 worker；HTTP 字节通过 `WM_APP` 消息交回窗口线程，DOM/libcss/NetSurf/GDI 从不跨线程。TEST3/43/44 与真实 TEST13 已确认真实正文进度、后台资源阶段、成功 swap 和主文档失败回滚。parse/style/image-discovery/layout 现用一次性 WM timer 在调用之间让出消息循环；单个不可重入调用仍可能卡顿。

- **当前取舍**：同一时刻只允许一个导航请求；旧页可绘制和滚动，但加载中再次点击链接会被忽略。HTML parse、style、cache copy 和 layout 仍在 UI 提交阶段同步执行，全部网络完成后仍可能短暂卡顿。
- **已验收观测**：next86 的 TEST13 报 total/network/max-UI=6435/5503/673ms，parse/style/images/layout/paint=11/182/6/673/36ms；2 个资源全部成功，document/cache=10499/121111 bytes，预算拒绝为 0。网络主导总时长，layout 主导单次 UI 停顿；该数据用于定位，不是已完成的调度优化，也不是产品遥测上传。
- **已验收诊断**：next87 的只读 layout breakdown 不改变布局行为；IANA 起始页报告 `580ms` 中 box/first=515/65，Reserved 子页的最后一次导航报告 `662ms` 中 box/first/settle=495/124/43。它证明构盒是当前首要细分对象，不证明任何性能改善，也不应据此跳过二次布局。
- **已取得诊断结论**：next88 的 IANA 起始页 tree/image=`523/518ms`，Reserved 为 `481/474ms`；backgrounds 为 0，tree-other 仅 `4/1ms`。它定位到 SVG retained object 创建，并不说明整个页面或 box tree 已缓存。
- **next89 已验收优化**：XML-like 图片字节先调用现有 SVG parser，避免 WM Imaging 的已知失败探测；成功/失败 retained 状态归 document image cache 所有，重排只新建轻量 carrier。TEST20/27 的 4/4 与 1/1 reuse 及全部默认门禁通过。TEST13 首次页面 image 仍为 `469ms`，随后 Reserved 降到 `37ms`；首屏冷启动未解决。对象仍在创建线程释放，不支持跨 document、跨导航或跨线程共享。
- **遥测边界**：TEST13 只显示导航完成快照，导航后的旋转布局不回写该统计，因此旋转后退出仍会显示初始 layout 的 reuse/markup-first。
- **next90 已取得设备诊断**：ABI 1.5 的 `PImage_SvgGetCreateStats` 与 core 的 `PCore_GetImageDecodeStats` 只记录成功 retained SVG 的 setup/parse/raster 创建时间。next91 日志中 TEST27 为 `59=0+58+1ms`，IANA Example 为 `37=0+37+0ms`，Reserved 为 `595=0+593+2ms`；当前冷创建成本几乎全在 `svgtiny_parse`，不是 wrapper setup 或 NanoSVG retained raster conversion。失败 SVG、后续 draw、字体 GDI 和布局不属于这四段；计时分辨率受 WM `GetTickCount` 限制。
- **next91 testbench 已通过首轮设备运行**：`auto=1` 将断言、首帧绘制和三步真实导航无人值守化，配置的 TEST13/20/27/43/44/56/58-62 最终全部 PASS，结果成功写入同目录 `test_host.log`。它不会做截图像素基线比较，也不能判断抗锯齿、字体观感或复杂真实页是否“好看”；网络 worker 仍依赖现有 HTTP 超时，未加入强制终止线程。首轮日志把 TEST44 的预期离线失败也标成通用 TEST13 NAV ERROR，后续候选已将逐页记录严格限定在 TEST13 自动路由。
- **next92 重叠文档 SVG 复用已验收**：实现沿用 NetSurf high-level cache 的“内容条目与使用者分离”原则，但范围刻意更小。只有 URL、长度和双内容哈希一致、且至少一个 document 仍持有引用的 SVG 才共享；引用归零立即释放，不设置任意 MiB 常驻预算。设备 TEST13 的 Reserved 页为 `image reuse=1`、`svg creates=0`、image=2ms，TEST63 也通过释放首 document 后的像素绘制。位图、空闲对象、跨线程和持久缓存均不在该机制内。
- **next93 checkbox/radio 交互已验收**：`PCore_FormActivateAt` 与链接命中使用相同 document-space 坐标，状态写回 libdom；宿主仅失效发生变化的控件联合区域。TEST64 验证 checkbox、disabled、radio 分组隔离和纵横屏重排保持，同批 TEST13/20/27/43/44/56/58-64 全部 PASS。这不是完整表单、焦点或事件系统。
- **next94/97 原生文本输入已验收**：core 以 UTF-8 ABI 暴露 text/password/textarea 的值与状态，WM 宿主使用原生单行/多行 `EDIT`，并通过真实 `EN_CHANGE` 把 UTF-16 编辑结果同步回 libdom。多行回写将 CRLF/CR 归一化为 LF。设备无人值守日志确认 TEST65/66 及同批 TEST13/20/27/43/44/56/58-64 全部 PASS。
- **next98 select 自动门禁已验收**：core 以独立 ABI 暴露单选/多选的 option 文本、value、selected/disabled 和几何，单选宿主使用 WM 原生 `COMBOBOX`。2026-07-30 设备日志确认 TEST13/20/27/43/44/56/58-67 全部 PASS。TEST67 的原生桥探针通过 `CB_SETCURSEL` 后调用与 `CBN_SELCHANGE` 相同的同步路径，只证明宿主适配和 DOM 状态连通，不等于真实手指展开下拉或多选 LISTBOX 已验收。button、label 激活、提交和完整事件系统仍未实现。
- **next99 button/提交已通过自动门禁**：按钮由 NetSurf CSS layout/redraw 呈现，不叠 WM 原生 BUTTON。core 只生成 UTF-8 URL-encoded successful controls，宿主 GET 替换 action query、POST 调用既有 `PHttp_PostEx`；资源子请求继续使用 GET。multipart 不会降级成残缺 POST，而是明确返回未实现。设备日志确认 TEST68 与同批 TEST13 深链全部 PASS；TEST68 本身不联网，只断言 core 数据与宿主请求对象，真实公网表单端点仍需后续批次覆盖。
- **next100 普通表单默认动作未通过门禁，next101 已修复**：next100 的 TEST13 深链和 TEST20/27/43/44/56/58-68 全部 PASS，但 TEST69 textarea reset 失败。libdom 0.4.2 首次读取 textarea value 时误置 `default_value_set`，使默认文本未保存；next101 将其改回 `value_set` 且不改原断言。设备日志随后确认 TEST69 与同批全部门禁 PASS。自动桥仍不代表真实手指、视觉位置、约束验证或 DOM 事件取消/传播已经验收。
- **next103 multipart/file 已通过设备门禁**：next102 的既有门禁全部 PASS，但 TEST70 在 reset 后残留显示文件名；libdom 会把无初始 `value` 属性的 file 控件第一次运行时 `set_value()` 记为 `defaultValue`。next103 在 reset 时按浏览器语义无条件清空 file 显示值与原始路径，设备日志随后确认 TEST13/20/27/43/44/56/58-70 全部 PASS。TEST70 不会自动操作系统文件选择器，也不访问公网回显端点；这些仍需后续人工/集成验收。
- **next104 WM multiple select 已通过设备门禁**：宿主为 multiple select 创建带 `LBS_MULTIPLESEL` 的原生 LISTBOX，按 NetSurf border-box 定位，并复用单选已有的滚动、换页销毁与旋转重建。`LBN_SELCHANGE` 与 `CBN_SELCHANGE` 共享同步入口；multiple 路径逐项读取 `LB_GETSEL`，写回 `PCore_SelectSetOptionSelected`，disabled option 被 Core 拒绝后立即恢复原生状态。TEST71 自动确认两项增删、disabled option/select、LISTBOX/COMBOBOX 类型、精确高度、重建保持、GET 重复值和 reset；同包 TEST13/20/27/43/44/56/58-71 全部 PASS。该断言不检查 owner-draw disabled 行、真实手指命中或视觉观感。
- **next106 首批 required 验证已通过设备门禁**：Core 对 text/password/textarea/file、checkbox、同名 radio、single/multiple select 计算 `valueMissing`，跳过 disabled/read-only，并在 URL-encoded 与 multipart 提交前阻止无效表单；宿主滚动并聚焦首个无效原生控件。next105 的 reset 失败来自 libdom 0.4.2 将第一次运行时 text/password 写值误记为默认值；next106 在写入前冻结解析时默认值，不改 TEST72 断言。设备日志确认 TEST72 和 TEST13/20/27/43/44/56/58-71 全部 PASS。尚无高级 validity 状态、`invalid` 事件或验证气泡。
- **next109 动态表单状态已通过设备门禁**：TEST73 验证 disabled/enabled、checkbox/option checked、focus、active、cache-only 重样式、纵横屏保持与 reset；同包 TEST13 三段导航及 TEST20/27/43/44/56/58-72 全部 PASS。next107/108 的失败不是颜色或伪类语义，而是测试夹具中无 CSS 尺寸的空 text input 几何为 0；next109 让夹具与既有 WM EDIT 前提一致地声明显式尺寸。默认 text input intrinsic size 保持为独立缺口，不通过放宽动态状态断言掩盖。
- **next110 最小 DOM Event 已通过设备门禁**：公开 ABI 只暴露 opaque listener handle 与普通 C callback，不泄露 libdom 类型；支持按 element id 或正式布局坐标派发 trusted generic Event。vendored libdom 的 target 重复传播、忽略 `bubbles/cancelable` 和 dispatch-only 状态残留已在本地按 DOM 语义修正。TEST74 覆盖 capture/target/bubble、非冒泡、取消、两种停止传播、移除监听器与坐标派发；TEST13 三段导航和 TEST20/27/43/44/56/58-73 同批全部 PASS。尚未实现专用事件数据、完整 HTML activation、异步事件队列或 JavaScript 绑定。
- **next110 最小 DOM Event 已通过设备门禁**：公开 ABI 只暴露 opaque listener handle 与普通 C callback，不泄露 libdom 类型；支持按 element id 或正式布局坐标派发 trusted generic Event。vendored libdom 的 target 重复传播、忽略 `bubbles/cancelable` 和 dispatch-only 状态残留已在本地按 DOM 语义修正。TEST74 覆盖 capture/target/bubble、非冒泡、取消、两种停止传播、移除监听器与坐标派发；TEST13 三段导航和 TEST20/27/43/44/56/58-73 同批全部 PASS。尚未实现专用事件数据、完整 HTML activation、异步事件队列或 JavaScript 绑定。
- **next111 基础定位已通过设备门禁**：TEST75 以四个不同标签的夹具断言正常流、relative 偏移、absolute block containing block 以及 absolute inline blockification，并打开正式 NetSurf layout/redraw 窗口做首帧冒烟；TEST13 三段导航和 TEST20/27/43/44/56/58-74 同批全部 PASS。该项只覆盖当前 slim builder 能安全接入的基础路径，float、sticky、复杂 containing-block 组合与 Grid/flex/table 定位交互仍保留。
- **next116 浮动候选已撤回**：设备日志为 `TEST79 FAIL`；即使自动 TEST13 记录了 OK，人工 Browse 截图仍显示导航和正文排版异常。源码、TEST79 默认配置和 ENGINE 接入已恢复到 next114，Float 方向暂挂。
- **next113 动态 `:hover` 已通过设备门禁**：TEST76 断言 `PCore_InteractionSetAt(..., PCORE_INTERACTION_HOVER)` 命中最近元素，重选样式得到红色，清除后恢复蓝色；宿主使用 WM6 可用的 `WM_MOUSEMOVE` 与定时器轮询，不调用桌面 `TrackMouseEvent`。同包 TEST13/20/27/43/44/56/58-75 全部 PASS。该项只覆盖 CSS 状态选择和离开窗口清理，不代表专用 MouseEvent、触屏 hover 或 JavaScript。
- **资源预算**：`test_host` 最多暂存 64 个去重 URL、合计 2 MiB 原始字节，成功提交时 core 会复制所需数据后立刻释放事务。该值用于限制 WM 峰值，是可替换的宿主策略，不是 `positron_core` ABI 或最终页面的硬上限。
- **后续实现**：单响应 `Content-Length`/progress 回调已实现并由 TEST3/13 确认；`@import` 事务已由 TEST45 确认；next114/TEST77 已建立脚本资源发现/缓存 ABI，next118-126 又提供不依赖浏览器的独立 `positron_script.dll` 能力。next134 的 `screen=240x320 dpi=96` 设备日志确认 TEST80-99 全部通过，但它们仍未接入 TEST13。高 DPI Browse 仍需在不同分辨率/DPI 下视觉复查；DOM/window/fetch/native bridge、整页多资源聚合进度、web fonts 和更广资源类型仍未实现。
- **next125 已设备验收（next134）**：TEST90-94 已实现同步 JSON 宿主回调注册/注销、替换、失败恢复和固定槽位上限；它是独立 DLL 的 native operation 边界，不是浏览器 JavaScript 或异步 native bridge。
- **next126 已设备验收（next134）**：TEST95-99 已实现 structured JSON global setter、跨调用 mutation、malformed/null 恢复、64 KiB 输入拒绝和 JSON 类型替换；setter 复制值进 context，不保留宿主输入指针，但仍受 Duktape heap 与 255 字节结果读取限制。
- **CSS import 边界**：最多追踪 16 层递归和本次样式 pass 的 64 个解析表；失败、循环和超深导入按 libcss 契约注册空表。成功导入复用每 document 最多 32 份/512 KiB 的 CSS 字节缓存；不含 HTTP 缓存失效、跨源安全策略或独立持久缓存。URL 合并由宿主回调负责，WM 宿主使用 `InternetCombineUrlA`，core 本身不绑定传输层。
- **并发约束**：在确认 libdom/libcss/NetSurf 移植层的线程安全前，不能让 worker 与 UI 同时操作同一 document 或共享全局 viewport context；过期请求只丢弃结果，不使用强制终止线程。
- **第一阶段完成条件**：慢网主文档 GET 期间旧页可滚动，loading 可见；成功后才 swap，错误保留当前页面，关闭窗口不会遗留线程。
- **当前完成条件**：TEST43 的 URL/去重/成功/失败断言通过；真实 TEST13 的 CSS/图片网络等待不阻塞 UI，generation 正确，成功后才 swap，失败资源保留 fallback。

### 图片格式与公共 retained 位图 ABI 已完成当前验收

WM Imaging 的 BMP/PNG/JPEG/GIF 均已在设备通过尺寸探测和 Draw 返回，但首轮多格式 fixture 的可见性与旧截断 BMP 不足以完成视觉验收。当前 `<img>` 解码失败时仍刻意回退到 alt/src 文本。

- **当前结论**：BMP/PNG/JPEG/GIF 四格式与 TEST20 缓存 `<img>` 已由设备视觉确认。2026-07-15 next45 又确认公共位图句柄的四格式颜色、清空调用方输入后的重复绘制、损坏输入拒绝、旧 `PCore_Image*` 转发和 NetSurf retained redraw；TEST13/26/27 同批无回归。句柄仍只允许在创建线程使用和释放。为保证 WM Imaging 的惰性解码数据源始终有效，句柄存活期间会保留一份编码字节；core 的 document cache 也保留原字节以支持重布局，因此当前以额外编码内存换取重绘不重复解码。TEST25-37/13 的 SVG 真机结论保持不变。CSS 背景仍不含 background-size 和多层背景；SVG 仍缺复杂 shaping、`textPath`、逐字 dx/dy、任意 shear、径向焦点 `fx/fy` 或 spread method。单次栅格源缓冲限制为 1,048,576 像素，超大输出会降低内部采样分辨率后再缩放。
- **独立消费与编码**：next46 已确认只导入 `positron_image.dll`/`COREDLL.dll` 的 ABI 1.0 示例横竖屏工作；next47 确认 ABI 1.1 的 PNG/JPEG 内存编码与释放/回读闭环；next48 证明 WM quality=100 不能修复小图色度串扰。ABI 1.2 保留 quality=-1 的 WM 默认路径，显式 0..100 使用静态 libjpeg-turbo 1.5.3 4:4:4，next49 已确认行方向、颜色和 SOF 采样正确。ARMV4I 构建无 SIMD，显式编码会额外生成约 `width*height*3` 字节的 24bpp 中间位图；Debug DLL 增加约 238 KiB，但静态 `.lib`、源码和独立 JPEG DLL 均不部署。next50 截图确认 ABI 1.3 的复制式 padded BGR24/BGRA32、RGB/alpha PNG、JPEG 与 SVG 视觉正确；next51 又确认 ABI 1.4 的 BMP/GIF 系统 encoder、签名与回读成功。next51 的退出仍失败：WM 标题栏 X 是 Smart Minimize，不保证发送 `WM_CLOSE`。next52 以系统 `aygshell.dll` 的 `SHDoneButton` 换成标题栏 OK，并由 `IDOK` 真退出；用户已确认任务管理器进程消失且可再次启动。这不会增加底部软键或占用客户区。跨线程句柄仍未提供。
- **完成条件**：每种宣称支持的格式均有内存单测和真实 Browse 页面实例，且资源失败仍保留可访问 fallback。

## 维护规则

1. 每次真机截图改变结论时，同时更新本文件、`HANDOFF.md`、`ROADMAP.md` 和根目录 `README.md`。
2. 测试名称后的“OK”只说明其明确断言成立；必须同时写出它没有覆盖的范围。
3. 新增临时 stub、降级、硬编码测试尺寸或线程取舍时，先在此登记后续任务和完成条件。
4. 新增失败分支或暂挂方向时，同时更新 [`FAILED_EXPERIMENTS.md`](./FAILED_EXPERIMENTS.md)；该索引必须写清状态、证据、是否可重试和重启门槛。

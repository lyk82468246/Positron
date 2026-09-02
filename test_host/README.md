# `test_host.exe`

`test_host.exe` 是 Positron 公共 DLL 的回归宿主、设备验收程序和组合示例。它不是公共库，也不属于最终应用必须部署的业务核心。

宿主的职责是把 WM6 窗口/消息、native 控件、网络 I/O、设备文件系统和测试 fixture 接到公共 DLL；可复用的 URL、history、DOM、Event、表单、图像或 script-session 语义必须位于相应 DLL。

## 硬性所有权边界

`test_host` 只能包含平台适配、网络/线程调度、应用级页面提交策略、测试夹具和断言。任何可被其他应用复用的产品语义（包括 URL/资源状态、history、DOM/Event、表单默认动作、图像、脚本 session 或生命周期）必须实现于对应顶层 DLL，由宿主通过公开 C ABI 调用。不得把产品 `.c` 文件编译进宿主，也不得在宿主定义 `PBrowser_*`、`PCore_*`、`PHttp_*`、`PTls_*`、`PJson_*`、`PImage_*` 或 `PScript_*` 公共入口。提交前的 `python scripts\audit_repo.py` 会执行这两类机械边界检查；它不能替代对静态 helper 是否承载业务语义的人工审查。

## 产物与依赖

- 工程：`test_host.vcproj`
- 输出：`bin\<Configuration>\test_host.exe`
- 配置：与 EXE 同目录的 `test_host.ini`
- 自动日志：与 EXE 同目录的 `test_host.log`
- 动态依赖：TLS、JSON、HTTP、Image、Script、Core、Browser DLL
- 平台依赖：`aygshell`、common controls、WinINet 和 WM6 GUI/IME/picker

宿主还为低层移植工程提供直接回归，但外部产品应用不应模仿这种静态库测试链接方式；应用只消费顶层公共 DLL。

## 构建与运行

使用仓库正式入口：

```bat
scripts\build.bat
scripts\stage.bat Debug C:\WMShare\Positron
```

`stage.bat` 会先构建，再把同一配置的 EXE、DLL、字体和 tracked INI 放入隔离目录。不要手工从不同配置或不同时间的输出目录拼包；Windows CE 还可能继续复用旧进程加载的 DLL。

在设备 File Explorer 中运行 staging 目录里的 `test_host.exe`，或在用户已通过 GUI 建立唯一 WMDC 连接后运行自动设备门：

```bat
scripts\device_gate.bat -Candidate local-check
```

详细操作与通过标准见 [`../docs/TESTING.md`](../docs/TESTING.md)。

## 配置

```ini
auto=1
javascript=0
tests=13,20,27,999
```

- `auto=1`：按选择运行、抑制结果对话框、覆盖写日志并自动判门。
- `auto=0`：保留启动确认、说明框、可见窗口和人工操作。
- `javascript=0`：默认不执行网页 classic script。
- `javascript=1`：显式启用实验性的 Browser script session。
- `tests=`：接受编号、范围及源码明确支持的特殊编号。

没有 INI 或 INI 无效时，宿主退回内置分组选择。移走 INI 不是“自动全量”；全量自动清单由 nightly/device tooling 从当前源码 dispatch 生成。

## 测试层次

宿主中的测试大致覆盖：

- 基础 DLL 的 ABI、所有权、错误和真实网络；
- 第三方移植库的解析/链接/设备行为；
- HTML/CSS/DOM、style/layout/paint、图片/SVG 和资源 cache；
- 表单、validation、submission、native 控件和 DOM Event；
- history、navigation、script session、DOM bridge 和平台事务；
- 固定离线 compatibility corpus，把 contenteditable、dialog/form、same-document navigation 和失败回滚组合成可重复的完整流程；
- 最新的滚动几何夹具还验证 Core/Browser 的布局尺寸、retained overflow offset，以及
  `Element.scrollIntoView()` 对最近可寻址 overflow 祖先和显式 `container:"all"` 链的一次
  有限 reveal，以及 `HTMLElement.focus()` 对该链的联动；页面提交后由宿主显式触发的
  `autofocus` 目标发现和无 id focus 事件保持，以及 Browser selector 列表/组合器、
  属性操作符、结构伪类、表单状态与有界 `:not()` 查询（TEST1146–1156）；
- 真实 Browse、DPI/旋转、SIP/IME、picker 和视觉 fixture。

编号只是 dispatch key，不是功能路线图。测试的准确含义应由 fixture、断言、开始提示和失败文本表达，不在 README 复制逐编号清单。

## 浏览器组合边界

### 页面导航

宿主持有后台网络 worker、loading/取消、候选文档和窗口 swap。旧页保持可绘制，直到新页面完成 parse/resource/style/layout 并可提交。较新的导航可以取代仍在准备的候选：宿主为每个候选创建 `PBrowser_NavigationCandidateCreate` handle，每个候选独占自己的 worker、response 和资源队列；Browser handle 拥有 generation、取消请求、退休状态、committed/failed 终态和 pending/committed/failed/cancelled/stale 结果分类，宿主用 `PBrowser_NavigationCandidateCanApply` 只允许最新候选的进度、完成和提交消息生效，并用 `PBrowser_NavigationCandidateGetResult` 记录分类。退休候选在 worker 收尾后才释放，退休队列达到固定上限时新导航 fail closed 并保持当前页。页面 layout/swap 前宿主调用 Browser 的 `PBrowser_NavigationCommitGetInfo`，消费 candidate result、resource gate 和 `can_commit`，不在宿主重建“资源失败却提交”或“候选过时却 teardown”的业务规则；最终仍由 `PBrowser_NavigationCandidateMarkCommitted` 重检。URL reference 解析调用 `positron_http.dll`；history 提交调用 `positron_browser.dll`。

每个候选拥有一个 `PBrowser_NavigationResourceCreate` 事务；Browser 在该事务中按 UTF-8 URL 去重并拥有 `pending`→`ready`/`failed`/`cancelled` 终态、成功字节、required/optional gate、失败摘要和 fallback 计数。宿主只保留 URL 到 Browser resource index 的短引用，负责 DNS/TCP/TLS/HTTP、worker、取消时机和页面提交，并通过 `BeginAttempt`、`SetData`、`Fail` 或 `Cancel` 提交结果。transport 失败由宿主按 Browser 的固定预算决定是否重试（每项最多 2 次，总计最多 3 次尝试）；HTTP、resolve、budget、memory 和取消不重试。样式表与 `@import` 标为 required，脚本与图片标为 optional；style pass 发现新的 pending 时回到 worker，layout/swap 前从 Browser 读取 gate 与 candidate/resource 组合快照。相同 URL（包括重复脚本/图片和深层 `@import`）共享一个事务条目并合并 stylesheet/script/image role bitmask。required 失败、未收敛或取消保留旧页，optional 失败允许候选提交并交给 Core fallback。宿主日志读取 Browser 提供的最多 4 项不含原始 URL 的 `role/failure#hash` 摘要和 fallback family 计数；宿主不复制资源状态、字节或摘要，也没有通用逐资源提交 UI。

worker 收尾后、释放 request 前，宿主调用 `PBrowser_NavigationCleanupGetInfo` 读取 Browser 的清理快照。失败或被取代的 request 先取消剩余 pending 资源；成功 request 必须已有 committed candidate 和 READY resource gate。宿主只把 `decision`、终态、gate、pending、`can_release` 以及有界失败/fallback 观测复制到日志统计，然后销毁 candidate/resource handles；快照是调用方自己的值，不能借用 handle 内部存储。pending 工作或 committed/non-ready 不一致会保持 `can_release=0`，不会被当作成功提交。这个清理入口不拥有 worker、response、窗口或应用日志语义。

History entry 的 viewport snapshot 也由 `positron_browser.dll` 持有。宿主在离开当前页面前调用 `PBrowser_HistorySetEntryScroll` 保存当前 `(scroll_x, scroll_y)`，在提交新文档或完成 history traversal 后用 `PBrowser_HistoryEntryScroll` 读取目标坐标，再读取 Core 的 page-level `PCore_DocumentWidth/Height`，根据 client area 调用自己的 scrollbar/HWND 逻辑对两个轴 clamp/apply。宿主不再维护第二份按 entry 保存的滚动数组；Browser 不访问窗口，也不替宿主决定坐标的物理单位。元素 overflow 走下方的 Core/Browser 桥，不混入 history snapshot。

如果当前脚本 session 的 `history.scrollRestoration` 为 `manual`，宿主通过
`PBrowser_ScriptSessionGetScrollRestoration` 得到
`PBROWSER_SCROLL_RESTORATION_MANUAL` 后必须跳过这次自动 snapshot restore，保留
当前 viewport；查询失败按默认 `AUTO` 继续，不能把错误当成手动策略。fragment
定位和显式脚本滚动不受这条自动恢复门影响。

浏览器脚本启用时，宿主还注册 `PBrowserScriptScrollCallbacks`。Browser 的
`window.scrollTo()`/`scrollBy()` 请求先经过该 callback，宿主把 page 坐标按
当前 document/client extent clamp，更新滚动条、native child 和绘制位置，再
返回实际 `(x, y)`；候选文档在提交前只回显坐标，不能触碰旧页。宿主完成用户
滚动、fragment reveal 或 resize 后把物理位置换算为 CSS page 坐标，再调用
`PBrowser_ScriptSessionNotifyScroll` 同步脚本侧并触发至多一次
去重的 `scroll` 事件。同步 callback 不可重入，脚本 callback 内不会再次进入
Browser runtime；同一通知还会先更新 `visualViewport.pageLeft/pageTop` 并派发
visual viewport `scroll`，再派发 window `scroll`。

窗口收到 `WM_SIZE` 时，宿主先按新的物理 client area 重新 style/layout、
clamp 两个 page-level scroll 轴并更新 native child，再把同一尺寸按当前 DPI
换算为 CSS viewport，调用 `PBrowser_ScriptSessionNotifyResize`。因此页面的
`innerWidth`/`devicePixelRatio`、`screen` 方向、已有 `matchMedia()` 列表和
window `resize` listener 看到的是新布局后的快照；匹配结果翻转的列表会先
收到同步 `change`，随后 visual viewport `resize` 再到 window `resize`。该调用只属于
WM 接线；Browser 不访问窗口，也不替宿主运行排队的 timer 或 animation frame。若页面的 resize handler 使用
`requestAnimationFrame`，宿主必须在自己的消息循环中按需调用已有 frame pump。

窗口收到 `WM_ACTIVATE` 时，宿主把 `WA_INACTIVE` 映射为零、其他激活值映射为非零，
调用 `PBrowser_ScriptSessionDispatchWindowFocus`。Browser 因此维护脚本可见的
`document.hasFocus()`，并在状态变化时派发一次 window `blur`/`focus`；宿主仍
负责 native 控件焦点、焦点矩形以及 OEM/跨窗口策略。

DOM、libcss 和 NetSurf document 只在 UI 线程操作。worker 不持有 DOM node、box、computed style 或 HDC。

### Core 与 Browser callbacks

宿主把当前 `PCore` document 包装为 size-tagged callbacks，供 Browser session 查询 DOM、属性、表单、validation、`contenteditable` 状态、文本、布局几何和可选原生选区。布局 callback 只转发 Core 已完成 layout 的 border-box union、有限 inline 行片段、六个布局尺寸快照和关系 38/39 的 retained overflow offset；Browser 负责把它们转换为 `getBoundingClientRect()`、`getClientRects()`、只读尺寸 getter 以及有 id 元素的滚动属性，宿主不复制 box tree 或实现第二份 box model。Browser 负责脚本对象、事件顺序、取消与事务状态；宿主只执行允许的 Core mutation、WM 默认动作和导航副作用。

callback 同步且不可重入。候选页面成功提交前，宿主必须在旧 document/session 仍有效时调用 `PBrowser_ScriptSessionDispatchPageTeardown`；它负责一次性的 `visibilitychange`→`pagehide`→`unload` 边界和页面队列清理。随后宿主停止新消息和事务，销毁 native 控件、Browser session 和 Core document，避免 stale token 或借用指针逃逸。失败候选不调用 teardown，旧页状态继续服务。

### 元素 overflow 滚动

宿主不拥有元素滚动状态。脚本 `Element.scrollTo()`/`scrollBy()` 由 Browser 的
`PBrowserScriptScrollInfo.element_id` 转入 `PCore_NodeOverflowScrollToById()`；Core
返回两个轴的 clamp 后 CSS 位置，宿主只负责按 dirty rect 重绘。WM 指针命中嵌套滚动条时，
宿主调用 `PCore_OverflowPointer()`，随后用 `PCore_OverflowScrollSnapshot()` 读取目标
id/位置，再调用 `PBrowser_ScriptSessionNotifyElementScroll()`。该通知更新脚本属性并
去重派发目标元素的 `scroll`，不会再进入 scroll callback。没有稳定 id、没有 layout 或
不支持的 smooth/scroll chaining 请求必须安全 no-op；这些限制属于公共 DLL 合同，不应由
宿主 helper 绕过。

`Element.scrollIntoView()` 的对齐和祖先选择属于 Browser。默认只处理最近的可寻址
retained overflow ancestor；调用方显式传入 `container:"all"` 时，Browser 从最近者向
外最多遍历 64 层，并在每个适用祖先滚动后重新读取目标矩形。宿主只提供 Core relation、
滚动 callback、clamp、重绘和实际位置通知；TEST1148 覆盖默认 nearest，TEST1149 覆盖
`container:"all"` 的双层滚动链，TEST1150 覆盖 focus 调用对该链的联动。完整 scroll
tree、scroll chaining/anchoring、scroll-margin、smooth/inertia、匿名目标和视觉滚动条
仍不在宿主或 Browser 合同内。宿主在 Ex focus callback 中必须尊重 Browser 传入的
`prevent_scroll`，不要在 Browser 的嵌套 reveal 前抢先移动 page viewport。

页面提交后若要使用 HTML `autofocus`，宿主在 Core style/layout 和 native 子控件创建
完成后调用 `PCore_AutofocusTargetInfo` 与 `PCore_InteractionFocusAutofocus`。有 id 的
目标可复用 Browser focus bridge；无 id 的目标由宿主用 `PCore_EventDispatchFocus` 派发
focus/focusin。这个时机和选择策略属于宿主应用生命周期，目标资格和焦点节点仍属于
Core；Browser 不自行执行初始焦点，`document.activeElement` 对无 id 目标按既有合同
回退到 `document.body`。TEST1151 是该组合的离线自动夹具，不能替代设备焦点矩形、
native HWND、滚动条或 OEM 输入视觉验收。

Browser 的 `matches()`、`closest()` 与 document selector 查询由
`positron_browser.dll` 拥有。宿主只需提供已有的 DOM parent/child/sibling relation
callback；TEST1152 用离线 fixture 断言顶层 selector 列表、后代/子代/兄弟组合器、
属性值中的逗号和非法 selector 的 fail-closed 行为；TEST1153 断言六类属性操作符，
TEST1154 断言 `:root`、`:empty`、child/of-type 与四种 `nth-*` 结构伪类及其非法
输入回退；TEST1155 断言 `input:checked`、直接 `disabled` 对应的
`:disabled`/`:enabled`、直接 `required` 对应的 `:required`/`:optional`，以及属性
mutation 后的查询更新和不支持输入的回退；TEST1156 断言单一简单 compound 参数的
`:not()`、mutation 后的查询更新、组合/列表顺序和不支持参数的回退。宿主不得在测试
helper 中复制 selector
解析或匹配规则；这些语义和固定预算都属于 Browser。

### Native EDIT/SELECT/button/file

WM subclass 把键盘、focus、composition、selection 和 click 转成 Browser typed transaction。只有 Browser 允许默认动作后，宿主才写入 Core/native 控件，并把提交结果送回 Browser 产生 `input`、`change`、submit/reset 等后续事件。对 contenteditable，宿主从 `PCore_ContentEditableTargetInfo` 枚举带 id 的已布局 editing host，创建最多 16 个 WM multiline EDIT 代理；`WM_CHAR` 默认处理返回后才回读最终文本并调用 Core mutation。宿主实现的 selection callback 把 WM EDIT 的 UTF-16 位置（包括 CRLF）转换为 Browser 使用的逻辑 UTF-16 位置，并只保存原生控件的短暂状态；原生范围确定后调用 `PBrowser_ScriptSessionNotifyContentEditableSelection`，由 Browser 去重并分发一次 `selectionchange`。这样可吸收 WM6 在默认处理期间提前发送的 `EN_CHANGE`，避免把旧值或空值提交为一次 input，也避免宿主经 Core 重复派发选区事件。对 `WM_PASTE`/`WM_COPY`/`WM_CUT`，宿主读取有界 `CF_UNICODETEXT`、规范化 CRLF，并把精确 data 交给 `beforeinput`；`WM_COPY` 的折叠选区保持现有剪贴板不变，允许的 paste/cut 才执行 native default 和 Core/input 提交，格式缺失或超长时 fail closed。WinCE 原生 `WM_CUT` 可能内部重入 `WM_COPY`，宿主只在该外层默认动作期间放行同一 HWND 的重入，不让它绕过自己的外部 copy 规则。键盘/拖选 anchor、Shift 状态和捕获/取消/焦点中断收尾同样只属于宿主平台接线。

系统 picker、文件路径、SIP/IME、HWND、COMBOBOX popup、native 控件焦点和焦点视觉仍属于宿主；Browser 只拥有上述脚本层窗口状态与事件合同。synthetic 消息只能做自动契约，不能替代 OEM 设备人工验收。

### 单元素 `contenteditable`

`test_host` 把 `PBrowserScriptContentEditableCallbacks` 与 `PBrowserScriptContentEditableSelectionCallbacks` 接到当前 Core 文档，并负责在真实输入源中编排 `beforeinput`、允许后的 `PCore_ContentEditableSetTextById` 和 `input`。Browser 维护脚本可见的 `selectionStart`/`selectionEnd`/`selectionDirection` 与 `selectionchange` 事件；宿主只在存在原生 editing host 时读写对应 HWND，将 multiline 的 CRLF 位置转换为逻辑 UTF-16 位置，在无修饰 `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` 和 Shift/方向键期间保留短暂 anchor，并在原生消息完成或捕获/取消/焦点中断后调用 Browser 的通知入口。宿主窗口不保存第二份文本模型，也不经 Core 再派发选区事件。当前测试覆盖继承、`false`/未知值、`plaintext-only`、合法 UTF-8、失效目标、取消回滚、WM EDIT 的允许/取消顺序、selection range、原生 selectionchange、无修饰鼠标拖选的连续方向、键盘方向保持和中断收尾，以及 TEST1115 的 `CF_UNICODETEXT` paste/cut data、取消回滚、单次 Core mutation、折叠 caret 同步和空/不支持格式 fail-closed；TEST1116 覆盖 `WM_COPY` 非空选区复制、折叠选区 no-op、超长 UTF-8/非 Unicode 拒绝，以及原生 `WM_CUT` 内部重入保护。Range/Selection 对象、OEM 特有键盘自动重复与复杂行导航、富文本、designMode、ClipboardEvent/async clipboard、CF_TEXT 转换或 OEM IME 仍未实现。

### 绘制与交互

Core 提供 layout、page-level extent、paint、link/control/fragment geometry、可聚焦目标和支持 box 的 retained overflow offsets。宿主持有 page scrollbar、DPI/旋转、HDC、native child reposition 和 `SetFocus`，并把同一 `(scroll_x, scroll_y)` 用于 page paint、命中测试、fragment reveal 和 child reposition；元素 overflow 只按公开 pointer/dirty-rect/notification API 接线。几何或 document token 不一致时，操作应 fail closed 并等待下一次有效 layout。

## 自动与人工结果

`auto=1` 的完整通过需要：每个所选测试完成、零 `ERROR`/`FAIL`、唯一 `TESTBENCH PASS`，并且真实 Browse fixture 的页面序列正确。进程退出、提示音或部分 `OK` 都不够。

下列内容仍需人工观察：

- 字体、颜色、边距、复杂布局和滚动条；
- 真实触摸、链接命中和键盘焦点；
- SIP 候选词、OEM IME 和硬键盘；
- 系统 picker 的选择/取消/返回；
- 旋转、screen/DPI 和失败网络旧页保留。

截图和日志放在本地 `tmp/`，不要加入 Git。

## 新增测试的纪律

新增一个纵向能力时：

1. 在产品 DLL 中实现通用语义；
2. 在宿主中只接平台 callback 和 fixture；
3. 为成功、取消、非法参数、资源清理和直接相邻旧路径写自动断言；
4. 更新 dispatch、开始提示、失败文本和最终汇总；
5. 只有确实需要时增加 manual-only fixture；
6. 使用 staging override 选择本批测试，不永久扩大 tracked smoke INI；
7. 不向 README、架构或测试指南追加本批设备流水。

如果实现记录只修改 `test_host`，应先检查是不是把产品能力错误地放进了测试平台。只有平台窗口、WM 消息、设备 GUI、网络调度和 fixture 本身才通常应独占宿主修改。

## 故障排查

- 先核对进程是否真正退出、EXE/DLL 是否来自同一 build、INI 是否在同目录。
- 读取完整 `test_host.log`，不要从提示音或最后一个对话框推断全批。
- 网络问题区分 DNS、TCP、TLS、证书、HTTP、redirect、resource 和 page commit。
- WMDC/RAPI、混包、SIP/IME 和高 DPI 的详细流程见 [`../docs/TROUBLESHOOTING.md`](../docs/TROUBLESHOOTING.md)。

公共所有权见 [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

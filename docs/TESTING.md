# 测试与验收

Positron 的测试由主机静态检查、VS2008 ARMV4I 构建、自动设备门和必要的人工设备验收组成。单一层级通过不能替代其他层级：桌面脚本不能证明 ARM 二进制可用，自动首帧也不能证明真实触摸、SIP 或视觉正确。

逐测试实现以 [`test_host/main.c`](../test_host/main.c) 为准；当前候选、测试选择和最新设备证据见 [`.agents/HANDOFF.md`](../.agents/HANDOFF.md)。本文只说明长期有效的操作和判定方法。

## 测试宿主的角色

`test_host.exe` 是公共 DLL 的回归宿主和示例消费者。它覆盖基础库、渲染、真实网页、表单、事件、history/session 和显式启用的浏览器 JavaScript。

测试编号是宿主实现细节，不是公共 ABI。增加或删除测试时，应修改源码中的 dispatch 和断言，而不是在稳定文档中维护一份容易漂移的逐编号目录。

## `test_host.ini`

INI 必须和 `test_host.exe` 位于同一目录。最小配置如下：

```ini
auto=1
javascript=0
tests=13,20,27,999
```

`tests` 接受逗号或空格分隔的编号和范围，也接受源码明确支持的特殊编号：

```ini
tests=1-5 7b 13 20,999
```

不存在 INI 时，宿主进入内置的交互分组选择。文件存在但为空、不可读或格式错误时，宿主会提示并退回分组选择，不会静默扩大为全量测试。

### 自动模式

`auto=1` 时：

- 直接运行 `tests=` 选择；
- 测试确认和结果 MessageBox 被抑制；
- 同目录 `test_host.log` 每次覆盖写入；
- 自动可视测试至少绘制一帧后自行关闭；
- 任一失败令批次失败，最终必须出现唯一 `TESTBENCH PASS` 才算通过。

自动模式只证明断言、资源计数、消息路径和首帧没有失败。它不能证明字体、边距、抗锯齿、触摸命中、系统 picker 或 OEM 输入法体验。

涉及导航的自动日志会给出 Browser 资源事务的 `queued/ready/failed/cancelled/pending` 计数、resolve/transport/HTTP/budget/memory 失败分类，以及 `attempts/retries/exhausted` 计数和 `gate/required-failed/optional-failed` 结果。宿主负责 worker、网络 I/O、取消和何时重试；Browser 负责按 URL 去重、资源字节、终态、预算、gate、hash-only 摘要和 fallback observation。宿主只读取这些结果写日志，不复制第二份资源状态。

候选通过 `PBrowser_NavigationCandidate*` handle 由 Browser 拥有 generation、取消/退休状态、committed/failed 终态和 pending/committed/failed/cancelled/stale 结果摘要；宿主只保留线程、消息、退休队列和页面提交策略，并直接记录 `PBrowser_NavigationCandidateGetResult` 的快照。提交边界调用 `PBrowser_NavigationCommitGetInfo`，一次读取 candidate result 与独立 resource gate；只有组合 `decision=READY` 且 `can_commit=1` 才允许 layout/swap，最终仍需 `MarkCommitted` 重检。

transport 失败每项最多重试 2 次（最多 3 次尝试）；HTTP、resolve、budget、memory 和取消不重试。style pass 发现新的 pending 时会回到 worker；layout/swap 前必须重新检查组合快照，required stylesheet/`@import` 失败、未收敛、候选过时或取消会保留旧页，optional image/script 失败可在 Core fallback 下继续。完成的候选应没有未处理的 `pending` 项；`cancelled` 是独立终态，不应被当作网络失败。日志还可输出最多 4 项不含原始 URL 的 `role/failure#hash` 失败摘要，以及 image/script/other/observed fallback family 的粗粒度计数；它们是 Browser 提供给宿主的有界观测，不是逐资源视觉保证。

TEST1081/TEST1082 覆盖 history entry 的 viewport snapshot：same-document fragment 和跨文档 traversal 都从 Browser 读取目标 `(scroll_x, scroll_y)`，新 entry 从零开始，短页面由宿主按 document extent clamp；TEST1082 还验证非负约束、横向值保持和非法索引 fail closed。宿主不得用私有按 entry 数组替代 `PBrowser_HistoryEntryScroll`。

TEST1128 覆盖 page-level horizontal viewport：离线宽页面通过 `PCore_DocumentWidth` 暴露溢出范围，宿主对 x/y 两轴执行 clamp、滚动和 native 坐标换算，Browser snapshot 保存/恢复横向位置，fragment 命中使用同一 document-space x 坐标。它不代表嵌套 overflow 容器或复杂真实页面的视觉兼容性。

TEST1129 覆盖浏览器脚本滚动与宿主视口的双向边界：`window.scrollTo()`/
`scrollBy()` 经过 Browser 的 typed scroll callback，由宿主按 document/client
extent clamp 并返回实际坐标；滚动条或其他宿主路径再通过
`PBrowser_ScriptSessionNotifyScroll` 更新脚本侧 `scrollX`/`scrollY`；设备 DPI
边界由宿主在物理坐标与 CSS page 坐标之间换算。断言覆盖
候选式回显、越界 clamp、`scroll` 事件去重、宿主反向同步、重复通知、回调
注销和非法坐标；回调期间不得重入脚本 runtime。它仍不证明嵌套 overflow、平滑
滚动、触摸惯性或真实页面的视觉体验。

TEST1130 覆盖真实 IANA 页面目录脚本需要的布局几何：Core 在成功 layout 前
拒绝几何关系，layout 后通过现有 DOM relation callback 暴露元素 border-box 的
`x/y/width/height`，Browser 将四个整数组合成 `getBoundingClientRect()` 快照，
并随 page-level scroll 返回 viewport-relative 的 `top`。断言同时检查矩形边界
算术和滚动后的尺寸/横坐标保持不变；它不证明 transforms、Range、多片段文本、
nested overflow 或真实页面的字体/像素视觉。

TEST1131 覆盖 WM_SIZE 到 Browser script session 的 viewport resize 合同：宿主
把物理 client area 按 DPI 换算为 CSS 宽高/DPR，Browser 更新
`innerWidth`/`outerWidth`/`devicePixelRatio` 与 `screen` 方向，并同步派发一次
可信、不可取消且不冒泡的 window `resize`。断言覆盖监听器顺序、事件字段、重复
三元组去重、方向变化和负值/零 DPR 的参数拒绝；它不替代真实旋转、字体、边距、
滚动条或页面在 resize handler 中排队的 animation-frame 视觉验收。

TEST1132 覆盖 viewport/DPR 变化对已有 `MediaQueryList` 的刷新：宽度和分辨率
条件在匹配结果实际翻转时各派发一次同步 `change`，事件带有 `media`、
`matches`、target/currentTarget 和可信/取消标志；重复 resize、未变化条件以及
移除监听器保持静默。Browser 会追踪每个 session 最多 64 个列表，超出部分仍
提供创建时快照但不保证后续 `change` 通知；完整媒体查询语法仍不在范围内。

TEST1133 覆盖浏览器脚本 `visualViewport` 的有限布局视口快照：`width`、`height`
和 page scroll 会随 Browser 的 resize/scroll 通知同步，`scale` 固定为 1，
`offsetLeft`/`offsetTop` 固定为 0。断言检查 visual `resize`/`scroll` 的 target、
事件字段、可信标志、监听器顺序和重复通知去重，并确认移除 visual handler 后
window 事件仍可独立到达。它不保证 pinch zoom、嵌套 overflow、视觉像素或真实
旋转体验。

TEST1134 覆盖 history traversal 的自动滚动恢复策略：Browser 以
`PBrowser_ScriptSessionGetScrollRestoration` 暴露脚本的 `auto`/`manual` 状态，
宿主在明确 `manual` 时保留当前 viewport，在 `auto` 时才读取并 clamp Browser
保存的 entry snapshot；查询的空句柄/空输出参数会 fail closed。fragment reveal
和显式脚本滚动不属于这条自动恢复门。

TEST1135 覆盖 `screen.orientation` 的 Browser 语义：同一 session 内多次读取保持
同一个对象身份，`type`/`angle` 随布局视口方向更新；方向翻转时先在媒体列表刷新后
同步派发一次以 orientation 为 target 的可信 `change`，再进入 visual/window
`resize`，而重复或同方向 resize 不重复派发。断言同时覆盖 `onchange`、有限
`addEventListener` 监听器、事件字段、监听器移除和非法 resize 的 fail-closed
保护；不证明设备真实旋转动画、非客户区或视觉像素结果。

TEST1136 覆盖页面替换前的 `beforeunload` Browser 合同：`window.onbeforeunload`
与 `addEventListener('beforeunload', ...)` 的事件字段、调用顺序、
`preventDefault()`、非空 `returnValue`、handler 返回字符串和监听器移除。它还
检查空句柄/空输出参数的 fail-closed 行为；参考宿主没有确认对话框，因此取消时
保留当前页面，测试不把 prompt UI 当作 Browser 的职责。

TEST1137 覆盖统一脚本任务检查点：Browser 按 `phase_mask` 选择 timer、
animation frame、message 和 idle callback，固定按阶段顺序运行，并在每个阶段后
执行一次有界 microtask。断言覆盖 frame timestamp、idle timeout、message limit、
只运行 microtask 的零 mask、未知 mask 的参数拒绝，以及每个阶段的事件顺序；参考
宿主还在窗口 UI 线程以 16 ms `WM_TIMER` 调用 `PBROWSER_SCRIPT_PUMP_ALL`。这项
测试证明队列语义和宿主接线，不证明 OEM 消息调度、功耗策略或真实页面的视觉结果。

TEST1138 覆盖 Browser 页面生命周期的 pageshow 组合：`complete` 在已有的
readystatechange/DOMContentLoaded/load 序列后派发一次初始 `pageshow`，重复
`complete` 不重复；进入 hidden 时按 visibilitychange→pagehide，回到 visible
时按 visibilitychange→pageshow，重复的 hidden 值保持静默。断言同时检查
`document.readyState`、`visibilityState` 和 `persisted === false`。它是离线
脚本语义夹具，不证明 bfcache、真实后台挂起、OEM 可见性通知或视觉结果。

TEST1139 覆盖 Browser 的顶层窗口焦点合同：新 session 的
`document.hasFocus()` 初始为真；宿主调用
`PBrowser_ScriptSessionDispatchWindowFocus()` 后，状态变化按 blur/focus 顺序
派发一次可信、不可取消、不冒泡的 window 事件，并同时支持属性 handler 和普通
listener。重复的零值或非零值保持静默，非零输入归一化为 focused；测试还断言
`target`、`currentTarget`、`isTrusted`、`preventDefault()` 和状态读取。它是
离线脚本语义夹具，不证明 OEM 激活通知、native 控件焦点矩形或真实窗口切换的
视觉结果。

TEST1140 覆盖 Core 焦点 id 到 Browser `document.activeElement` 的可选桥：未设置
焦点或焦点节点没有 id 时返回 `document.body`，有 id 时通过现有 DOM read adapter
解析到对应元素；清除焦点和注销 callback 都保持安全回退。断言还覆盖 Core
UTF-8 size-probe、过小缓冲的完整字节数和不修改输出、注册后 getter 的同步读取，
以及空/无效 id 的 fail-closed 行为。它是离线脚本/Core 语义夹具，不证明完整
浏览器焦点算法、native HWND 焦点矩形、自动初始焦点、焦点陷阱或真实窗口切换
视觉；这些仍由宿主和人工设备验收负责。

TEST1141 覆盖 `HTMLElement.focus()`/`blur()` 的可选 Browser→宿主桥：脚本请求按
 id 经过 Core 的已布局焦点资格检查，focus 在切换目标时先发旧目标的 blur/focusout，
 再发新目标的 focus/focusin；对非当前目标的 blur、disabled/plain 目标和注销后的
 bridge 都保持 no-op，`document.activeElement` 与 body 回退保持一致。它同时验证
 Core `PCore_FocusTargetInfoById`/`PCore_InteractionFocusById` 的边界和固定
 native-function 预算回归。页面级滚动可见性与 options 结果由下一条夹具单独验证；
 本测试不证明完整焦点算法、自动初始焦点、焦点矩形、真实 native HWND/OEM 控件或
 跨窗口视觉。

TEST1142 覆盖 focus request Ex bridge 的页面级 viewport 组合：默认
`HTMLElement.focus()` 会把 Core layout 矩形滚入宿主的 page-level viewport，Browser
在宿主 callback 返回后才同步实际 CSS `scrollY` 并派发一次 window `scroll`；
`focus({preventScroll:true})` 更新 active element 和 focus family，但保留原滚动
位置且不产生滚动事件。夹具同时断言目标矩形在 viewport 内、Core geometry 与
Browser `getBoundingClientRect()` 使用同一滚动偏移，以及 callback 内不重入脚本。
它专门覆盖 page-level 路径；焦点与嵌套 retained overflow 的联动由 TEST1150 覆盖。

TEST1143 覆盖页面级 `Element.scrollIntoView()`：默认 block start/inline nearest、
布尔值 `false` 的末端对齐，以及有限的 center 对齐都复用既有 page-level scroll
callback，并在实际变化时只产生一次 window `scroll`；已经可见的 nearest 请求保持
静默。夹具还断言不支持的 `behavior:"smooth"` 安全拒绝，不改变 viewport 或事件
队列。它不证明 scroll-margin、nested overflow、平滑/惯性滚动、真实触摸/滚动条
视觉或复杂布局中的完整浏览器对齐算法。

TEST1144 覆盖 `Element.getClientRects()` 的有界单矩形集合：每次调用都会得到新的
array-like 集合和矩形，`length`、索引 `0` 与 `.item(0)` 与同一元素的
`getBoundingClientRect()` 保持一致，越界项返回 `null`；集合随 page-level scroll
更新，隐藏元素返回空集合。它不证明 transforms、inline 多片段/Range、nested
overflow 或视觉像素精度。

TEST1145 覆盖 Core 到 Browser 的 inline 多片段组合：窄容器中的带边框文本被分成
多个按行排列的正尺寸矩形，Core 的 count/index relations 与 Browser 的
`getClientRects()` 使用同一快照，`getBoundingClientRect()` 是这些片段的 union。
夹具同时检查每次调用的新集合/新矩形身份、`.item()` 边界、索引顺序和旧的
`getClientRects()` 单矩形行为不回退。它不证明 transforms、Range/Selection、
nested overflow、pinch zoom、复杂字体度量或视觉像素精度。

TEST1146 覆盖 Core 到 Browser 的布局尺寸快照：支持的 block box 暴露
`offset*`、`client*` 与 `scroll*` 六个整数 CSS 像素 getter，offset/client 的
border/padding/retained-scrollport 关系以及有界后代 extent 由离线夹具断言；Browser
还检查 getter 为只读、descriptor 存在、隐藏元素安全返回 `0`，Core relation 与脚本值
保持一致。该测试不证明元素滚动操作或真实滚动条呈现。

TEST1147 覆盖带 DOM `id` 的嵌套 overflow box：Core 的关系 38/39、按 id setter
和非负 clamp 与 Browser 的 `scrollLeft`/`scrollTop`、`scrollTo()`、`scrollBy()`
保持一致；脚本请求只接受 `auto`/`instant`，实际位置改变时目标元素收到一次非冒泡、
不可取消的 `scroll` 事件。夹具还把 Core 的 retained scrollbar pointer snapshot
通过宿主 adapter 送回 `PBrowser_ScriptSessionNotifyElementScroll()`，验证 host-driven
位置同步和事件去重。它不证明完整滚动容器树、scroll chaining/anchoring、nested
`scrollIntoView()`、smooth/inertia、匿名目标、触摸手势或滚动条视觉。

TEST1148 覆盖 `Element.scrollIntoView()` 对最近可寻址 retained overflow 祖先的有限
嵌套 reveal：Core 关系 40/41 报告两个轴是否有 retained scrollbar，42/43 提供
padding/client edge 文档坐标，Browser 沿 `parentElement` 最多遍历 64 层并只移动最近
祖先。断言覆盖两个轴的 start 对齐、页面 viewport 保持、目标元素一次非冒泡 scroll
事件、重复 nearest 静默以及 smooth 请求拒绝；没有可用嵌套 client bridge 时仍应回退
到既有 page-level 行为。它不证明完整 scroll tree、scroll chaining/anchoring、
scroll-margin、平滑/惯性滚动、匿名目标、复杂布局或真实滚动条/触摸视觉。

TEST1149 覆盖 `Element.scrollIntoView({container:"all"})` 的有界多层 reveal：离线
夹具包含两个 retained overflow ancestor，断言 Browser 从最近到最外层依次重新读取
目标矩形并移动两个滚动容器，页面 viewport 保持稳定，元素 `scroll` 事件顺序为
inner→outer。重复 `nearest` 请求、未知 `container` 和 `behavior:"smooth"` 都必须
保持位置与事件队列不变；64 层上限、完整 scroll tree、scroll chaining/anchoring、
scroll-margin、平滑/惯性、匿名目标和视觉滚动条仍不由该测试覆盖。

TEST1150 覆盖 `HTMLElement.focus()` 与有限 nested `scrollIntoView()` 的组合：Browser
发现可寻址 retained overflow ancestor 时，Ex callback 的 `prevent_scroll` 会要求宿主
暂缓 page-level reveal，随后 Browser 依次滚动 inner→outer，并保持 page viewport 不变。
夹具断言两个轴的 retained offset、目标在两层 client edge 内、focus/focusin→scroll
事件顺序、重复 focus 静默、`focus({preventScroll:true})` 不移动任何滚动容器，以及
远端目标 `blur()` 不触发滚动。它不证明完整 scroll tree、scroll chaining/anchoring、
scroll-margin、平滑/惯性滚动、真实触摸滚动或 OEM 控件焦点视觉。

TEST1151 覆盖页面提交后由宿主显式触发的有界 `autofocus`：Core 按 DOM 顺序跳过
hidden、disabled 和未布局目标，提供 UTF-8 id size-probe 与 geometry 快照，并只更新
Core focus node；有 id 目标复用 Browser focus bridge，无 id 目标通过
`PCore_EventDispatchFocus` 保留焦点节点并派发 focus/focusin。夹具同时断言重复应用
不重复派发、移除 id 后仍能安全完成 Core 事务、过小 id buffer 不发生部分写入，以及
Browser 的 id-addressable `document.activeElement` 按合同回退到 `body`。它不证明
完整焦点算法、Browser 自主初始焦点、native HWND/焦点矩形、完整滚动树或 OEM 视觉。

TEST1152 覆盖 Browser selector 子集的组合查询：顶层逗号列表、后代空格、子代 `>`、
相邻兄弟 `+` 和一般兄弟 `~` 在 `matches()`、`closest()`、`querySelector()` 与
`querySelectorAll()` 中保持一致，带逗号的属性值不会被误拆分，查询结果按文档顺序
返回 snapshot。夹具还断言三段组合链、非法/空 selector 的 fail-closed 结果，以及
`closest()` 的列表匹配。组合链和祖先/兄弟遍历各自有 64 步上限；动态伪类、伪元素、
属性大小写修饰符、namespace、shadow DOM 和完整 CSS Selectors 语法仍不在范围内；
已实现的结构伪类由 TEST1154 单独覆盖。

TEST1153 覆盖 Browser selector 的有界属性操作符：`=`, `^=`, `$=`, `*=`, `~=`, `|=`
在 `matches()`、`closest()`、`querySelector()` 与 `querySelectorAll()` 中支持精确、前缀、
后缀、子串、空白分词和语言前缀匹配。夹具同时覆盖通配标签、组合器、顶层列表顺序、
引号内的空格/逗号/`]`、空操作数、未闭合引号、未支持的大小写修饰符和无属性值的
fail-closed 结果；属性名、值和 selector 链仍受 Browser 的固定大小/步数预算约束。

TEST1154 覆盖 Browser selector 的有界结构伪类：`:root`、`:empty`、`:first-child`、
`:last-child`、`:only-child`、四种 `*-of-type` 变体，以及 `:nth-child()`、
`:nth-last-child()`、`:nth-of-type()` 和 `:nth-last-of-type()` 的整数、`odd`/`even`
与受限 `an+b` 公式。断言同时覆盖列表、`matches()`、`closest()`、空元素与文本元素，
并确认空公式、`of` 过滤、伪元素和超大数值安全 fail closed。结构判断使用
只读 DOM 关系快照；遍历、公式系数和脚本 heap 都有固定上限，不代表完整 CSS Selectors。

TEST1155 覆盖 Browser selector 的有界表单状态：`input:checked` 读取现有 checked
callback 的当前值，`:disabled`/`:enabled` 读取 input、button、select、textarea、option
的直接属性，`:required`/`:optional` 读取 input、select、textarea 的直接
`required` 属性。夹具通过 `matches()`、`closest()`、`querySelector()` 和
`querySelectorAll()` 断言初始状态、checked/属性 mutation 后的实时结果、列表顺序和
组合查询，并确认带参数、交互伪类、`:not()` 的不支持参数、伪元素和非表单元素安全
fail closed。
option 的动态 selected→`:checked` 映射由 TEST1157 单独覆盖，也不代表完整 CSS
Selectors 语法。fieldset/optgroup 的 effective disabled 继承由 TEST1166 覆盖。

TEST1156 覆盖 Browser selector 的有界 `:not()`：参数只能是一个不含伪类、伪元素、
列表或组合器的简单 compound（标签、`#id`、`.class`、属性存在或精确 `=` 值），并在
`matches()`、`closest()`、`querySelector()` 与 `querySelectorAll()` 中保持一致。夹具
覆盖属性/类 mutation、后代与子代组合、顶层列表顺序和多个 `:not()` 链；空参数、
逗号列表、空格/组合器、嵌套伪类、伪元素和非精确属性操作符都必须 fail closed。该能力
覆盖 `details:not([open])`、`:not(summary)` 等本项目所需的有限场景，不代表完整 CSS
Selectors 语法。

TEST1157 覆盖 Browser selector 对 `<option>` 动态 selected 状态的 `:checked` 映射：
单选初始选择、`selectedIndex` mutation、多选初始选择、`matches()`、`closest()`、
顶层列表顺序和 input/option 的区分都必须保持一致；带参数、非 option 元素、嵌套伪类
和伪元素仍安全 fail closed。Core 的 checked getter 只扩展为读取 option.selected，
option 选择 mutation 仍由 select/option API 完成。

TEST1158 覆盖 Browser selector 对 Core 约束验证结果的有界 `:valid`/`:invalid` 映射：
required 空值、value mutation、custom validity mutation、form 聚合结果和
`willValidate` 非候选排除都必须保持一致；带参数、非验证元素、嵌套伪类和伪元素仍安全
fail closed。该测试复用既有 validation callback，不改变验证规则或生成 native UI。

TEST1159 覆盖 Browser selector 对焦点状态的有界映射：`:focus` 只匹配当前
activeElement，`:focus-within` 沿最多 64 层的已寻址祖先链匹配；焦点切换、blur 清理、
`matches()`、`closest()`、两种 query 和顶层遍历保持一致。带参数、伪元素和注销
activeElement callback 的输入必须 fail closed。该测试复用既有 Core focus/activeElement
桥，不改变宿主 native focus、焦点矩形或 OEM 输入行为。

TEST1160 覆盖 Browser selector 对静态链接状态的有界映射：`:link`/`:any-link` 只匹配
带 `href` 属性的 `<a>`/`<area>`，空属性值也算链接；移除或新增 `href` 后，
`matches()`、`closest()`、`querySelector()` 和 `querySelectorAll()` 的结果与文档顺序
必须立即更新。带参数、伪元素和尾随逗号等不支持输入必须 fail closed。Browser 不保存
visited history；脚本 `:active`/`:hover` 由 TEST1165 的可选 interaction callback
单独覆盖，真实链接绘制、pointer 时机、导航和历史样式仍属于宿主集成与人工观察。

TEST1161 覆盖 Browser selector 的有界 `:target`：当前 URL fragment 经
`decodeURIComponent` 后与元素当前非空 `id` 相等时，`matches()`、`closest()`、
`querySelector()` 和 `querySelectorAll()` 只返回该目标；fragment 导航、百分号编码、
id mutation、无 fragment 和 malformed percent-encoding 均按当前 session 状态更新。
仅有 `name` 的 named anchor、带参数的 `:target(foo)`、伪元素和尾随逗号必须
fail closed；stale wrapper 读取也只能安全不匹配。该合同只决定 selector 结果，
不承诺 fragment reveal、页面滚动或真实设备视觉。

TEST1162 覆盖 Browser selector 的有界 `:lang()`：当前元素及最多 64 层父链上的
`lang`/`xml:lang` 按大小写不敏感的精确值或 `-` 子标签前缀匹配，元素 `lang` 优先，
空值停止继承；`matches()`、`closest()`、`querySelector()` 和 `querySelectorAll()`
都使用同一实时状态。夹具验证属性 mutation、`xml:lang` fallback、无覆盖时的祖先继承、
空参数、语言标签列表、引号形式、伪元素和尾随逗号的 fail-closed 行为；它不实现完整
BCP 47/namespace 语言解析，也不替代字体、布局或设备视觉验收。

TEST1163 覆盖 Browser selector 的有界正向分组：`:is()` 与 `:where()` 接受最多 16 个
逗号分隔的简单 compound 分支，并在 `matches()`、`closest()`、`querySelector()` 和
`querySelectorAll()` 中保持同一匹配和文档顺序。夹具验证类/属性分支、子代组合、
查询与 `closest()`、类 mutation 后的实时结果，以及空分支、嵌套伪类、组合器、伪元素、
未闭合和尾随逗号的 fail-closed 行为；不承诺完整 Selectors 语法或 specificity 计算。

TEST1164 覆盖 Browser selector 的有界 `:has()`：每个伪类最多接受 16 个相对简单
compound 分支，支持无前缀后代、`>` 直接子代、`+` 相邻兄弟和 `~` 后续兄弟关系；每个
后代或兄弟遍历最多 64 步。`matches()`、`closest()`、`querySelector()` 和
`querySelectorAll()` 使用同一实时结果，夹具还验证属性操作符、`button:disabled`、
类/属性 mutation、分支数量上限和查询顺序。空分支、链式关系、伪元素、未闭合或尾随
逗号等输入必须 fail closed；这不是完整 Selectors `:has()` 或任意相对 selector 语法。

TEST1165 覆盖 Core 指针交互状态到 Browser selector 的可选映射：宿主以
`PBrowserScriptInteractionCallbacks` 读取 `PCore_InteractionStateElementId` 的
`active`/`hover` id，`matches()`、`closest()`、`querySelector()` 与
`querySelectorAll()` 只匹配当前精确节点。夹具覆盖 hit-test 后的状态切换、Core
size-probe 和过小缓冲、`:active`/`:hover` 的参数/伪元素拒绝，以及注销 callback
后的 fail-closed 行为。Browser 不自动派发 pointer 事件、重做 style/layout 或重绘；
真实触摸、按下/释放时机、pointer capture 和视觉仍由宿主与设备验收。

TEST1166 覆盖 Core effective-disabled relation 到 Browser selector 与表单提交的统一：
disabled fieldset 的 first-legend exemption、disabled optgroup 对 option 的继承、
fieldset/optgroup 属性 mutation、`matches()`/两种 query、关系 size-probe、disabled
option 的选择拒绝和 successful form submission 排除均由同一离线 fixture 断言。旧宿主
未提供 relation 44 时 Browser 回退到直接属性；该测试不扩展完整 CSS Selectors、native
SELECT popup 或设备视觉承诺。

TEST1167 覆盖 Browser selector 对 Core 范围验证结果的有界 `:in-range`/`:out-of-range`
映射：仅支持 input 的 number/range/date/month/week/time/datetime-local 类型；非空值且
具有 `min`/`max`/`step`（range 类型的默认范围也算）时读取既有 validation callback 的
underflow/overflow 标志。空值、bad/type mismatch、disabled/readonly、无范围限制和
非 input 元素两者都不匹配；stepMismatch 本身不算 out-of-range。夹具覆盖 matches/closest/
query、值和约束属性 mutation、查询顺序及带参数/伪元素/非法列表的 fail-closed 行为；
它不扩展完整 Selectors、原生范围控件视觉或本地化 validation UI。

TEST1168 覆盖 Browser selector 的有界 `:read-only`/`:read-write`：文本输入类型与
`textarea` 在未 readonly 且未 effective-disabled 时匹配 `:read-write`，Core 的
`isContentEditable` callback 让显式或继承的 editing host 也能匹配；readonly、禁用、
不支持编辑的 input 类型和普通元素匹配 `:read-only`。夹具覆盖属性 mutation、祖先继承、
`matches()`、`closest()`、三种 query、结果顺序和注销 contenteditable callback 后的
fail-closed 行为；不承诺完整 CSS Selectors、富文本编辑或 native 输入视觉。

TEST1169 覆盖 Browser selector 的有界 `:placeholder-shown`：省略 `type` 或使用
`text`、`search`、`url`、`tel`、`email`、`password` 的 input，以及 textarea，在 live
`value` 为空且 `placeholder` 值非空时匹配；不支持的 input 类型、空 placeholder、普通
元素和带参数/伪元素形式必须 fail closed。夹具覆盖初始状态、value/type/placeholder
mutation、`matches()`、`closest()`、两种 query 的文档顺序和非法输入；不承诺 native
placeholder 绘制、SIP/IME 或设备视觉。

TEST1170 覆盖 Core 显式 form-owner 到 Browser `Element.form`/`HTMLFormElement.elements` 的
有界映射：默认最近祖先归属、`form="id"` 的跨树控件、存在属性时不回退祖先、空值/无效
目标无 owner，以及 form 控件集合的文档顺序、`namedItem()`、label association 和
mutation 后重新查询。旧集合保持 snapshot identity，不会原地变成 live collection；宿主
只提供既有 DOM relation/attribute callback，不复制表单归属或集合规则。

TEST1171 覆盖同一显式 form-owner 规则在 Core 生命周期中的消费：form 外的
`form="primary"` input/textarea 被纳入 DOM-only validation、`reportValidity()` 的
invalid-event 扫描、urlencoded successful-control 序列和按坐标的外部 submit/reset 激活；
reset 恢复这些控件的初始值，空 required 值再次阻止提交，另一个 form 的控件不会混入。
这是离线自动合同，不承诺 native 表单视觉、SIP/IME、picker、触摸或不同 DPI 结果。

TEST1172 覆盖 `PCore_FormResetById` 的无坐标 state-only reset：同一 owner 规则会恢复
form 子树内以及显式 `form="primary"` 的外部 input、checkbox、select 和 textarea，
而无效 owner 的控件保持原值；缺失 id、非 form、空 id 和 NULL 参数均 fail closed。
调用方仍须在该入口前完成可取消 reset 事件策略，并在成功后重新 layout/paint；该夹具
不承诺 native 表单视觉、SIP/IME、picker、触摸或不同 DPI 结果。

TEST1173 覆盖 Browser 脚本 `HTMLFormElement.reset()` 的完整事务：第一次调用由 form id
派发 `bubbles=1`、`cancelable=1` 的 `reset` 事件，listener 通过 `preventDefault()` 取消
时，form 子树和显式 `form="primary"` 外部控件的 live state 均保持不变；第二次调用在
事件允许后进入宿主 reset callback，由 Core 恢复 input、checkbox、select 和 textarea 的
初始值，并在参考宿主重新 layout。夹具还断言 `event.target === event.currentTarget`、
事件顺序、两次调用计数、方法返回 `undefined` 以及无重复事件。它验证的是 Browser/Core
事务和 adapter 合同，不承诺 native 表单视觉、SIP/IME、picker、触摸或不同 DPI 结果。

TEST1174 覆盖 Browser 脚本 `HTMLFormElement.requestSubmit([submitter])` 的完整事务：
无效 required 值先阻止默认动作，`formnovalidate` submitter 可绕过约束，listener 的
`preventDefault()` 会取消提交，省略 submitter 使用无按钮的 successful-control 序列；
非法 submitter、非 form receiver 和缺少 id 的目标均安全失败。夹具断言 validation→
可取消 `submit`→Core urlencoded 默认动作的顺序、`target/currentTarget`、POST action/body、
调用次数及 `undefined` 返回值；宿主只接线和记录导航，不复制验证或 submitter 规则。
该夹具不承诺 native 表单视觉、SIP/IME、picker、触摸或不同 DPI 结果。

TEST1175 覆盖 Browser 脚本 `HTMLFormElement.submit()` 的直接默认动作：三个带 required
控件的 form 分别走 urlencoded POST、`method="dialog"` 和 multipart；直接方法返回
`undefined`，跳过 constraint validation、submit 事件和 submitter，因此空 required 值
仍进入 Core 的成功控件结果，dialog return value 与 multipart submitter 也保持为空。
夹具同时检查 validation callback 为零、三个 direct/default callback 各调用一次、方法
分类、POST body 和 multipart action。宿主只接线到 Core 的 NoValidationById primitive、
记录应用策略并断言，不复制表单语义；该夹具不承诺 native 表单视觉、SIP/IME、picker、
触摸或不同 DPI 结果。

TEST1176 覆盖 Browser `new FormData(form)` 的 detached snapshot：Core 按统一 form-owner
和 successful-control 规则收集 form 子树及显式 `form="id"` 外部控件，保留 checkbox、
重复 select 和 textarea 顺序，排除 disabled、未勾选、unnamed 与 submit 控件。夹具确认
返回对象身份、查询/重复项、后续 value mutation 不会改写旧 snapshot，第二次构造读取新
状态，未派发 submit 事件；宿主只接线
`PBrowserScriptFormDataCallbacks` 和 Core snapshot API。该桥最多 64 项并有受限 UTF-8
字段，文件只返回 metadata；不承诺完整 live collection、文件读取或 native 表单视觉、
SIP/IME、picker、触摸和不同 DPI 结果。
TEST1177 覆盖 Browser `new FormData(form, submitter)` 的 Ex bridge：启用的 submit-type
input/button（包括显式 `form="id"` 的外部控件）按 successful-control 与文档顺序加入快照；
`null`/`undefined` 等同省略 submitter，普通控件、禁用控件、其他 form 的 submitter 和
伪造的普通对象均安全抛出 `TypeError`。夹具确认每次构造仍是 detached snapshot，且不会
派发 submit 事件或执行默认动作；宿主只接线 `PBrowserScriptFormDataCallbacksEx` 与
`PCore_FormDataByIdEx`，不复制表单收集语义。该桥继续受 64 项及 UTF-8 字段容量限制，
文件只返回 metadata。

TEST1178 覆盖 Browser `new FormData(form[, submitter])` 的 `formdata` 事件：构造完成
成功控件快照后同步派发非冒泡、不可取消的 `FormDataEvent`，事件的 `formData` 与
返回对象保持同一身份；监听器可在构造返回前追加字段，`form.onformdata` 也会按正常
EventTarget 规则接收事件。夹具确认 `target`/`currentTarget`、构造器继承关系、
阻止默认无效、body 不冒泡且不触发 submit；宿主只提供 fixture 和断言，事件与 FormData
对象语义仍属于 Browser。

TEST1179 覆盖 Browser selector 的可选 `:visited` 映射：宿主以同一 interaction Ex
callback 返回明确批准的访问结果，Browser 对 `<a>`/`<area>` 的绝对/相对 href、fragment
和空 href 在 `matches()`、`closest()`、`querySelector()`、`querySelectorAll()` 中
保持一致；href mutation 与查询顺序即时更新，带参数、伪元素、尾随逗号和注销 callback
安全 fail closed。Browser 不写入或持久化 history，也不导航、重做 style/layout/paint；
宿主负责 URL 解析、历史来源和隐私策略。该离线门不证明真实链接颜色、visited 隐私隔离、
跨窗口 history 或设备视觉。

TEST1180 覆盖 Browser selector 的有界 `:scope` context：element query 把 receiver
作为 scope，`:scope > ...` 与 `:scope ...` 分别匹配直接子代和后代，并在带 scope 的
查询中按文档顺序把 owner 放在结果首位；不带 `:scope` 的 element query 仍排除 owner，
document query 把 `document.documentElement` 作为 scope。`matches()`/`closest()`
以 receiver 作为 scope，因此 `node.matches(':scope')` 与 `node.closest(':scope')`
只命中自身。带参数、伪元素、尾随逗号及嵌套在当前不支持参数中的 scope 必须 fail closed；
宿主只提供既有 DOM relation callback，不复制 selector 解析、scope 或排序语义。

TEST1181 覆盖 Browser selector 的有界 `:default`：checkbox/radio 读取 content
`checked` 属性作为 default state，`option` 通过 Core relation 45 读取
default-selected 快照，submit-capable button/input/image 选择其 form 中按文档顺序的
第一个 submit control。夹具分别断言初始状态、query 顺序、移除默认属性、live
`.checked`/`selectedIndex` mutation、`matches()`/`closest()` 以及带参数、伪元素和尾随
逗号的 fail-closed 行为；宿主只注册既有 Core DOM relation/attribute callback，不复制
默认状态或 selector 解析。为保持 WM6 上固定的 768 KiB Browser heap，初始、mutation 和
非法输入断言使用多个短脚本 session；这是一种测试编排约束，不是扩大运行时预算的承诺。

TEST1182 覆盖 Browser/Core 的 `<option>` `selected`/`defaultSelected` 属性桥：脚本先读
取单选与多选的 live/default 状态，再通过 `selected` setter 验证单选互斥、多选独立选择
和 `selectedIndex` 的一致性；通过 `defaultSelected` setter 验证默认基线可独立更新，
不会偷偷改写当前 live 选择。非 option 目标、缺失 adapter 和无效 id 必须安全失败；
宿主只注册独立的 `PBrowserScriptOptionCallbacks` 并转调 Core 按 id API，不复制选择或
默认状态规则。该离线门不承诺 native SELECT popup、触摸、SIP/IME、视觉或不同 DPI。

TEST1183 覆盖 Browser `<option>` 的基础 `value`/`label`/`text` 属性桥：脚本先验证显式
`value`/`label` attribute 优先，缺失时分别回退到 option 文本，`text` 读写会更新文本
以及后续的 option/select 读取；`select.value` 与 `selectedIndex` 在 live 选择变化后
保持一致。夹具还区分空 attribute 与缺失 attribute，并确认非 option setter、无效目标或
失败 mutation 安全拒绝。实现复用既有 Core DOM attribute/text callback，不增加 ABI 或
native slot；宿主只做回调接线和断言。该离线门不承诺 native SELECT popup、键盘/触摸、
SIP/IME、layout/paint 或不同 DPI 视觉。

TEST1184 覆盖 Browser `select`/`option` collection 的有界组合：`options` 与
`selectedOptions` 按可寻址 option 的文档顺序生成独立 HTMLCollection snapshot，支持
`item()`/`namedItem()`；`selected` mutation 会在下一次读取时反映，`select.length` 与
`option.index` 保持一致，optgroup 本身不进入列表。夹具还确认 snapshot 数组的本地
修改不会回写 DOM，非 select/option 目标安全返回。实现只复用现有 Core DOM relation、
selected 和 attribute callback，不增加 ABI 或 native slot；每次最多遍历 256 个节点、
返回 64 个 option，缺少稳定 id 的元素不可寻址。该离线门不承诺完整 live
HTMLCollection、`length` setter、append/remove、native SELECT popup、键盘/触摸、
SIP/IME、layout/paint 或不同 DPI 视觉。

TEST1185 覆盖 Browser `<option>.form` 的有界 owner 投影：嵌套 `optgroup` 通过最多 64 层
可寻址父链找到所属 `select`，再复用 `select.form`；显式 `select form="id"`、form
attribute mutation、无 owner、无效 owner 和非 option 目标分别验证成功或 `null`，并
确认既有 input owner 不回退。宿主只提供现有 DOM relation、attribute mutation 和断言，
不复制 form-owner 规则；该门不承诺完整 HTML option/form 算法、native SELECT popup、
键盘/触摸、SIP/IME、layout/paint 或不同 DPI 视觉。

TEST1186 覆盖 Browser 的 select/optgroup 元数据：`select.type` 在 `multiple` 属性的
live mutation 下保持 `select-one`/`select-multiple`，只读 setter 不写入 `type` attribute；
`optgroup.label` 读取、mutation 和缺失 attribute 的空字符串回退，`option.label` 的文本
回退与非目标元素的安全结果同时断言。宿主只提供现有 DOM attribute/text callback、脚本
接线和 fixture，不复制属性语义；该离线门不承诺 native SELECT popup、键盘/触摸、SIP/IME、
layout/paint 或不同 DPI 视觉。

TEST1187 覆盖 Browser/Core 的 `HTMLFieldSetElement` 组合：`fieldset.type` 为只读
`fieldset`，`fieldset.form` 复用祖先与显式 `form="id"` owner 规则，`fieldset.elements`
按 DOM 顺序返回含嵌套 fieldset 控件的独立 HTMLCollection snapshot；`item()`/
`namedItem()`、snapshot 隔离、属性 mutation、无效 owner 和非 fieldset 空集合均由同一
fixture 断言。集合只投影带稳定 id 的 input/select/textarea/button，最多遍历 256 个节点、
返回 64 项；其他 listed elements、无 id 节点、append/remove、native 控件视觉、键盘/触摸、
SIP/IME 和不同 DPI 仍不在契约内。

TEST1188 覆盖 `HTMLFormElement.elements` 与 fieldset owner 的组合：form 关系按文档顺序
返回有 id 的 input、select、fieldset 及其后代控件，并保留 `item()`/`namedItem()`、独立
snapshot、显式 `form="id"` mutation 和无效 owner 的 fail-closed。Core 关系层同时确认
fieldset 出现在 `FORM_CONTROL_COUNT/AT`，但 `PCore_FormDataById` 的 successful-control
快照仍只包含可提交控件，不把 fieldset 当作字段。宿主只提供现有 DOM relation、attribute
callback 和断言；不新增 ABI、native slot、live collection、append/remove 或视觉/平台输入
保证，后者仍需人工验收。

TEST1189 覆盖 `output` 的 form-associated 组合：Core/Browser 按文档顺序把 form 内及显式
`form="id"` 的 output 纳入 `form.elements`，fieldset 子树也能枚举 output；`output.form`、
`labels`、`item()`/`namedItem()`、snapshot 隔离、name/form mutation 和无效 owner 均有断言。
同一 fixture 通过 `PCore_FormDataById` 确认 output 不进入 successful-control 快照。该 fixture
不覆盖 `img` 的 owner-only 投影、其他尚未覆盖的 form-associated 扩展、live collection、
native 表单视觉或平台输入保证。

TEST1190 覆盖 output 的值状态纵切：Core 直接断言 `value` 反映 descendant text，设置
`value` 后保留独立的 default override，`defaultValue` 在有 override 时只改默认基线，
`PCore_FormResetById` 恢复默认并清除 override；随后 `form.reset()` 的 Browser bridge
验证同一语义可由脚本观察。夹具还确认 output 仍不进入 successful-control/FormData，且
`output.type` 为只读的 `output`。该门不承诺完整 output listed-content 算法、live collection、
native 表单视觉或平台输入保证。

TEST1191 覆盖 `<object>` 的 listed form-associated 组合：Core/Browser 按文档顺序枚举
form 内、fieldset 子树内和显式 `form="id"` 的 object；`object.form`、
`fieldset.elements`、`item()`/`namedItem()`、owner mutation 与无效 owner 均有断言。
object 只进入 owner/collection snapshot，不进入 validation、successful-control 或
FormData，也不触发 plugin、替代内容窗口或 native 控件接线；其他尚未覆盖的
form-associated 扩展仍不在本批范围。

TEST1192 覆盖 `<img>` 的 form-associated owner-only 组合：Core/Browser 解析 form 内、fieldset
子树内和显式 `form="id"` 的 img owner，并验证 `form.elements`/`fieldset.elements` 不包含
img；`img.form`、无效 owner、owner mutation 与 snapshot/命名集合排除均有断言。img 不进入
validation、successful-control 或 FormData；该门不改变既有图片资源发现、解码、layout/paint，
也不承诺 native 图像视觉、完整 form-associated 扩展或 live collection。

TEST1193 覆盖 `HTMLImageElement` 的有界元数据与 Core 资源状态：离线 Browser fixture
断言 `document.images` 的 DOM 顺序 snapshot、raw `src`/`srcset`/`sizes`/`useMap` 等
attribute reflection、`crossOrigin` 的 `null` 回退、boolean/尺寸 setter 边界、
非 `img` 的安全结果，以及 `naturalWidth`/`naturalHeight`/`complete` 的资源状态投影；
Core fixture 另外断言成功 SVG、终态 fetch failure、无 source 和仅有 `srcset` 的状态，
并验证 layout 后 retained decode 才暴露自然尺寸。该门不实现或承诺 `srcset`/`sizes`
选择、绝对 URL、CORS/referrer enforcement、Promise/事件生命周期、image-map 命中、
完整 loading/fetch-priority 策略或 native 图像视觉；宿主只提供资源 callback、DOM 接线、
fixture 与断言，产品语义位于 Core/Browser。

TEST1194 覆盖 `HTMLImageElement.decode()` 与宿主驱动的图像终态事件：脚本先验证无 source、
pending、成功尺寸、终态失败、非 `img` 目标和 source mutation 的 Promise 状态；Core fetch/
style/layout 使成功与失败关系可观察后，宿主通过
`PBrowser_ScriptSessionNotifyImageEvent` 派发可信、非冒泡、不可取消的 `load`/`error`，
并断言重复通知幂等、相反/过时/缺失 id fail closed、Promise settle 顺序和 page teardown
对剩余 pending 请求的 `AbortError`；每个 session 的 image/source 终态映射最多 64 项，
source 改变会释放旧项，超限的新终态通知保持 fail closed。该门不覆盖 `srcset`/`sizes`
选择、绝对 URL、CORS/referrer enforcement、完整 loading 策略、image-map 命中或 native
图像视觉；宿主只负责先更新 Core relation、调用通知入口和泵出 microtask，产品语义位于
Browser/Core。

TEST1195 覆盖 Core 的有界 image-map 命中纵切：已布局 `<img usemap>` 解析对应 `<map>`，
按 DOM 顺序处理 linked `<area>` 的 `rect`、`circle`、`poly`/`polygon` 与 `default`，把
自然图像坐标缩放到渲染尺寸并对 malformed coords、`nohref`、未知形状和超出预算安全
忽略。夹具同时断言 area 优先于外层 `<a>` 的 `PCore_LinkAt(Ex)` href/target/rel、
`PCore_LinkInfoById(Ex)` 的区域几何、`PCore_InteractionSetAt` 的 active/hover 节点，
以及按坐标 Core 事件沿 `<area> → <map>` 正常冒泡；Browser 事件对象的 `isTrusted`
和兼容 `trusted` 字段保持一致。该门不扩展 `srcset`/`sizes` 选择、transforms、CORS、
完整图像视觉或 pointer/touch 手势；map 解析和命中语义属于 Core，宿主只接线与断言。

TEST1123 以离线夹具覆盖重复资源、三层 `@import`、摘要脱敏和 fallback observation；TEST1124 覆盖 candidate handle 的 generation admission、取消、退休幂等、过时 generation 隔离和 committed/failed 终态；TEST1125 覆盖 Browser 派生的 pending、committed、failed、cancelled 和 stale 结果分类；TEST1126 覆盖资源 gate 与 candidate result 的组合 decision、可提交标志、取消/过时/终态优先级和非法参数；TEST1127 覆盖 cleanup snapshot 的 pending/terminal decision、required failure、optional fallback、取消、stale、清理前复制和 handle 销毁后的快照存活性。`PBrowser_NavigationCleanupGetInfo` 只提供 Browser-owned 的有界值，宿主在 join worker、收敛资源后读取它，再释放 request。

### 手动模式

`auto=0` 时保留启动确认、测试说明和人工关闭流程。可视页面通常停留在设备上，验收者按说明操作后用 `Esc`、页面空白处或测试明确提供的关闭入口继续。

交互模式不保证生成完整自动日志；截图、设备信息和操作记录应保存在本地 `tmp/`。需要机器判门时，完成观察后再用相同范围运行一轮 `auto=1`，但自动日志不能替代人工结果。

### 浏览器 JavaScript

- `javascript=0`：默认产品路径，不执行页面 classic script。
- `javascript=1`：显式启用实验性的浏览器 script session、受限 DOM/Event/input/navigation bridge 和 inline/external classic script。

独立 `positron_script.dll` 测试不依赖此开关。开启浏览器 JavaScript 也不表示支持完整 DOM、Web API、ECMAScript host environment 或浏览器安全沙箱。

### 完成提示音

TEST999 是专用完成提示音。只有显式选中、且前序批次未令进程失败时，宿主退出前才请求一次系统提示音。听到声音、进程退出或看到部分 `OK` 都不能替代最终日志判定。

## 四种配置方式

| 目标 | 配置 |
|---|---|
| 部分测试、自动断言 | `auto=1`，`tests=` 写所需编号；通常保留 `javascript=0` |
| 部分测试、人工模式 | `auto=0`，`tests=` 写所需编号；脚本 fixture 按需要启用 JavaScript |
| 所有自动安全测试 | 使用 nightly 包自动生成的 INI，或由设备门显式生成当前源码 dispatch 清单 |
| 所有测试、人工模式 | 在全量自动清单中加入发布说明列出的 manual-only fixture，并设 `auto=0` |

移走或改名 INI 只会进入旧式分组选择，不等于“自动运行所有测试”。manual-only fixture 不得放入 `auto=1` 默认清单；它们会主动拒绝自动运行，以免伪造人工验收结果。

Nightly 包会从当前 `run_configured_tests` dispatch 动态生成全量自动清单，不在脚本里硬编码每天变化的测试目录。详情见 [NIGHTLY_RELEASE.md](NIGHTLY_RELEASE.md)。

## 本机验证

### C89 转换回归

修改产品 C、移植代码或 C89 转换脚本后先运行：

```bat
python scripts\test_c89ize.py
```

对受转换器管理的源码再次执行转换应为幂等，不得靠 VS2008 恰好接受错误输出掩盖脚本回归。

### 仓库审计

提交前运行：

```bat
python scripts\audit_repo.py
```

审计覆盖工程输入、版本 pin、许可证、Git 跟踪、UTF-8、Markdown 链接、文档结构和 `test_host` 产品边界（禁止编译产品 `.c` 或定义产品公共入口）。审计成功不代替目标构建、设备行为或对宿主 helper 语义归属的人工审查。

### ARMV4I 构建

使用正式入口：

```bat
scripts\build.bat
scripts\build.bat Debug rebuild
```

局部低风险修改通常先做增量构建；工程依赖、生成规则、静态库或无法解释的混包问题使用 clean rebuild。不要直接调用 ARM `cl.exe` 拼装一部分目标。

## 自动设备门

### 前提

先由用户在 WMDC 或 Device Emulator GUI 中手动建立唯一目标连接。USB 真机和 DMA emulator 都可以。设备门使用 RAPI 1 的“当前 WMDC 会话”，因此它：

- 不枚举或选择设备；
- 不绑定 emulator VMID；
- 不启动、cradle、断开、重置或杀死设备；
- 不能替代 GUI 连接操作。

### 运行

从仓库根目录执行：

```bat
scripts\device_gate.bat -Candidate feature-name
```

默认测试选择来自 tracked `test_host/test_host.ini`。定向批次使用 staging override，不修改 tracked INI：

```bat
scripts\device_gate.bat -Candidate feature-name ^
  -TestSelection "1095-1102,999"
```

需要实验性浏览器脚本时显式加：

```bat
scripts\device_gate.bat -Candidate feature-name ^
  -EnableJavaScript -TestSelection "1095-1102,999"
```

脚本执行正式构建、隔离 staging、整包部署、启动、有限等待、日志回收和自动判门。每次运行使用唯一设备目录，本地证据保存在 `tmp/device-runs/`，不会纳入 Git。

部署前的 RAPI 预检会分别记录两类空间：优先使用
`CeGetDiskFreeSpaceEx` 查询 `-RemoteBase` 所在目标卷，同时使用
`CeGetStoreInformation` 查询内部 object store。微软文档说明后者是历史 API，且只描述
object store；因此外部 `\Storage Card` 目标没有路径级 API 时会 fail closed，不会用
错误的对象存储数字冒险部署。目标卷的硬性要求是当前 staging 全部文件大小加 1 MiB
运行余量；目标卷不足、无法查询或路径范围不安全都会在复制第一个文件前停止。
内部 object store 另设 64 KiB 的系统缓存告警线：外部目标下它只产生
`LOW_ADVISORY`/`UNAVAILABLE_ADVISORY`，不把内部粗粒度数字误报成目标卷不足；已知内部
目标则直接用 object store 数字执行硬性容量门。结果文件记录兼容的 `storage_*` 别名，
以及更明确的 `target_storage_*`、`internal_storage_*`、`internal_cache_reserve_bytes`
和 `internal_storage_check` 字段。参考：
[`GetStoreInformation`（Microsoft Learn）](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms891023%28v%3Dmsdn.10%29)。
即使预检阻止部署，也会在本地证据目录写出 `device-gate-preflight.txt`，便于确认失败
发生在远端复制之前。

为避免设备空间被旧包逐次吃完，设备门只把名字符合自身时间戳格式的旧目录视为候选。
每个旧目录都必须先把 `test_host.log` 成功复制两次并得到稳定的 `TESTBENCH PASS` 或
`TESTBENCH FAIL` 终态，才允许删除；缺失、仍在增长或复制失败的日志会让目录保留并在
输出中说明原因。若目标卷或已知内部 object store 确认空间不足，设备门会再做一次有界的
应急回收：只处理门自己生成、且不是当前运行目录的旧目录，先尽力把日志复制到本地
`prior-logs`；只有日志完整且稳定的目录才允许删除来释放空间，日志不完整的目录始终
保留。未知目录不会删除，当前运行目录也不会在预检阶段删除。回收后会重新查询空间，仍不足
才阻止部署。自定义 `-RemoteBase`
同样遵守这条规则；`device-gate-preflight.txt` 和 `device-gate-result.txt` 会记录
`prior_cleanup_*`、`space_reclaim_removed/partial/preserved`、最终空间状态、`current_cleanup` 和
`complete_log_retrieved`。

RAPI 没有安全的通用远端终止语义。超时会保存可取得的日志并返回非零，但不会强杀设备进程；重试前应在设备 GUI 确认真正结束遗留 `test_host.exe`。WMDC/RAPI 错误按 [故障排查](TROUBLESHOOTING.md#wmdc-自动设备门不要混淆-corecon-与-rapi)处理。

### 自动通过标准

一次设备门只有同时满足下列条件才通过：

1. 构建和整包 staging 成功；
2. 启动日志来自本次唯一候选目录；
3. 每个所选测试都有预期完成记录；
4. `ERROR` 和 `FAIL` 计数为零；
5. 最终只有一个 `TESTBENCH PASS`；
6. 涉及真实 Browse 时，路由和最终页面序列符合该 fixture 的要求；
7. 没有旧 EXE/DLL 混包或遗留进程证据。

只有启动头、部分 `OK`、提示音、窗口关闭或 RAPI 成功都不是通过证据。

## 回归范围

每批测试范围应与风险相称，不需要每次都从头跑到尾。

定向门通常包括：

- 本批新增契约；
- 直接共享的数据结构或默认动作；
- 一个真实页面/导航哨兵（若变更触及页面组合）；
- TEST999。

出现以下情况时运行更宽回归或全量门：

- 多个低风险批次已经累计；
- 修改公共 ABI、所有权或生命周期；
- 修改 layout/paint、输入基础设施、网络/TLS 或资源缓存；
- 准备里程碑交付或 nightly 基线；
- 出现崩溃、超时、数据错误、混包或无法解释的行为。

全量清单从当前源码生成，不复制到本文。上一次全量与当前累计风险由 handoff 记录。

## 人工验收

以下风险必须由真实设备观察，或明确进入允许累计的人工清单：

- 字体 fallback、字形、抗锯齿、颜色和渐变；
- 左右边距、居中容器、换行、表格、列表和滚动条观感；
- 嵌套 overflow 的滚动条位置、内容裁剪、指针拖动、触摸滚动和不同 DPI 下的可视区域；
- 真实触摸、链接命中、键盘焦点、滚动和后退；
- SIP 候选词、IME composition、Unicode 与代理对输入；
- 旋转、screen/DPI 差异和滚动位置保持；
- 系统文件选择器、窗口返回和取消后的状态；
- OEM 剪贴板与其他应用的 copy/paste、CF_TEXT 或富文本格式互操作；自动门只覆盖宿主自备的有界 `CF_UNICODETEXT` contenteditable paste/cut/copy，折叠复制 no-op 和超长/非 Unicode 拒绝也只在该宿主契约内成立；
- loading、失败网络、旧页保留和深层真实导航。

contenteditable 的自动 fixture 证明有效 editing host 的 WM EDIT 代理、`beforeinput` 取消、Core 文本提交、`input` 顺序、脚本 `selectionStart`/`selectionEnd`/`selectionDirection` 的范围语义，以及只对实际变化分发一次的非冒泡、不可取消 `selectionchange`；设备门还验证这些范围可同步到原生 EDIT，并由原生消息触发同一 Browser 事件。TEST1113 用 WM EDIT 的 `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` 序列验证无修饰拖选的连续 forward/backward 方向和重复范围抑制；TEST1114 覆盖 Shift/方向键方向保持，以及 `WM_CAPTURECHANGED`、`WM_CANCELMODE`、`WM_KILLFOCUS`/`WM_SETFOCUS` 中断时的收尾和去重；TEST1115 覆盖宿主有界 `CF_UNICODETEXT` paste/cut 的精确 `beforeinput.data`、取消回滚、Core 单次提交、折叠选区同步，以及空或不支持格式时的 fail-closed；TEST1116 覆盖 `WM_COPY` 的非空选区复制、折叠选区 no-op、超长 UTF-8 与非 Unicode 格式拒绝，并确认 WinCE 原生 `WM_CUT` 的内部 `WM_COPY` 重入不会破坏剪切。由于 WinCE 直接 `SendMessage` 不会更新键盘状态表，TEST1114 在 key-up 前注入有界原生范围来验证宿主通知路径；真实 OEM 默认键盘、SIP 候选词、完整 IME composition、硬键盘和跨应用剪贴板格式仍应在设备人工矩阵中观察。

低风险视觉或输入变化可以累计若干批次后集中验收。崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即复核，不能等待累计窗口。

### 人工记录

每组观察至少记录：

- commit/候选名和精确 `tests=`；
- 设备型号、screen、DPI、方向和系统时间；
- 初始页面、操作步骤和最终 URL/状态；
- 预期行为与实际行为；
- 必要截图和同批自动日志。

截图和日志只放 `tmp/`。比较两张截图前先确认 viewport、DPI、方向、滚动位置和二进制身份一致，避免把网络波动、旧页、旧 DLL 或不同滚动位置误判为渲染回归。

## 网络测试

WM6 镜像的系统时间经常过旧。证书测试前先校准时间，并把失败分为：DNS、TCP、TLS handshake、证书/hostname、HTTP status、redirect、资源获取、页面解析和最终提交。

离线 fixture 用于稳定契约；真实网页用于集成哨兵。真实端点的暂时不可达不能通过放宽离线断言解决，离线契约通过也不能证明任意互联网网站兼容。资源失败应依据日志分类处理；宿主只对 transport 失败提供每项最多 2 次的有界重试，取消、预算拒绝或 HTTP/resolve/memory 错误不会重试。样式表/`@import` 属于 required，失败或未收敛时不提交候选；图片/script 属于 optional，失败时可在 Core fallback 下继续。Browser 资源事务提供最多 4 项、不含原始 URL 的 `role/failure#hash` 摘要，以及 `resource fallback image/script/other/observed` 粗粒度计数；这些结果由宿主写入日志，不提供逐资源 UI 或视觉保证。TEST1123 在离线夹具中覆盖重复资源、三层 `@import`、摘要脱敏和 fallback observation。不能把资源事务的 gate 或摘要当成完整浏览器降级体验。

## 候选成为基线

只有满足以下条件，候选才能写入当前 handoff：

1. 修改范围、ABI 和所有权清楚；
2. C89 回归与仓库审计通过；
3. ARMV4I 正式构建通过；
4. staging 文件来自同一批构建；
5. 风险相称的设备日志完整通过；
6. 必要人工验收完成，或风险明确进入允许累计清单；
7. handoff、限制、路线图和稳定文档分别只更新自身职责。

逐批设备日志不追加到本文。需要追溯时使用当前 handoff、本地证据、专用历史文档和 Git 历史。

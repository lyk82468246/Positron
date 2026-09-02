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
的直接 `disabled` 属性，`:required`/`:optional` 读取 input、select、textarea 的直接
`required` 属性。夹具通过 `matches()`、`closest()`、`querySelector()` 和
`querySelectorAll()` 断言初始状态、checked/属性 mutation 后的实时结果、列表顺序和
组合查询，并确认带参数、交互伪类、`:not()` 的不支持参数、伪元素和非表单元素安全
fail closed。
该子集不推导 fieldset/optgroup 继承；option 的动态 selected→`:checked` 映射由
TEST1157 单独覆盖，也不代表完整 CSS Selectors 语法。

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
必须立即更新。带参数、伪元素、`:visited` 和尾随逗号等不支持输入必须 fail closed。
Browser 没有 visited-state 存储；真实链接绘制、hover/active、导航和历史样式仍属于
宿主集成与人工观察。

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

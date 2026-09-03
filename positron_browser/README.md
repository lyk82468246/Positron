# `positron_browser.dll`

`positron_browser.dll` 是无窗口的浏览器会话组合层，负责 history、script session、bootstrap、DOM/Event adapter、native 控件事务和导航候选摘要。它不创建窗口、不抓取网络、不持有 Core document，也不直接操作 WM 控件。

## 产物与依赖

- 工程：`positron_browser.vcproj`
- 公共头：`positron_browser.h`
- 输出：`bin\<Configuration>\positron_browser.dll` 与 import library
- 运行时依赖：`positron_script.dll`、`positron_json.dll`
- 典型组合：宿主另行使用 `positron_core.dll`、HTTP/TLS、窗口和 native 控件

其他项目链接 `positron_browser.lib` 并部署三个 DLL。不要复制 Browser 内部结构，也不要把 `test_host.exe` 当作运行时依赖。

## 能力分组

### History

`PBrowser_History*` 管理有界的进程内条目、当前位置、state、same-document 操作和 traversal。URL/state 查询返回借用字符串，在同一 history handle 的下一次 mutation 或 destroy 后失效。

每个条目还可以保存一个有界的 `(scroll_x, scroll_y)` viewport snapshot。坐标是调用方约定的非负整数；Browser 只随条目保存和搬移它们，不知道文档高度、不创建窗口，也不负责 clamp。宿主在真正提交页面或完成 traversal 后读取快照，再按自己的 client area 和 document extent 应用它：

```c
int scroll_x;
int scroll_y;

PBrowser_HistorySetEntryScroll(history, entry_index, 0, current_scroll_y);
if (PBrowser_HistoryEntryScroll(history, entry_index,
        &scroll_x, &scroll_y) == PBROWSER_OK) {
    host_scroll_to(scroll_x, scroll_y); /* host clamps to its document */
}
```

新文档 entry 和同 URL 的新 document 从 `(0, 0)` 开始；`replaceState` 与 history traversal 保留已有 snapshot，`pushState` 的新 entry 从零开始，history 达到上限裁剪时 snapshot 与 URL/state 一起移动。History 只决定条目语义，不请求 URL、不保存文档、不创建窗口，也不持久化到磁盘。宿主只有在页面真正提交后才应 commit 新导航；失败候选不得污染 history。

浏览器脚本的 `history.scrollRestoration` 初始为 `auto`，也可以设为
`manual`。Browser 通过 `PBrowser_ScriptSessionGetScrollRestoration()` 把这项
策略提供给宿主；宿主只有在结果为 `PBROWSER_SCROLL_RESTORATION_AUTO` 时才应在
history traversal 后自动应用 entry snapshot，`MANUAL` 则保留当前 viewport。这个
查询不改变 history 或 viewport；fragment reveal 和应用显式请求的滚动仍由宿主
单独处理。只有明确读到 `MANUAL` 才能跳过恢复，查询失败不能把页面误判为手动模式。

### Page viewport 与脚本滚动

注册 `PBrowserScriptScrollCallbacks` 后，Browser bootstrap 的
`window.scrollTo()`、`scroll()` 和 `scrollBy()` 会把规范化后的非负 CSS
page 坐标交给宿主。宿主负责按当前 Core document extent 和 client area
clamp，并通过 `out_x/out_y` 返回实际应用的坐标；候选 session 尚未提交时应
只回显请求，不能改变旧页面。Browser 只在最终坐标改变时分发一次非冒泡的
`scroll` 事件。该 callback 结构末尾的 `element_id` 仅在 `Element.scrollTo()`/
`scrollBy()` 请求中非空；为 `NULL` 时保持原有 page viewport 合同。

宿主处理滚动条、触摸、键盘、resize 或 fragment reveal 后，应先把物理
viewport 位置换算为 CSS page 坐标，再调用
`PBrowser_ScriptSessionNotifyScroll(session, x, y)` 同步脚本侧 viewport。这个
入口只更新脚本侧 `scrollX`/`scrollY` 并做去重，不会再次调用 scroll adapter，
因此宿主可以在完成自己的 clamp/apply 后安全调用它。回调同步且不可重入；
宿主不得在 scroll callback 内再次进入或销毁同一 Browser session。

宿主完成物理窗口的 style/layout 和 viewport clamp 后，还应把 CSS viewport
宽高与当前 `devicePixelRatio` 传给
`PBrowser_ScriptSessionNotifyResize(session, width, height, dpr)`。Browser
会更新 `innerWidth`、`innerHeight`、`outerWidth`、`outerHeight`、
`devicePixelRatio` 以及 `screen` 的宽高/方向，并同步派发一次不冒泡、不可取消、
`isTrusted` 为真的 window `resize` 事件。`screen.orientation` 是跨读取保持身份
稳定的对象；其 `type`/`angle` 随布局视口的横竖方向更新，支持 `onchange` 和
有限的 `addEventListener('change', ...)` 监听器。方向真正翻转时，Browser 先完成
同一次通知中的媒体列表刷新，再派发 orientation `change`，最后派发 visual viewport
与 window 的 `resize`；仅尺寸或 DPR 改变而方向不变时不会伪造 orientation 事件。宽高允许为零，
DPR 必须为正；参数必须是有限数值。完全相同的三元组仍返回成功，但不会重复派发
事件。该入口只更新脚本侧快照，不触发 Core style/layout，也不自动运行 timer 或
`requestAnimationFrame` 队列；宿主若页面在 resize handler 中排队下一帧，必须用
已有的 frame pump 另行驱动。会话会追踪最多 64 个 `matchMedia()` 列表；有效
viewport 或 DPR 变化时，只有 `matches` 实际翻转的列表才同步派发一次 `change`
事件，且发生在同一次 `resize` 事件之前。事件提供 `media`、`matches`、
`target`、`currentTarget`、`isTrusted` 和固定的非冒泡/不可取消字段；重复快照和
未发生匹配变化都保持静默。超过追踪上限的列表仍返回初始快照，但不会收到后续
`change` 通知。该能力不扩展到 nested overflow 或完整媒体查询语法。

同一个 bootstrap 还暴露有限的 `window.visualViewport`。它是当前布局视口的
稳定 `EventTarget` 快照：`width`/`height` 读取当前 viewport，`pageLeft`/
`pageTop` 读取当前 page scroll，`scale` 固定为 `1`，`offsetLeft`/`offsetTop`
固定为 `0`。宿主的有效 resize 会先同步派发 visual viewport `resize`，再派发
window `resize`；有效 scroll 会先派发 visual viewport `scroll`，再派发 window
`scroll`。重复快照保持静默，`onresize`/`onscroll` 与普通 listener 都可使用。
该对象不模拟 pinch zoom、视觉 viewport 偏移或 nested overflow；宿主仍负责
真实窗口、Core layout 和滚动应用。

### Layout geometry 与 `getBoundingClientRect()` / `getClientRects()`

DOM relation callback 提供当前 Core layout 的整数 CSS 像素几何。`LAYOUT_RECT_*`
关系返回一个元素的 border-box union；`LAYOUT_FRAGMENT_COUNT` 与索引化的
`LAYOUT_FRAGMENT_*_AT` 关系返回同一元素的有界视觉片段快照。块级元素通常只有一个
片段，参与 inline flow 的元素按实际行片段返回，最多
`PBROWSER_SCRIPT_LAYOUT_FRAGMENT_MAX`（16）个。未完成 layout、没有可用 box 或
relation callback 未注册时，查询失败；Browser 不触发 style/layout，也不暴露 Core
的 box 指针。

`Element.getClientRects()` 每次调用都新建 array-like 集合，并把每个正尺寸片段转换
为 viewport-relative CSS 像素：`length` 为 0 至 16，片段按文档绘制顺序出现在整数
索引，`.item(index)` 对合法索引返回同一对象，对越界或非负整数之外的参数返回 `null`。集合
和其中的矩形不与上一次调用共享身份。`Element.getBoundingClientRect()` 对同一份
片段集合计算 union，返回有限的 `{left, top, right, bottom, x, y, width, height}`
快照；空集合返回全零矩形。两种方法都会扣除当前 page-level `scrollX`/`scrollY`，
并随 Browser 的滚动同步更新。宿主仍负责在 DOM mutation、resize 或页面提交后重新
style/layout，并按实际滚动位置调用 `PBrowser_ScriptSessionNotifyScroll`。

该边界只覆盖 Core 已布局的普通 block 与 inline 行片段，不承诺 transforms、Range/
Selection、完整 nested overflow 坐标传播、pinch zoom、平滑滚动或视觉像素精度。

#### 布局尺寸快照

当宿主注册 DOM relation callback 后，Browser 为已布局的支持元素安装只读的
`offsetWidth`、`offsetHeight`、`clientWidth`、`clientHeight`、`scrollWidth` 和
`scrollHeight` getter。getter 每次从最近一次 Core layout 快照读取整数 CSS 像素：
offset 包含 border，client 为 retained scrollport 的 padding 区域（滚动条覆盖在边缘，
不从 client 尺寸再扣除固定宽度），scroll 包含有界后代内容 extent。Core 不提供快照时
这些属性返回 `0`；它们不会触发 style/layout，也不会改变滚动位置。宿主仍须在 DOM/样式
变化后自行重新 layout，并继续通过 page-level scroll callback 管理页面滚动。对于有有效
DOM `id` 且存在 retained overflow scrollbar 的支持 box，Browser 还安装
`scrollLeft`/`scrollTop` getter/setter、`scrollTo()`、`scroll()` 和 `scrollBy()`；请求
通过 callback 的 `element_id` 交给 Core clamp，成功后只在实际位置改变时派发一次目标元素的
非冒泡、不可取消 `scroll`。宿主的 WM 指针路径应调用
`PBrowser_ScriptSessionNotifyElementScroll()` 同步 Core 位置。该桥本身不实现 scroll
chaining、scroll-margin、smooth/inertia 或无 id 目标；`scrollIntoView()` 的有限 nested
reveal 见下文。

### 元素 overflow 滚动桥

元素滚动是一个有界的 Core/Browser 组合，而不是宿主自己的第二套布局模型：

- Core 对已布局的常见 block、replaced、flex box 保留 `overflow: scroll/auto` 的滚动条，
  通过 relation 38/39 暴露当前 CSS 像素偏移，并由
  `PCore_NodeOverflowScrollToById()` 执行两个轴的非负 clamp；
- Browser 的 `Element.scrollLeft`/`scrollTop` 以及 `scrollTo()`/`scrollBy()` 只接受
  `auto`/`instant`，把目标 id 和请求交给同一 scroll callback，再用 Core 返回的实际值
  更新脚本状态；重复位置不产生事件；
- 宿主收到 WM pointer 后调用 `PCore_OverflowPointer()`，按
  `PCore_OverflowDirtyRect()` 做局部失效，并把 `PCore_OverflowScrollSnapshot()` 的
  id/位置交给 `PBrowser_ScriptSessionNotifyElementScroll()`。后者只更新脚本状态和派发
  去重事件，不会再次调用 scroll callback，因此不会递归；
- 该能力要求目标有稳定 DOM `id` 和可用 layout。关系 40/41 报告两个 retained
  scrollbar 轴，关系 42/43 报告 padding/client edge 的文档坐标；Browser 用它们为
  `scrollIntoView()` 选择最近的可寻址祖先，或在 `container:"all"` 下依次处理最多
  64 层父链。它不实现完整滚动容器树、滚动链、scroll anchoring、scroll-margin、
  平滑/惯性动画或匿名目标的宿主归因。

### `Element.scrollIntoView()`

Browser 还提供页面级的 `Element.scrollIntoView()`。它读取同一
`getBoundingClientRect()` 矩形；如果父链中存在可寻址的 retained overflow box，
最多遍历 64 层。默认选择最近者，用 Core 的轴可用标志与 client edge 坐标在对应轴上
直接对齐；对象形式的 `container: "all"` 则从最近者向外依次处理每个可寻址的
retained overflow ancestor，并在每次滚动后重新读取目标矩形。没有适用祖先时把请求
交给已有的 `window.scrollTo()` page-level callback；`container: "all"` 处理完祖先后，
仅当目标仍在页面视口外才使用该 fallback。宿主仍负责 extent/client area 的 clamp、
物理滚动、绘制和实际位置回报。默认等价于 `{block: "start", inline: "nearest"}`，
布尔值 `false` 选择 block end；对象形式只接受 `block`/`inline` 的 `start`、`center`、
`end`、`nearest`，`behavior` 的 `auto` 或 `instant`，以及 `container` 的 `nearest`
或 `all`。无可用 layout、矩形或 nested client bridge 时安全回退/ no-op；没有 scroll
callback 时不会影响宿主真实 viewport，脚本侧仍遵循既有 `scrollTo()` 的本地状态规则。
不支持 `smooth`、scroll-margin、完整滚动容器树、scroll chaining、scroll anchoring 或
匿名祖先/目标。调用完成后，实际位置变化仍由既有 scroll callback
和 `PBrowser_ScriptSessionNotifyScroll`/`PBrowser_ScriptSessionNotifyElementScroll`
负责同步与事件去重。

### Script session

`PBrowser_ScriptSessionCreate` 创建有预算的浏览器脚本 context；Browser bootstrap 使用
独立的 714 KiB heap ceiling。`Destroy` 释放 bootstrap、队列、native function 和事务
状态。浏览器脚本使用 `positron_script.dll` 中同一 Duktape 引擎，但它的 Web host
objects 由 Browser callbacks 提供。

典型生命周期：

```c
HANDLE history;
HANDLE session;

history = PBrowser_HistoryCreate();
session = PBrowser_ScriptSessionCreate(2500);
if (history == NULL || session == NULL) {
    /* handle allocation failure */
}

/* Register size-tagged callback tables, then bootstrap/evaluate scripts. */

PBrowser_ScriptSessionDestroy(session);
PBrowser_HistoryDestroy(history);
```

精确预算、返回码和 callback 结构以 `positron_browser.h` 为准。

### `document.activeElement` 与 Core 焦点桥

`document.activeElement` 是一个按需安装的 Browser 投影，不是 Browser 自己猜测
native 焦点。宿主在 bootstrap 完成前或完成后注册
`PBrowserScriptActiveElementCallbacks`，然后在页面脚本运行前确保 Core 的 DOM
读回调也已注册。回调只需同步返回当前焦点节点的非空 UTF-8 DOM id；返回空值、
过长 id、已失效 id 或 Core 没有可用的 id-addressable 焦点时，getter 都安全地
返回 `document.body`。注册后再注销只移除 native id 来源，已安装的 getter 仍会
回退到 `body`；从未注册该表的 session 不安装这个可选属性，以免增加 WM6 的
bootstrap 成本。

参考宿主把 Core 的 `PCore_InteractionFocusElementId` 适配到这个 callback：

```c
PBrowserScriptActiveElementCallbacks active;

memset(&active, 0, sizeof(active));
active.size = sizeof(active);
active.pw = bridge;
active.get_active_element = host_get_active_element_id;
PBrowser_ScriptSessionRegisterActiveElementCallbacks(session, &active);
```

`PCore_InteractionFocusElementId` 使用与其他 UTF-8 查询相同的 size-probe 和
固定容量规则；它只报告当前 Core 交互状态中的 id，不改变焦点、style 或 layout。
因此这项组合不提供完整浏览器焦点算法、自动初始焦点、焦点矩形、native HWND
切换或跨窗口策略。回调指针只在同步调用期间借用，宿主仍拥有 native 焦点状态。

#### `:active` / `:hover` interaction bridge

指针状态也可以按需投影到 Browser selector。宿主注册
`PBrowserScriptInteractionCallbacks`，在 callback 中按 `state`（`"active"` 或
`"hover"`）返回当前 Core 节点的非空 UTF-8 id；参考接线使用
`PCore_InteractionStateElementId`，因此不会维护第二份交互模型：

```c
PBrowserScriptInteractionCallbacks interaction;

memset(&interaction, 0, sizeof(interaction));
interaction.size = sizeof(interaction);
interaction.pw = bridge;
interaction.get_interaction_element = host_get_interaction_element_id;
PBrowser_ScriptSessionRegisterInteractionElementCallbacks(session,
        &interaction);
```

注册后，各 selector 查询都会读取 callback；无 callback、空/过长/失效 id、带参数或伪元素
安全不匹配。Browser 不派发 pointer 事件、不改变 Core/style/layout/paint；宿主负责
hit-test、时机、事件、失效和视觉。桥只匹配精确当前元素，不实现祖先 `:hover`、完整
pointer capture 或其他 pointer/touch 状态。

需要让页面读取宿主访问历史时，使用同一 native function slot 的
`PBrowserScriptInteractionCallbacksEx`：

```c
PBrowserScriptInteractionCallbacksEx interaction;

memset(&interaction, 0, sizeof(interaction));
interaction.size = sizeof(interaction);
interaction.pw = bridge;
interaction.get_interaction_element = host_get_interaction_element_id;
interaction.get_link_visited = host_get_link_visited;
PBrowser_ScriptSessionRegisterInteractionElementCallbacksEx(session,
        &interaction);
```

`get_link_visited` 只为带 `href` 的 `<a>`/`<area>` 同步接收借用的 UTF-8 `element_id` 与
原始 `href`；正数表示宿主批准已访问，零表示未访问，负数表示无法判断。宿主负责历史
存储、隐私策略和 URL 解析，Browser 只在 `:visited` 查询时读取结果，不修改 history、
不导航、不重做 style/layout/paint。未注册 Ex callback、无 id、超长/非法输入或 callback
失败都安全不匹配。
旧 `PBrowserScriptInteractionCallbacks` ABI 保持兼容；Ex 与旧表不可在同一 session 重复
注册，注销使用 `PBrowser_ScriptSessionUnregisterInteractionElementCallbacks`。

#### `HTMLElement.focus()` / `blur()` 请求

脚本主动聚焦是另一条按需安装的桥。宿主注册
`PBrowserScriptFocusRequestCallbacks` 后，Browser 在 bootstrap 后为每个
`PElement` 安装 `focus()` 与 `blur()`；旧 callback 只接收当前元素的 id 和
`focused` 值，返回值保持 `undefined`。宿主应以
`PCore_FocusTargetInfoById` 检查已布局目标，以
`PCore_InteractionFocusById` 更新 Core，再切换相应 native HWND 并派发一次
`blur`/`focusout` 或 `focus`/`focusin`。disabled、hidden、stale、无 id、未布局、
对非当前目标的 `blur()` 都必须安全 no-op；重复 `focus()` 不再派发额外焦点事件，
但 Ex callback 仍可按默认规则把已聚焦且不可见的目标 reveal。注销 callback 会
移除 native 请求来源，已安装的方法仍保留为 no-op；未注册 callback 的 session
不安装这组可选方法，以控制 WM6 bootstrap 成本。

需要页面级滚动可见性时，宿主注册新增的
`PBrowserScriptFocusRequestCallbacksEx`。Browser 把
`focus({preventScroll:true})` 表示为 `prevent_scroll=1`；如果 Browser 能从
当前目标沿 `parentElement` 找到 retained overflow ancestor，也会把这一位设为
1，要求宿主先不要做 page-level reveal。宿主在完成自己的 viewport clamp/apply 后，
把实际 CSS page 坐标写入 `PBrowserScriptFocusRequestResult`。Browser 等 callback
返回后才更新脚本侧 `scrollX`/`scrollY`，因此滚动事件不会从 native callback 内
递归进入 runtime。普通 `focus()` 随后由 Browser 调用有界的
`scrollIntoView({block:"nearest",inline:"nearest",container:"all"})`：先处理
最近到最外的可寻址 retained overflow ancestor，仍在页面视口外时才使用 page-level
scroll callback。`preventScroll:true` 完全保持现有页面和元素滚动位置；`blur()` 不滚动。

只需要焦点事务时的最小注册形态如下，`request_focus` 的 `element_id` 只在同步
callback 期间借用：

```c
PBrowserScriptFocusRequestCallbacks focus;

memset(&focus, 0, sizeof(focus));
focus.size = sizeof(focus);
focus.pw = bridge;
focus.request_focus = host_request_focus;
PBrowser_ScriptSessionRegisterFocusRequestCallbacks(session, &focus);
```

页面级滚动扩展的注册形态如下；`out_result` 由 Browser 预先清零并在 callback
返回后读取，宿主不应保存它或其中的借用指针：

```c
PBrowserScriptFocusRequestCallbacksEx focus_ex;

memset(&focus_ex, 0, sizeof(focus_ex));
focus_ex.size = sizeof(focus_ex);
focus_ex.pw = bridge;
focus_ex.request_focus = host_request_focus_ex;
PBrowser_ScriptSessionRegisterFocusRequestCallbacksEx(session, &focus_ex);
```

#### 宿主驱动的初始 `autofocus`

Browser 不自主执行 `autofocus`。宿主在 Core layout/native
子控件完成后调用 `PCore_AutofocusTargetInfo` 与
`PCore_InteractionFocusAutofocus`；有 id 目标复用 focus bridge，无 id 目标用
`PCore_EventDispatchFocus` 派发 focus/focusin。无合格目标、过期目标或超长 id 安全回退；
无 id 的 `document.activeElement` 返回 `body`，事件监听器应按可空 `target` 处理。

该扩展覆盖 id-addressable Core 目标、page-level viewport 和最多 64 层可寻址的
retained overflow ancestor 链，但不提供完整 focus navigation、自动初始焦点、
focus ring、完整 scroll tree、scroll chaining、scroll-margin、平滑/惯性滚动、
跨窗口策略或 OEM 控件视觉保证。

### DOM、表单与 validation adapters

Browser 不认识 libdom 节点。宿主注册 size-tagged UTF-8 callbacks，把当前 Core document
的受限查询和 mutation 映射为文本/属性/value/checked、节点关系、form/selected state、
validation/custom validity、contenteditable 纯文本选区、event listener 和 navigation。
Browser 负责 JSON 参数、脚本对象形状、错误映射与同步 dispatch；Core/宿主负责真实状态，
callback 参数和输出缓冲只在调用期间借用。

`Element.form` 和 `HTMLFormElement.elements` 复用 Core form-owner relation。支持的
`input`、`select`、`textarea`、`button` 控件默认归最近祖先 form；控件存在 `form="id"`
时解析文档中对应的 form，因此可以把 form 外的控件纳入 `form.elements`，而空值或无效
目标没有 owner，也不回退到祖先。`elements` 每次读取都是按文档顺序建立的有界 snapshot，
  文档 mutation 后应重新读取；Browser 不扩展为完整 live form collection。

启用 `PBrowserScriptFormResetCallbacks` 和按 id 的
`PBrowserScriptFormEventCallbacksEx` 后，脚本 `HTMLFormElement.reset()` 先派发可冒泡、
可取消的 `reset`，只有事件未被阻止时才调用宿主 `reset_form` callback；宿主应在其中
调用 `PCore_FormResetById`，成功后重新 style/layout/paint，方法返回 `undefined`。
启用 `PBrowserScriptFormSubmitCallbacks` 后还会安装
`HTMLFormElement.requestSubmit([submitter])`：Browser 先让宿主按 form/submitter id
调用 Core validation，验证通过后派发可冒泡、可取消的 `submit`，再调用宿主默认动作
callback。Core 的 by-id submission primitives 负责 enabled submitter、`novalidate`/
`formnovalidate`、urlencoded/multipart/dialog 结果；宿主负责网络导航或 dialog close。
启用 `PBrowserScriptFormSubmitDirectCallbacks` 后还会安装
`HTMLFormElement.submit()`：Browser 只解析当前有 id 的 form，跳过 constraint
validation、`submit` 事件和 submitter 选择，调用 direct callback 后返回
`undefined`。宿主应把该 callback 接到
`PCore_FormSubmissionNoValidationById`、
`PCore_FormDialogSubmissionNoValidationById` 或
`PCore_MultipartSubmissionNoValidationById`，再按应用策略执行网络导航或
dialog close；旧的 form-submit ABI 保持兼容。Direct 缺少 callback、非法目标或 adapter
失败会安全 no-op；Core state-only 入口不派发事件、导航或操作 native 控件，原有按坐标的
`PBrowser_ScriptSessionDispatchFormEvent` ABI 不变。

启用 `PBrowserScriptFormDataCallbacks` 后，为有 id 的 `<form>` 安装
`new FormData(form)`；第二参数使用 `PBrowserScriptFormDataCallbacksEx` 与对应注册函数。
构造同步读取 count/entry 得到 detached 对象；不做 validation、submit/reset、默认动作、导航
或重新 layout。成功后在 form 上同步派发非冒泡、不可取消的 `formdata`；
`event.formData` 就是返回对象，监听器可在返回前用 `append()`/`set()`/`delete()`
修改它，`form.onformdata` 也可用。Ex callback 收到 form id 和 submitter id，宿主转给
`PCore_FormDataByIdEx`；旧表只支持无显式 submitter。两种表都用
`PCore_FormDataInfo`/`PCore_FormDataEntryInfo` 填充借用的 UTF-8 缓冲并配对
`PCore_FreeFormData`，不得暴露 picker 路径。submitter 必须是 Browser 的 `PElement`、
启用且归属于该 form 的 submit-type input/button；`null`/`undefined` 等同省略 submitter，
其他无效 owner 抛出 `TypeError`。
最多 64 项，名称 64 字节、字符串值 128 字节、文件名和 MIME 类型各 64 字节；文件只保留
filename/type 和空内容，不承诺完整文件读取、live collection 或其他 form-associated 元素。

selector bridge 提供有界 compound/列表/组合器/属性/结构/表单状态，以及
focus/link/visited/fragment/language、`:not()`/`:is()`/`:where()`/`:has()`、
`:read-only`/`:read-write`/`:placeholder-shown`。`:visited` 通过 interaction Ex callback
读取宿主批准结果，Browser 不保存 history；参数、分支和遍历有固定预算，非法或未注册
callback fail closed。完整限制见 [`../.agents/KNOWN_LIMITATIONS.md`](../.agents/KNOWN_LIMITATIONS.md)。

### `dialog` 生命周期

启用浏览器 JavaScript 后，`<dialog>` 元素提供一个有界的生命周期接口：

- `show()`/`showModal()` 要求元素已连接且当前未打开；同一 session 只允许一个 modal。
- `close(value)` 移除 `open`、更新 `returnValue` 并派发非冒泡 `close`；`requestClose(value)`
  先派发可取消的非冒泡 `cancel`，未被阻止才 close。`open` 反映 DOM 属性，属性变化
  由宿主 DOM callback 进入正常 restyle/layout，`oncancel`/`onclose` 与监听器均可用。
- 宿主把 Escape 交给 `PBrowser_ScriptSessionRequestDialogClose`；有活动 modal 时即使
  `cancel` 阻止关闭也消费手势。Tab 时读取 `PBrowser_ScriptSessionGetActiveDialogId`
  并交给 `PCore_FocusTargetInfoWithin` 限定 dialog 子树。指针路径由宿主用 Core 几何
  命中，dialog 外部请求关闭并消费 backdrop，Browser 不读取窗口坐标。
- `method="dialog"` 表单由宿主让 Core 验证/解析后派发 `submit`；事件允许时调用
  `PBrowser_ScriptSessionCloseDialogById` 执行 `dialog.close(value)`、更新 `returnValue`
  并派发 `close`，不发网络导航。显式点击、脚本 `click()` 与隐式 Enter 可复用该路径。

这些方法只维护 Browser 脚本生命周期，不创建 HWND 或绘制 top layer/backdrop。宿主可把
活动 id 交给 `PCore_PaintDocumentWithModal` 组合有限 backdrop 和 dialog 重绘，并负责
初始焦点、native HWND、焦点视觉及 nested overflow 指针；跨文档 modal 生命周期未覆盖。

### 单元素 `contenteditable`

注册 `PBrowserScriptContentEditableCallbacks` 后，Browser 为每个元素暴露只读的 `isContentEditable`。该查询由宿主转给 Core，因而也能正确处理没有 id 的祖先和 `true`/空值、`false`、`plaintext-only`、未知值继承。`contentEditable` 仍是原始 attribute reflection，不应拿它代替有效状态。

`innerText` getter 读取 Core 的文本快照；对有效可编辑元素的 setter 走 `__pcoreSetContentEditableText`，由宿主调用 `PCore_ContentEditableSetTextById` 执行有界合法 UTF-8 纯文本替换。setter 是程序化 mutation，不自动产生 `beforeinput`/`input`。宿主若把 Core 的 editing-host 快照映射为 WM EDIT，真实键盘、SIP/IME 或其他输入源必须沿用已有 typed input 事务：先派发可取消 `beforeinput`，仅在允许后提交原生文本并调用 Core mutation，再派发 `input`；Browser 只决定事件、取消和顺序，不创建 HWND。

剪贴板不是 Browser 直接访问的系统 API。宿主可以在 WM EDIT 的 `WM_PASTE`/`WM_CUT`/`WM_COPY` 路径读取 `CF_UNICODETEXT` 或选中文本，将 CRLF 规范化为逻辑 UTF-8 后传给 `PBrowser_ScriptSessionDispatchNativeEditBeforeInput`；允许后执行 native default，再用 `PBrowser_ScriptSessionDispatchNativeEditInput` 和 selection notification 完成事务。`WM_COPY` 的折叠选区由宿主保持为 no-op，不产生空格式。当前边界只承诺单元素纯文本、`CF_UNICODETEXT` 和小于 `PBROWSER_SCRIPT_NATIVE_EDIT_MAX_TEXT_BYTES` 的 UTF-8 data；格式缺失、读取失败或超长时宿主应在 native mutation 前 fail closed。Browser 不提供 `ClipboardEvent`、async clipboard 或格式转换，也不拥有 WinCE 原生 `WM_CUT` 内部重入策略。

`selectionStart`、`selectionEnd` 和 `selectionDirection` 使用 JavaScript UTF-16 code-unit 偏移。调用 `setSelectionRange()` 或 `select()` 时，Browser 先更新有界脚本状态，再尝试通过可选的 `PBrowserScriptContentEditableSelectionCallbacks` 同步宿主的原生 editing host；没有原生窗口（例如离线 fixture 或未布局的后代元素）时保留脚本侧回退。宿主的 multiline EDIT 适配器负责把 CRLF 原生位置转换为 Core/Browser 的逻辑 LF 位置。

当范围或方向实际改变时，Browser 只分发一次非冒泡、不可取消的 `selectionchange`；重复赋值保持静默。脚本修改产生不可信事件，宿主在原生 caret/range 更新后调用 `PBrowser_ScriptSessionNotifyContentEditableSelection` 时可标记为可信。该入口负责校验、去重、更新脚本状态和事件分发；宿主不应再通过 Core 重新派发同一事件。参考宿主已将无修饰 WM EDIT 鼠标拖选、Shift/方向键扩展、受限 paste/cut 事务，以及 capture/cancel/focus 中断后的范围通知接入此入口；Browser 本身不拥有 HWND、键盘状态、鼠标 capture、平台剪贴板或平台 hit-test。当前仍只覆盖单元素纯文本，不包含 Range/Selection 对象、OEM 特有键盘自动重复与复杂行导航、富文本、designMode、ClipboardEvent/async clipboard、CF_TEXT 转换或完整 IME 语义。

### Event 与平台事务

Typed callback families 覆盖 input、keyboard、focus、EDIT、SELECT、click、form、invalid 和 navigation。对于 native 控件，推荐使用相应的 `Ex` 注册和 transaction dispatch：

- native EDIT（包括宿主为 contenteditable 创建的代理）：beforeinput、composition/result、commit→input、dirty、blur→change，以及 contenteditable 选区同步和有界 `selectionchange`；宿主可将有界 `CF_UNICODETEXT` paste/cut/copy data 作为 beforeinput payload，复制的折叠选区由宿主在 Browser 事务之外判定为 no-op；
- native SELECT：focus、key、dropdown candidate/confirm/cancel、commit→input/change；
- checkbox/radio：click、Core mutation 后的 input/change；
- button：click、validation、submit/reset/default action；
- file input：picker request/open/close/cancel 与 selection input/change；
- anchor/disclosure/programmatic click：可取消 click 与有界默认动作。

通用顺序由 Browser 决定。宿主仍拥有 WM 消息、控件窗口、Core mutation、picker、SIP/IME、HDC、网络和页面生命周期。

脚本三种 form 方法的顺序见上面的 DOM/form 小节；宿主把各默认动作接到对应 Core
primitive 并执行网络或 dialog 策略，不复制表单 owner、验证或事件规则。

每个事务使用稳定非零 token，并受固定容量限制。控件销毁、文档替换或 session reset 前，宿主必须调用相应 reset/unregister 入口。stale token、非法 phase、几何变化或 adapter error 会 fail closed，不允许部分默认动作。键盘焦点顺序由 Core 的 `PCore_FocusTargetInfo`（或 modal 场景的 `PCore_FocusTargetInfoWithin`）提供；Browser 负责报告活动 modal id，并把宿主的 WM key transaction 按取消和默认动作规则分发给当前目标。

### Navigation 与 target

Browser 可把 anchor/programmatic navigation 分类为 assign、replace、fragment、reload、history traversal 或 open，并把 target 分类为默认、`_self`、`_parent`、`_top`、`_blank` 或 named。

它不解析完整 URL、不连接网络、不创建 HWND，也不决定下载和外部协议。单窗口宿主可以接受当前-context target，并对 `_blank` 或不匹配的 named target 保守返回失败。target、rel、URL 和 context name 都是同步借用快照。

### Navigation resource transaction

`PBrowser_NavigationResource*` 把一次候选页面的资源状态放在 Browser，而不是测试宿主或应用自己的重复结构中。事务 handle 由 `PBrowser_NavigationResourceCreate` 创建、由 `PBrowser_NavigationResourceDestroy` 销毁；它按 UTF-8 URL 去重，合并 stylesheet/script/image role 和 required/optional policy，并拥有成功字节、终态、失败分类、attempt/retry 计数、资源预算、commit gate、hash-only failure summary 与 fallback observation。

Browser 不执行 DNS、TCP、TLS、HTTP 或 worker。宿主（或其他应用）负责发现资源、调度网络和决定取消/重试时机，再按顺序提交结果：

```c
HANDLE tx;
int index;
PBrowserNavigationResourceInfo info;
const char css_url[] = "https://example.invalid/site.css";
const char *bytes;
int byte_count;

tx = PBrowser_NavigationResourceCreate();
if (tx != NULL &&
    PBrowser_NavigationResourceRegister(
        tx, css_url,
        PBROWSER_NAVIGATION_RESOURCE_REQUIRED,
        PBROWSER_NAVIGATION_RESOURCE_ROLE_STYLESHEET,
        &index) == PBROWSER_OK &&
    PBrowser_NavigationResourceBeginAttempt(tx, index) == PBROWSER_OK) {
    /* Network code owns response bytes until this call returns. */
    PBrowser_NavigationResourceSetData(tx, index, bytes, byte_count);
}

if (PBrowser_NavigationResourceCommitGate(tx) ==
    PBROWSER_NAVIGATION_GATE_READY) {
    /* The host may now run layout and submit the candidate page. */
}
PBrowser_NavigationResourceGet(tx, index, &info);
PBrowser_NavigationResourceDestroy(tx);
```

`SetData` copies the input into Browser-owned storage; the caller still owns `bytes`. Failed or cancelled attempts use `Fail`/`Cancel`, and `ShouldRetry` only permits transport retries within the fixed budget. `GetStats` supplies the gate counters and bounded summary for logs. `CopyData` returns a caller-owned copy of a ready resource. The APIs are synchronous and must be serialized by the caller; they do not normalize URLs, cache across transactions, create threads, or retain Core document pointers.

### Candidate/resource commit snapshot

`PBrowser_NavigationCommitGetInfo` composes a candidate handle and its
independent resource transaction at the page-commit boundary. It does not
merge either handle or perform a transition. The bounded snapshot reports the
candidate result, resource gate and `can_commit` flag in one call:

```c
PBrowserNavigationCommitInfo commit;

memset(&commit, 0, sizeof(commit));
commit.size = sizeof(commit);
if (PBrowser_NavigationCommitGetInfo(
        candidate, tx, current_generation, &commit) == PBROWSER_OK &&
        commit.decision == PBROWSER_NAVIGATION_COMMIT_READY &&
        commit.can_commit) {
    /* Parse/layout/swap are still owned by the host. */
}
```

`READY` requires an active, current, uncancelled candidate and a ready
resource gate. The call does not transition either handle; it may refresh the
resource transaction's derived gate/summary fields. `RESOURCE_PENDING`, `REQUIRED_FAILED` and
`RESOURCE_CANCELLED` describe the resource side; `CANDIDATE_CANCEL_REQUESTED`,
`CANDIDATE_CANCELLED`, `CANDIDATE_STALE`, `CANDIDATE_FAILED` and
`CANDIDATE_COMMITTED` describe candidate-side exclusion. Optional resource
failures remain compatible with `READY`, while required failures and any
cancelled resource remain fail-closed. The snapshot is synchronous;
`PBrowser_NavigationCandidateMarkCommitted` must still recheck the
candidate after the host's final layout/swap boundary. The host may log the
snapshot, but must not reproduce these classifications from worker flags or
resource counters.

### Navigation cleanup snapshot

`PBrowser_NavigationCleanupGetInfo` is the last observation before a host
releases the two independent handles for one request. It copies the candidate
result and the complete bounded resource stats—including the resource gate,
pending count, hash-only failure summary and fallback-family counters—into a
caller-owned `PBrowserNavigationCleanupInfo`. The output does not retain either
handle and remains valid after both handles are destroyed.

The snapshot does not settle work itself. The host first joins the request's
worker and settles any remaining resource entries (normally with
`PBrowser_NavigationResourceCancelAll` for a failed or superseded request),
then makes an otherwise-active candidate terminal and reads the snapshot. A
`can_release` value of `1` means there is no pending candidate/resource work;
`COMMITTED` additionally requires a `READY` resource gate. Pending work reports
`PBROWSER_NAVIGATION_CLEANUP_CANDIDATE_PENDING` or
`PBROWSER_NAVIGATION_CLEANUP_RESOURCE_PENDING` with `can_release == 0`, while a
committed candidate paired with a non-ready settled gate reports
`PBROWSER_NAVIGATION_CLEANUP_INCONSISTENT` and must not be treated as a
successful commit. Terminal failed,
cancelled and stale candidates are releasable once their resource transaction
is settled.

该 API 只提供同步的 Browser-owned 状态；它不暴露响应字节、线程、窗口、消息或应用日志，
也不替代页面交换边界的最终 `PBrowser_NavigationCandidateMarkCommitted` 检查。

### Navigation candidate lifecycle

`PBrowser_NavigationCandidate*` owns the admission state for one pending
document without owning the document itself. The handle stores an immutable
generation and the states `ACTIVE`, `RETIRED`, `COMMITTED` or `FAILED`, plus a
cooperatively polled cancellation flag. `PBrowser_NavigationCandidateCanApply`
requires both an active, uncancelled handle and the host's current generation;
`MarkCommitted` enforces the same check, so a late worker message cannot commit
an older candidate. `RequestCancel` and `Retire` are separate operations:
cancel asks the worker to stop, while retire makes the candidate permanently
ineligible and is idempotent.

The host owns the monotonic counter, worker/thread, response, resource
transaction, WM messages and page swap. It should create a candidate when it
assigns a generation, request cancellation before retiring a superseded
candidate, keep the opaque handle reachable until the worker completion is
consumed, and then mark the current candidate committed or failed:

```c
HANDLE candidate;
PBrowserNavigationCandidateInfo info;
int network_or_document_failure;

candidate = PBrowser_NavigationCandidateCreate(next_generation);
/* The host starts its worker and keeps candidate with that request. */
PBrowser_NavigationCandidateRequestCancel(candidate); /* when superseded */
PBrowser_NavigationCandidateRetire(candidate);
/* On UI completion, compare against the host's current generation. */
if (PBrowser_NavigationCandidateCanApply(candidate, current_generation)) {
    PBrowser_NavigationCandidateMarkCommitted(candidate, current_generation);
} else if (network_or_document_failure) {
    PBrowser_NavigationCandidateMarkFailed(candidate);
} else {
    /* stale or retired: discard without page teardown/history mutation */
}
PBrowser_NavigationCandidateGetInfo(candidate, current_generation, &info);
PBrowser_NavigationCandidateDestroy(candidate);
```

取消标志可由 worker 在宿主请求取消时轮询，其他状态和快照由宿主串行化；这些 API 不会
中止阻塞 socket、发窗口消息、执行 page teardown 或提交 history。失败或退休的 candidate
必须由宿主丢弃，不得进入 teardown 或修改 history。

`PBrowser_NavigationCandidateGetResult` 返回按 Browser 状态和当前 generation 分类的只读诊断
快照（`PENDING`、`COMMITTED`、`FAILED`、`CANCELLED` 或 `STALE`）；宿主只复制结果，不重算
分类，也不把它当作网络错误文本。

### 队列与生命周期

Browser 提供受限 timer、animation frame、microtask、idle callback、message、visibility、window focus 和 page lifecycle 运行入口。队列由宿主在 UI 消息循环中按预算驱动；DLL 不建立自己的线程或无限 event loop。

宿主可以逐项调用这些入口，也可以用
`PBrowser_ScriptSessionRunTaskCheckpoint` 驱动一次统一检查点。统一入口按
`phase_mask` 选择 timer、animation frame、message 和 idle callback；每个已选阶段
完成后立即运行一次有界 microtask checkpoint。阶段顺序固定为 timer → frame →
message → idle，未选择的阶段不会被隐式执行；`phase_mask == 0` 只运行
microtask。宿主传入自己的单调毫秒时钟、frame timestamp、idle deadline 和
message limit，Browser 不创建线程、不接管时钟，也不拥有窗口消息。参考宿主在
窗口 UI 线程上以 16 ms `WM_TIMER` 调用 `PBROWSER_SCRIPT_PUMP_ALL`；其他应用可以
按自己的消息循环和功耗策略选择阶段或继续使用独立入口。任何入口返回脚本错误
时，宿主都应停止继续调度该 session，并按应用策略记录失败。

```c
unsigned long now_ms;

now_ms = host_monotonic_ms();
if (PBrowser_ScriptSessionRunTaskCheckpoint(
        session, now_ms, now_ms, now_ms, 16UL,
        PBROWSER_SCRIPT_PUMP_ALL) != PSCRIPT_OK) {
    /* Stop scheduling this session and keep the host's error policy. */
}
```

页面首次完成加载时，宿主在 classic script 执行完毕后调用
`PBrowser_ScriptSessionDispatchPageLifecycle(session, "complete")`。Browser 按
`readystatechange`、`DOMContentLoaded`、window/document `load` 的既有顺序
更新 `document.readyState`，随后只派发一次 window `pageshow`。宿主若再次提交
`complete` 不会重复这次初始事件。窗口重新可见时，宿主调用
`PBrowser_ScriptSessionDispatchVisibility(session, 0)`；从可见变为隐藏先派发
document `visibilitychange` 再派发 window `pagehide`，恢复可见时按同样边界派发
`visibilitychange` 和一次 `pageshow`。相同的 hidden 值不会产生重复事件，所有
这些有限 page event 的 `persisted` 都是 `false`，因为 Browser 不实现 bfcache。

顶层窗口的激活状态也由宿主显式推进：每次收到 `WM_ACTIVATE` 时调用
`PBrowser_ScriptSessionDispatchWindowFocus(session, focused)`，其中非零值表示
激活、零表示停用。Browser 对输入值归一化，只在状态真正变化时更新
`document.hasFocus()`，并同步派发一次可信、不可取消、不冒泡的 window `focus`
或 `blur`；`window.onfocus`/`onblur` 和普通监听器都遵循同一顺序。新 session
默认从 focused 开始，若宿主在非激活窗口中创建它，必须立即补发一次零值。这个
入口只描述脚本可见的顶层窗口状态，不创建 HWND，也不代替宿主管理 native
控件焦点、焦点矩形或跨窗口策略。

跨文档候选已经完成 parse、资源、style、layout 并准备提交时，宿主仍应保留旧 document/session，先调用 `PBrowser_ScriptSessionDispatchBeforeUnload`。该入口同步派发当前 window 的 cancelable、non-bubbling、trusted `beforeunload` 事件，覆盖 `window.onbeforeunload` 与 `addEventListener('beforeunload', ...)`；调用 `preventDefault()`、写入非空 `event.returnValue`，或从 `window.onbeforeunload` 返回非空字符串都会把 `out_prevented` 置为非零。它只返回取消决定，不显示提示框、不执行导航或 teardown；提示 UI 和“继续/取消”的产品策略由宿主决定，脚本调用或结果解析失败时必须按取消处理。参考宿主没有确认对话框，因此直接拒绝候选提交或窗口关闭并保留旧页。

只有 `beforeunload` 允许后，宿主才调用 `PBrowser_ScriptSessionDispatchPageTeardown`。首次 teardown 在旧页可见时依次派发 document `visibilitychange`、window `pagehide` 和 `unload`，再清理 timer、animation frame、microtask、idle、message 队列；已隐藏或已派发 `pagehide` 的页面不会重复派发，重复调用也保持幂等。宿主随后停止 native 回调、销毁控件和 script session，最后释放旧 Core document 并安装候选页。失败或被取消的候选不得调用 teardown，旧页、旧 session 和旧队列必须继续保留。两个入口都不创建线程、不访问 HWND，也不得从 Browser callback 内重入或销毁当前 session。

## 典型 Core 组合

一个浏览器宿主通常：

1. 用 `positron_core.dll` 创建、style、layout 当前文档；
2. 为该文档构造 callback context；
3. 把 DOM/Event/form/navigation callback tables 注册到 Browser session；按需增加
   activeElement/focus、interaction 和 form method callbacks，并让宿主把 form id
   映射到 Core reset 或 validation/no-validation primitive；页面级滚动可见性使用
   focus request 的 Ex callbacks；
4. 显式 bootstrap；在页面脚本运行前注册可选的 activeElement/focus request callback，并按文档
   顺序执行允许的 classic script；
  5. 把 WM 输入转换为 Browser typed transaction；嵌套 overflow 指针则按 Core 的 document-space 合同调用 `PCore_OverflowPointer`；
  6. 只在 Browser 允许默认动作后修改 Core/native 控件；`Element.scrollTo()`/`scrollBy()` 的位置由 Core callback 返回；
  7. mutation 或 overflow scroll 后重新 layout/paint，并用 dirty rect 限定失效；
  8. 在导航或 fragment/traversal 改变页面前，把当前 viewport 写入对应 history entry；目标页面提交后读取目标 snapshot，并由宿主 clamp/apply；
  9. 导航候选成功后，在旧 document/session 仍有效时调用 Browser 的 page-teardown 入口，再销毁旧 session/document 并提交新页面与 history；失败候选保留旧页及其队列。

完整组合示例见 [`../test_host/`](../test_host/README.md)，但产品应用应根据自己的窗口、网络和安全策略实现 callbacks。

## 所有权与错误

- History 和 script-session handle 由 Browser 创建/销毁，不使用 `CloseHandle`。
- 导航资源事务 handle、URL 副本和资源字节均由 Browser 拥有；每个 create 都必须配对 `PBrowser_NavigationResourceDestroy`，调用方需要自己的数据时使用 `PBrowser_NavigationResourceCopyData`。
- 导航 candidate handle、generation、取消/退休标志、committed/failed 终态和结果分类由 Browser 拥有；每个 create 都必须配对 `PBrowser_NavigationCandidateDestroy`，不得用 `CloseHandle` 或复制内部状态。结果结构是同步借用快照，调用方只应复制值，不得把它当作可释放对象。
- History 返回的 entry/state 字符串是借用值，调用方不得 free。
- History 的 viewport snapshot 由 Browser entry 拥有；`PBrowser_HistoryEntryScroll` 只写入调用方提供的两个整数，`PBrowser_HistorySetEntryScroll` 不保存指针。Browser 不替宿主 clamp、滚动窗口或绘制页面。
- callback table 和 `pw` 由宿主持有，必须活到 unregister 或 session destroy。
- callback 中的字符串、event info 和输出缓冲只在同步调用期间有效。
- 结构体必须设置正确 `cbSize`；较小旧结构保持兼容，未知布局应拒绝。
- `PBROWSER_OK` 以外的稳定错误区分参数、容量、origin、状态、范围和方法问题。

## 当前边界

- 浏览器 JavaScript 是显式 opt-in 的有限组合，不是完整 DOM/Web API 或安全沙箱。
- History 有界且不持久；多窗口、opener 和跨窗口 history 未实现。
- `document.activeElement`、focus/blur、autofocus 只有在宿主注册 Core callback 后才工作；
  Browser 只提供 id 投影、`preventScroll` 和最多 64 层 nested reveal，不提供完整焦点算法。
- 几何与滚动 getter 只读取最近一次 Core layout 的整数快照；`getClientRects()` 最多 16
  个片段，宿主负责 extent、clamp 和 CSS/设备换算。transforms、Range/Selection、pinch
  zoom、scroll-margin、smooth/inertia 和完整 scroll tree 不在范围内。
- `:visited` 仅反映宿主 Ex callback，不提供持久化、隐私隔离或跨窗口共享；完整 Selectors、
  backdrop、Range/Selection、富文本、clipboard 和 IME 不在范围内。
- 系统 picker、OEM SIP/IME、真实触摸、旋转和焦点视觉必须由宿主和设备验收。
- ABI、常量和结构布局只以 [`positron_browser.h`](positron_browser.h) 为准。

参见 [`ARCHITECTURE.md`](../docs/ARCHITECTURE.md) 与 [`KNOWN_LIMITATIONS.md`](../.agents/KNOWN_LIMITATIONS.md)。

# `positron_browser`

`positron_browser.dll` 是浏览器 session/history 与浏览器脚本 bridge 的产品组合层。
它不创建窗口、不抓取网络、不拥有 Core document，也不代替宿主处理 WM 控件；它把
浏览器状态、脚本 context、bootstrap 和稳定 typed callback ABI 组合成可复用入口。

## 输出与依赖

- 工程：`positron_browser.vcproj`
- 输出：`bin\Debug\positron_browser.dll`、对应 `.lib`
- 公共头：`positron_browser.h`
- 运行时依赖：`positron_script.dll`、`positron_json.dll`
- 典型组合：调用者另行持有 `positron_core.dll`、网络和 native WM 控件

其他项目链接 `positron_browser.lib`，部署 DLL 依赖，并以 callback 形式提供宿主的
document、DOM、navigation、event、input、keyboard、focus、EDIT change/post-change input、click、programmatic `HTMLElement.click()`、`HTMLElement.disabled`、控件与受限 form-level `checkValidity()`/`reportValidity()`、`willValidate`、`validity` 查询、`setCustomValidity()`、`validationMessage`、`required`、`readOnly`、`multiple`、`noValidate`、`formNoValidate`、`name`、form `action`/`method`/`enctype`/`target`/`autocomplete`/`acceptCharset`、submitter `formAction`/`formMethod`/`formEnctype`、控件 `placeholder`/`autocomplete`/`inputMode`/`type`、`min`/`max`/`step`、`pattern`/`minLength`/`maxLength`、submit/reset、invalid、file-input、checkbox/radio input/change 和 SELECT input/change 适配。这些表单属性通过既有 attribute callback bridge 实现；validation query 通过独立的 size-tagged callback 获取 core 的控件状态或 form 聚合结果，report-validity callback 负责同步 report/query 与 invalid-event 路由，custom validity 通过另一个 size-tagged UTF-8 get/set callback 获取/更新 application-owned message，`validationMessage` 在 custom message 为空时可使用宿主提供的固定英文 fallback；对程序化 click，推荐使用 Ex callback，让 DLL 负责 disabled 抑制、typed click、submit/reset 事件顺序和 submit 验证，再由宿主 default-action callback 执行 core/WM 副作用；file input 仍只由宿主排队系统 picker。系统 picker、文件系统权限和窗口生命周期仍由宿主 GUI 拥有。
`test_host.exe` 是一个完整的组合示例，但不是私有 API 的唯一消费者。

锚点激活有两层入口：旧的 `PBrowser_ScriptSessionDispatchAnchorClick()` 保持 href-only
兼容；需要 target/rel 的消费者使用 size-tagged
`PBrowser_ScriptSessionDispatchAnchorClickEx()`。当宿主通过 programmatic anchor callback
提供 Core 的 href、target、rel 快照时，browser layer 先派发可取消 `click`，再把同一组
借用字符串放入 `PBrowserScriptNavigationInfo` 的导航回调；`HTMLElement.target` 与
`HTMLElement.rel` 反射原始属性。browser DLL 不创建窗口、不解析 URL、不请求网络，也不
决定 `_blank`/named target 的窗口复用；这些副作用仍由宿主负责。导航回调同时收到由
browser layer 分类的 `target_kind`（default、`_self`、`_parent`、`_top`、`_blank` 或
named），因此宿主不必复制关键字解析。Core 对应的
`PCore_LinkInfoByIdEx()`/`PCore_LinkAtEx()` 只复制到调用者缓冲区并在容量不足时失败。

`window.open(url, target, features)` 通过同一导航 callback 传递一个
`PBROWSER_SCRIPT_NAVIGATION_OPEN` 请求。browser layer 只负责 URL/target 的有界参数和
target_kind 分类；宿主接受 `_self`、`_parent` 或 `_top` 时可把请求映射为当前文档导航，
脚本得到当前 bounded `window`。没有窗口管理器时，省略 target（DEFAULT）、`_blank` 和
不匹配当前 `window.name` 的 named target 必须返回 `null`；匹配当前名称的 named target
可以复用当前 context。browser layer 会把当前名称作为 OPEN metadata 的 `context_name`
快照传给宿主；features 在当前子集中只被忽略，不创建 HWND、不改变 opener 或安全策略。
窗口创建、复用、关闭、跨窗口 history 和网络仍由宿主拥有。

native EDIT 的产品事务入口是 additive 的
`PBrowser_ScriptSessionRegisterNativeEditCallbacksEx()`。调用者提供现有的 input/change
core propagation callback，并为每个 native EDIT 传入稳定的非零 session token 和文档几何；
`PBrowser_ScriptSessionDispatchNativeEditBeforeInput()` 由 browser layer 负责 beforeinput
取消与 pending metadata，`PBrowser_ScriptSessionDispatchNativeEditInput()` 在宿主确认
`PCore_TextInputSetValue()` 成功后派发 input 并记录 dirty，
`PBrowser_ScriptSessionDispatchNativeEditBlur()` 只在 dirty 时派发一次 change，
`PBrowser_ScriptSessionDispatchNativeEditComposition()` 处理 START/UPDATE/END 的
compositionstart、insertCompositionText beforeinput、compositionupdate、compositionend
顺序并把 UPDATE metadata 接到 native commit；`PBrowser_ScriptSessionDispatchNativeEditResult()`
把完整 WM6 `GCS_RESULTSTR` 作为一次 browser-owned composition update，保留同一 pending
input metadata，宿主随后只负责原生文本替换、native commit 和 composition end；
`PBrowser_ScriptSessionResetNativeEditState()` 用于销毁/重建 native 控件时丢弃状态。
最多跟踪 16 个 token，每个 inputType/data/preedit 字符串最多 255 字节；这些入口不拥有
WM EDIT/WM_IME、文本 mutation、焦点窗口、SIP/IME 候选词窗口或 SELECT 控件。旧的独立 input/edit
注册入口继续兼容其他宿主。

native SELECT 的产品提交入口是 additive 的
`PBrowser_ScriptSessionRegisterNativeSelectCallbacksEx()`。宿主在 WM SELECT 控件和
`PCore_SelectSetOptionSelected()`/对应多选 mutation 成功后，传入稳定 token、几何和选择
快照；browser layer 统一同步派发不可取消的 `input` → `change`，并校验同一 token 的
single/multiple 形状。`PBrowser_ScriptSessionResetNativeSelectState()` 用于销毁或重建控件时
丢弃有界 token 状态，最多跟踪 16 个 token。`PBrowser_ScriptSessionDispatchNativeSelectKey()`
现在校验 native SELECT 的稳定 token 与 `keydown`/`keyup` phase，复用已有 typed key callback
并返回 cancel/default-allowed，由宿主决定是否让 WM 控件继续处理真正的默认动作。另有
`PBrowser_ScriptSessionDispatchNativeSelectFocus()` 复用同一 token state，统一派发不可取消的
`focus` → `focusin` / `blur` → `focusout`，并抑制重复焦点通知；宿主在控件焦点消息后只提供
几何和 focused 状态。上述入口不拥有 WM SELECT、Core selection mutation、下拉展开/关闭、
窗口重绘、SIP/IME 或系统 picker。

单选 COMBOBOX 的下拉事务使用同一 bounded state 的
`PBrowser_ScriptSessionDispatchNativeSelectInteraction()`：宿主在 `CBN_DROPDOWN`、候选
`CBN_SELCHANGE`、`CBN_SELENDOK` 或 `CBN_SELENDCANCEL` 时提交 size-tagged phase；browser
layer 只记录候选，不在 begin/candidate 阶段改变 Core。END_OK 通过 `out_should_commit` 告知
宿主是否观察到候选，宿主随后才调用 commit 入口；END_CANCEL 清除候选，宿主负责把原生
COMBOBOX 恢复到 Core 选中项。该入口仅适用于 single-select，不拥有 WM 下拉窗口、键盘默认
动作、Core selection mutation、回滚消息、SIP/IME 或视觉绘制；最多继续复用 16 个 token。

`PBrowser_ScriptSessionRegisterProgrammaticClickCallbacksEx()` 是向新消费者推荐的
程序化表单激活入口。调用者提供 `get_target`（返回 checkbox/radio/submit/reset/file 的
UTF-8 id、几何和 disabled）、`validate_submit`、`perform_default` 与
`dispatch_generic`；浏览器 DLL 自己调用已注册的 click/form-event callback，并在
`perform_default` 前固定顺序。`perform_default` 的 action 使用
`PBROWSER_SCRIPT_CLICK_DEFAULT_TOGGLE/SUBMIT/RESET/FILE`，所有结构体均为 size-tagged，
字符串和 target/default 信息只在同步 callback 内借用。旧的
`PBrowser_ScriptSessionRegisterProgrammaticClickCallbacks()` 仍保留给需要自行拥有整套
激活逻辑的兼容宿主。文件控件的系统 picker 不属于 browser DLL；宿主在打开 picker 前、
Core 写入新路径后或用户取消/失败时调用
`PBrowser_ScriptSessionDispatchNativeFileSelection()` 的 BEGIN/COMMIT/CANCEL phase，
由 browser layer 统一产生一次不可取消的 `input(insertFromFile)` → `change`，并用
`PBrowser_ScriptSessionResetNativeFileState()` 清除销毁/重建时的 16-token bounded state。

当前 DOM snapshot 还提供 browser-owned 的 `document.doctype`：它是稳定、只读的
`DocumentType` wrapper，包含有限 metadata、owner/root/position/contains、identity/equality
和 document child order 视图；它不要求调用者提供 core doctype parser，也不提供节点创建或
mutation。该能力仍遵守 session-scoped、fail-closed 的关系边界。

当前 Node snapshot 还为 document、DocumentType、HTML element、CharacterData 和 Attr wrapper
提供受控的 `baseURI`、`namespaceURI`、`prefix`、`lookupNamespaceURI()`、`lookupPrefix()` 与
`isDefaultNamespace()`，以及元素上的有界 `setAttributeNS()`/`removeAttributeNS()`。
`baseURI` 读取并跟随当前 session URL；namespace 只承诺 HTML/XML 的有限值，未知 prefix/URI
和非法 mutation 组合 fail closed，Attr 查询沿 owner element 上下文工作。该能力不实现完整
XML/namespace parser、NamespaceError、节点创建或完整 DOM tree。

next588 又在 document 与 HTML element wrapper 上提供受控的
`getElementsByTagName()`/`getElementsByClassName()`。查询沿当前 bounded relation snapshot
按 DFS 文档顺序生成静态 HTMLCollection，支持 tag 大小写归一、`*`、规范化多 class token、
`item()`/`namedItem()`、`forEach()`/`keys()`/`values()`/`entries()`、默认 iterator 和
`Symbol.toStringTag`；element 查询排除 owner，document 查询包含 structural `documentElement`。
空白/未知输入返回空集合；这不是 live collection、通用 CSS selector、节点创建或 mutation API。
为容纳这组 bootstrap，browser session heap ceiling 为 608 KiB，独立 `positron_script` 默认堆
仍为 512 KiB。

next589 又把同一受限 matcher 接入 document 的 `querySelector()`/`querySelectorAll()`：支持
tag、`#id`、class、有限 attribute、compound、`*` 和 `:root`，按 DFS 文档顺序返回首个匹配
或 NodeList snapshot；root/head/body wrapper 复用既有 identity，空白和 `>`/`+`/`~` 组合器
fail closed。它不提供完整 CSS parser、live DOM、节点创建或 mutation。

next590 又在同一 bounded traversal 上提供 document named collection projection：
`document.getElementsByName()` 精确匹配显式 `name` 值并返回 DFS 顺序的 NodeList snapshot，
`document.forms`、`document.images`、`document.scripts` 返回静态 HTMLCollection。它们复用
`item()`/`namedItem()`、`forEach()`/`keys()`/`values()`/`entries()`、默认 iterator、
`Symbol.toStringTag` 和稳定 wrapper identity；不提供 live 更新、通用 named properties、节点
创建、通用 mutation 或新的 core ABI。

next591 又在同一 traversal 上提供 `document.links` 与 `document.anchors`：`links` 只收集显式
`href` 的 `a`/`area`，`anchors` 只收集显式 `name` 的 `a`，都按 DFS 顺序返回静态
HTMLCollection，复用 `item()`/`namedItem()`、迭代协议和 wrapper identity。它不实现链接 URL
解析、导航副作用、live 更新、节点创建、通用 mutation 或新的 core ABI。

next592 又在同一 traversal 上提供 document 与 element 的 `getElementsByTagNameNS()`：namespace
支持 `*` 或精确字符串，localName 支持 `*` 或大小写敏感字符串；document 结果包含 root，element
结果排除 owner。返回静态 HTMLCollection，复用 `item()`/`namedItem()`、迭代协议和 wrapper
identity；null、空或未知输入 fail closed，不实现 XML/SVG namespace parser、live 更新、节点
创建、通用 mutation 或新的 core ABI。

next593 又在同一属性 snapshot 上提供只读的 `getAttributeNS()`、`hasAttributeNS()`、
`getAttributeNodeNS()` 及 Attr 的 `namespaceURI`/`prefix`/`localName` 元数据；null/空 namespace
表示无 namespace，`xml`/`xmlns` 映射已知 XML/XMLNS namespace，未知输入 fail closed。返回的
Attr 仍复用同 owner 的 live value/nodeValue wrapper；不提供 namespace mutation、XML/SVG parser、
live collection、节点创建或新的 core ABI。对应 `TEST802–821` 及兼容/缩减回归门已由自动设备门
覆盖。

next594 又为既有 `NamedNodeMap` 增加只读的 `getNamedItemNS(namespace, localName)`：它复用
next593 的 null/空 namespace、XML/XMLNS 已知前缀、未知输入 fail closed、大小写敏感 localName、
coercion、Attr identity 和 map 对属性增删的观察语义；不提供 `setNamedItemNS()`、
`removeNamedItemNS()`、XML/SVG parser、namespace mutation、节点创建或 live collection。对应
`TEST822–841` 及缩减回归门已由自动设备门覆盖。

next595 又在同一 wrapper snapshot 上提供只读 `lookupPrefix(namespace)`：document、DocumentType、
HTML element、CharacterData 和 Attr 都识别有限的 XML → `xml` 映射；XMLNS → `xmlns` 仅对
对应 `xmlns:*` Attr 生效，HTML default、null/空值和未知 URI fail closed。参数仅做有限
String coercion，不解析 namespace declaration，不提供 prefix mutation、节点创建、live collection
或新的 core ABI。对应 `TEST842–861` 及缩减回归门已通过自动设备门。

next596 又为元素 wrapper 提供有界 `setAttributeNS(namespace, qualifiedName, value)` 与
`removeAttributeNS(namespace, localName)`：null/空 namespace 只接受无前缀名称，XML/XMLNS
只接受对应 `xml`/`xmlns` 前缀；未知 URI、未知 prefix、空名称和多重冒号安全无操作。成功
写入复用既有 attribute bridge，Attr identity 与 namespace read API 保持一致；不提供完整
NamespaceError、namespace declaration、XML/SVG parser、节点创建、live collection 或新的
core ABI。对应 `TEST862–881` 及缩减回归门已通过自动设备门。

next597 又为既有 `NamedNodeMap` 提供有界 `setNamedItemNS()`/`removeNamedItemNS()`，并为元素
wrapper 提供 `setAttributeNodeNS()`。null/空 namespace 与 XML/XMLNS 前缀沿用 next596 的有限
边界；跨 owner Attr 只复制名称和值，source `ownerElement` 与 wrapper identity 保持不变，替换
返回目标 owner 的旧 Attr，未知/非法输入和缺失删除 fail closed。该能力不实现完整
NamespaceError、namespace declaration、XML/SVG parser、节点创建、live collection 或新的
core ABI；对应 `TEST882–901` 与 `TEST802–901` namespace 缩减回归已通过自动设备门。

next598 又为 Attr wrapper 提供 bounded leaf-node 语义：`isId` 仅识别无 namespace 的 `id`，
`textContent` 与 `value`/`nodeValue` live 同步；`childNodes` 始终为空但支持 `item()`/iterator，
`hasChildNodes()` 为 false，parent/child/sibling 关系返回 null；`isSameNode()` 按 identity，
`isEqualNode()` 只比较 nodeType/name/value。`ownerElement` 不会被伪装为 tree parent，也不新增
core ABI、节点创建或 live collection；对应 `TEST902–921` 与 `TEST802–921` 缩减回归已通过自动设备门。

next599 又为 Attr wrapper 提供 detached-node relation 语义：`isConnected` 固定为 false，
`getRootNode()` 返回自身，`contains()` 只接受自身，`compareDocumentPosition()` 对非自身对象
返回固定 `DISCONNECTED|IMPLEMENTATION_SPECIFIC`。`ownerElement` 仍只是 metadata，不变成
parent；不新增 core ABI、节点创建或 live collection；对应 `TEST922–941` 与
`TEST802–941` 缩减回归已通过自动设备门。

next600 又为 `childNodes` 返回的文本、注释和无 id 子节点 wrapper 提供 bounded `contains()`：
wrapper 自包含，父元素可包含直接子节点，owner/兄弟/document/非法对象均 fail closed。实现复用
受控 relation bridge，不新增 core ABI、文本 mutation、节点创建或 live collection；对应
`TEST942–961` 与 `TEST802–961` 缩减回归已通过自动设备门。

next601 又为 `document.doctype` snapshot 提供只读 `publicId`、`systemId`、`internalSubset`：
HTML doctype 默认值分别为空字符串、空字符串和 `null`，字段不可重定义或删除，且不改变既有
DocumentType 的 branding、owner/root/position/contains、namespace 或 baseURI。该能力不解析
DTD/实体、不提供节点 mutation 或新的 core ABI；对应 `TEST962–981` 与 `TEST802–981` 缩减回归
已通过自动设备门。

next602 又为同一 `document.doctype` snapshot 提供独立、稳定、冻结的空 `entities` 与 `notations`
`NamedNodeMap`。两者的 length 为 0，indexed/named/namespace lookup 为空，iterator 立即结束，
mutation 方法 fail closed，并带有限的 `NamedNodeMap` branding；实现 lazy 复用既有 `m10(null)`，
不解析 DTD/实体、不创建节点、不增加 core ABI。对应 `TEST982–998` 与 `TEST802–998` 缩减回归
均已通过自动设备门；tracked `test_host.ini` 默认仍为 `javascript=0`，本批不需要人工页面验收。

next603 又为普通属性 `NamedNodeMap` 与 doctype 的空 `entities`/`notations` map 提供有界的
`forEach()`、`keys()`、`values()`、`entries()` 和默认 values iterator。迭代器自身可迭代，Attr
wrapper identity 保持稳定，`forEach` 的 callback/`thisArg` 与非法 callback 边界明确；这些方法
读取同步 snapshot，不提供 live collection、节点创建、DTD/实体解析或新的 core ABI。对应
`TEST1000–1017` 与拆开特殊 `TEST999` 的 `TEST802–998,1000–1017` 缩减回归均已通过自动设备门，
本批不需要人工页面验收。

next604 又为静态 `HTMLCollection` snapshot 增加只读、不可枚举的 `id`/`name` 直达属性；
`item()`、`namedItem()`、`forEach()`、`keys()`、`values()`、`entries()`、默认 iterator 与
wrapper identity 保持不变，`NodeList` 保留无 named projection 的边界。保留方法名、`length`、
数字索引等已有成员，不让页面属性覆盖它们；属性值只来自创建该 snapshot 时的元素，不承诺
live 更新，也不引入节点创建、mutation 或新的 core ABI。对应 `TEST1018–1035` 与拆开特殊
`TEST999` 的 `TEST802–998,1000–1035` 缩减回归均已通过自动设备门，本批不需要人工页面验收。

next605 又为 `form.elements` 的重复 `id`/`name` 增加有限的 `RadioNodeList` snapshot：唯一匹配
仍返回 element，重复匹配返回可迭代、只读的分组 wrapper；`value` getter 读取当前选中 radio，
setter 只选择同值控件。直接属性不可枚举、不可写、不可配置，并与 `namedItem()` 复用 session
内缓存 identity；缺失名称返回 `null`，普通 HTMLCollection 仍返回首匹配 element。next605
本身仍不实现 live `HTMLFormControlsCollection` 或 label 关联；后续 next614 只补齐
有界的 label/control 关系，不改变这些 live/mutation 边界。fieldset 的有效 disabled 状态由
next615 的 `positron_core.dll` 表单判定提供，browser layer 仍不把祖先状态伪装成原始
`control.disabled` 属性。
对应 `TEST1036–1053` 定向门和 `TEST802–998,1000–1053` 缩减回归均已通过自动设备门；为
容纳新增 bootstrap，browser session heap ceiling 为 624 KiB，独立 `positron_script` 默认堆仍为
512 KiB。本批不需要人工页面验收。

next607 将程序化表单激活的产品策略从宿主迁入本 DLL：
`PBrowser_ScriptSessionRegisterProgrammaticClickCallbacksEx()` 由 browser layer 统一处理
disabled 静默、typed click、submit/reset form-event 顺序、submit validation 与取消，然后
调用宿主的 target/validation/default-action/generic callback。宿主仍拥有 Core document、WM
窗口、native EDIT/SELECT、系统 picker、导航和绘制副作用；旧的 programmatic-click callback
入口保持兼容。TEST228–230 与 TEST1055 已在 WM6 设备门通过，未改变默认 `javascript=0` 配置，
也不宣称完整 HTML activation 或 OEM SIP/IME 兼容。

next608 将 native EDIT 的输入事务策略从宿主迁入本 DLL：
`PBrowser_ScriptSessionRegisterNativeEditCallbacksEx()` 配合三个同步入口，统一处理
beforeinput 的取消与 pending input metadata、native value commit 到 input、dirty tracking
以及 blur 时一次性 change。宿主只在 WM EDIT 消息后提交 core value，并提供几何和 core event
propagation callback；session token 和字符串状态由 browser layer 有界保存，控件销毁/重建时
由宿主调用 reset。该入口不拥有 WM EDIT、文本 mutation、焦点窗口、composition 生命周期、
SIP/IME 或 SELECT。TEST1056 及 TEST228–230、1055、999 的 next608 设备门已通过。

next609 将 native SELECT 的提交事件顺序迁入本 DLL：
`PBrowser_ScriptSessionRegisterNativeSelectCallbacksEx()` 与
`PBrowser_ScriptSessionDispatchNativeSelectCommit()` 在宿主完成 Core 选择 mutation 后，
有界校验 stable token、single/multiple 形状和选择快照，并同步派发 `input` → `change`。
宿主在控件销毁/重建前调用 `PBrowser_ScriptSessionResetNativeSelectState()`；WM SELECT、
键盘 default-action、Core mutation、重绘和 SIP/IME 仍由宿主持有。TEST1057、67、71、118、
999 的 next609 设备门已通过。

next610 在该 bounded state 上增加 native SELECT 焦点族入口：
`PBrowser_ScriptSessionDispatchNativeSelectFocus()` 接收稳定 token、几何和 focused 状态，
由 browser layer 维护每 token 的焦点状态并同步派发 `focus` → `focusin` 或
`blur` → `focusout`；重复通知幂等，callback 失败保持旧状态以便重试。该入口复用已注册的
`PBrowserScriptFocusCallbacks`，不新增宿主私有结构；WM 焦点窗口、Core interaction、下拉
展开/关闭、键盘默认动作和 OEM SIP/IME 仍由宿主负责。TEST1058、67、71、1057、999 的 next610
设备门已通过。

next611 在同一 bounded state 上增加单选 SELECT 下拉事务入口：
`PBrowser_ScriptSessionDispatchNativeSelectInteraction()` 记录 begin/candidate/confirm/cancel
phase，只有 END_OK 且此前收到候选时才返回 `out_should_commit=1`。宿主接到该结果后才执行
Core selection mutation 和既有 input/change commit；END_CANCEL 时由宿主将 COMBOBOX 恢复到
Core 快照。TEST1059 覆盖 interaction ABI，TEST67 覆盖合成 WM 通知探针；该入口不承诺
下拉窗口视觉、键盘默认动作、SIP/IME 或 OEM 行为兼容。

next612 在同一 bounded state 上增加 native SELECT 键盘入口：
`PBrowser_ScriptSessionDispatchNativeSelectKey()` 校验稳定 token、`keydown`/`keyup` phase
和 Enter/Arrow 元数据，复用 `PBrowserScriptKeyCallbacks` 并把取消/default-allowed 结果交回
宿主。宿主继续拥有 WM COMBOBOX 的真正默认动作、Core selection mutation、下拉窗口和
平台副作用。TEST1060 覆盖 ABI 的成功、取消、失败、reset 和注销；TEST118 在真实 WM6
页面上验证未取消的 ArrowDown 同时移动原生控件和 Core selection。

next613 在 native EDIT 的同一 bounded state 上增加
`PBrowser_ScriptSessionDispatchNativeEditComposition()`：browser layer 校验稳定 token 与
start/update/end phase，持有最后一段不超过 255 字节的 UTF-8 preedit，并同步派发
`compositionstart`、不可取消的 `beforeinput(insertCompositionText)` →
`compositionupdate` 以及 `compositionend`。START 的取消结果返回给宿主；更新后的
beforeinput metadata 会与既有 native commit → input 事务衔接。宿主仍负责 WM_IME、SIP、
原生文本 mutation 和平台副作用；TEST1061、TEST123–125 已在 WM6 设备门通过。next618
在宿主边界补齐 WinCE `GCS_RESULTSTR` 的完整候选落地，但没有改变该 ABI 或把 OEM 候选词
窗口、SIP 视觉体验伪装成 browser 兼容性保证；TEST1066 只验证可重复的多字节结果提交。

next619 在同一 bounded state 上增加
`PBrowser_ScriptSessionDispatchNativeEditResult()`：browser layer 校验稳定 token、活动
composition 和不超过 255 字节的借用 UTF-8 result，派发
`beforeinput(insertCompositionText)` → `compositionupdate` 并把 metadata 接入既有 native
commit → input 事务。宿主仍拥有 `ImmGetCompositionStringW`、`EM_REPLACESEL`、WM_IME/SIP
窗口和平台副作用；TEST1067 覆盖无效输入、容量、未开始/已结束 composition、完整 result、
native commit、reset 和 unregister。该 API 不宣称 OEM 候选条视觉或所有输入法兼容。

next620 在现有 typed input/select callback 上增加
`PBrowser_ScriptSessionDispatchNativeFileSelection()`：browser layer 保存最多 16 个文件
控件 token 的 BEGIN/COMMIT/CANCEL 状态，COMMIT 时固定派发一次
`input(insertFromFile)` → `change`，取消、重复提交、容量不足和 adapter 错误均有稳定边界。
宿主仍负责 WM6 系统 picker、文件系统权限和 `PCore_FileInputSetPath()`；文件路径不进入
公共 browser ABI。TEST1068 覆盖未注册、非法 phase、重复 begin、提交/取消、回调失败、
reset 和容量，TEST262 覆盖 programmatic picker 的实际消费者路径；TEST232/263 仍只需人工
确认真实系统对话框的视觉、选择和取消体验。

next621 在同一文件控件边界上增加
`PBrowser_ScriptSessionDispatchNativeFilePicker()`：每个脚本 session 只保留一个
pending/active picker request；重复 `file.click()` 合并为成功 no-op，OPEN/CLOSE/CANCEL
按稳定 token 校验生命周期，reset 在文档/session 销毁前清理状态。宿主仍负责
`PostMessage`、系统对话框、文件系统和路径，API 不携带 picker handle 或文件名。
TEST1069 覆盖非法输入、重复 request、错误 phase、reset 和多 session 隔离；TEST262
继续验证宿主实际队列接线。

next622 增加 `PBrowser_ScriptSessionDispatchAnchorClick()`：browser layer 对宿主命中的
受信任锚点先派发一次可取消 click，未被阻止时再以 ASSIGN 调用已注册导航适配器。
href 只在同步调用中借用，API 不拥有网络、窗口或文档；宿主仍负责 PCore_LinkAt、网络
请求和窗口替换。TEST1070 覆盖接受、preventDefault、导航拒绝、适配器错误和宿主 helper
接线。

next623 增加 `PBrowser_ScriptSessionDispatchNativeToggle()` 与 reset 入口，持有每个
session 最多 16 个 checkbox/radio stable token 的 CLICK/COMMIT/CANCEL 事务。browser layer
先派发一次可取消 click；宿主报告 Core 的 checked 状态已提交且确实变化后，DLL 才派发一次
不可取消的 `input` → `change`。禁用、preventDefault、取消、无状态变化、回调失败和 reset
均有明确边界；宿主仍拥有命中、Core mutation、原生 WM 默认动作、重绘和 label/窗口副作用。
TEST1071 覆盖接受、无变化、取消、禁用、回调错误、reset 和宿主 helper 接线。

next624 增加 `PBrowser_ScriptSessionDispatchNativeButton()` 与 reset 入口，持有每个
session 最多 16 个 submit/reset stable token 的 CLICK/COMMIT/CANCEL 事务。browser layer
先派发一次可取消 click；宿主在 click 回调之后查询 Core validation，再由 COMMIT 派发
submit 或 reset。禁用、preventDefault、无效校验、取消、kind mismatch、回调错误和容量
边界不放行默认动作；宿主仍拥有命中、Core validation/default action、导航、窗口、重绘和
label 副作用。TEST1072 覆盖产品契约、生命周期和宿主 helper 接线。
next625 扩展该入口支持 `PBROWSER_SCRIPT_NATIVE_BUTTON_BUTTON`（普通
`<button type="button">`）：CLICK 仍可取消，COMMIT 只返回普通按钮默认已接受，不派发
submit/reset；普通按钮不要求注册 form-event callback。宿主负责消费该默认动作，避免
generic click 后关闭窗口。TEST1073 覆盖该 kind 的事件、取消、禁用、非法输入和共享
session helper。

next626 复用同一入口承接宿主的 native button 键盘激活：宿主把焦点 button 的 typed
`keydown`/`keyup` 交给既有 browser key-event callback，并在 Enter/Space 的相应时机调用
CLICK→COMMIT。browser DLL 不新增 WM 或焦点所有权，继续负责 click/form 取消、事务状态和
事件顺序；普通按钮仍不产生 submit/reset。TEST1074 是宿主真实窗口消息级接线门，验证重复
键不会重复 trusted activation，取消和 form-event default policy 不泄漏。

next627 不新增 ABI，而是要求宿主在 label 命中 button 时复用同一
`PBrowser_ScriptSessionDispatchNativeButton()` CLICK→COMMIT 入口。browser layer 因而继续
统一 ordinary、submit、reset 的 click/form 取消与 disabled 策略；宿主只提供 label/control
命中、稳定 token、Core validation/default action 和窗口副作用。TEST1075 覆盖 label 自身
click 后的目标按钮事件顺序、reset 状态、取消和 disabled 静默；label 几何、触摸和焦点视觉
仍由消费者负责。
next628 不新增 ABI，而是要求宿主在 label 命中 checkbox/radio 时复用既有
`PBrowser_ScriptSessionDispatchNativeToggle()` CLICK→COMMIT 入口。browser layer 继续统一目标
click 的取消和一次 `input` → `change`；宿主只提供 label/control 命中、稳定 token、Core
checked/radio mutation、重绘和窗口副作用。TEST1076 覆盖 checkbox 提交、radio 互斥、目标
click `preventDefault` 和 disabled 静默；label 几何、触摸、焦点视觉以及 select/file/textarea
等其他 labelable 控件仍由消费者负责。

next629 不新增 ABI，而是补齐宿主对其余 native labelable 控件的 trusted target click 接线：
启用脚本时，label 命中 text/password/textarea/select/file 后先通过既有
`PBrowser_ScriptSessionDispatchClickEvent()` 派发目标的可取消 `click`；只有未取消时，宿主才
把焦点交给 native EDIT/SELECT，或继续进入系统 file picker。disabled/stale target 不合成目标
click；browser layer 仍只拥有事件取消/传播，WM 控件、Core focus、picker、路径和重绘仍由
消费者负责。TEST1077 在真实 native EDIT/SELECT 子窗口上覆盖 text/textarea/select 的事件、
焦点、取消和 disabled 闸门；文件 picker 的模态对话框与真实触摸仍需独立人工验收。

next630 扩展既有 `PBrowser_ScriptSessionRegisterProgrammaticClickCallbacksEx()` 的 target-kind
集合，新增 text、password、textarea 和 select 以及
`PBROWSER_SCRIPT_CLICK_DEFAULT_FOCUS`。浏览器层现在对这些脚本
`HTMLElement.click()` 目标统一执行 disabled 静默、typed `click` 和取消策略，再把
focus default-action 交给宿主；它不修改控件值，也不拥有 native HWND、select 下拉弹窗、
系统 picker 或 WM/OEM 副作用。新增常量保持现有 size-tagged callback 结构布局不变，旧
target-kind 的行为不变。消费者的 target callback 必须返回稳定的 DOM 几何/状态，click
callback 应按目标身份派发（不能依赖可能被 native child window 遮挡的坐标命中），default
callback 再执行真实控件焦点。TEST1078 在真实 render window 上覆盖 text/password/textarea/
select 的 `click`→`focus`→`focusin` 顺序、取消和 disabled 静默；select popup 展开仍是
宿主/OEM 的独立边界。

next631 在不改变上述 form-click callback ABI 的前提下增加了独立的
`PBrowser_ScriptSessionRegisterProgrammaticAnchorCallbacks()`。宿主以 DOM id 返回已布局
`<a href>` 的几何和 UTF-8 URL，browser layer 复用既有 `PBrowser_ScriptSessionDispatchAnchorClick()`
的 cancelable `click` → ASSIGN navigation 事务；`preventDefault()`、导航适配器拒绝和未知/无
`href` 元素都保持 fail-closed 或 generic click。Core 侧对应的
`PCore_LinkInfoById()` 只复制非空 `href`，不暴露 libdom/box 指针；网络、窗口替换和文档生命
周期仍由宿主负责。TEST1079 在真实设备脚本 session 上覆盖按 id 解析、一次 click、接受导航、
`preventDefault()`、缺失/容量 fail-closed 和无 href 的 generic 边界；TEST1070 继续覆盖
导航适配器拒绝。

next632 在同一锚点事务上补齐 fragment-only href 的产品导航分类：
`PBrowser_ScriptSessionDispatchAnchorClick()` 对以 `#` 开头的 href 提交
`PBROWSER_SCRIPT_NAVIGATION_FRAGMENT`，其余 href 继续提交 ASSIGN。browser DLL 仍只持有
click 取消和 typed navigation；宿主负责把片段 URL 绑定到当前页面、history/hashchange、
目标几何查询和视口滚动。`PCore_FragmentInfoById()` 只提供已布局、literal UTF-8 DOM id
的几何；未知目标不伪造失败或网络请求，`<a name>`、percent-decoding、target/rel/window
和跨文档导航策略仍未覆盖。TEST1080 覆盖 fragment/ASSIGN 分类、目标几何、URL 绑定和
未知目标保持位置。

next633 不改变 browser 公共 ABI，而是让宿主在 browser-owned history traversal 成功后复用
同一 fragment 目标查询：`history.back()`、`history.forward()` 和 `history.go()` 返回同文档
条目时恢复 viewport，未知目标保持位置。`positron_browser.dll` 仍只持有 history/event
事务与回调，不拥有滚动、网络或窗口替换；TEST1081 覆盖该宿主消费者边界。

next634 继续保持这个所有权边界：跨文档 history 的滚动偏移由宿主在自己的 bounded mirror
中保存，并在目标文档布局完成后恢复/按 viewport 上限裁剪；新条目默认为零。该逻辑不新增
`PBrowser_History*` 导出，也不把窗口、持久化或页面缓存状态带入 `positron_browser.dll`。
TEST1082 覆盖该消费者侧行为。

next637 在 anchor navigation ABI 上追加 `target_kind`，由 `positron_browser.dll` 对 raw
target 做 bounded ASCII keyword 分类。它不创建窗口，也不改变 raw target/rel 的借用寿命：
单窗口消费者可把 default、`_self`、`_parent`、`_top` 映射到当前 context，并对 `_blank`/
named 交给窗口管理器或 fail-closed。`test_host` 当前选择后者，防止尚未支持多窗口时误替换
当前文档；实际窗口复用、生命周期、跨窗口 history 和视觉仍是宿主后续边界。TEST1085
覆盖大小写/空白、所有分类、fragment 传播和单窗口拒绝策略。

next638 在该 ABI 上增加 `PBROWSER_SCRIPT_NAVIGATION_OPEN`。bootstrap 的
`window.open()` 只在宿主接受显式当前上下文 target 时返回同一 bounded global；DEFAULT、
`_blank`、不匹配的 named target 或空 URL 返回 `null`，不会静默发起当前页导航。features 不
产生窗口特性，真正的窗口 manager、opener/noopener、跨窗口 history 和视觉仍不在 DLL 边界
内。TEST1086 覆盖 callback metadata、注销后的 fail-closed 以及 test_host 的 current-target
admission。

next639 在 `PBrowserScriptNavigationInfo` 的兼容尾字段增加 `context_name`。对 OPEN 请求，
browser bootstrap 将当前 `window.name` 作为有界借用快照传给宿主；单窗口消费者可以只在
named target 与该快照精确匹配时复用当前 context。browser DLL 不保存跨 document 的窗口
管理状态、不创建第二个 global；名称恢复和 context 生命周期由宿主完成。未知 named、
`_blank` 和没有匹配 context 的请求仍应返回 `null`。TEST1087 覆盖快照传播、名称恢复和
单窗口 admission。

next640 的普通 anchor 不新增 browser ABI 字段：browser layer 继续把 raw target 与
`target_kind` 传给 navigation callback，宿主可在已有 session 上读取活动 `window.name`，仅对
精确匹配的 named target 复用当前 context，并把 ASSIGN/REPLACE/FRAGMENT 路由交给自己的
窗口/网络层。未知或空名称、`_blank` 仍由单窗口宿主 fail closed；异步消息边界的再次检查和
真实多窗口生命周期不属于 browser DLL。TEST1088 是该 host consumer policy 的回归门。

next641 沿用 next636 的 `HTMLElement.rel` 反射，为 `a`、`area`、`link`、`form` wrapper 增加
同一 `rel` 属性上的稳定 `relList`。wrapper 提供 `length`、`item()`、`value`、
`contains()`、`add()`、`remove()`、`toggle()`、`replace()`、`forEach()` 和 iterator；读取按
ASCII 大小写不敏感的 unique token 集合工作，mutation/value setter 立即反映到 `rel`，空 token
或含空白 token 抛出 `DOMException` `SyntaxError`。其他元素返回 `null`。这只是 DOMTokenList
反射，不实现完整 link-type processing、noopener/opener 窗口安全或窗口创建，也不扩展公共
C ABI。next642 又为同一 wrapper 增加 `supports()`：只有 `<link>` 上 Core 实际处理的
`stylesheet`（ASCII 大小写不敏感）返回 true，其他关系词和 `a`/`area`/`form` 返回 false；
空白 token 仍在 bootstrap 层抛出 `SyntaxError`。TEST1089/1090 是对应的自动产品/消费者门。

next644 在同一 bootstrap 中为 `<link>` 与 `<style>` wrapper 增加受限的 `media` UTF-8
属性反射：缺失值为空串，setter、`setAttribute()` 和 `removeAttribute()` 保持 live
一致，`null` setter 按普通 `String()` 规则处理；非 `link`/`style` 元素返回 `undefined`，
其 setter 不修改 raw attribute。该属性只反映 stylesheet metadata，不触发动态样式重排、
MediaQueryList 事件或 link 下载策略；TEST1092 与 TEST1090/1091/999 的 WM6 定向设备门
通过 4/4，未新增公共 C ABI。

next648 的 Core UA stylesheet 现在消费已有的 `HTMLElement.open` attribute reflection，提供
`details`/`dialog`/`summary` 的静态默认布局：closed details 隐藏非 summary 子项，closed
dialog 不生成布局盒，带 `open` 的控件恢复布局。browser DLL 仍只负责 open 属性读写；summary
点击、属性变化后的自动重排、模态焦点、backdrop 和 dialog 生命周期不在本批范围内。TEST1096
由 `test_host` 作为 Core 消费者验证，未新增 browser 公共 ABI。

next651 扩展既有 `PBrowser_ScriptSessionRegisterProgrammaticClickCallbacksEx()` typed
adapter，增加 `PBROWSER_SCRIPT_CLICK_TARGET_DISCLOSURE` 与
`PBROWSER_SCRIPT_CLICK_DEFAULT_DISCLOSURE`。宿主的 target callback 对已布局的首个直接
`summary` 返回 Core 几何；browser layer 仍统一派发可取消 `click`，只有未调用
`preventDefault()` 才向 default callback 交还 disclosure action。宿主随后调用
`PCore_DisclosureToggleById()` 并为活动渲染页安排既有 style/layout 重排；browser DLL 不
持有 DOM、窗口、绘制或 dialog 生命周期。物理点击可以复用同一 click 传播与 Core point
toggle，但真实触摸、键盘 summary 激活和 dialog modal/backdrop 仍是消费者边界。TEST1099
验证该程序化 target/default 接线和 `details.open` 状态。

next614 在同一 relation callback 上增加 bounded label/control 语义：`HTMLLabelElement.control`
处理非空 `for` 指向和无 `for` 时的第一个嵌套 labelable 控件；input（排除 hidden）、select、
textarea、button 的 `labels` 返回按文档顺序的静态 NodeList。无效 `for`、非控件、hidden、
无 ID label 和越界索引均 fail closed；不提供 live labels、节点创建或完整 labelable 集合。
TEST1062 与 554–561、1023–1053 相邻回归已在 WM6 设备门通过。

next615 的 fieldset disabled 语义由 `positron_core.dll` 统一计算并被 browser 的 validation
query 消费：disabled fieldset 的第一个 legend 后代豁免，其他后代和嵌套 fieldset 逐层继承；
动态切换会反映到 `willValidate`、`checkValidity()` 和 core 控件信息，而原始
`control.disabled` 仍只反映自身属性。TEST1063 与 264–270、554–561、1062 的回归设备门
通过；native invalid UI、窗口样式、SIP/IME 和文件选择器仍属于宿主边界。

## 其他项目如何调用

历史状态和脚本 session 是两个明确的 opaque 生命周期。脚本 session 的典型顺序是：

```c
#include "positron_browser.h"
#include "positron_script.h"  /* PSCRIPT_DEFAULT_BUDGET_MS */

HANDLE session;

session = PBrowser_ScriptSessionCreate(PSCRIPT_DEFAULT_BUDGET_MS);
if (session == NULL) {
    return 1;
}
/* 先注册调用者自己的 DOM/navigation/event/native callbacks。下面两个
 * 变量代表已填好的 PBrowserScript*Callbacks 结构体。 */
PBrowser_ScriptSessionRegisterDomReadCallbacks(session, &dom_read);
PBrowser_ScriptSessionRegisterValidationCallbacks(session, &validation);
PBrowser_ScriptSessionRegisterReportValidityCallbacks(session, &report_validity);
PBrowser_ScriptSessionRegisterCustomValidityCallbacks(session, &custom_validity);
PBrowser_ScriptSessionRegisterDomRelationCallbacks(session, &dom_relation);
PBrowser_ScriptSessionRegisterNavigationCallbacks(session, &navigation);
PBrowser_ScriptSessionRegisterProgrammaticAnchorCallbacks(session,
        &programmatic_anchor);
PBrowser_ScriptSessionEvaluateBootstrap(session);
PBrowser_ScriptSessionEvaluate(session, "document.title", -1);
/* PBrowser_ScriptSessionGetResult/GetError 返回借用字符串。 */
PBrowser_ScriptSessionDestroy(session);
```

对原生 SELECT，调用者先注册已有的 `PBrowserScriptFocusCallbacks` 和
`PBrowserScriptNativeSelectCallbacksEx`，在 WM 焦点通知后提交稳定 token 与文档几何：

```c
PBrowserScriptNativeSelectFocusInfo focus;

PBrowser_ScriptSessionRegisterFocusCallbacks(session, &focus_callbacks);
PBrowser_ScriptSessionRegisterNativeSelectCallbacksEx(session,
        &native_select_callbacks);
focus.size = sizeof(focus);
focus.target_token = select_token;  /* non-zero, stable while attached */
focus.x = select_x;
focus.y = select_y;
focus.focused = 1;                  /* 0 on kill-focus */
PBrowser_ScriptSessionDispatchNativeSelectFocus(session, &focus);
/* destroy/rebuild native controls before: */
PBrowser_ScriptSessionResetNativeSelectState(session);
```

`dispatch_focus` 同步收到 `focus`/`focusin` 或 `blur`/`focusout` 两个事件；重复状态不会
再次回调，失败返回后可用同一状态重试。WM 控件、Core selection mutation、下拉窗口和
SIP/IME 仍属于调用者。

键盘消息使用同一 session 的 typed key adapter；native SELECT 可用专用入口保留稳定 token
边界，并依据 `default_allowed` 决定是否把消息交给 WM 控件：

```c
PBrowserScriptNativeSelectKeyInfo key;
int default_allowed;

PBrowser_ScriptSessionRegisterKeyCallbacks(session, &key_callbacks);
key.size = sizeof(key);
key.target_token = select_token;
key.x = select_x;
key.y = select_y;
key.event_type = "keydown";       /* or "keyup" */
key.key = "ArrowDown";             /* or "Enter" */
key.key_code = 40;                  /* VK_DOWN */
key.char_code = 0;
key.repeat = 0;
key.shift = 0;
key.ctrl = 0;
key.alt = 0;
key.is_composing = 0;
if (PBrowser_ScriptSessionDispatchNativeSelectKey(session, &key,
        &default_allowed) == PSCRIPT_OK && default_allowed) {
    /* call the original WM COMBOBOX procedure/default action */
}
```

该入口只负责 browser-owned 事件 contract 和取消结果；调用者仍负责 WM 消息、Core selection
mutation、`input`/`change` commit、下拉窗口、SIP/IME 和平台副作用。

受信任的 submit/reset 原生按钮使用同一 session 的 bounded transaction。CLICK 返回允许后，
调用者重新查询 Core validation，再 COMMIT；取消、文档替换或默认动作未执行时发送 CANCEL：

```c
PBrowserScriptNativeButtonInfo button;
int default_allowed;

button.size = sizeof(button);
button.target_token = button_token; /* non-zero, stable while attached */
button.x = button_x;
button.y = button_y;
button.kind = PBROWSER_SCRIPT_NATIVE_BUTTON_SUBMIT;
button.disabled = 0;
button.validation_valid = 0;
button.phase = PBROWSER_SCRIPT_NATIVE_BUTTON_CLICK;
if (PBrowser_ScriptSessionDispatchNativeButton(session, &button,
        &default_allowed) == PSCRIPT_OK && default_allowed) {
    button.phase = PBROWSER_SCRIPT_NATIVE_BUTTON_COMMIT;
    button.validation_valid = core_validation_valid ? 1 : 0;
    if (PBrowser_ScriptSessionDispatchNativeButton(session, &button,
            &default_allowed) == PSCRIPT_OK && default_allowed) {
        /* perform Core submission/reset/default window action */
    } else {
        /* form event was cancelled or the adapter failed */
    }
} else {
    button.phase = PBROWSER_SCRIPT_NATIVE_BUTTON_CANCEL;
    PBrowser_ScriptSessionDispatchNativeButton(session, &button,
            &default_allowed);
}
```

`PBrowser_ScriptSessionResetNativeButtonState()` 清理控件重建前的 bounded token；Core
validation、普通按钮默认消费、默认提交/重置、导航、窗口和重绘仍由调用者拥有。

主要公共能力包括：

- `PBrowser_History*`：opaque history、同源判断、commit/replace、push/replaceState、
  back/forward/go 和同文档导航投影；
- `PBrowser_ScriptSession*`：创建/销毁浏览器专用 PScript context、求值、JSON global、bootstrap、
  DOM read/write/attribute/value/checked/form-property、navigation/location/history 事件、
  Event JSON 和 native input/keyboard/focus/EDIT change/post-change input/click、programmatic
  `HTMLElement.click()`、`HTMLElement.disabled`、按 id 的 DOM 关系/`children`/`contains()`/
  `HTMLLabelElement.control`/控件 `labels`、
  基础 `compareDocumentPosition()`/受限 `matches()`/`closest()`/元素作用域 querySelector、form
  owner 与 `form.elements` collection、attribute count/name/value、`getAttributeNames()`、
  `attributes`/`Attr`/受限 NamedNodeMap（`length`、`item()`、named lookup、`getNamedItemNS()`、
  `setNamedItemNS()`/`removeNamedItemNS()`、同 owner/跨 owner bounded mutation、Attr leaf metadata、元素
  `setAttributeNS()`/`removeAttributeNS()`/`setAttributeNodeNS()`、
  iterator、indexed 0–7）、控件与受限 form-level `checkValidity()`/`reportValidity()`/`willValidate`/`validity` 查询、`setCustomValidity()`/`validationMessage`、约束相关 `required`/`readOnly`/`multiple`/`noValidate`/
  `formNoValidate`/`min`/`max`/`step`、submit/reset/invalid/file-input/checkbox/radio input/change/SELECT input/change typed dispatch；
  bounded `childNodes` NodeList（文本/注释/id-less element wrapper、`item()`/iterator、`nodeType`/
  `nodeName`/`nodeValue`/`textContent`/CharacterData `data`/`length`/`substringData()`、父子/兄弟与
  element-sibling 视图、`Node` 常量）；`childNodes`、`children`、`form.elements` 和元素作用域
  `querySelectorAll()` 结果还提供有界 `forEach()`、`keys()`、`values()`、`entries()`、默认
  iterator 与 `Symbol.toStringTag`，其中 `children`/`form.elements` 保留 `namedItem()`；Node
  identity/root boundary 另外提供
  `isSameNode()`、受限 `isEqualNode()`、`getRootNode()`、`compareDocumentPosition()`、
  `contains()` 和 document-position 常量；文档结构入口还提供稳定的
  `document.documentElement`、`document.head`、`document.body`，并把它们接入同一
  parent/child/sibling、`children`/`childNodes`、identity/root/position/contains 和集合
  协议 snapshot；文档级 `querySelector()`/`querySelectorAll()` 现在提供受限的 tag、id、class、
  attribute、compound、`*` 和 `:root` 查询，按 DFS 顺序返回 NodeList，但不宣称通用 selector。
  core 通过三个保留结构 token 映射无 id 的结构节点，
  不伪造 HTML `id`，也不提供通用 DOM 创建、mutation 或 live collection；
- `PBrowser_ScriptSessionRunMicrotasks()`：在调用者自己的窗口/宿主循环中推进当前 session
  的 bounded microtask 队列并返回本次执行数量；Promise reaction 不会自行创建线程或隐式
  event loop。`RunIdleCallbacks()`、`RunMessages()` 同样由宿主按生命周期和关闭策略显式调用；
- `PBrowser_ScriptSessionRuntime` 仅是迁移期只读诊断借用句柄，不转移所有权。

回调结构体是 size-tagged，字符串和事件信息只在同步 callback 内借用。validation callback
按 DOM id 返回控件的 `valid`、`will_validate` 和 flags，或返回 form 的聚合 `valid`（此时
`will_validate=0`、flags=0）；custom-validity callback 按 DOM id 读写 UTF-8
application-owned message；getter 在没有 custom message 时可返回固定英文 validity fallback，
不做本地化。report-validity callback 只返回当前 valid 结果并派发可寻址控件的 trusted
`invalid` 事件；`preventDefault()` 不改变 boolean 结果，也不触发 native invalid UI、焦点/滚动
或提交。Ex programmatic-click callback 由产品层管理激活顺序，宿主只管理 document、窗口、
网络、控件几何/状态、default action、core 事件传播以及导航提交/回滚，并在 session 销毁前
注销或保证 callback `pw` 仍有效。

## 边界与验证

浏览器 JavaScript 仍由显式开关控制，默认 `javascript=0`。本 DLL 不是完整浏览器、
不是 URL Standard parser，也不应暴露 Duktape、libdom 或窗口对象。公共 ABI 变更必须
保持 UTF-8、opaque handle、明确所有权和 VS2008/ARMV4I 兼容；修改后运行正式构建、
脚本/设备门和相应人工门。

显式启用脚本时，bootstrap 还提供受限的 `dataset`/节点 metadata、FormData/Headers/Storage/
classList/relList/style iterator、Headers/URLSearchParams/FormData mutation-safe snapshots、
`TextEncoder.encodeInto()`、TextDecoder 选项快照、同步 Request/Response JSON/one-shot body 与
clone ownership、Blob/File metadata/slice/JSON、Headers `getSetCookie()`、URL authority
userinfo/default-port 与 URLSearchParams pair/delete-value/按值查询、cookie Max-Age 删除、
AbortSignal `timeout`/`any`/`onabort`/`abort`/tags、timer extra arguments/`setImmediate`、
MessagePort/BroadcastChannel（`onmessage` 自动 start）、structuredClone、Storage/HashChange/
PopState/Error/Progress/Close event 构造器、同步 PerformanceObserver/EntryList 快照与选项校验、
performance entry JSON、NodeList/HTMLCollection item/namedItem/forEach/keys/values/entries/iterator、
稳定 element/classList/relList/style wrapper identity、
DOM wrapper tags、navigator 方法、viewport 派生的 `screen.orientation`、window aliases、
受限的当前上下文 `window.open()` 和 `window.close()` no-op，以及由宿主显式 microtask pump 驱动的 bounded Promise（含 `then`/`catch`/`finally`、
`resolve`/`reject`、`all`/`race`/`allSettled`/`any`）。它们只在单个 session 内存中运行；
Request/Response 不联网，MessagePort/BroadcastChannel/timeout/Promise 需宿主显式 pump，
PerformanceObserver 只读取 observe 时已有 entries，不等于完整 DOM、fetch/stream、真实窗口
生命周期或后台浏览器调度。Promise handler 和组合器输入均限制为 64 项。公共 bootstrap 现在按
十三个顺序 IIFE 评估以保持脚本 source 上限；browser session heap ceiling 为 624 KiB，独立
`positron_script` context 的默认 heap 仍为 512 KiB。

DOM relation callback 是独立的 size-tagged ABI：调用者提供 `get_relation`，按元素 id 返回
UTF-8 字段或数量；label/control relation、attribute name/value 和 `CHILD_NODE_*` 字段也沿用同一 probe/truncation
contract。关系值是 session 内稳定 wrapper 的只读 snapshot；legacy `children`/form collection
仍按可寻址元素工作，而 `childNodes` 额外保留文本、注释和无 id 元素的直接子节点；集合遍历
方法只读取这些同步 snapshot。缺失 id、越界索引和不支持关系 fail closed。它不提供通用 DOM
mutation、节点创建、live collection、
shadow tree、复杂 CSS selector、namespace、layout 或 native control 查询；Node position 只沿
当前 bounded parent/child snapshot 计算，未知对象或跨快照关系返回 false/33；`isEqualNode()`
也不是完整 Web IDL 深结构相等，`form.elements`、NamedNodeMap 和 childNodes 也不是完整 live
Web IDL 集合。

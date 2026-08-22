# Positron 当前限制

更新时间：2026-08-22

这里只记录当前仍存在的产品或验收边界。已完成批次和设备流水不保留在本文件；最近证据见
[`HANDOFF.md`](HANDOFF.md)，稳定架构见
[`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

## 当前状态（next594）

next402–421 已把一组完整但受控的浏览器 JavaScript 子功能放入
`positron_browser.dll`：页面 readyState/visibility 生命周期和环境快照、有限 URL 与
URLSearchParams、session storage/cookie、classList/style、受限 selector 查询、FormData、
输入选择/数值步进/setRangeText、document/window metadata、viewport/scroll、合成事件，以及
由宿主显式推进的 timer、animation-frame 和 visibility 队列。next422–441 又补齐了事件
options/构造器/AbortController、microtask/idle/postMessage pump、base64/UTF-8 codec、受限
Blob/File/FormData 文件值、URL 静态 helpers/iterator、navigator/media/performance、history
  scrollRestoration 和 storage event。next442–461 又补齐了对象 listener、Event 生命周期、
  DOMException、dataset/节点常量、FormData iterator、受限 Headers、同步 Request/Response、
  AbortSignal timeout/any/onabort、timer 参数/setImmediate、MessageChannel、structuredClone、
  navigator 方法、screen.orientation 和 URLSearchParams pair/delete-value。next462–481 又补齐了
  encodeInto/decoder 选项、同步 JSON body readers、Storage named/toJSON、DOM/style 迭代器、
  toggleAttribute/ownership、事件构造器、MessagePort/BroadcastChannel、PerformanceObserver、
  window aliases/open-close 边界和 AbortSignal.abort。next482–501 又增加了 Blob/File 元数据与
  slice 边界、FormData snapshot iterator、Request/Response one-shot body、URL authority/default
  port、URLSearchParams 按值 has、cookie Max-Age 删除、NodeList/element identity、dataset、Event
  constants/dispatch reset、MessagePort/BroadcastChannel 状态和 PerformanceObserverEntryList
  视图。next502–521 又补齐 Headers/Request/Response ownership 与 metadata JSON、
URLSearchParams/FormData mutation-safe snapshot、Storage/DOM wrapper tags、classList token
validation、performance entry/observer option metadata、MessagePort auto-start、AbortSignal/
Controller tags 和 Blob/File JSON metadata。next522–541 又补齐了受宿主显式 pump 驱动的 bounded
Promise 构造器、then/catch/finally、thenable assimilation、组合器和错误/容量边界。next542–561
又补齐了按 DOM id 的受控树关系、基础 selector ancestor 查询、form owner 与
`form.elements` collection 视图。TEST389–448、TEST482–621 与 TEST502–521 定向门均已通过；
这些切片默认关闭 JavaScript 时不会被发现、抓取或执行。next562–581 又增加了按 DOM id 的
attribute count/name/value relation，以及 `getAttributeNames()`、`hasAttributes()`、受限
`NamedNodeMap`/`Attr` wrapper 和跨 owner fail-closed mutation；attribute map 的 indexed
properties 固定为 0–7，仍不提供完整 namespace API 或通用 DOM mutation。next562–581 时产品
bootstrap 由公共入口按十个顺序 IIFE 评估，共享一个 Duktape context；浏览器 session 的 heap ceiling 为 576 KiB，
独立 `positron_script` context 的 512 KiB 默认值不变。分段只用于保持源码上限，不引入第二套
JavaScript 引擎。

next582 在同一 relation bridge 上增加了有界 childNodes/CharacterData snapshot：
`PCore_NodeRelationById()` 现在可按直接子节点索引返回类型、name/value/textContent 和可用
element id；browser bootstrap 提供 `childNodes` NodeList、文本/注释/id-less element wrapper、
父子/兄弟及 element-sibling 视图、`Node` 常量和只读 `data`/`length`/`substringData()`。
这组 wrapper 仍是同步、session-scoped、只读 snapshot；有 id 的元素复用既有 wrapper，其他
节点仅由 owner+index 的内部 token 表示，不提供文本节点 mutation、节点创建、live collection、
shadow tree、layout 或 native control。`TEST582–601,999` 定向门与
`TEST389,390–448,482–601,999` 相邻回归门均通过；bootstrap 现在为十一个 IIFE，browser
heap ceiling 仍为 576 KiB，独立 script 默认堆仍为 512 KiB。

next583 在不改动 core relation ABI 的前提下增加了 bounded Node identity/root/position view：
element、文本、注释、无 id element wrapper 和 document 支持 `isSameNode()`、受限
`isEqualNode()`、`getRootNode()`、`compareDocumentPosition()` 与 `contains()`，并补充
document-position 常量和 document 的 parent/owner/value/connection metadata。位置查询只沿当前
ID-addressable parent 与 childNodes snapshot 计算；未知对象、跨快照和缺失 parent 链 fail closed，
不提供完整 document order、shadow tree、节点创建、mutation 或 live collection。`TEST602–621,999`
定向门与 `TEST389,390–448,482–621,999` 相邻回归门均通过；bootstrap 现在为十二个 IIFE，
browser heap ceiling 仍为 576 KiB，独立 script 默认堆仍为 512 KiB。

next584 在不改动 core relation ABI 的前提下，为既有 DOM 集合 snapshot 补齐了有界迭代协议：
`childNodes`、`children`、`form.elements` 和元素作用域 `querySelectorAll()` 结果提供
`forEach()`、`keys()`、`values()`、`entries()`、可复用默认迭代器与 `Symbol.toStringTag`；
`children`/`form.elements` 仍保留 `namedItem()`，元素作用域查询返回 `NodeList` 类型标识。
这些集合仍是同步、session-scoped、只读的有限快照，不是 live collection，也没有新增节点创建、
通用 mutation、复杂 selector 或 layout 语义。`TEST622–641,999` 定向门和
`TEST389,390–448,482–641,999` 相邻回归门均通过；bootstrap 现在为十三个 IIFE，browser
heap ceiling 仍为 576 KiB，独立 script 默认堆仍为 512 KiB。

next585 在既有 relation bridge 上增加了三个保留结构 token，将没有 HTML `id` 的 document root、
直接 `head` 和直接 `body` 接入同一 bounded snapshot。`positron_browser.dll` 暴露稳定的
`document.documentElement`、`document.head`、`document.body` wrapper，并复用既有
parent/child/sibling、`children`/`childNodes`、root selector、identity/root/position/contains
和集合协议；`documentElement.parentNode` 指向 document，`parentElement` 为空。真实 id 查找
优先于 token fallback，结构 wrapper 不伪造 `id`；next585 当时的文档级 selector 只新增
`html`、`:root`、`head`、`body` 四种结构查询，复杂 selector 仍 fail closed。由于 root parent 现在可寻址，
同一 body snapshot 中原先 disconnected 的 ID 子树可排序，TEST549 的 root/form 断言相应
更新为顺序值 `4`。`TEST642–661,999` 定向门与 `TEST549,642–661,999` 兼容重跑已通过；
最终相邻回归 `TEST389,390-448,482-661,999` 在
`tmp/device-runs/20260821-175025-next585-regression-r3/` 通过 241/241。bootstrap 仍为十三个 IIFE，browser heap ceiling 仍为 576 KiB，
独立 script 默认堆仍为 512 KiB。

next586 在不改动 core relation ABI 的前提下增加了 browser-owned 的 `DocumentType` synthetic
snapshot。`document.doctype` 是稳定、只读、session-scoped singleton，提供 `name`、`nodeType`、
`nodeName`、owner/root/position/contains、identity/equality 和字符串 brand；它没有子节点，
document 的 `childNodes` 顺序为 `[doctype, documentElement]`，而 `document.children` 仍只返回
`documentElement`。该切片不提供通用 doctype parser、public doctype token、节点创建、mutation、
live collection 或完整 document tree。`TEST662–681,999` 定向门、`TEST549,642–681,999` 兼容门
和 `TEST389,390–448,482–681,999` 相邻回归分别通过 21/21、42/42、261/261；bootstrap 仍为
十三个 IIFE，browser heap ceiling 仍为 576 KiB，独立 script 默认堆仍为 512 KiB。

next587 在不改动 core relation ABI 的前提下，为 document、DocumentType、HTML element、
CharacterData 和 Attr wrapper 增加受控的 `baseURI`、`namespaceURI`、`prefix`、
`lookupNamespaceURI()` 与 `isDefaultNamespace()` 元数据。`baseURI` 只反映当前 session URL，
并随受控 history replacement 更新；HTML element 返回 HTML namespace，document、doctype、
文本/注释和普通 Attr 的 namespace 视图为 null，只有 `xml` prefix 映射到 XML namespace，
未知 prefix 或不支持的 namespace 请求 fail closed。Attr 的 namespace 查询沿 owner element
上下文工作。该切片不提供 XML/namespace parser、prefix mutation、节点创建或通用 DOM mutation；
`TEST682–701,999` 定向门、`TEST549,642–701,999` 兼容门和
`TEST389,390–448,482–701,999` 相邻回归分别通过 21/21、62/62、281/281。bootstrap 仍为
十三个 IIFE，browser heap ceiling 仍为 576 KiB，独立 script 默认堆仍为 512 KiB。

next588 在不改动 core relation ABI 的前提下，为 document 和 HTML element wrapper 增加
`getElementsByTagName()` 与 `getElementsByClassName()`。tag 查询支持 HTML 大小写归一和 `*`，
class 查询按规范化后的多个 token 做合取；结果按当前 bounded snapshot 的深度优先文档顺序返回，
document 查询包含 structural `documentElement`，element 查询不包含 owner。结果是静态
HTMLCollection snapshot，提供 `item()`/`namedItem()`、`forEach()`、`keys()`、`values()`、
`entries()`、默认 iterator 和 `Symbol.toStringTag`；空白/未知输入 fail closed，不提供 live 更新、
通用 selector、mutation、namespace/tree 扩展或新的 core ABI。`TEST702–721,999`、
`TEST549,642–721,999`、`TEST389,390–448,482–721,999` 分别通过 21/21、82/82、301/301，
证据位于 `tmp/device-runs/20260822-152000-next588-final/`、
`tmp/device-runs/20260822-152109-next588-compat-final/` 和
`tmp/device-runs/20260822-152431-next588-regression-final/`。新增 bootstrap 的容量在 576 KiB
上稳定复现 TEST540 内存上限后，browser session ceiling 提高到 608 KiB；
`tmp/device-runs/20260822-151937-next588-540-r3/` 的 TEST540/999 2/2 通过，独立
`positron_script` 默认堆仍为 512 KiB。

next589 在不改动 core relation ABI 的前提下，把既有受限 selector matcher 接到 document
作用域：`document.querySelector()`/`querySelectorAll()` 沿当前 bounded snapshot 支持
tag、`#id`、class、有限 attribute、compound、`*` 和 `:root`，按 DFS 文档顺序返回静态
NodeList；root/head/body wrapper 复用既有 identity。空白、缺失和包含 `>`、`+`、`~` 的组合器
仍 fail closed，不提供完整 CSS selector、live 更新、节点创建或 mutation。`TEST722–741,999`、
`TEST549,642–741,999`、`TEST389,390–448,482–741,999` 分别通过 21/21、102/102、321/321，
证据位于 `tmp/device-runs/20260822-155230-next589-r5/`、
`tmp/device-runs/20260822-155506-next589-compat-r2/` 和
`tmp/device-runs/20260822-160014-next589-regression/`。历史 TEST658 负例已改为验证组合器
仍然 fail closed；本批不涉及视觉、触摸、SIP、picker、旋转或网络失败，因此不新增人工页面验收。

next590 在不改动 core relation ABI 的前提下，把同一 bounded traversal 扩展为 document named
collection projection：`document.getElementsByName()` 精确匹配显式 `name` 值并返回 DFS 顺序的
NodeList snapshot；`document.forms`、`document.images` 和 `document.scripts` 返回相应静态
HTMLCollection，并复用 `item()`/`namedItem()`、迭代器和 wrapper identity。结果只覆盖当前
ID-addressable session snapshot，不提供 live 更新、节点创建、通用 mutation、完整 named
properties 或新的 core ABI。`TEST742–761,999`、`TEST549,642–761,999`、
`TEST389,390–448,482–761,999` 分别通过 21/21、122/122、341/341，证据位于
`tmp/device-runs/20260822-163511-next590-r1/`、
`tmp/device-runs/20260822-163636-next590-compat-r1/` 和
`tmp/device-runs/20260822-164253-next590-regression-r1/`。本批只涉及同步脚本 API/DOM
snapshot，不涉及视觉、触摸、SIP、picker、旋转或网络失败，因此不新增人工页面验收。

next591 在不改动 core relation ABI 的前提下增加了 document hyperlink collection projection：
`document.links` 按 DFS 顺序收集显式 `href` 的 `a`/`area`，`document.anchors` 按 DFS 顺序收集
显式 `name` 的 `a`，两者都返回静态 HTMLCollection，复用 `item()`/`namedItem()`、迭代器和
wrapper identity。属性增删只影响后续查询，不提供 live 更新、通用 named properties、节点
创建、通用 mutation、URL 解析或新的 core ABI。`TEST762–781,999`、`TEST549,642–781,999`、
`TEST389,390–448,540,549,642–781,999` 分别通过 21/21、142/142、203/203，证据位于
`tmp/device-runs/20260822-171429-next591-r1/`、
`tmp/device-runs/20260822-171552-next591-compat-r1/` 和
`tmp/device-runs/20260822-172242-next591-regression-r1/`。本批采用缩减回归而非旧的 341 项
全回归；保留核心事件、TEST540 内存边界、TEST549 和 next642–781 风险区间。本批不涉及视觉、
触摸、SIP、picker、旋转或网络失败，因此不新增人工页面验收。

next592 在不改动 core relation ABI 的前提下增加了 document/element 的
`getElementsByTagNameNS(namespace, localName)`：`*` namespace 或精确 namespace 字符串与
`*`/大小写敏感 localName 组合筛选当前 HTML element snapshot；document 查询包含
`documentElement`，element 查询排除 owner。结果为静态 HTMLCollection，复用
`item()`/`namedItem()`、`forEach()`、`keys()`、`values()`、`entries()`、默认 iterator 和
wrapper identity；null、空 namespace、未知 namespace/localName 与空 localName fail closed。
该切片不提供 XML/SVG namespace parser、prefix mutation、live collection、节点创建、通用
mutation 或新的 core ABI。`TEST782–801,999`、`TEST549,642–801,999`、
`TEST389,390–448,540,549,642–801,999` 分别通过 21/21、162/162、223/223，证据位于
`tmp/device-runs/20260822-192042-next592-r2/`、
`tmp/device-runs/20260822-192203-next592-compat-r1/` 和
`tmp/device-runs/20260822-192923-next592-regression-r1/`。本批只涉及同步脚本 API/DOM
snapshot，不涉及视觉、触摸、SIP、picker、旋转或网络失败，因此不新增人工页面验收。

next593 在不改动 core relation ABI 的前提下增加了只读 namespace-aware attribute projection：
`getAttributeNS()`、`hasAttributeNS()`、`getAttributeNodeNS()` 和 Attr 的
`namespaceURI`/`prefix`/`localName` 元数据。null 与空 namespace 表示无 namespace；`xml` 与
`xmlns` 前缀分别映射 XML/XMLNS namespace，未知前缀和未知 namespace fail closed；返回的 Attr
仍通过同 owner wrapper 读取/写回 value/nodeValue。该切片不提供 `setAttributeNS()`、
`removeAttributeNS()`、XML/SVG parser、prefix mutation、节点创建、live collection 或新的
core ABI。`TEST802–821,999`、`TEST549,642–821,999`、
`TEST389,390–448,540,549,642–821,999` 分别通过 21/21、182/182、243/243，证据位于
`tmp/device-runs/20260822-201726-next593-r2/`、
`tmp/device-runs/20260822-201838-next593-compat-r2/` 和
`tmp/device-runs/20260822-202712-next593-regression-r2/`。本批只涉及同步脚本 API/DOM
snapshot，不涉及视觉、触摸、SIP、picker、旋转或网络失败，因此不新增人工页面验收。

next594 在不改动 core relation ABI 的前提下，为既有 `NamedNodeMap` 增加只读的
`getNamedItemNS(namespace, localName)`。它复用 next593 的 namespace/Attr 语义：null/空
namespace 表示无 namespace，`xml`/`xmlns` 映射已知 XML/XMLNS namespace，未知输入 fail closed，
localName 大小写敏感并接受 String coercion；同一 map 继续观察属性增删和值更新。该切片不提供
`setNamedItemNS()`、`removeNamedItemNS()`、XML/SVG parser、namespace mutation、节点创建、live
collection 或新的 core ABI。`TEST822–841,999` 与
`TEST389,390–448,540,549,642–841,999` 分别通过 21/21、263/263，证据位于
`tmp/device-runs/20260822-204905-next594-r1/` 和
`tmp/device-runs/20260822-205012-next594-regression-r1/`；兼容子集沿用 next593 的
`TEST549,642–821,999` 证据。本批只涉及同步脚本 API/DOM snapshot，不涉及视觉、触摸、SIP、
picker、旋转或网络失败，因此不新增人工页面验收。

这些 API 的共同限制如下：

- 所有状态都属于单个脚本 session，保存在内存中；storage/cookie 没有持久化、域/路径安全策略、
  配额或跨 session 同步，FormData 也不连接 Blob/File/multipart 传输。
- selector 只支持当前实现声明的 `#id`、`.class`、有限 attribute、tag/compound 组合和 `*`；
  `matches()`、`closest()`、元素作用域 `querySelector()`/`querySelectorAll()` 只在当前受限匹配器
  上工作，不提供通用 CSS selector、布局命中测试或完整动态 DOM 语义。
- URL 的 userinfo、默认端口和 URLSearchParams 按值查询只覆盖当前 bounded parser 的 authority
  形态；不提供完整 URL Standard、IPv6/转义异常和 origin 安全策略。NodeList/HTMLCollection 的
  `item()`、`namedItem()`、`forEach()`、`keys()`、`values()`、`entries()` 和 iterator 只作用于
  当前 document 的同步 snapshot；与 repeated `getElementById()` identity 一样，不创建通用 DOM tree，
  也不提供 live 更新；next588 的 `getElementsBy*()` 与 next590 的 named collection projection
  同样只返回当前 session 的静态快照；next591 的 `links`/`anchors` 与 next592 的 namespace
  collection 也只过滤当前快照，next593 的 namespace attribute lookup 与 next594 的
  `NamedNodeMap.getNamedItemNS()` 也只读取当前 owner 的属性快照。
- selection、numeric step、setRangeText 是产品 bridge 的逻辑状态，不等于 WM native EDIT 的
  光标、SIP、IME composition、候选词、Unicode preedit 或原生文本选择 UI。
- document/window metadata、viewport、scroll 是脚本可见的受控快照；它们不自动改变真实窗口、
  layout、paint、滚动条或 DPI。synthetic event 只在产品注册表中按目标分发，不替代 native/WM
  冒泡与默认行为。
- timer、animation frame 和 visibility 都需要宿主显式调用公共 pump/dispatch API；没有后台线程、
  OS lifecycle 自动接线或跨导航持久队列。计时器行为受 session 预算和 pump 时刻限制，关闭/导航
  时应由宿主丢弃队列。
- 事件 options、EventTarget、CustomEvent、AbortController 和 handler 属性只覆盖产品注册表内
  的受控分发；没有完整 DOM 冒泡/捕获树、默认动作或 native event retargeting。microtask、idle
  和 postMessage 是 session 内、有限容量、宿主显式 pump 的队列，不等于浏览器线程调度。
- Event 静态 phase 常量、构造 timestamp 和 dispatch 后 state reset 只保证产品 synthetic event
  contract；MessagePort/BroadcastChannel 的 started/closed/messageerror 仍限于同一 session、
  有界 message pump，不代表 native port 或跨页面通信。
- TextEncoder/TextDecoder、atob/btoa、Blob/File/FormData 文件值是 UTF-8/内存 bounded 适配；
  Blob 的 `text()`/`arrayBuffer()` 仍同步返回，未实现 fetch、stream、multipart 传输或持久
  文件句柄。
- `dataset` 只通过现有按 id attribute bridge 反射 `data-*` 名称，legacy 节点关系和 tag/name 只通过
  next542–561 的 ID-addressable snapshot 暴露；next582 的 `childNodes` 另提供直接文本/注释/
  无 id 元素 snapshot。仍没有通用 `createElement()`/mutation、outerHTML、
  完整属性枚举或 layout 语义。`children`、兄弟/父子 wrapper、`contains()`、基础
  `compareDocumentPosition()`、`form` 和 `form.elements` 只对当前 document fixture 的可寻址节点
  工作；next585 的结构 token 只覆盖 document root、直接 head/body，不扩展到任意无 id 后代；
  next586 的 doctype 是 browser-owned synthetic leaf，不代表 core parser token，也不提供
  doctype metadata mutation 或通用 document child creation；
  collection 的 `item()`/`namedItem()` 是有序、有限的同步视图，不是 live HTML DOM。
  `keys()`/`toJSON()` 只报告当前 session 触碰过的 named keys。FormData 的 `Symbol.iterator`、`entries()`/`keys()`/`values()`
  是有序 session snapshot，保留旧 iterator `.length` 兼容字段，不提供 multipart 或异步流。
- `getAttributeNames()` 与 `Attr`/`NamedNodeMap` 只覆盖当前 ID-addressable element 的 parser-order
  attribute snapshot；`Attr.value`/`nodeValue` 通过既有 attribute bridge 做同步内存 mutation，
  `setNamedItem()`/`removeNamedItem()` 只接受同 owner wrapper，跨 owner 或缺失项 fail closed。
  Indexed access 只保证 0–7；next593 的 element namespace lookup 与 next594 的
  `getNamedItemNS()` 只提供同步读 API 和已知 `xml`/`xmlns` 元数据，不实现 namespace
  mutation、XML/SVG parser、通用节点创建、live collection 或完整 Web IDL descriptor 语义。
  Attr 的 namespace/localName/prefix 仍只在当前 owner 上下文中有效。
- Headers、Request、Response 是内存 bounded 的同步数据模型；它们不建立网络连接、不执行 fetch、
  不提供 stream，body `text()`/`json()`/`arrayBuffer()` 是 one-shot 消费并同步标记
  `bodyUsed`；clone 在已消费后受控抛出 TypeError。Headers 受条目和值数量限制，非法名称和超限
  以受控异常失败。
- `AbortSignal.timeout/any/onabort`、setImmediate 和 MessageChannel 都依赖宿主显式 timer/message
  pump；没有后台线程、跨页面通信或导航后队列保留。`structuredClone` 只克隆受限 primitive/array/
  plain object/Blob/File，深度和对象数量受脚本预算影响，循环/函数等不可克隆值会抛出
  `DOMException('DataCloneError')`。
- Promise 是产品 bootstrap 提供的 bounded polyfill：构造器、`then`/`catch`/`finally`、
  `resolve`/`reject`、`all`/`race`/`allSettled`/`any` 只在单个脚本 session 内工作；反应队列
  必须由宿主显式调用 `PBrowser_ScriptSessionRunMicrotasks()` 推进（测试 probe 使用其 bootstrap
  helper `__pcoreRunMicrotasks()`），不创建后台线程或隐式 event loop。每个
  Promise 最多保留 64 个 handler，组合器输入最多 64 项；输入超限、非法构造器和全拒绝的
  `any` 会受控拒绝。它不连接 fetch、stream、网络、文件句柄或跨 session 调度。
- navigator 新增的 `javaEnabled()`/`sendBeacon()` 只是冻结能力快照（当前返回 false），
  `screen.orientation` 只在 session 初始化时由 viewport 派生，不监听旋转、不驱动重排或绘制；
  URLSearchParams pair constructor/delete(value) 仍是受限字符串实现。
- `matchMedia` 使用初始化 viewport/DPI 快照，不会监听窗口重排；performance 只保留 session 内
  mark/measure 条目；PerformanceObserverEntryList 只提供 observe 时的同步 indexed/iterable
  snapshot，`clearResourceTimings()` 不会清理真实网络资源；navigator 是冻结能力快照；storage
  event 为单 session 内的受控通知，不提供跨页面/跨进程持久化同步。
- `TextEncoder.encodeInto()` 只在给定的 typed-array 容量内同步写入并返回 bounded progress；
  `TextDecoder` 的 `fatal`/`ignoreBOM` 目前是构造时选项快照，未扩展完整流式解码或所有编码错误
  策略。Request/Response 的 `json()` 同步消费 body，Blob-backed Request clone 仍是内存对象，
  不建立文件句柄、网络或 Promise 链。
- Storage named properties 只通过 session 内 Proxy 映射到 `getItem`/`setItem`/`removeItem`，
  `toJSON()` 返回一次性 detached snapshot；不提供持久化、跨窗口同步或完整 Web IDL property
  enumeration。Storage 的 tag 只在当前 Proxy get path 中保证；classList/style wrapper 在 session
  内保持 identity，DOM 仍没有通用 createElement/tree API。classList token validation 仅覆盖当前
  `DOMTokenList` wrapper 的空白/空 token 失败边界。
- StorageEvent、HashChangeEvent、PopStateEvent、ErrorEvent、ProgressEvent、CloseEvent 只提供
  构造器字段和产品事件基类；MessagePort 的 `messageerror`、close 与 BroadcastChannel 仅在同一
  script session、有限 message pump 中工作，跨页面/跨进程、transferable 和后台通信不在范围。
- PerformanceObserver 在 `observe()` 时对已有 performance entries 做同步快照并通过
  `takeRecords()` 读取，不监听未来异步条目；`supportedEntryTypes` 只声明 `mark`/`measure`，
  `type` 与 `entryTypes` 冲突或空数组会同步抛出 TypeError；window `self/top/parent/frames/defaultView` 是同一
  bounded global 的别名，`open()` 返回 null、`close()` 不关闭真实窗口，`closed/length` 只为稳定
  no-op 状态。Request/Response clone 只复制内存 body、headers 和 bounded metadata；`toJSON()`
  不建立网络或 stream。`AbortSignal.abort()`/`throwIfAborted()` 只覆盖同步 reason 传播，
  `onmessage` 自动 start 也只作用于 session 内 MessagePort pump。
- `Blob.prototype.toJSON()`、`File.prototype.toJSON()`、Request/Response metadata JSON 和
  performance entry `toJSON()` 都是 Positron 为诊断/页面脚本提供的 bounded snapshot，不是完整
  Web IDL serialization；Blob/File readers 仍无 Promise-backed stream、持久文件句柄或 multipart
  transport。
- `PCore_NodeRelationById()` 对 legacy 关系只返回 ID-addressable 元素的 UTF-8 字段或计数；
  next582 的 `CHILD_NODE_*` 关系另按直接 child index 返回文本、注释和无 id 元素的 bounded
  字段。缺失 id、越界索引、非 form 控件的 form 查询和不支持的关系会 fail closed。它不提供
  完整 Node API、节点创建或动态 mutation、shadow tree、label/fieldset 关联、layout 或 native
  control 状态；`form.elements` 和 childNodes 也不承诺完整 live collection 更新。
- `scripts\device_gate.bat -EnableJavaScript` 只修改隔离 staging；tracked
  `test_host/test_host.ini` 仍为 `javascript=0`。本轮只改产品 API/状态，没有新增视觉、触摸、
  SIP 或系统 picker 人工门；这些风险仍须按下方验收边界单独检查。

## 浏览器 JavaScript

当前状态：

- 默认 `javascript=0`。
- `positron_browser.dll` 已拥有独立 history/session 产品层、PScript context、host JSON callback
  的 session 注册/调用生命周期、产品 bootstrap 文本和求值入口，以及 DOM 只读（按 id 查询与
  textContent 读取）、textContent 写入、attribute、input value、checked、`HTMLElement.disabled`、表单属性
  `name`/form `action`/`method`/`enctype`/`target`/`autocomplete`/`acceptCharset`、submitter `formAction`/`formMethod`/`formEnctype` 与约束相关 `required`/`readOnly`/`multiple`/`noValidate`/`formNoValidate`/`min`/`max`/`step`/`pattern`/`minLength`/`maxLength` 反射、form property
  （defaultValue/defaultChecked/selectedIndex）、navigation、同文档 location/history 事件分发、event JSON 分发、native input/composition、keyboard、focus-family、EDIT change/post-change input、click、programmatic `HTMLElement.click()`（file input 只到 typed click）、submit/reset、invalid、reportValidity、file-input input/change、checkbox/radio input/change 和 SELECT input/change typed dispatch entry；显式开启时仍有 classic inline/external
  script、页面 context，以及一套尚在宿主迁移中的其余 form/input bridge；导航的窗口、网络、core
  事件传播和 history side effect 仍由宿主 typed adapter 提供；
  Event callback 的产品 JSON 分发已迁入，但 core/document typed listener 适配仍由宿主提供。

- next542–561 新增了独立的 DOM-relation callback 和 core `PCore_NodeRelationById()`：脚本可在
  同一 session 内通过元素 id 读取 parent/child/sibling、tag/name、`children`、`contains()`、
  基础 `compareDocumentPosition()`、受限 `matches()`/`closest()`、作用域 querySelector，以及
  form owner/`form.elements` 的有序 `item()`/`namedItem()`。这些关系是同步只读 snapshot；不提供
  通用 DOM mutation、shadow tree、复杂 CSS selector、live collection、layout 或 native control
  查询；legacy element-only collection 仍跳过无 id 元素和文本节点。next582 的 `childNodes`
  snapshot 只补齐直接文本/注释/无 id 元素的读取，不改变这些限制。

- next562–581 在同一 relation bridge 上增加了 attribute count/name/value，并由 browser bootstrap
  暴露 `getAttributeNames()`、`hasAttributes()`、受限 `NamedNodeMap` 和 `Attr`。属性名保持 parser
  顺序；`Attr.value`/`nodeValue` 复用已有同步 attribute mutation，map/Attr identity 在 session
  内稳定，`setNamedItem()`/`removeNamedItem()` 对跨 owner 或缺失项 fail closed。为了适配 WM6
  脚本堆，indexed access 只保证 0–7；NamedNodeMap 仍不提供 namespace/localName/prefix lookup、
  通用节点创建、live collection 或完整 Web IDL descriptor 语义。bootstrap 目前为十个 IIFE，
  browser session heap ceiling 为 576 KiB，独立 `positron_script` 默认堆仍为 512 KiB。

- next587 又把 browser-owned Node metadata 统一到 document、DocumentType、HTML element、
  CharacterData 和 Attr wrapper：`baseURI` 读取当前 session URL，`namespaceURI` 只承诺 HTML/XML
  的有限视图，`prefix` 只读且恒为 null，`lookupNamespaceURI()`/`isDefaultNamespace()` 对未知值
  fail closed，Attr 查询沿 owner element 上下文工作。它不解析 XML namespace、不支持 prefix 或
  namespace mutation，也不改变 core ABI、document tree 或节点创建边界；`TEST682–701` 只验证
  同步、内存内的脚本 API。该切片的 bootstrap 为十三个 IIFE，browser session heap ceiling 为
  576 KiB，独立 script 默认堆仍为 512 KiB。

- next588 在上述 snapshot 上增加 document/element 的 `getElementsByTagName()` 与
  `getElementsByClassName()`，形成有限 HTMLCollection 查询族。它只沿当前有界 relation 读取，
  按 DFS 文档顺序生成静态结果，支持大小写不敏感 tag、通配符、规范化多 class token、
  `item()`/`namedItem()` 和集合迭代协议；owner 自身排除，document root 明确包含，未知/空白
  输入 fail closed。它不是 live collection、通用 CSS selector 或 mutation API；
  `TEST702–721` 只验证同步内存语义。为容纳这组 bootstrap，browser session ceiling 现为
  608 KiB，独立 `positron_script` 默认堆仍为 512 KiB；TEST540 在该上限下保持通过。

- next589 把同一 bounded matcher 接入 document 作用域：`querySelector()` 返回首个 DFS
  匹配 wrapper，`querySelectorAll()` 返回 NodeList snapshot，支持已声明的 tag/class/id/
  attribute/compound/`*`/`:root` 形态，并保留 root/head/body identity；空白和不支持组合器
  fail closed。它不扩展 core ABI，不提供通用 CSS parser、live collection、节点创建或 mutation；
  `TEST722–741` 覆盖顺序、identity、NodeList 迭代、快照和负例。

- next298 新增了独立的 validation query callback 和 bootstrap 的
  `HTMLElement.checkValidity()`、`willValidate`、`validity`（基础 flags）查询。它按 DOM id
  读取 core 的当前控件状态，覆盖 required、disabled、readonly、select 和已有的数值约束；
  非候选元素返回 `valid=true`、`willValidate=false`。这是查询型兼容切片，不触发 invalid 事件，
  不实现 form `checkValidity()`、`validationMessage`、`setCustomValidity()` 的 DOM 方法或
  native invalid UI。

- next299 新增了独立 custom-validity callback 和 core 按 DOM id 的 UTF-8 message set/get；
  bootstrap 现在提供控件级 `setCustomValidity()` 与 `validationMessage`，并让
  `validity.customError`/`checkValidity()` 随 text/password、number/range/date/time 家族、
  textarea、select、checkbox/radio 和 file candidates 的 application-owned message 动态更新。
  这仍不实现 form `checkValidity()`、invalid 事件触发、validation UI 或完整本地化错误消息。

- next300 新增了按 form DOM id 的约束聚合查询；现有 validation callback 让 bootstrap 的同一
   `PElement.checkValidity()` 对 form 返回聚合 `valid`，并保持 `willValidate=false`、flags 为空。
   该查询忽略 `novalidate`、跳过 disabled/readonly/submit 等不参与候选的控件，可在布局前后运行；
   它仍不触发 invalid 事件、不提交表单，也不提供 `reportValidity()` 或 native invalid UI。

- next301 新增了独立 report-validity callback、`PCore_FormReportValidityById` 和 bootstrap 的
  `reportValidity()`。它按 form/control DOM id 查询当前状态，并按 DOM 顺序向有非空 id 的
  invalid controls 派发 trusted、non-bubbling、cancelable `invalid` 事件；`preventDefault()`
  不改变 boolean 结果，`novalidate` 也不绕过 report query，disabled/readonly 控件被跳过。
  该切片仍不提供 native validation UI、焦点/滚动、完整错误消息本地化、提交副作用或无 id
  控件的脚本事件目标。

- next302 新增了 `PCore_FormGetValidationMessageById`，并让 bootstrap 的
  `validationMessage` 在 custom message 为空时返回固定英文 fallback；覆盖当前 flags 的
  优先级、动态恢复和安全截断。它不做本地化，不显示 native validation UI，也不改变
  `setCustomValidity()` 的 application-owned message 语义。

- next303 在既有 attribute bridge 上增加了 `pattern`、`minLength`、`maxLength` 反射；
  `minLength`/`maxLength` 的非负有限整数 setter 会拒绝负数和非有限值，动态值会重新驱动
  `tooShort`/`tooLong`/`patternMismatch` 与 `checkValidity()`。它不声称完整 Web IDL
  异常类型或原生控件 UI。

- next304 在既有 attribute bridge 上增加了 form、input、textarea、select、button 的 `name`
  属性反射；动态改名会进入当前 successful-control submission。它不声称完整
  `HTMLFormControlsCollection`、表单关联算法或其他未迁移的 form/input API。

- next305 在同一 attribute bridge 上增加了 form `action`/`method` 属性反射；动态更新会进入
  当前受限的 GET/urlencoded-POST submission。两者保留 raw attribute 语义，不实现完整
  URL 解析、target 或 multipart 行为。

- next306 在同一 attribute bridge 上增加了 form `enctype` 属性反射；动态切换会进入现有的
  urlencoded 或 multipart submission snapshot 路径。它保留 raw attribute 语义，不实现
  `encoding` 别名、完整 enctype 规范化或 multipart 传输边界。

- next307 在同一 attribute bridge 上增加了 submitter `formAction` 属性反射；动态值会覆盖
  当前受限 urlencoded/multipart submission 的 action，移除后恢复 form action。它保留 raw
  attribute 语义，不实现完整 URL 解析、相对 URL 规范化或其他 submitter override 属性。
- next308 在同一 attribute bridge 上增加了 submitter `formMethod` 属性反射；动态已支持的
  `get`/`post` 值会覆盖当前受限 submission method，移除后恢复 form method。未知值/完整
  规范化与 multipart submitter override 仍留待后续切片。
- next309 在同一 attribute bridge 上增加了 submitter `formEnctype` 属性反射；已支持的
  `multipart/form-data` 与 `application/x-www-form-urlencoded` 值会覆盖当前 submission
  snapshot 的编码选择，移除后恢复 form enctype。未知值、别名与完整编码规范化仍留待后续切片。
- next310 让 text-input 的隐式 Enter submission 与显式 submitter 共用首个 submitter 的
  action/method/enctype override；`PCore_FormSubmissionForTextInput` 与 multipart snapshot
  仅覆盖当前受限的 first-submit 选择，不引入完整 implicit-submission 或键盘事件规范。
- next311 在同一 attribute bridge 上增加 form `target` 属性反射；raw getter/setter 不改变
  当前受限 submission action/method，也不实现新窗口、target browsing context 或导航副作用。
- next312 在同一 attribute bridge 上增加 form `autocomplete` 属性反射；raw getter/setter 不改变
  当前 submission，也不实现自动填充策略、凭据存储或控件级 autocomplete 语义。
- next313 在同一 attribute bridge 上增加 form `acceptCharset` ↔ `accept-charset` 属性反射；
  raw getter/setter 不改变当前 submission，也不实现字符集转换、编码协商或非 UTF-8 wire body。
- next314 在同一 attribute bridge 上增加控件 `placeholder` 属性反射；raw getter/setter 不改变
  current value 或 successful-control submission，也不实现 placeholder 绘制、SIP 或原生提示 UI。
- next315 在同一 attribute bridge 上确认 input `autocomplete` 属性反射；raw getter/setter 不改变
  current value 或 successful-control submission，也不实现自动填充策略、凭据存储或原生提示 UI。
- next316 在同一 attribute bridge 上增加 input `inputMode` ↔ `inputmode` 属性反射；raw getter/setter
  不改变 current value 或 successful-control submission，也不实现 SIP、键盘布局或输入法策略。
- next317 在同一 attribute bridge 上增加 input `type` raw 属性反射；当前切片只保证 attribute
  round-trip 与既有 text-control submission 不变，不实现动态控件重建、完整 Web IDL type 规范或
  native type UI。
- next318 只为 textarea 的既有 `placeholder` raw 反射补充独立设备断言；当前切片保证 textarea
  current value 与 submission 不变，不实现 placeholder 绘制、SIP、原生提示 UI 或完整 textarea
  Web IDL 语义。
- next319 只为 select 的既有 `autocomplete` raw 反射补充独立设备断言；当前切片保证选中值与
  submission 不变，不实现自动填充策略、凭据存储或完整 select Web IDL 语义。
- next320 只覆盖 button `type` 的 raw 反射和 submitter 恢复边界；提交断言要求脚本恢复
  `type=submit` 后才执行，不实现动态控件重建、完整 button Web IDL 或 native button UI。
- next321 只验证未知 form `method` 原始值保留并在 submission 时安全回落到 GET；不实现其他
  HTTP 方法、method 规范化或导航副作用。
- next322 只验证未知 form `enctype` 原始值保留并在 urlencoded POST 路径安全回落；不实现
  enctype 规范化、multipart 传输或其他编码格式。
- next323 只验证 method/enctype 的大小写不敏感匹配与 raw case 反射；不实现规范化 getter、
  完整 Web IDL 枚举语义或导航副作用。
- next324 只验证动态 action/method/value 更新后，反复重排不会陈旧化 submission metadata；
  不实现导航提交、异步任务或完整浏览器生命周期。
- next325 只验证 form reset 恢复控件默认值而保留动态 form action/method，并重新生成正确的
  submission metadata；不实现 reset event 默认动作之外的完整浏览器生命周期或导航。
- next326 是累计验证检查点，不新增产品语义；110 项启用 JavaScript 的相关回归通过，现有
  bootstrap timeout 仅作为环境噪声记录，不改变公共 API 或限制边界。
- next327 在既有 browser attribute bridge 上增加 `HTMLElement.title` raw UTF-8 反射；
  TEST294 及最近回归通过。该切片只保证属性往返，不实现 tooltip 绘制、原生提示 UI 或
  完整 HTMLElement Web IDL 语义。
- next328 在同一 bridge 上增加 `HTMLElement.lang` raw UTF-8 反射；TEST295 及最近回归通过。
  该切片只保证属性往返，不实现语言解析、本地化或完整 HTMLElement Web IDL 语义。
- next329 在同一 bridge 上增加 `HTMLElement.dir` raw UTF-8 反射；TEST296 及最近回归通过。
  该切片只保证属性往返，不实现 CSS 方向布局或完整 HTMLElement Web IDL 语义。
- next330 在同一 bridge 上增加 `HTMLElement.hidden` 布尔反射；TEST297 及最近回归通过。
  该切片只保证布尔属性往返，不实现隐藏布局算法、视觉或完整 HTMLElement Web IDL 语义。
- next331 在同一 bridge 上增加 `HTMLElement.accessKey` raw UTF-8 反射；TEST298 及最近回归通过。
  该切片只保证属性往返，不触发 WM 快捷键、焦点副作用或完整 HTMLElement Web IDL 语义。
- next332 在同一 bridge 上增加 `HTMLElement.role` raw UTF-8 反射；TEST299 及最近回归通过。
  该切片只保证属性往返，不实现辅助技术树、语义计算或完整 HTMLElement Web IDL 语义。
- next333 在同一 bridge 上增加 `HTMLElement.ariaLabel` ↔ `aria-label` raw UTF-8 反射；TEST300
  及重试后的最近回归通过。该切片只保证属性往返，不实现 ARIA 语义树或辅助技术计算。
- next334 在同一 bridge 上增加 `HTMLElement.contentEditable` raw UTF-8 反射；TEST301 及最近
  回归通过。该切片只保证属性往返，不改变 layout、编辑控件、native IME 或完整 Web IDL 语义。
- next335 在同一 bridge 上增加 `HTMLElement.draggable` raw UTF-8 反射；TEST302 及最近回归通过。
  该切片只保证属性往返，不实现拖放手势、命中测试、native pointer 或完整 Web IDL 语义。
- next336 在同一 bridge 上增加 `HTMLElement.tabIndex` 的有限整数 raw 反射；TEST303 及最近回归
  通过。缺失或非法 raw attribute 回落为 `-1`，setter 只接受有限整数；该切片不实现焦点导航、
  滚动、键盘顺序或完整 HTMLElement Web IDL 语义。
- next337 在同一 bridge 上增加 `HTMLInputElement.accept` raw UTF-8 反射；TEST304 及最近回归
  通过。该切片只保证属性往返，不实现文件类型解析、过滤、系统 picker 或完整 input Web IDL
  语义。
- next338 在同一 bridge 上增加 `HTMLInputElement.capture` raw UTF-8 反射；TEST305 及最近回归
  通过。该切片只保证属性往返，不实现摄像头/麦克风捕获、文件类型过滤、系统 picker 或完整
  input Web IDL 语义。
- next339 在同一 bridge 上增加 `HTMLInputElement.dirname` raw UTF-8 反射；TEST306 及最近回归
  通过。该切片只保证属性往返，不改变提交方向字段、编码或其他表单默认行为，也不实现完整
  input Web IDL 语义。
- next340 在同一 bridge 上增加 `HTMLInputElement.list` raw UTF-8 反射；TEST307 及重试后的最近
  回归通过。该切片只保证属性往返，不实现 datalist 解析、建议项、自动完成或完整 input Web
  IDL 语义；首次近期门的既有 bootstrap timeout 不作为基线。
- next341 在同一 bridge 上增加 `HTMLTextAreaElement.wrap` raw UTF-8 反射；TEST308 及最近回归
  通过。该切片只保证属性往返，不实现软/硬换行布局、提交编码差异或完整 textarea Web IDL
  语义。
- next342 在同一 bridge 上增加 `HTMLElement.htmlFor` ↔ `for` raw UTF-8 反射；TEST309 及最近
  回归通过。它不实现 label 关联、焦点转移或完整 HTMLElement Web IDL；`className` 重定义因
  已有不可配置 descriptor 冲突而撤回，边界见 `FAILED_EXPERIMENTS.md`。
- next343 在同一 bridge 上增加 `HTMLElement.slot` ↔ `slot` raw UTF-8 反射；TEST310 及最近
  回归通过。该切片只保证属性往返，不实现 Shadow DOM、slot 分配或完整 HTMLElement Web IDL。
- next344 在同一 bridge 上增加 `HTMLElement.itemId` ↔ `itemid` raw UTF-8 反射；TEST311 及最近
  回归通过。该切片只保证属性往返，不实现 microdata 解析、语义树或完整 HTMLElement Web IDL。
- next345 在同一 bridge 上增加 `HTMLElement.itemProp` ↔ `itemprop` raw UTF-8 反射；TEST312 及
  最近回归通过。该切片只保证属性往返，不实现 microdata token 解析、语义树或完整 HTMLElement
  Web IDL。
- next346 在同一 bridge 上增加 `HTMLElement.itemRef` ↔ `itemref` raw UTF-8 反射；TEST313 及最近
  回归通过。该切片只保证属性往返，不实现 microdata 引用解析、语义树或完整 HTMLElement Web
  IDL。
- next347 在同一 bridge 上增加 `HTMLElement.itemScope` ↔ `itemscope` 布尔反射；TEST314 及最近
  回归通过。该切片只保证布尔属性往返，不实现 microdata item 解析、语义树或完整 HTMLElement
  Web IDL。
- next348 在同一 bridge 上增加 `HTMLElement.itemType` ↔ `itemtype` raw UTF-8 反射；TEST315 及
  最近回归通过。该切片只保证属性往返，不实现 microdata vocabulary 解析、语义树或完整
  HTMLElement Web IDL。
- next349 在同一 bridge 上增加 `HTMLElement.nonce` ↔ `nonce` raw UTF-8 反射；TEST316 及最近
  回归通过。该切片只保证属性往返，不实现 CSP nonce 校验、安全策略、脚本执行或完整
  HTMLElement Web IDL；首个 `<script>` 测试夹具没有产生 probe 结果，已改用普通 `<div>`，失败
  边界保留在 `HANDOFF.md` 的设备证据中。
- next350 在同一 bridge 上增加 `HTMLElement.part` ↔ `part` raw UTF-8 反射；TEST317 及最近
  回归通过。该切片只保证属性往返，不实现 Shadow DOM 部件导出、CSS 选择器语义或完整
  HTMLElement Web IDL。
- next351 在同一 bridge 上增加 `HTMLElement.exportParts` ↔ `exportparts` raw UTF-8 反射；TEST318
  及最近回归通过。该切片只保证属性往返，不实现 Shadow DOM 部件导出算法或完整 HTMLElement
  Web IDL。
- next352 在同一 bridge 上增加 `HTMLElement.inert` 布尔反射；TEST319 及最近回归通过。该切片
  只保证属性往返，不实现焦点、键盘、无障碍树或完整 HTMLElement Web IDL。
- next353 在同一 bridge 上增加 `HTMLElement.popover` ↔ `popover` raw UTF-8 反射；TEST320 及
  最近回归通过。该切片只保证属性往返，不实现 popover 显示/隐藏、焦点管理、top-layer 或
  完整 HTMLElement Web IDL。
- next354 在同一 bridge 上增加 `HTMLElement.autofocus` 布尔反射；TEST321 及重试后的最近
  回归通过。该切片只保证属性往返，不实现焦点调度、窗口激活或完整 HTMLElement Web IDL；
  首次近期门的既有 TEST266 bootstrap timeout 不作为基线。
- next355 在同一 bridge 上增加 `HTMLInputElement.enterKeyHint` ↔ `enterkeyhint` raw UTF-8
  反射；TEST322 及最近回归通过。该切片只保证属性往返，不实现 SIP、键盘布局、输入法策略
  或完整 input Web IDL。
- next356 在同一 bridge 上增加 `HTMLInputElement.virtualKeyboardPolicy` ↔
  `virtualkeyboardpolicy` raw UTF-8 反射；TEST323 及最近回归通过。该切片只保证属性往返，不
  实现 SIP、虚拟键盘策略执行或完整 input Web IDL。
- next357 在同一 bridge 上增加 `HTMLInputElement.webkitDirectory` 布尔反射；TEST324 及最近
  回归通过。该切片只保证属性往返，不触发目录 picker、实现目录选择语义或完整 input Web IDL。
- next358 在同一 bridge 上为 `HTMLInputElement.size` 接入有限整数反射；TEST325 及最近回归
  通过。该切片只保证有限整数往返、malformed 回落和移除恢复，不声明默认 20、控件宽度、
  范围钳制或原生输入 UI 语义。
- next359 在同一 bridge 上为 `HTMLTextAreaElement.cols` 接入有限整数反射；TEST326 及最近
  回归通过。该切片只保证有限整数往返、malformed 回落和移除恢复，不声明 textarea 布局
  宽度或完整 Web IDL 语义。
- next360 在同一 bridge 上为 `HTMLTextAreaElement.rows` 接入有限整数反射；TEST327 及最近
  回归通过。该切片只保证有限整数往返、malformed 回落和移除恢复，不声明 textarea 布局
  高度或完整 Web IDL 语义。
- next361 在同一 bridge 上增加 `HTMLElement.open` 布尔反射；TEST328 及最近回归通过。该切片
  只保证属性往返，不实现 details 展开布局、summary 激活、disclosure 交互或完整 HTMLElement
  Web IDL。
- next362–364 在同一 bridge 上增加 `HTMLElement.autocapitalize`、`itemValue`、`is` 的 raw
  UTF-8 属性反射；TEST329–331 通过。该切片不实现输入法/大小写策略、microdata 解析或
  customized built-in 升级。
- next365–381 在同一 bridge 上增加 `HTMLElement.ariaAtomic`、`ariaBusy`、`ariaChecked`、
  `ariaCurrent`、`ariaDescription`、`ariaDisabled`、`ariaExpanded`、`ariaHasPopup`、`ariaHidden`、
  `ariaKeyShortcuts`、`ariaLabelledBy`、`ariaLevel`、`ariaLive`、`ariaModal`、`ariaPlaceholder`、
  `ariaPressed`、`ariaSelected` 对应 `aria-*` 的 raw UTF-8 属性反射；TEST332–348 通过。该切片
  只保证 getter/setter、attribute round-trip 和移除恢复，不实现 ARIA 语义计算、可访问性树、
  辅助技术通知、焦点/交互或完整 Web IDL。
- next382–401 在同一 bridge 上增加 `HTMLElement.ariaColCount`、`ariaColIndex`、
  `ariaColIndexText`、`ariaControls`、`ariaDescribedBy`、`ariaDetails`、`ariaErrorMessage`、
  `ariaFlowTo`、`ariaInvalid`、`ariaMultiLine`、`ariaMultiSelectable`、`ariaOrientation`、
  `ariaOwns`、`ariaPosInSet`、`ariaReadOnly`、`ariaRelevant`、`ariaRequired`、
  `ariaRoleDescription`、`ariaRowCount`、`ariaRowIndex` 对应 `aria-*` 的 raw UTF-8 属性反射；
  TEST349–368 通过。该切片只保证 getter/setter、attribute round-trip 和移除恢复，不实现
  ARIA 语义计算、可访问性树、辅助技术通知、焦点/交互或完整 Web IDL。

尚未完成：完整 DOM/window、其余 form/input callback 实现、完整规范/本地化 validationMessage、native invalid UI、module、
异步任务、CSP、同源策略、任意 Web API 和完整 URL Standard；JavaScript bridge 仍有一部分
实现位于 `test_host/main.c`，尚未成为可供正式浏览器应用复用的 browser-layer API。

完成方法：每个上游能力单独做纵向测试；关闭路径不得新增脚本请求；完整自动门和对应
人工门都通过后才能扩大声明。

## 独立 JavaScript DLL

当前状态：Duktape 2.7.0 的持久 context、执行预算、内存限制、模块、global/JSON 和
native callback 已存在。

尚未完成：它不是浏览器环境；context 不支持并发或 callback 重入；源码、模块、内存和
callback 均有硬上限。

完成方法：只在外部消费者有明确需求时扩展稳定 ABI，不以浏览器私有对象污染公共 DLL。

## URL 与 History

当前状态：已有有限的成功-GET 宿主历史、后退/前进/go、受控 state、
fragment/hashchange，以及逐步扩展的相对 URL 分类。

尚未完成：

- 不是完整 URL parser；
- 没有页面缓存和持久历史；
- 没有滚动/表单恢复；
- 没有完整跨文档 state 生命周期或 POST 恢复；
- 仍不是完整 URL Standard parser。

完成方法：每种规范化语义都要有正反例、无 GET、state/length 和事件门；设备全量通过后
才能提升基线。

## HTML/CSS 布局

当前状态：block/inline、常见 flex/table/list、基础 relative/absolute、overflow 和 hover
可以覆盖一批轻量页面。

尚未完成：float 已撤回；Grid、sticky、复杂 containing block、完整 table 边界、
完整 CSS Lists 和大量高级 CSS 未实现。

完成方法：优先移植或复用上游数据流，并同时通过离线几何、TEST13 深链、旋转和人工截图。

## 页面视觉

当前状态：example.com/IANA 主链已有多轮设备与人工证据。

尚未完成：这些证据不能外推为任意网站兼容。局部容器尺寸、字体测量、抗锯齿以及
高 DPI/旋转组合仍可能异常。

完成方法：为可重复页面记录 viewport/DPI、computed style、box geometry 和前后截图，
再建立针对性回归。

## 表单与输入

当前状态：已有 native EDIT/SELECT、file input、textarea、checkbox/radio、提交/reset、基础 constraint
validation、keyboard/focus-family/EDIT change/post-change input/click/submit/reset/invalid/reportValidity/file-input input/change/checkbox-radio input/change/label activation/checkbox-radio keyboard activation/checkbox-radio programmatic `click()`/submit-reset-button programmatic `click()`/file-input programmatic `click()` typed dispatch/SELECT input/change typed dispatch、`HTMLElement.disabled` 和约束属性的 attribute-backed getter/setter、控件级 `setCustomValidity()`/`validationMessage`、受限 form-level `checkValidity()`/`reportValidity()` 聚合查询、动态 form-level/button-level no-validate 语义和部分 composition bridge；WM 控件与 core 事件传播仍由宿主负责。

尚未完成：任意 OEM IME、完整 composition/preedit、除 type=number 的 min/max/step 和 type=range
默认/显式边界/缺省中点核心校验外的完整类型/范围/step 语义、除 email 和保守 url 核心校验外的
email/URL 类型校验、除 bounded date/time（含规范 step base）/month/week/datetime-local（含 step）/color 核心
校验外的完整 date/time/month/week/datetime-local/color 类型校验、native invalid UI、
native file input 的完整文件选择体验。程序化 click 在自动/无窗口路径仍只
分发 typed click；在有 render window 的宿主路径中，next295 已把未取消的 request 排入窗口
消息循环，再复用既有 host picker adapter。TEST231 已自动覆盖 host picker 的注入错误/空选择
边界，TEST262 已覆盖排队合并、disabled、取消和文档替换丢弃；TEST232 的真实 WM6 picker
选择/同窗口取消/窗口返回已由用户人工验收，TEST263 的程序化 picker 真实 GUI 入口也已
由用户验收。TEST264 自动覆盖 `HTMLElement.disabled` 的 attribute round-trip、required
validation 和 successful-control submission；TEST265 自动覆盖约束属性反射、动态 min/max/step、
readonly 和 no-validate 绕过/恢复及 successful-control submission。产品 DLL 仍不提供系统
picker 或窗口生命周期 API。

完成方法：synthetic event 与真实 SIP 分开验收；至少覆盖候选词、Unicode、旋转和
native control 生命周期。

## 图像与 SVG

当前状态：BMP/PNG/JPEG/GIF、缓存 `<img>`、SVG retained draw、损坏 fallback 和部分编码
已经验证。

尚未完成：位图 codec 受设备影响；没有完整 SVG/CSS Images、动画、全部滤镜/字体或通用
web-font 系统。

完成方法：公共 `positron_image` 直测和 core 正式链同时通过，包含损坏输入、所有权和
多次 redraw。

## 网络与安全

当前状态：HTTPS 默认验证证书链和 hostname；明文 HTTP 使用 WinInet；页面资源分轮加载。

尚未完成：Mbed TLS 固定在停止维护的 2.16.12；HTTP/1.1 能力有限；设备旧时钟、OEM
网络栈和站点变化会影响结果。

完成方法：中期评估仍兼容 MSVC9 的受维护 TLS；网络失败必须按
DNS/TCP/TLS/HTTP/页面提交阶段取证。

## 性能与线程

当前状态：主文档和资源网络工作已移出 UI 线程；已有阶段遥测和文档内/重叠文档图像复用。

尚未完成：parse/style/layout 和部分 image create 仍在 UI 提交阶段；没有完整浏览器缓存、
后台 DOM/layout 或全面内存策略。

完成方法：只有设备热点阻塞可用性时插队；先用阶段数据定位，不跨线程共享 DOM/GDI。

## 字体

当前状态：随包提供 symbol 和单色 emoji fallback，缺失时可以诊断并降级。

尚未完成：它不是普通语言 web-font 系统；覆盖范围和 OEM 字体渲染有限。

完成方法：新字体必须有明确页面需求、来源/许可证、内存预算和设备视觉证据。

## 验收边界

- 自动首帧和数值断言不能替代字体、边距、抗锯齿、触摸、SIP 和旋转人工检查。
- 设备日志不在工作区时，不能仅凭口头“跑完了”把候选升级为基线。
- 真实网站是集成哨兵，其远端内容和网络状态可能变化；稳定语义还需要离线 fixture。
- 96 DPI 不是产品常量。每次 viewport、旋转或 interaction restyle 都要保留真实设备 DPI。
- `test_host.exe` 是消费者，不能把只适合宿主的私有接口伪装成公共 DLL 能力。
- WMDC RAPI 1 不暴露可靠远端退出码/等待/终止；自动 gate 以完整日志标记判定完成，超时后
  不会杀死设备进程，需要人工确认进程状态。

## 当前 URL 分类边界

绝对和根相对 URL path 中多个完整 `%2E%2E` segment 的受控折叠已由 next221 全量设备门
验证；history state 的根相对 path/query/fragment 与 query-relative 写法已由 next222 验证，
当前 document 目录下的单段 sibling、显式 `./` 单段/多段 sibling、裸多段
document-relative sibling、显式 `./?query`/`./#fragment` trailing-slash 写法，以及
同源 absolute URL 在 path 不变时的 query/fragment 变化已由 next228 验证。
HTTP 的 `:80`/无端口和 HTTPS 的 `:443`/无端口同源等价已由 next229 验证。
安全的同源 absolute pathname 变化已由 next230 的 TEST197 和 145 项全量设备门验证；
普通 percent-encoded pathname segment 已由 next231 的 TEST198 和 146 项全量设备门验证；
根相对 pathname 的同一安全校验已由 next232 的 TEST199 和 147 项全量设备门验证；
显式 `undefined` history URL 默认当前 document URL、显式空字符串保持同 URL entry 的语义
已由 next233 的 TEST200 和 148 项全量设备门验证，均不发 GET；history/session 状态机迁入
`positron_browser.dll` 已由 next234 的 TEST201、TEST149-201 定向门和 149 项全量门验证；
浏览器脚本 session 的 context 所有权、JSON callback 注册/调用和销毁已由 next235 的 TEST202、
55 项脚本回归门和 150 项全量门验证；bootstrap 文本与求值入口已由 next236 的 TEST203、56
项脚本回归门和 151 项全量门验证；DOM 只读 callback adapter 已由 next237 的 TEST204、
`TEST189-204,999`（17 项）定向门和 `TEST13/20/27/43/44/56/58-77/80-204/999`（152 项）
全量门验证；textContent 写入 callback 已由 next238 的 TEST205、
`TEST112-135,137-152,189-205,999`（58 项）定向门和
`TEST13/20/27/43/44/56/58-77/80-205/999`（153 项）全量门验证；DOM attribute callback 已由
next239 的 TEST206、`TEST112-135,137-152,189-206,999`（59 项）定向门和
`TEST13/20/27/43/44/56/58-77/80-206/999`（154 项）全量门验证；Event callback 已由
next240 的 TEST207、`TEST112-135,137-152,189-207,999`（60 项）定向门和
`TEST13/20/27/43/44/56/58-77/80-207/999`（155 项）全量门验证；next241 的 TEST208、
`TEST112-135,137-152,189-208,999`（61 项）定向门和
`TEST13/20/27/43/44/56/58-77/80-208/999`（156 项）全量门进一步验证 input value callback；
next242 的 TEST209、`TEST112-135,137-152,189-209,999`（62 项）定向门和
`TEST13/20/27/43/44/56/58-77/80-209/999`（157 项）全量门进一步验证 checked callback；
next243 的 TEST210、`TEST112-135,137-152,189-210,999`（63 项）定向门和
`TEST13/20/27/43/44/56/58-77/80-210/999`（158 项）全量门验证 form-property callback；
next244 的 TEST211、`TEST112-135,137-152,189-211,999`（64 项）定向门和
`TEST13/20/27/43/44/56/58-77/80-211/999`（159 项）全量门验证 navigation JSON dispatch；
next251 的 TEST218、`TEST112-135,137-152,189-218,999`（71 项）定向门和
`TEST13/20/27/43/44/56/58-77/80-218/999`（166 项）全量门验证 EDIT change typed dispatch
contract；next252 的 TEST219、`TEST112-135,137-152,189-219,999`（72 项）定向门和
`TEST13/20/27/43/44/56/58-77/80-219/999`（167 项）全量门验证 EDIT post-change input typed dispatch
contract；next253 的 TEST220、`TEST112-135,137-152,189-220,999`（73 项）定向门和
`TEST13/20/27/43/44/56/58-77/80-220/999`（168 项）全量门验证 native click typed dispatch contract；next254 的 TEST221、`TEST112-135,137-152,189-221,999`（74 项）定向门和
`TEST13/20/27/43/44/56/58-77/80-221/999`（169 项）全量门验证 native submit/reset typed dispatch contract；next255 的 TEST222、`TEST112-135,137-152,189-222,999`（75 项）定向门和
`TEST13/20/27/43/44/56/58-77/80-222/999`（170 项）全量门验证 native invalid typed dispatch contract；next250 的 SELECT input、next249 的 SELECT change、next248 的 focus-family、next247 的 native keyboard、next246 的 native input/composition contract 保持通过；其余 form/input callback 实现、
core 事件传播及导航 side effect 仍在宿主，
尚未计入产品层完成项。
next256 的 TEST223、`TEST223/999`（2 项）定向门和 `TEST189-223/999`（36 项）相关回归门
验证 file-input 复用 input/select typed callback、`insertFromFile` metadata、事件顺序、取消、
非法参数、adapter error 和注销；本批未重复 next255 的 170 项全量门；next257 的 TEST224、
`TEST224/999`（2 项）定向门和 `TEST189-224/999`（37 项）相关回归门验证 checkbox/radio
状态提交后的非可取消 change、脚本 target、已选 radio/disabled 静默边界；本批仍未重复
next255 的 170 项全量门。next258 的 TEST225、`TEST225/999`（2 项）定向门和
`TEST189-225/999`（38 项）相关回归门验证 checkbox/radio 状态提交后的非可取消
`input` → `change` 顺序、空 InputEvent metadata、脚本 target、已选 radio/disabled 静默边界；
本批仍未重复 next255 的 170 项全量门。next259 的 TEST226、`TEST226/999`（2 项）定向门和
`TEST189-226/999`（39 项）相关回归门验证 label/native checkbox activation 的 label click、
目标 control click、`input` → `change` 顺序、取消阻断和 disabled 静默边界；本批仍未重复
next255 的 170 项全量门。next260 的 TEST227、`TEST227/999`（2 项）定向门和
`TEST189-227/999`（40 项）相关回归门验证 checkbox/radio 的 Space/Enter WM keyboard
activation、`keydown` → `click` → `input` → `change` → `keyup` 顺序、keydown/click
取消、重复 keydown 不重复切换和 disabled 静默边界；本批仍未重复 next255 的 170 项全量门。
next261 的 TEST228、`TEST228/999`（2 项）定向门和 `TEST189-228/999`（41 项）相关回归门
验证 `HTMLElement.click()` 的 checkbox/radio target、`click` → `input` → `change` 顺序、
取消、disabled/no-op、radio 互斥、programmatic-click adapter error 和资源关闭；本批仍未重复
next255 的 170 项全量门。
next262 的 TEST229、`TEST229/999`（2 项）定向门和 `TEST68-69,189-229/999`（44 项）相关回归门
验证 native submit/reset/button 的程序化 click target、submit/reset form-event 顺序、取消、
reset 初值恢复、generic/disabled no-op 和 reset 重复事件边界；回归首尝 TEST193 的既有
JavaScript timeout 以原配置重试通过，本批仍未重复 next255 的 170 项全量门。
next263 的 TEST230、`TEST230/999`（2 项）定向门和 `TEST70,189-230/999`（44 项）相关回归门
验证 native file input 的程序化 click target、取消、disabled/no-op、空 value/path 和系统 picker
边界；程序化路径不打开 picker，相关 adapter error、注销和 native function 资源关闭由 TEST228
继续覆盖，本批仍未重复 next255 的 170 项全量门。
next264 的 TEST231、`TEST231/999`（2 项）定向门和 `TEST70,189-231/999`（45 项）相关回归门
验证宿主 picker adapter 的选择、取消、错误、空选择提交错误、文件 value/path、`input` → `change`
顺序、再次取消保留状态和同步 callback 生命周期；真实 WM6 picker 仍需人工验收，本批仍未重复
next255 的 170 项全量门。
以下仍按普通导航或不支持处理：

- 完整与半编码 double-dot 混合；
- 字面 `..` 与编码 segment 混合；
- 含 literal/mixed/complete encoded dot segment、重复分隔符的 absolute/root-relative pathname
  （普通 percent-encoded segment 已受限支持）；
- 越过 origin 根或没有非空前驱目录的折叠。
- 裸 `./`、`.` 和 `../` history state URL；
- protocol-relative history state URL；
- 同源 absolute/root-relative history URL 的不安全 path 变化（安全 pathname、普通
  percent-encoded segment、同 path query/fragment 变化已受限支持）；
- IDN、userinfo 和其他完整 URL Standard origin 规范化；默认端口只支持上述 HTTP/HTTPS
  两组等价形式。

## 不得用限制掩盖回归

限制表示能力尚未完成，不表示可以接受新崩溃、数据损坏、旧页面严重布局破坏或核心交互
阻塞。遇到这些情况立即回退候选路径并查
[`FAILED_EXPERIMENTS.md`](FAILED_EXPERIMENTS.md)，不能通过删除测试、扩大预算或放宽断言
把失败写成“已知限制”。

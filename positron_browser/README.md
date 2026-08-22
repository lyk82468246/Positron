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
document、DOM、navigation、event、input、keyboard、focus、EDIT change/post-change input、click、programmatic `HTMLElement.click()`、`HTMLElement.disabled`、控件与受限 form-level `checkValidity()`/`reportValidity()`、`willValidate`、`validity` 查询、`setCustomValidity()`、`validationMessage`、`required`、`readOnly`、`multiple`、`noValidate`、`formNoValidate`、`name`、form `action`/`method`/`enctype`/`target`/`autocomplete`/`acceptCharset`、submitter `formAction`/`formMethod`/`formEnctype`、控件 `placeholder`/`autocomplete`/`inputMode`/`type`、`min`/`max`/`step`、`pattern`/`minLength`/`maxLength`、submit/reset、invalid、file-input、checkbox/radio input/change 和 SELECT input/change 适配。这些表单属性通过既有 attribute callback bridge 实现；validation query 通过独立的 size-tagged callback 获取 core 的控件状态或 form 聚合结果，report-validity callback 负责同步 report/query 与 invalid-event 路由，custom validity 通过另一个 size-tagged UTF-8 get/set callback 获取/更新 application-owned message，`validationMessage` 在 custom message 为空时可使用宿主提供的固定英文 fallback；对 file input，programmatic click 只负责 typed click 分发；系统 picker、文件系统权限和窗口生命周期仍由宿主 GUI 拥有。
`test_host.exe` 是一个完整的组合示例，但不是私有 API 的唯一消费者。

当前 DOM snapshot 还提供 browser-owned 的 `document.doctype`：它是稳定、只读的
`DocumentType` wrapper，包含有限 metadata、owner/root/position/contains、identity/equality
和 document child order 视图；它不要求调用者提供 core doctype parser，也不提供节点创建或
mutation。该能力仍遵守 session-scoped、fail-closed 的关系边界。

当前 Node snapshot 还为 document、DocumentType、HTML element、CharacterData 和 Attr wrapper
提供受控的 `baseURI`、`namespaceURI`、`prefix`、`lookupNamespaceURI()` 与
`isDefaultNamespace()`。`baseURI` 读取并跟随当前 session URL；namespace 只承诺 HTML/XML 的
有限值，未知 prefix fail closed，Attr 查询沿 owner element 上下文工作。该能力不实现 XML/
namespace parser、节点创建、prefix/namespace mutation 或完整 DOM tree。

next588 又在 document 与 HTML element wrapper 上提供受控的
`getElementsByTagName()`/`getElementsByClassName()`。查询沿当前 bounded relation snapshot
按 DFS 文档顺序生成静态 HTMLCollection，支持 tag 大小写归一、`*`、规范化多 class token、
`item()`/`namedItem()`、`forEach()`/`keys()`/`values()`/`entries()`、默认 iterator 和
`Symbol.toStringTag`；element 查询排除 owner，document 查询包含 structural `documentElement`。
空白/未知输入返回空集合；这不是 live collection、通用 CSS selector、节点创建或 mutation API。
为容纳这组 bootstrap，browser session heap ceiling 为 608 KiB，独立 `positron_script` 默认堆
仍为 512 KiB。

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
PBrowser_ScriptSessionEvaluateBootstrap(session);
PBrowser_ScriptSessionEvaluate(session, "document.title", -1);
/* PBrowser_ScriptSessionGetResult/GetError 返回借用字符串。 */
PBrowser_ScriptSessionDestroy(session);
```

主要公共能力包括：

- `PBrowser_History*`：opaque history、同源判断、commit/replace、push/replaceState、
  back/forward/go 和同文档导航投影；
- `PBrowser_ScriptSession*`：创建/销毁浏览器专用 PScript context、求值、JSON global、bootstrap、
  DOM read/write/attribute/value/checked/form-property、navigation/location/history 事件、
  Event JSON 和 native input/keyboard/focus/EDIT change/post-change input/click、programmatic
  `HTMLElement.click()`、`HTMLElement.disabled`、按 id 的 DOM 关系/`children`/`contains()`/
  基础 `compareDocumentPosition()`/受限 `matches()`/`closest()`/元素作用域 querySelector、form
  owner 与 `form.elements` collection、attribute count/name/value、`getAttributeNames()`、
  `attributes`/`Attr`/受限 NamedNodeMap（`length`、`item()`、named lookup、同 owner mutation、
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
  协议 snapshot；文档级 `querySelector()`/`querySelectorAll()` 只额外支持 `html`、`:root`、
  `head`、`body`，不宣称通用 selector。core 通过三个保留结构 token 映射无 id 的结构节点，
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
或提交。产品层只管理
session 与脚本对象；宿主必须管理 document、窗口、网络、控件默认行为、core 事件传播
以及导航提交/回滚，并在 session 销毁前注销或保证 callback `pw` 仍有效。

## 边界与验证

浏览器 JavaScript 仍由显式开关控制，默认 `javascript=0`。本 DLL 不是完整浏览器、
不是 URL Standard parser，也不应暴露 Duktape、libdom 或窗口对象。公共 ABI 变更必须
保持 UTF-8、opaque handle、明确所有权和 VS2008/ARMV4I 兼容；修改后运行正式构建、
脚本/设备门和相应人工门。

显式启用脚本时，bootstrap 还提供受限的 `dataset`/节点 metadata、FormData/Headers/Storage/
classList/style iterator、Headers/URLSearchParams/FormData mutation-safe snapshots、
`TextEncoder.encodeInto()`、TextDecoder 选项快照、同步 Request/Response JSON/one-shot body 与
clone ownership、Blob/File metadata/slice/JSON、Headers `getSetCookie()`、URL authority
userinfo/default-port 与 URLSearchParams pair/delete-value/按值查询、cookie Max-Age 删除、
AbortSignal `timeout`/`any`/`onabort`/`abort`/tags、timer extra arguments/`setImmediate`、
MessagePort/BroadcastChannel（`onmessage` 自动 start）、structuredClone、Storage/HashChange/
PopState/Error/Progress/Close event 构造器、同步 PerformanceObserver/EntryList 快照与选项校验、
performance entry JSON、NodeList/HTMLCollection item/namedItem/forEach/keys/values/entries/iterator、
稳定 element/classList/style wrapper identity、
DOM wrapper tags、navigator 方法、viewport 派生的 `screen.orientation`、window aliases/open-close
no-op，以及由宿主显式 microtask pump 驱动的 bounded Promise（含 `then`/`catch`/`finally`、
`resolve`/`reject`、`all`/`race`/`allSettled`/`any`）。它们只在单个 session 内存中运行；
Request/Response 不联网，MessagePort/BroadcastChannel/timeout/Promise 需宿主显式 pump，
PerformanceObserver 只读取 observe 时已有 entries，不等于完整 DOM、fetch/stream、真实窗口
生命周期或后台浏览器调度。Promise handler 和组合器输入均限制为 64 项。公共 bootstrap 现在按
十三个顺序 IIFE 评估以保持脚本 source 上限；browser session heap ceiling 为 608 KiB，独立
`positron_script` context 的默认 heap 仍为 512 KiB。

DOM relation callback 是独立的 size-tagged ABI：调用者提供 `get_relation`，按元素 id 返回
UTF-8 字段或数量；attribute name/value 和 `CHILD_NODE_*` 字段也沿用同一 probe/truncation
contract。关系值是 session 内稳定 wrapper 的只读 snapshot；legacy `children`/form collection
仍按可寻址元素工作，而 `childNodes` 额外保留文本、注释和无 id 元素的直接子节点；集合遍历
方法只读取这些同步 snapshot。缺失 id、越界索引和不支持关系 fail closed。它不提供通用 DOM
mutation、节点创建、live collection、
shadow tree、复杂 CSS selector、namespace、layout 或 native control 查询；Node position 只沿
当前 bounded parent/child snapshot 计算，未知对象或跨快照关系返回 false/33；`isEqualNode()`
也不是完整 Web IDL 深结构相等，`form.elements`、NamedNodeMap 和 childNodes 也不是完整 live
Web IDL 集合。

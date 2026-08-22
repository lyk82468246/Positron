# Positron 架构与公共边界

## 项目使命

Positron 面向 Windows Mobile 6 Professional / Windows CE 5.2，在不替换操作系统的前提下
补齐现代 TLS、JSON、HTTP、图像、脚本和网页运行能力。

项目同时服务两类消费者：

1. 普通 WM6 C/C++ 程序，可以单独使用一个或多个公共 DLL。
2. 浏览器或 Positron 应用运行时，通过宿主组合这些 DLL，提供窗口、网络、DOM、布局、
   输入、脚本和 native bridge。

`test_host.exe` 是回归宿主和示例消费者，不是公共 API 的所有者。

## 分层

```text
WM6 application / test_host
        |
        +-- positron_tls       modern TLS
        +-- positron_json      JSON
        +-- positron_http ----- positron_tls
        +-- positron_image     bitmap/SVG services
        +-- positron_script    standalone JavaScript runtime
        |
        +-- positron_core
              |
              +-- ported NetSurf static libraries
              +-- DOM / CSS / layout / paint / interaction
              +-- host callbacks for URL resolution and resources

        +-- positron_browser
              |
              +-- browser session / history / same-origin state
              +-- script bootstrap + DOM read/write/attribute/value/checked/disabled/validation/custom-validity/form-property/navigation/location/event/native-input/key/focus/edit-input/click/programmatic-click/form-event/invalid/file-input/checkbox-radio-change/select bridge
              +-- remaining form/input bridge (in migration)

Browser host = composition of positron_browser + positron_core
               + positron_script + networking + native WM controls
```

内部 NetSurf、libdom、libcss、libhubbub、libsvgtiny 等静态库被封装在产品边界后面。
应用程序不应包含它们的头文件，也不应依赖其符号或对象布局。

## 公共 DLL

### `positron_tls.dll`

提供 TLS 1.2 客户端、嵌入式 CA bundle、证书链验证和主机名验证。默认产品路径应使用
`PTls_ConnectVerified`。`PTls_Connect` 跳过证书验证，只适合自签名环境或诊断。

初始化和清理成对进行；连接句柄由 `PTls_Close` 释放。额外根证书必须在并发连接开始前
加入，因为修改全局信任链不是线程安全操作。

### `positron_json.dll`

把 cJSON 封装为 UTF-8、opaque-handle C ABI。顶层解析结果由调用者通过 `PJson_Free`
释放；子对象和数组项借用顶层对象的生命周期。序列化结果必须使用
`PJson_FreeString` 释放，避免跨 CRT 释放。

### `positron_http.dll`

提供同步 HTTP/1.1 GET/POST 和可选的响应进度回调。HTTPS 使用 `positron_tls`，明文
HTTP 使用 WM WinInet。响应对象无论成功或传输失败都由 `PHttp_FreeResponse` 释放。

HTTP 模块负责传输，不拥有 DOM、布局或浏览器导航策略。页面资源的调度、缓存和提交由
浏览器宿主管理。

### `positron_image.dll`

提供保留式位图和 SVG 对象、绘制、原始像素导入和编码。输入缓冲由调用者拥有，DLL 在
需要保留时复制数据；输出对象和缓冲必须使用匹配的 `PImage_Free*` API 释放。

保留式图像对象具有创建线程亲和性。位图 codec 能力受设备 WM Imaging 安装情况影响；
SVG 由固定的开源解析/栅格化链处理，不代表完整 SVG 浏览器实现。

### `positron_script.dll`

把 Duktape 2.7.0 封装为独立 JavaScript 执行服务。它提供持久 context、求值、模块、
JSON-compatible global、native callback、执行预算和内存限制，但本身不创建浏览器窗口、
不抓取资源，也不拥有 DOM。

每个 context 是调用者拥有的 opaque handle，不支持并发调用。宿主 callback 不得重入或
销毁正在执行的 context。源码、模块数、native function 数、结果和内存都有明确上限；
精确常量以 [`positron_script.h`](../positron_script/positron_script.h) 为准。

### `positron_core.dll`

这是 HTML/CSS 渲染产品边界。它封装：

- HTML 解析和 DOM 生命周期；
- CSS 解析、级联、整树 computed style；
- NetSurf layout 和 redraw；
- GDI 绘制、命中、滚动和动态 viewport/DPI；
- 链接、表单、文本输入、资源发现和一组 DOM 事件；
- 表单提交同时提供显式 submitter 与 text-input 隐式 Enter 路径；两者共享首个 submitter
  的受限 action/method/enctype override 和 multipart snapshot 语义；
- 按 DOM id 查询已布局 form-control 几何/状态，供宿主实现程序化 activation；`PCore_FormResetAt`
  只提交 reset 状态，取消事件由宿主在调用前分发；
- 按 DOM id 查询控件的约束状态（`valid`、`will_validate` 和 `PCORE_VALIDITY_*` flags），供浏览器
  bridge 或其他宿主在布局前后读取；该查询不触发 invalid 事件，也不应用 form-level no-validate
  提交按钮绕过；
- 按 DOM id 聚合查询 form 的约束状态（`PCore_FormValidationById`），供浏览器 bridge 或其他
  宿主实现受限的 form-level `checkValidity()`；该查询忽略 form 的 `novalidate`，跳过不参与
  constraint validation 的控件，不依赖布局，也不触发 invalid 事件、提交或 native invalid UI；
- 按 DOM id 执行受限的 form `reportValidity()`（`PCore_FormReportValidityById`）：沿用同一候选
  规则，按 DOM 顺序向有非空 id 的 invalid control 派发 trusted、non-bubbling、cancelable
  `invalid` 事件，并返回聚合 valid 状态；不提供 native invalid UI、焦点/滚动、提交或本地化提示；
- 按 DOM id 设置/读取当前控件的 application-owned custom validity message，供脚本 bridge
  实现 `setCustomValidity()` 和 `validationMessage`；支持现有 form-control candidates，不触发
  invalid 事件或 native invalid UI；
- 按 DOM id 读取当前控件的 `validationMessage`（`PCore_FormGetValidationMessageById`）：
  custom message 优先，否则按当前 flags 返回固定英文 fallback；提供完整字节长度和安全截断，
  不做本地化或 native validation UI；
- transport-agnostic 的 URL resolve、fetch 和 free callback。

文档、样式表及其他返回句柄必须使用对应 `PCore_Free*` API 释放。查询接口可能返回
借用指针或借用句柄；调用者必须遵循公共头文件中的具体生命周期说明。

Core 不依赖某个固定网络实现。宿主可以使用 `positron_http`、WinInet、离线 fixture 或其他
传输，只要满足 callback 的同步所有权约定。

### `positron_browser.dll`

这是浏览器运行时的产品组合层，不是窗口或网络实现。当前稳定切片提供独立的
history/session opaque handle、有限同源 URL 判定、文档导航提交、push/replace state、
后退/前进/go 目标和待提交导航投影；另提供由该 DLL 持有的浏览器脚本 session、
host JSON callback 注册、求值和调用生命周期。它依赖 `positron_json.dll` 验证 history state，
并依赖 `positron_script.dll` 持有脚本 context，但不依赖窗口、网络或 WM 控件。

bootstrap、按 id 查询元素、读取/写入 textContent、attribute、input value、checked、`HTMLElement.disabled`/`title`/`lang`/`dir`/`hidden`/`accessKey`/`role`/`ariaLabel`/`contentEditable`、表单属性 `name`/`action`/`method`/`enctype`/`target`/`autocomplete`/`acceptCharset`、submitter `formAction`/`formMethod`/`formEnctype`、控件 `placeholder`/`autocomplete`/`inputMode`/`type`、控件约束查询、form-level `checkValidity()`/`reportValidity()` 聚合查询与 invalid-event dispatch、custom validity query/set、约束相关
`required`/`readOnly`/`multiple`/`noValidate`/`formNoValidate`/`min`/`max`/`step`/`pattern`/`minLength`/`maxLength`、form property
（defaultValue/defaultChecked/selectedIndex）、navigation、同文档 location/history 事件、event 的
DOM JSON 分发以及 native input/composition/keyboard/focus-family/EDIT-change/post-change-input/click/
programmatic `HTMLElement.click()`（包括 file input 的 typed click 边界）/`checkValidity()`/`reportValidity()`/`willValidate`/`validity` 查询/`setCustomValidity()`/`validationMessage`、submit/reset/invalid/file-input/checkbox/radio input/change/SELECT-input/change
typed dispatch entry 已由此 DLL 持有并执行；
其余 form/input 适配和页面生命周期会逐步迁入此 DLL。窗口、传输、native EDIT/SELECT、系统文件选择器、core
事件传播、焦点/控件默认行为、history/navigation side effect 和绘制调度仍由应用宿主负责。
当前 raw metadata bridge 还提供 `HTMLElement.draggable` 的 UTF-8 属性往返；这不等于拖放手势或
完整 HTMLElement Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLElement.tabIndex` 的有限整数往返（缺失或非法值回落
`-1`）；这不等于焦点导航、滚动、键盘顺序或完整 HTMLElement Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLInputElement.accept` 的 UTF-8 属性往返；这不等于文件
类型解析、过滤、系统 picker 或完整 input Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLInputElement.capture` 的 UTF-8 属性往返；这不等于摄像头/
麦克风捕获、文件类型过滤、系统 picker 或完整 input Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLInputElement.dirname` 的 UTF-8 属性往返；这不等于提交
方向字段、编码行为或完整 input Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLInputElement.list` 的 UTF-8 属性往返；这不等于 datalist
解析、建议项、自动完成或完整 input Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLTextAreaElement.wrap` 的 UTF-8 属性往返；这不等于软/硬
换行布局、提交编码差异或完整 textarea Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLElement.htmlFor` ↔ `for` 的 UTF-8 属性往返；这不等于
label 关联、焦点转移或完整 HTMLElement Web IDL 实现。`className` 已有独立的 class/classList
描述符，不得重复定义。
当前 raw metadata bridge 还提供 `HTMLElement.slot` ↔ `slot` 的 UTF-8 属性往返；这不等于
Shadow DOM 或 slot 分配。
当前 raw metadata bridge 还提供 `HTMLElement.itemId` ↔ `itemid` 的 UTF-8 属性往返；这不等于
microdata 解析或语义树。
当前 raw metadata bridge 还提供 `HTMLElement.itemProp` ↔ `itemprop` 的 UTF-8 属性往返；这不
等于 microdata token 解析或语义树。
当前 raw metadata bridge 还提供 `HTMLElement.itemRef` ↔ `itemref` 的 UTF-8 属性往返；这不
等于 microdata 引用解析或语义树。
当前 raw metadata bridge 还提供 `HTMLElement.itemScope` ↔ `itemscope` 的布尔往返；这不等于
microdata item 解析或语义树。
当前 raw metadata bridge 还提供 `HTMLElement.itemType` ↔ `itemtype` 的 UTF-8 属性往返；这不
等于 microdata vocabulary 解析或语义树。
当前 raw metadata bridge 还提供 `HTMLElement.nonce` ↔ `nonce` 的 UTF-8 属性往返；这不等于
CSP nonce 校验、安全策略、脚本执行或完整 HTMLElement Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLElement.part` ↔ `part` 的 UTF-8 属性往返；这不等于
Shadow DOM 部件导出、CSS 选择器语义或完整 HTMLElement Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLElement.exportParts` ↔ `exportparts` 的 UTF-8 属性
往返；这不等于 Shadow DOM 部件导出算法或完整 HTMLElement Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLElement.inert` 的布尔属性往返；这不等于焦点、键盘、
无障碍树或完整 HTMLElement Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLElement.popover` ↔ `popover` 的 UTF-8 属性往返；这不
等于 popover 显示/隐藏、焦点管理、top-layer 或完整 HTMLElement Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLElement.autofocus` 的布尔属性往返；这不等于焦点调度、
窗口激活或完整 HTMLElement Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLInputElement.enterKeyHint` ↔ `enterkeyhint` 的 UTF-8
属性往返；这不等于 SIP、键盘布局、输入法策略或完整 input Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLInputElement.virtualKeyboardPolicy` ↔
`virtualkeyboardpolicy` 的 UTF-8 属性往返；这不等于 SIP、虚拟键盘策略执行或完整 input Web
IDL 实现。
当前 raw metadata bridge 还提供 `HTMLInputElement.webkitDirectory` 的布尔属性往返；这不等于
目录 picker、目录选择语义或完整 input Web IDL 实现。
当前 raw metadata bridge 还为 `HTMLInputElement.size` 提供有限整数属性往返；这不等于默认
20、控件宽度、范围钳制或完整 input Web IDL 实现。
当前 raw metadata bridge 还为 `HTMLTextAreaElement.cols` 提供有限整数属性往返；这不等于
textarea 布局宽度或完整 textarea Web IDL 实现。
当前 raw metadata bridge 还为 `HTMLTextAreaElement.rows` 提供有限整数属性往返；这不等于
textarea 布局高度或完整 textarea Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLElement.open` 的布尔属性往返；这不等于 details 展开
布局、summary 激活、disclosure 交互或完整 HTMLElement Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLElement.autocapitalize`、`itemValue`、`is` 的 UTF-8
属性往返；这不等于输入法/大小写策略、microdata 解析或 customized built-in 升级。
当前 raw metadata bridge 还提供 `HTMLElement.ariaAtomic`、`ariaBusy`、`ariaChecked`、
`ariaCurrent`、`ariaDescription`、`ariaDisabled`、`ariaExpanded`、`ariaHasPopup`、`ariaHidden`、
`ariaKeyShortcuts`、`ariaLabelledBy`、`ariaLevel`、`ariaLive`、`ariaModal`、`ariaPlaceholder`、
`ariaPressed`、`ariaSelected` 与对应 `aria-*` 的 UTF-8 属性往返；这不等于 ARIA 语义计算、
可访问性树、辅助技术通知、焦点/交互或完整 HTMLElement Web IDL 实现。
当前 raw metadata bridge 还提供 `HTMLElement.ariaColCount`、`ariaColIndex`、`ariaColIndexText`、
`ariaControls`、`ariaDescribedBy`、`ariaDetails`、`ariaErrorMessage`、`ariaFlowTo`、`ariaInvalid`、
`ariaMultiLine`、`ariaMultiSelectable`、`ariaOrientation`、`ariaOwns`、`ariaPosInSet`、
`ariaReadOnly`、`ariaRelevant`、`ariaRequired`、`ariaRoleDescription`、`ariaRowCount`、
`ariaRowIndex` 与对应 `aria-*` 的 UTF-8 属性往返；这不等于 ARIA 语义计算、可访问性树、
辅助技术通知、焦点/交互或完整 HTMLElement Web IDL 实现。
`test_host.exe` 只通过公共 API 组合和验证这些能力，不拥有 product history、script context
或 bootstrap 文本。

#### next402–421 的脚本 session 扩展

本轮新增能力仍归 `positron_browser.dll` 的产品 session 所有，而不是 `test_host.exe` 的私有
实现：

- 页面生命周期与环境快照：readyState、`hidden`/`visibilityState`、`window.name`、受控
  navigator 字段，以及 `pagehide`/`pageshow`/`visibilitychange`；
- 受限的 `URLSearchParams`、`URL`、session `Storage`、session cookie、`classList`、style
  declaration、selector query 和 `FormData`；
- input selection、numeric step、`setRangeText`、document metadata、窗口/viewport/scroll
  状态和 target-local synthetic event；
- 宿主显式泵送的异步队列：
  `PBrowser_ScriptSessionRunTimers`、`PBrowser_ScriptSessionRunAnimationFrames` 和
  `PBrowser_ScriptSessionDispatchVisibility`。

这些 API 都是同步、session-scoped、受预算限制的兼容切片。计时器和 animation frame 不创建
后台线程，viewport/scroll 不直接驱动 layout/paint，visibility 不自动连接操作系统窗口生命周期；
宿主必须在合适的时钟、导航和窗口事件上调用对应入口，并在 session 关闭或导航替换时丢弃队列。
selector、URL、storage/cookie、FormData、selection 和 synthetic event 均不承诺完整 Web
标准语义。精确参数、返回值和所有权以 `positron_browser/positron_browser.h` 为准。

#### next422–441 的脚本平台扩展

本轮仍由 `positron_browser.dll` 持有实现，`test_host.exe` 只通过公共 session API 组合 fixture
和断言：

- 事件层增加 listener options（capture/once/passive/signal）、通用 `EventTarget`、
  `CustomEvent`、Mouse/Keyboard/Input/Focus/Submit/MessageEvent 构造器、AbortController 和
  元素 handler 属性；它们只在产品注册表中分发，不伪装完整 DOM 冒泡树或 native 默认动作。
- 异步层增加 `PBrowser_ScriptSessionRunMicrotasks`、`RunIdleCallbacks`、`RunMessages`，以及
  对应 `queueMicrotask`、`requestIdleCallback`、同窗口 `postMessage` 队列。宿主拥有时钟、pump
  顺序和 session 关闭/导航时的丢弃策略；产品不创建后台线程。
- 脚本 bootstrap 增加 atob/btoa、UTF-8 TextEncoder/TextDecoder、bounded Blob/File 和
  FormData 文件值、URL `canParse`/`parse`/`toJSON`、稳定 URLSearchParams iterator、navigator
  capability snapshot、静态 matchMedia、performance mark/measure、history
  scrollRestoration/location JSON 与 storage event。
- 为遵守 `positron_script` 的 `PSCRIPT_MAX_SOURCE_BYTES`，bootstrap 由公共初始化入口按顺序
  评估三个独立 IIFE；每段都在既有 source limit 内，仍共享同一个 script context，不改变执行预算。

这些 API 都是 session-scoped、内存 bounded 的兼容切片。Blob `text()`/`arrayBuffer()` 为同步
适配，Promise、fetch、stream、真实文件句柄、跨页面 storage 同步、动态 media re-evaluation 和
完整 URL/DOM 标准均明确不在本边界内。新增 C ABI 只追加稳定导出，不暴露 Duktape、libdom 或
宿主私有结构。

#### next442–461 的脚本平台扩展

本轮继续把完整但受控的页面数据/异步互操作能力放在 `positron_browser.dll` 的 bootstrap 中，
`test_host.exe` 只通过公共 session 入口提供 fixture 与断言：

- 事件与 DOM：对象 `handleEvent` listener、Event `initEvent`/`composed`/`cancelBubble`/
  `returnValue`，受控 `DOMException`，`dataset` data-* 反射，以及 document/element node 常量；
- 数据集合：FormData 的 Symbol.iterator，大小受限且不联网的 Headers（case-insensitive、append/
  set/get/delete、forEach/iterator），同步 Request/Response 元数据和 bounded body helper；
- 取消与调度：AbortSignal `timeout`/`any`/`onabort`，timer callback extra arguments、
  `setImmediate`，以及通过既有 `RunMessages` pump 派送的 MessageChannel/MessagePort；
- 工具快照：受限 `structuredClone`、navigator `javaEnabled`/`sendBeacon` 能力快照、初始化时派生的
  `screen.orientation`，以及 URLSearchParams 的 pair sequence constructor 和按值 delete。

这一批没有引入网络、Promise、fetch、stream、后台线程、DOM 树关系或旋转/布局副作用。Request/
Response/Headers、Blob/File、structuredClone 都是 session 内存 bounded 的同步适配；MessageChannel、
timer 和 AbortSignal timeout 必须由宿主显式泵送。Bootstrap 由公共初始化入口按顺序评估四个独立 IIFE，
保持既有 `PSCRIPT_MAX_SOURCE_BYTES` 和执行预算不变。

#### next462–481 的脚本平台扩展

本轮继续由 `positron_browser.dll` 持有产品语义，`test_host.exe` 只通过公共 session 入口提供
fixture、泵送和断言。新增的能力边界包括：

- 编码与 body：`TextEncoder.encodeInto()` 的容量受限写入、`TextDecoder` 的 fatal/ignoreBOM
  选项快照、同步 `Request.prototype.json()`/`Response.prototype.json()`、Blob-backed Request
  clone，以及 Headers `getSetCookie()` 和 canonical iterator 视图；这些都不建立网络、Promise
  或文件句柄。
- 集合与 DOM：Storage named properties 和 detached `toJSON()` snapshot、classList/style 的
  Symbol.iterator、`toggleAttribute()`，以及 `ownerDocument`/`isConnected`/`nodeValue` 元数据。
  iterator 与 storage 仍是 session 内快照/Proxy，不引入完整 DOM tree 或通用 createElement。
- 事件与窗口：Storage/HashChange/PopState/Error/Progress/Close event 构造器，document.defaultView
  和同一 bounded global 的 window aliases；`open()` 返回 null、`close()` 为 no-op，不创建新窗口。
- 通信与观测：MessagePort close/messageerror、同 session `BroadcastChannel` 和
  `PerformanceObserver` 的同步 snapshot/takeRecords。消息仍通过既有 `RunMessages` pump，观测
  不监听未来异步 entries，不引入后台线程或跨页面通信。
- 取消：`AbortSignal.abort(reason)` 与 `throwIfAborted()` 的同步 reason 传播。

本批 bootstrap 由公共初始化入口按顺序评估五个独立 IIFE，仍共享同一 Duktape context，并保持
`PSCRIPT_MAX_SOURCE_BYTES`、执行预算、opaque handle 和既有 C ABI 不变。所有新增状态都是
session-scoped、内存 bounded；完整 Web IDL、网络/fetch/stream、transferable、持久化 storage、
真实窗口生命周期和完整 DOM 树仍明确不在此边界内。

#### next482–501 的脚本平台扩展

本轮继续由 `positron_browser.dll` 持有产品语义，`test_host.exe` 只通过公共 session 入口提供
fixture、pump 和断言。新增边界包括：

- Blob/File metadata 的 `Symbol.toStringTag` 与受限 `slice()` 边界；FormData 的独立
  `entries()`/`keys()`/`values()` snapshot、forEach snapshot 和兼容 `length` 字段；
- Request/Response 同步 one-shot body readers（`text()`/`json()`/`arrayBuffer()`）与已消费
  clone 错误；URL authority userinfo、默认 HTTP(S) 端口归一化、userinfo mutation 序列化和
  URLSearchParams 按值 `has()`；cookie `Max-Age=0` 删除；
- NodeList/HTMLCollection-like `item()`/`namedItem()`、`forEach()`、`keys()`、`values()`、`entries()`/
  iterator、重复 `getElementById()` 的稳定 wrapper identity、dataset
  named keys/`toJSON()`；Event phase constants/timestamp 与 dispatch 后 state reset；
- MessagePort started/closed、BroadcastChannel clone error/closed、PerformanceObserverEntryList
  的 indexed/iterable/`toJSON()` snapshot，以及 performance `clearResourceTimings()`/`toJSON()`。

这些能力都是单 session、内存 bounded 的同步或宿主显式 pump 语义，不引入网络、Promise、stream、
完整 URL Standard、DOM tree、后台线程、跨页面通信、真实窗口生命周期或 native 输入副作用。公共
初始化入口现在按顺序评估六个独立 IIFE，共享同一 Duktape context，并保持
`PSCRIPT_MAX_SOURCE_BYTES`、既有执行预算、opaque handle 和 C ABI 不变。

#### next502–521 的脚本平台互操作性强化

本批仍由 `positron_browser.dll` 持有产品语义，`test_host.exe` 只通过公共 session 入口提供
fixture、显式 pump 和断言；没有新增公共 C ABI。新增边界包括：

- Headers 的独立 iterator/forEach snapshot；Request/Response clone 对内存 body、headers 和
  bounded metadata 的 ownership 隔离，并提供诊断用 JSON snapshot；
- URLSearchParams 的 mutation-safe iterator/forEach，FormData 对无名 Blob 的默认 `blob` 文件名
  和显式 filename 保留；
- Storage/DOM wrapper 的 `Symbol.toStringTag`、classList 空白/空 token 的 SyntaxError 边界、
  classList/style identity，以及 dataset tag；
- performance entry 的 `toJSON()`、PerformanceObserver 的 `supportedEntryTypes` 与冲突/空
  `observe()` 选项校验；MessagePort 的 `onmessage` 自动 `start()`；AbortSignal/Controller 和
  Blob/File 的 bounded metadata tags/JSON。

这些语义全部 session-scoped、内存 bounded；Request/Response 不联网，MessagePort 和
PerformanceObserver 仍依赖宿主显式 pump 或同步快照。它们不等价于完整 Fetch、Streams、DOM
tree、Web IDL serialization 或后台浏览器调度。公共 bootstrap 现在按顺序评估七个独立 IIFE，
共享同一 Duktape context，并保持 `PSCRIPT_MAX_SOURCE_BYTES`、既有执行预算、opaque handle 和
C ABI 不变。

#### next522–541 的受控 Promise 扩展

本批仍由 `positron_browser.dll` 持有产品语义，`test_host.exe` 只通过公共 session 入口提供
fixture、显式 microtask pump 和断言；没有新增公共 C ABI。bootstrap 第八个 IIFE 提供：

- bounded Promise 构造器、`then`/`catch`/`finally` 链和同步 executor one-shot 语义；
- `resolve`/`reject`、thenable assimilation，以及 `all`/`race`/`allSettled`/`any` 组合器；
- 构造器错误、抛出 handler、rejection recovery、AggregateError-like 全拒绝结果、Promise
  `Symbol.toStringTag` 和 64 项 handler/input 限制。

Promise reaction 只进入现有 session 的 `queueMicrotask` 队列，由宿主显式调用公共
`PBrowser_ScriptSessionRunMicrotasks()`（内部转发到 bootstrap pump）推进；产品不创建后台线程、隐式 event loop、网络、fetch、stream、
文件句柄或跨 session 调度。组合器当前接受 bounded array-like 输入，超限或非法输入受控拒绝。
这些语义是内存内兼容切片，不代表完整 ECMAScript Promise/iterator/host scheduling 标准；公共
初始化入口现在按顺序评估九个独立 IIFE，仍共享同一 Duktape context，并保持
`PSCRIPT_MAX_SOURCE_BYTES`、既有执行预算、opaque handle 和 C ABI 不变。

#### next542–561 的 DOM 关系与表单集合边界

本批把树关系的最小可用纵切放在正确的产品层，而不是留在 `test_host.exe`：

- `positron_core.dll` 导出 `PCore_NodeRelationById()`。调用者用文档句柄、元素 id、关系常量
  和可选索引查询父元素、首/尾子元素、前/后兄弟、子元素数量、tag/name、form owner，或按
  DOM 顺序查询 form control 数量/项。字符串结果使用 UTF-8 probe/truncation 约定，计数通过
  `out_number` 返回；缺失 id、越界和不支持关系都以明确的 fail-closed 结果返回。
- `positron_browser.dll` 通过独立的 size-tagged `PBrowserScriptDomRelationCallbacks` 注册
  该查询，并在 bootstrap 中包装为 `parentElement`、`firstChild`/`lastChild`、兄弟节点、
  `children`/`childElementCount`、`tagName`/`nodeName`/`localName`、`contains()`、基础
  `compareDocumentPosition()`、受限 `matches()`/`closest()`、元素作用域
  `querySelector()`/`querySelectorAll()` 以及 form `elements` 的 `item()`/`namedItem()`。
- 关系对象是同一脚本 session 内稳定的 wrapper，但底层查询是 ID-addressable、同步、只读
  snapshot。此批不提供通用 Node mutation、文本节点遍历、shadow tree、复杂 CSS selector、
  layout/native control 查询，也不把 libdom 类型暴露到公共 ABI。

`test_host.exe` 只实现 callback adapter、fixture 和 TEST542–561 断言；它不是关系 API 的所有者。
这组 API 仅在显式 `javascript=1` 的 browser session 中可见，默认 Browse 路径不变。

#### next562–581 的属性集合边界

本批继续沿用同一产品分层，把属性集合查询和受控属性 wrapper 放在 core/browser DLL，而不是
放进 `test_host.exe`：

- `positron_core.dll` 的 `PCore_NodeRelationById()` 增加 attribute count、name-at 和 value-at
  三个关系常量。调用者仍使用 UTF-8 probe/truncation 和 `out_number` 计数约定；属性按 libdom
  parser order 枚举，越界、缺失 id 和 DOM 错误 fail closed。
- `positron_browser.dll` 复用现有 relation callback 槽位，在第十个 bootstrap IIFE 中提供
  `Element.getAttributeNames()`、`hasAttributes()`、`attributes`、`getAttributeNode()`、
  `setAttributeNode()` 和 `removeAttributeNode()`；NamedNodeMap 提供 `length`、`item()`、
  `getNamedItem()`、`setNamedItem()`、`removeNamedItem()`、iterator 和 indexed slots 0–7。
  Attr wrapper 提供 `nodeType`、`nodeName`、`name`、`value`、`nodeValue`、`specified`、
  `ownerElement` 和稳定 identity；value/nodeValue mutation 复用既有 attribute callback，
  跨 owner 绑定和缺失删除返回 null，不伪造节点转移。
- 该集合是同步、ID-addressable、session-scoped 的 bounded view；不暴露 libdom 对象，不实现
  namespaces、prefix/localName、通用节点创建、live collection、完整 Web IDL descriptors、
  layout 或 native control side effect。浏览器 session 为这层 bootstrap 使用 576 KiB heap
  ceiling；独立 `positron_script` context 的 512 KiB 默认值和公共 ABI 不变。

`test_host.exe` 只实现 callback adapter、fixture 和 TEST562–581 断言；它不是属性 API 的所有者。
这组 API 仍只在显式 `javascript=1` 的 browser session 中可见，默认 Browse 路径不变。

#### next582 的 childNodes 与 CharacterData 边界

本批仍把 DOM 语义放在 core/browser 产品 DLL，只让 `test_host.exe` 提供 fixture、callback
adapter 和断言：

- `positron_core.dll` 的 `PCore_NodeRelationById()` 追加 `CHILD_NODE_*` 关系常量。调用者
  可以按 ID-addressable 元素查询所有直接 childNodes 的数量、节点类型、节点名、节点值、
  textContent 和可用的子元素 id；文本、注释和无 id 元素不会被旧的 `children` 关系过滤掉。
  返回仍遵循 UTF-8 probe/truncation、`out_number` 和 0/2/1 的成功/缺失/错误约定。
- `positron_browser.dll` 在第十一个 bootstrap IIFE 中包装这些关系，提供有界 `childNodes`
  NodeList、`item()`/iterator、稳定的 text/comment/id-less element wrapper、`nodeType`/
  `nodeName`/`nodeValue`/`textContent`/`data`/`length`、`substringData()`、父子/兄弟以及
  `firstElementChild`/`nextElementSibling` 等 element-sibling 视图，并补齐 `Node` 常量。
- childNodes 是同步、session-scoped、只读 snapshot；有 id 的元素复用既有 element wrapper，
  其余节点用 owner+index 的不透明内部 token 表示。它不提供通用文本节点 mutation、节点创建、
  live collection、shadow tree、布局或 native control side effect。浏览器 bootstrap 现在按
  十一个顺序 IIFE 评估，576 KiB browser heap ceiling 和独立 script 的 512 KiB 默认值不变。

`test_host.exe` 只实现 callback adapter、fixture 和 TEST582–601 断言；这组 API 仍只在显式
`javascript=1` 的 browser session 中可见，默认 Browse 路径不变。

#### next583 的 Node 身份、根节点与位置边界

本批不扩展 core relation ABI，而是在既有 childNodes snapshot 上补齐一组只读 Node 兼容方法：

- `positron_browser.dll` 为 ID-addressable element、文本/注释/id-less element wrapper 和
  `document` 提供稳定的 `isSameNode()`、受限 `isEqualNode()`、`getRootNode()`、
  `compareDocumentPosition()` 与 `contains()`；未知对象、跨快照或缺失 parent 链返回
  `false`/`33`，不伪造节点身份。
- `Node` 增加六个 document-position 常量。元素、字符数据与 document 暴露必要的
  `nodeType`/`nodeName`/`nodeValue`/`ownerDocument`/`parentNode`/`isConnected` 元数据；
  `getRootNode({composed: ...})` 在当前无 shadow tree 的边界内返回同一 session document。
- 位置计算只沿当前 ID-addressable parent 与 childNodes snapshot 走，支持同一受控树内的
  祖先、直接字符/元素子节点和兄弟顺序；不实现完整 document order、shadow DOM、节点创建、
  live collection 或 mutation。`isEqualNode()` 也只比较当前 bounded metadata/text snapshot，
  不声称完整 Web IDL 深结构相等。
- bootstrap 现在按十二个顺序 IIFE 评估，browser session 仍使用 576 KiB heap ceiling，
  独立 `positron_script` 默认堆仍为 512 KiB。`test_host.exe` 只提供 fixture、adapter 和
  `TEST602–621` 断言。

本批仍只改变脚本状态/API，不触及窗口绘制、真实触摸、SIP、旋转、系统 picker 或网络；默认
`javascript=0` 路径不变，也不需要人工页面验收。

#### next584 的 DOM 集合遍历边界

本批不扩展 core relation ABI，而是在既有同步 DOM snapshot 上补齐集合协议：

- `positron_browser.dll` 为 `childNodes`、`children`、`form.elements` 和元素作用域
  `querySelectorAll()` 结果提供 `forEach()`、`keys()`、`values()`、`entries()`、可复用的默认
  iterator 以及 `Symbol.toStringTag`。`children` 和 `form.elements` 保留 `item()`/`namedItem()`；
  元素作用域查询使用 `NodeList` 类型标识，避免把 HTMLCollection 的命名查找混入查询结果。
- 迭代器在创建时固定当前集合长度，返回值、键和 `[index,value]` 对都只引用当前 session 内的
  wrapper snapshot；非函数 `forEach` callback 抛出 `TypeError`，不会改变集合或宿主 DOM。
- 集合仍是同步、只读、session-scoped 的有限数组视图，不提供 live 更新、节点创建、通用 DOM
  mutation、完整 Web IDL descriptor、复杂 selector、layout 或 native control side effect；
  `test_host.exe` 只提供 fixture、callback adapter 和 `TEST622–641` 断言。
- bootstrap 现在按十三个顺序 IIFE 评估，browser session 仍使用 576 KiB heap ceiling，独立
  `positron_script` 默认堆仍为 512 KiB；本批不涉及窗口绘制、真实触摸、SIP、旋转、系统 picker
  或网络，因此不新增人工页面验收。

#### next585 的 document 结构节点边界

本批为既有 ID-addressable DOM snapshot 增加最小的文档结构入口，仍把产品语义放在 core/browser
DLL，`test_host.exe` 只提供 fixture、callback adapter 和断言：

- `positron_core.dll` 在既有 `PCore_NodeRelationById()` 入口上增加三个保留结构 token：
  `__positron_document_element__`、`__positron_document_head__` 和
  `__positron_document_body__`。真实 HTML `id` 查找优先于 token fallback；因此普通页面 id
  仍保持原有身份，结构 token 只映射 parser 得到的 document root 及其直接 `head`/`body`
  元素。既有 UTF-8 probe/truncation、0/2/1 返回和 opaque document ownership 不变。
- `positron_browser.dll` 将这三个 token 暴露为稳定的 `documentElement`、`head`、`body`
  wrapper，并让已有 parent/child/sibling、`children`/`childNodes`、元素作用域 selector、
  identity/root/position/contains 和 collection protocol 复用同一 wrapper cache。文档级
  `querySelector()`/`querySelectorAll()` 仅增加 `html`、`:root`、`head`、`body` 四种结构
  查询；其他复杂 document selector 继续 fail closed。
- `documentElement.parentNode` 返回当前 document，而 `parentElement` 为空；`head`/`body`
  的 parent/sibling/children 关系则沿结构 token 返回。结构节点没有伪造 HTML `id`，不引入
  通用节点创建、outerHTML、DOM mutation、live collection、shadow tree、layout 或 native
  control side effect。
- 本批使原先因缺少 root parent 而被视为 disconnected 的 bounded ID 子树能够在同一 body
  snapshot 中排序；因此 `TEST549` 的 root/form 位置断言从 fail-closed `33` 更新为同一文档
  中的顺序值 `4`，这是结构入口的预期语义扩展而不是放宽断言。

本批 bootstrap 仍按十三个既有 IIFE 顺序评估，browser session heap ceiling 为 576 KiB，独立
`positron_script` 默认堆为 512 KiB。`TEST642–661` 只验证同步脚本 API 和 DOM snapshot，
不触及窗口绘制、真实触摸、SIP、旋转、系统 picker 或网络，因此不新增人工页面验收。

#### next586 的 DocumentType 边界

next586 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation
ABI。浏览器 session 为 `document.doctype` 创建一个稳定、只读的 synthetic wrapper，提供
`name`、`nodeType`、`nodeName`、owner/root/position/contains、identity/equality 和字符串
brand；它是没有 child 的 leaf。document 的 `childNodes` 以 `[doctype, documentElement]` 为顺序
快照，`document.children` 仍是 element-only，因此 doctype 不会伪装成普通 HTML element。

该边界不承诺完整 HTML doctype parser、public core doctype token、节点创建、mutation、live
collection、outerHTML 或完整 document tree；wrapper 只复用既有 session-scoped relation/Node
snapshot 和 fail-closed position semantics。`test_host.exe` 仅提供 fixture、callback adapter
和自动断言，不拥有该 API。browser bootstrap 仍为十三个 IIFE，session heap ceiling 仍为
576 KiB，独立 `positron_script` 默认堆仍为 512 KiB；本批不涉及视觉、触摸、SIP、picker、旋转
或网络，因此不新增人工页面验收。

#### next587 的 Node URL 与 namespace 边界

next587 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation
ABI。browser-owned 的 document、DocumentType、HTML element、CharacterData 和 Attr wrapper
提供只读 `baseURI`、`namespaceURI`、`prefix`、`lookupNamespaceURI()` 与
`isDefaultNamespace()`。`baseURI` 读取当前 session 的 `location.href`，因此在受控
`history.replaceState()` 后反映新的 URL；它不是完整 URL Standard parser，也不建立新的网络
或文档生命周期。

HTML element wrapper 的 `namespaceURI` 为 HTML namespace；document、DocumentType、文本/注释
和普通 Attr wrapper 的 namespace 视图为 null。`xml` prefix 映射到 XML namespace，未知 prefix
和不支持的 namespace 请求返回 null/false；Attr 的有限查询沿 owner element 上下文工作。
`prefix` 为只读 null。该边界不实现 XML/namespace parser、`createElementNS()`、prefix 或
namespace mutation、完整 namespace tree、live collection 或通用 DOM mutation；`test_host.exe`
只提供 URL fixture、callback adapter 和自动断言，不能成为 API 所有者。browser bootstrap 仍为
十三个 IIFE，session heap ceiling 为 576 KiB，独立 `positron_script` 默认堆仍为 512 KiB；
本批仅覆盖同步脚本 API/DOM snapshot，不新增人工页面验收。

## 独立 JavaScript 与浏览器 JavaScript

项目只有一套 JavaScript 引擎实现：`positron_script.dll` 内的 Duktape。

“独立 JavaScript”指普通应用直接创建 `PScript` context，执行与网页无关的脚本。
“浏览器 JavaScript”指产品浏览器层和宿主在显式开关开启时：

1. browser layer 持有 `positron_script` context，并按 DOM 顺序驱动 classic inline/external script；
2. browser layer 通过稳定 ABI 注册宿主提供的 typed DOM 读写/attribute/value/checked/`HTMLElement.disabled`/`title`/`lang`/`dir`/`hidden`/`accessKey`/`role`/`ariaLabel`/`contentEditable`/validation query（包括 form-level 聚合）/custom-validity/form/constraint-related reflected properties（含 `name`、form `action`/`method`/`enctype`/`target`/`autocomplete`/`acceptCharset`、submitter `formAction`/`formMethod`/`formEnctype`、控件 `placeholder`/`autocomplete`/`inputMode`/`type`、`pattern`/`minLength`/`maxLength`）/form-property/navigation 适配，承接同文档 location/history 事件分发和 native input/composition/keyboard/focus/EDIT-change/post-change-input/click/programmatic-click（file input 只承接 typed click，系统 picker 仍由宿主触发）/submit-reset/invalid/file-input/checkbox/radio input/change/SELECT-input/change dispatch contract，并逐步承接其余表单适配；
2a. report-validity callback 只返回同步 valid 结果并路由可寻址控件的 trusted `invalid` 事件；
    它不负责 native invalid UI、焦点/滚动或表单提交。
3. browser layer 持有并执行产品 bootstrap；后续把其余 form/input callback 实现从 `test_host` 迁入 browser layer；
4. 宿主继续提供资源、窗口和控件回调，browser layer 在页面提交、失败或关闭时释放 context 和 bridge。

因此浏览器绑定不是第二个引擎，也不应把 Duktape 或 libdom 类型暴露成公共 ABI。当前
history/session、脚本 context 所有权、bootstrap 和 DOM 读写/attribute/value/checked/disabled/validation-query/custom-validity/constraint-reflection/form-property、ID-addressable
DOM relation/form collection、navigation/location-event/native-input/keyboard/focus/EDIT-change/post-change-input/click/programmatic-click/
submit-reset/invalid/report-validity/file-input/checkbox-radio-change/SELECT-input/change dispatch entry 已进入
`positron_browser.dll`，其余 DOM bridge 仍在迁移中且默认关闭；
不能将其描述为完整 `window`、DOM、Web API 或 URL Standard 实现。

## ABI 与所有权原则

- 公共导出保持 `extern "C"` C ABI，兼容 MSVC 9.0 和 ARMV4I。
- 文本接口使用 UTF-8；UTF-16 只留在 WM UI/消息边界。
- 外部只能看到 opaque handle、size-tagged data 或明确稳定的结构体。
- 在哪个 DLL 分配的内存，就由该 DLL 提供的释放函数回收。
- 借用结果必须标注有效期，不能由调用者释放。
- 新增接口优先兼容扩展；不能通过暴露第三方对象快速绕过边界。
- `test_host` 可以演示和验证 API，但不得成为只有它才能使用的隐含接口。

## 平台与移植策略

目标工具链是 VS2008 SP1、Windows Mobile 6 Professional SDK、C89/ARMV4I。上游 C99
代码通过仓库脚本做可重复转换；不得在正式构建之外维护另一套手工编译路径。

WM6 已有且足够的能力优先复用，例如 GDI、WinInet、WM Imaging、CryptoAPI 和 native
EDIT/SELECT 控件。系统能力无法满足现代要求时，再封装或移植成熟上游组件。

每个第三方组件必须记录：

- 固定版本或提交；
- 官方来源；
- 原始许可证；
- Positron 本地补丁或生成过程；
- 与公共 DLL 的隔离方式。

根 [`THIRD_PARTY.md`](../THIRD_PARTY.md) 是第三方总索引，各组件目录中的 `UPSTREAM.md`
或 `POSITRON_PORT.md` 记录局部来源和移植差异。

## 非目标

当前架构不承诺：

- 完整现代浏览器兼容性；
- 完整 HTML、CSS、DOM、SVG、URL 或 Web API 标准；
- 在 UI 线程之外并发操作 DOM/GDI；
- 让应用直接链接 NetSurf、Duktape 或其他 vendored 内部 ABI；
- 用 `test_host` 的私有行为替代公共 DLL 设计。

具体尚未实现的能力见
[`.agents/KNOWN_LIMITATIONS.md`](../.agents/KNOWN_LIMITATIONS.md)。

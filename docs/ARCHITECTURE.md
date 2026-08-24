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
              +-- typed programmatic activation, native EDIT transaction and native SELECT commit/focus/
                  single-select dropdown transaction policy

Browser host = composition of positron_browser + positron_core
               + positron_script + networking + native WM controls
```

内部 NetSurf、libdom、libcss、libhubbub、libsvgtiny 等静态库被封装在产品边界后面。
应用程序不应包含它们的头文件，也不应依赖其符号或对象布局。

## 公共 DLL

### `positron_tls.dll`

提供两个刻意分开的 TLS 1.2 信任模型。普通 HTTPS 客户端使用嵌入式/调用方追加 CA、
证书链和主机名验证，默认产品路径是 `PTls_ConnectVerified`；`PTls_Connect` 跳过验证，
只保留给明确的诊断或兼容场景。

ABI v2 还提供 LocalSend 一类局域网 peer 所需的公共能力：持久 ECDSA P-256 自签名身份、
证书 DER SHA-256 指纹、携带客户端证书的 peer connect、握手返回前的指纹钉扎，以及可选择
强制客户端证书的 IPv4 TLS listener。NULL/空 pin 仅表示 discovery/TOFU，不能视为已经认证；
已配对流量必须传入预期指纹。peer 身份属于应用的长期状态，不属于 `test_host` 或 HTTP
模块。

初始化和清理成对进行；连接、listener 和 identity 分别由匹配的 close 入口释放。身份必须
活得比引用它的 listener/connection 更久；销毁顺序是 connection、listener、identity、全局
cleanup。额外根证书必须在并发连接开始前加入。线程、文件安全和失败边界以
[`positron_tls/README.md`](../positron_tls/README.md) 及公共头为准。

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
programmatic `HTMLElement.click()`（包括 disabled 抑制、typed click、submit/reset 事件顺序、
submit 验证与取消、以及 file input 的 typed click 边界）/`checkValidity()`/`reportValidity()`/
`willValidate`/`validity` 查询/`setCustomValidity()`/`validationMessage`、submit/reset/invalid/
file-input/checkbox/radio input/change/SELECT-input/change typed dispatch entry 已由此 DLL 持有并执行；
宿主只通过 size-tagged target/validation/default-action callback 提供控件几何、core 状态与 WM 副作用。
native EDIT 的 `PBrowser_ScriptSessionRegisterNativeEditCallbacksEx()` 另外由 DLL 持有
beforeinput pending metadata、native commit 到 input、dirty tracking 和 blur/change 顺序；宿主
仍提交 native value 并传播 core 事件。native SELECT 的
`PBrowser_ScriptSessionRegisterNativeSelectCallbacksEx()` 现在由 DLL 持有 commit 后的
input→change 顺序、single/multiple 形状校验、bounded target state，以及
`PBrowser_ScriptSessionDispatchNativeSelectFocus()` 的焦点族顺序与 bounded focus state；
`PBrowser_ScriptSessionDispatchNativeSelectInteraction()` 还持有单选下拉的
begin/candidate/confirm/cancel 事务状态，只在确认且观察到候选时给宿主 commit 闸门。宿主仍
提交 Core selection mutation、处理 WM 通知、在取消时恢复原生控件并传播 core 事件。native
SELECT 的 `PBrowser_ScriptSessionDispatchNativeSelectKey()` 现在持有 keydown/keyup 的
stable-token 校验、typed key adapter 复用和 cancel/default-allowed policy；native WM 控件的
真正默认动作、下拉窗口/视觉、composition 生命周期、系统文件选择器、焦点窗口/
绘制调度、history/navigation side effect 和 OEM 平台副作用仍由应用宿主负责；其余 form/input
适配会逐步迁入此 DLL。
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
  selector 在 next589 扩展前仅增加 `html`、`:root`、`head`、`body` 四种结构查询；其他
  document selector 当时继续 fail closed。
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

#### next588 的 HTMLCollection 查询边界

next588 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation
ABI。browser-owned 的 document 与 HTML element wrapper 现在提供
`getElementsByTagName()` 和 `getElementsByClassName()`：HTML tag 查询按大小写归一并支持
`*`，class 查询把规范化后的多个 token 作为合取条件；element 查询沿当前 bounded relation
树按深度优先文档顺序返回并排除 owner，document 查询额外包含 structural `documentElement`。
结果是静态 HTMLCollection snapshot，支持 `item()`、`namedItem()`、`forEach()`、`keys()`、
`values()`、`entries()`、默认 iterator 和 `Symbol.toStringTag`。空白或未知输入返回空集合，
不改变 document tree，也不引入 live 更新、通用 CSS selector、节点创建、mutation、layout 或
namespace 语义。

这组查询只复用现有 browser relation callback 和 wrapper identity，不新增 core ABI；
`test_host.exe` 仅提供 fixture、adapter 和 `TEST702–721` 自动断言。新增 bootstrap 后，
576 KiB browser session ceiling 在既有 TEST540 Promise boundary 上稳定触发内存上限，因此
当前 browser session ceiling 为 608 KiB；独立 `positron_script` context 的 512 KiB 默认堆、
公共 ABI 和断言均未放宽。定向、兼容和相邻回归门分别为 21/21、82/82、301/301，均不涉及
视觉、真实触摸、SIP、旋转、picker 或网络失败反馈，因此不新增人工页面验收。

#### next589 的 document selector 边界

next589 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation
ABI。`document.querySelector()` 与 `document.querySelectorAll()` 现在复用 element 作用域的
bounded matcher 和 wrapper cache：支持 HTML tag（含大小写归一）、`#id`、class、有限
attribute、compound、`*` 和 `:root`；`querySelector()` 返回 DFS 文档顺序中的首个匹配，
`querySelectorAll()` 返回按同一顺序排列的 NodeList snapshot。document root 参与匹配，
head/body 结构 wrapper 保持既有 identity，结果集合沿现有 `item()`、迭代器和
`Symbol.toStringTag` 协议工作。

空白、缺失和包含 `>`、`+`、`~` 的组合器返回 null/空 NodeList；这是 fail-closed 的受限
selector 入口，不是完整 CSS parser，不提供 live collection、节点创建、mutation、layout 或
shadow tree。`test_host.exe` 只提供 fixture、adapter 和 `TEST722–741` 自动断言；本批不新增
core ABI、视觉/触摸/SIP/picker/旋转/网络人工门。browser session 仍使用 608 KiB ceiling，
独立 `positron_script` 默认堆仍为 512 KiB。

#### next590 的 document named collection 边界

next590 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation
ABI。`document.getElementsByName(name)` 在当前 bounded relation snapshot 上精确匹配显式
`name` 属性，按 DFS 文档顺序返回 NodeList snapshot；`document.forms`、`document.images` 和
`document.scripts` 则通过既有 tag traversal 返回静态 HTMLCollection。四种入口都复用当前
`item()`、`namedItem()`、`forEach()`、iterator、`Symbol.toStringTag` 和稳定 wrapper identity，
但每次查询只反映当时的 session snapshot，不提供 live collection、通用 named properties、
节点创建、通用 mutation、完整 HTML parser 或新的 core ABI。

`test_host.exe` 只提供 named-collection fixture、adapter 和 `TEST742–761` 自动断言；本批
定向、兼容和相邻回归门分别为 21/21、122/122、341/341，均不涉及视觉、触摸、SIP、系统
picker、旋转或网络失败人工门。browser session 仍使用 608 KiB ceiling，独立
`positron_script` 默认堆仍为 512 KiB。

#### next591 的 document hyperlink collections 边界

next591 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation
ABI。`document.links` 在当前 bounded traversal 上筛选显式 `href` 的 `a`/`area` 元素，
`document.anchors` 筛选显式 `name` 的 `a` 元素；两者按 DFS 文档顺序返回静态
HTMLCollection，并复用 `item()`、`namedItem()`、迭代协议和稳定 wrapper identity。属性
增删只影响后续查询，不提供 live collection、链接 URL 解析、导航副作用、通用 named
properties、节点创建、通用 mutation 或新的 core ABI。

`test_host.exe` 只提供 hyperlink fixture、adapter 和 `TEST762–781` 自动断言；定向、兼容和
缩减回归门分别为 21/21、142/142、203/203。本批未跑旧的 341 项全回归，但保留核心事件、
TEST540 内存边界、TEST549 和 next642–781 风险区间；不涉及视觉、触摸、SIP、picker、旋转或
网络失败人工门。browser session 仍使用 608 KiB ceiling，独立 `positron_script` 默认堆仍为
512 KiB。

#### next592 的 namespace-aware collection 边界

next592 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation
ABI。document 与 HTML element wrapper 都提供 `getElementsByTagNameNS(namespace, localName)`：
namespace 接受 `*` 或精确字符串，localName 接受 `*` 或大小写敏感字符串；document 结果包含
`documentElement`，element 结果只遍历后代并排除 owner。查询复用已有 namespace metadata、DFS
traversal、`HTMLCollection` 协议和稳定 wrapper identity，返回同步静态 snapshot；null、空或
未知 namespace/localName fail closed。

这组入口不解析 XML/SVG namespace，不修改 prefix 或节点，不提供 live collection、完整 Web IDL、
通用 DOM mutation 或新的 core ABI。`test_host.exe` 只提供 fixture、adapter 和 `TEST782–801`
自动断言；定向、兼容和缩减回归门分别为 21/21、162/162、223/223，证据位于
`tmp/device-runs/20260822-192042-next592-r2/`、`tmp/device-runs/20260822-192203-next592-compat-r1/`
和 `tmp/device-runs/20260822-192923-next592-regression-r1/`。本批只涉及同步脚本 API/DOM
snapshot，不涉及视觉、触摸、SIP、系统 picker、旋转或网络失败人工门；browser session 仍使用
608 KiB ceiling，独立 `positron_script` 默认堆仍为 512 KiB。

#### next593 的 namespace-aware attribute lookup 边界

next593 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation
ABI。现有 attribute snapshot 上新增只读的 `getAttributeNS(namespace, localName)`、
`hasAttributeNS()` 和 `getAttributeNodeNS()`；返回的 Attr wrapper 还暴露一致的
`namespaceURI`、`prefix` 和 `localName`。null 与空 namespace 都表示无 namespace，已知的
`xml`/`xmlns` 前缀分别映射到 XML/XMLNS namespace，未知前缀或 namespace 请求 fail closed。
Attr 的 value/nodeValue 仍通过同 owner 的现有同步 bridge 读取/写回。

这不是 namespace parser 或 mutation API：本批不提供 `setAttributeNS()`、`removeAttributeNS()`、
XML/SVG parser、prefix mutation、节点创建或 live collection，也不引入新的 core ABI。
`test_host.exe` 只提供 fixture、adapter 和 `TEST802–821` 自动断言；定向、兼容和缩减回归门分别为
21/21、182/182、243/243，证据位于
`tmp/device-runs/20260822-201726-next593-r2/`、
`tmp/device-runs/20260822-201838-next593-compat-r2/` 和
`tmp/device-runs/20260822-202712-next593-regression-r2/`。本批不涉及视觉、触摸、SIP、picker、
旋转或网络失败人工门。browser session 仍使用 608 KiB ceiling，独立 `positron_script` 默认堆
仍为 512 KiB。

#### next594 的 NamedNodeMap namespace lookup 边界

next594 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation
ABI。既有 `NamedNodeMap` 现在提供只读的 `getNamedItemNS(namespace, localName)`，复用
next593 的 null/空 namespace、XML/XMLNS 已知前缀、未知输入 fail-closed、大小写敏感
localName、String coercion 和稳定 Attr wrapper 语义；同一 map 对后续 attribute 增删和值更新
保持可观察。

本批不提供 `setNamedItemNS()`、`removeNamedItemNS()`、XML/SVG parser、namespace mutation、
节点创建或 live collection，也不引入新的 core ABI。`test_host.exe` 只提供 fixture、adapter 和
`TEST822–841` 自动断言；定向门 21/21、缩减回归 263/263，证据位于
`tmp/device-runs/20260822-204905-next594-r1/` 和
`tmp/device-runs/20260822-205012-next594-regression-r1/`。本批不涉及视觉、触摸、SIP、picker、
旋转或网络失败人工门；browser session 仍使用 608 KiB ceiling，独立 `positron_script` 默认堆
仍为 512 KiB。

#### next595 的 lookupPrefix 边界

next595 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共
relation ABI。document、DocumentType、HTML element、CharacterData 和 Attr wrapper 现在都
提供只读 `lookupPrefix(namespace)`：有限的 XML namespace 映射返回 `xml`；XMLNS namespace
只在相应 `xmlns:*` Attr 上返回 `xmlns`；HTML default namespace、null/空值、未知 URI 与未知
Attr prefix 均返回 `null`。参数做有限 String coercion，结果不会改变 wrapper metadata。

这不是 namespace declaration/parser 或 mutation API：本批不提供 `setAttributeNS()`、
`removeAttributeNS()`、prefix mutation、节点创建、live collection 或新的 core ABI。
`test_host.exe` 只提供 fixture、adapter 和 `TEST842–861` 自动断言；定向门 21/21、缩减回归
283/283，证据位于 `tmp/device-runs/20260822-211732-next595-r1/` 和
`tmp/device-runs/20260822-211920-next595-regression-r1/`。本批不涉及视觉、触摸、SIP、picker、
旋转或网络失败人工门；browser session 仍使用 608 KiB ceiling，独立 `positron_script` 默认堆
仍为 512 KiB。

#### next596 的 namespace mutation 边界

next596 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation
ABI。元素 wrapper 现在提供 `setAttributeNS(namespace, qualifiedName, value)` 与
`removeAttributeNS(namespace, localName)`：null/空 namespace 只接受无前缀名称，XML/XMLNS
只接受对应的 `xml`/`xmlns` 前缀，且未知 URI、未知 prefix、空名称和多重冒号安全无操作。
成功写入复用既有 attribute bridge，因此已有 namespace read API、Attr identity 和属性 map
观察语义保持一致。

这不是完整 DOM NamespaceError 或 namespace parser：本批不提供 namespace declaration 解析、
XML/SVG parser、节点创建、live collection 或新的 core ABI。`test_host.exe` 只提供 fixture、
adapter 和 `TEST862–881` 自动断言；定向门 21/21、缩减回归 303/303，证据位于
`tmp/device-runs/20260823-095421-next596-r2/` 和
`tmp/device-runs/20260823-095546-next596-regression-r1/`。本批不涉及视觉、触摸、SIP、picker、
旋转或网络失败人工门；browser session 仍使用 608 KiB ceiling，独立 `positron_script` 默认堆
仍为 512 KiB。

#### next597 的 NamedNodeMap/Attr namespace mutation 边界

next597 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation
ABI。既有 `NamedNodeMap` 现在提供受控的 `setNamedItemNS(namespace-aware Attr)` 与
`removeNamedItemNS(namespace, localName)`，元素 wrapper 另外提供 `setAttributeNodeNS(Attr)`。
这些入口复用 next596 的 null/空 namespace、XML/XMLNS 已知前缀和 `Attr` live wrapper；跨 owner
传入的 Attr 只把名称和值复制到目标 owner，source `ownerElement` 与 wrapper identity 不转移。
成功替换返回目标 owner 的旧 Attr，缺失项返回 null；未知 URI/prefix、非法 qualified name、非
Attr 输入和不支持的 namespace 组合均 fail closed。

这仍不是完整 DOM ownership 或 NamespaceError 实现：本批不解析 namespace declaration，不提供
XML/SVG parser、节点创建、live collection、通用 DOM mutation 或新的 core ABI。`test_host.exe`
只提供 fixture、adapter 和 `TEST882–901` 自动断言；定向门 21/21、namespace 缩减回归 101/101，
证据位于 `tmp/device-runs/20260823-103228-next597-r3/` 和
`tmp/device-runs/20260823-103700-next597-regression-r2/`。本批不涉及视觉、触摸、SIP、picker、
旋转或网络失败人工门；browser session 仍使用 608 KiB ceiling，独立 `positron_script` 默认堆
仍为 512 KiB。

#### next598 的 Attr leaf-node 边界

next598 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation
ABI。Attr wrapper 现在提供 `isId`、可读写的 `textContent`、空的 `childNodes`、
`hasChildNodes()`、null 的 `parentNode`/`parentElement`/首尾子节点/兄弟节点，以及
identity-based `isSameNode()` 和受控的 `isEqualNode()`。`textContent`、`value`、`nodeValue`
共用既有 attribute bridge；`isEqualNode()` 只比较 Attr nodeType/name/value，不能替代完整 DOM
深结构相等。Attr 的 `ownerElement` 仍是 owner metadata，不是 tree parent。

空 child collection 每次读取均是有界空 snapshot，支持 `item()` 和 iterator；调用者对返回数组的
修改不会写回 DOM。`test_host.exe` 只提供 fixture、adapter 和 `TEST902–921` 自动断言；定向门
21/21、缩减回归 121/121，证据位于 `tmp/device-runs/20260823-105508-next598/` 和
`tmp/device-runs/20260823-105630-next598-regression/`。本批不提供通用 DOM tree、Attr parent
挂接、节点创建、live collection 或新的 core ABI，也不涉及视觉、触摸、SIP、picker、旋转或网络
失败人工门。

#### next599 的 Attr detached-node relation 边界

next599 继续把语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` 的公共 relation ABI。
Attr wrapper 的 `isConnected` 固定为 false，`getRootNode()` 返回 Attr 自身并忽略 bounded
`composed` 选项；`contains()` 只对自身返回 true。`compareDocumentPosition()` 对自身返回 0，
对其他 Attr、owner element、null 或非法对象返回固定 `33`，即
`Node.DOCUMENT_POSITION_DISCONNECTED | Node.DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC`，不猜测
脱离树节点之间的顺序。`ownerElement` 仍是 owner metadata，不是 parent；Attr 也没有 child tree。

`test_host.exe` 只提供 fixture、adapter 和 `TEST922–941` 自动断言；定向门 21/21、缩减回归
141/141，证据位于 `tmp/device-runs/20260823-111516-next599/` 和
`tmp/device-runs/20260823-111631-next599-regression/`。本批不提供通用 DOM tree、节点创建、
live collection 或新的 core ABI，也不涉及视觉、触摸、SIP、picker、旋转或网络失败人工门。

#### next600 的 child-wrapper containment 边界

next600 仍把语义放在 `positron_browser.dll`，复用已有 `__pcoreNodeContains12` relation bridge，
不扩展 `positron_core.dll` ABI。`childNodes` 返回的文本、注释和无 id 子节点 wrapper 现在提供
`contains()`：wrapper 自身返回 true，已知父元素可包含其直接子节点；兄弟、owner metadata、
document、null 和非法对象返回 false。无 id 子节点仍使用同步 bounded snapshot，不能因此创建
通用 child tree 或 live collection。

`test_host.exe` 只提供 fixture、adapter 和 `TEST942–961` 自动断言；定向门 21/21、缩减回归
161/161，证据位于 `tmp/device-runs/20260823-113402-next600/` 和
`tmp/device-runs/20260823-113512-next600-regression/`。本批不提供文本 mutation、节点创建、
新的 core ABI，也不涉及视觉、触摸、SIP、picker、旋转或网络失败人工门。

#### next601 的 DocumentType 外部子集元数据边界

next601 继续把语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` ABI。既有
`document.doctype` wrapper 现在提供只读 `publicId`、`systemId` 和 `internalSubset`；当前
HTML doctype 的值固定为空字符串、空字符串和 `null`。字段在 bootstrap 时定义为不可写、不可
配置、可枚举属性，并随 frozen DocumentType snapshot 保持不变；它们不伪造外部 DTD、实体或
解析器，也不改变 owner/root/position/contains、namespace 或 baseURI。

`test_host.exe` 只提供 fixture、adapter 和 `TEST962–981` 自动断言；定向门 21/21、缩减回归
181/181，证据位于 `tmp/device-runs/20260823-115525-next601/` 和
`tmp/device-runs/20260823-115645-next601-regression/`。本批不新增 core ABI、DTD/实体解析、
节点 mutation 或人工视觉/触摸/SIP/picker/旋转/网络失败门。

#### next602 的 DocumentType entities/notations 边界

next602 继续把语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` ABI。既有
`document.doctype` wrapper 现在提供两个独立、稳定、冻结的空 `NamedNodeMap` snapshot：
`entities` 与 `notations`。它们的 `length` 为 0，固定 indexed slots、`item()`、named/namespace
lookup 和 iterator 都 fail closed/立即结束，并暴露 `NamedNodeMap` 的有限 branding；
`setNamedItem*()`/`removeNamedItem*()` 不改变状态。该边界不解析 DTD/实体、不创建 Attr/节点，
也不把 parser token 偷渡进 core；browser bootstrap 通过 lazy helper 复用既有 `m10(null)`，
避免重复实现造成 session heap 压力。

`test_host.exe` 只提供 fixture、adapter 和 `TEST982–998` 自动断言；定向门 18/18、缩减回归
198/198，证据位于 `tmp/device-runs/20260823-122704-next602/` 和
`tmp/device-runs/20260823-122827-next602-regression/`。首次长回归发现重复 map bootstrap 会在
TEST901 越过既有脚本堆上限，随后改为复用 helper 后通过，未提高预算。本批不新增 core ABI、
DTD/实体解析、节点 mutation 或人工视觉/触摸/SIP/picker/旋转/网络失败门。

#### next603 的 NamedNodeMap 迭代边界

next603 继续把语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` ABI。普通属性
`NamedNodeMap` 与 doctype 的空 `entities`/`notations` map 现在提供有界的 `forEach()`、`keys()`、
`values()`、`entries()` 和默认 values iterator。每次迭代读取当前属性名的同步 snapshot，
Attr wrapper 复用既有 session identity；iterator 对象自身可迭代，`forEach` 传递 Attr、索引、
map 与 `thisArg`，非法 callback 抛出受控 `TypeError`。这些方法不创建节点、不提供 live collection、
DTD/实体解析或异步调度。

`test_host.exe` 只提供 fixture、adapter 和 `TEST1000–1017` 自动断言；定向门 19/19、缩减回归
216/216，证据位于 `tmp/device-runs/20260823-125404-next603-r2/` 和
`tmp/device-runs/20260823-131725-next603-regression-final/`。回归选择把特殊 TEST999 从数字区间
中拆出后通过；本批不新增 core ABI、节点 mutation 或人工视觉/触摸/SIP/picker/旋转/网络失败门。

#### next604 的 HTMLCollection named projection 边界

next604 继续把语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` ABI。所有已有的静态
`HTMLCollection` snapshot（包括 `children`、`form.elements`、tag/class/namespace 查询和文档
named collections）现在在创建时为非空元素定义只读、不可枚举的 `id` 与 `name` 直达属性；属性值
复用该 snapshot 中的 element wrapper，方法名、`length`、数字索引和 `namedItem()` 保持原有
优先级。`NodeList` 查询结果不获得这组 named projection，未知名称和空 collection 仍 fail closed，
属性不会随 DOM 或属性 mutation live 更新。该实现不创建节点、不构造 Proxy、不改变 relation
bridge，也不提供通用 DOM mutation 或 live collection。

`test_host.exe` 只提供 tree/form fixture、adapter 和 `TEST1018–1035` 自动断言；定向门
`TEST1018–1035,999` 通过 19/19，缩减回归 `TEST802–998,1000–1035,999` 通过 234/234，
证据位于 `tmp/device-runs/20260823-134449-next604-r2/` 和
`tmp/device-runs/20260823-134557-next604-regression/`。两次最终运行均无 ERROR/FAIL、唯一
`TESTBENCH PASS` 且 `test13_route_ok=True`；本批只涉及同步 snapshot API，不新增视觉、触摸、
SIP、picker、旋转或网络失败人工门。

#### next605 的 form.elements RadioNodeList 边界

next605 继续把语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` ABI。只有
`form.elements` 这个已标记的 HTMLCollection 对重复 `id`/`name` 做分组：唯一匹配仍返回原
element，多个匹配返回静态 `RadioNodeList` snapshot；`item()`、数组索引、有限迭代器和
`Symbol.toStringTag` 复用既有 collection decorator，`value` getter 读取当前已选 radio，
setter 只选择同值 radio。缺失名称返回 `null`，普通 HTMLCollection 仍然使用首匹配
`namedItem()`，因此该特殊分组不会泄漏到其他 collection 或 NodeList。

RadioNodeList 的缓存只在当前脚本 session 内按控件 token 复用，direct named property 是
不可枚举、不可写、不可配置的 snapshot；不承诺 live `HTMLFormControlsCollection`、fieldset/
label 关联、节点创建、通用 mutation 或跨 session identity。新增 bootstrap 使 browser session
heap ceiling 从 608 KiB 调整到 624 KiB；独立 `positron_script` 的 512 KiB 默认堆、公共 C ABI、
所有权和宿主职责不变。`test_host.exe` 只提供 fixture 与断言。

`TEST1036–1053,999` 定向门通过 19/19，`TEST802–998,1000–1053,999` 缩减回归通过
252/252，证据分别位于 `tmp/device-runs/20260823-142518-next605-r2/` 和
`tmp/device-runs/20260823-142642-next605-regression-r2/`；两次最终运行均无 ERROR/FAIL、
唯一 `TESTBENCH PASS` 且 `test13_route_ok=True`。中途首次回归在 608 KiB 下由 TEST901 暴露
堆上限，单测确认 624 KiB 是本批的最小通过调整；未放宽断言。本批只涉及同步脚本 API/DOM
snapshot，不新增视觉、触摸、SIP、picker、旋转或网络失败人工门。

#### next608 的 native EDIT 输入事务边界

next608 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` ABI。新增的
`PBrowser_ScriptSessionRegisterNativeEditCallbacksEx()` 与三个同步入口把 native EDIT 的
beforeinput 接受/取消、pending input metadata、native value commit 到 input、dirty tracking
和 blur 时一次性 change 顺序放在 browser session 内；目标使用宿主提供的非零 session token，
几何仍以文档 CSS 像素传入。pending/type/data 有界为最多 16 个 token、每个字符串 255 字节，
session 销毁或宿主重建 native controls 时可显式 reset。

`test_host.exe` 只提供 WM EDIT 消息、`PCore_TextInputSetValue()` 文本同步、控件几何和 core
事件传播 callback；它不再保存 pending input 或 change-pending 状态。WM 控件、composition
生命周期、SIP/IME、焦点窗口、文本 mutation、native SELECT 和系统 picker 仍是宿主边界，
因此本批不宣称 OEM 候选词或完整 composition 兼容。TEST1056 覆盖接受/取消、commit metadata、
dirty/blur、reset/unregister；TEST228–230、1055、999 保持回归覆盖。证据位于
`tmp/device-runs/20260823-172005-next608-native-edit-r2/` 和
`tmp/device-runs/20260823-172030-next608-native-edit-regression/`，均为唯一 PASS、零
ERROR/FAIL、路由正确。本批不新增视觉、触摸、旋转、picker 或 OEM SIP/IME 人工门。

#### next609 的 native SELECT commit 边界

next609 继续把产品语义放在 `positron_browser.dll`，不扩展 `positron_core.dll` ABI。新增的
`PBrowser_ScriptSessionRegisterNativeSelectCallbacksEx()` 与
`PBrowser_ScriptSessionDispatchNativeSelectCommit()` 接收宿主完成 Core selection mutation
后的稳定 token、几何和选择快照；browser layer 对每次成功 commit 同步发出不可取消的
`input` → `change`，并保持同一 token 的 single/multiple 形状不变。最多跟踪 16 个 token，
宿主在 native 控件销毁/重建前调用 `PBrowser_ScriptSessionResetNativeSelectState()`。

`test_host.exe` 仍负责 WM SELECT 键盘消息、typed key callback 的 default-allowed 结果、
`PCore_SelectSetOptionSelected()`/多选 mutation、窗口重绘和平台副作用；它不保存 input/change
顺序状态。TEST1057 覆盖注册、重复注册、非法快照、input→change、adapter error、形状冲突、
reset 和 unregister；TEST67、TEST71、TEST118、TEST999 的 next609 设备门通过。该批不宣称
OEM SIP/IME、候选词或完整 native keyboard 默认动作兼容，也不新增视觉人工门。

#### next610 的 native SELECT 焦点族边界

next610 继续把可发布的焦点语义放在 `positron_browser.dll`，不扩展
`positron_core.dll` ABI。`PBrowser_ScriptSessionDispatchNativeSelectFocus()` 接收宿主提供的
稳定 token、几何和 focused 状态；browser layer 对每个 token 保持有界焦点状态，focused=1
同步派发不可取消的 `focus` → `focusin`，focused=0 同步派发 `blur` → `focusout`，重复状态
通知不重复派发，adapter 失败不提交新状态以便调用者重试。状态和 commit 的 16-token 上限
共用 native SELECT session，宿主在控件销毁/重建前继续调用
`PBrowser_ScriptSessionResetNativeSelectState()`。

`test_host.exe` 只把 WM `CBN/LBN_SETFOCUS`、`CBN/LBN_KILLFOCUS` 转换为该入口，并继续负责
`PCore_InteractionSetAt()`、控件焦点窗口、重绘和无脚本 fallback；它不再保存 SELECT 焦点族
顺序或重复通知状态。TEST1058 覆盖非法值、顺序、幂等、callback 失败恢复、多 token、reset
和 unregister；TEST67、TEST71、TEST1057、TEST999 的 next610 设备门通过。本批不宣称下拉
展开/关闭视觉、WM 键盘默认动作、SIP/IME 候选词或其他 OEM 行为兼容。

#### next611 的 native SELECT 单选下拉事务边界

next611 继续把可发布的事务策略放在 `positron_browser.dll`，不扩展
`positron_core.dll` ABI。`PBrowser_ScriptSessionDispatchNativeSelectInteraction()` 为单选
COMBOBOX 接收稳定 token、几何、选择快照和 begin/candidate/confirm/cancel phase；browser
layer 只保存有界候选状态，确认且候选存在时返回 `out_should_commit=1`，取消或无候选确认
不派发 input/change。交付前先调用 interaction END，再由宿主调用既有
`PBrowser_ScriptSessionDispatchNativeSelectCommit()`。

`test_host.exe` 仍负责 WM `CBN_DROPDOWN`/`CBN_SELCHANGE`/`CBN_SELENDOK`/`CBN_SELENDCANCEL`
通知、Core selection mutation、取消时 `CB_SETCURSEL` 回滚、控件重绘以及无脚本即时回退；
`CBN_CLOSEUP` 只作为中性关闭提示，不提前结束事务，因为 WM 允许 `CBN_SELCHANGE` 在其前后
到达。不会把 COMBOBOX 下拉窗口、键盘默认动作、SIP/IME 或 OEM 视觉伪装成 browser 语义。TEST1059
覆盖 interaction ABI 的非法输入、候选抑制、确认/取消、无候选确认、reset/unregister；TEST67
的合成 WM 探针覆盖 Core 延迟 mutation、取消回滚和确认提交。

#### next612 的 native SELECT 键盘边界

next612 继续把可发布的键盘事件策略放在 `positron_browser.dll`，不扩展
`positron_core.dll` ABI。`PBrowser_ScriptSessionDispatchNativeSelectKey()` 接收宿主提供的
稳定 token、文档几何、`keydown`/`keyup` phase 和 Enter/Arrow 元数据，复用已注册的
`PBrowserScriptKeyCallbacks`，并把 callback 的 cancel/default-allowed 结果返回给宿主；
adapter 失败时保持 fail-open 的 WM 兼容回退。browser layer 不模拟 COMBOBOX 默认动作，
也不拥有 Core selection mutation。

`test_host.exe` 仍负责将 WM key message 转成该入口、依据结果决定是否调用原生控件、接收
`CBN_SELCHANGE` 并更新 Core。TEST1060 覆盖 ABI 的 phase/token/metadata、取消、失败、reset
和注销；TEST118 在真实 WM6 页面上证明未取消的 ArrowDown 同时移动 COMBOBOX 和 Core selection。
Enter 的平台提交、下拉窗口、SIP/IME composition、视觉和 OEM 副作用继续属于宿主边界，
不能由该自动门扩展为完整键盘或移动端兼容性声明。

#### next613 的 native EDIT composition 边界

next613 继续把 native EDIT 的可发布 composition 语义放在 `positron_browser.dll`，不扩展
`positron_core.dll` ABI。新增的 `PBrowser_ScriptSessionDispatchNativeEditComposition()`
接收宿主提供的稳定 token、几何、START/UPDATE/END phase 和借用的 UTF-8 数据；browser
layer 在最多 16 个 token 的 native EDIT 状态中保存不超过 255 字节的最后 preedit，并以
`compositionstart`、不可取消的 `beforeinput(insertCompositionText)` →
`compositionupdate` → `compositionend` 顺序调用既有 input callback。START 的 callback
取消会返回 `out_default_allowed=0`，UPDATE 的 beforeinput metadata 会复用 next608 的
native commit → input 事务，END 允许 NULL 数据回放最后一次 preedit。

`test_host.exe` 仍负责 WM_IME、ImmGetCompositionStringW、SIP/候选词窗口、原生 EDIT
文本 mutation、WM_CHAR 抑制和平台重绘；它只把平台 phase/data 转换成该 ABI，不保存产品
事件顺序。TEST1061 覆盖顺序、取消、adapter 失败重试、显式/隐式 END、reset 和 unregister；
TEST123–125 在真实 WM6 上保持 composition/InputEvent/KeyboardEvent 元数据回归。该批不
宣称 OEM 候选词整词提交、SIP 视觉、触摸、旋转或其他平台副作用兼容。

next618 仍不扩张 browser ABI：宿主在收到 WM_IME 的 `GCS_RESULTSTR` 后，先通过
`ImmGetCompositionStringW` 取得完整结果并转换为 UTF-8，再用一次 `EM_REPLACESEL` 写入
当前 native EDIT composition selection；由既有 `EN_CHANGE` 回写 Core，继续复用
next613 的 composition metadata 和 browser-owned commit→input 事务。若 UTF-8 转换失败，
宿主保留原来的 EDIT default-procedure fallback。这样修正的是 WinCE 平台副作用，不把
WM 控件、SIP 窗口或 OEM 候选词体验冒充成 `positron_browser.dll` 语义；TEST1066 只覆盖
可重复的完整多字节结果落地，真实 SIP 候选窗口和视觉仍需人工验收。

next619 把“完整 IME result”这项可发布的事务策略补回
`positron_browser.dll`，但不把平台副作用搬进产品层。新增的
`PBrowser_ScriptSessionDispatchNativeEditResult()` 要求已开始的稳定 token composition，
校验不超过 255 字节的借用 UTF-8 result，并同步派发
`beforeinput(insertCompositionText)` → `compositionupdate`，把同一数据接入既有 pending
native commit → input metadata。`test_host.exe` 只将 `ImmGetCompositionStringW` 的结果交给
该入口，然后执行 `EM_REPLACESEL`、接收 native value mutation 和调用 composition end；
WM_IME、SIP/候选词窗口、原生控件和视觉仍归宿主。TEST1067 覆盖 invalid/capacity、生命周期、
完整多字节结果、commit、reset 和 unregister，不宣称 OEM 输入法视觉兼容。

next620 把文件输入 picker 返回后的产品事务放入
`positron_browser.dll`，但不搬迁平台 picker。新增
`PBrowser_ScriptSessionDispatchNativeFileSelection()` 接收稳定的 file-control token 和
BEGIN/COMMIT/CANCEL phase，保存最多 16 个 token；COMMIT 只派发一次不可取消的
`input`（`inputType="insertFromFile"`）→ `change`，CANCEL 幂等且不派发事件。宿主在打开
`GetOpenFileNameEx` 前 BEGIN，Core 通过 `PCore_FileInputSetPath()` 写入成功后 COMMIT；
picker 窗口、文件系统权限、路径和重绘仍由宿主负责。TEST1068 是 ABI 契约，TEST262 是
消费者回归，TEST232/263 继续作为真实对话框人工门。

next621 在文件选择事件事务之外，再把 programmatic `file.click()` 的 picker 请求仲裁放入
`positron_browser.dll`。`PBrowser_ScriptSessionDispatchNativeFilePicker()` 为每个脚本
session 保存一个 pending/active request：REQUEST 在 host 投递窗口消息前占位，OPEN 在进入
模态 WM6 picker 前转为 active，CLOSE/CANCEL 或 reset 清理；同一 session 的重复 request
只返回成功 no-op，不会生成第二个 picker。token、phase 和 session 生命周期均在产品层
校验；宿主只保存 HWND/document/index 并执行 PostMessage、系统 picker、文件系统和路径
写入。TEST1069 是离线 ABI 门，TEST262 继续验证实际宿主接线。

next622 再把受信任物理锚点点击的默认动作接入 `positron_browser.dll`：
`PBrowser_ScriptSessionDispatchAnchorClick()` 接收宿主命中的 href，先复用 browser-owned
可取消 click；只有未被阻止时才向已注册导航适配器提交 ASSIGN。href、命中测试、网络
请求、窗口替换和文档生命周期仍由宿主拥有，公共 API 不暴露 core/link/window 类型。
TEST1070 是产品契约及宿主 helper 接线门。

next623 将受信任 checkbox/radio 激活的事务边界继续放入 `positron_browser.dll`：
`PBrowser_ScriptSessionDispatchNativeToggle()` 对每个 session 保留最多 16 个 stable token，
先派发可取消 click；宿主报告 Core checked 状态提交后，browser layer 才按一次不可取消的
`input` → `change` 派发，取消、禁用和无状态变化不派发伪造事件。宿主仍负责命中、Core
mutation、WM 鼠标/键盘默认动作、重绘和 label/窗口副作用；公共 ABI 不暴露 core 控件或
原生窗口。TEST1071 是产品契约与宿主 helper 接线门。

#### next614 的 label/control 关系边界

next614 沿既有 DOM relation callback 把 label 与控件的最小关联迁入产品 DLL：
`positron_core.dll` 的 `PCore_NodeRelationById()` 增加 label-control、control-label-count
和 control-label-at 三个只读关系；`positron_browser.dll` 将其包装为
`HTMLLabelElement.control` 与控件的 `labels` 静态 NodeList。显式 `for` 优先指向同文档的
labelable input（排除 hidden）、select、textarea 或 button；没有 `for` 时只取 label 内第一个
可寻址的嵌套控件。无效目标、无 ID label、非控件、hidden 和越界索引 fail closed。

这是一组同步、session-scoped snapshot，不是 live `HTMLFormControlsCollection`，也不实现
节点创建、mutation 或完整 labelable 元素集合。fieldset disabled 的有效状态由
`positron_core.dll` 的统一判定提供给验证、successful controls、控件信息和交互闸门；它支持
第一个 legend 后代豁免和嵌套 fieldset，但不等于完整 live DOM，也不替宿主完成 native 窗口样式、
invalid UI、SIP/IME 或文件选择器副作用。`test_host.exe` 仅提供 fixture、callback adapter 和
TEST1062/1063 断言；新的 core/browser ABI 可由其他消费者复用，宿主仍拥有窗口、焦点、原生
控件和视觉副作用。

#### next615 的 fieldset disabled 边界

next615 将 form-control 的有效 disabled 状态集中到 `pcore_node_effectively_disabled()`：元素
自身的 `disabled` 属性优先；没有自身属性时，祖先 disabled fieldset 会使 input、button、
select、textarea 失效，但该 fieldset 的第一个直接 legend 及其后代豁免，嵌套 fieldset 仍按
各自祖先逐层计算。该判定在 `positron_core.dll` 内被验证、成功控件序列化、默认 submitter、
`PCore_FormControlInfo*`、表单激活和交互状态查询复用，因此动态修改 `fieldset.disabled` 不必
重建 layout 才能更新这些语义；HTML `control.disabled` 仍只反射元素自身属性。

该批不宣称完整 HTML fieldset/live DOM 标准、native 控件视觉或 invalid UI；WM 窗口样式、真实
触摸、SIP/IME、文件选择器和其他平台副作用仍由宿主负责。`test_host.exe` 只提供 TEST1063
fixture/断言与设备门消费者。

## 独立 JavaScript 与浏览器 JavaScript

项目只有一套 JavaScript 引擎实现：`positron_script.dll` 内的 Duktape。

“独立 JavaScript”指普通应用直接创建 `PScript` context，执行与网页无关的脚本。
“浏览器 JavaScript”指产品浏览器层和宿主在显式开关开启时：

1. browser layer 持有 `positron_script` context，并按 DOM 顺序驱动 classic inline/external script；
2. browser layer 通过稳定 ABI 注册宿主提供的 typed DOM 读写/attribute/value/checked/`HTMLElement.disabled`/`title`/`lang`/`dir`/`hidden`/`accessKey`/`role`/`ariaLabel`/`contentEditable`/validation query（包括 form-level 聚合）/custom-validity/form/constraint-related reflected properties（含 `name`、form `action`/`method`/`enctype`/`target`/`autocomplete`/`acceptCharset`、submitter `formAction`/`formMethod`/`formEnctype`、控件 `placeholder`/`autocomplete`/`inputMode`/`type`、`pattern`/`minLength`/`maxLength`）/form-property/navigation 适配，承接同文档 location/history 事件分发和 native input/composition/keyboard/focus/EDIT-change/post-change-input/click/programmatic-click（包括 typed click、disabled 抑制、验证与 submit/reset 事件顺序；file input 只承接 typed click，系统 picker 仍由宿主触发）/submit-reset/invalid/file-input/checkbox/radio input/change/SELECT-input/change dispatch contract；native EDIT 的事务状态由 `PBrowser_ScriptSessionRegisterNativeEditCallbacksEx()` 持有，native SELECT 的 commit input→change、focus-family、单选下拉 interaction 和 key dispatch/default-allowed policy 由 `PBrowser_ScriptSessionRegisterNativeSelectCallbacksEx()`、`PBrowser_ScriptSessionDispatchNativeSelectInteraction()` 与 `PBrowser_ScriptSessionDispatchNativeSelectKey()` 持有，宿主只提供 value/selection/focus transition、WM phase 与 core propagation；
2a. report-validity callback 只返回同步 valid 结果并路由可寻址控件的 trusted `invalid` 事件；
    它不负责 native invalid UI、焦点/滚动或表单提交。
3. browser layer 持有并执行产品 bootstrap，并在 `PBrowser_ScriptSessionRegisterProgrammaticClickCallbacksEx()` 中执行程序化表单激活策略；宿主 Ex callback 只提供 target lookup、submit validation、default action 和非表单 click 传播；native EDIT/SELECT Ex callback 只提供 value/selection commit、composition phase/data、焦点转换后的 core 事件传播；native SELECT 键盘取消通过 `PBrowser_ScriptSessionDispatchNativeSelectKey()` 返回 default-allowed；后续把其余 form/input callback 实现从 `test_host` 迁入 browser layer；
4. 宿主继续提供资源、窗口和控件回调，browser layer 在页面提交、失败或关闭时释放 context 和 bridge。

因此浏览器绑定不是第二个引擎，也不应把 Duktape 或 libdom 类型暴露成公共 ABI。当前
history/session、脚本 context 所有权、bootstrap 和 DOM 读写/attribute/value/checked/disabled/validation-query/custom-validity/constraint-reflection/form-property、ID-addressable
DOM relation/form collection/label-control snapshot、navigation/location-event/native-input/keyboard/focus/EDIT-change/post-change-input/click/programmatic-click/
submit-reset/invalid/report-validity/file-input/checkbox-radio-change/SELECT-input/change dispatch entry，及
程序化 click 的 typed activation policy、native EDIT 的 beforeinput/input/dirty/change 事务策略与
composition phase/preedit policy、
native SELECT 的 commit input/change、focus-family 顺序、单选下拉事务策略与 typed key dispatch policy 已进入
`positron_browser.dll`；native SELECT WM 控件真正默认动作、下拉窗口/视觉、native EDIT 的 WM_IME/SIP
与原生文本 mutation、文件 picker/文件系统，以及其余 native form/input bridge 仍在迁移中且
默认关闭；完整 IME result 的事件事务现在由
`PBrowser_ScriptSessionDispatchNativeEditResult()` 持有，文件选择后的事件事务由
`PBrowser_ScriptSessionDispatchNativeFileSelection()` 持有，但平台副作用仍由宿主完成；不能将
其描述为完整 `window`、DOM、Web API 或 URL Standard 实现。

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

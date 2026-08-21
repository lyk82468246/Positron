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
history/session、脚本 context 所有权、bootstrap 和 DOM 读写/attribute/value/checked/disabled/validation-query/custom-validity/constraint-reflection/form-property/
navigation/location-event/native-input/keyboard/focus/EDIT-change/post-change-input/click/programmatic-click/
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

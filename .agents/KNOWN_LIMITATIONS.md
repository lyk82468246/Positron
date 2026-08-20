# Positron 当前限制

更新时间：2026-08-20

这里只记录当前仍存在的产品或验收边界。已完成批次和设备流水不保留在本文件；最近证据见
[`HANDOFF.md`](HANDOFF.md)，稳定架构见
[`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

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

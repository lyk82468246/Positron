# Positron 路线图

更新时间：2026-08-23

本文件只列尚未完成的目标。已提交的 next 批次不继续停留在路线图；当前候选和设备门见
[`HANDOFF.md`](HANDOFF.md)，当前能力缺口见
[`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 总原则

Positron 是给 WM6 补齐现代能力，不是拆掉 WM6 重建。

- WM6 已经足够的部分优先复用 GDI、WinInet、WM Imaging、CryptoAPI 和 native control。
- 缺失的协议、解析器、编解码器或 runtime 优先移植成熟上游，再用稳定 DLL 隔离平台差异。
- “能力是否存在”优先于观感和性能微调；崩溃、数据损坏和核心交互阻塞始终最高优先级。
- 每批只推进一个边界明确的纵向能力，保留正例、反例、共享路径和真实页面门。
- 公共 DLL 服务任意 WM 应用；浏览器和 `test_host` 只是组合消费者。

## 当前中期里程碑

在浏览器 JavaScript 默认关闭的前提下，把已存在的 Duktape 页面 context 与 DOM、事件、表单、
输入、location/history 逐步接成可预测、可回归的轻量网页运行能力，同时不破坏默认 Browse
路径和公共 DLL 边界。

该里程碑的完成不是“完整浏览器 JavaScript”，而是：

- 生命周期和所有权明确；
- 常见轻量页面脚本能够完成基本 DOM、事件、输入和导航任务；
- 关闭开关时零额外脚本发现、抓取和执行；
- 每个受支持语义都有设备正反例和真实页面哨兵；
- 未支持能力明确失败或走普通导航，不产生隐式近似。

## 短期目标

### 1. 将浏览器 JavaScript bridge 迁入产品层

next234 已将 history/session 状态机迁入 `positron_browser.dll`，next235 已迁移 PScript context
及 host JSON callback 的 session 所有权和生命周期，next236 已迁移 browser bootstrap 文本与
求值入口，next237 已迁移 DOM 只读 callback 的 JSON 分发，next238 已迁移 textContent 写入，
next239 已迁移 DOM attribute callback，next240 已迁移 Event callback，next241 已迁移 input value callback，next242 已迁移 checked callback，next243 已迁移 form-property callback（defaultValue/defaultChecked/selectedIndex），next244 已迁移 navigation JSON callback dispatch，next245 已迁移同文档 location/history 事件分发，next246 已迁移 native input/composition typed dispatch entry，next247 已迁移 native keyboard typed dispatch entry，next248 已迁移 focus-family typed dispatch entry，next249 已迁移 native SELECT change typed dispatch entry，next250 已迁移 native SELECT input typed dispatch entry，next251 已迁移 native EDIT change typed dispatch entry，next252 已迁移 native EDIT post-change input typed dispatch entry，next253 已迁移 native click typed dispatch entry，next254 已迁移 native submit/reset typed dispatch entry，next255 已迁移 native invalid typed dispatch entry，next256 已迁移 file-input input/change typed dispatch entry，next257 已迁移 checkbox/radio change typed dispatch entry，next258 已迁移 checkbox/radio input typed dispatch entry，next259 已迁移 checkbox/radio label/native activation click target and event ordering，next260 已迁移 checkbox/radio keyboard activation through the existing key/click/input/change contracts，next261 已迁移 checkbox/radio programmatic `HTMLElement.click()` through a product callback and the existing click/input/change contracts，next262 已迁移 native submit/reset/button 的 programmatic `HTMLElement.click()` through typed click/form-event and existing host defaults，next263 已让 native file input 的 programmatic `HTMLElement.click()` 复用 typed click contract，并明确自动脚本与系统 picker 的宿主 GUI 边界，next264 已把宿主 GUI picker 的选择/取消/错误与 file input 提交通知拆成可注入的同步 adapter；next265 的 TEST232 真实 WM6 picker 已完成选择/同窗口取消的人工验收。next295 已补齐可见 render window 中的 deferred picker request：脚本回调返回后由宿主消息循环排队一次，再复用既有 picker adapter；TEST262 自动边界和 TEST263 真实 GUI 均已通过。next296 已通过既有 attribute bridge 暴露 `HTMLElement.disabled`，TEST264 及相关自动回归已通过。next297 已在同一 bridge 上补齐 `required`、`readOnly`、`multiple`、`noValidate`、`formNoValidate` 和 `min`/`max`/`step` 反射，TEST265 及相关自动回归已通过。导航的窗口、网络和 history side effect、core 事件传播、焦点与控件默认行为仍由宿主 typed adapter 提供。`test_host` 只能作为
宿主适配和测试消费者；
next298 已在 `positron_core.dll` 提供按 DOM id 的控件约束状态查询，并在
`positron_browser.dll` 增加独立 validation callback；bootstrap 的 `checkValidity()`、
`willValidate` 和基础 `validity` flags 已由 TEST266 及 77 项启用 JavaScript 的相关回归验证。
该切片只查询控件状态，不触发 invalid 事件，也不宣称 form-level validation、validationMessage
或 native invalid UI；`test_host` 仍只是宿主适配和测试消费者。
next299 已在 `positron_core.dll` 提供按 DOM id 的 custom-validity UTF-8 message set/get，扩展
现有 candidate validation 以保留 `customError`；`positron_browser.dll` 增加独立 custom-validity
callback，bootstrap 的 `setCustomValidity()` 与 `validationMessage` 已由 TEST267 及 84 项
启用 JavaScript 的相关回归验证。该切片仍只覆盖控件级 message，不触发 invalid 事件或 native
invalid UI，也不宣称 form-level `checkValidity()`；`test_host` 仍只是宿主适配和测试消费者。
next300 已在 `positron_core.dll` 提供按 form DOM id 的约束聚合查询，并让现有 browser validation
callback 将 form 的聚合 `valid` 接入 bootstrap 的受限 `checkValidity()`；TEST268 及 85 项启用
JavaScript 的相关回归已通过。该切片忽略 form `novalidate`、跳过不参与约束验证的控件，但不触发
invalid 事件、不提交表单、不实现 `reportValidity()` 或 native invalid UI；`test_host` 仍只是
宿主适配和测试消费者。
next301 已在 `positron_core.dll` 提供 `PCore_FormReportValidityById`，并在
`positron_browser.dll` 增加 report-validity callback 与 bootstrap `reportValidity()`；TEST269/999
定向门已通过。该切片按 DOM 顺序派发可寻址 invalid controls 的 trusted、non-bubbling、cancelable
`invalid` 事件，`preventDefault()` 不改变 boolean 结果，仍不提供 native validation UI、焦点/滚动、
本地化提示或提交副作用；`test_host` 仍只是宿主适配和测试消费者。
next302 已在 `positron_core.dll` 提供 `PCore_FormGetValidationMessageById`，并让现有
browser custom-validity getter 在没有 application-owned message 时返回固定英文 validity fallback；
TEST270/999 与启用 JavaScript 的 87 项回归已通过。该切片保留 custom message 优先级，提供完整
字节长度和安全截断，但不做本地化、不显示 native validation UI；`test_host` 仍只是宿主适配和
测试消费者。
next303 已在现有 browser attribute bridge 上增加 `pattern`、`minLength`、`maxLength` 反射；
TEST271/999 与启用 JavaScript 的 88 项回归已通过，动态属性会继续驱动
`tooShort`/`tooLong`/`patternMismatch`，并拒绝负数/非有限 setter。该切片不涉及视觉、SIP、系统 picker 或完整 Web IDL
异常类型；`test_host` 仍只是宿主适配和测试消费者。
next304 已在同一 browser attribute bridge 上增加 form、input、textarea、select、button 的
`name` 反射；TEST272/999 与启用 JavaScript 的 89 项相关回归已通过，动态改名会进入
successful-control submission。该切片不涉及视觉、SIP、系统 picker 或完整
`HTMLFormControlsCollection`/表单关联算法；`test_host` 仍只是宿主适配和测试消费者。
next305 已在同一 browser attribute bridge 上增加 form `action`/`method` 反射；TEST273/999
与启用 JavaScript 的 90 项相关回归已通过，动态更新会改变受限 submission 的目标和 GET/
urlencoded-POST 判定。该切片不涉及完整 URL parser、target/enctype 或 multipart；`test_host`
仍只是宿主适配和测试消费者。
next306 已在同一 browser attribute bridge 上增加 form `enctype` 反射；TEST274/999 与启用
JavaScript 的 91 项相关回归验证了 urlencoded/multipart 动态切换和恢复。该切片复用已有
multipart submission snapshot，不实现 `encoding` 别名、完整规范化或传输边界；`test_host`
仍只是宿主适配和测试消费者。
next307 已在同一 browser attribute bridge 上增加 submitter `formAction` 反射；TEST275/999
与启用 JavaScript 的 92 项相关回归验证了 urlencoded/multipart action 覆盖、移除恢复和
raw attribute 边界。该切片不实现完整 URL parser 或其他 submitter override 属性；`test_host`
仍只是宿主适配和测试消费者。
next308 已在同一 browser attribute bridge 上增加 submitter `formMethod` 反射；TEST276/999
与启用 JavaScript 的 93 项相关回归验证 get/post 覆盖和移除恢复。该切片不扩展 multipart
submitter eligibility 或完整 method 规范化；`test_host` 仍只是宿主适配和测试消费者。
next309 已在同一 browser attribute bridge 上增加 submitter `formEnctype` 反射；TEST277/999
与启用 JavaScript 的 94 项相关回归验证 urlencoded/multipart 覆盖、snapshot eligibility 和
移除恢复。该切片不实现未知值/别名的完整编码规范化或传输边界；`test_host` 仍只是宿主适配
和测试消费者。
next310 已验证 text-input 隐式 Enter submission 与显式首个 submitter 共用 action/method/
enctype override 及 multipart snapshot；TEST278/999 与启用 JavaScript 的 95 项相关回归通过。
该切片不扩展完整 implicit-submission 算法或键盘事件规范；`test_host` 仍只是宿主适配和测试消费者。
next311 已在同一 browser attribute bridge 上增加 form `target` 反射；TEST279/999 与启用
JavaScript 的 96 项相关回归验证 raw round-trip 且 submission action/method 不变。该切片不实现
新窗口、target browsing context 或导航副作用；`test_host` 仍只是宿主适配和测试消费者。
next312 已在同一 browser attribute bridge 上增加 form `autocomplete` 反射；TEST280/999 与启用
JavaScript 的 97 项相关回归验证 raw round-trip 且 submission 不变。该切片不实现自动填充、凭据
存储或控件级 autocomplete 语义；`test_host` 仍只是宿主适配和测试消费者。
next313 已在同一 browser attribute bridge 上增加 form `acceptCharset` ↔ `accept-charset` 反射；
TEST281/999 与启用 JavaScript 的 98 项相关回归验证 raw round-trip 且 submission 不变。该切片
不实现字符集转换、编码协商或非 UTF-8 wire body；`test_host` 仍只是宿主适配和测试消费者。
next314 已在同一 browser attribute bridge 上增加控件 `placeholder` 反射；TEST282/999 与启用
JavaScript 的 99 项相关回归验证 raw round-trip、current value 和 submission 不变。该切片不实现
placeholder 绘制、SIP 或原生提示 UI；`test_host` 仍只是宿主适配和测试消费者。
next315 已在同一 browser attribute bridge 上确认 input `autocomplete` 反射；TEST283/999 与启用
JavaScript 的 100 项相关回归验证 raw round-trip、current value 和 submission 不变。该切片不实现
自动填充策略、凭据存储或原生提示 UI；`test_host` 仍只是宿主适配和测试消费者。
next316 已在同一 browser attribute bridge 上增加 input `inputMode` ↔ `inputmode` 反射；TEST284/999
与启用 JavaScript 的 101 项相关回归验证 raw round-trip、current value 和 submission 不变。该切片
不实现 SIP、键盘布局或输入法策略；`test_host` 仍只是宿主适配和测试消费者。
next317 已在同一 browser attribute bridge 上增加 input `type` raw 反射；TEST285/999 与启用
JavaScript 的 102 项相关回归验证属性往返和既有 submission 不变。该切片不实现动态控件重建、完整
Web IDL type 规范或 native type UI；`test_host` 仍只是宿主适配和测试消费者。
next318 为同一 attribute bridge 增加 textarea `placeholder` 的独立设备覆盖；TEST286/999 与
最近的 264–286/999 24 项回归验证 raw round-trip、current value 和 submission 不变。该切片不实现
placeholder 绘制、SIP、原生提示 UI 或完整 textarea Web IDL 语义；`test_host` 仍只是宿主适配和测试消费者。
next319 为同一 attribute bridge 增加 select `autocomplete` 的独立设备覆盖；TEST287/999 与最近的
264–287/999 25 项回归验证 raw round-trip、选中值和 submission 不变。该切片不实现自动填充策略、
凭据存储或完整 select Web IDL 语义；`test_host` 仍只是宿主适配和测试消费者。
next320 为 button `type` raw 反射增加 submitter 边界覆盖；TEST288/999 与最近的 264–288/999 26
项回归验证类型往返、恢复为 submit 后的 successful-control submission 不变。该切片不实现动态控件
重建、完整 button Web IDL 或 native button UI；`test_host` 仍只是宿主适配和测试消费者。
next321 验证未知 form `method` 原始值反射与 submission 的安全 GET fallback；TEST289/999 与最近的
264–289/999 27 项回归保持 action/body 不变。该切片不实现其他 HTTP 方法、规范化或导航副作用；
`test_host` 仍只是宿主适配和测试消费者。
next322 验证未知 form `enctype` 原始值反射与 urlencoded POST fallback；TEST290/999 与最近的
264–290/999 28 项回归保持 action/body 不变。该切片不实现 enctype 规范化、multipart 传输或其他
编码格式；`test_host` 仍只是宿主适配和测试消费者。
next323 验证 method/enctype 的大小写不敏感匹配与 raw case 反射；TEST291/999 与最近的 264–291/999
29 项回归保持 mixed-case urlencoded POST 的 action/body 不变。该切片不实现规范化 getter、完整
Web IDL 枚举语义或导航副作用；`test_host` 仍只是宿主适配和测试消费者。
next324 验证动态 action/method/value 更新后反复重排的 submission metadata 一致性；TEST292/999
与最近的 264–292/999 30 项回归保持 method、action/body 及 size metadata 正确。该切片不实现导航
提交、异步任务或完整浏览器生命周期；`test_host` 仍只是宿主适配和测试消费者。
next325 验证 form reset 恢复控件默认值而保留动态 action/method，并重新生成正确 submission metadata；
TEST293/999 与最近的 264–293/999 31 项回归保持 reset 前后 action/method、body 和 size 字段正确。
该切片不实现额外导航或完整浏览器生命周期；`test_host` 仍只是宿主适配和测试消费者。
next326 完成累计检查点：`TEST68-73,189-231,233-262,264-293/999` 共 110 项启用 JavaScript 的
相关回归全部通过；不新增产品语义，后续仍从路线图选择单一、不依赖人工页面观察的能力。
next327 在 browser bootstrap 的既有 raw attribute bridge 上增加 `HTMLElement.title` 反射；
TEST294/999 与最近 `TEST264-294/999` 已通过。该切片只覆盖 UTF-8 属性往返，不涉及 tooltip
绘制、原生提示 UI 或完整 HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next328 在同一 raw attribute bridge 上增加 `HTMLElement.lang` 反射；TEST295/999 与最近
`TEST264-295/999` 已通过。该切片只覆盖 UTF-8 属性往返，不涉及语言解析、本地化或完整
HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next329 在同一 raw attribute bridge 上增加 `HTMLElement.dir` 反射；TEST296/999 与最近
`TEST264-296/999` 已通过。该切片只覆盖 UTF-8 属性往返，不涉及 CSS 方向布局或完整
HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next330 在同一 bridge 上增加 `HTMLElement.hidden` 布尔反射；TEST297/999 与最近
`TEST264-297/999` 已通过。该切片只覆盖布尔属性往返，不涉及隐藏布局算法、视觉或完整
HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next331 在同一 bridge 上增加 `HTMLElement.accessKey` raw 反射；TEST298/999 与最近
`TEST264-298/999` 已通过。该切片只覆盖 UTF-8 属性往返，不触发 WM 快捷键、焦点副作用或
完整 HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next332 在同一 bridge 上增加 `HTMLElement.role` raw 反射；TEST299/999 与最近
`TEST264-299/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现辅助技术树、语义计算或完整
HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next333 在同一 bridge 上增加 `HTMLElement.ariaLabel` ↔ `aria-label` raw 反射；TEST300/999 与
近期链重试后的 `TEST264-300/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 ARIA 语义树
或辅助技术计算；首次近期链的既有 bootstrap timeout 不作为基线。
next334 在同一 bridge 上增加 `HTMLElement.contentEditable` raw 反射；TEST301/999 与最近
`TEST264-301/999` 已通过。该切片只覆盖 UTF-8 属性往返，不改变 layout、编辑控件、native IME
或完整 HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next335 在同一 bridge 上增加 `HTMLElement.draggable` raw 反射；TEST302/999 与最近
`TEST264-302/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现拖放手势、命中测试、native
pointer 或完整 HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next336 在同一 bridge 上增加 `HTMLElement.tabIndex` 的有限整数 raw 反射；TEST303/999 与最终
重试后的最近 `TEST264-303/999` 已通过。该切片只覆盖有限整数往返、非法/缺失回落 `-1` 和
setter 边界，不实现焦点导航、滚动、键盘顺序或完整 HTMLElement Web IDL；`test_host` 仍只是
宿主适配和测试消费者。
next337 在同一 bridge 上增加 `HTMLInputElement.accept` raw 反射；TEST304/999 与最近
`TEST264-304/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现文件类型解析、过滤、系统
picker 或完整 input Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next338 在同一 bridge 上增加 `HTMLInputElement.capture` raw 反射；TEST305/999 与最近
`TEST264-305/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现摄像头/麦克风捕获、文件类型
过滤、系统 picker 或完整 input Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next339 在同一 bridge 上增加 `HTMLInputElement.dirname` raw 反射；TEST306/999 与最近
`TEST264-306/999` 已通过。该切片只覆盖 UTF-8 属性往返，不改变提交方向字段、编码或其他
表单默认行为，也不实现完整 input Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next340 在同一 bridge 上增加 `HTMLInputElement.list` raw 反射；TEST307/999 与重试后的最近
`TEST264-307/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 datalist 解析、建议项、自动
完成或完整 input Web IDL；首次近期门的既有 bootstrap timeout 不作为基线；`test_host` 仍只是
宿主适配和测试消费者。
next341 在同一 bridge 上增加 `HTMLTextAreaElement.wrap` raw 反射；TEST308/999 与最近
`TEST264-308/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现软/硬换行布局、提交编码
差异或完整 textarea Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next342 在同一 bridge 上增加 `HTMLElement.htmlFor` ↔ `for` raw 反射；TEST309/999 与最近
`TEST264-309/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 label 关联、焦点转移或完整
HTMLElement Web IDL；`className` 重定义候选已撤回并记录失败边界；`test_host` 仍只是宿主
适配和测试消费者。
next343 在同一 bridge 上增加 `HTMLElement.slot` ↔ `slot` raw 反射；TEST310/999 与最近
`TEST264-310/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 Shadow DOM、slot 分配或完整
HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next344 在同一 bridge 上增加 `HTMLElement.itemId` ↔ `itemid` raw 反射；TEST311/999 与最近
`TEST264-311/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 microdata 解析、语义树或完整
HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next345 在同一 bridge 上增加 `HTMLElement.itemProp` ↔ `itemprop` raw 反射；TEST312/999 与最近
`TEST264-312/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 microdata token 解析、语义树
或完整 HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next346 在同一 bridge 上增加 `HTMLElement.itemRef` ↔ `itemref` raw 反射；TEST313/999 与最近
`TEST264-313/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 microdata 引用解析、语义树
或完整 HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next347 在同一 bridge 上增加 `HTMLElement.itemScope` ↔ `itemscope` 布尔反射；TEST314/999 与
最近 `TEST264-314/999` 已通过。该切片只覆盖布尔属性往返，不实现 microdata item 解析、语义树
或完整 HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next348 在同一 bridge 上增加 `HTMLElement.itemType` ↔ `itemtype` raw 反射；TEST315/999 与最近
`TEST264-315/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 microdata vocabulary 解析、
语义树或完整 HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next349 在同一 bridge 上增加 `HTMLElement.nonce` ↔ `nonce` raw 反射；TEST316/999 与最近
`TEST264-316/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 CSP nonce 校验、安全策略、
脚本执行或完整 HTMLElement Web IDL；首个 `<script>` 夹具无 probe 结果，已改用普通 `<div>`；
`test_host` 仍只是宿主适配和测试消费者。
next350 在同一 bridge 上增加 `HTMLElement.part` ↔ `part` raw 反射；TEST317/999 与最近
`TEST264-317/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 Shadow DOM 部件导出、CSS
选择器语义或完整 HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next351 在同一 bridge 上增加 `HTMLElement.exportParts` ↔ `exportparts` raw 反射；TEST318/999 与
最近 `TEST264-318/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 Shadow DOM 部件导出
算法或完整 HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next352 在同一 bridge 上增加 `HTMLElement.inert` 布尔反射；TEST319/999 与最近
`TEST264-319/999` 已通过。该切片只覆盖布尔属性往返，不实现焦点、键盘、无障碍树或完整
HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next353 在同一 bridge 上增加 `HTMLElement.popover` ↔ `popover` raw 反射；TEST320/999 与最近
`TEST264-320/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 popover 显示/隐藏、焦点管理、
top-layer 或完整 HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next354 在同一 bridge 上增加 `HTMLElement.autofocus` 布尔反射；TEST321/999 与重试后的最近
`TEST264-321/999` 已通过。该切片只覆盖布尔属性往返，不实现焦点调度、窗口激活或完整
HTMLElement Web IDL；首次近期门的既有 TEST266 bootstrap timeout 不作为基线；`test_host` 仍
只是宿主适配和测试消费者。
next355 在同一 bridge 上增加 `HTMLInputElement.enterKeyHint` ↔ `enterkeyhint` raw 反射；
TEST322/999 与最近 `TEST264-322/999` 已通过。该切片只覆盖 UTF-8 属性往返，不实现 SIP、
键盘布局、输入法策略或完整 input Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next356 在同一 bridge 上增加 `HTMLInputElement.virtualKeyboardPolicy` ↔
`virtualkeyboardpolicy` raw 反射；TEST323/999 与最近 `TEST264-323/999` 已通过。该切片只
覆盖 UTF-8 属性往返，不实现 SIP、虚拟键盘策略执行或完整 input Web IDL；`test_host` 仍只是
宿主适配和测试消费者。
next357 在同一 bridge 上增加 `HTMLInputElement.webkitDirectory` 布尔反射；TEST324/999 与最近
`TEST264-324/999` 已通过。该切片只覆盖布尔属性往返，不触发目录 picker、实现目录选择语义
或完整 input Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next358 在同一 bridge 上为 `HTMLInputElement.size` 接入有限整数反射；TEST325/999 与最近
`TEST264-325/999` 已通过。该切片只覆盖有限整数往返、malformed 回落和移除恢复，不声明默认
20、控件宽度、范围钳制或原生输入 UI 语义；`test_host` 仍只是宿主适配和测试消费者。
next359 在同一 bridge 上为 `HTMLTextAreaElement.cols` 接入有限整数反射；TEST326/999 与最近
`TEST264-326/999` 已通过。该切片只覆盖有限整数往返、malformed 回落和移除恢复，不声明
textarea 布局宽度或完整 Web IDL 语义；`test_host` 仍只是宿主适配和测试消费者。
next360 在同一 bridge 上为 `HTMLTextAreaElement.rows` 接入有限整数反射；TEST327/999 与最近
`TEST264-327/999` 已通过。该切片只覆盖有限整数往返、malformed 回落和移除恢复，不声明
textarea 布局高度或完整 Web IDL 语义；`test_host` 仍只是宿主适配和测试消费者。
next361 在同一 bridge 上增加 `HTMLElement.open` 布尔反射；TEST328/999 与最近
`TEST264-328/999` 已通过。该切片只覆盖布尔属性往返，不实现 details 展开布局、summary 激活、
disclosure 交互或完整 HTMLElement Web IDL；`test_host` 仍只是宿主适配和测试消费者。
next362–364 已完成 `HTMLElement.autocapitalize`、`itemValue`、`is` 的 raw UTF-8 反射；
TEST329–331 通过。next365–381 已完成 17 个 `aria-*` 对应的 HTMLElement raw UTF-8 反射，
TEST332–348 通过；这些批次不实现 ARIA 语义、可访问性树、辅助技术通知或完整 Web IDL。
next382–401 已完成 20 个 `aria-*` 对应的 HTMLElement raw UTF-8 反射（TEST349–368）；
这些批次不实现 ARIA 语义、可访问性树、辅助技术通知或完整 Web IDL。
next265 的 TEST232 真实 WM6 picker 人工入口和独立 staging INI 已通过用户人工验收；其
GUI picker 仍是宿主能力，不是产品 DLL 公共 API。
人工测试暂缓期间，next266 已先完成 input type=number 的 min/max/malformed value 核心校验，
后续优先推进不依赖人工的 form/input 自动边界；picker 人工门继续单独登记，不作为自动通过条件。
next267 已补齐 input type=number 的 min-based step/default-step/step=any 校验；下一候选为
input type=email 的 typeMismatch，不依赖人工页面观察。
next268 已补齐 input type=email 的单地址和 multiple 列表 typeMismatch；下一候选为
input type=url 的受限 typeMismatch，不依赖人工页面观察。
next269 已补齐 input type=url 的保守 scheme/relative/network-path typeMismatch；下一候选为
input type=range 的默认边界和步长约束，不依赖人工页面观察。
next270 已补齐 input type=range 的默认 0..100/显式 min/max/step 约束；下一候选为
input type=date 的 ISO 日期和边界校验，不依赖人工页面观察。
next271 已补齐 input type=date 的 bounded ISO 日历/闰年/min/max 校验；下一候选为
input type=time 的 bounded HH:MM 语法和边界校验，不依赖人工页面观察。
next272 已补齐 input type=time 的 bounded HH:MM/seconds/fraction/min/max 校验；下一候选为
input type=month 的 bounded YYYY-MM 和边界校验，不依赖人工页面观察。
next273 已补齐 input type=month 的 bounded YYYY-MM/month-range/min/max 校验；下一候选为
input type=week 的 bounded ISO 周和边界校验，不依赖人工页面观察。
next274 已补齐 input type=week 的 bounded ISO 周/week-53/min/max 校验；下一候选为
input type=datetime-local 的 bounded date/time 组合校验，不依赖人工页面观察。
next275 已补齐 input type=datetime-local 的 bounded date/time 组合、非法时间和 min/max
校验；下一候选为 input type=color 的 bounded #RRGGBB 语法校验，不依赖人工页面观察。
next276 已补齐 input type=color 的 bounded #RRGGBB/typeMismatch 校验；下一候选为
input type=date 的 min-based step 语义，不依赖人工页面观察。
next277 已补齐 input type=date 的 min-based step/默认/any/非法回退校验；下一候选为
input type=time 的 min-based step 语义，不依赖人工页面观察。
next278 已补齐 input type=time 的 min-based step/默认 60 秒/any/非法回退校验；下一候选为
input type=month 的 min-based step 语义，不依赖人工页面观察。
next279 已补齐 input type=month 的 min-based step/默认/any/非法回退校验；下一候选为
input type=week 的 min-based step 语义，不依赖人工页面观察。
next280 已补齐 input type=week 的 min-based step/默认/any/非法回退校验；下一候选为
input type=datetime-local 的 min-based step 语义，不依赖人工页面观察。
next281 已补齐 input type=datetime-local 的 min-based step/默认 60 秒/any/非法回退校验；
下一候选为 text/password controls 的 product custom validity setter，不依赖人工页面观察。
next282 已补齐 text/password controls 的 product custom validity setter/clear；下一候选为
修正 date step 在缺少 min 时优先采用有效 value 作为基准，不依赖人工页面观察。
next283 已修正 date 缺少 min 时使用固定 default value 作为 step base；下一候选为
修正 time 缺少 min 时的 value step base，不依赖人工页面观察。
next284 已修正 time 缺少 min 时使用固定 default value 作为 step base；下一候选为
修正 month 缺少 min 时的 value step base，不依赖人工页面观察。
next285 已修正 month 缺少 min 时使用固定 default value 作为 step base；下一候选为
修正 week 缺少 min 时的 value step base，不依赖人工页面观察。
next286 已修正 week 缺少 min 时使用固定 default value 作为 step base；下一候选为
修正 datetime-local 缺少 min 时的 value step base，不依赖人工页面观察。
next287 已修正 datetime-local 缺少 min 时使用固定 default value 作为 step base；下一候选为
修正 custom validity 与 required 空值同时存在时的组合 flags，不依赖人工页面观察。
next288 已修正 custom validity 与 required 空值的组合 flags；下一候选为
补齐 textarea 的 product custom validity 入口，不依赖人工页面观察。
next289 已补齐 textarea 的 product custom validity setter；下一候选为
补齐 text/password custom validity 的安全 getter/读回能力，不依赖人工页面观察。
next290 已补齐 text/password custom validity 的 UTF-8 getter；下一候选为
补齐 textarea custom validity 的安全 getter/读回能力，不依赖人工页面观察。
next291 已补齐 textarea custom validity 的 UTF-8 getter；下一候选为
验证 custom validity 状态跨 layout/reflow 的文档生命周期保持，不依赖人工页面观察。
next292 已验证 custom validity 跨 layout/reflow 的状态保持；下一候选为
补齐 range 缺省 value 的默认中点提交语义，不依赖人工页面观察。
next293 已补齐 range 缺省 value 的默认中点提交语义；下一候选为
让有效显式 min/max 下的 range 中点同时通过 text-control bridge 暴露，不依赖人工页面观察。
next294 已补齐有效显式 min/max 下 range 中点的 text-control bridge 读回、验证和提交；
next295 已补齐 programmatic file-input click 的宿主 deferred picker request，限定在可用
render window、click 未取消和单次排队，不把系统 picker 或窗口生命周期移入产品 DLL；TEST263
的真实 WM6 GUI 结果已通过用户验收。next296 已补齐 `HTMLElement.disabled` 的
attribute-backed getter/setter 和 required validation/submission 自动语义；TEST264 及相关
回归已通过，不需要人工页面验收。next297 已补齐约束相关反射属性和动态
min/max/step、readonly、form-level/button-level no-validate 语义；TEST265 及相关回归已通过，
不需要人工页面验收。
产品层必须继续保持 opaque handle、UTF-8、明确所有权、受控 callback 数和页面生命周期，
不把窗口、网络或完整 URL Standard parser 一起塞入 core/browser DLL。

### 当前短期状态：next604

next402–421、next422–441、next442–461、next462–481、next482–501、next502–521 已完成并从未完成路线图移出；
完整产品边界：encodeInto/decoder 选项、同步 Request/Response JSON、Blob Request clone、Headers
getSetCookie、Storage named/toJSON、classList/style iterator、toggleAttribute、节点 ownership、
window aliases/defaultView/open/close、Storage/HashChange/PopState/Error/Progress/Close 事件构造器、
MessagePort 生命周期与 messageerror、同 session BroadcastChannel、同步 PerformanceObserver
快照和 AbortSignal.abort；next482–501 又完成 Blob/File metadata/slice、FormData snapshot
iterators、Request/Response one-shot body、URL authority/default-port、cookie Max-Age、
NodeList/element identity、dataset、Event/MessagePort/BroadcastChannel state 和
PerformanceObserverEntryList 视图；next502–521 又完成 Headers/Request/Response ownership 与
metadata JSON、URLSearchParams/FormData snapshot、Storage/DOM wrapper tags、classList token
validation、performance entry/observer option metadata、MessagePort auto-start、AbortSignal/
Controller tags 和 Blob/File JSON metadata；next522–541 又完成 bounded Promise 构造器、微任务
时序、then/catch/finally、thenable assimilation、`resolve`/`reject`、`all`/`race`/`allSettled`/`any`
和错误/容量边界。对应 TEST429–448、TEST482–541 与相邻累计回归门均通过，且没有修改 tracked
`test_host.ini`。Bootstrap 现在分成八个顺序评估阶段以保持
`PSCRIPT_MAX_SOURCE_BYTES` 不变；这不是提升脚本预算。

next542–561 已完成一个完整的 DOM 关系与表单集合边界：`positron_core.dll` 新增按 DOM id 的
`PCore_NodeRelationById()`，提供父子/兄弟、tag/name、form owner、控件计数和索引查询；
`positron_browser.dll` 新增 size-tagged relation callback 与第九个 bootstrap IIFE，提供
`parentElement`、`children`、`contains()`、基础 `compareDocumentPosition()`、受限
`matches()`/`closest()`、元素作用域 querySelector 及 `form.elements` 的 `item()`/`namedItem()`。
缺失 id、越界、非 form 控件和不支持关系均 fail closed；没有把完整 DOM mutation、layout、shadow
tree 或 native controls 偷渡进产品层。`TEST542-561,999` 与相邻
`TEST389,390-448,482-581,999` 设备门均通过，tracked `test_host.ini` 保持
`javascript=0`。后续仍需选择新的完整能力边界，而不是把已完成能力拆成更小的反射门；涉及窗口、
布局、真实 SIP、系统 picker、旋转或网络失败反馈的候选必须另列
人工验收，不能用宿主注入日志替代。

next562–581 已完成受控属性集合边界：`positron_core.dll` 新增 attribute count/name/value
relations，`positron_browser.dll` 新增 `getAttributeNames()`、`hasAttributes()`、受限
`NamedNodeMap`/`Attr`（length/item/named lookup、同 owner mutation、identity、iterator 和
0–7 indexed slots）。定向 `TEST562-581,999` 通过 21/21；相邻
`TEST389,390-448,482-581,999` 通过 161/161，均无 ERROR/FAIL，`TESTBENCH PASS` 唯一且
`test13_route_ok=True`。这批只提供同步、ID-addressable、session-scoped 内存语义，不实现
namespace、通用 DOM mutation、live collection 或 layout；bootstrap 为十个 IIFE，browser heap
ceiling 为 576 KiB。

`next582` 作为单一批次完成受控 childNodes/CharacterData snapshot：`positron_core.dll` 的
`PCore_NodeRelationById()` 增加直接 childNodes 的数量、类型、name/value/textContent/id 关系；
`positron_browser.dll` 的第十一个 bootstrap IIFE 提供 `childNodes` NodeList、文本/注释/id-less
element wrapper、`item()`/iterator、父子/兄弟与 element-sibling 视图、`Node` 常量和只读
CharacterData metadata。`TEST582-601,999` 通过 21/21，证据为
`tmp/device-runs/20260821-150052-next582-r5/`；相邻
`TEST389,390-448,482-601,999` 通过 181/181，证据为
`tmp/device-runs/20260821-150157-next582-regression-r2/`。本批不涉及视觉、触摸、SIP、picker、
旋转或网络失败，tracked `test_host.ini` 仍保持 `javascript=0`；之后继续按一个清晰批次编号推进。

`next583` 作为单一批次在既有 childNodes snapshot 上完成 Node identity/root/position 只读边界：
`positron_browser.dll` 为 element、文本/注释/id-less element wrapper 和 document 提供
`isSameNode()`、受限 `isEqualNode()`、`getRootNode()`、`compareDocumentPosition()`、
`contains()`、document-position 常量及必要 document metadata；位置只沿当前 bounded
parent/child snapshot 计算，未知对象和跨快照关系 fail closed。`TEST602-621,999` 定向门通过
21/21，证据为 `tmp/device-runs/20260821-152913-next583/`；相邻
`TEST389,390-448,482-621,999` 通过 201/201，证据为
`tmp/device-runs/20260821-153044-next583-regression/`。本批不涉及视觉、触摸、SIP、picker、
旋转或网络失败，tracked `test_host.ini` 仍保持 `javascript=0`；后续继续按一个清晰批次编号推进。

`next584` 作为单一批次在既有 DOM snapshot 上补齐集合遍历协议：`childNodes`、`children`、
`form.elements` 和元素作用域 `querySelectorAll()` 结果提供 `forEach()`、`keys()`、`values()`、
`entries()`、可复用默认迭代器和 `Symbol.toStringTag`；`children`/`form.elements` 保留
`namedItem()`，元素作用域查询使用 `NodeList` 类型标识，不新增 core ABI。`TEST622-641,999`
通过 21/21，证据为 `tmp/device-runs/20260821-160529-next584/`；相邻
`TEST389,390-448,482-641,999` 通过 221/221，证据为
`tmp/device-runs/20260821-160635-next584-regression/`。本批仍只提供同步、session-scoped、
只读快照，不提供 live collection、节点创建、通用 mutation、复杂 selector 或 layout；
tracked `test_host.ini` 保持 `javascript=0`，因此不新增人工页面验收。

`next585` 作为单一批次在既有 relation bridge 上增加了文档结构入口：core 以三个保留结构
token 识别没有 HTML `id` 的 document root、直接 `head` 和直接 `body`，browser 暴露稳定的
`documentElement`/`head`/`body` wrapper，并接通 root selector、parent/child/sibling、
`children`/`childNodes`、identity/root/position/contains 与集合协议。真实 id 查找优先，结构
wrapper 不伪造 `id`；复杂 document selector、通用 DOM 创建/mutation、live collection 和 layout
仍不在范围内。`TEST642-661,999` 定向门与 `TEST549,642-661,999` 兼容重跑通过，最终相邻回归
`TEST389,390-448,482-661,999` 在
`tmp/device-runs/20260821-175025-next585-regression-r3/` 通过 241/241；本批不涉及视觉、触摸、SIP、picker、旋转或网络失败，tracked
`test_host.ini` 继续保持 `javascript=0`。

`next586` 作为单一批次在 browser 层完成了受控 `DocumentType` snapshot：`document.doctype` 提供
稳定的 `name`/`nodeType`/`nodeName`、owner/root/position/contains、identity/equality 和只读
leaf 关系；document `childNodes` 保持 `[doctype, documentElement]`，`document.children` 保持
element-only。该切片不新增 core ABI，不实现通用 doctype parser、节点创建、mutation、live
collection 或完整 document tree。`TEST662-681,999` 定向门在
`tmp/device-runs/20260822-135235-next586-r2/` 通过 21/21，`TEST549,642-681,999` 兼容门在
`tmp/device-runs/20260822-135350-next586-compat/` 通过 42/42，最终
`TEST389,390-448,482-681,999` 相邻回归在
`tmp/device-runs/20260822-135558-next586-regression/` 通过 261/261；三次均无
ERROR/FAIL 且 `TESTBENCH PASS` 唯一。tracked `test_host.ini` 继续保持 `javascript=0`，本批
只涉及同步脚本 API/DOM snapshot，不新增人工页面验收。

`next587` 作为单一批次在 browser 层完成了受控 Node URL/namespace metadata：document、
DocumentType、HTML element、CharacterData 和 Attr wrapper 提供只读 `baseURI`、有限
`namespaceURI`/`prefix`、`lookupNamespaceURI()` 与 `isDefaultNamespace()`；baseURI 随当前
session URL 更新，HTML namespace 与 XML prefix 的边界明确，未知值 fail closed。该切片不新增
core ABI，不实现 XML/namespace parser、prefix mutation、节点创建或通用 DOM mutation；
`TEST682-701,999` 定向门在 `tmp/device-runs/20260822-142806-next587/` 通过 21/21，兼容门
重跑在 `tmp/device-runs/20260822-143147-next587-compat-r2/` 通过 62/62，相邻回归在
`tmp/device-runs/20260822-143521-next587-regression/` 通过 281/281。三次最终门均无
ERROR/FAIL、`TESTBENCH PASS` 唯一且 `test13_route_ok=True`；本批只涉及同步脚本 API/DOM
snapshot，不新增人工页面验收，tracked `test_host.ini` 继续保持 `javascript=0`。

`next588` 作为单一批次在 browser 层完成了 bounded HTMLCollection 查询纵切：document 和
HTML element wrapper 提供 `getElementsByTagName()`/`getElementsByClassName()`，支持 HTML tag
大小写归一、通配符、规范化多 class token、DFS 文档顺序、`item()`/`namedItem()` 和完整的
当前集合迭代协议；element 查询排除 owner，document 查询包含 structural `documentElement`，
空白/未知输入 fail closed。结果是 session-scoped 静态 snapshot，不实现 live collection、
通用 selector、mutation、layout 或新的 core ABI；`test_host.exe` 只提供 fixture 和
`TEST702–721` 断言。`TEST702-721,999` 在
`tmp/device-runs/20260822-152000-next588-final/` 通过 21/21，兼容门
`TEST549,642-721,999` 在 `tmp/device-runs/20260822-152109-next588-compat-final/` 通过
82/82，相邻回归 `TEST389,390-448,482-721,999` 在
`tmp/device-runs/20260822-152431-next588-regression-final/` 通过 301/301；三次均无
ERROR/FAIL 且 `TESTBENCH PASS` 唯一。新增 bootstrap 使 576 KiB 在 TEST540 边界上不足，
browser session ceiling 已调整为 608 KiB，独立 script 默认堆仍为 512 KiB；
`tmp/device-runs/20260822-151937-next588-540-r3/` 的 TEST540/999 2/2 通过。本批只涉及
同步脚本 API/DOM snapshot，不新增视觉、触摸、SIP、picker、旋转或网络失败人工门，tracked
`test_host.ini` 继续保持 `javascript=0`。

`next589` 作为单一批次在 browser 层完成了 bounded document selector 纵切：
`document.querySelector()`/`querySelectorAll()` 复用既有 element matcher 和 wrapper cache，
支持 tag、`#id`、class、有限 attribute、compound、`*`、`:root`，按 DFS 文档顺序返回首个
匹配或 NodeList snapshot；root/head/body 结构节点保持既有 identity。空白、缺失和 `>`/`+`/`~`
组合器 fail closed，不实现完整 CSS parser、live DOM、节点创建、mutation、layout 或新的 core
ABI；`test_host.exe` 只提供 fixture 和 `TEST722–741` 断言。`TEST722-741,999` 在
`tmp/device-runs/20260822-155230-next589-r5/` 通过 21/21，兼容门
`TEST549,642-741,999` 在 `tmp/device-runs/20260822-155506-next589-compat-r2/` 通过
102/102，相邻回归 `TEST389,390-448,482-741,999` 在
`tmp/device-runs/20260822-160014-next589-regression/` 通过 321/321；三次均无 ERROR/FAIL
且 `TESTBENCH PASS` 唯一。本批只涉及同步脚本 API/DOM snapshot，不新增视觉、触摸、SIP、
picker、旋转或网络失败人工门，tracked `test_host.ini` 继续保持 `javascript=0`。

`next590` 作为单一批次在 browser 层完成 document named collection projection：
`document.getElementsByName()` 精确匹配显式 `name` 值并返回 DFS 顺序的 NodeList snapshot；
`document.forms`、`document.images` 和 `document.scripts` 返回静态 HTMLCollection，复用
`item()`/`namedItem()`、迭代协议和稳定 wrapper identity。该切片只覆盖当前 ID-addressable
bounded snapshot，不新增 core ABI、live collection、节点创建或通用 mutation；`test_host.exe`
只提供 fixture、adapter 和 `TEST742–761` 断言。定向门 `TEST742-761,999` 在
`tmp/device-runs/20260822-163511-next590-r1/` 通过 21/21，兼容门
`TEST549,642-761,999` 在 `tmp/device-runs/20260822-163636-next590-compat-r1/` 通过
122/122，相邻回归 `TEST389,390-448,482-761,999` 在
`tmp/device-runs/20260822-164253-next590-regression-r1/` 通过 341/341；三次均无
ERROR/FAIL 且 `TESTBENCH PASS` 唯一。本批只涉及同步脚本 API/DOM snapshot，不新增视觉、
触摸、SIP、picker、旋转或网络失败人工门，tracked `test_host.ini` 继续保持 `javascript=0`。

`next591` 作为单一批次在 browser 层完成 hyperlink collection projection：
`document.links` 按 DFS 顺序收集显式 `href` 的 `a`/`area`，`document.anchors` 按 DFS 顺序收集
显式 `name` 的 `a`，两者返回静态 HTMLCollection，复用 `item()`/`namedItem()`、迭代协议和
稳定 wrapper identity。该切片只覆盖当前 bounded snapshot，不新增 core ABI、live collection、
节点创建、通用 mutation 或 URL parser；`test_host.exe` 只提供 fixture、adapter 和
`TEST762–781` 断言。定向门 `TEST762-781,999` 在
`tmp/device-runs/20260822-171429-next591-r1/` 通过 21/21，兼容门
`TEST549,642-781,999` 在 `tmp/device-runs/20260822-171552-next591-compat-r1/` 通过
142/142；缩减回归 `TEST389,390-448,540,549,642-781,999` 在
`tmp/device-runs/20260822-172242-next591-regression-r1/` 通过 203/203。三次均无 ERROR/FAIL
且 `TESTBENCH PASS` 唯一。本批未跑旧的 341 项全回归，但保留核心事件、TEST540 内存边界、
TEST549 和 next642–781 风险区间；本批只涉及同步脚本 API/DOM snapshot，不新增视觉、触摸、
SIP、picker、旋转或网络失败人工门，tracked `test_host.ini` 继续保持 `javascript=0`。

`next592` 作为单一批次在 browser 层完成 namespace-aware element collection projection：
document 与 element 的 `getElementsByTagNameNS()` 复用既有 DFS snapshot 和 namespace metadata，
支持 `*`/精确 namespace、`*`/大小写敏感 localName、document root 包含和 element owner 排除，
结果为静态 HTMLCollection 并复用既有集合协议与 wrapper identity。null、空或未知 namespace/
localName fail closed；不新增 XML/SVG parser、live collection、节点创建、通用 mutation 或 core
ABI，`test_host.exe` 只提供 fixture、adapter 和 `TEST782–801` 断言。定向门
`TEST782-801,999` 在 `tmp/device-runs/20260822-192042-next592-r2/` 通过 21/21，兼容门
`TEST549,642-801,999` 在 `tmp/device-runs/20260822-192203-next592-compat-r1/` 通过
162/162；缩减回归 `TEST389,390-448,540,549,642-801,999` 在
`tmp/device-runs/20260822-192923-next592-regression-r1/` 通过 223/223。三次均无 ERROR/FAIL
且 `TESTBENCH PASS` 唯一。本批只涉及同步脚本 API/DOM snapshot，不新增视觉、触摸、SIP、
picker、旋转或网络失败人工门，tracked `test_host.ini` 继续保持 `javascript=0`。

`next593` 作为单一批次在 browser 层完成 namespace-aware attribute read projection：
`getAttributeNS()`、`hasAttributeNS()`、`getAttributeNodeNS()` 以及 Attr 的
`namespaceURI`/`prefix`/`localName` 元数据复用既有 attribute snapshot 和 owner wrapper。
null/空 namespace 表示无 namespace，`xml`/`xmlns` 映射已知 XML/XMLNS namespace，未知前缀或
namespace fail closed；没有加入 namespace mutation、XML/SVG parser、节点创建、live collection
或新的 core ABI。`TEST802-821,999`、`TEST549,642-821,999`、
`TEST389,390-448,540,549,642-821,999` 分别在
`tmp/device-runs/20260822-201726-next593-r2/`、
`tmp/device-runs/20260822-201838-next593-compat-r2/`、
`tmp/device-runs/20260822-202712-next593-regression-r2/` 通过 21/21、182/182、243/243，
均无 ERROR/FAIL 且 `TESTBENCH PASS` 唯一。本批不涉及视觉、触摸、SIP、picker、旋转或网络失败，
tracked `test_host.ini` 继续保持 `javascript=0`，因此不新增人工页面验收。

`next594` 作为单一批次在 browser 层为既有 `NamedNodeMap` 增加只读的
`getNamedItemNS(namespace, localName)`。它复用 next593 的 null/空 namespace、XML/XMLNS 已知
前缀、未知输入 fail-closed、大小写敏感 localName、String coercion 和稳定 Attr wrapper
语义；保留的 map 会观察后续属性增删和值更新。没有加入 `setNamedItemNS()`、
`removeNamedItemNS()`、XML/SVG parser、namespace mutation、节点创建、live collection 或新的
core ABI。`TEST822-841,999` 在 `tmp/device-runs/20260822-204905-next594-r1/` 通过 21/21，
缩减回归 `TEST389,390-448,540,549,642-841,999` 在
`tmp/device-runs/20260822-205012-next594-regression-r1/` 通过 263/263；兼容子集沿用
next593 的 `TEST549,642-821,999` 证据。两次均无 ERROR/FAIL 且 `TESTBENCH PASS` 唯一，
本批不涉及视觉、触摸、SIP、picker、旋转或网络失败，tracked `test_host.ini` 继续保持
`javascript=0`，因此不新增人工页面验收。

`next595` 作为单一批次在 browser 层补齐受控的 `lookupPrefix(namespace)`：document、
DocumentType、HTML element、CharacterData 和 Attr wrapper 都支持已知 XML namespace 的
`xml` 映射；XMLNS 的 `xmlns` 只对对应 `xmlns:*` Attr 暴露；HTML default、null/空值和未知
namespace fail closed，并接受有限 String coercion。该批不改 core relation ABI，不实现
namespace declaration/parser、prefix mutation、节点创建或 live collection。`TEST842-861,999`
通过 21/21，证据为 `tmp/device-runs/20260822-211732-next595-r1/`；缩减回归
`TEST389,390-448,540,549,642-861,999` 通过 283/283，证据为
`tmp/device-runs/20260822-211920-next595-regression-r1/`。两次均无 ERROR/FAIL 且
`TESTBENCH PASS` 唯一；本批只涉及同步脚本 API/DOM snapshot，不涉及视觉、触摸、SIP、picker、
旋转或网络失败，tracked `test_host.ini` 继续保持 `javascript=0`，因此不新增人工页面验收。

`next596` 作为单一批次在 browser 层补齐有界 namespace mutation：元素 wrapper 提供
`setAttributeNS(namespace, qualifiedName, value)` 与 `removeAttributeNS(namespace, localName)`。
null/空 namespace 只接受无前缀名称，XML/XMLNS 只接受对应 `xml`/`xmlns` 前缀；未知 URI、未知
prefix、空名称和多重冒号安全无操作。成功写入复用既有 attribute bridge，不改 core relation ABI，
不实现完整 NamespaceError、namespace declaration、XML/SVG parser、节点创建或 live collection。
`TEST862-881,999` 通过 21/21，证据为 `tmp/device-runs/20260823-095421-next596-r2/`；缩减回归
`TEST389,390-448,540,549,642-881,999` 通过 303/303，证据为
`tmp/device-runs/20260823-095546-next596-regression-r1/`。两次均无 ERROR/FAIL 且
`TESTBENCH PASS` 唯一；本批只涉及同步脚本 API/DOM snapshot，不涉及视觉、触摸、SIP、picker、
旋转或网络失败，tracked `test_host.ini` 继续保持 `javascript=0`，因此不新增人工页面验收。

`next597` 作为单一批次在 browser 层补齐 namespace-aware `NamedNodeMap`/Attr mutation：增加
`setNamedItemNS()`、`removeNamedItemNS()` 和元素 `setAttributeNodeNS()`。null/空 namespace、
XML/XMLNS 仍复用 next596 的有限名称边界；跨 owner Attr 只复制名称和值，绝不转移 source
owner 或 wrapper identity，未知/非法输入和缺失删除 fail closed。该批不改 core relation ABI，
不实现完整 NamespaceError、namespace declaration/parser、XML/SVG parser、节点创建或 live
collection。`TEST882-901,999` 在 `tmp/device-runs/20260823-103228-next597-r3/` 通过 21/21，
缩减 namespace 回归 `TEST802-901,999` 在
`tmp/device-runs/20260823-103700-next597-regression-r2/` 通过 101/101；两次均无 ERROR/FAIL
且 `TESTBENCH PASS` 唯一。本批只涉及同步脚本 API/DOM snapshot，不新增视觉、触摸、SIP、
picker、旋转或网络失败人工门，tracked `test_host.ini` 继续保持 `javascript=0`。

`next598` 作为单一批次在 browser 层补齐 Attr 的 bounded leaf-node 语义：`isId`、live
`textContent`、空 `childNodes`/`hasChildNodes()`、null parent/sibling/child relations，以及
identity-based `isSameNode()` 与受控 name/value `isEqualNode()`。Attr 不被伪造为 element tree 的
子节点，也不扩展 core ABI 或通用 DOM mutation。`TEST902-921,999` 在
`tmp/device-runs/20260823-105508-next598/` 通过 21/21，缩减回归 `TEST802-921,999` 在
`tmp/device-runs/20260823-105630-next598-regression/` 通过 121/121；两次均无 ERROR/FAIL 且
`TESTBENCH PASS` 唯一。本批只涉及同步脚本 API/DOM snapshot，不新增视觉、触摸、SIP、picker、
旋转或网络失败人工门，tracked `test_host.ini` 继续保持 `javascript=0`。

`next599` 作为单一批次在 browser 层补齐 Attr 的 detached-node relation 语义：`isConnected` 固定
为 false，`getRootNode()` 返回自身，`contains()` 只对自身返回 true，`compareDocumentPosition()`
对自身返回 0、对其他对象返回固定 `DISCONNECTED|IMPLEMENTATION_SPECIFIC`。Attr 仍不挂入
element tree，不扩展 core ABI 或通用 DOM mutation。`TEST922-941,999` 在
`tmp/device-runs/20260823-111516-next599/` 通过 21/21，缩减回归 `TEST802-941,999` 在
`tmp/device-runs/20260823-111631-next599-regression/` 通过 141/141；两次均无 ERROR/FAIL 且
`TESTBENCH PASS` 唯一。本批只涉及同步脚本 API/DOM snapshot，不新增视觉、触摸、SIP、picker、
旋转或网络失败人工门，tracked `test_host.ini` 继续保持 `javascript=0`。

`next600` 作为单一批次在 browser 层补齐 childNodes wrapper 的 bounded `contains()`：文本、注释
和无 id 子节点 wrapper 自包含，父元素可包含直接子节点，但 child wrapper 不把 owner、兄弟、
document 或非法对象误判为后代。实现复用受控 relation bridge，不扩展 core ABI、文本 mutation、
节点创建或 live collection。`TEST942-961,999` 在 `tmp/device-runs/20260823-113402-next600/`
通过 21/21，缩减回归 `TEST802-961,999` 在
`tmp/device-runs/20260823-113512-next600-regression/` 通过 161/161；两次均无 ERROR/FAIL 且
`TESTBENCH PASS` 唯一。本批只涉及同步脚本 API/DOM snapshot，不新增视觉、触摸、SIP、picker、
旋转或网络失败人工门，tracked `test_host.ini` 继续保持 `javascript=0`。

`next601` 作为单一批次为既有 `document.doctype` snapshot 补齐只读 `publicId`、`systemId` 和
`internalSubset` 元数据；HTML doctype 默认值为空字符串、空字符串和 `null`，不可重定义或删除，
且不改变既有 DocumentType 关系、identity、namespace 或 baseURI。`TEST962-981,999` 在
`tmp/device-runs/20260823-115525-next601/` 通过 21/21，缩减回归 `TEST802-981,999` 在
`tmp/device-runs/20260823-115645-next601-regression/` 通过 181/181；两次最终运行均无
ERROR/FAIL 且 `TESTBENCH PASS` 唯一。本批只涉及同步脚本 API/DOM snapshot，不新增视觉、触摸、
SIP、picker、旋转或网络失败人工门，tracked `test_host.ini` 继续保持 `javascript=0`。

`next602` 作为单一批次在 browser 层为 `document.doctype` 增加独立、稳定、冻结的空
`entities` 与 `notations` `NamedNodeMap` snapshot。两者支持有限的 `length`/indexed
`item()`/named lookup/namespace lookup/iterator/branding 语义，但不解析 DTD/实体，mutation
方法 fail closed，不创建节点，也不扩展 core ABI；实现 lazy 复用既有 `m10(null)` 以避免脚本堆
重复占用。`TEST982-998,999` 在 `tmp/device-runs/20260823-122704-next602/` 通过 18/18，
缩减回归 `TEST802-998,999` 在 `tmp/device-runs/20260823-122827-next602-regression/` 通过
198/198；最终运行均无 ERROR/FAIL 且 `TESTBENCH PASS` 唯一。本批只涉及同步脚本 API/DOM
snapshot，不新增视觉、触摸、SIP、picker、旋转或网络失败人工门，tracked `test_host.ini` 继续
保持 `javascript=0`。

`next603` 作为单一批次为普通属性 `NamedNodeMap` 以及 doctype 的空 `entities`/`notations` map
补齐有界的 `forEach()`、`keys()`、`values()`、`entries()` 和默认 values iterator。每次迭代读取
当前属性名快照，Attr wrapper 复用既有 identity；iterator 自身可迭代，非法 callback fail closed，
不提供 live collection、节点创建、DTD/实体解析或新的 core ABI。`TEST1000-1017,999` 在
`tmp/device-runs/20260823-125404-next603-r2/` 通过 19/19，缩减回归使用拆开的特殊 999：
`TEST802-998,1000-1017,999` 在 `tmp/device-runs/20260823-131725-next603-regression-final/`
通过 216/216；最终运行均无 ERROR/FAIL 且 `TESTBENCH PASS` 唯一。本批只涉及同步脚本 API/DOM
snapshot，不新增视觉、触摸、SIP、picker、旋转或网络失败人工门，tracked `test_host.ini` 继续
保持 `javascript=0`。

`next604` 作为单一批次为静态 `HTMLCollection` snapshot 增加只读、不可枚举的 `id`/`name` 直达
属性；`item()`、`namedItem()`、数字索引、迭代器和 wrapper identity 保持不变，reserved member
不被覆盖，`NodeList` 不获得 named projection。属性只在 snapshot 创建时定义，不承诺 live 更新，
不创建节点、不做 mutation、不扩展 core ABI。`TEST1018–1035,999` 在
`tmp/device-runs/20260823-134449-next604-r2/` 通过 19/19，缩减回归拆开特殊 999 后以
`TEST802–998,1000–1035,999` 在 `tmp/device-runs/20260823-134557-next604-regression/`
通过 234/234；两次最终运行均无 ERROR/FAIL 且 `TESTBENCH PASS` 唯一。本批只涉及同步脚本
API/DOM snapshot，不新增视觉、触摸、SIP、picker、旋转或网络失败人工门，tracked `test_host.ini`
继续保持 `javascript=0`。

## 中期目标

### 浏览器 JavaScript 与 Web 平台

- 继续补齐有真实页面价值的 location/history 语义，而不是自写完整 URL parser。
- 评估 cookies、简单 storage 和更完整的页面生命周期；每项都需要明确持久化、配额和失败策略。
- 扩展 DOM/Event 时保持 size-tagged ABI、受控 native callback 数和文档生命周期。
- 评估模块、异步任务或计时器前，先解决取消、关闭、导航替换和执行预算。

### 页面兼容与布局

- 从可重复真实页面缺口选择基础 Grid、background size/repeat 或其他高价值能力。
- Float 只有在完整 box construction/normalisation 方案存在时才能重启；禁止恢复旧 TEST79 实验。
- 补齐复杂 positioning、table 边界、overflow 和 CSS Lists 时，继续同时验证高 DPI 与旋转。
- 保持 TEST13 深层导航和旧页失败回滚，不让离线几何通过覆盖真实页面回归。

### 输入、表单与事件

- 完整真实 SIP/IME composition、候选词、Unicode 和 preedit 生命周期。
- 类型/范围/step、custom validity、native invalid UI、完整 `invalid` 事件传播和首个无效控件反馈。
- label、Enter、multiple select、文件选择和 native control 视觉/焦点行为。
- 区分 synthetic event、WM 消息和真实用户输入的证据。

### 网络、资源与安全

- 在 MSVC9/WM6 约束下评估受维护 TLS 方案，避免永久停留在 Mbed TLS 2.16.12。
- 统一 CSS、图片、脚本和后续资源的 URL、失败、redirect、预算与缓存策略。
- 完善整页进度和失败反馈，但不在未确认线程安全前把 DOM/GDI 移到 worker。

### 公共 DLL 生态

- 让 TLS、JSON、HTTP、image、script、core 都能被独立 WM 程序使用。
- 以真实外部消费者需求决定新增 API，不让 `test_host` 私有结构泄漏。
- 继续保持 C ABI、UTF-8、opaque handle、跨 CRT 所有权和版本化结构。

## 长期目标

- 从轻量浏览器发展为可编写 Positron 应用的运行时。
- 提供受控窗口/导航、fetch、文件、本地存储和 native bridge。
- 建立固定轻量真实网页集，覆盖文本、图片、表格、flex/nav、表单和脚本。
- 持续管理图片、CSS、字体、脚本和页面缓存的内存上限与释放。
- 在真实设备数据证明需要时做重排节流、绘制剔除和热点优化。

## 每批完成标准

1. 只有一个清晰能力边界。
2. 正例、反例、共享旧路径和失败行为都有自动断言。
3. C 改动通过 C89 回归和正式 ARMV4I 构建。
4. 候选包整体 stage，不混用 DLL/EXE。
5. 风险相关设备门通过；达到阈值或出现异常时跑全量门。
6. 必需人工检查完成，或明确列入允许累计清单。
7. `HANDOFF` 覆写当前状态，`KNOWN_LIMITATIONS` 只更新剩余边界，完成项退出本路线图。
8. 只提交本批 tracked 文件并推送当前分支；`tmp/` 永不提交。

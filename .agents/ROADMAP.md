# Positron 路线图

更新时间：2026-08-19

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
next239 已迁移 DOM attribute callback，next240 已迁移 Event callback，next241 已迁移 input value callback，next242 已迁移 checked callback，next243 已迁移 form-property callback（defaultValue/defaultChecked/selectedIndex），next244 已迁移 navigation JSON callback dispatch，next245 已迁移同文档 location/history 事件分发，next246 已迁移 native input/composition typed dispatch entry，next247 已迁移 native keyboard typed dispatch entry，next248 已迁移 focus-family typed dispatch entry，next249 已迁移 native SELECT change typed dispatch entry，next250 已迁移 native SELECT input typed dispatch entry，next251 已迁移 native EDIT change typed dispatch entry，next252 已迁移 native EDIT post-change input typed dispatch entry，next253 已迁移 native click typed dispatch entry，next254 已迁移 native submit/reset typed dispatch entry，next255 已迁移 native invalid typed dispatch entry，next256 已迁移 file-input input/change typed dispatch entry，next257 已迁移 checkbox/radio change typed dispatch entry，next258 已迁移 checkbox/radio input typed dispatch entry，next259 已迁移 checkbox/radio label/native activation click target and event ordering，next260 已迁移 checkbox/radio keyboard activation through the existing key/click/input/change contracts，next261 已迁移 checkbox/radio programmatic `HTMLElement.click()` through a product callback and the existing click/input/change contracts，next262 已迁移 native submit/reset/button 的 programmatic `HTMLElement.click()` through typed click/form-event and existing host defaults，next263 已让 native file input 的 programmatic `HTMLElement.click()` 复用 typed click contract，并明确自动脚本与系统 picker 的宿主 GUI 边界，next264 已把宿主 GUI picker 的选择/取消/错误与 file input 提交通知拆成可注入的同步 adapter；next265 的 TEST232 真实 WM6 picker 已完成选择/同窗口取消的人工验收。next295 已补齐可见 render window 中的 deferred picker request：脚本回调返回后由宿主消息循环排队一次，再复用既有 picker adapter；TEST262 自动边界和 TEST263 真实 GUI 均已通过。next296 已通过既有 attribute bridge 暴露 `HTMLElement.disabled`，TEST264 及相关自动回归已通过。导航的窗口、网络和 history side effect、core 事件传播、焦点与控件默认行为仍由宿主 typed adapter 提供。`test_host` 只能作为
宿主适配和测试消费者；
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
回归已通过，不需要人工页面验收。
产品层必须继续保持 opaque handle、UTF-8、明确所有权、受控 callback 数和页面生命周期，
不把窗口、网络或完整 URL Standard parser 一起塞入 core/browser DLL。

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
- 类型/范围/step、custom validity、`invalid` 事件和首个无效控件反馈。
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

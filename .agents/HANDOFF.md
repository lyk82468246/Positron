# Positron 当前交接

更新时间：2026-08-20

稳定使命、架构和公共边界见
[`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。本文件只记录当前仓库基线、最近设备证据、
当前边界和唯一下一步。

下方按批次保留当时的验证语境；若历史条目与本文件顶部或“唯一下一步”冲突，以当前条目为准。

## Git 与仓库基线

- 分支：`main`，跟踪 `origin/main`。
- 最新已验证产品基线：next324（本批采用定向门；最近一次完整自动基线仍为 next255）。
- next294 批次让有效显式 min/max 下的 range 缺省中点同时通过 text-control bridge 读回、验证和
  successful-control submission；没有新增 native slider 视觉/触摸声明。
- next295 在 `test_host` 宿主中把可见 render window 内、未取消的 file-input
  `HTMLElement.click()` 延迟排队到窗口消息循环，再复用既有 picker adapter；不新增
  `positron_browser.dll` ABI，也不让脚本回调重入系统对话框。TEST262 自动通过，TEST263
  已由用户在真实 WM6 GUI 中验收：选择后显示文件名和一次 `input`/`change`，同窗口
  Cancel 保持原状态。
- next296 在产品 bootstrap 中通过既有 attribute bridge 暴露 `HTMLElement.disabled` 布尔属性；
  TEST264 自动断言 getter/setter、required validation 和 successful-control submission 的
  禁用/启用语义，无需人工页面验收。
- next297 在同一 attribute bridge 上补齐 `required`、`readOnly`、`multiple`、`noValidate`、
  `formNoValidate` 和 `min`/`max`/`step` 反射；TEST265 自动覆盖属性往返、动态范围约束、
  readonly 绕过/恢复、form-level/button-level no-validate 和 successful-control submission，
  不需要人工页面验收。
- next298 在 `positron_core.dll` 新增按 DOM id 的控件约束状态查询，并在
  `positron_browser.dll` 增加独立 size-tagged validation callback；bootstrap 现在提供
  `checkValidity()`、`willValidate` 和基础 `validity` flags。TEST266 自动覆盖 required、
  disabled、readonly、select、number 下溢/上溢/step mismatch 及动态恢复；不触发 invalid
  事件、不声称 form-level validation 或 native invalid UI。
- next299 在 `positron_core.dll` 增加按 DOM id 的 custom-validity UTF-8 message set/get，
  扩展当前 form-control candidates 的 `customError`；`positron_browser.dll` 增加独立
  size-tagged custom-validity callback，bootstrap 现在提供控件级 `setCustomValidity()` 和
  `validationMessage`。TEST267 自动覆盖 text、number、textarea、select、checkbox 的
  set/get/clear、core 直接 API 和 button unsupported 边界；不触发 invalid 事件、不声称
  form-level validation 或 native invalid UI。为容纳该受控 callback，独立 script native
  function 上限从 16 提升为 17。
- next300 在 `positron_core.dll` 增加按 DOM id 的 form 约束聚合查询
  `PCore_FormValidationById`，忽略 form `novalidate` 并跳过 disabled/readonly/submit 等不参与
  constraint validation 的控件；现有 browser validation callback 对 form id 返回聚合 `valid`，
  bootstrap 因而提供受限的 form `checkValidity()` 查询。TEST268 自动覆盖 required、custom
  validity、number range、动态禁用/恢复和 `novalidate` 语义；不触发 invalid 事件、不提交表单，
  不声称 `reportValidity()` 或 native invalid UI。
- next301 在 `positron_core.dll` 增加 `PCore_FormReportValidityById`，沿用 form 约束候选规则，
  按 DOM 顺序为有非空 id 的 invalid controls 派发 trusted、non-bubbling、cancelable `invalid`
  事件；`positron_browser.dll` 增加独立 size-tagged report-validity callback，bootstrap 现在提供
  form/control `reportValidity()`。TEST269 自动覆盖事件目标/顺序、动态恢复、`preventDefault()`
  不改变 boolean 结果、`novalidate` 和 disabled/readonly 边界；不提供 native validation UI、
  焦点/滚动、提交或本地化提示。为容纳该受控 callback，独立 script native function 上限从 17
  提升为 18。
- next302 在 `positron_core.dll` 增加 `PCore_FormGetValidationMessageById`，让现有 browser
  custom-validity getter 在没有 application-owned message 时返回固定英文 validity fallback；
  TEST270 自动覆盖 required/range/type mismatch、custom message 优先级、动态清除和安全截断，
  不做本地化或 native validation UI。
- next303 在现有 attribute bridge 上增加 `pattern`、`minLength`、`maxLength` 的脚本反射；
  `minLength`/`maxLength` 使用非负整数 getter/setter，动态属性更新继续驱动
  `tooShort`/`tooLong`/`patternMismatch` 与 `checkValidity()`。TEST271 自动覆盖 input 与
  textarea、动态恢复和负数/非有限 setter 拒绝，不涉及视觉或人工验收。
- next304 在同一 attribute bridge 上增加表单 `name` 属性反射；TEST272 自动覆盖 form、
  input、textarea、select、button 的 getter/setter、attribute round-trip，以及动态改名后的
  successful-control submission，不涉及视觉或人工验收。
- next305 在同一 attribute bridge 上增加 form `action`/`method` 属性反射；TEST273 自动覆盖
  getter/setter、attribute round-trip，以及动态更新后的受限 GET/urlencoded-POST submission
  目标和 method 判定，不涉及视觉或人工验收。
- next306 在同一 attribute bridge 上增加 form `enctype` 属性反射；TEST274 自动覆盖
  getter/setter、attribute round-trip，以及动态 urlencoded/multipart submission 切换和恢复，
  不涉及视觉或人工验收。
- next307 在同一 attribute bridge 上增加 submitter `formAction` 属性反射；TEST275 自动覆盖
  getter/setter、attribute round-trip，以及 urlencoded/multipart action 覆盖和移除恢复，不涉及
  视觉或人工验收。
- next308 在同一 attribute bridge 上增加 submitter `formMethod` 属性反射；TEST276 自动覆盖
  getter/setter、attribute round-trip，以及 get/post method 覆盖和移除恢复，不涉及视觉或人工验收。
- next309 在同一 attribute bridge 上增加 submitter `formEnctype` 属性反射；TEST277 自动覆盖
  getter/setter、attribute round-trip，以及 urlencoded/multipart 覆盖、snapshot eligibility 和移除恢复，
  不涉及视觉或人工验收。
- next310 验证 text-input 隐式 Enter submission 与显式首个 submitter 共用 action/method/enctype
  override 及 multipart snapshot；TEST278 自动覆盖，不涉及视觉或人工验收。
- next311 在同一 attribute bridge 上增加 form `target` 属性反射；TEST279 自动覆盖
  getter/setter、attribute round-trip、移除恢复以及 submission action/method 不变，不涉及视觉或人工验收。
- next312 在同一 attribute bridge 上增加 form `autocomplete` 属性反射；TEST280 自动覆盖
  getter/setter、attribute round-trip、移除恢复以及 submission 不变，不涉及视觉或人工验收。
- next313 在同一 attribute bridge 上增加 form `acceptCharset` ↔ `accept-charset` 属性反射；
  TEST281 自动覆盖 getter/setter、attribute round-trip、移除恢复以及 submission 不变，不涉及视觉或人工验收。
- next314 在同一 attribute bridge 上增加控件 `placeholder` 属性反射；TEST282 自动覆盖
  getter/setter、attribute round-trip、移除恢复以及 current value/submission 不变，不涉及视觉或人工验收。
- next315 在同一 attribute bridge 上确认 input `autocomplete` 属性反射；TEST283 自动覆盖
  getter/setter、attribute round-trip、移除恢复以及 current value/submission 不变，不涉及视觉或人工验收。
- next316 在同一 attribute bridge 上增加 input `inputMode` ↔ `inputmode` 属性反射；TEST284 自动覆盖
  getter/setter、attribute round-trip、移除恢复以及 current value/submission 不变，不涉及视觉或人工验收。
- next317 在同一 attribute bridge 上增加 input `type` raw 属性反射；TEST285 自动覆盖 getter/setter、
  attribute round-trip、移除恢复以及既有 text-control current value/submission 不变，不涉及动态控件重建、
  完整 Web IDL type 规范、native type UI、视觉或人工验收。
- next318 为 textarea 的既有 `placeholder` raw 反射补充独立设备覆盖；TEST286 自动覆盖 getter/setter、
  attribute round-trip、移除恢复以及 textarea current value/submission 不变，不涉及 placeholder 绘制、
  SIP、原生提示 UI 或完整 textarea Web IDL 语义。
- next319 为 select 的既有 `autocomplete` raw 反射补充独立设备覆盖；TEST287 自动覆盖 getter/setter、
  attribute round-trip、移除恢复以及选中值/submission 不变，不涉及自动填充策略、凭据存储或完整
  select Web IDL 语义。
- next320 为 button `type` raw 反射增加 submitter 边界覆盖；TEST288 自动覆盖 getter/setter、
  attribute round-trip、移除恢复以及恢复为 submit 后的 successful-control submission，不涉及动态控件
  重建、完整 button Web IDL 或 native button UI。
- next321 验证未知 form `method` 原始值反射与 submission 的安全 GET fallback；TEST289 自动覆盖
  getter/setter、attribute round-trip、移除恢复和 action/body 不变，不实现其他 HTTP 方法、规范化或
  导航副作用。
- next322 验证未知 form `enctype` 原始值反射与 urlencoded POST fallback；TEST290 自动覆盖
  getter/setter、attribute round-trip、移除恢复和 action/body 不变，不实现 enctype 规范化、multipart
  传输或其他编码格式。
- next323 验证 method/enctype 的大小写不敏感匹配与 raw case 反射；TEST291 自动覆盖 mixed-case
  getter/setter、attribute round-trip、移除恢复和 urlencoded POST action/body 不变，不实现规范化
  getter、完整 Web IDL 枚举语义或导航副作用。
- next324 验证动态 action/method/value 更新后反复重排的 submission metadata 一致性；TEST292 自动
  覆盖 method、action/body 及 `action_bytes/body_bytes` 在两次 viewport 重排后保持正确，不实现导航
  提交、异步任务或完整浏览器生命周期。
- next293 批次让 range 缺省 value 在默认/有效 min/max 范围中点上生成成功控件值；
  没有新增 native slider 视觉/触摸声明。
- next292 批次验证 custom validity 状态跨 `PCore_LayoutDocument` 重排保持；没有新增视觉/触摸
  声明，也没有宣称完整 DOM 生命周期。
- next291 批次新增 textarea custom validity UTF-8 getter，与 setter 形成成对 API；
  没有宣称完整 DOM `validationMessage`。
- next290 批次新增 text/password custom validity UTF-8 getter，定义完整字节长度和安全截断；
  没有宣称完整 DOM `validationMessage`。
- next289 批次新增 textarea-indexed product custom validity setter，并让 textarea validation
  保留 `PCORE_VALIDITY_CUSTOM_ERROR`；没有宣称完整 DOM `setCustomValidity()` 或 native invalid UI。
- next288 批次修正 text/password custom validity 与 required 空值同时存在时保留
  `valueMissing|customError` 组合 flags；没有宣称完整 DOM `setCustomValidity()` 或 native invalid UI。
- next287 批次修正 input type=datetime-local 在没有有效 min 时以固定 default value 作为 step base；
  没有新增 native datetime picker 或视觉/触摸声明。
- next286 批次修正 input type=week 在没有有效 min 时以固定 default value 作为 step base；
  没有新增 native week picker 或视觉/触摸声明。
- next285 批次修正 input type=month 在没有有效 min 时以固定 default value 作为 step base；
  没有新增 native month picker 或视觉/触摸声明。
- next284 批次修正 input type=time 在没有有效 min 时以固定 default value 作为 step base；
  没有新增 native time picker 或视觉/触摸声明。
- next283 批次修正 input type=date 在没有有效 min 时以当前有效 value 作为 step base；没有
  新增 native date picker 或视觉/触摸声明。
- next282 批次把 text/password controls 的 product custom validity setter/clear 和
  `PCORE_VALIDITY_CUSTOM_ERROR` 接入 positron_core.dll；没有宣称完整 DOM `setCustomValidity()`
  或 native invalid UI。
- next281 批次把 input type=datetime-local 的 min-based step（秒）和 default/any/fallback
  核心校验接入 positron_core.dll；没有新增 native datetime picker 或视觉/触摸声明。
- next280 批次把 input type=week 的 min-based step（周）和 default/any/fallback 核心校验
  接入 positron_core.dll；没有新增 native week picker 或视觉/触摸声明。
- next279 批次把 input type=month 的 min-based step（月）和 default/any/fallback 核心校验
  接入 positron_core.dll；没有新增 native month picker 或视觉/触摸声明。
- next278 批次把 input type=time 的 min-based step（秒）和 default/any/fallback 核心校验
  接入 positron_core.dll；没有新增 native time picker 或视觉/触摸声明。
- next277 批次把 input type=date 的 min-based step（天）和 default/any/fallback 核心校验
  接入 positron_core.dll；没有新增 native date picker 或视觉/触摸声明。
- next276 批次把 input type=color 的 bounded #RRGGBB typeMismatch 核心校验接入
  positron_core.dll；没有新增 native color picker 或视觉/触摸声明。
- next275 批次把 input type=datetime-local 的 bounded date/time 组合和 min/max 核心校验接入
  positron_core.dll；没有新增 native datetime picker 或视觉/触摸声明。
- next274 批次把 input type=week 的 bounded ISO YYYY-Www、week-53 规则和 min/max 核心
  校验接入 positron_core.dll；没有新增 native week picker 或视觉/触摸声明。
- next273 批次把 input type=month 的 bounded YYYY-MM、月份范围和 min/max 核心校验接入
  positron_core.dll；没有新增 native month picker 或视觉/触摸声明。
- next272 批次把 input type=time 的 bounded HH:MM/seconds/fraction、无效时间和 min/max
  核心校验接入 positron_core.dll；没有新增 native time picker 或视觉/触摸声明。
- next271 批次把 input type=date 的 bounded YYYY-MM-DD 日历、闰年和 min/max 核心校验接入
  positron_core.dll；没有新增 native date picker 或视觉/触摸声明。
- next270 批次把 input type=range 的默认 0..100、显式 min/max 和 step 约束接入
  positron_core.dll；range 仍没有宣称 native slider 视觉或真实触摸行为。
- next269 批次把 input type=url 的保守 typeMismatch 核心校验接入 positron_core.dll：支持
  scheme、relative 和 network-path 形式，拒绝空 authority/空白；明确不实现完整 URL Standard。
- next268 批次把 input type=email 的 typeMismatch 核心校验接入 positron_core.dll：支持单地址、
  multiple 逗号列表、ASCII 空白裁剪和动态修复；没有改变 native control 的视觉或真实 SIP 行为。
- next267 批次把 input type=number 的 step mismatch 接入核心约束校验：min 是步长基准，
  缺省/非法/非正 step 保守回退到 1，step=any 放行，浮点边界使用有限容差；没有改变
  native control 的视觉或真实 SIP 行为。
- next266 批次把 `input type=number` 的核心约束校验接入 `positron_core.dll`：合法的
  `min`/`max` 产生 inclusive range 结果，非法数值产生 `badInput` 标志，malformed
  range 属性保守忽略；没有改变 native control 的视觉或真实 SIP 行为。
- next264 批次把宿主 GUI picker 的系统调用、选择/取消/错误结果、core 文件状态提交和
  `input` → `change` 通知拆成可注入的同步 host-only adapter；真实 WM6 picker 仍由
  `GetOpenFileNameEx` 触发，未新增产品 ABI，也未改变程序化 `HTMLElement.click()` 边界。
- next265 新增的 host-only TEST232、显式 `javascript=1` 的真实 WM6 picker 页面、以及
  `scripts\stage_manual_picker.bat` 已完成用户人工验收：真实选择后文件名和
  `input|file;change|file;` trace 可见，同一 render 窗口再次打开并 Cancel 后状态保持；
  关闭后重新运行创建新 fixture。该验收不把 picker 或页面 fixture 迁入产品 DLL。
- next263 批次让 native file input 的程序化 `HTMLElement.click()` 先通过既有 typed click
  contract；disabled 控件静默，自动脚本不打开系统 picker，picker、文件系统权限和窗口生命周期
  仍由宿主 GUI 路径拥有。TEST228 继续覆盖 programmatic-click adapter error、注销和资源关闭。
- next262 批次让脚本可见的 `HTMLElement.click()` 对 native submit/reset/button 先复用 typed click，再接入 typed form-event 和既有提交/重置宿主默认路径；reset 由宿主先发可取消事件、core 只提交状态，避免重复 reset；保持取消、disabled/no-op、generic button 和资源边界；next261 批次把脚本可见的 `HTMLElement.click()` 接入 `positron_browser.dll` 的 programmatic-click typed callback；宿主按 DOM id 复用既有 typed click、input、change contracts，保持取消、disabled/no-op、radio 互斥和资源关闭边界；next260 批次让 core checkbox/radio 的 Space/Enter WM keyboard activation 复用既有 typed key/click/input/change contracts，并保持 keydown/click 取消、重复 keydown 和 disabled 边界；next259 批次让 label/native checkbox/radio activation 先通过既有 typed click contract 分发 label click 与目标 control click，并保持取消阻断及 `input` → `change` 顺序；next258 批次让 native checkbox/radio activation 在核心状态提交后按 `input` → `change` 顺序复用 `positron_browser/` 已有的 typed input/select callback contracts；next257 批次让 native checkbox/radio activation 在核心状态提交后复用 `positron_browser/` 已有的 select-style typed `change` callback contract；next256 批次让 `test_host` 的 native file input 选取完成路径复用 `positron_browser/` 已有的 input/select typed callback contract，按 `input` → `change` 顺序分发 file metadata；next255 批次在 `positron_browser/` 产品 DLL 中加入 native invalid typed dispatch callback ABI，并让 `test_host` 的 constraint-validation feedback 先经过该 contract；next254 批次加入 native submit/reset typed dispatch callback ABI，并让 `test_host` 的 form button/Enter activation 先经过该 contract；next253 批次加入 native click typed dispatch callback ABI，并让 `test_host` 的 WM_LBUTTONDOWN 先经过该 contract；next252 批次在 `test_host` 中把 native EDIT 的 EN_CHANGE 后 input 事件接到既有 `positron_browser/` input typed dispatch callback contract；next251 批次在产品 DLL 中加入 native EDIT change typed dispatch callback ABI；next250 批次让既有 SELECT typed dispatch callback ABI 同时承接 native SELECT input 事件；next249 批次加入 native SELECT change typed dispatch callback ABI；next248 的 focus-family typed dispatch callback ABI、next247 的 native keyboard typed dispatch callback ABI、next246 的 native input/composition typed dispatch callback ABI、next245 的同文档 history traversal/hash location 事件分发 API、next244 的 navigation callback typed registration、JSON
  分发和 session 生命周期、next243 的 form-property callback、next242 的 checked callback、next241 的 input value callback、next240 的 Event callback、next239 的 DOM attribute 三件套、next238 的 textContent 写入、next237 的 DOM 只读 callback、next236 的产品 bootstrap
  文本与求值入口、next235 的浏览器脚本 session 所有权与 host JSON callback 注册均保持通过；`test_host` 适配与工程接线继续使用当前
  WMDC/RAPI 会话的 `scripts/device_gate.bat`、`scripts/device_gate.ps1`，环境修复脚本为
  `scripts/repair_wmdc_rapi.*`。
- 当前工作区的 `test_host/test_host.ini` 保持自动模式：`auto=1`、`javascript=0`、
  `tests=13,20,27,56,58,62,64-67,73,75,999`。这是窄的自动 smoke 选择，不是完整基线；
- next255 的 170 项自动全量证据已经通过；next264 的定向门见下方；人工视觉/输入包若需要弹窗，必须临时把 `auto` 改为 0，
  验收结束后恢复为 1。
- next264 定向证据位于 `tmp/device-runs/20260818-231007-next264-file-picker-stage/`；`TEST70,189-231/999`
  相关回归证据位于 `tmp/device-runs/20260818-231032-next264-file-picker-regression/`。next263 定向证据位于 `tmp/device-runs/20260818-225735-next263-file-programmatic-stage/`；`TEST70,189-230/999`
- next265 候选的自动复核证据位于 `tmp/device-runs/20260818-233029-next265-picker-regression/`
  （`TEST231/999` 2/2）和 `tmp/device-runs/20260818-233055-next265-file-regression/`
  （`TEST70,189-231/999` 45/45）；两组均无 ERROR/FAIL。TEST232 仍未有人工设备结果。
- next266 定向证据位于 `tmp/device-runs/20260818-234713-next266/`：`TEST233/999` 2/2，
  零 ERROR/FAIL，唯一 `TESTBENCH PASS`，`test13_route_ok=True`。TEST232 仍保持人工待验收，
  不因本批自动门通过而提升为已验证。
- next267 定向证据位于 tmp/device-runs/20260818-235628-next267/：TEST233,234/999 3/3，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next268 定向证据位于 tmp/device-runs/20260819-000239-next268/：TEST233-235/999 4/4，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next269 定向证据位于 tmp/device-runs/20260819-000656-next269/：TEST233-236/999 5/5，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next270 定向证据位于 tmp/device-runs/20260819-001208-next270/：TEST233-237/999 6/6，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next271 定向证据位于 tmp/device-runs/20260819-001721-next271/：TEST233-238/999 7/7，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next272 定向证据位于 tmp/device-runs/20260819-002142-next272/：TEST233-239/999 8/8，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next273 定向证据位于 tmp/device-runs/20260819-002554-next273/：TEST233-240/999 9/9，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next274 定向证据位于 tmp/device-runs/20260819-003032-next274/：TEST233-241/999 10/10，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next275 定向证据位于 tmp/device-runs/20260819-003530-next275/：TEST233-242/999 11/11，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next276 定向证据位于 tmp/device-runs/20260819-004741-next276-rerun/：TEST233-243/999 12/12，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next277 定向证据位于 tmp/device-runs/20260819-005443-next277/：TEST233-244/999 13/13，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next278 定向证据位于 tmp/device-runs/20260819-010232-next278-rerun2/：TEST233-245/999 14/14，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next279 定向证据位于 tmp/device-runs/20260819-010724-next279/：TEST233-246/999 15/15，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next280 定向证据位于 tmp/device-runs/20260819-011121-next280/：TEST233-247/999 16/16，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next281 定向证据位于 tmp/device-runs/20260819-011517-next281/：TEST233-248/999 17/17，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next282 定向证据位于 tmp/device-runs/20260819-012218-next282/：TEST233-249/999 18/18，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next283 初次定向门 `tmp/device-runs/20260819-012535-next283/` 暴露了把动态 value 当作
  step base 的实现错误（TEST250 未通过）；该实现未提交，改为读取 libdom default_value 后，
  重跑证据位于 tmp/device-runs/20260819-012830-next283-rerun2/：TEST233-250/999 19/19，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next284 定向证据位于 tmp/device-runs/20260819-013311-next284/：TEST233-251/999 20/20，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next285 定向证据位于 tmp/device-runs/20260819-013944-next285/：TEST233-252/999 21/21，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next286 定向证据位于 tmp/device-runs/20260819-014525-next286/：TEST233-253/999 22/22，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next287 定向证据位于 tmp/device-runs/20260819-015001-next287/：TEST233-254/999 23/23，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next288 定向证据位于 tmp/device-runs/20260819-015310-next288/：TEST233-255/999 24/24，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next289 定向证据位于 tmp/device-runs/20260819-015708-next289/：TEST233-256/999 25/25，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next290 定向证据位于 tmp/device-runs/20260819-020138-next290/：TEST233-257/999 26/26，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next291 定向证据位于 tmp/device-runs/20260819-020524-next291/：TEST233-258/999 27/27，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next292 定向证据位于 tmp/device-runs/20260819-020813-next292/：TEST233-259/999 28/28，
  零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。TEST232 仍保持人工待验收。
- next293 初次定向门 `tmp/device-runs/20260819-021432-next293/` 在 TEST260 暴露了只修复
  multipart 路径、遗漏 urlencoded `PCore_FormSubmissionAt` 路径的默认 range 值错误；该证据不作基线。
  统一两条提交路径后的权威重跑位于 tmp/device-runs/20260819-021754-next293/：
  TEST233-260/999 29/29，零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。
  TEST232 仍保持人工待验收。
- next294 初次定向门 `tmp/device-runs/20260819-022428-next294/` 和诊断重跑
  `tmp/device-runs/20260819-022559-next294/` 暴露了默认 range 值只误放在 textarea 分支的实现错误；
  修正分支后权威证据位于 tmp/device-runs/20260819-022946-next294/：
  TEST233-261/999 30/30，零 ERROR/FAIL，唯一 TESTBENCH PASS，test13_route_ok=True。
  TEST232 随后的用户人工验收已通过：真实 WM6 picker 选择、同窗口 Cancel 后状态保持，
  且文件监听器 trace 只出现一次 `input` 后一次 `change`；本地修复同时让动态 DOM
  文本更新在宿主重排后可见。自动证据仍以本目录下的定向门为准。
- next295 自动候选证据：最终 `TEST262/999` 2/2 位于
  `tmp/device-runs/20260819-202715-next295-file-programmatic-picker-final/`；此前同门
  的通过重跑位于 `tmp/device-runs/20260819-201234-next295-file-programmatic-picker-rerun3/`；
  `TEST70,189-231,233-262/999` 75/75 位于
  `tmp/device-runs/20260819-201353-next295-file-programmatic-picker-regression-rerun/`。
  两组均零 ERROR/FAIL、唯一 TESTBENCH PASS；第一次错误的自动回归选择包含 manual-only
  TEST232，已按仓库规则排除且不作为证据。TEST263 手工包位于
  `C:\WMShare\Positron-manual-next295`，已由用户完成真实 GUI 验收。
- next296 自动候选证据：`TEST264/999` 2/2 位于
  `tmp/device-runs/20260819-203903-next296-disabled-property-rerun/`；相关
  `TEST70,189-231,233-262,264/999` 76/76 位于
  `tmp/device-runs/20260819-204007-next296-disabled-property-regression-rerun/`。
  两组均零 ERROR/FAIL、唯一 TESTBENCH PASS。首次诊断回归误把 manual-only TEST263
  放入自动选择，且早先 TEST264 夹具把控件 id 当成 tag；两者均已修正，失败运行不作为证据。
- next297 自动候选证据：`TEST265/999` 2/2 位于
  `tmp/device-runs/20260819-211540-next297-form-properties-rerun/`；扩大后的
  `TEST68-73,189-231,233-262,264-265/999` 82/82 位于
  `tmp/device-runs/20260819-211616-next297-form-properties-regression/`。
  两组均零 ERROR/FAIL、唯一 TESTBENCH PASS。首次诊断失败是 TEST265 夹具误把
  `formNoValidate=true` 的规范性绕过结果断言为 invalid，已修正并重跑；失败运行不作为证据。
- next298 自动候选证据：`TEST266/999` 2/2 位于
  `tmp/device-runs/20260819-213340-next298/`；启用 JavaScript 的
  `TEST189-231,233-262,264-266/999` 相关回归 77/77 位于
  `tmp/device-runs/20260819-213736-next298-js-validation-regression-rerun/`。
  两组均零 ERROR/FAIL、唯一 TESTBENCH PASS，`test13_route_ok=True`。相关回归的 staging
  INI 临时使用 `javascript=1`，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`；
  首次用默认关闭开关运行脚本回归时在 TEST189 停止，属于配置不匹配，不作为失败代码证据。
- next299 自动候选证据：`TEST267/999` 2/2 位于
  `tmp/device-runs/20260819-221723-next299-custom-validity-rerun2/`；启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-267/999` 相关回归 84/84 位于
  `tmp/device-runs/20260819-221803-next299-custom-validity-regression/`。
  两组均零 ERROR/FAIL、唯一 TESTBENCH PASS，`test13_route_ok=True`。回归 staging INI
  临时使用 `javascript=1`，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
  首次定向门因新增 callback 使 native function 数达到 17、旧上限 16 而失败；已同步提升
  `PSCRIPT_MAX_NATIVE_FUNCTIONS`/browser 严格计数，重建并重跑通过；首次失败不作为能力证据。
  独立 script 上限回归 `TEST93/999` 2/2 位于
  `tmp/device-runs/20260819-222439-next299-script-limit/`，同样零 ERROR/FAIL。
- next300 自动候选证据：`TEST268/999` 2/2 位于
  `tmp/device-runs/20260819-223518-next300-form-validation/`；启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-268/999` 相关回归 85/85 位于
  `tmp/device-runs/20260819-223614-next300-form-validation-regression/`。
  两组均零 ERROR/FAIL、唯一 TESTBENCH PASS，`test13_route_ok=True`。回归 staging INI
  临时使用 `javascript=1`，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next301 自动候选证据：`TEST269/999` 2/2 位于
  `tmp/device-runs/20260819-231341-next301-report-validity-final/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。该批只涉及脚本校验查询与 invalid-event 路由，
  不需要人工视觉/触摸验收；启用 JavaScript 的 `TEST68-73,189-231,233-262,264-269/999`
  相关回归 86/86 位于 `tmp/device-runs/20260819-231431-next301-report-validity-regression/`；
  tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next302 自动候选证据：`TEST270/999` 2/2 位于
  `tmp/device-runs/20260819-233450-next302-validation-message-final/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-270/999` 相关回归 87/87 位于
  `tmp/device-runs/20260819-232921-next302-validation-message-regression/`；
  tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。该批不涉及视觉、触摸、SIP、
  系统 picker 或本地化/native validation UI，不需要人工页面验收。
- next303 自动候选证据：`TEST271/999` 2/2 位于
  `tmp/device-runs/20260820-103112-next303-constraint-reflection-final/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-271/999` 相关回归 88/88 位于
  `tmp/device-runs/20260820-103233-next303-constraint-reflection-regression-final/`；
  tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。此前未启用 JavaScript 的
  首次尝试仅验证了配置错误，不作为能力证据。
- next304 自动候选证据：`TEST272/999` 2/2 位于
  `tmp/device-runs/20260820-105247-next304-name-reflection-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-272/999` 相关回归原配置重试后 89/89 位于
  `tmp/device-runs/20260820-105715-next304-name-reflection-regression-retry-js/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。此前一次回归
  在既有 TEST191 的 bootstrap 阶段超时，未触及 TEST272，不作为能力失败证据。
- next305 自动候选证据：`TEST273/999` 2/2 位于
  `tmp/device-runs/20260820-113226-next305-submission-reflection-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-273/999` 相关回归 90/90 位于
  `tmp/device-runs/20260820-113331-next305-submission-reflection-regression-js/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next306 自动候选证据：`TEST274/999` 2/2 位于
  `tmp/device-runs/20260820-115956-next306-enctype-reflection-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-274/999` 相关回归 91/91 位于
  `tmp/device-runs/20260820-120021-next306-enctype-reflection-regression-js/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next307 自动候选证据：`TEST275/999` 2/2 位于
  `tmp/device-runs/20260820-121644-next307-submit-action-final/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-275/999` 相关回归 92/92 位于
  `tmp/device-runs/20260820-121703-next307-submit-action-regression-js/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next308 自动候选证据：`TEST276/999` 2/2 位于
  `tmp/device-runs/20260820-122437-next308-submit-method-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-276/999` 相关回归 93/93 位于
  `tmp/device-runs/20260820-122501-next308-submit-method-regression-js/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next309 自动候选证据：`TEST277/999` 2/2 位于
  `tmp/device-runs/20260820-123830-next309-submit-enctype-js-retry/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-277/999` 相关回归 94/94 位于
  `tmp/device-runs/20260820-123929-next309-submit-enctype-regression-js/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next310 自动候选证据：`TEST278/999` 2/2 位于
  `tmp/device-runs/20260820-124937-next310-implicit-submitter-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-278/999` 相关回归 95/95 位于
  `tmp/device-runs/20260820-125241-next310-implicit-submitter-regression-js-retry2/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。前两次长链超时
  证据（TEST189 与 TEST262）未作为基线。
- next311 自动候选证据：`TEST279/999` 2/2 位于
  `tmp/device-runs/20260820-125830-next311-target-reflection-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-279/999` 相关回归 96/96 位于
  `tmp/device-runs/20260820-125854-next311-target-regression-js/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next312 自动候选证据：`TEST280/999` 2/2 位于
  `tmp/device-runs/20260820-130448-next312-form-autocomplete-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-280/999` 相关回归 97/97 位于
  `tmp/device-runs/20260820-130514-next312-form-autocomplete-regression-js/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next313 自动候选证据：`TEST281/999` 2/2 位于
  `tmp/device-runs/20260820-131112-next313-accept-charset-js-retry/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-281/999` 相关回归 98/98 位于
  `tmp/device-runs/20260820-131135-next313-accept-charset-regression-js/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。首尝定向门的
  单次 bootstrap timeout 未作为基线。
- next314 自动候选证据：`TEST282/999` 2/2 位于
  `tmp/device-runs/20260820-131719-next314-placeholder-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-282/999` 相关回归 99/99 位于
  `tmp/device-runs/20260820-131742-next314-placeholder-regression-js/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next315 自动候选证据：`TEST283/999` 2/2 位于
  `tmp/device-runs/20260820-133705-next315-input-autocomplete-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-283/999` 相关回归 100/100 位于
  `tmp/device-runs/20260820-133726-next315-input-autocomplete-regression-js/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next316 自动候选证据：`TEST284/999` 2/2 位于
  `tmp/device-runs/20260820-134204-next316-inputmode-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-284/999` 相关回归 101/101 位于
  `tmp/device-runs/20260820-134336-next316-inputmode-regression-js-retry/`；零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。首尝在既有
  TEST277 bootstrap 处 timeout，未作为基线。
- next317 自动候选证据：`TEST285/999` 2/2 位于
  `tmp/device-runs/20260820-134938-next317-type-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。启用 JavaScript 的
  `TEST68-73,189-231,233-262,264-285/999` 相关回归 102/102 位于
  `tmp/device-runs/20260820-135001-next317-type-regression-js/`；零 ERROR/FAIL，tracked
  `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next318 自动候选证据：`TEST286/999` 2/2 位于
  `tmp/device-runs/20260820-135849-next318-textarea-placeholder-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。完整启用 JavaScript 链的多次尝试在既有
  TEST192/227/272/277/279 bootstrap timeout 处提前结束，未作为基线；最近
  `TEST264-286/999` 相关段 24/24 位于
  `tmp/device-runs/20260820-140648-next318-textarea-placeholder-recent-js-retry2/`，零
  ERROR/FAIL，tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next319 自动候选证据：`TEST287/999` 2/2 位于
  `tmp/device-runs/20260820-141139-next319-select-autocomplete-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。最近
  `TEST264-287/999` 相关段 25/25 位于
  `tmp/device-runs/20260820-141208-next319-select-autocomplete-recent-js/`，零 ERROR/FAIL，
  tracked `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next320 自动候选证据：`TEST288/999` 2/2 位于
  `tmp/device-runs/20260820-142003-next320-button-type-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。最近
  `TEST264-288/999` 相关段 26/26 位于
  `tmp/device-runs/20260820-142025-next320-button-type-recent-js/`，零 ERROR/FAIL，tracked
  `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next321 自动候选证据：`TEST289/999` 2/2 位于
  `tmp/device-runs/20260820-142613-next321-unknown-method-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。最近
  `TEST264-289/999` 相关段 27/27 位于
  `tmp/device-runs/20260820-142636-next321-unknown-method-recent-js/`，零 ERROR/FAIL，tracked
  `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next322 自动候选证据：`TEST290/999` 2/2 位于
  `tmp/device-runs/20260820-143307-next322-unknown-enctype-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。最近
  `TEST264-290/999` 相关段 28/28 位于
  `tmp/device-runs/20260820-143433-next322-unknown-enctype-recent-js/`，零 ERROR/FAIL，tracked
  `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next323 自动候选证据：`TEST291/999` 2/2 位于
  `tmp/device-runs/20260820-143927-next323-case-boundary-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。最近
  `TEST264-291/999` 相关段 29/29 位于
  `tmp/device-runs/20260820-144007-next323-case-boundary-recent-js/`，零 ERROR/FAIL，tracked
  `test_host/test_host.ini` 已恢复默认 `javascript=0`。
- next324 自动候选证据：`TEST292/999` 2/2 位于
  `tmp/device-runs/20260820-144727-next324-metadata-relayout-js/`；零 ERROR/FAIL、唯一
  TESTBENCH PASS，`test13_route_ok=True`。最近
  `TEST264-292/999` 相关段 30/30 位于
  `tmp/device-runs/20260820-144750-next324-metadata-relayout-recent-js/`，零 ERROR/FAIL，tracked
  `test_host/test_host.ini` 已恢复默认 `javascript=0`。
  相关回归证据位于 `tmp/device-runs/20260818-225807-next263-file-programmatic-regression/`。next262 定向证据位于 `tmp/device-runs/20260818-223755-next262-programmatic-form-stage-final/`；`TEST68-69,189-229/999`
  相关回归证据位于 `tmp/device-runs/20260818-223854-next262-programmatic-form-regression-retry/`。next261 定向证据位于 `tmp/device-runs/20260818-220809-next261-programmatic-stage/`；`TEST189-228/999`
  相关回归证据位于 `tmp/device-runs/20260818-221000-next261-programmatic-regression/`。next260 定向证据位于 `tmp/device-runs/20260818-214758-next260-toggle-key-stage-rerun/`；`TEST189-227/999`
  相关回归证据位于 `tmp/device-runs/20260818-214821-next260-toggle-key-regression-rerun/`。next259 定向证据位于 `tmp/device-runs/20260818-212733-next259-toggle-click-probe-rerun/`；`TEST189-226/999`
  相关回归证据位于 `tmp/device-runs/20260818-212025-next259-toggle-click-regression/`。next258 定向证据位于 `tmp/device-runs/20260818-204727-next258-toggle-input-stage/`；`TEST189-225/999`
  相关回归证据位于 `tmp/device-runs/20260818-204800-next258-toggle-input-regression/`。next257 定向证据位于 `tmp/device-runs/20260818-203313-next257-toggle-stage-retry/`；`TEST189-224/999`
  相关回归证据位于 `tmp/device-runs/20260818-203338-next257-toggle-regression/`。next256 的 file-input
  证据仍位于 `tmp/device-runs/20260818-201531-next256-file-stage/` 和
  `tmp/device-runs/20260818-201556-next256-file-regression/`；next255 的完整本地设备证据仍位于
  `tmp/device-runs/20260818-195825-next255-invalid-final/`；TEST222 定向证据位于
  `tmp/device-runs/20260818-195657-next255-invalid-stage/`，EDIT/SELECT/focus/key/input/click/form-event/invalid 回归证据位于
  `tmp/device-runs/20260818-195727-next255-invalid-regression/`。next254 的 form-event 全量证据仍位于
  `tmp/device-runs/20260818-194141-next254-form-event-final/`；next253 的 click 全量证据仍位于
  `tmp/device-runs/20260818-192039-next253-click-final/`；next252 的 EDIT input 全量证据仍位于
  `tmp/device-runs/20260817-203107-next252-edit-input-final-rerun/`；next251 的 EDIT change 全量证据仍位于
  `tmp/device-runs/20260817-200046-next251-edit-final/`；next250 的 SELECT input 全量证据仍位于
  `tmp/device-runs/20260817-194231-next250-select-input-final/`；next249 的 SELECT change 全量证据仍位于
  `tmp/device-runs/20260815-222840-next249-select-final/`；next248 的 focus 全量证据仍位于
  `tmp/device-runs/20260815-221705-next248-focus-final/`；TEST215 定向证据位于
  `tmp/device-runs/20260815-221554-next248-focus-stage/`，focus/key/input 回归证据位于
  `tmp/device-runs/20260815-221611-next248-focus-regression/`。next247 的 keyboard 全量证据仍位于
  `tmp/device-runs/20260815-154724-next247-key-final-retry/`；TEST214 定向证据位于
  `tmp/device-runs/20260815-154407-next247-key-stage/`，keyboard/input 回归证据位于
  `tmp/device-runs/20260815-154427-next247-key-regression/`。next246 的 input 全量证据仍位于
  `tmp/device-runs/20260815-152753-next246-input-final/`；TEST213 定向证据位于
  `tmp/device-runs/20260815-152635-next246-input-stage/`，input/script 回归证据位于
  `tmp/device-runs/20260815-152655-next246-input-regression/`。next245 的 location 证据仍位于
  `tmp/device-runs/20260815-150634-next245-location-final2/`；TEST212 定向证据位于
  `tmp/device-runs/20260815-150218-next245-location-stage2/`，location/history 回归证据位于
  `tmp/device-runs/20260815-150234-next245-location-regression/`。`tmp/` 不跟踪，干净 clone 中没有该日志，
  不能据此假定新环境也已经连接或通过。

接管时仍须重新运行 `git status --short --branch` 和 `git diff`；以上列表不是 Git 的替代品。

## 最近已验证设备证据

### 最新定向检查点：next294

- 配置：TEST233-261/999 定向 30 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：30 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST261 覆盖有效 min=10/max=40 时 range 缺省中点 25 的 core 读回、验证、submission 和显式值 35
  覆盖；TEST260 覆盖 range 缺省 value 的默认 0..100 中点 50、显式值覆盖和成功 submission；
  TEST259 覆盖 custom validity 跨 re-layout 的状态保持、清空消息和恢复 submission；
  TEST258 覆盖 textarea custom validity getter 的 UTF-8 完整长度、安全截断和清空读回；
  TEST257 覆盖 text/password custom validity getter 的 UTF-8 完整长度、安全截断和清空读回；
  TEST256 覆盖 textarea setter、required/customError 阻断、清空消息后的恢复 submission；
  TEST255 覆盖 required 空 text input 同时保留 valueMissing 与 customError、填值后仅保留
  customError 及清空消息后的恢复 submission；TEST254 覆盖 datetime-local 无 min 时的固定 value
  step base、动态不对齐值和恢复 submission；
  TEST253 覆盖 week 无 min 时的固定 value step base；TEST252 覆盖 month 无 min 时的固定 value step base；
  TEST251 覆盖 time 无 min 时的固定 value step base；TEST250 覆盖 date 无 min 时的固定 value step base；
  TEST233-249 的
  number/range/email/url/date/time/month/week/datetime-local/color/custom validity 回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-022946-next294/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next282

- 配置：TEST233-249/999 定向 18 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：18 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST249 覆盖 product text-input custom validity setter、customError 阻断、清空和 submission；
  TEST233-248 的 number/range/email/url/date/time/month/week/datetime-local/color 回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-012218-next282/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next281

- 配置：TEST233-248/999 定向 17 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：17 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST248 覆盖 datetime-local min-based step、默认 60 秒、step=any、非法/非正回退、动态
  恢复和 submission；TEST233-247 的 number/range/email/url/date/time/month/week/datetime-local/color
  回归同批通过。TEST242 的 sub-minute composition probe 已显式声明 step=any。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-011517-next281/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next280

- 配置：TEST233-247/999 定向 16 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：16 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST247 覆盖 ISO week min-based step、默认 step、step=any、非法/非正回退、动态恢复和
  submission；TEST233-246 的 number/range/email/url/date/time/month/week/datetime-local/color
  回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-011121-next280/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next279

- 配置：TEST233-246/999 定向 15 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：15 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST246 覆盖 month min-based step、默认 step、step=any、非法/非正回退、动态恢复和
  submission；TEST233-245 的 number/range/email/url/date/time/month/week/datetime-local/color
  回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-010724-next279/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next278

- 配置：TEST233-245/999 定向 14 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：14 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST245 覆盖 time min-based step、默认 60 秒、step=any、非法/非正回退、动态恢复和
  submission；TEST233-244 的 number/range/email/url/date/time/month/week/datetime-local/color
  回归同批通过。TEST239 的 sub-minute syntax probe 已显式声明 step=any。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-010232-next278-rerun2/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next277

- 配置：TEST233-244/999 定向 13 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：13 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST244 覆盖 date min-based step、默认 step、step=any、非法/非正回退、动态恢复和
  submission；TEST233-243 的 number/range/email/url/date/time/month/week/datetime-local/color
  回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-005443-next277/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next276

- 配置：TEST233-243/999 定向 12 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：12 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST243 覆盖 #RRGGBB 语法、非法十六进制值、动态恢复和 submission；TEST233-242
  的 number/range/email/url/date/time/month/week/datetime-local 回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-004741-next276-rerun/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next275

- 配置：TEST233-242/999 定向 11 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：11 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST242 覆盖 date/time 组合、非法时间、min/max 越界、动态恢复和 submission；TEST233-241
  的 number/range/email/url/date/time/month/week 回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-003530-next275/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next274

- 配置：TEST233-241/999 定向 10 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：10 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST241 覆盖 bounded ISO week、week-53 规则、min/max 越界、动态恢复和 submission；
  TEST233-240 的 number/range/email/url/date/time/month 回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-003032-next274/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next273

- 配置：TEST233-240/999 定向 9 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：9 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST240 覆盖 bounded YYYY-MM、非法月份、min/max 越界、动态恢复和 submission；TEST233-239
  的 number/range/email/url/date/time 回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-002554-next273/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next272

- 配置：TEST233-239/999 定向 8 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：8 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST239 覆盖 HH:MM、秒/毫秒、无效时间、min/max 越界、动态恢复和 submission；TEST233-238
  的 number/range/email/url/date 回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-002142-next272/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next271

- 配置：TEST233-238/999 定向 7 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：7 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST238 覆盖 ISO 语法、闰年/无效日、min/max 越界、动态恢复和 submission；TEST233-237
  的 number/range/email/url 回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-001721-next271/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next270

- 配置：TEST233-237/999 定向 6 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：6 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST237 覆盖 range 默认边界、显式边界、下溢/上溢、step mismatch、动态恢复和 submission；
  TEST233-236 的 number/email/url 回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-001208-next270/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next269

- 配置：TEST233-236/999 定向 5 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：5 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST236 覆盖 URL 空 authority/空白负例、scheme/relative/network-path 正例、动态修复和
  urlencoded submission；TEST233-235 的 number/email 回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-000656-next269/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next268

- 配置：TEST233-235/999 定向 4 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：4 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST235 覆盖单地址、multiple 地址列表、非法 token、动态修复和 urlencoded submission；
  TEST233/234 的 number min/max/step 回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260819-000239-next268/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next267

- 配置：TEST233,234/999 定向 3 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，screen=640x480 dpi=192。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：3 项均有 OK；零 ERROR、零 FAIL，唯一 TESTBENCH PASS，completion_marker=PASS，
  test13_route_ok=True。
- TEST234 覆盖 min-based step alignment、default step=1、step=any、非法 step 回退、动态值
  阻断/恢复和 urlencoded submission；TEST233 的 min/max/bad-input 回归同批通过。
- 自动证据：python scripts/test_c89ize.py、python scripts/audit_repo.py、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于 tmp/device-runs/20260818-235628-next267/；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next266

- 配置：`TEST233/999` 定向 2 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：2 项均有 `OK`；零 `ERROR`、零 `FAIL`，唯一 `TESTBENCH PASS`，
  `completion_marker=PASS`，`test13_route_ok=True`。
- TEST233 覆盖 `type=number` 的 valid inclusive min/max、下溢、上溢、malformed value、
  malformed range attributes 和恢复后的 urlencoded submission。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建均通过。证据位于
  `tmp/device-runs/20260818-234713-next266/`；本批未重复 next255 的 170 项全量门。

### 已验证检查点：next264

- 配置：`TEST231/999` 定向 2 项；`TEST70,189-231/999` 相关回归 45 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：2 项与 45 项均全部有 `OK`；零 `ERROR`、零 `FAIL`，每组唯一 `TESTBENCH PASS`，
  `completion_marker=PASS`，`test13_route_ok=True`。
- TEST231 用注入的同步 picker adapter 覆盖取消、picker 错误、空选择提交错误、成功选择后的
  文件 value/path、`input` → `change` 顺序、再次取消保留既有值，以及 callback 的非重入/调用后
  active=0；真实 GUI picker 仍由 `GetOpenFileNameEx` 的宿主路径负责。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建均通过。定向证据位于
  `tmp/device-runs/20260818-231007-next264-file-picker-stage/`，相关回归证据位于
  `tmp/device-runs/20260818-231032-next264-file-picker-regression/`；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next263

- 配置：`TEST230/999` 定向 2 项；`TEST70,189-230/999` 相关回归 44 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：2 项与 44 项均全部有 `OK`；零 `ERROR`、零 `FAIL`，每组唯一 `TESTBENCH PASS`，
  `completion_marker=PASS`，`test13_route_ok=True`。
- TEST230 通过真实脚本验证启用 file input 的 click 目标和 `defaultPrevented=false`、取消 click、
  disabled/no-op，以及三个 file input 的 value/path 仍为空；程序化路径没有打开系统 picker，
  因此文件系统权限、picker 和窗口生命周期仍留在宿主 GUI 路径。TEST228 的 adapter error、注销
  和 native function 资源关闭断言在相关回归中保持通过。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建均通过。定向证据位于
  `tmp/device-runs/20260818-225735-next263-file-programmatic-stage/`，相关回归证据位于
  `tmp/device-runs/20260818-225807-next263-file-programmatic-regression/`；本批未重复
  next255 的 170 项全量门。

### 已验证检查点：next262

- 配置：`TEST229/999` 定向 2 项；`TEST68-69,189-229/999` 相关回归 44 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：2 项与 44 项均全部有 `OK`；零 `ERROR`、零 `FAIL`，每组唯一 `TESTBENCH PASS`，
  `completion_marker=PASS`，`test13_route_ok=True`。相关回归首尝 TEST193 出现一次既有
  JavaScript timeout，未改预算或断言，原配置重试通过。
- TEST229 通过真实脚本验证 submit/reset/button 的程序化 click 目标和顺序：click 后进入
  submit/reset form-event；submit/reset 取消阻止默认动作，允许 reset 恢复初值，generic button
  不产生额外 form-event，disabled button 静默；typed click、form-event adapter 错误映射和
  资源关闭由 next261 TEST228 的产品断言继续覆盖。
- reset 的可取消事件由宿主先分发，`PCore_FormResetAt` 现在只提交 DOM 初始状态，避免重复
  reset 事件；提交收集、约束校验、导航/网络和窗口副作用仍由既有宿主路径拥有。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建均通过。定向证据位于
  `tmp/device-runs/20260818-223755-next262-programmatic-form-stage-final/`，相关回归证据位于
  `tmp/device-runs/20260818-223854-next262-programmatic-form-regression-retry/`；本批未重复
  next255 的 170 项全量门。

### 最新定向检查点：next261

- 配置：`TEST228/999` 定向 2 项；`TEST189-228/999` 相关回归 41 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：2 项与 41 项均全部有 `OK`；零 `ERROR`、零 `FAIL`，每组唯一 `TESTBENCH PASS`，
  `completion_marker=PASS`，`test13_route_ok=True`。
- TEST228 通过浏览器脚本 `HTMLElement.click()` 验证 checkbox/radio 的程序化 click 目标、
  `click` → `input` → `change` 顺序、click 取消、disabled 静默、radio 互斥和 checked 状态；
  新增 programmatic-click callback 的重复注册、非法参数、adapter error、注销和 native
  function 资源关闭也通过。控件 checked 属性同步、radio 算法、重绘和窗口副作用仍由宿主/core
  负责，其他 form controls 的程序化默认动作尚未扩大声明。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建均通过。定向证据位于
  `tmp/device-runs/20260818-220809-next261-programmatic-stage/`，相关回归证据位于
  `tmp/device-runs/20260818-221000-next261-programmatic-regression/`；本批未重复 next255 的
  170 项全量门。

### 最新定向检查点：next260

- 配置：`TEST227/999` 定向 2 项；`TEST189-227/999` 相关回归 40 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：2 项与 40 项均全部有 `OK`；零 `ERROR`、零 `FAIL`，每组唯一 `TESTBENCH PASS`，
  `completion_marker=PASS`，`test13_route_ok=True`。
- TEST227 通过真实渲染窗口主窗口的 WM_KEYDOWN/WM_KEYUP 验证 checkbox/radio 的 Space/Enter
  activation，顺序为 `keydown` → `click` → `input` → `change` → `keyup`；keydown/click
  取消阻止默认切换，重复 keydown 不重复切换，disabled 控件静默。控件状态、radio 互斥、
  重绘和窗口副作用仍由宿主负责。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建均通过。定向证据位于
  `tmp/device-runs/20260818-214758-next260-toggle-key-stage-rerun/`，相关回归证据位于
  `tmp/device-runs/20260818-214821-next260-toggle-key-regression-rerun/`；本批未重复 next255 的
  170 项全量门。

### 上一批定向检查点：next259

- 配置：`TEST226/999` 定向 2 项；`TEST189-226/999` 相关回归 39 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：2 项与 39 项均全部有 `OK`；零 `ERROR`、零 `FAIL`，每组唯一 `TESTBENCH PASS`，
  `completion_marker=PASS`，`test13_route_ok=True`。
- TEST226 通过真实 label activation 路径验证 label click、目标 checkbox/radio click、
  `input` → `change` 顺序；目标 click 被取消时 radio 不激活，disabled 控件不接收合成 click。
  相关 TEST189–225 回归保持 next258 的状态/input/change 合约通过；控件默认状态、radio
  互斥、重绘和窗口副作用仍由宿主负责。
- 自动证据：`python scripts/test_c89ize.py`、VS2008 ARMV4I Debug 正式构建均通过。定向证据位于
  `tmp/device-runs/20260818-212733-next259-toggle-click-probe-rerun/`，相关回归证据位于
  `tmp/device-runs/20260818-212025-next259-toggle-click-regression/`；本批未重复 next255 的
  170 项全量门。

### 上一批定向检查点：next258

- 配置：`TEST225/999` 定向 2 项；`TEST189-225/999` 相关回归 38 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：2 项与 38 项均全部有 `OK`；零 `ERROR`、零 `FAIL`，每组唯一 `TESTBENCH PASS`，
  `completion_marker=PASS`，`test13_route_ok=True`。
- TEST225 通过真实 core form activation 路径验证 checkbox/radio 在状态提交后按
  `input` → `change` 顺序发出；`input` 使用正确目标、`bubbles=1/cancelable=0`、空
  `inputType/data` 和 `isComposing=0`，已选 radio 与 disabled checkbox 不误报。相关
  TEST189–224 回归保持通过；next257 的 change contract 与 next256 的 file input
  `insertFromFile` metadata 仍保持通过。控件默认状态、radio 互斥、重绘和窗口副作用仍由宿主负责。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建均通过。定向证据位于
  `tmp/device-runs/20260818-204727-next258-toggle-input-stage/`，相关回归证据位于
  `tmp/device-runs/20260818-204800-next258-toggle-input-regression/`；本批未重复 next255 的 170 项
  全量门。

### 最新全量检查点：next255

- 配置：`TEST13/20/27/43/44/56/58-77/80-222/999`，共 170 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：170 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–221 的既有 product callback/location/input/key/focus/select/edit/click/form-event 断言保持通过；TEST222 直接验证
  product native invalid typed dispatch contract、坐标与冒泡字段、取消结果、非法事件、adapter error 映射和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property、
  navigation、同文档 location/history 事件分发、Event JSON 分发、native input/composition、keyboard、
  focus-family、EDIT change/post-change input、click、submit/reset、invalid 和 SELECT input/change dispatch entry；`test_host` 仅保留 WM 消息/控件、坐标命中、core/document/
  form/navigation/listener adapter 及窗口、网络、core 事件传播和控件默认副作用，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST222/999` 证据位于
  `tmp/device-runs/20260818-195657-next255-invalid-stage/`，`TEST112-135/137-152/189-222/999`
  （75 项）位于 `tmp/device-runs/20260818-195727-next255-invalid-regression/`，全量最终证据位于
  `tmp/device-runs/20260818-195825-next255-invalid-final/`。

### 最新全量检查点：next248

- 配置：`TEST13/20/27/43/44/56/58-77/80-215/999`，共 163 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：163 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–214 的既有 product callback/location/input/key 断言保持通过；TEST215 直接验证 product
  focus/blur/focusin/focusout typed dispatch contract、bubbles 字段、非法事件、adapter error 映射和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property、
  navigation、同文档 location/history 事件分发、Event JSON 分发、native input/composition、keyboard 和
  focus-family dispatch entry；`test_host` 仅保留 WM 消息/控件、坐标命中、core/document/form/navigation/
  listener adapter 及窗口、网络、core 事件传播和控件默认副作用，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST215/999` 证据位于
  `tmp/device-runs/20260815-221554-next248-focus-stage/`，`TEST112-135/137-152/189-215/999`
  （68 项）位于 `tmp/device-runs/20260815-221611-next248-focus-regression/`，全量最终证据位于
  `tmp/device-runs/20260815-221705-next248-focus-final/`。

### 最新全量检查点：next247

- 配置：`TEST13/20/27/43/44/56/58-77/80-214/999`，共 162 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：162 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–213 的既有 product callback/location/input 断言保持通过；TEST214 直接验证 product
  native keyboard typed dispatch contract、keydown/keyup 字段、取消、adapter error 映射和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property、
  navigation、同文档 location/history 事件分发、Event JSON 分发、native input/composition 和 keyboard
  dispatch entry；`test_host` 仅保留 WM 消息/控件、坐标命中、core/document/form/navigation/listener
  adapter 及窗口、网络、core 事件传播和控件默认副作用，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST214/999` 证据位于
  `tmp/device-runs/20260815-154407-next247-key-stage/`，`TEST112-135/137-152/189-214/999`
  （67 项）位于 `tmp/device-runs/20260815-154427-next247-key-regression/`，全量最终证据位于
  `tmp/device-runs/20260815-154724-next247-key-final-retry/`。首次全量在 TEST117 出现一次设备
  JavaScript timeout；TEST117/999 定向重跑及最终全量重试通过，未调整执行预算或放宽断言。

### 最新全量检查点：next246

- 配置：`TEST13/20/27/43/44/56/58-77/80-213/999`，共 161 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：161 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–212 的既有 product callback/location 断言保持通过；TEST213 直接验证 product native
  input/composition typed dispatch contract、取消结果、adapter error 映射和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property、
  navigation、同文档 location/history 事件分发、Event JSON 分发和 native input/composition dispatch
  entry；`test_host` 仅保留 core/document/form/navigation/listener/native-control adapter 及窗口、网络、
  core 事件传播和 history/navigation side effect，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST213/999` 证据位于
  `tmp/device-runs/20260815-152635-next246-input-stage/`，`TEST112-135/137-152/189-213/999`
  （66 项）位于 `tmp/device-runs/20260815-152655-next246-input-regression/`，全量最终证据位于
  `tmp/device-runs/20260815-152753-next246-input-final/`。

### 最新全量检查点：next245

- 配置：`TEST13/20/27/43/44/56/58-77/80-212/999`，共 160 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：160 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–211 的既有 product callback 断言保持通过；TEST212 直接验证 product same-document
  traversal/hash location dispatch、popstate/hashchange 顺序、状态更新和临时 global 清理。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property、
  navigation、同文档 location/history 事件分发和 Event JSON 分发；`test_host` 仅保留 typed
  core/document/form/navigation/listener adapter 及窗口、网络、history/navigation side effect，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST212/999` 证据位于
  `tmp/device-runs/20260815-150218-next245-location-stage2/`，`TEST112-135/137-152/189-212/999`
  （65 项）位于 `tmp/device-runs/20260815-150234-next245-location-regression/`，全量最终证据位于
  `tmp/device-runs/20260815-150634-next245-location-final2/`。
- 全量首尝在 TEST166 出现一次 JavaScript timeout；TEST166/212 定向重跑通过，最终全量重试 160/160
  通过，未修改执行预算或放宽断言。

### 最新全量检查点：next244

- 配置：`TEST13/20/27/43/44/56/58-77/80-211/999`，共 159 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：159 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–210 的既有 product callback 断言保持通过；TEST211 直接验证 product navigation JSON
  dispatch、十种 operation kind、URL/state/delta 编码、pushState 返回值、非法输入和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property、
  navigation 和 Event JSON 分发；`test_host` 仅保留 typed core/document/form/navigation/listener
  adapter 及窗口、网络、history side effect，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST211/999` 证据位于
  `tmp/device-runs/20260815-143748-next244-navigation-stage2/`，`TEST112-135/137-152/189-211/999`
  （64 项）位于 `tmp/device-runs/20260815-143813-next244-navigation-regression2/`，全量最终证据位于
  `tmp/device-runs/20260815-144551-next244-navigation-final4/`。
- 全量中途曾出现 TEST129、TEST153、TEST192 的单次 JavaScript timeout；各自前后定向回归均通过，
  最终全量重试 159/159 通过，未修改执行预算或放宽断言。

### 最新全量检查点：next243

- 配置：`TEST13/20/27/43/44/56/58-77/80-210/999`，共 158 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：158 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–209 的既有 product callback 断言保持通过；TEST210 直接验证 product form-property JSON
  dispatch、defaultValue/defaultChecked/selectedIndex typed adapters、缺失/非法参数和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property
  和 Event JSON 分发；`test_host` 仅保留 typed core/document/value/checked/form-property/listener
  adapter，其余 form/input/location/navigation callback 实现仍在宿主，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST210/999` 证据位于
  `tmp/device-runs/20260815-140531-next243-form-stage/`，`TEST112-135/137-152/189-210/999`
  （63 项）位于 `tmp/device-runs/20260815-140551-next243-form-regression/`，全量最终重试证据位于
  `tmp/device-runs/20260815-140823-next243-form-final-retry/`。
- 全量第一次尝试在 TEST13 因网络 `header block read failed` 停止，未执行后续测试；重试后完整通过，
  未修改预算或放宽断言。

### 最新全量检查点：next242

- 配置：`TEST13/20/27/43/44/56/58-77/80-209/999`，共 157 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：157 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204 直接验证 product DOM read JSON dispatch；TEST205 直接验证 product DOM write JSON
  dispatch；TEST206 直接验证 product DOM attribute JSON dispatch、typed get/set/remove adapters 和注销；
  TEST207 直接验证 product Event JSON registration/dispatch、typed add/remove adapters、事件数据和
  preventDefault 结果；TEST208 直接验证 product DOM value JSON dispatch、typed get/set adapters、
  缺失目标、非法参数和注销；TEST209 直接验证 product DOM checked JSON dispatch、typed get/set
  adapters、缺失目标、非法参数和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked 和 Event JSON
  分发；`test_host` 仅保留 typed core/document/value/checked/listener adapter，defaultValue/selectedIndex、
  其余 form/input/location/navigation callback 实现仍在宿主，产品 session 是唯一销毁者，下一批迁移
  defaultValue/selectedIndex、其余表单输入和导航 callback。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST209/999` 证据位于
  `tmp/device-runs/20260815-134414-next242-checked-stage/`，`TEST112-135/137-152/189-209/999`
  （62 项）位于 `tmp/device-runs/20260815-134433-next242-checked-regression/`，全量证据位于
  `tmp/device-runs/20260815-134848-next242-checked-final2/`。

### 上一全量检查点：next236

- 配置：`TEST13/20/27/43/44/56/58-77/80-203/999`，共 151 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：151 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST203 直接验证 product bootstrap 的创建、求值和 document/location/history 对象持久性。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期和
  browser bootstrap 文本/求值入口；`test_host` 的 bridge 仍保留 core/窗口/导航适配及 DOM/Event/
  form/input/location callback 实现，产品 session 是唯一销毁者，下一批迁移 callback 实现。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、15 项隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接
  `TEST203/999` 证据位于 `tmp/device-runs/20260815-100256-next236-browser-bootstrap/`，
  脚本回归 `TEST112-135/137-152/189-203/999`（56 项）位于
  `tmp/device-runs/20260815-100313-next236-browser-bootstrap-regression/`，全量证据位于
  `tmp/device-runs/20260815-100357-next236-browser-bootstrap-final/`。

### 上一全量检查点：next235

- 配置：`TEST13/20/27/43/44/56/58-77/80-202/999`，共 150 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：150 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST202 直接验证 product script session 的创建、求值、host JSON callback 注册/注销和销毁。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context 及其 callback 注册/调用生命周期；
  `test_host` 的 bridge 只保留 core/窗口/导航适配和一个只读诊断 runtime 借用句柄，产品 session
  是唯一销毁者。bootstrap 文本和 DOM/Event/form/input/location callback 实现仍在宿主，下一批迁移。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、15 项隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接
  `TEST202/999` 证据位于 `tmp/device-runs/20260815-094753-next235-script-session/`，
  脚本回归 `TEST112-135/137-152/189-202/999`（55 项）位于
  `tmp/device-runs/20260815-094818-next235-script-session-regression/`，全量证据位于
  `tmp/device-runs/20260815-095055-next235-script-session-final/`。

### 上一全量检查点：next234

- 配置：`TEST13/20/27/43/44/56/58-77/80-201/999`，共 149 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：149 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`。
- 产品边界：新增 `positron_browser.dll`，以独立 opaque history/session handle 拥有
  history entries、state、document identity、same-origin/default-port 判断、文档导航提交、
  push/replace state、traversal target 和 pending-navigation projection。`test_host` 的旧
  history 调用现在只通过该 DLL，固定数组仅作断言镜像；TEST201 直接调用公共 API。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、15 项隔离 staging/部署、SHA-256 清单和日志自动判门均通过。定向
  `TEST201/999` 证据位于 `tmp/device-runs/20260814-234503-next234-browser-history/`，
  `TEST149-201/999` 证据位于 `tmp/device-runs/20260814-234517-next234-history201/`，
  参数边界定向证据位于 `tmp/device-runs/20260814-235415-next234-browser-history-contract/`，
  修订后全量证据位于 `tmp/device-runs/20260814-235642-next234-browser-history-final2/`。

### 更早全量检查点：next233

- 配置：`TEST13/20/27/43/44/56/58-77/80-200/999`，共 148 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：148 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`。
- 能力终点：`history.pushState`/`replaceState` 的第三个 URL 参数显式为 `undefined` 时使用当前
  document URL；显式空字符串仍表示当前 URL 的同 URL history entry。两种写法都不发 GET，保留
  state/length、traversal、popstate/hashchange 顺序和后续 replace/push 行为；既有安全
  absolute/root-relative/document-relative pathname、query/fragment 和拒绝规则不变。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、14 文件隔离 staging/部署、SHA-256 清单和日志自动判门均通过。定向
  `TEST149-200/999` 证据位于 `tmp/device-runs/20260814-231852-next233-history200/`，
  全量证据位于 `tmp/device-runs/20260814-232021-next233-final/`。

### 更早全量检查点：next232

- 配置：`TEST13/20/27/43/44/56/58-77/80-199/999`，共 147 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：147 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`。
- 能力终点：`history.pushState`/`replaceState` 支持安全的同源 absolute/root-relative pathname，以及同源根相对
  path/query/fragment、query-relative URL、裸单段/多段 sibling 和显式 `./` 单段/多段 sibling URL；显式
  `./?query`/`./#fragment` 会落到当前目录的 trailing-slash URL，且同文档 traversal
  恢复 URL/state 并按 popstate 后 hashchange 排序；裸 `./`、`../`、dot segment、
  重复分隔符、编码 dot segment、protocol-relative 和跨源 URL 仍拒绝；普通 percent-encoded
  pathname segment 可以保留；同源 absolute/root-relative URL 在
  path 完全相同的前提下可以更新 query/fragment，HTTP 默认端口 80 与 HTTPS 默认端口
  443 在同源比较中按无端口形式等价处理。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、14 文件隔离 staging/部署、SHA-256 清单和日志自动判门均通过。定向
  `TEST149-199/999` 证据和默认全量 gate 均已保存到上述 `tmp/device-runs/` 路径。
- gate 会在设备端只回收自己命名的旧候选目录；本次因旧目录占满 `\Temp` 暴露并验证了该
  回收路径。WMDC 旧 COM 注册的 5 个 RAPI 类已由正式修复脚本做 32/64 位幂等验证。

### 最近定向检查点：next219 修正版

- 配置：`TEST13/151-186/999`，共 38 项。
- 结果：37 条标准数字 `OK`、1 条 TEST13 overview、零 `ERROR`、零 `FAIL`、最终
  `TESTBENCH PASS`。
- 能力终点：根相对 URL 的末尾半编码 double-dot segment。
- 首包 `C:\WMShare\Positron-next219` 因 20,991 字符 DOM bootstrap 在 TEST162 超过既有
  1000ms 预算而失败；修正版使用共享 `ppartial` helper 将 bootstrap 降到 19,735 字符，
  没有提高预算。旧首包不得作为基线。

### 仍有效的人工证据

- next167 已人工确认 example.com `Learn More` 后页面容器边距正常。
- next167 已人工确认真实 SIP 候选词可以完整提交，不再只输入下一个字符。
- 2026-08-14 的 TEST75 纵向/横向截图（`tmp/QQ20260814-195629.png`、
  `tmp/QQ20260814-195643.png`）显示灰色定位父框和四个彩色元素均在预期坐标内；
  小色块内文字的溢出是 fixture 的预期内容，不是布局回归。
- 视觉、真实触摸、SIP、旋转和失败网络允许累计后集中复核；崩溃、数据损坏、严重布局
  破坏或核心交互阻塞必须立即检查。

## 已关闭批次：next232

目标：让根相对 history state URL 与既有安全 absolute/document-relative pathname 使用同一套
路径段规则；继续在不发 GET 的前提下保留普通 percent-encoded pathname segment，并保持
state、length、traversal、popstate/hashchange 行为可预测。编码 dot segment、重复分隔符、
跨源和 protocol-relative URL 仍明确拒绝。

实现边界：

- JS bootstrap 的 `phistoryRelativePath` 保留 raw dot segment、重复分隔符和父路径拒绝；
  根相对 URL 先剥离 query/fragment，再复用同一校验，普通 percent-encoded segment（例如
  `%2Ebook`、`file%2Ejson`、`%2Fencoded`）仍可保留，但完整编码/混合编码的 `.`、`..`
  segment 仍拒绝。
- C 侧 history bridge 未扩大 URL parser，只沿用已验证的同源判定和 HTTP `:80`、HTTPS `:443`
  默认端口等价规则，把安全 pathname 结果写入现有历史条目。
- TEST149、TEST189–194、TEST195、TEST196 的旧“任意 absolute path 都拒绝”负例继续覆盖
  真正不安全的 dot/repeated-separator path；TEST197 覆盖安全 pathname replace/push；
  TEST198 新增普通 percent-encoded segment replace/push、无 GET、state/length、traversal、
  popstate/hashchange 和编码 dot/cross-origin 拒绝覆盖；TEST199 覆盖根相对与
  document-relative 普通编码段、根相对不安全路径拒绝和同样的 traversal 事件门。
- 默认 `javascript=0`、TEST13 行为、公共 ABI 和 callback 总上限不变。

自动化同步完成：

- `device_gate.ps1` 使用 WMDC 当前 RAPI 会话，不依赖 CoreCon 活动连接或 VMID；支持
  `-TestSelection` 只覆盖隔离 staging 的测试选择。
- gate 只回收 `\Temp\Positron-device-gate` 下符合自身 candidate/timestamp 规则的旧目录；
  未识别目录保留。
- `repair_wmdc_rapi.bat/.ps1` 自行请求 UAC，幂等修复 5 个已知 WMDC RAPI COM 类的 32/64
  位旧 `%windir%` 路径；未知注册值拒绝修改。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- PowerShell 解析、修复脚本 `changed=0/status=PASS` 幂等门；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST149-199/999` 和默认 147 项全量 WMDC/RAPI 设备门；
- TEST13 三段真实导航、零 `ERROR`、零 `FAIL`、唯一 `TESTBENCH PASS`。

## 已关闭批次：next233

目标：补齐 `history.pushState`/`replaceState` 可选 URL 参数的一个明确边界：调用者显式传入
`undefined` 时默认当前 document URL，同时保持显式空字符串的同 URL entry 语义，并证明两者
都不会触发网络请求或破坏 history traversal。

实现边界：

- 浏览器脚本 bridge 只在第三个参数确实不是 `undefined` 时字符串化 URL；缺省/显式
  `undefined` 走当前 URL，显式 `''` 继续走当前 URL 的同 URL entry；未扩大 URL parser 或
  放宽既有安全路径拒绝规则。
- TEST200 覆盖 replace/push 返回值、location/document.URL、history length/state、bridge
  URL/state、无 GET、back/forward 的 popstate 顺序，以及 traversal 后再次 replace/push；
  TEST198/199 和上一批测试继续覆盖普通编码段、根相对路径与不安全 URL 拒绝。
- 默认 `javascript=0`、TEST13 行为、公共 ABI 和 callback 总上限不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST149-200/999`（53 项）和全量 `TEST13/20/27/43/44/56/58-77/80-200/999`
  （148 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、零 `ERROR`、零 `FAIL`、唯一 `TESTBENCH PASS`。

## 已关闭批次：next234

目标：建立正式浏览器产品组合层，先把无窗口、无网络、无 JavaScript 依赖的 history/session
状态机从 `test_host/main.c` 迁入 `positron_browser.dll`，并让宿主通过公共 C ABI 消费它。

实现边界：

- 新增 `positron_browser.dll`、`positron_browser.h` 和 VS2008 ARMV4I 工程；它只依赖
  `positron_json.dll` 验证 JSON state，不依赖 DOM、脚本、网络或 WM 控件。
- 公共 ABI 使用 UTF-8、opaque history handle、明确销毁函数和借用字符串生命周期；覆盖
  document commit/replace/traversal、push/replace state、same-origin/default-port、
  history length/index/state 投影和独立 document identity。
- `test_host` 的旧 history 函数保留为兼容适配与断言镜像，产品状态不再由宿主数组维护；
  TEST201 绕过适配层直接验证 DLL。JS bootstrap、DOM/Event/form bridge 仍未迁移，下一批处理。
- stage、device gate、test_host 工程和所有面向读者文档已包含第七个产品 DLL；默认
  `javascript=0`、TEST13、公共 core/script ABI 和既有人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST201/999`（2 项）、`TEST149-201/999`（54 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-201/999`（149 项）WMDC/RAPI 设备门；
- 参数边界/非法 JSON 的最新定向 `TEST201/999` 证据位于
  `tmp/device-runs/20260814-235415-next234-browser-history-contract/`；
- TEST13 三段真实导航、零 `ERROR`、零 `FAIL`、唯一 `TESTBENCH PASS`。

## 已关闭批次：next235

目标：把浏览器 JavaScript session 的 PScript context 所有权、host JSON callback 注册/调用和
销毁生命周期从 `test_host/main.c` 收拢到 `positron_browser.dll`，同时保持现有宿主适配边界。

实现边界：

- `positron_browser.dll` 新增 `PBrowser_ScriptSession*` opaque API，持有一个 PScript context，
  负责求值、全局值、JSON callback 注册/注销/调用、结果/错误读取和 native function count；
  `PBrowser_ScriptSessionRuntime` 仅作为迁移期只读诊断借用句柄。
- `test_host` 的 bridge 通过 product session API 驱动既有脚本路径；窗口、core document、导航、
  DOM/Event/form/input/location callback 实现和 bootstrap 文本仍在宿主，下一批再迁。
- TEST202 直接调用 product script session API，覆盖 context 持久求值、host callback、注销、
  参数错误和产品所有权；默认 `javascript=0`、TEST13、公共 core/script ABI 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST202/999`（2 项）、`TEST112-135/137-152/189-202/999`（55 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-202/999`（150 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST202 product session、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next236

目标：把浏览器 bootstrap 文本及其产品求值入口从 `test_host/main.c` 迁入
`positron_browser.dll`，让宿主只负责安装 `__pcore*` globals、JSON callback 和 core/窗口/导航
适配，不再拥有浏览器对象初始化脚本。

实现边界：

- `positron_browser.dll` 新增 `PBrowser_ScriptSessionEvaluateBootstrap`，持有 browser
  `window`、`document`、`location`、`history`、事件和表单对象的 bootstrap 文本；该入口复用
  已迁入的 product script session，不拥有 core document、native controls 或 host callback pw。
- `test_host` 的 bootstrap 常量已删除，既有 callback 注册和页面执行路径改为调用公共 bootstrap
  API；DOM/Event/form/input/location/navigation callback 实现仍在宿主，下一批迁移。
- TEST203 直接验证 product bootstrap 的参数错误、持久 session、document URL、location、
  history length/state 和 native callback 计数；默认 `javascript=0`、TEST13、公共 ABI 和
  人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST203/999`（2 项）、`TEST112-135/137-152/189-203/999`（56 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-203/999`（151 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST203 product bootstrap、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next237

目标：把 `__pcoreHasElement` 与 `__pcoreGetText` 的 DOM 只读 JSON callback 分发从
`test_host/main.c` 迁入 `positron_browser.dll`，让宿主只提供 typed document adapter，同时保持
现有脚本、页面和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptDomReadCallbacks` 及注册/注销 API，负责
  解析 `{id}` 参数、调用 typed `has_element`/`get_text`、处理负错误码、probe/精确分配和 JSON
  bool/string 结果编码；DOM read 绑定随 script session 创建和销毁。
- `test_host` 删除两套 DOM read JSON 实现，只保留 `PCore_NodeExistsById`、
  `PCore_NodeTextContentById` 的 typed adapter；DOM 写入/Event/form/input/location/navigation
  callback 仍留在宿主，未扩大本批范围。
- TEST204 直接验证参数错误、typed callback 注册、native callback 数量、bootstrap 读取、缺失
  元素和注销；默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST204/999`（2 项）、`TEST189-204/999`（17 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-204/999`（152 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST204 product DOM read、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next238

目标：把 `__pcoreSetText` 的 DOM textContent 写入 JSON callback 分发从 `test_host/main.c`
迁入 `positron_browser.dll`，让宿主只提供 typed document adapter，同时保持现有 bootstrap、
页面脚本和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptDomWriteCallbacks` 及注册/注销 API，
  负责解析 `{id,text}` 参数、调用 typed `set_text`、区分成功/目标缺失/adapter error 并编码
  JSON bool 结果；DOM write 绑定随 script session 创建和销毁。
- `test_host` 删除 `__pcoreSetText` 的 JSON 实现，只保留 `PCore_NodeSetTextContentById` 的
  typed adapter；attribute、value、checked、Event/form/input/location/navigation callback 仍留
  在宿主，未扩大本批范围。
- TEST205 直接验证参数错误、typed callback 注册、native callback 数量、成功/缺失目标结果、
  callback 状态更新和注销；默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST205/999`（2 项）、`TEST112-135/137-152/189-205/999`（58 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-205/999`（153 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST205 product DOM write、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next239

目标：把 `__pcoreGetAttribute`、`__pcoreSetAttribute` 和 `__pcoreRemoveAttribute` 的 DOM
attribute JSON callback 分发从 `test_host/main.c` 迁入 `positron_browser.dll`，让宿主只提供
typed document adapter，同时保持现有 bootstrap、页面脚本和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptDomAttributeCallbacks` 及注册/注销 API，
  负责解析 `{id,name,value}` 参数、处理 getter 的 `null`/字符串结果、调用 typed get/set/remove
  adapter、校验 probe 长度并编码 JSON 结果；attribute 绑定随 script session 创建和销毁。
- `test_host` 删除三个 attribute JSON 实现，只保留 `PCore_NodeAttributeById`、
  `PCore_NodeSetAttributeById`、`PCore_NodeRemoveAttributeById` 的 typed adapter；Event/form/input/
  location/navigation callback 仍留在宿主，未扩大本批范围。
- TEST206 直接验证参数错误、typed callback 注册、native callback 数量、属性缺失/设置/读取/删除、
  setter false 语义和注销；默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST206/999`（2 项）、`TEST112-135/137-152/189-206/999`（59 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-206/999`（154 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST206 product DOM attribute、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next240

目标：把 `__pcoreAddEvent`、`__pcoreRemoveEvent` 的事件注册/注销 JSON 分发和原生事件数据编码
从 `test_host/main.c` 迁入 `positron_browser.dll`，让宿主只提供 core/document/listener typed
adapter，同时保持现有 bootstrap、页面脚本和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptEventCallbacks` 和
  `PBrowserScriptEventInfo`，负责验证事件类型、解析注册/注销参数、返回宿主 listener token，
  并把同步事件元数据编码为 `__pcoreDispatchEvent` JSON；`preventDefault` 结果通过稳定 bitmask
  返回，事件绑定随 script session 创建和销毁。
- `test_host` 删除 `__pcoreAddEvent`/`__pcoreRemoveEvent` 的 JSON 实现和事件 JSON 编码，只保留
  `PCore_EventListenerAdd/Remove`、listener 生命周期和 `PCoreEventInfo` 到 product typed event
  info 的适配；form/input/location/navigation callback 仍留在宿主，未扩大本批范围。
- TEST207 直接验证参数错误、typed callback 注册/重复注册/注销、native callback 数量、事件字段
  编码、handler 发现和 `preventDefault` action；默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST207/999`（2 项）、`TEST112-135/137-152/189-207/999`（60 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-207/999`（155 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST207 product Event、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next241

目标：把 `__pcoreGetValue`、`__pcoreSetValue` 的 input value JSON 分发从
`test_host/main.c` 迁入 `positron_browser.dll`，让宿主只提供 core/document value typed
adapter，同时保持现有 bootstrap、页面脚本和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptDomValueCallbacks`，负责解析 value
  读写参数、执行 UTF-8 size-probe/结果编码，并在 script session 创建、注销和销毁时管理两个
  native globals。
- `test_host` 删除 value JSON 解析和编码，只保留 `PCore_NodeValueById`、
  `PCore_NodeSetValueById` 到 product typed value adapter 的转换；checked/defaultValue/
  selectedIndex、其余 form/input/location/navigation callback 仍留在宿主，未扩大本批范围。
- TEST208 直接验证参数错误、typed callback 注册/重复注册/注销、native callback 数量、值读取、
  更新、缺失目标和非法参数；默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST208/999`（2 项）、`TEST112-135/137-152/189-208/999`（61 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-208/999`（156 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST208 product DOM value、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next242

目标：把 `__pcoreGetChecked`、`__pcoreSetChecked` 的 checked JSON 分发从
`test_host/main.c` 迁入 `positron_browser.dll`，让宿主只提供 core/document checked typed
adapter，同时保持现有 bootstrap、页面脚本和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptDomCheckedCallbacks`，负责解析 checked
  读写参数、规范化布尔值、结果编码，并在 script session 创建、注销和销毁时管理两个 native globals。
- `test_host` 删除 checked JSON 解析和编码，只保留 `PCore_NodeCheckedById`、
  `PCore_NodeSetCheckedById` 到 product typed checked adapter 的转换；defaultChecked/defaultValue/
  selectedIndex、其余 form/input/location/navigation callback 仍留在宿主，未扩大本批范围。
- TEST209 直接验证参数错误、typed callback 注册/重复注册/注销、native callback 数量、checked 读取、
  更新、缺失目标和非法参数；默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST209/999`（2 项）、`TEST112-135/137-152/189-209/999`（62 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-209/999`（157 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST209 product DOM checked、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。TEST144 曾有一次设备执行超时，单测重跑及最终全量均通过，
  未调整执行预算。

## 已关闭批次：next243

目标：把 `__pcoreFormProperty` 的 form-property JSON 分发从 `test_host/main.c` 迁入
`positron_browser.dll`，让宿主只提供 defaultValue、defaultChecked、selectedIndex 三组
core/document typed adapter，同时保持既有 bootstrap、页面脚本、native callback 数量和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptFormCallbacks`，由产品层解析并编码
  `getDefaultValue`/`setDefaultValue`、`getDefaultChecked`/`setDefaultChecked`、
  `getSelectedIndex`/`setSelectedIndex` 六种操作；一个 `__pcoreFormProperty` global 在 session
  注册、注销和销毁时由产品层完整管理。
- `test_host` 删除 form-property JSON 解析和编码，只保留
  `PCore_NodeDefaultValueById`、`PCore_NodeSetDefaultValueById`、
  `PCore_NodeDefaultCheckedById`、`PCore_NodeSetDefaultCheckedById`、
  `PCore_NodeSelectedIndexById`、`PCore_NodeSetSelectedIndexById` 到 product typed adapter 的转换；
  其余 form/input/location/navigation callback 仍留在宿主，未扩大本批范围。
- TEST210 直接验证参数错误、typed callback 注册/重复注册/注销、单一 native callback 数量、六种
  form-property 操作、缺失目标、非法操作和资源关闭；既有 TEST112–209 的 native callback 数量断言保持 14。
  默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST210/999`（2 项）、`TEST112-135/137-152/189-210/999`（63 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-210/999`（158 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST210 product form-property、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；完整证据路径见本文件顶部。全量首尝因 TEST13 网络瞬态失败，重试通过，未
  调整执行预算。

## 已关闭批次：next244

目标：把 `__pcoreNavigation` 的 JSON 分发从 `test_host/main.c` 迁入
`positron_browser.dll`，让产品层拥有 navigation 参数解析、结果编码和 session 生命周期；宿主只
提供 typed navigation adapter，继续保留 history、窗口、网络和导航副作用。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptNavigationCallbacks` 及注册/注销 API，
  负责 `replaceState`、`pushState`、`back`、`forward`、`go`、`assign`、`reload`、`replace`、
  `fragment`、`fragmentReplace` 十种 operation 的 JSON 参数校验、URL/state/delta 编码、返回值
  编码和 native global 的完整生命周期管理。
- `test_host` 删除 navigation JSON 解析和编码，只保留既有 history/base-origin/窗口副作用适配，
  通过 typed `PBrowserScriptNavigationInfo` 接收产品层结果；公共 core/script ABI、既有 URL 政策、
  native callback 数量和页面行为不变。
- TEST211 直接验证产品 navigation JSON dispatch、十种 operation、pushState 返回 history length、
  非法参数、未知 operation、重复注册和注销；既有 TEST112–210 回归保持通过。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST211/999`（2 项）、`TEST112-135/137-152/189-211/999`（64 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-211/999`（159 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST211 product navigation、零 `ERROR`、零 `FAIL`、唯一 `TESTBENCH PASS`；
  完整证据路径见本文件顶部。全量中途曾出现 TEST129、TEST153、TEST192 的单次 JavaScript
  timeout；定向回归及最终全量重试通过，未调整执行预算或放宽断言。

## 已关闭批次：next264

目标：把 native file input 的宿主 GUI picker 选择、取消、错误和生命周期边界拆开验证；
保留 picker、文件系统权限、窗口和真实控件副作用在 `test_host`，不新增产品 ABI。

实现边界：

- `pcore_file_picker_system` 只封装 WM6 `GetOpenFileNameEx`；其 false 返回按用户取消处理，
  picker adapter 的负值表示 host-side open/error，结果在同步调用结束时归一化并保证缓冲区结尾。
- `pcore_file_input_commit_selection` 负责 UTF-16 → UTF-8、按 file index 提交 core value/path，
  先读取布局信息再写入，成功后复用既有 file `input`/`change` typed notifications；窗口为空时
  不做无主窗口重绘。
- `pcore_handle_file_input_with_picker` 保留真实 WM/label 路径，测试通过 host-only callback 注入
  选择/取消/错误，不把 callback 或 picker 状态暴露到 `positron_core.dll`/`positron_browser.dll`。
- TEST231 覆盖 cancel、picker error、空选择提交错误、成功 selection、`input` → `change`、再次
  cancel 保留已选状态和同步 callback active 生命周期。

已经核验并提升为定向基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建（0 errors，3 个既有 libcss C4244 warnings）；
- 定向 `TEST231/999`（2 项）和 `TEST70,189-231/999`（45 项）WMDC/RAPI 设备门；
- 两组均零 `ERROR`、零 `FAIL`，唯一 `TESTBENCH PASS`；next255 的 170 项完整门仍是最近一次
  全量基线，本批未重复全量。

## 已关闭批次：next263

目标：把 native file input 的程序化 `HTMLElement.click()` 接到既有 typed click contract，
同时明确自动脚本与系统文件选择器的边界；不把 picker、文件系统权限或窗口生命周期迁入产品 DLL。

实现边界：

- `test_host` 的 programmatic-click adapter 对 kind 10 file control 先通过既有 typed click
  callback；disabled 控件沿用统一静默规则，click 被取消时不发生任何默认动作。
- 程序化 file click 在同步脚本路径只分发 click 并返回，不调用 `pcore_handle_file_input`；系统
  picker 仍只由真实 WM/label GUI 路径显式打开，选取完成后的既有 file `input`/`change` metadata
  contract 保持不变。
- TEST230 覆盖启用/取消/disabled file click、click 事件目标和空 value/path，TEST228 继续覆盖
  programmatic-click adapter error、注销和 native function 资源关闭。

已经核验并提升为定向基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建（0 errors，3 个既有 libcss C4244 warnings）；
- 定向 `TEST230/999`（2 项）和 `TEST70,189-230/999`（44 项）WMDC/RAPI 设备门；
- 两组均零 `ERROR`、零 `FAIL`，唯一 `TESTBENCH PASS`；next255 的 170 项完整门仍是最近一次
  全量基线，本批未重复全量。

## 已关闭批次：next262

目标：把 native submit/reset/button 的程序化 `HTMLElement.click()` 接入既有 typed click、
form-event 和提交/重置宿主默认路径；宿主继续拥有表单收集、约束校验、导航/网络和窗口副作用。

实现边界：

- `test_host` 的 programmatic adapter 对 submit/reset/button 先通过既有 typed click contract；
  submit/reset 再按控件中心复用 typed form-event，generic button 只有 click，disabled 控件静默。
- `pcore_handle_form_button_default` 抽出既有表单默认路径，程序化 submit/reset 在当前 render
  document 上复用提交收集、invalid、导航和重绘；初始导航脚本的 reset 状态可直接提交，窗口/网络
  生命周期没有迁入产品 DLL。
- `positron_core` 的 reset helper 不再调用会自行发出第二次 reset 事件的 libdom reset 入口，
  只恢复 input/textarea/select 的初始状态；可取消 reset 事件继续由宿主先分发。
- TEST229 覆盖 submit/reset/button 的 click 与 form-event 顺序、submit/reset 取消、reset 初值
  恢复、generic/disabled no-op 和按 id 的控件 kind/geometry；next261 TEST228 继续覆盖
  programmatic-click callback 的 adapter error、重复注册、注销和资源关闭。

已经核验并提升为定向基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建（0 errors，3 个既有 libcss C4244 warnings）；
- 定向 `TEST229/999`（2 项）和 `TEST68-69,189-229/999`（44 项）WMDC/RAPI 设备门；
- 两组最终均零 `ERROR`、零 `FAIL`，唯一 `TESTBENCH PASS`；回归首尝 TEST193 的既有
  JavaScript timeout 原配置重试通过；next255 的 170 项完整门仍是最近一次全量基线。

## 已关闭批次：next261

目标：把 checkbox/radio 的程序化 activation（`HTMLElement.click()`）接入产品浏览器
session，并让宿主按 DOM id 复用既有 typed click、input、change contracts；宿主继续拥有
checked 状态写回、radio group 互斥、重排、窗口和控件默认副作用。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptProgrammaticClickInfo`、
  `PBrowserScriptProgrammaticClickCallbacks`、注册/注销 API 和
  `PBrowser_ScriptSessionDispatchProgrammaticClick`；bootstrap 为每个 `PElement` 提供
  `click()`，同步调用宿主 adapter，adapter error 映射为 `PSCRIPT_ERROR_NATIVE`。
- `positron_core.dll` 新增 `PCore_FormControlInfoById`，只返回已布局 form gadget 的
  document-space geometry/kind/state，不迁移 checked 属性或 radio 算法。
- `test_host` 的 programmatic adapter 对 disabled form control 静默；checkbox/radio 先
  通过既有 typed click contract，以 id 对应控件中心分发 click，允许后复用既有 form-toggle
  `input` → `change`；取消、未命中和已选 radio 不提交默认状态，重绘/restyle 仍由宿主负责。
- TEST228 同时覆盖 product callback 的重复注册、非法参数、adapter error、注销和资源
  关闭，以及真实脚本 `click()` 的事件目标、顺序、取消、disabled/no-op、radio 互斥和状态。

已经核验并提升为定向基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建（0 errors，3 个既有 libcss C4244 warnings）；
- 定向 `TEST228/999`（2 项）和 `TEST189-228/999`（41 项）WMDC/RAPI 设备门；
- TEST228 与相关回归均零 `ERROR`、零 `FAIL`，各自唯一 `TESTBENCH PASS`；next255 的 170 项
  完整门仍是最近一次全量基线，本批未重复全量。

## 已关闭批次：next260

目标：把 core checkbox/radio 的 Space/Enter 键盘激活接入既有 typed key/click/input/change
contracts；宿主继续拥有键盘消息翻译、焦点记录、checked 状态、radio 互斥、重排、窗口和
控件默认副作用。

实现边界：

- 不新增产品 ABI；宿主在 core toggle 获得焦点后，主窗口的 Space/Enter WM_KEYDOWN/UP
  通过既有 typed key contract 分发；允许的首次 keydown 再通过既有 typed click contract，
  最后复用 form-toggle 的 `input` → `change`。keydown 或 click 被取消时不提交状态；重复
  keydown 只分发键盘事件、不重复激活；disabled 控件不获得键盘 toggle 焦点。
- 焦点索引、WM 消息和默认激活仍由 `test_host` 宿主持有；未迁移 checked 属性同步、radio
  算法、程序化 `click()` 或 native control 生命周期，也没有改变公共产品 ABI。
- TEST227 通过真实渲染窗口和消息路径验证 Space/Enter、事件顺序、keydown/click 取消、重复
  keydown 和 disabled/no-op 边界。

已经核验并提升为定向基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建（0 errors，3 个既有 libcss C4244 warnings）；
- 定向 `TEST227/999`（2 项）和 `TEST189-227/999`（40 项）WMDC/RAPI 设备门；
- TEST227 与相关回归均零 `ERROR`、零 `FAIL`，各自唯一 `TESTBENCH PASS`；next255 的 170 项
  完整门仍是最近一次全量基线，本批未重复全量。

## 已关闭批次：next259

目标：把 checkbox/radio 的 label/native activation click 目标与事件顺序接入现有
`positron_browser.dll` typed click contract；宿主继续拥有命中、控件状态、radio 互斥、重排、
窗口和控件默认副作用。

实现边界：

- 不新增产品 ABI；label 自身的初始 click 仍由 `WM_LBUTTONDOWN` 通过既有 click contract
  分发。label 目标为启用 checkbox/radio 时，宿主再以目标控件坐标调用同一 typed click
  contract；目标 click 取消则阻止后续状态提交和 `input`/`change`，adapter 错误按既有
  fail-closed 规则处理，disabled 目标不产生合成 click。
- 目标 click 允许后，既有 form-toggle 路径继续负责 checked 状态、radio group 互斥及
  `input` → `change`；未迁移 checked 属性同步、radio 算法、WM 控件生命周期或视觉状态。
- TEST226 通过真实 label、checkbox/radio 和 disabled 控件验证 click 目标、冒泡/可取消字段、
  取消阻断、事件顺序和静默边界；没有改变公共产品 ABI 或 test_host 之外的所有权。

已经核验并提升为定向基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建（0 errors，3 个既有 libcss C4244 warnings）；
- 定向 `TEST226/999`（2 项）和 `TEST189-226/999`（39 项）WMDC/RAPI 设备门；
- TEST226 与相关回归均零 `ERROR`、零 `FAIL`，各自唯一 `TESTBENCH PASS`；next255 的 170 项
  完整门仍是最近一次全量基线，本批未重复全量。

## 已关闭批次：next258

目标：把 native checkbox/radio activation 完成后的 `input` 事件分发入口从
`test_host/main.c` 的直接状态路径接到 `positron_browser.dll` 已有的 typed input
callback contract；宿主继续拥有点击命中、checked 状态写回、radio group 互斥、重排、
窗口和控件默认副作用。

实现边界：

- 不新增产品 ABI；复用 `PBrowserScriptInputEventInfo` 和
  `PBrowser_ScriptSessionDispatchInputEvent`，用目标控件中心文档坐标、空
  `inputType/data`、`is_composing=0`、`bubbles=1/cancelable=0` 表达 checkbox/radio
  的 non-text `input` notification。
- `test_host` 只在 `PCore_FormActivateAt` 成功提交且 selected 状态确实改变后按
  `input` → `change` 顺序分发；adapter error/callback cancellation 不回滚已提交的 core
  状态，也不改变重绘、radio 互斥、窗口或网络策略。disabled、已选 radio 和未命中控件保持静默。
- TEST225 通过真实 core 文档、产品脚本 session 和事件 listener 验证 checkbox/radio
  input metadata、目标、冒泡/取消字段、事件顺序及静默边界；TEST213/223 已覆盖既有
  input contract 的非法参数、adapter error、注销和资源关闭边界。

已经核验并提升为定向基线：

- `python scripts/test_c89ize.py`；
- VS2008 ARMV4I Debug 正式构建（0 errors，3 个既有 libcss C4244 warnings）；
- 定向 `TEST225/999`（2 项）和 `TEST189-225/999`（38 项）WMDC/RAPI 设备门；
- TEST225 与相关回归均零 `ERROR`、零 `FAIL`，各自唯一 `TESTBENCH PASS`；next255 的 170 项
  完整门仍是最近一次全量基线，本批未重复全量。

## 已关闭批次：next257

目标：把 native checkbox/radio activation 完成后的 `change` 事件分发入口从
`test_host/main.c` 的直接 `PCore_FormActivateAt` 路径接到 `positron_browser.dll` 已有的
typed select-style callback contract；宿主继续拥有点击命中、checked 状态写回、radio group
互斥、重排、窗口和控件默认副作用。

实现边界：

- 不新增产品 ABI；复用 `PBrowserScriptSelectEventInfo` 和
  `PBrowser_ScriptSessionDispatchSelectEvent` 的非可取消 `change` contract，并明确该
  selection-like contract 同时覆盖 SELECT、checkbox/radio 和 file-input 的 change 通知。
- `test_host` 新增 form-toggle 激活包装器，先记录目标 checkbox/radio 的 selected 状态，调用
  `PCore_FormActivateAt`，确认状态确实改变后再按点击/label 目标坐标分发 `change`；disabled、
  已选 radio 和未命中控件不发事件，adapter error 不回滚核心状态。重绘和 interaction restyle
  仍由宿主处理。
- TEST224 通过真实 core 文档、产品脚本 session 和事件 listener 验证 checkbox/radio change
  的目标、冒泡/取消字段、状态变化顺序，以及 disabled/已选控件的静默边界；没有迁移 click、
  checked 属性同步、radio 算法或视觉状态。

已经核验并提升为定向基线：

- `python scripts/test_c89ize.py`；
- VS2008 ARMV4I Debug 正式构建（0 errors，3 个既有 libcss C4244 warnings）；
- 定向 `TEST224/999`（2 项）和 `TEST189-224/999`（37 项）WMDC/RAPI 设备门；
- TEST224 与相关回归均零 `ERROR`、零 `FAIL`，各自唯一 `TESTBENCH PASS`；next255 的 170 项
  完整门仍是最近一次全量基线，本批未重复全量。

## 已关闭批次：next256

目标：把 native file input 选取完成后的 `input`/`change` 事件分发入口从
`test_host/main.c` 的直接文件控件路径接到 `positron_browser.dll` 已有的 typed input/select
callback contract；宿主继续拥有系统文件选择器、路径所有权、文件读取、窗口和网络副作用。

实现边界：

- 不新增产品 ABI；`input` 复用 `PBrowserScriptInputEventInfo`，用文件控件中心的文档坐标、
  `inputType="insertFromFile"`、空 `data`、`is_composing=0` 和 `bubbles=1/cancelable=0`
  表达 InputEvent metadata；`change` 复用 `PBrowserScriptSelectEventInfo`，保持同一坐标、
  `bubbles=1/cancelable=0` 和不可取消语义。
- `test_host` 只在 `PCore_FileInputSetPath` 与 `PCore_FileInputInfo` 成功后按 `input` → `change`
  顺序分发；选择器取消仍只退出 picker，adapter error 或 callback cancellation 不回滚已提交的
  core 路径，也不改变文件读取、multipart 编码、窗口或网络策略。
- TEST223 直接验证两个既有 callback 的注册/重复注册、file metadata、顺序、取消结果、非法
  参数、input/change adapter error、注销和资源关闭；TEST189–222 相关回归保持通过。

已经核验并提升为定向基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST223/999`（2 项）和 `TEST189-223/999`（36 项）WMDC/RAPI 设备门；
- TEST223 file-input typed callback reuse、零 `ERROR`、零 `FAIL`、每组唯一
  `TESTBENCH PASS`；next255 的 170 项完整门仍是最近一次全量基线，完整证据路径见本文件顶部。

## 已关闭批次：next255

目标：把 native form constraint validation 产生的 `invalid` 事件分发入口从
`test_host/main.c` 的直接验证/UI 路径接到 `positron_browser.dll` 的稳定 typed callback
contract；宿主继续拥有约束算法、首个无效控件收集、焦点/滚动/提示音反馈、窗口、网络和导航副作用。

实现边界：

- `positron_browser.dll` 新增 `PBrowserScriptInvalidEventInfo`、
  `PBrowserScriptInvalidCallbacks`、注册/注销 API 和
  `PBrowser_ScriptSessionDispatchInvalidEvent`；产品层只接受 `invalid`，同步调用宿主
  adapter，统一 default-allowed 和 adapter-error 结果。
- `test_host` 在既有 constraint validation 找到首个无效控件后，用该控件中心的文档坐标构造
  产品 invalid typed info；事件不冒泡且可取消，宿主 adapter 仍只把产品请求转成
  `PCore_EventDispatchAt`。取消或 adapter error 会抑制宿主 invalid 反馈，但不绕过验证阻止
  提交；没有迁移约束算法、invalid UI、WM 消息或表单提交编码。
- TEST222 直接验证注册/重复注册、invalid 字段、成功/取消、非法事件、adapter error、注销和
  资源关闭；既有 TEST13/TEST204–221 回归保持通过。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST222/999`（2 项）、`TEST112-135/137-152/189-222/999`（75 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-222/999`（170 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST222 product native invalid dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next254

目标：把 native form activation 产生的 `submit`/`reset` 事件分发入口从
`test_host/main.c` 的直接 core/form 路径接到 `positron_browser.dll` 的稳定 typed callback
contract；宿主继续拥有表单数据收集、验证、控件默认 activation、窗口、网络和导航副作用。

实现边界：

- `positron_browser.dll` 新增 `PBrowserScriptFormEventInfo`、
  `PBrowserScriptFormEventCallbacks`、注册/注销 API 和
  `PBrowser_ScriptSessionDispatchFormEvent`；产品层只接受 `submit`/`reset`，同步调用宿主
  adapter，统一 default-allowed 和 adapter-error 结果。
- `test_host` 的 submit/reset button 路径在默认动作前构造产品 typed info；有效的 submit
  还经过既有 constraint validation，native EDIT 的 Enter 隐式提交使用文本控件中心坐标；宿主
  adapter 仍只把产品请求转成 `PCore_EventDispatchAt`，保留文档坐标、冒泡/cancelable 字段、
  preventDefault 结果以及表单数据收集/验证/导航副作用。没有迁移 WM 消息、提交编码、URL
  解析、网络或控件默认 activation。
- TEST221 直接验证注册/重复注册、submit/reset 字段、成功/取消、非法事件、adapter error、
  注销和资源关闭；既有 TEST13/TEST204–220 回归保持通过。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST221/999`（2 项）、`TEST112-135/137-152/189-221/999`（74 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-221/999`（169 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST221 product native submit/reset dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next253

目标：把 native `click` 事件分发入口从 `test_host/main.c` 的直接
`PCore_EventDispatchAt` 调用接到 `positron_browser.dll` 的稳定 typed callback contract；
宿主继续拥有 WM 鼠标消息、坐标命中、core 事件传播以及表单/控件默认 activation 和导航副作用。

实现边界：

- `positron_browser.dll` 新增 `PBrowserScriptClickEventInfo`、
  `PBrowserScriptClickCallbacks`、注册/注销 API 和
  `PBrowser_ScriptSessionDispatchClickEvent`；产品层只接受 `click`，同步调用宿主 adapter，
  统一 default-allowed 和 adapter-error 结果。
- `test_host` 的 `WM_LBUTTONDOWN` 路径构造产品 click typed info；宿主 adapter 仍只把产品请求
  转成 `PCore_EventDispatchAt`，保留文档坐标、冒泡/cancelable 字段、preventDefault 结果和
  控件/链接默认副作用。没有迁移 WM 消息、表单提交、窗口、网络或导航策略。
- TEST220 直接验证注册/重复注册、click 字段、成功/取消、非法事件、adapter error、注销和
  资源关闭；既有 TEST13/TEST204–219 回归保持通过。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST220/999`（2 项）、`TEST112-135/137-152/189-220/999`（73 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-220/999`（168 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST220 product native click dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next252

目标：把 native EDIT 在 `EN_CHANGE` 后的 `input` 事件分发入口接到已有的
`positron_browser.dll` input typed callback contract；宿主继续拥有 WM 消息、控件生命周期、
坐标命中、core 事件传播和 EDIT 的原生默认行为。

实现边界：

- `test_host` 为每个允许默认行为的 native EDIT `beforeinput` 暂存 `inputType` 与 `data`，
  在 `EN_CHANGE` 同步更新 core 文本后构造产品 `input` typed info；取消、失败和 native 控件
  销毁路径都会清理借用字符串的宿主副本。composition 提交继续使用既有 composition 数据路径。
- `positron_browser.dll` 未新增或改变 ABI；既有 `PBrowserScriptInputEventInfo`/
  `PBrowserScriptInputCallbacks` 现在同时承接 EDIT post-change input，`test_host` adapter
  仍只把产品请求转成 `PCore_EventDispatchInputExAt`，保留坐标命中、冒泡、InputEvent metadata
  和控件默认行为。
- TEST219 直接验证 `input` 事件字段、成功/取消结果、非法事件、adapter error、注销和资源
  关闭；没有迁移 WM 消息、SIP、IME、控件默认行为、表单提交或 composition 生命周期。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST219/999`（2 项）、`TEST112-135/137-152/189-219/999`（72 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-219/999`（167 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST219 product EDIT post-change input dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next251

目标：把 native EDIT 的 `change` 事件分发入口接到
`positron_browser.dll` 的独立 typed callback ABI；宿主继续拥有控件生命周期、坐标命中、core
事件传播和 EDIT 的原生默认行为。

实现边界：

- `positron_browser.dll` 新增 `PBrowserScriptEditEventInfo`、`PBrowserScriptEditCallbacks`、
  注册/注销 API 和 `PBrowser_ScriptSessionDispatchEditEvent`；产品层只接受 `change`，同步调用
  宿主 adapter 并统一 adapter-error 结果，没有复用 SELECT 或 composition ABI。
- `test_host` 的 native EDIT change 路径改为构造产品 typed info；宿主 adapter 仍只把产品请求
  转成 `PCore_EventDispatchAt`，保留坐标命中、冒泡字段、WM 控件同步和控件默认行为。
- TEST218 直接验证注册/重复注册、change 字段、非法事件、adapter error、注销和资源关闭；没有
  迁移 EDIT input、WM 消息、SIP、控件默认行为或表单提交。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST218/999`（2 项）、`TEST112-135/137-152/189-218/999`（71 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-218/999`（166 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST218 product EDIT change dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next250

目标：把 native SELECT 的 `input` 事件分发入口接到已存在的
`positron_browser.dll` SELECT typed callback ABI；宿主继续拥有控件生命周期、坐标命中、core
事件传播和 SELECT 的原生默认行为。

实现边界：

- `positron_browser.dll` 扩展 `PBrowserScriptSelectEventInfo` 的事件契约，允许 `input` 与既有
  `change`，并继续由 `PBrowser_ScriptSessionDispatchSelectEvent` 负责参数校验、同步 callback
  和 adapter-error 映射；没有新增 native callback 槽位或改变旧 ABI 的结构布局。
- `test_host` 的 native SELECT input 路径改为构造产品 typed info；宿主 adapter 仍只把产品请求
  转成 `PCore_EventDispatchAt`，保留坐标命中、冒泡字段、WM 控件同步和 input/change 默认行为。
- TEST217 直接验证 input 字段、注册/重复注册、非法事件、adapter error、注销和资源关闭；既有
  TEST216 的 change 契约保持通过，没有迁移 EDIT change、WM 消息、控件默认行为或表单提交。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST217/999`（2 项）、`TEST112-135/137-152/189-217/999`（70 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-217/999`（165 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST217 product SELECT input dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。回归首尝 TEST134 出现一次 JavaScript
  timeout，定向重跑和最终全量通过，未调整预算或放宽断言。

## 已关闭批次：next249

目标：把 native SELECT 的 `change` 事件分发入口从 `test_host/main.c` 的直接 core 调用迁入
`positron_browser.dll` 的 typed callback ABI；宿主继续拥有控件生命周期、坐标命中、core 事件传播
和 SELECT 的原生默认行为。

实现边界：

- `positron_browser.dll` 新增 `PBrowserScriptSelectEventInfo`、`PBrowserScriptSelectCallbacks`、
  注册/注销 API 和 `PBrowser_ScriptSessionDispatchSelectEvent`；产品层只接受 `change` 事件，
  同步调用宿主 adapter 并统一 adapter-error 结果。
- `test_host` 的 native SELECT change 路径改为构造产品 typed info；宿主 adapter 只把产品请求
  转成 `PCore_EventDispatchAt`，保留坐标命中、冒泡字段、WM 控件同步和 input/change 默认行为。
- TEST216 直接验证注册/重复注册、change 字段、非法事件、adapter error、注销和资源关闭；没有
  迁移 EDIT change、SELECT input、WM 消息、控件默认行为或表单提交。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST216/999`（2 项）、`TEST112-135/137-152/189-216/999`（69 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-216/999`（164 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST216 product SELECT change dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next248

目标：把 native EDIT/SELECT 的 `focus`、`blur`、`focusin` 和 `focusout` 事件分发入口从
`test_host/main.c` 的直接 core 调用迁入 `positron_browser.dll` 的 typed callback ABI；宿主继续
拥有 WM 焦点消息、控件生命周期、坐标命中、core 事件传播和 SIP/控件默认行为。

实现边界：

- `positron_browser.dll` 新增 `PBrowserScriptFocusEventInfo`、`PBrowserScriptFocusCallbacks`、
  注册/注销 API 和 `PBrowser_ScriptSessionDispatchFocusEvent`；产品层限制事件类型为四种
  focus-family 事件，同步调用宿主 adapter 并统一 adapter-error 结果。
- `test_host` 的 native EDIT/SELECT 焦点路径改为构造产品 typed info；宿主 adapter 只把产品请求
  转成 `PCore_EventDispatchAt`，保留 focus/blur 非冒泡和 focusin/focusout 冒泡语义。
- TEST215 直接验证注册/重复注册、四种事件字段、非法事件、adapter error、注销和资源关闭；
  没有迁移 WM 焦点消息、SIP、SELECT 默认行为或表单提交。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST215/999`（2 项）、`TEST112-135/137-152/189-215/999`（68 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-215/999`（163 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST215 product focus-family dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next247

目标：把 native EDIT/SELECT 的 `keydown`、`keyup` 和 `keypress` 键盘事件分发入口从
`test_host/main.c` 的直接 core 调用迁入 `positron_browser.dll` 的 typed callback ABI；宿主继续
拥有 WM 消息翻译、控件坐标命中、core 事件传播和焦点/控件默认行为。

实现边界：

- `positron_browser.dll` 新增 `PBrowserScriptKeyEventInfo`、`PBrowserScriptKeyCallbacks`、
  注册/注销 API 和 `PBrowser_ScriptSessionDispatchKeyEvent`；产品层校验事件契约、同步调用
  宿主 adapter，统一 default-allowed 和 adapter-error 结果。
- `test_host` 的 EDIT/SELECT 键盘路径改为构造产品 typed info；宿主 adapter 只把产品请求转成
  `PCore_EventDispatchKeyExAt`，保留 key/keyCode/charCode、repeat、修饰键和 composing 字段。
- TEST214 直接验证注册/重复注册、keydown/keyup 字段、取消、adapter error、注销和资源关闭；
  没有迁移焦点、SELECT 默认行为、表单提交或 WM 消息处理。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST214/999`（2 项）、`TEST112-135/137-152/189-214/999`（67 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-214/999`（162 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST214 product native keyboard dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next246

目标：把 native 文本输入产生的 `beforeinput`、`input` 和 composition 事件入口从
`test_host/main.c` 的直接 core 调用迁入 `positron_browser.dll` 的 typed callback ABI；宿主继续
拥有 WM EDIT/IME、坐标命中、core 事件传播和 native 默认行为。

实现边界：

- `positron_browser.dll` 新增 `PBrowserScriptInputEventInfo`、
  `PBrowserScriptInputCallbacks`、注册/注销 API 和
  `PBrowser_ScriptSessionDispatchInputEvent`；产品层校验事件契约、同步调用宿主 adapter，
  统一 default-allowed 和 adapter-error 结果。
- `test_host` 的文本输入/composition 路径改为构造产品 typed info；宿主 adapter 只把产品请求
  转成 `PCore_EventDispatchInputExAt`，既有 WM 控件、IME、取消和页面脚本语义不变。
- TEST213 直接验证注册/重复注册、beforeinput 与 composition 字段、取消、adapter error、注销
  和资源关闭；没有迁移键盘、焦点、SELECT 或表单提交副作用。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST213/999`（2 项）、`TEST112-135/137-152/189-213/999`（66 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-213/999`（161 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST213 product native input/composition dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next245

目标：把同文档 history traversal 和 hash location 事件分发从 `test_host/main.c` 的临时全局+
源码求值迁入 `positron_browser.dll`，让产品层拥有脚本 context 内的 `popstate`/`hashchange`
状态更新和事件顺序；宿主继续拥有 history 提交/回滚、窗口、网络和导航副作用。

实现边界：

- `positron_browser.dll` 新增 `PBrowser_ScriptSessionDispatchHistoryTraversal` 和
  `PBrowser_ScriptSessionDispatchHashNavigation` 公共 API，负责临时状态注入、产品 bootstrap
  调用、事件分发和临时 global 清理；不拥有宿主 history entry 或 URL parser。
- `test_host` 删除 traversal/hash 的临时 global 设置与源码求值，只保留已提交 history 的复制、
  回滚、窗口/网络副作用和 typed navigation adapter；既有 public core/script ABI 与 native
  callback 数量不变。
- TEST212 直接验证 traversal/hash API、`popstate` 后 `hashchange` 顺序、location/history 状态、
  非法 JSON/长度和临时 global 清理；既有 TEST112–211 回归保持通过。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST212/999`（2 项）、`TEST112-135/137-152/189-212/999`（65 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-212/999`（160 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST212 product location dispatch、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；完整证据路径见本文件顶部。全量首尝 TEST166 单次 timeout，定向重跑和最终
  全量重试通过，未调整预算或放宽断言。

## 唯一下一步

next295 的自动与人工门、next296 的 `HTMLElement.disabled` 自动门、next297 的表单约束属性
反射与动态语义门、next298 的 validation query 门、next299 的 custom-validity 门、next300 的
form-level validation query 门、next301 的 report-validity/invalid-event 门、next302 的
validationMessage fallback 门、next303 的 pattern/length reflection 门、next304 的 name
reflection 门、next305 的 form submission reflection 门、next306 的 enctype reflection 门、next307 的 submitter action reflection 门、next308 的 submitter method reflection 门、next309 的 submitter enctype reflection 门、next310 的 implicit-submit consistency 门、next311 的 target reflection 门、next312 的 form autocomplete reflection 门、next313 的 acceptCharset reflection 门、next314 的 placeholder reflection 门、next315 的 input autocomplete reflection 门、next316 的 inputMode reflection 门、next317 的 input type reflection 门、next318 的 textarea placeholder coverage 门、next319 的 select autocomplete coverage 门、next320 的 button type submitter boundary 门、next321 的 unknown method fallback 门、next322 的 unknown enctype fallback 门、next323 的 case boundary 门以及 next324 的 metadata relayout 门均已通过。
唯一下一步是从
`KNOWN_LIMITATIONS.md` 和 `ROADMAP.md` 选择下一个不依赖人工页面观察的单一能力，继续保持
每批一个清晰的产品边界。

完成标准：

- TEST292/999、C89、审计和正式构建均保持通过；下一次启用 JavaScript 的相关回归采用
  `68–73/189–231/233–262/264–292/999` 定向选择；next299 的
  TEST93/999 script-limit 门也保持通过；共享的
  回归门采用定向选择，
  只有累计达到检查点或出现风险时再跑全量；
- 后续能力必须有 valid/mismatch 负例和动态值更新；若能力影响 submission，则还要有阻断/恢复的
  自动断言，并通过定向设备门；涉及系统 picker、窗口、真实 SIP、旋转或视觉布局的能力仍须另行标为 manual-only，
  不能以注入 adapter 日志代替真实设备验收；
- 最新 TEST75 纵向/横向截图已核对无异常，其余人工包由用户报告正常；人工验收若切换为
  `auto=0` 不会创建 `test_host.log`，这部分仍以截图/操作记录为人工证据，不替代自动日志；
- 若出现崩溃、数据损坏、严重布局破坏或核心交互阻塞，立即停止累计并进入 debug；
- 候选通过后覆写本文件，并从路线图中选择下一个单一代码能力。

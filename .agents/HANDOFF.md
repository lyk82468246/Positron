# Positron 当前交接

更新时间：2026-08-25

本文件只保存接手下一批工作所需的当前快照。已完成批次、旧故障和旧验收记录以 Git 历史、`docs/history/` 与本地 `tmp/device-runs/` 为准，不在这里累计。

## 权威来源

- 项目使命、DLL 边界、ABI 和所有权：`docs/ARCHITECTURE.md`
- 当前未解决边界：`.agents/KNOWN_LIMITATIONS.md`
- 未来工作及优先级：`.agents/ROADMAP.md`
- 失败路线及重试前提：`.agents/FAILED_EXPERIMENTS.md`
- agent 工作规则：根目录 `AGENTS.md` 与 `.agents/README.md`
- “当前”结论必须由 Git、源码、构建、测试和设备日志交叉验证。

## 当前仓库基线

- 分支：`main`；交付前后必须重新核对远端和工作区，不能沿用本文件中的 Git 结论。
- 当前能力批次：next646，页面 stylesheet `media`、disabled 与 rel-token 选择 → Core 将 `<style media>` 与
  `<link rel="stylesheet" media>` 的 UTF-8 条件交给 libcss，并在同一文档重排时复用外部
  CSS cache；收集外部 stylesheet link 时会跳过存在 `disabled` 属性的 link，不 fetch、解析或
  选择其 CSS；`rel` 按 ASCII whitespace token、大小写不敏感匹配 `stylesheet`，但含
  `alternate` 的 link 继续 fail closed；它沿用 next642 的 `relList.supports()` 保守 link-type 能力探测和
  next641 的 bounded DOMTokenList 反射/枚举/变更与
  next636 的 `rel` 属性桥，不扩展公共 C ABI，也不把 `noopener` 等关系词误称为窗口安全
  策略。`<link rel="stylesheet">` 是当前唯一报告为 supported 的关系词，未实现关系词和
  `a`/`area`/`form` 均 fail closed。next640 的 fragment-only `HTMLElement.click()`/物理锚点 → cancelable click/
  fragment history/hashchange + host-owned target scroll，并在同文档和跨文档 history
  back/forward/go 时恢复 bounded viewport；fragment token 按 id 优先兼容 `<a name>`，
  anchor 的 href/target/rel 现在由 Core 查询并随 browser anchor navigation 传给宿主
  （自动设备门已完成）。browser layer 还会把 raw target 分类为 bounded `target_kind`；
  当前单窗口宿主接受 default/`_self`/`_parent`/`_top`，对 `_blank`/未知或空 named 请求
  fail-closed；普通锚点的 named target 只有与活动 `window.name` 精确匹配时才复用当前 context。
  `window.open()` 现在只对显式 `_self`/`_parent`/`_top` 请求走当前文档导航并返回当前
  bounded global；对 named target 仅当名称与当前 `window.name` 精确匹配时复用当前
  context，DEFAULT/`_blank`/未知 named/空 URL 或 callback 注销返回 null。`window.name`
  在同一宿主的跨文档 script session 中有界恢复。跨页 href 仍为 ASSIGN；真实 `_blank`/
  named window 创建、第二个 global、窗口生命周期和跨窗口 history 尚未实现；next618 的
  TEST65 真实 SIP 候选词仍待人工确认，
  file picker/真实 label 触摸仍是独立人工边界。next644 又为 `<link>` 与 `<style>` wrapper
  增加了受限的 `media` UTF-8 属性反射；缺失返回空串，`setAttribute`/setter/
  `removeAttribute` 保持 live 一致，其他元素返回 `undefined` 且 setter 不改变 raw 属性。
  这不触发脚本侧 MediaQueryList 事件或自动重排。
- 测试编号上限：`TEST_MAX_NUMBER 1094`。
- 跟踪的 `test_host/test_host.ini` 保持默认自动模式：
  - `javascript=0`
  - 默认选择 `13,20,27,56,58,62,64-67,73,75,999`
- 最近一次全范围自动设备基线仍为 next255；其后的批次使用针对性门和相关回归门验证。

## 项目使命与当前里程碑

Positron 的目标是在 Windows Mobile 6 / Windows CE 设备上提供可嵌入、稳定 C ABI 的轻量应用与浏览器运行时。可发布能力必须归属于 `positron_core`、`positron_browser`、`positron_script` 等产品 DLL；`test_host.exe` 只负责回归编排、平台窗口、网络接入和示例消费。

当前中期里程碑仍是：在默认关闭 JavaScript 的安全基线不变的前提下，使浏览器会话中的
HTML、CSS、表单、DOM 与单一 Duktape 引擎组合成可预测、资源有界、可由产品 DLL 复用的
轻量 Web 运行时。

next606 是一次已完成的安全基础设施中断：把仅有互联网客户端能力的 `positron_tls.dll`
扩展为可供 LocalSend 一类消费者复用的 peer TLS ABI v2。next607–614 已恢复并继续浏览器
产品语义迁移；next613 只处理 native EDIT composition dispatch/default policy，不扩张 TLS
协议层。

## 已验证的产品状态

- 公共接口遵循稳定 C ABI、UTF-8、opaque handle 和明确内存所有权。
- 浏览器 JavaScript 与独立脚本 API 共用 `positron_script` 中的 Duktape，不存在第二套浏览器 JS 引擎。
- JavaScript 默认关闭；启用是显式的会话配置。
- 浏览器会话的脚本 heap 上限为 624 KiB；独立脚本会话默认上限为 512 KiB。
- next605 在产品侧增加了有界的 `RadioNodeList` 表单集合语义；`test_host` 仍只是消费者和断言宿主。
- next606 在 `positron_tls.dll` 产品侧增加了 ABI 查询、持久 ECDSA P-256 身份、DER SHA-256
  指纹、携带客户端证书并在返回前钉扎指纹的 peer connect、可选/强制客户端证书的 IPv4
  listener，以及安全错误快照；ABI v1 的九个导出均保留。
- peer TLS 和普通 CA/hostname HTTPS 是分离的信任模型；空 pin 只用于 discovery/TOFU，不
  表示认证。身份文件属于消费者持久状态，`test_host` 只创建隔离临时文件做回归。
- next607 在 `positron_browser.dll` 增加了 additive 的
  `PBrowser_ScriptSessionRegisterProgrammaticClickCallbacksEx()`：DLL 现在执行
  `HTMLElement.click()` 的 disabled 抑制、typed click、submit/reset 事件顺序、submit
  验证与取消策略；`test_host` 只提供 target lookup、validation、default action 和非表单
  click 传播，窗口、原生控件、设备网络等平台副作用继续由宿主持有。
- next608 在 `positron_browser.dll` 增加了 additive 的
  `PBrowser_ScriptSessionRegisterNativeEditCallbacksEx()` 与 native EDIT 事务入口：DLL
  持有 bounded beforeinput pending metadata、native commit 到 input 的转换、dirty 状态和
  blur/change 一次性顺序；`test_host` 只提供 WM EDIT 消息、文本值提交、几何和 core 事件
  propagation。该批不把 WM 控件、IME/SIP、焦点窗口或文本 mutation 搬入产品 DLL。
- next609 在 `positron_browser.dll` 增加了 additive 的
  `PBrowser_ScriptSessionRegisterNativeSelectCallbacksEx()` 与 commit/reset 入口：DLL
  持有 native SELECT commit 后不可取消的 input→change 顺序、single/multiple 形状校验和
  16-token bounded state；`test_host` 只提供 WM SELECT 键盘、Core selection mutation、
  几何和 core 事件传播。native SELECT 的 WM 默认动作、OEM SIP/IME composition 生命周期、
  系统 picker 和导航副作用仍需后续逐项迁移或人工验收，不能笼统宣称 native form/input 已完成。
- next610 在同一 native SELECT bounded state 上增加了
  `PBrowser_ScriptSessionDispatchNativeSelectFocus()`：browser layer 现在持有每个稳定
  token 的焦点状态，并按 `focus`→`focusin` 或 `blur`→`focusout` 成对同步派发，重复通知
  幂等、adapter 失败可重试；`test_host` 只提供 WM 焦点转换、控件几何、Core 交互和无脚本
  fallback。下拉展开/关闭、WM 控件真正的默认动作、SIP/IME composition 和 OEM 副作用仍由
  宿主持有。
- next611 在同一 native SELECT bounded state 上增加了
  `PBrowser_ScriptSessionDispatchNativeSelectInteraction()`：browser layer 现在记录单选
  下拉的 begin/candidate/confirm/cancel 事务，只有确认且观察到候选时才允许宿主提交
  `input`→`change`；取消会清空候选，宿主把原生 COMBOBOX 恢复到 Core 选中项。宿主仍拥有
  WM 通知、Core selection mutation、控件回滚和无脚本即时回退；多选、WM 控件真正的键盘
  默认动作、下拉视觉和 OEM SIP/IME composition 不在 next611 本批宣称范围。
- next612 在同一 native SELECT bounded state 上增加了
  `PBrowser_ScriptSessionDispatchNativeSelectKey()`：browser layer 校验稳定 token 与
  `keydown`/`keyup` phase，复用 typed key adapter 并返回 cancel/default-allowed 结果；宿主
  仍执行 WM 控件的真正 Enter/Arrow 默认动作、Core selection mutation、窗口和 OEM 副作用。
  TEST1060 覆盖 ArrowDown/Enter 元数据、取消、adapter error、reset 和 unregister；TEST118
  在真实 WM6 页面上断言允许的 ArrowDown 同时改变 COMBOBOX 与 Core selection。
- next613 在 native EDIT 的 bounded state 上增加了
  `PBrowser_ScriptSessionDispatchNativeEditComposition()`：browser layer 校验稳定 token 与
  START/UPDATE/END phase，保存不超过 255 字节的最后 preedit，按
  `compositionstart` → `beforeinput(insertCompositionText)` → `compositionupdate` →
  `compositionend` 顺序调用既有 input callback，并把 UPDATE 的 pending metadata 接入
  next608 的 native commit→input 事务。宿主只提供 WM_IME/SIP phase、借用数据、原生文本
  mutation 和平台副作用；TEST1061、123–125 的自动设备门已通过。OEM 候选词整词提交和
  SIP 视觉仍然是人工风险，不能由该入口宣称兼容。
- next614 在现有只读 DOM relation bridge 上增加了 bounded label/control 关联：
  `label.control` 支持显式 `for` 和嵌套的 input/select/textarea/button，labelable 控件的
  `labels` 返回按文档顺序的静态 NodeList snapshot；input type=hidden、非控件目标、无效
  `for` 和越界索引 fail closed。`positron_core.dll` 只提供关系查询，`positron_browser.dll`
  负责脚本属性和集合形状，`test_host` 只消费公共 bridge；没有宣称完整 live labels 或其他
  成功控件边缘规则。
- next615 在 `positron_core.dll` 增加了统一的有效禁用判定：支持 disabled ancestor fieldset、
  第一个 legend 后代豁免和嵌套 fieldset，并把它接入约束验证、URL-encoded/multipart 成功控件、
  默认 submitter、表单控件信息、程序化激活及交互闸门。原始 HTML `control.disabled` 仍只反映
  自身属性；native 控件的窗口样式、真实 SIP/IME 和文件选择器仍由宿主拥有。
- next616 在 `test_host` 的网络/窗口边界统一 HTTP(S) URL 解析：主文档导航与 CSS/图片等
  子资源都经过 WinINet 的目录相对、`.`/`..`、query-only、network-path 和绝对 URL 解析，
  再由宿主做严格的 scheme、authority、端口和路径校验并去除 fragment。产品 DLL 的 URL/
  history ABI 没有扩张；不支持的 scheme、无 origin 的普通相对引用、userinfo、IPv6 和非法
  端口 fail closed。TEST1064 是离线契约门。
- next617 在 `positron_http.dll` 增加 additive 的 `PHttp_ResolveReference`，把目录相对、
  点段、query-only、network-path、绝对 HTTP(S)、fragment stripping 以及 authority/端口
  fail-closed 语义放到产品 DLL。主文档导航、CSS/图片资源和 HTTP GET 的 3xx `Location`
  现在共用该 resolver；`test_host` 只提供 origin、窗口/网络副作用和公共 API 消费，不再
  复制 URL 业务规则。TEST1064 保留宿主回归，TEST1065 验证产品契约。
- next618 保持 next613 的 browser-owned composition 生命周期不变，修正宿主 WM6 IME
  `GCS_RESULTSTR` 的平台落地：完整 UTF-8 候选词先进入当前 native EDIT composition
  selection，再沿既有 `EN_CHANGE` → Core value → browser input 路径提交，不再依赖某些
  WinCE EDIT 默认过程只生成的首个 `WM_CHAR`。TEST1066 覆盖多字节多字符候选的完整值；
  OEM SIP 候选窗口、视觉和真实设备输入仍需单独人工确认。
- next619 在 `positron_browser.dll` 增加 additive 的
  `PBrowser_ScriptSessionDispatchNativeEditResult()`：browser layer 校验活动 composition
  和有界 UTF-8 result，派发 `beforeinput(insertCompositionText)` → `compositionupdate`，
  并将 result metadata 接入既有 native commit → input 事务；宿主仍拥有
  `ImmGetCompositionStringW`、`EM_REPLACESEL`、WM_IME/SIP 和原生文本 mutation。
  TEST1067 覆盖生命周期、容量、完整多字节 result、commit、reset 和 unregister。
- next620 在 `positron_browser.dll` 增加 additive 的
  `PBrowser_ScriptSessionDispatchNativeFileSelection()` 与 reset 入口：browser layer
  持有每个 session 最多 16 个 stable token 的 BEGIN/COMMIT/CANCEL 状态，并在 COMMIT
  时一次性派发 `input(insertFromFile)` → `change`。`test_host` 仍拥有
  `GetOpenFileNameEx`、文件系统、权限、`PCore_FileInputSetPath`、重绘和取消回滚；
  没有活动脚本 session 时保留旧 fallback。TEST1068 是产品契约门，TEST262 是宿主
  文件输入回归；TEST232/263 的真实 picker 和视觉仍需人工验收。
- next621 在 `positron_browser.dll` 增加 additive 的
  `PBrowser_ScriptSessionDispatchNativeFilePicker()` 与 reset 入口：每个 session 只保留
  一个 pending/active picker request，REQUEST 合并重复 `file.click()`，OPEN/CLOSE/CANCEL
  校验 stable token 并清理状态。宿主仍拥有 `PostMessage`、HWND/document/index、系统
  picker、文件系统和路径写入；TEST1069 是产品契约门，TEST262 是宿主消费者回归。
- next622 在 `positron_browser.dll` 增加 additive 的
  `PBrowser_ScriptSessionDispatchAnchorClick()`：browser layer 对宿主命中的受信任
  href 先派发一次可取消 click，只有未被阻止时才通过已注册导航适配器提交 ASSIGN。宿主
  仍拥有 `PCore_LinkAt`、网络、窗口和文档生命周期；TEST1070 是产品契约及 helper
  消费者回归。
- next623 在 `positron_browser.dll` 增加 additive 的
  `PBrowser_ScriptSessionDispatchNativeToggle()` 与 reset：browser layer 对受信任
  checkbox/radio 激活持有最多 16 个 stable token 的 CLICK/COMMIT/CANCEL 状态，先派发
  可取消 click，只有宿主报告 Core checked-state 变化已提交后才派发一次 input→change。
  宿主仍拥有 hit-test、Core mutation、WM 默认动作、label fallback 和重绘；TEST1071 是
  产品契约及 helper 消费者回归。
- next624 在 `positron_browser.dll` 增加 additive 的
  `PBrowser_ScriptSessionDispatchNativeButton()` 与 reset：browser layer 对受信任
  submit/reset 原生按钮持有最多 16 个 stable token 的 CLICK/COMMIT/CANCEL 状态，先派发
  可取消 click；宿主在 click 之后查询 Core validation，再由 COMMIT 派发 submit 或 reset，
  只有事件未取消时才允许默认动作。宿主仍拥有 hit-test、Core validation/default action、
  导航、窗口和重绘；TEST1072 是产品契约及 helper 消费者回归。
- next625 扩展同一 `PBrowser_ScriptSessionDispatchNativeButton()` ABI，支持普通
  `<button type="button">`：browser layer 仍持有可取消 click 与 bounded COMMIT，普通按钮
  不派发 submit/reset；宿主把已接受的普通按钮默认动作消费掉，不再落入关闭窗口 fallback。
  `test_host` 仍拥有 hit-test、Core/WM 副作用和重绘；TEST1073 是产品契约及 helper 回归。
- next626 在 `test_host` 为 enabled 的 Core button 增加 document/index/kind 焦点状态和
  WM 键盘路由：Enter 在 keydown、Space 在 keyup 复用同一 CLICK→COMMIT 事务，重复 keydown
  只保留脚本可观察性而不重复激活；browser DLL 继续拥有 click/form 取消和事件顺序。TEST1074
  以实际 render window 消息队列覆盖 ordinary、submit、reset、取消、禁用焦点和窗口不误关闭。
- next627 在 `test_host` 把 label 命中的 native button 转入既有
  `PBrowser_ScriptSessionDispatchNativeButton()` CLICK→COMMIT：label 自身 click 先按物理
  命中派发，ordinary、submit、reset 目标再复用 browser-owned click/form 取消与事件顺序；
  宿主只执行已接受的 Core 默认动作，stale/disabled target 不合成 click，也不关闭窗口。
  TEST1075 覆盖普通、submit、reset、取消、disabled 和 reset 值恢复。
- next628 在 `test_host` 把 label 命中的 checkbox/radio 转入既有
  `PBrowser_ScriptSessionDispatchNativeToggle()` CLICK→COMMIT：label 自身 click 先按物理
  命中派发，目标 control 再由 Core 执行 checked/radio mutation；browser layer 继续拥有目标
  click 取消和一次 `input` → `change`，stale/disabled target 不合成 click。TEST1076 覆盖
  checkbox、radio 互斥、preventDefault 和 disabled 静默。
- next629 在 `test_host` 把 label 命中的 text/password/textarea/select/file target
  先交给既有 browser click adapter；只有目标 click 未取消且目标有效时，宿主才聚焦
  native EDIT/SELECT 或进入系统 file picker。disabled/stale target 不合成目标 click，
  无脚本 fallback 保持不变。TEST1077 覆盖 text/textarea/select 的真实 render-window
  click、focus/focusin、select 取消和 disabled 静默；file picker 模态框与真实触摸仍是
  独立人工边界。
- next630 扩展既有 programmatic-click target-kind/default-action 常量，使
  `positron_browser.dll` 对 text/password/textarea/select 的 `HTMLElement.click()` 统一执行
  disabled 抑制、typed click 和取消；`test_host` 以同步 DOM id 进行目标事件传播，并只在
  default callback 中执行真实 native EDIT/SELECT focus。`TEST1078` 已验证 text、password、
  textarea、select 的 click/focus/focusin 顺序、select 取消和 disabled 静默；select popup、
  picker、触摸、SIP/IME、焦点视觉和 OEM 行为仍不在自动保证内。
- next631 增加独立的 programmatic-anchor callback，不改变 next607–630 的 form-click ABI：
  `positron_browser.dll` 对带 href 的 `HTMLElement.click()` 复用 cancelable click 与 ASSIGN
  navigation，`positron_core.dll` 以 `PCore_LinkInfoById()` 按 DOM id 提供已布局几何和 UTF-8
  href；未知/无 href 元素回到 generic click，网络、窗口替换和文档生命周期仍由宿主拥有。
  `TEST1079` 已覆盖接受导航、`preventDefault()`、容量/缺失和无 href 边界；TEST1070 继续
  覆盖导航适配器拒绝。
- next632 在同一锚点路径上把以 `#` 开头的 href 分类为
  `PBROWSER_SCRIPT_NAVIGATION_FRAGMENT`；宿主 adapter 绑定当前 history URL，使用
  Core 的片段查询读取目标几何并滚动自己的 viewport，未知 token 保持位置且不发起网络请求。
  跨页 href 仍走 ASSIGN；target/rel/window 和真实页面视觉未覆盖。TEST1080 已在 WM6 设备门
  覆盖分类、URL 绑定、target geometry、滚动和 unknown-id 边界。
- next633 补齐同文档 fragment history traversal 的宿主滚动恢复：`back()`、`forward()` 和
  `go()` 在 browser-owned history traversal 成功后复用 `PCore_FragmentInfoById()`，把当前
  viewport 移到目标；未知目标保持原位置，不触发网络或文档替换。`positron_browser.dll`
  的 history/event ABI 未改变，跨文档 traversal 仍由既有导航路径处理。TEST1081 已在 WM6
  设备门覆盖点击后目标位置、back/forward、unknown-id 和无网络边界。
- next634 在同一宿主 history mirror 中保存每个跨文档条目的 scroll offset：导航离开前保存，
  目标文档布局完成后由 back/forward/go 恢复，new entry 默认零，短文档按 viewport 上限裁剪。
  `positron_browser.dll` 公共 ABI 未扩张；TEST1082 与 TEST1081、1080、1079、1070、999
  的 Debug 窄门已通过 6/6。
- next635 在 `positron_core.dll` 增加 additive 的 `PCore_FragmentInfoByToken()`：先按 literal
  UTF-8 id 匹配，再兼容 HTML 旧式 `<a name>`；`test_host` 在查询前做有界 `%HH` 字节解码，
  `+` 保持字面，malformed/NUL/unknown token 保持滚动。TEST1083 与 1082–1070、999 的
  Debug 窄门已通过 7/7；browser history ABI 未扩张。
- next636 在 `positron_core.dll` 增加 `PCore_LinkAtEx()`/
  `PCore_LinkInfoByIdEx()` 的 href/target/rel 有界查询，在 `positron_browser.dll` 增加
  `PBrowser_ScriptSessionDispatchAnchorClickEx()` 并沿既有 programmatic anchor 与
  navigation info 传播元数据；bootstrap 增加 `HTMLElement.rel` 反射。TEST1084 与
  1079–1083、999 的 Debug 窄门通过 7/7，累积回归门通过 22/22；旧 anchor 入口保留，
  窗口创建和 target policy 仍由宿主负责。
- next637 在 `PBrowserScriptNavigationInfo` 追加兼容的 `target_kind`，由
  `positron_browser.dll` 对 raw target 统一分类；单窗口 `test_host` 在 navigation adapter
  与窗口消息边界只接受当前上下文策略，对 `_blank`/named 请求 fail-closed。TEST1085 与
  1079–1084、999 的 Debug 窄门通过 8/8，未改变旧 anchor ABI。
- next641 在 `positron_browser.dll` bootstrap 中补齐受限的 anchor/area/link/form `relList`：
  稳定 wrapper 提供 `length`/`item`/`value`、ASCII 大小写不敏感的 unique token 读取、
  `contains`/`add`/`remove`/`toggle`/`replace`、`forEach` 与 iterator；变更实时反映到
  `rel`，空 token/含空白 token fail-closed。该批不新增 C ABI，不实现 `supports()`、完整
  link-type processing 或窗口安全策略。TEST1089 只消费现有属性 bridge。

## 最近验证证据

next606 已完成与风险相称的本地和设备验证：

- C89 检查、正式 Debug/Release ARMV4I 构建和仓库/文档审计通过；`positron_tls` 在两种
  配置均为 0 error/0 warning。Release DLL 为 379,392 bytes（ABI v1 基线为 340,992），
  导出表为 19 个未修饰 C 符号，旧九个入口全部保留。
- peer 定向设备门：
  `tmp/device-runs/20260823-160330-next606-tls-peer-r4/device-gate-result.txt`
  — PASS，2/2（TEST1054 与 TEST999），错误与失败均为 0，唯一 PASS，路由正确。
- ABI v1、HTTP 和证书兼容门：
  `tmp/device-runs/20260823-155644-next606-tls-peer-compat-r1/device-gate-result.txt`
  — PASS，7/7（TEST1–5、1054、999），包含真实 HTTPS GET/POST、有效证书接受、过期和
  自签名证书拒绝。
- TEST1054 覆盖身份生成/重载/错配、指纹、双向证书、pin 成败、可选/强制客户端证书、
  失败恢复、并发连接和 listener close 取消，不依赖人工判断。
- 官方 LocalSend 当前使用 rustls 0.23.43 `ring`+`tls12`；源码核对确认其默认 provider 与
  Positron 共享 ECDHE-ECDSA/P-256/SHA-256/AES-128-GCM suite。尚未运行实际 rustls↔WM6
  双向互操作，因此该项只标记为配置兼容，不标记为消费者端到端通过。
- 首次设备候选暴露 WinCE Winsock 对 `SO_RCVTIMEO/SO_SNDTIMEO` 返回 WSA10042；正式实现
  改为非阻塞 socket + `select` 控制 connect/handshake 期限后重跑通过，没有放宽断言。
- 本批没有视觉、真实触摸、SIP、旋转或 picker 风险，不新增人工验收门。

next607 的同步脚本/表单自动门：

- `tmp/device-runs/20260823-165349-next607-programmatic-click-r2/device-gate-result.txt`
  — PASS，5/5（TEST228–230、1055、999），错误与失败均为 0，唯一 PASS，路由正确。
- TEST228–230 验证真实 checkbox/radio、submit/reset 和 file-input 程序化 click 集成；
  TEST1055 验证 Ex ABI 的 toggle、valid/invalid submit、form-event/click 取消、disabled
  静默和 generic fallback 顺序。
- C89 检查、Debug/Release ARMV4I 正式构建、仓库/文档审计均通过；tracked INI 未修改，
  仍为 `javascript=0` 的窄 smoke 选择。本批只涉及同步语义，不新增人工页面门。

next608 的 native EDIT 自动门：

- `tmp/device-runs/20260823-172005-next608-native-edit-r2/device-gate-result.txt`
  — PASS，2/2（TEST1056、999），错误与失败均为 0，唯一 PASS，路由正确。
- `tmp/device-runs/20260823-172030-next608-native-edit-regression/device-gate-result.txt`
  — PASS，6/6（TEST228–230、1055–1056、999），错误与失败均为 0，唯一 PASS，路由正确。
- TEST1056 覆盖 beforeinput 接受/取消、pending input metadata、native commit、dirty/blur
  change、重复 blur、显式 commit fallback、state reset 和 unregister；设备日志还证明
  browser DLL/host 在真实 WM6 上可启动并完整退出。
- C89 检查、Debug/Release 正式 ARMV4I 构建、仓库/文档审计和相关设备门均已通过。tracked
  INI 未修改，仍为 `javascript=0` 的窄 smoke 选择。本批没有新增视觉、
  真实触摸、旋转、picker 或 OEM SIP/IME 人工门。

next609 的 native SELECT 自动门：

- `tmp/device-runs/20260823-174421-next609-native-select-r1/device-gate-result.txt`
  — PASS，5/5（TEST67、71、118、1057、999），错误与失败均为 0，唯一 PASS，路由正确。
- TEST1057 覆盖 Ex 注册/重复注册、非法 single 快照、input→change 顺序、single/multiple
  形状冲突、adapter error、reset 和 unregister；TEST67/71/118 在真实 WM6 控件上覆盖单选、
  多选和键盘路径，证明宿主 WM/Core 适配仍能启动并完整退出。
- C89 检查、Debug/Release 正式 ARMV4I 构建、仓库/文档审计和相关设备门均已通过。tracked
  INI 未修改，仍为 `javascript=0` 的窄 smoke 选择。本批没有新增视觉、
  真实触摸、旋转、picker 或 OEM SIP/IME 人工门。

next610 的 native SELECT 焦点族自动门：

- `tmp/device-runs/20260823-181515-next610-native-select-focus-final/device-gate-result.txt`
  — PASS，6/6（TEST67、71、118、1057、1058、999），错误与失败均为 0，唯一 PASS，路由正确。
- TEST1058 覆盖新入口的非法 focused 值、focus/focusin 与 blur/focusout 顺序、重复通知幂等、
  adapter 失败后的重试、多个 token、reset 和 unregister；TEST67/71 在真实 WM6 控件上覆盖
  单选、多选、重建和退出路径。该批只迁移焦点族策略与状态，不宣称下拉视觉、键盘默认动作
  或 OEM SIP/IME 兼容。
- `python scripts/test_c89ize.py`、Debug/Release 正式 ARMV4I 构建和仓库/文档审计均已在本批
  最终工作区通过；tracked INI 仍未修改，保持 `javascript=0` 的窄 smoke 选择。

next611 的 native SELECT 单选下拉事务自动门：

- `tmp/device-runs/20260823-184446-next611-native-select-transaction-final-r2/device-gate-result.txt`：
  PASS，7/7（TEST67、71、118、1057、1058、1059、999），错误与失败均为 0，唯一 PASS，
  `test13_route_ok=True`；RAPI 不暴露远程退出码，但完成标记和选定测试集合均匹配。
- TEST1059 覆盖 interaction ABI 的非法输入、begin/candidate 抑制、确认后 commit、取消、
  无候选确认、reset 和 unregister；TEST67 的真实 WM COMBOBOX 探针覆盖 Core 不提前变化、
  取消回滚和确认提交。该批没有新增视觉或人工门。
- `python scripts/test_c89ize.py`、Debug/Release 正式 ARMV4I 构建和 `python scripts/audit_repo.py`
  均通过；Release 与 Debug 保留既有 libcss/fpmath 的 3 个 C4244 警告，产品 DLL 无新增警告。

next612 的 native SELECT 键盘默认动作自动门：

- `tmp/device-runs/20260823-190158-next612-native-select-key-final/device-gate-result.txt`：
  PASS，3/3（TEST1060、TEST118、TEST999），错误与失败均为 0，唯一 `TESTBENCH PASS`，
  `test13_route_ok=True`；RAPI 不暴露远程退出码，但完成标记和选定测试集合均匹配。
- TEST1060 覆盖 browser-owned key 入口的稳定 token、keydown/keyup phase、ArrowDown/Enter
  元数据、取消、adapter error、reset 和 unregister；TEST118 在真实 WM6 页面上验证
  `keydown` 未取消时原生 COMBOBOX 的 ArrowDown 默认动作同步更新 Core selection。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建和
  `python scripts/audit_repo.py` 均通过；Release 与 Debug 保留既有 libcss/fpmath 的 3 个
  C4244 警告，产品 DLL 无新增警告。tracked INI 仍保持 `javascript=0` 的窄 smoke 选择，
  本批没有新增视觉、触摸、旋转、picker 或 OEM SIP/IME 人工门。

next613 的 native EDIT composition 自动门：

- `tmp/device-runs/20260823-195411-next613-native-edit-composition-final-r3/device-gate-result.txt`：
  PASS，5/5（TEST1061、TEST123–125、TEST999），错误与失败均为 0，唯一 `TESTBENCH PASS`，
  `test13_route_ok=True`；RAPI 不暴露远程退出码，但完成标记和选定测试集合均匹配。
- TEST1061 覆盖 composition start/update/end 顺序、START 取消、重复 START、隐式/显式 END、
  adapter 失败重试、reset 和 unregister；TEST123–125 在真实 WM6 上继续证明
  composition/InputEvent/KeyboardEvent 元数据和原生 commit→input 数据没有回归。
- 中途候选运行暴露了 UPDATE 未接入 pending input metadata，以及新增边界断言的测试状态复位
  错误；修订后未放宽断言并在 final-r3 通过。TEST65 的实际 SIP 候选词整词提交仍需人工验收，
  不能由该自动门替代。

next614 的 label/control 关联自动门：

- 当前源码最终窄门 `tmp/device-runs/20260823-203232-next614-label-control-final/device-gate-result.txt`：
  PASS，2/2（TEST1062、TEST999），错误与失败均为 0，唯一 `TESTBENCH PASS`，
  `test13_route_ok=True`。
- `tmp/device-runs/20260823-201802-next614-label-control-r4/device-gate-result.txt`：PASS，
  2/2（TEST1062、TEST999），错误与失败均为 0，唯一 `TESTBENCH PASS`，
  `test13_route_ok=True`。
- `tmp/device-runs/20260823-201832-next614-label-control-regression/device-gate-result.txt`：
  PASS，41/41（TEST554–561、TEST1023–1053、TEST1062、TEST999），错误与失败均为 0，
  唯一 `TESTBENCH PASS`，`test13_route_ok=True`。
- TEST1062 同时验证 `PCore_NodeRelationById` 的显式/嵌套 control、文档顺序 labels、越界、
  hidden/non-control fail-closed，以及 browser JavaScript 的 `control`、静态 `labels` snapshot
  和 wrapper 形状。中途候选发现 JSON relation 层遗漏新 count 关系，修复后重跑通过；没有放宽
  断言。该批没有视觉、触摸、SIP、旋转、picker 或网络失败人工门。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建和 `python scripts/audit_repo.py`
  均通过；Release 与 Debug 仅保留既有 libcss/fpmath 的 3 个 C4244 警告，产品 DLL 无新增警告。

next615 的 fieldset 有效禁用自动门：

- `tmp/device-runs/20260823-210420-next615-fieldset-disabled-final/device-gate-result.txt`：
  PASS，2/2（TEST1063、TEST999），错误与失败均为 0，唯一 `TESTBENCH PASS`，
  `test13_route_ok=True`。
- `tmp/device-runs/20260823-210451-next615-fieldset-disabled-regression/device-gate-result.txt`：
  PASS，18/18（TEST264–270、554–561、1062–1063、999），错误与失败均为 0，唯一
  `TESTBENCH PASS`，`test13_route_ok=True`。
- TEST1063 覆盖 disabled fieldset 的第一个 legend 豁免、第二个 legend/嵌套 fieldset 的继承、
  动态 `fieldset.disabled` 切换、原始 `control.disabled` 与 `willValidate` 的区分、控件信息和
  successful form submission。该批没有视觉、真实触摸、SIP、旋转、picker 或网络失败风险，
  不新增人工门。
- `python scripts/test_c89ize.py`、Debug/Release 正式 ARMV4I 构建、`python scripts/audit_repo.py`
  和文档审计均已通过；tracked INI 未修改，仍保持自动模式的窄 smoke 选择。

next616 的宿主 URL 解析候选当前状态：

- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式工程构建、Release 增量构建和
  `python scripts/audit_repo.py` 已通过；新增 TEST1064 覆盖目录相对、`.`/`..`、query-only、
  network-path、绝对 URL、空 href、fragment 去除以及 unsupported/no-origin fail-closed。
- 按既有设备门基线使用 Debug 配置后，`tmp/device-runs/20260823-222112-next616-beep-debug/`
  的 TEST999 通过，`tmp/device-runs/20260823-222153-next616-url-resolution-debug/` 的
  TEST43、TEST1064、TEST999 通过，均为唯一 `TESTBENCH PASS` 且无 ERROR/FAIL。
- 首轮真实页面门 `tmp/device-runs/20260823-213537-next616-url-resolution-final/` 在
  `13,43,1064,999` 的 1200 秒内只有启动头；RAPI 按安全契约没有远程强杀遗留进程。
- 该首轮使用 Release payload；其后的 Release 探针也只写启动头，而 Debug payload 正常通过，
  因此本批设备证据以项目既有 Debug gate 为准，Release 停滞保留为配置/设备兼容观察，不写成
  URL 断言失败。当前真实页面门 `tmp/device-runs/20260823-222224-next616-url-resolution-final-debug/`
  已完成 example.com 第一跳，第二跳 IANA 仍受外网响应影响，尚未形成最终 PASS。
- 离线门已满足交付标准；若需要继续网络取证，先确认设备连接和外网可达，不得把 IANA 超时
  混入离线 URL 契约。详见 `.agents/FAILED_EXPERIMENTS.md` 的 next616 条目。

next617 的 HTTP reference/Location 产品解析已完成：

- `python scripts/test_c89ize.py`、Debug ARMV4I 全量正式重建 16/16、Release 增量构建和
  `python scripts/audit_repo.py` 均通过；修改的 `positron_http` 与 `test_host` 在两种配置
  均为 0 error/0 warning。
- 定向设备门 `tmp/device-runs/20260823-232147-next617-http-reference-contract-r5/`
  的 `device-gate-result.txt` 为 PASS，TEST1065 与 TEST999 通过 2/2，唯一
  `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。日志证明
  成功、失败输出清零、容量边界和最终系统提示音均已执行；本批无人工视觉或输入门。
- r1/r2 的 RAPI 失败是重连前远端关闭（Win32 10101），r3/r4 已完成部署并暴露出产品
  解析器的失败输出清理缺口；修复后 r5 通过，不能把中间候选当作最终设备证据。

next618 当前候选的本地验证和自动设备门已经完成，尚余一项真实 SIP 人工门：

- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式重建和本批源码检查均通过；
  Release 全量重建为 16/16，test_host 为 0 error/3 个既有警告。
- 首次窄门在 `CeCreateDirectory(\Temp)` 收到 WinSock 10101，属于当时连接被远端关闭；
  但随后反复出现的 `CeRapiInitEx()` 30 秒超时不是单纯主机故障。对照微软 API 契约和
  `d2e33d42` 的变更后确认：gate 错把 `RAPIINIT.heRapiInit` 当成调用者输入，自建事件并
  等待/关闭；该成员实际是 `CeRapiInitEx()` 返回的完成事件。错误探针还与 Application log
  中 `WcesComm`/`RapiMgr` 的 `0xc0000008` 无效句柄崩溃逐次对应。此前“只需恢复主机”的
  归因已被这项证据替代。
- gate 已改为等待 API 写回的 `heRapiInit`，并保留 30 秒超时和 `CeRapiUninit()` 清理；
  它仍只消费用户在 GUI 中建立的当前唯一连接，不连接、选择、Cradle、断开或重置设备。
- 修复后的最小证据
  `tmp/device-runs/20260824-105452-next618-rapi-returned-event-probe/` 为 TEST999 1/1 PASS；
  完整窄门
  `tmp/device-runs/20260824-105511-next618-native-ime-result-final-fixed/` 为
  TEST1066、123–125、999 共 5/5 PASS，唯一 `TESTBENCH PASS`，`error_count=0`、
  `fail_count=0`、`test13_route_ok=True`。修复后的两次 gate 期间没有新增 RAPI 服务崩溃。
- 提权重试和 10 个已知 32/64 位 COM 注册值核对分别证明本次故障不是调用者权限，也不是
  `0x8007007E` 注册路径问题；不要为相同症状运行注册修复或管理 WMDC 进程。
- next618 的自动完成条件已经满足；晋升为完成项前只需在同一构建的 TEST65 中人工点选一个
  多字符 SIP 候选词，确认输入框一次出现完整候选词，并核对密码、readonly、disabled、
  maxlength 等相邻行为未退化。

next619 的产品事务自动门已经完成：

- `tmp/device-runs/20260824-110948-next619-native-ime-result-transaction/` 的
  `device-gate-result.txt` 为 PASS，TEST1067、1066、123–125、999 共 6/6，唯一
  `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、`python scripts/audit_repo.py`
  和文档审计均通过；Release/Debug 保留既有 libcss/fpmath 的 3 个 C4244 警告，产品 DLL
  无新增警告。
- 本批只新增 browser-owned result transaction ABI 和宿主消费者接线，不扩大默认 INI，
  不新增视觉、触摸、旋转、picker 或 OEM SIP 人工门；next618 的 TEST65 人工门仍独立存在。

`tmp/` 不跟踪，以上路径只用于本机证据定位；长期可追溯结论必须落在提交、源码和跟踪文档中。

next620 的文件输入选择产品事务自动门已经完成：

- `tmp/device-runs/20260824-112810-next620-native-file-selection-transaction/` 的
  `device-gate-result.txt` 为 PASS，TEST1068、262、230、999 共 4/4，唯一
  `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。
- TEST1068 覆盖 BEGIN/COMMIT/CANCEL 生命周期、非法 phase、重复提交、幂等取消、callback
  错误、reset/unregister 和 16-token 容量边界；TEST262 覆盖宿主 picker 的成功/失败
  路径接线。产品 DLL 不拥有系统 picker、路径写入或视觉，TEST232/263 仍是人工门。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建和
  `python scripts/audit_repo.py` 均通过；Release/Debug 仅保留既有 libcss/fpmath 的 3 个
  C4244 警告，产品 DLL 无新增警告。

next621 的 programmatic file-picker request arbitration 自动门已经完成：

- `tmp/device-runs/20260824-120418-next621-file-picker-arbitration/` 的
  `device-gate-result.txt` 为 PASS，TEST1069、262、230、999 共 4/4，唯一
  `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。
- TEST1069 覆盖 REQUEST coalescing、OPEN/CLOSE/CANCEL 生命周期、错误 token、重复 OPEN、
  reset 和多 session 隔离；TEST262 证明宿主在 queue 前 REQUEST、modal 前 OPEN、返回后
  CLOSE，stale/cancel 会清理产品状态。系统 picker、文件路径和视觉仍由宿主拥有。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建和
  `python scripts/audit_repo.py` 均通过；Release/Debug 仅保留既有 libcss/fpmath 的 3 个
  C4244 警告，产品 DLL 无新增警告。

next622 的 trusted physical anchor activation 自动门已经完成：

- `tmp/device-runs/20260824-122629-next622-anchor-activation-r2/` 的
  `device-gate-result.txt` 为 PASS，TEST1070、230、262、999 共 4/4，唯一
  `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。
- TEST1070 覆盖 click 接受、preventDefault、导航适配器拒绝、适配器错误，以及宿主 helper
  使用共享 session 的消费者接线。真实点击坐标、链接命中、target/rel/window 和页面视觉
  仍不由该自动门保证。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、
  `python scripts/audit_repo.py` 和窄设备门均已通过；Release/Debug 仅保留既有
  libcss/fpmath 的 3 个 C4244 警告，产品 DLL 无新增警告。

next625 的 trusted native ordinary button activation 自动门已经完成：

- `tmp/device-runs/20260824-133502-next625-native-button-r3/` 的
  `device-gate-result.txt` 为 PASS，TEST1073、1072、64、73、999 共 5/5，唯一
  `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。
- TEST1073 覆盖 ordinary button 的 click→commit、无 form 事件、无 form callback、click 取消、
  禁用、非法 kind 和共享 session helper；普通按钮的真实触摸视觉、键盘焦点/激活和 label 转发仍未由
  该自动门保证。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、`python scripts/audit_repo.py`
  和相关回归设备门均已通过。

next626 的 trusted native button keyboard activation 自动门已经完成：

- `tmp/device-runs/20260824-135909-next626-native-button-keyboard-r4/` 的
  `device-gate-result.txt` 为 PASS，TEST1074、1073、1072、64、73、999 共 6/6，唯一
  `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。
- TEST1074 使用实际 render window 消息队列覆盖 ordinary/submit/reset 的 Enter/Space、
  重复 keydown、keydown 取消、禁用焦点和激活后不误关闭窗口；这是消息/事件契约门，不把
  OEM 键盘映射、焦点视觉、真实触摸或 label 完整事务写成已验证事实。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、`python scripts/audit_repo.py`
  和窄设备门均已通过；Release/Debug 仅保留既有 libcss/fpmath 的 3 个 C4244 警告，产品
  DLL 无新增警告。

next627 的 trusted label→native button activation 自动门已经完成：

- `tmp/device-runs/20260824-141126-next627-label-button-r2/` 的
  `device-gate-result.txt` 为 PASS，TEST1075、1074、1073、1072、999 共 5/5，唯一
  `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。
- TEST1075 覆盖 label 自身 click 后 ordinary/submit/reset 目标的 browser-owned CLICK→COMMIT、
  submit/reset 事件顺序、reset 值恢复、button click 取消和 disabled 静默；首次候选暴露
  label 几何中心不一定是可命中点，最终实现沿用 bounded label-box 探测并保持严格事件断言。
  真实 label 触摸坐标、焦点视觉、OEM 按键映射和其他 labelable 控件仍未由该自动门证明。
- `python scripts/test_c89ize.py` 与 Debug 正式 ARMV4I 构建已通过；提交前必须补跑
  Release 构建、`python scripts/audit_repo.py` 和同一窄设备门，保持既有 3 个 libcss/fpmath
  C4244 警告基线，产品 DLL 不得新增警告。

next628 的 trusted label→native toggle activation 自动设备门已经完成：

- `tmp/device-runs/20260824-142420-next628-label-toggle-r1/` 的
  `device-gate-result.txt` 为 PASS，TEST1076、1075、1074、1071、64、73、999 共 7/7，唯一
  `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。
- TEST1076 覆盖 label→checkbox 的 click/input/change 顺序、radio 目标的 preventDefault
  抑制和后续互斥提交，以及 disabled label 的静默；它证明产品/宿主事件事务，不证明真实
  label 触摸坐标、焦点视觉、OEM 行为或其他 labelable 控件。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、`python scripts/audit_repo.py`
  和同一窄设备门均已通过；Release/Debug 仅保留既有 libcss/fpmath 的 3 个 C4244 警告，
  产品 DLL 无新增警告。

next629 的 trusted label→native text/select forwarding 自动设备门已经完成：

- `tmp/device-runs/20260824-145252-next629-label-native-r9/` 的
  `device-gate-result.txt` 为 PASS，TEST1077、1076、1075、1074、1073、1072、1071、64、
  73、999 共 10/10，唯一 `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、
  `test13_route_ok=True`。
- TEST1077 覆盖 label→text/textarea/select 的 browser-owned target click、接受后的
  focus/focusin、select `preventDefault`、disabled target 静默；没有把系统 file picker
  的模态框、路径提交、真实 label 触摸、SIP/IME 或 OEM 焦点视觉写成已验证保证。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、`python scripts/audit_repo.py`
  和窄设备门均已通过；Release/Debug 仅保留既有 libcss/fpmath 的 3 个 C4244 警告，产品
  DLL 无新增警告。首次实验的旧远端目录仍作为本地诊断证据保留，r9 使用独立远端目录完成
  验证，不应删除或带入 Git。

next630 的 programmatic native focus 自动门已经完成：

- `tmp/device-runs/20260824-162638-next630-programmatic-focus-r15/` 的窄门为 PASS，
  TEST1078、229、999 共 3/3，唯一 `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、
  `test13_route_ok=True`。
- `tmp/device-runs/20260824-162751-next630-programmatic-focus-r16/` 的相关累计门为
  PASS，TEST1078、1077、1076、1075、1074、1073、1072、1071、64、73、999 共 11/11，
  唯一 `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。
- TEST1078 在真实 render window 子窗口上覆盖 script `HTMLElement.click()` 的
  text/password/textarea/select typed click、focus/focusin、select cancel 和 disabled no-op；
  select popup、系统 picker、真实触摸、SIP/IME、焦点视觉和 OEM 副作用仍不由自动门保证。
- `python scripts/test_c89ize.py`、Debug 增量构建和相关设备门已通过；提交前仍需完成
  Release ARMV4I 构建、`python scripts/audit_repo.py`、文档审计和最终工作区核对。tracked
  INI 未修改，设备证据仍只在 `tmp/`。

next631 的 programmatic anchor click 自动门已经完成：

- `tmp/device-runs/20260824-165852-next631-programmatic-anchor-r4/` 的窄门为 PASS，
  TEST1079、1070、999 共 3/3，唯一 `TESTBENCH PASS`，`error_count=0`、
  `fail_count=0`、`test13_route_ok=True`；相关累计门
  `tmp/device-runs/20260824-165225-next631-programmatic-anchor-regression-r2/`
  另通过 18/18，同样零错误/失败。
- TEST1079 在真实 WM6 script session 上覆盖按 DOM id 解析已布局 `<a href>`、一次
  cancelable click、接受 ASSIGN、`preventDefault()`、容量/缺失/无 href generic 边界；
  TEST1070 在累计门中继续覆盖导航适配器拒绝；
  `PCore_LinkInfoById()` 只复制非空 href，网络、窗口替换和文档生命周期仍由宿主拥有。
- `python scripts/test_c89ize.py`、Debug ARMV4I 构建和窄设备门已通过；提交前仍需完成
  Release ARMV4I 构建、`python scripts/audit_repo.py`、文档审计和最终工作区核对。tracked
  INI 未修改，设备证据仍只在 `tmp/`。

next632 的同页 fragment anchor 自动门已经完成：

- `tmp/device-runs/20260824-172351-next632-fragment-anchor-r1/` 的窄门为 PASS，
  TEST1080、1079、1070、999 共 4/4，唯一 `TESTBENCH PASS`，`error_count=0`、
  `fail_count=0`、`test13_route_ok=True`。
- TEST1080 覆盖 fragment-only href 的 FRAGMENT 分类、跨页 href 的 ASSIGN 分类、
  `PCore_FragmentInfoById()` literal id 几何、当前 URL 绑定、目标滚动和 unknown-id 保持位置；
  `positron_browser.dll` 不拥有滚动或网络副作用。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、
  `python scripts/audit_repo.py`、文档审计和 `git diff --check` 均已通过；Release 保留既有
  libcss/fpmath 的 C4244 警告，产品 DLL 无新增警告。tracked INI 未修改，设备证据只在
  `tmp/`。
- Release 配置的设备探针在启动头后停滞，未产生 PASS；已安全停止本地主机 gate 进程，不能把
  它写成 Release 设备证据。next632 的设备事实仍以 Debug 窄门为准，WMDC/设备运行时停滞
  作为环境观察保留，不改变源码断言结论。

next633 的同文档 fragment history scroll 自动门已经完成：

- `tmp/device-runs/20260825-105254-next633-fragment-history-scroll-r2/` 的窄门为 PASS，
  TEST1081、1080、1079、1070、999 共 5/5，唯一 `TESTBENCH PASS`，`error_count=0`、
  `fail_count=0`、`test13_route_ok=True`。
- TEST1081 覆盖 fragment 导航后的目标滚动、同文档 back/forward 恢复、未知目标保持当前位置，
  并确认没有进入网络/文档替换路径；next632 的分类和 URL 绑定回归仍保留。
- C89、Debug/Release ARMV4I 正式构建和 Debug 设备门通过；Release 设备门不作为证据，
  tracked INI 未修改，设备日志只保留在 `tmp/`。

next634 的跨文档 history scroll 自动门已经完成：

- `tmp/device-runs/20260825-111645-next634-cross-document-history-scroll-r4/` 的窄门为 PASS，
  TEST1082、1081、1080、1079、1070、999 共 6/6，唯一 `TESTBENCH PASS`，`error_count=0`、
  `fail_count=0`、`test13_route_ok=True`。
- TEST1082 覆盖 A→B 后 back/forward 各自恢复、new entry 从零开始和短文档 clamp；相关
  fragment/anchor 回归仍在同一门内。C89、Debug/Release ARMV4I 正式构建、audit、文档审计
  和 `git diff --check` 均已通过；Release 设备探针仍不作为证据。真实页面视觉、触摸、SIP、
  旋转和 picker 仍按累计人工边界处理。

next635 的 fragment token 自动门已经完成：

- `tmp/device-runs/20260825-112545-next635-fragment-token-r1/` 的窄门为 PASS，TEST1083、
  1082、1081、1080、1079、1070、999 共 7/7，唯一 `TESTBENCH PASS`，`error_count=0`、
  `fail_count=0`、`test13_route_ok=True`。
- TEST1083 覆盖 Core id 优先、旧式 `<a name>` fallback、宿主有效 `%HH` 解码，以及 malformed/
  unknown token 不改变 viewport；没有扩张 browser history ABI。C89、Debug ARMV4I 正式构建
  和设备门已通过；Release、audit、文档审计及最终 Git 核对仍待完成。

next636 的 anchor target/rel 元数据自动门已经完成：

- `tmp/device-runs/20260825-115309-next636-anchor-target-rel/` 的窄门为 PASS，TEST1079–1084、
  999 共 7/7，唯一 `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。
- `tmp/device-runs/20260825-115405-next636-anchor-target-rel-regression/` 的相关累计门为
  PASS，TEST1064–1084、999 共 22/22，唯一 `TESTBENCH PASS`，无 ERROR/FAIL。
- TEST1084 覆盖 `PCore_LinkInfoByIdEx()`/`PCore_LinkAtEx()` 的 id/坐标查询、缺失属性、容量
  边界、`a.target/a.rel`、programmatic click 的 metadata 传播和 preventDefault；`test_host`
  只消费 Core/browser ABI，没有创建窗口。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、`python scripts/audit_repo.py`
  和 `git diff --check` 均通过；Release/Debug 仅保留既有 libcss/fpmath 的 3 个 C4244 警告。
  本批没有新增视觉、真实触摸、SIP、旋转或 picker 人工门。

next637 的 anchor target policy 自动门已经完成：

- `tmp/device-runs/20260825-130233-next637-anchor-target-policy-r2/` 的窄门为 PASS，
  TEST1079–1085、999 共 8/8，唯一 `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、
  `test13_route_ok=True`。
- TEST1085 覆盖空/空白、大小写、`_self`/`_parent`/`_top`/`_blank`/named 分类，fragment
  导航传播以及单窗口对新 context 的 fail-closed；TEST1084 与 1079–1083 保留相邻回归。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、`python scripts/audit_repo.py`
  和 `git diff --check` 均通过；仅保留既有 libcss/fpmath 的 3 个 C4244 警告。本批没有新增
  视觉、真实触摸、SIP、旋转或 picker 人工门。

next638 的 bounded `window.open()` 当前上下文自动门已经完成：

- `tmp/device-runs/20260825-132745-next638-window-open-regression/` 的回归门为 PASS，
  TEST1080–1086、TEST999 共 8/8，唯一 `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、
  `test13_route_ok=True`。
- TEST1086 覆盖 `PBROWSER_SCRIPT_NAVIGATION_OPEN` 的 URL/raw target/target_kind 传播，
  `_self`/`_parent`/`_top` 的当前上下文接受，DEFAULT/`_blank`/named/空 URL 拒绝，以及
  注销 callback 后的 null 回退；TEST1085 保留 anchor target policy 相邻回归。
- Debug 定向设备门已通过；Release ARMV4I、C89、仓库审计、文档审计和最终 Git 核对仍是
  本批交付前置项。本批没有新增必须立即人工复核的视觉、真实触摸、SIP、旋转或 picker 门。

next639 的单窗口 browsing context 身份自动门已经完成：

- `tmp/device-runs/20260825-142743-next639-window-name-regression-r2/` 的回归门为 PASS，
  TEST1080–1087、TEST999 共 9/9，唯一 `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、
  `test13_route_ok=True`。
- TEST1087 覆盖 `context_name` 快照传播、匹配当前 `window.name` 的 named reuse、未知
  named/`_blank` 拒绝，以及新 script session 的名称恢复；TEST1086 保留显式当前上下文
  open 语义和注销后的 fail-closed。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I、仓库审计、文档审计、diff 检查和
  设备门均已通过；仅剩最终 Git 状态、提交和推送核对。本批没有新增必须立即人工复核的视觉、
  真实触摸、SIP、旋转或 picker 门。

next640 的 named anchor 当前 context 自动门已经完成：

- `tmp/device-runs/20260825-145401-next640-named-anchor-regression-r5/` 的回归门为 PASS，
  TEST1080–1088、TEST999 共 10/10，唯一 `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、
  `test13_route_ok=True`。
- TEST1088 覆盖普通锚点的 named ASSIGN 路由、与活动 `window.name` 的精确匹配、未知名称和
  空名称的 fail-closed；窗口消息边界再次使用同一 context 检查。browser DLL 的
  `context_name` 仍是 OPEN-only 尾字段，宿主通过活动 session 快照为锚点做 admission。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I、仓库审计、文档审计、diff 检查和
  设备门均已通过；本批只改变宿主导航策略，没有新增必须立即人工复核的视觉、真实触摸、
  SIP、旋转或 picker 门。

next641 的 anchor `relList` 自动门已经完成：

- `tmp/device-runs/20260825-152127-next641-anchor-rel-list-final-r2/` 的回归门为 PASS，TEST1080–1089、
  TEST999 共 11/11，零 `ERROR`/`FAIL`，唯一 `TESTBENCH PASS` 且 `test13_route_ok=True`。
- TEST1089 覆盖 relList 稳定 identity、去重、ASCII 大小写不敏感的 contains/remove/toggle/replace、
  add、value 反射、item/length、forEach/iterator、非法 token 异常和非 rel 元素的 null 边界。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、仓库审计、文档审计和
  `git diff --check` 均通过；本批只改变 browser bootstrap 和 test_host 断言，不新增视觉、
  触摸、SIP、旋转或 picker 人工门，也没有修改 tracked INI。

next642 的 `relList.supports()` 自动门已经完成：

- `tmp/device-runs/20260825-153256-next642-rel-list-supports-final/` 的回归门为 PASS，
  TEST1080–1090、TEST999 共 12/12，零 `ERROR`/`FAIL`，唯一 `TESTBENCH PASS` 且
  `test13_route_ok=True`。
- TEST1090 覆盖 `<link>` 的 `stylesheet` 正例与 ASCII 大小写、未实现 `preload`、`a` 的
  `noopener`、`form` 的 `stylesheet`、非 rel 元素和非法 token `SyntaxError`；该方法只
  报告 Core 已处理的 stylesheet，不改变窗口/安全策略。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、仓库审计、文档审计和
  `git diff --check` 均通过；本批只改变 browser bootstrap、TEST1090 和相关文档，没有
  修改公共 C ABI 或 tracked INI，也没有新增视觉、触摸、SIP、旋转或 picker 人工门。

next643 的页面 stylesheet `media` 自动门已经完成：

- `tmp/device-runs/20260825-155459-next643-style-media-final/` 的相关回归门为 PASS，
  TEST21、TEST24、TEST1091、TEST999 共 4/4，零 `ERROR`/`FAIL`，唯一 `TESTBENCH PASS` 且
  `test13_route_ok=True`。
- TEST1091 在同一文档的 320px/299px 两次样式事务中覆盖 `<style media>`、
  `<link rel="stylesheet" media>` 的匹配选择，并确认外部 CSS cache 使总 fetch/free 保持
  2/2；这是 Core 样式选择契约，不是脚本侧动态媒体事件或 link 下载策略。
- `python scripts/test_c89ize.py` 和 Debug ARMV4I 正式构建、设备门均通过；候选阶段曾因
  夹具每轮新建文档而观察到 4/4 fetch/free，修正为同一文档跨视口重排后通过，未放宽产品
  断言；本批没有修改公共 C ABI 或 tracked INI，也没有新增视觉、触摸、SIP、旋转或 picker
  人工门。

next644 的 stylesheet `media` DOM 反射自动门已经完成：

- `tmp/device-runs/20260825-160422-next644-stylesheet-media-reflection/` 的定向门为 PASS，
  TEST1090、TEST1091、TEST1092、TEST999 共 4/4，零 `ERROR`/`FAIL`，唯一
  `TESTBENCH PASS` 且 `test13_route_ok=True`。
- TEST1092 覆盖 `<link>`/`<style>` 的初始值、缺失值、wrapper identity、raw attribute
  变化、setter 的字符串化、removeAttribute 恢复，以及非目标元素的 fail-closed 行为。
  该属性只反映 UTF-8 metadata，不宣称动态样式重排、link 下载策略或其他 link type。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建和仓库/文档审计均通过；
  本批没有修改公共 C ABI 或 tracked INI，也没有新增视觉、触摸、SIP、旋转或 picker 人工门。

next645 的 disabled stylesheet 选择自动门已经完成：

- `tmp/device-runs/20260825-161729-next645-stylesheet-disabled/` 的定向门为 PASS，
  TEST21、TEST24、TEST1091、TEST1093、TEST999 共 5/5，零 `ERROR`/`FAIL`，唯一
  `TESTBENCH PASS` 且 `test13_route_ok=True`。
- TEST1093 在同一文档的两次样式事务中确认：存在 `disabled` 属性的
  `<link rel="stylesheet">` 不发生 fetch、不会把外部颜色写入 computed style；启用的 link
  仍生效，第二次事务复用 document-owned CSS cache（1 次 fetch、1 次 free）。这是 Core
  资源收集边界，不扩张为 `type`/`alternate`、动态 link 生命周期或脚本事件。
- `python scripts/test_c89ize.py`、Debug ARMV4I 正式构建和设备门均通过；本批没有修改公共
  C ABI 或 tracked INI，也没有新增视觉、触摸、SIP、旋转或 picker 人工门。

next646 的 stylesheet rel-token 选择自动门已经完成：

- `tmp/device-runs/20260825-162659-next646-stylesheet-rel-tokens/` 的定向门为 PASS，
  TEST21、TEST24、TEST1091、TEST1093、TEST1094、TEST999 共 6/6，零 `ERROR`/`FAIL`，
  唯一 `TESTBENCH PASS` 且 `test13_route_ok=True`。
- TEST1094 确认混合大小写、ASCII 空白分隔的 `stylesheet` token 会加载并应用；含
  `alternate stylesheet` 的 link 不 fetch、不覆盖 inline 基线；同一文档第二次样式事务
  命中 CSS cache，不产生第二次 fetch。
- `python scripts/test_c89ize.py`、Debug ARMV4I 正式构建和设备门均通过；本批没有修改公共
  C ABI 或 tracked INI，也没有新增视觉、触摸、SIP、旋转或 picker 人工门。

next623 的 trusted native toggle activation 自动门已经完成：

- `tmp/device-runs/20260824-124858-next623-native-toggle-r5/` 的
  `device-gate-result.txt` 为 PASS，TEST1071、64、73、999 共 4/4，唯一
  `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。
- TEST1071 覆盖接受、无状态变化、preventDefault、禁用、kind mismatch、取消、回调错误、
  reset，以及共享 session helper 的 CLICK→Core commit→COMMIT 接线；真实触摸、label
  转发、OEM 视觉和旋转仍不由该自动门保证。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、
  `python scripts/audit_repo.py` 和窄设备门均已通过；Release/Debug 仅保留既有
  libcss/fpmath 的 3 个 C4244 警告，产品 DLL 无新增警告。

next624 的 trusted native submit/reset button activation 自动门已经完成：

- `tmp/device-runs/20260824-131847-next624-native-button-r4/` 的
  `device-gate-result.txt` 为 PASS，TEST1072、64、73、999 共 4/4，唯一
  `TESTBENCH PASS`，`error_count=0`、`fail_count=0`、`test13_route_ok=True`。
- TEST1072 覆盖 submit/reset 的 CLICK→COMMIT 顺序、click/form 取消、无效校验时抑制
  submit、禁用、回调错误、kind mismatch、幂等 CANCEL、16-token 容量、reset 和共享
  session helper 接线；真实按钮触摸坐标、label 转发、窗口视觉和旋转仍不由该自动门保证。
- `python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建、
  `python scripts/audit_repo.py` 和窄设备门均已通过；Release/Debug 仅保留既有
  libcss/fpmath 的 3 个 C4244 警告，产品 DLL 无新增警告。

## 当前已知边界

需要继续面对而不能用断言掩盖的边界包括：

- DOM、表单集合、历史、存储、请求响应和异步模型仍是资源有界的子集，不是完整现代浏览器。
- Anchor href/target/rel 元数据查询与受信任点击传播已有 next636 自动契约；next637 已把
  raw target 分类并让单窗口宿主对 `_blank`/named 请求 fail-closed。真实 `_blank`/named
  window 创建、窗口复用/生命周期和跨窗口 history 仍未覆盖。next638 的
  `window.open()` 只提供显式当前-context target 的安全复用，next639/640 只增加匹配当前
  `window.name` 的单窗口 named reuse（分别覆盖 OPEN 与普通 anchor），不能扩大为多窗口保证。
- 布局仍缺少 Grid、sticky、复杂包含块及完整表格/列表行为；float 路线已撤回。
- SIP/IME、候选词、旋转、文件选择器和视觉几何仍可能需要真实设备人工验收。
- Mbed TLS 2.16.12 已停止维护；peer 模式仍只有 TLS 1.2/IPv4，私钥为未加密 PEM，同步
  DNS 解析本身不能取消。详细安全契约见 `positron_tls/README.md`。
- 更新批次的针对性回归很强，但不能被表述为 TEST1–1085 的最新全范围覆盖。

详细的当前边界与解除条件见 `.agents/KNOWN_LIMITATIONS.md`。

## 当前工作区与候选状态

- next612 的 Debug/Release 构建、C89 检查、audit 和针对性设备门均已通过；最终远端状态需在
  提交/推送后重新核对。
- next613 的 Debug/Release ARMV4I 正式构建、C89 检查和 `python scripts/audit_repo.py` 均已通过；
  Release 与 Debug 保留既有 libcss/fpmath 的 3 个 C4244 警告，产品 DLL 无新增警告。
- next614 的 Debug/Release ARMV4I 正式构建、C89 检查和针对性设备门均已通过；Release 与 Debug
  保留既有 libcss/fpmath 的 3 个 C4244 警告，产品 DLL 无新增警告。
- next616 的源码、C89、Debug/Release 构建、audit、离线 Debug 设备门和文档均已完成；网络
  Debug 门已完成 example.com 第一跳，IANA 后续跳转属于外网可达性限制，不影响离线交付标准。
  tracked 改动只覆盖宿主 URL 解析、TEST1064、
  相关 README/测试/交接文档；提交时不要把 `tmp/` 设备证据或无关工作区文件带入。
- next617 的源码、C89、Debug/Release 构建、定向设备门、audit、Git diff 和远端状态均已在
  `f2f0dbcb` 推送前后核对；tracked 改动只覆盖该批 `positron_http` resolver、消费者和文档。
- next618 的 Debug/Release 全量 ARMV4I rebuild、C89、audit、RAPI gate 修复和窄设备门均已
  完成；其唯一未完成项是用户对 TEST65 的真实 SIP 候选词人工确认。
- next619 的 browser-owned IME result transaction、TEST1067、Debug/Release 构建、C89、audit
  和窄设备门已完成；tracked 改动只覆盖 `positron_browser` ABI/实现、`test_host` 消费者、
  TEST1067 与相关文档。不要把 `tmp/` 设备证据或无关工作区文件带入。
- next620 的 browser-owned file selection transaction、TEST1068、Debug/Release 构建、C89、
  audit 和窄设备门已完成；tracked 改动只覆盖 `positron_browser` ABI/实现、`test_host`
  消费者、TEST1068 与相关文档。不要把 `tmp/` 设备证据或无关工作区文件带入。
- next621 的 browser-owned file-picker arbitration、TEST1069、Debug/Release 构建、C89、
  audit 和窄设备门已完成；tracked 改动只覆盖 `positron_browser` ABI/实现、`test_host`
  消费者、TEST1069 与相关文档。不要把 `tmp/` 设备证据或无关工作区文件带入。
- next622 的 browser-owned trusted anchor activation、TEST1070、Debug/Release 构建、C89、
  audit 和窄设备门已完成；tracked 改动只覆盖 `positron_browser` ABI/实现、`test_host`
  消费者、TEST1070 与相关文档。不要把 `tmp/` 设备证据或无关工作区文件带入。
- next623 的 browser-owned trusted native toggle activation、TEST1071、Debug/Release 构建、
  C89、audit 和窄设备门已完成；tracked 改动只覆盖 `positron_browser` ABI/实现、`test_host` 消费者、
  TEST1071 与相关文档。不要把 `tmp/` 设备证据或无关工作区文件带入。
- next624 的 browser-owned trusted native submit/reset button activation、TEST1072、
  Debug/Release 构建、C89、audit 和窄设备门已完成；tracked 改动只覆盖 `positron_browser`
  ABI/实现、`test_host` 消费者、TEST1072 与相关文档。不要把 `tmp/` 设备证据或无关工作区
  文件带入。
- next625 的 browser-owned trusted native ordinary button activation、TEST1073、
  Debug/Release 构建、C89、audit 和窄设备门已完成；tracked 改动只覆盖 `positron_browser`
  ABI/实现、`test_host` 消费者、TEST1073 与相关文档。不要把 `tmp/` 设备证据或无关工作区
  文件带入。
- next626 的宿主 native button keyboard focus/activation、TEST1074、Debug/Release 构建、
  C89、audit 和窄设备门已完成；tracked 改动只覆盖 `test_host` 消费者、TEST1074 与相关
  文档。`positron_browser` 公共 ABI 未扩张，继续复用既有 button/key 入口；不要把 `tmp/`
  设备证据或无关工作区文件带入。
- next627 的宿主 label→native button forwarding、TEST1075、C89、Debug 构建和窄设备门已
  完成；tracked 改动只覆盖 `test_host` 消费者、TEST1075 与相关文档，`positron_browser`
  公共 ABI 未扩张；不要把 `tmp/` 证据或无关工作区文件带入。
- next628 的宿主 label→native toggle forwarding、TEST1076、C89、Debug/Release 构建、
  audit 和窄设备门已完成；tracked 改动只覆盖 `test_host` 消费者、TEST1076 与相关文档，
  `positron_browser` 公共 ABI 未扩张。不要把 `tmp/` 证据或无关工作区文件带入。
- next629 的宿主 label→native text/textarea/select forwarding、TEST1077、C89、
  Debug/Release 构建、audit 和窄设备门已完成；tracked 改动只覆盖 `test_host` 消费者、
  TEST1077 与相关文档，`positron_browser` 公共 ABI 未扩张。没有修改 tracked INI；不要把
  `tmp/` 证据或无关工作区文件带入。
- next630 的 programmatic `HTMLElement.click()` native focus、TEST1078、C89、Debug 构建和
  相关累计设备门已完成；tracked 改动覆盖 `positron_browser` target-kind/default-action
  常量、`test_host` 的 DOM-id 事件接线与 TEST1078，以及对应 README/架构/测试/交接文档。
  没有修改 tracked INI；不要把 `tmp/` 证据或无关工作区文件带入 Git。
- next631 的 programmatic anchor click、`PCore_LinkInfoById()`、TEST1079、C89、Debug 构建
  和窄设备门已完成；tracked 改动只覆盖 `positron_core`/`positron_browser` 的 additive
  ABI、`test_host` 消费者和相关文档。没有修改 tracked INI；提交前补跑 Release、audit 和
  最终 diff 检查，不要把 `tmp/` 证据或无关工作区文件带入 Git。
- next632 的 fragment anchor 分类、`PCore_FragmentInfoById()`、TEST1080、C89、Debug/Release
  构建、audit、文档审计和窄设备门已完成；tracked 改动只覆盖 `positron_core`/
  `positron_browser` 的 additive ABI、`test_host` 消费者和相关文档。没有修改 tracked INI；
  Release 设备探针停滞未计入证据，不要把 `tmp/` 证据或无关工作区文件带入 Git。
- next633 的 fragment history traversal scroll restore、TEST1081、C89、Debug/Release 构建和
  Debug 窄设备门已完成；tracked 改动只覆盖 `test_host` 消费者与相关文档，未扩张
  `positron_browser` 公共 ABI，也没有修改 tracked INI。Release 设备探针不计入证据，不要把
  `tmp/` 证据或无关工作区文件带入 Git。
- next634 的跨文档 history scroll restore、TEST1082、C89、Debug/Release 构建、audit、文档
  审计和 Debug 窄设备门已完成；tracked 改动只覆盖 `test_host` 消费者与相关文档，未扩张
  `positron_browser` 公共 ABI，也没有修改 tracked INI。Release 设备探针仍不计入证据；提交
  前只需完成最终 Git 状态、提交和推送核对。
- next635 的 fragment token resolution、`PCore_FragmentInfoByToken()`、TEST1083、C89、
  Debug 构建和 Debug 窄设备门已完成；tracked 改动覆盖 `positron_core` additive ABI、
  `test_host` 消费者/断言和相关文档，没有修改 tracked INI。提交前补跑 Release、audit、
  文档审计、diff 检查并完成最终 Git 状态、提交和推送核对。
- next636 的 anchor target/rel metadata resolution、`PCore_LinkAtEx()`/
  `PCore_LinkInfoByIdEx()`、`PBrowser_ScriptSessionDispatchAnchorClickEx()`、TEST1084、
  C89、Debug/Release 构建、7/7 窄门、22/22 累积门、audit、文档审计和 diff 检查均已完成；
  tracked 改动只覆盖 `positron_core`/`positron_browser` additive ABI、`test_host` 消费者/
  断言和相关文档，没有修改 tracked INI。提交前只需完成最终 Git 状态、提交和推送核对。
- next637 的 browser-owned anchor target policy 分类、单窗口 fail-closed 路由、TEST1085、
  C89、Debug/Release 构建、8/8 窄门、audit、文档审计和 diff 检查均已完成；tracked 改动
  只覆盖 `positron_browser` additive ABI、`test_host` 消费者/断言和相关文档，没有修改
  tracked INI。提交前只需完成最终 Git 状态、提交和推送核对。
- next638 的 bounded `window.open()` 当前-context 路由、TEST1086、Debug 8/8 回归门已完成；
  tracked 改动只覆盖 `positron_browser` additive navigation kind、`test_host` 消费者/断言和
  相关文档，没有修改 tracked INI。提交前仍需完成 C89、Release、audit、文档审计、diff 检查、
  最终 Git 状态、提交和推送核对。
- next639 的 `context_name`/`window.name` browsing-context 身份、TEST1087、设备 9/9
  回归门、C89、Debug/Release、audit、文档审计和 diff 检查均已完成；tracked 改动只覆盖
  `positron_browser` additive navigation ABI、`test_host` 消费者/断言和相关文档，没有修改
  tracked INI。提交前只需完成最终 Git 状态、提交和推送核对。
- next640 的 named anchor 当前 context admission、TEST1088、设备 10/10 回归门、C89、
  Debug/Release、audit、文档审计和 diff 检查均已完成；tracked 改动只覆盖 `test_host` 消费者/
  断言和相关文档，没有修改 tracked INI。提交前只需完成最终 Git 状态、提交和推送核对。
- next641 的 anchor `relList` bootstrap、TEST1089、设备 11/11 回归门、C89、Debug/Release、
  audit、文档审计和 diff 检查均已完成；tracked 改动只覆盖 `positron_browser` bootstrap、
  `test_host` 消费者/断言和相关文档，没有修改公共 C ABI 或 tracked INI。提交前只需完成
  最终 Git 状态、提交和推送核对。
- next642 的 `relList.supports()` bootstrap、TEST1090、设备 12/12 回归门、C89、Debug/Release、
  audit、文档审计和 diff 检查均已完成；tracked 改动只覆盖 `positron_browser` bootstrap、
  `test_host` 消费者/断言和相关文档，没有修改公共 C ABI 或 tracked INI。提交前只需完成
  最终 Git 状态、提交和推送核对。
- 若后续出现 composition 顺序、候选词数据或 native commit→input 错误，应先保留
  browser/WM/Core 边界，不要通过跳过生命周期或放宽长度断言掩盖回归。
- tracked INI 不应为了下一批开发永久改成人工模式或扩大默认测试集。
- 接手者必须先检查工作区；任何未提交改动默认属于用户，不能覆盖。

## 唯一下一步

next646 的自动契约已经完成；继续开发时应按路线图的真实页面/应用语料选择下一个高价值
纵切，不要为了补编号添加孤立 API。Core 现在会按 viewport 选择 `<style media>` 与
`<link rel="stylesheet" media>`，按 rel token 选择 stylesheet、跳过带 `disabled` 属性的外部
stylesheet，并在同文档重排复用外部 CSS cache；browser wrapper 还提供 `<link>`/`<style>` 的
bounded `media` 反射，但
不提供动态 MediaQueryList 事件、完整 link 下载策略或 noopener/opener 的窗口安全处理。
TEST65 的多字符 SIP 候选词、select/file picker 模态框、
真实 label 触摸、OEM 窗口视觉和键盘映射仍是独立人工边界；在人工证据出现前不得把它们写成
通用产品保证。anchor href/target/rel 元数据与 target_kind、bounded `window.open()`、普通
named anchor 和匹配当前 `window.name` 的 named reuse 已有 Core/browser/host 契约，但真实
`_blank`/named window 创建、第二个 global、窗口复用与生命周期仍未覆盖；fragment-only 锚点继续在当前宿主路径支持有界
`%HH` 解码、id 优先和 `<a name>` fallback。history traversal 只在当前宿主进程内恢复有界
的同文档目标或跨文档条目偏移，不提供持久缓存/跨进程恢复。下一次重要产品/生命周期风险
累积后，再安排新的全范围设备基线。

## next617 完成标准

- `PHttp_ResolveReference` 作为 additive 稳定 C ABI 导出，产品 resolver 统一服务页面导航、
  子资源和 HTTP GET 重定向；宿主不再拥有同一套 URL 业务副本。
- 公共 ABI、UTF-8、opaque handle、内存所有权及 VS2008 / WM6 ARMV4I / C89 兼容性不退化。
- `python scripts/test_c89ize.py`、正式工程构建和 `python scripts/audit_repo.py` 通过。
- 通过 TEST1065 的产品 HTTP(S) reference/Location 契约、TEST1064 的宿主消费者回归和
  TEST999 完成提示音；设备门必须是唯一 `TESTBENCH PASS` 且无 ERROR/FAIL。网络可用时
  再以 TEST13 证明真实页面消费，但外网超时必须与离线门分开归因。
- 只有出现视觉、真实触摸、SIP、旋转、文件选择器或失败网络风险时才累计人工门；崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即人工复核。
- 跟踪的默认 INI 恢复为自动模式且选择集不被无意扩大。
- 用当批事实覆盖本文件的当前快照，更新限制和路线图；只提交本批 tracked 文件并推送 `main`。

## next618 完成标准

- `GCS_RESULTSTR` 的完整 UTF-8 结果在 WM6 native EDIT 当前 composition selection 中一次性
  提交；失败时保留原有 default-procedure fallback，不改变无脚本路径。
- TEST1066 的多字节多字符候选值完整到达 Core；TEST123–125、TEST65 相关回归不退化，
  TEST999 只触发一次提示音。
- `python scripts/test_c89ize.py`、Debug/Release 正式 ARMV4I 构建和
  `python scripts/audit_repo.py` 通过；设备门必须是唯一 `TESTBENCH PASS` 且无 ERROR/FAIL。
- 真实 SIP 候选词整词提交和视觉只在人工确认后提升为设备事实；人工未确认前继续记录为
  限制，不扩张默认 `test_host.ini` 选择集。

## next619 完成标准

- `PBrowser_ScriptSessionDispatchNativeEditResult()` 作为 additive 稳定 C ABI，校验活动
  composition、UTF-8 result 容量和生命周期，并复用 browser-owned pending native commit
  metadata；宿主不获得新的产品事件所有权。
- TEST1067 与 TEST1066、123–125、999 的窄设备门唯一 PASS、零 ERROR/FAIL；Debug/Release
  正式构建、C89 和仓库/文档 audit 通过。
- 不新增视觉、触摸、旋转、picker 或 OEM SIP 人工保证；TEST65 仍按 next618 的独立人工门处理。

## next620 完成标准

- `PBrowser_ScriptSessionDispatchNativeFileSelection()` 作为 additive 稳定 C ABI，校验
  stable token、phase 和固定容量，持有 BEGIN/COMMIT/CANCEL 状态；COMMIT 只派发一次
  `input(insertFromFile)` → `change`，CANCEL 幂等，reset/unregister 清理状态。
- `test_host` 在打开系统 picker 前 BEGIN，成功完成 `PCore_FileInputSetPath` 后 COMMIT，
  取消、失败或路径写入失败时 CANCEL；无脚本路径保留原 fallback，宿主不复制产品事件
  策略。
- TEST1068、262、230、999 的窄设备门必须是唯一 `TESTBENCH PASS` 且无 ERROR/FAIL；
  Debug/Release 正式构建、C89 和仓库 audit 通过。TEST232/263 的真实 picker、权限、
  重入和视觉继续作为人工门，不写成自动化保证。
- 跟踪的默认 INI 保持自动模式和原有选择集；更新限制、路线图和交接快照，只提交本批
  tracked 文件并推送 `main`。

## next621 完成标准

- `PBrowser_ScriptSessionDispatchNativeFilePicker()` 作为 additive 稳定 C ABI，校验
  stable token、phase 和 session 生命周期；每个 session 只允许一个 pending/active
  request，REQUEST 重复调用 coalesce，OPEN 只接受 matching pending，CLOSE/CANCEL 和
  reset 清理状态，API 不拥有 picker handle、文件名或路径。
- `test_host` 在投递 `PostMessage` 前 REQUEST，进入系统 picker 前 OPEN，返回后 CLOSE；
  投递失败或文档替换时 CANCEL。宿主保留 HWND/document/index、WM6 picker、文件系统和
  路径写入副作用，不复制产品仲裁规则。
- TEST1069、262、230、999 的窄设备门必须是唯一 `TESTBENCH PASS` 且无 ERROR/FAIL；
  Debug/Release 正式构建、C89 和仓库 audit 通过。TEST232/263 的真实 picker 和视觉
  继续作为人工门。
- 跟踪的默认 INI 保持自动模式和原有选择集；更新限制、路线图和交接快照，只提交本批
  tracked 文件并推送 `main`。

## next622 完成标准

- `PBrowser_ScriptSessionDispatchAnchorClick()` 作为 additive 稳定 C ABI，校验
  session、click/navigation callback、借用 UTF-8 href 和固定容量；browser layer 先派发
  一次可取消 click，只有未被阻止时才提交 ASSIGN，导航适配器拒绝或失败不伪造成功。
- `test_host` 在启用脚本且 `PCore_LinkAt` 命中时调用该入口；宿主保留 href 命中、
  网络、窗口和文档副作用，未启用脚本时保留既有 generic click → navigate_to fallback。
- TEST1070、230、262、999 的窄设备门必须是唯一 `TESTBENCH PASS` 且无 ERROR/FAIL；
  Debug/Release 正式构建、C89 和仓库 audit 通过。真实点击坐标、程序化 anchor click、
  target/rel/window 和视觉继续保持未覆盖边界。
- 跟踪的默认 INI 保持自动模式和原有选择集；更新限制、路线图和交接快照，只提交本批
  tracked 文件并推送 `main`。

## next624 完成标准

- `PBrowser_ScriptSessionDispatchNativeButton()` 作为 additive 稳定 C ABI，校验
  stable token、CLICK/COMMIT/CANCEL phase、submit/reset kind 和布尔状态；browser layer
  先派发可取消 click，宿主在 click 之后查询 Core validation，再由 COMMIT 派发 submit 或
  reset，CANCEL 幂等，回调错误不放行默认动作。
- `test_host` 在启用脚本且命中 submit/reset 时调用 CLICK，随后以 COMMIT 或 CANCEL 告知
  browser DLL；宿主保留 Core validation/default action、导航、窗口和无脚本 fallback，避免
  复制产品事件顺序。
- TEST1072、64、73、999 的窄设备门必须是唯一 `TESTBENCH PASS` 且无 ERROR/FAIL；
  Debug/Release 正式构建、C89 和仓库 audit 通过。真实按钮坐标、label 转发、窗口视觉、
  旋转和其他 OEM 副作用继续保持未覆盖边界。
- 跟踪的默认 INI 保持自动模式和原有选择集；更新限制、路线图和交接快照，只提交本批
  tracked 文件并推送 `main`。

## next625 完成标准

- `PBrowser_ScriptSessionDispatchNativeButton()` 继续作为 additive 稳定 C ABI，接受
  ordinary button kind；CLICK 派发可取消 click，COMMIT 不派发 submit/reset，只在事件未
  取消时返回普通按钮默认已消费，CANCEL、禁用、非法 kind 和回调错误保持 fail-closed。
- `test_host` 在命中 `kind=9` 的普通 `<button type="button">` 时调用 CLICK/COMMIT；宿主
  保留 hit-test、Core/WM 副作用和重绘，但不再让已接受的普通按钮落入关闭窗口 fallback。
- TEST1073、1072、64、73、999 的窄设备门必须是唯一 `TESTBENCH PASS` 且无 ERROR/FAIL；
  Debug/Release 正式构建、C89 和仓库 audit 通过。普通按钮真实触摸视觉、键盘焦点/激活与
  label 转发继续保持未覆盖边界。
- 跟踪的默认 INI 保持自动模式和原有选择集；更新限制、路线图和交接快照，只提交本批
  tracked 文件并推送 `main`。

## next626 完成标准

- `test_host` 为 enabled 的 Core native button 维护 document/index/kind 焦点；WM Enter
  keydown 与 Space keyup 复用 `PBrowser_ScriptSessionDispatchNativeButton()` 的
  CLICK→COMMIT，重复 keydown 不重复激活，焦点失效/禁用/销毁清理 pending 状态。
- browser DLL 继续拥有 typed key callback、click/form 取消与事件顺序；宿主只拥有 WM
  消息、Core 坐标、焦点、默认动作和窗口生命周期，不新增公共 ABI 或复制产品仲裁语义。
- TEST1074、1073、1072、64、73、999 的窄设备门必须是唯一 `TESTBENCH PASS` 且无
  ERROR/FAIL；Debug/Release 正式 ARMV4I 构建、C89 和仓库 audit 通过。真实 OEM 键盘映射、
  焦点视觉、触摸和 label 完整事务继续保持人工边界。
- 跟踪的默认 INI 保持自动模式和原有选择集；更新限制、路线图和交接快照，只提交本批
  tracked 文件并推送 `main`。

## next627 完成标准

- `test_host` 在启用脚本且 label 命中 Core `kind=7..9` button 时先派发 label click，再以
  control index/kind 调用既有 `PBrowser_ScriptSessionDispatchNativeButton()` CLICK→COMMIT；
  browser DLL 继续拥有目标 click/form 取消和事件顺序，宿主只执行已接受的默认动作。
- disabled 或 stale label target 不合成目标 click，不改变表单状态，也不落入窗口关闭 fallback；
  无脚本路径继续使用原 generic fallback。
- TEST1075、1074、1073、1072、999 的窄设备门必须是唯一 `TESTBENCH PASS` 且无 ERROR/FAIL；
  Debug/Release 正式 ARMV4I 构建、C89 和仓库 audit 通过。真实 label 触摸坐标、焦点视觉、
  OEM 按键映射和其他 labelable 控件继续保持人工/独立边界。
- 跟踪的默认 INI 保持自动模式和原有选择集；更新限制、路线图和交接快照，只提交本批
  tracked 文件并推送 `main`。

## next628 完成标准

- `test_host` 在启用脚本且 label 命中 Core `kind=1/2` toggle 时先派发 label click，再以
  control index/kind 调用既有 `PBrowser_ScriptSessionDispatchNativeToggle()` CLICK→COMMIT；
  browser DLL 继续拥有目标 click 取消及 `input` → `change` 顺序，宿主只执行 Core mutation
  与重绘。
- disabled 或 stale label target 不合成目标 click、不改变 checked/radio 状态，也不落入窗口
  关闭 fallback；无脚本路径继续使用原 generic fallback。
- TEST1076、1075、1074、1071、64、73、999 的窄设备门必须是唯一 `TESTBENCH PASS` 且无
  ERROR/FAIL；Debug/Release 正式 ARMV4I 构建、C89 和仓库 audit 通过。真实 label 触摸坐标、
  焦点视觉、OEM 行为和其他 labelable 控件继续保持人工/独立边界。
- 跟踪的默认 INI 保持自动模式和原有选择集；更新限制、路线图和交接快照，只提交本批
  tracked 文件并推送 `main`。

## next629 完成标准

- enabled label 的 text/password/textarea/select/file target 先经过既有 browser click
  adapter；click 未取消且目标有效时，宿主才执行对应 native EDIT/SELECT focus 或系统
  picker；disabled/stale target 不产生目标 click，取消保持无默认动作，无脚本路径不变。
- TEST1077 与 1076、1075、1074、1073、1072、1071、64、73、999 的窄设备门必须是唯一
  `TESTBENCH PASS` 且无 ERROR/FAIL；设备日志记录 `test13_route_ok=True`。
- `python scripts/test_c89ize.py`、Debug/Release 正式 ARMV4I 构建、`python scripts/audit_repo.py`
  和 `git diff --check` 通过；只保留既有 libcss/fpmath 的 3 个 C4244 警告，不扩张
  `positron_browser` 公共 ABI，不改默认 INI。
- 系统 file picker 的打开/取消/路径提交、真实 label 触摸、SIP/IME、旋转和 OEM 视觉
  必须作为独立人工门记录，不能由 TEST1077 的自动断言代替。

## next630 完成标准

- `PBrowser_ScriptSessionRegisterProgrammaticClickCallbacksEx()` 继续保持原有 size-tagged
  ABI 布局；browser layer 接受 text/password/textarea/select target-kind，执行 disabled
  静默、typed click 和取消，并把 `PBROWSER_SCRIPT_CLICK_DEFAULT_FOCUS` 的平台副作用交给
  default-action callback。
- `test_host` 按同步 DOM id 派发 programmatic click，避免 native child window 遮挡导致的
  坐标误命中；default callback 只负责对应 native EDIT/SELECT 的焦点。select popup、系统
  picker、窗口、SIP/IME 和 OEM 视觉仍由宿主拥有。
- TEST1078、1077、1076、1075、1074、1073、1072、1071、64、73、999 的相关设备门必须为
  唯一 `TESTBENCH PASS` 且无 ERROR/FAIL；本批已以 11/11 通过。
- `python scripts/test_c89ize.py`、正式 Debug/Release ARMV4I 构建、`python scripts/audit_repo.py`
  和 `git diff --check` 必须通过；tracked INI 保持默认自动模式，tmp 证据不加入 Git。

# Positron 当前交接

更新时间：2026-08-23

本文件只保存接手下一批工作所需的当前快照。已完成批次、旧故障和旧验收记录以 Git 历史、`docs/history/` 与本地 `tmp/device-runs/` 为准，不在这里累计。

## 权威来源

- 项目使命、DLL 边界、ABI 和所有权：`docs/ARCHITECTURE.md`
- 当前未解决边界：`.agents/KNOWN_LIMITATIONS.md`
- 未来工作及优先级：`.agents/ROADMAP.md`
- 失败路线及重试前提：`.agents/FAILED_EXPERIMENTS.md`
- agent 工作规则：根目录 `AGENTS.md` 与 `.agents/README.md`
- “当前”结论必须由 Git、源码、构建、测试和设备日志交叉验证。

## 当前仓库基线

- 分支：`main`；本批开始时基线为 `69ffa52b next609: move native select commit policy to browser`，交付时必须
  重新核对远端和工作区，不能沿用这一结论。
- 当前能力批次：next610，native SELECT 焦点族事件语义迁移。
- 测试编号上限：`TEST_MAX_NUMBER 1058`。
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
扩展为可供 LocalSend 一类消费者复用的 peer TLS ABI v2。next607–610 已恢复并继续浏览器
产品语义迁移；next610 只处理 native SELECT 焦点族事件，不扩张 TLS 协议层。

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
  fallback。下拉展开/关闭、WM 键盘默认动作、SIP/IME composition 和 OEM 副作用仍未迁移。

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

`tmp/` 不跟踪，以上路径只用于本机证据定位；长期可追溯结论必须落在提交、源码和跟踪文档中。

## 当前已知边界

需要继续面对而不能用断言掩盖的边界包括：

- DOM、表单集合、历史、存储、请求响应和异步模型仍是资源有界的子集，不是完整现代浏览器。
- 布局仍缺少 Grid、sticky、复杂包含块及完整表格/列表行为；float 路线已撤回。
- SIP/IME、候选词、旋转、文件选择器和视觉几何仍可能需要真实设备人工验收。
- Mbed TLS 2.16.12 已停止维护；peer 模式仍只有 TLS 1.2/IPv4，私钥为未加密 PEM，同步
  DNS 解析本身不能取消。详细安全契约见 `positron_tls/README.md`。
- 更新批次的针对性回归很强，但不能被表述为 TEST1–1058 的最新全范围覆盖。

详细的当前边界与解除条件见 `.agents/KNOWN_LIMITATIONS.md`。

## 当前工作区与候选状态

- next610 的 Debug/Release 构建、相关设备门和仓库/文档审计均已通过。
- next610 的 tracked 改动应只覆盖 `positron_browser` 的 native SELECT focus ABI、`test_host`
  消费者/TEST1058 与对应交接/架构/测试文档；提交时不要把 `tmp/` 设备证据或无关工作区
  文件带入。
- next610 候选的第一轮设备门已通过；若后续出现焦点事件错误，应先保留 Core/WM 边界，
  不要通过跳过成对顺序或重复通知断言掩盖回归。
- tracked INI 不应为了下一批开发永久改成人工模式或扩大默认测试集。
- 接手者必须先检查工作区；任何未提交改动默认属于用户，不能覆盖。

## 唯一下一步

next611：继续审计仍由宿主独占的 native SELECT 默认动作与下拉生命周期；只有在真实页面或
设备日志可重复证明后，才选择键盘 Enter/Arrow 默认动作、展开/关闭状态、SIP/IME 候选词或
其他 form/input 缺口。复用 next610 的边界：browser layer 持有事件策略与状态，宿主只保留
WM 控件、Core mutation、窗口重绘和 OEM 平台副作用；不得把 OEM 行为伪装成已迁移语义，也
不得重做 next607–610。

不要为了填充 next 编号加入互不相关的小 API，也不要顺便重构其他模块。

## next610 完成标准

- 产品级语义不再由 `test_host` 独占，宿主通过公共 API 消费它。
- 公共 ABI、UTF-8、opaque handle、内存所有权及 VS2008 / WM6 ARMV4I / C89 兼容性不退化。
- `python scripts/test_c89ize.py`、正式工程构建和 `python scripts/audit_repo.py` 通过。
- 通过覆盖新行为的针对性自动设备门，以及包含相关既有能力和 TEST999 的回归门。
- 只有出现视觉、真实触摸、SIP、旋转、文件选择器或失败网络风险时才累计人工门；崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即人工复核。
- 跟踪的默认 INI 恢复为自动模式且选择集不被无意扩大。
- 用当批事实覆盖本文件的当前快照，更新限制和路线图；只提交本批 tracked 文件并推送 `main`。

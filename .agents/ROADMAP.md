# Positron 路线图

更新时间：2026-08-23

本文件只描述未来工作，不累计已完成 next、提交记录或设备日志。当前基线见 `.agents/HANDOFF.md`，当前边界见 `.agents/KNOWN_LIMITATIONS.md`，项目使命与公共 DLL 职责见 `docs/ARCHITECTURE.md`。

## 路线原则

- 一个 next 代表一套边界清楚、可独立验证的纵向能力，不用多个编号包装同一批工作，也不用一个编号塞入互不相关的小 API。
- 优先完成真实用户行为，再补观感和性能；兼容能力由目标页面、应用流程和故障证据驱动。
- 产品语义进入 `positron_core`、`positron_browser`、`positron_script` 或其他适当 DLL；`test_host` 只做平台适配、回归编排和示例消费。
- 公共接口保持稳定 C ABI、UTF-8、opaque handle 和明确内存所有权。
- 任何能力都必须适应 VS2008 / WM6 ARMV4I、C89 与目标设备的有界资源。
- 默认自动化；视觉、真实触摸、SIP、旋转、文件选择器和失败网络风险可积累后集中人工验收，严重故障立即复核。
- 每批使用与风险相称的目标测试和相关回归；不再机械运行全部测试。重要里程碑仍需新的全范围基线。
- 失败路线先记录边界和重试前提，不通过放宽断言或永久扩大 heap 假装成功。

## 当前中期里程碑

在 JavaScript 默认关闭的安全基线不变的前提下，形成一个可嵌入、资源有界、行为可预测的轻量 Web 运行时：

- HTML/CSS/布局能承载固定真实页面语料。
- DOM、表单和 Web API 子集足以支持目标应用，而不是追求现代浏览器表面 API 数量。
- 浏览器 JavaScript 与独立脚本 API 共享唯一 Duktape 引擎和一致的生命周期规则。
- 产品语义由可发布 DLL 持有；宿主只负责 WM 窗口、网络、原生控件和测试。
- 自动设备门能覆盖成功、失败、容量、销毁和回归，人工门只处理自动化无法可靠判断的体验。

完成这一里程碑不等于实现完整现代浏览器，而是得到一个边界明确、可供 Positron 应用层稳定消费的运行时。

## 当前短期目标

### 1. next607：程序化表单激活语义迁移（已完成）

next607 将一组完整的 `HTMLElement.click()` 表单行为从 `test_host` 下沉到
`positron_browser.dll`。新增的 additive `PBrowser_ScriptSessionRegisterProgrammaticClickCallbacksEx()`
让 browser layer 负责 disabled 控件静默、typed click、submit/reset 事件顺序、submit 验证
和取消；宿主只提供 target lookup、validation、default action 与非表单 click 传播。
TEST228–230 和 TEST1055 已在真实 WM6 设备上通过，tracked INI 未被扩大，未新增人工页面门。

### 2. next608：native EDIT 输入事务适配（已完成）

next608 将 native EDIT 的一组完整输入事务从 `test_host` 下沉到 `positron_browser.dll`：
新增 additive `PBrowser_ScriptSessionRegisterNativeEditCallbacksEx()` 及 beforeinput、native
commit、blur 入口，由 browser layer 持有 bounded pending metadata、input 转换、dirty tracking
和一次性 change 顺序；宿主只提供 WM EDIT 消息、文本值提交、几何和 core 事件传播。
TEST1056 的成功/取消/显式 fallback/reset/unregister 断言，以及 TEST228–230、1055、999 回归
已在真实 WM6 设备上通过。WM 控件、文本 mutation、composition 生命周期、SIP/IME 和 SELECT
键盘仍不在本批产品语义内。

### 3. next609：native SELECT 键盘/选择事件纵切（已完成）

next609 将 native SELECT 在 Core selection mutation 成功后的 `input` → `change` 顺序迁入
`positron_browser.dll`：新增 additive `PBrowser_ScriptSessionRegisterNativeSelectCallbacksEx()`、
commit 和 reset 入口，由 browser layer 校验 stable token、single/multiple 形状并保持有界
target state。宿主仍拥有 WM SELECT 键盘消息、typed key cancellation、窗口重绘、Core
selection mutation 和平台副作用；不把 SIP/IME OEM 行为伪装成产品兼容性，也没有重做
next607/608。TEST1057 的注册/非法输入/顺序/错误/reset/unregister 断言与 TEST67、71、118、
999 的真实设备门均通过。

### 4. next610：native SELECT 焦点族事件迁移（已完成）

next610 在 next609 的 bounded native SELECT state 上增加了
`PBrowser_ScriptSessionDispatchNativeSelectFocus()`。browser layer 按稳定 token 持有焦点状态，
统一同步派发 `focus` → `focusin` 或 `blur` → `focusout`，重复 WM 通知幂等，callback 失败后
状态保持旧值以便重试；宿主只提供 WM 焦点转换、控件几何、Core 交互和无脚本 fallback。
TEST1058 覆盖非法输入、成对顺序、幂等、失败恢复、多 token、reset 和 unregister；TEST67、71、
1057、999 的真实设备门通过。该批不宣称下拉展开/关闭、键盘默认动作或 OEM SIP/IME 兼容。

### 5. next611：native SELECT 单选下拉事务迁移（已完成）

next611 把单选 COMBOBOX 的 begin/candidate/confirm/cancel 事务状态迁入
`positron_browser.dll`，新增 `PBrowser_ScriptSessionDispatchNativeSelectInteraction()`。
browser layer 只记录有界候选状态，并在确认且确有候选时给宿主一个 commit 闸门；宿主仍拥有
WM 通知、Core selection mutation、原生控件取消回滚、窗口重绘和无脚本即时回退。TEST1059
覆盖 ABI 的非法输入、候选抑制、确认/取消、无候选确认、reset 和 unregister；TEST67 通过
合成 WM 通知探针断言 Core 不提前改变、取消恢复和确认提交。窄定向设备门
`tmp/device-runs/20260823-184446-next611-native-select-transaction-final-r2/` 通过 7/7
（TEST67、71、118、1057、1058、1059、999），Debug/Release ARMV4I 构建、C89 检查和
仓库/文档 audit 均通过；本批没有新增人工门。

### 6. next612：native SELECT 键盘默认动作审计（已完成）

next612 将 native SELECT 的键盘事件入口从宿主私有的 generic key 调用改为
`positron_browser.dll` 的 additive `PBrowser_ScriptSessionDispatchNativeSelectKey()`。
browser layer 校验稳定 token 和 `keydown`/`keyup` phase，复用已注册的 typed key adapter，
并把 cancelable/default-allowed 结果返回给宿主；宿主仍负责 WM 控件真正的 Enter/Arrow 默认动作、
Core selection mutation、窗口和 OEM 副作用。TEST1060 覆盖 ArrowDown/Enter 元数据、取消、
adapter error、reset 和 unregister；TEST118 在真实 WM6 页面上验证允许的 ArrowDown 同时移动
COMBOBOX 与 Core selection。窄定向设备门
`tmp/device-runs/20260823-190158-next612-native-select-key-final/` 通过 3/3
（TEST1060、118、999），Debug ARMV4I 构建和 C89 检查通过；本批没有新增人工门。
Release ARMV4I 构建、仓库/文档 audit 也已通过。

### 7. next613：native EDIT composition 生命周期迁移（已完成）

next613 将 native EDIT 的 compositionstart/update/end 语义从宿主私有 generic dispatch 下沉到
`positron_browser.dll`。新增 `PBrowser_ScriptSessionDispatchNativeEditComposition()`，由
browser layer 校验稳定 token、phase 和 255 字节 preedit 上限，负责
`compositionstart` → `beforeinput(insertCompositionText)` → `compositionupdate` →
`compositionend` 顺序，并把 UPDATE 的 pending metadata 接入 next608 的 native commit→input
事务；宿主仍负责 WM_IME、SIP/候选词窗口、原生文本 mutation 和平台副作用。TEST1061、123–125、
999 的定向设备门已通过；TEST65 的真实候选词整词提交仍保留人工门。

### 8. 建立真实页面驱动的兼容队列

在迁移工作之外，维护一个小而固定的页面/交互语料，用它选择下一项 DOM、CSS、表单或 JavaScript 能力。优先处理：

- 页面内容不可达或严重错位；
- 核心表单流程不能完成；
- 状态、事件或导航出现可重复错误；
- 当前产品边界迫使消费者复制语义。

只有不涉及上述真实缺口时，才考虑独立 Web API 补齐。

### 9. 安排新的全范围检查点

next255 之后的批次主要依赖目标门和相关回归。满足以下任一条件时，安排一次新的全范围设备基线，而不是每批都运行：

- 完成一组有意义的产品所有权迁移；
- 改变 heap、调度、销毁或跨模块生命周期；
- 累积多项输入、布局或导航风险；
- 准备宣告新的中期里程碑或发布候选。

全范围检查点仍不能替代视觉、SIP、触摸、旋转、文件选择器和真实失败网络的人工门。

## 中期工作流

### 浏览器 JavaScript 与 Web 平台

- 根据真实页面补齐 DOM/表单对象关系，而非平铺孤立构造函数。
- 明确快照集合、live 集合、容量上限和会话销毁语义。
- 统一 timer、promise job、网络回调与宿主泵送契约。
- 为 storage、cookie、Request/Response 和 history 定义清晰的内存模型与不支持行为。
- 在改变 JavaScript 默认开关前完成安全、内存、异常隔离和页面兼容评估。

### 布局与页面兼容

- 先巩固现有块、行内、盒模型、定位和已支持的弹性行为。
- 从固定页面语料选择下一项高价值布局缺口。
- Grid、sticky、复杂表格/列表和复杂包含块必须作为独立完整纵切规划。
- float 保持撤回状态；只有具备完整算法、设备预算和回归矩阵时才重新评估。
- 建立跨 DPI、方向、字体和滚动状态的集中视觉门。

### 表单与输入

- 补齐目标流程需要的 successful controls、验证、label/fieldset 和事件顺序。
- 把可自动判断的值、事件、取消、重入和状态保持全部纳入设备断言。
- 将 SIP/IME 候选词、composition、真实触摸、旋转和文件选择器体验打包成人工批次。
- 对原生控件与 HTML 视图的焦点、叠放和销毁建立稳定宿主契约。

### 网络与安全

- 将纯 Request/Response/URL 语义和实际设备 I/O 明确分层。
- 在现有 TLS peer ABI v2 基线上，由实际消费者补齐配对、身份迁移/重置和协议授权；不能把
  discovery 的空 pin 当成认证。
- 继续覆盖 DNS/连接失败、超时、取消、证书错误、重定向和资源释放。
- 制定 Mbed TLS 2.16.12 的升级或替代计划，包含许可证、VS2008 兼容、代码尺寸和设备验证。
- 禁止用关闭证书验证、吞掉错误或无限重试作为默认兼容策略。

### 公共 DLL 生态

- 持续检查每个子项目 README 中的 DLL 职责、调用方式和所有权示例。
- 为稳定公共 API 提供最小消费者示例和 ABI 级测试。
- 避免让示例宿主成为唯一实现或唯一文档来源。
- 当边界稳定后，再评估二进制发布、符号、版本标识和第三方集成包。

## 长期目标

Positron 最终应提供一个面向 WM6/Windows CE 的轻量应用平台，至少包括：

- 稳定的应用、窗口、导航和生命周期模型；
- 可组合的 HTML/CSS/脚本运行时；
- 有界的 fetch、文件、存储与 native bridge；
- 固定真实页面和应用流程语料；
- 可重复的内存、性能、稳定性和长时间运行基线；
- 面向其他项目的清晰 DLL 集成文档与兼容承诺。

长期目标仍受老设备内存、工具链、TLS 生态和 OEM 差异约束，不以复刻完整现代桌面浏览器为衡量标准。

## 下一批选择规则

每次只从以下顺序选择一个纵向能力：

1. 当前可重复的崩溃、数据损坏或核心流程阻塞；
2. 产品语义错误滞留在 `test_host`；
3. 固定页面/应用语料中的高价值兼容缺口；
4. 安全、生命周期或资源上限不明确；
5. 经过证据证明有价值的观感或性能优化；
6. 其他标准 API 补齐。

若候选不能说明真实消费者、所有权、失败边界和可验证结果，就不应成为下一批。

## 每批完成定义

- 纵向行为完整，且没有顺便扩大到无关重构。
- 产品语义位于正确 DLL，宿主没有新增业务所有权。
- C ABI、UTF-8、opaque handle、内存所有权和 C89/VS2008 兼容性清楚。
- 自动测试覆盖新行为及其失败/容量/销毁边界。
- 正式构建、`scripts/test_c89ize.py`、`scripts/audit_repo.py` 和与风险相称的设备门通过。
- 人工风险被明确归类：立即复核、累计复核或无需人工。
- tracked INI 回到默认自动模式；本地 `tmp/` 证据不加入 Git。
- handoff、限制和路线图只更新当前事实与未来计划。
- 只提交本批 tracked 文件并推送当前分支。

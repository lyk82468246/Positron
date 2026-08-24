# Positron 路线图

更新时间：2026-08-24

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

### 8. next614：label/control 关联纵切（已完成）

next614 从已记录的表单关系缺口出发，把 label 与控件的可复用 DOM 语义放入
`positron_core.dll`/`positron_browser.dll`：`label.control` 支持非空 `for` 指向的
input（排除 hidden）、select、textarea、button，以及没有 `for` 时的第一个嵌套控件；
这些 labelable 控件的 `labels` 返回按文档顺序的静态 NodeList snapshot。core 通过三个新的
只读 relation 值提供 control、label count 和 label-at，browser layer 负责属性和集合包装；
`test_host` 只注册既有 relation bridge。无效目标、非控件、hidden、无 ID label 和越界索引
fail closed，不把它扩展为 live labels 或完整 labelable 类型集合。

TEST1062 的 core/browser 契约门与 TEST554–561、1023–1053 相邻关系回归已通过 41/41，
证据位于 `tmp/device-runs/20260823-201832-next614-label-control-regression/`；该批没有新增
视觉、真实触摸、SIP、旋转、picker 或网络失败人工门。

### 9. next615：fieldset 有效禁用传播纵切（已完成）

next615 把 disabled ancestor fieldset 的有效状态放入 `positron_core.dll` 的统一判定，明确
第一个 legend 后代豁免，并处理嵌套 fieldset。该状态同时进入约束验证、URL-encoded 与
multipart successful controls、默认 submitter、控件信息、程序化激活和交互闸门；原始
`control.disabled` 仍只反映元素自身属性，native 窗口样式、invalid UI、SIP/IME 和文件选择器
仍由宿主负责。

TEST1063 覆盖静态和动态 `fieldset.disabled`、legend 豁免、嵌套继承、`willValidate`、控件信息
和提交结果；定向门 `TEST1063,999` 通过 2/2，相关回归 `TEST264–270、554–561、1062–1063、999`
通过 18/18，证据见 `.agents/HANDOFF.md`。这批只涉及自动可判定的 DOM/表单状态，不新增
视觉、真实触摸、SIP、旋转、picker 或网络失败人工门。

### 10. next616：HTTP(S) 深层链接 URL 解析（已完成）

next616 修正了宿主导航和子资源请求共用的 URL 解析边界。`test_host` 不再手工拼接同目录
字符串，而是让 WinINet 处理目录相对、`.`/`..`、query-only、network-path 和绝对 URL，
随后以有界 HTTP(S) parser 校验 authority、端口和路径并剥离 fragment。这样真实页面中的
主文档链接、外部 CSS 和图片引用使用同一套规则；产品 DLL 的 URL/history ABI 仍保持不变，
网络 I/O 和窗口替换继续由宿主持有。TEST1064 覆盖成功解析与 unsupported scheme、无 origin
普通相对引用的 fail-closed 边界。Debug 设备门已通过 TEST999 与 TEST43/1064/999；真实
页面门已完成 example.com 第一跳。IANA 后续跳转受外网响应影响，作为网络环境限制单列，
不替代离线契约门；Release 停滞也不替代项目既有 Debug gate。

### 11. next617：HTTP reference/Location 解析产品化（已完成）

next617 将 next616 在 `test_host` 中验证过的有界 HTTP(S) reference 解析迁入
`positron_http.dll` 的 `PHttp_ResolveReference`。页面主文档、外部 CSS/图片资源和 HTTP
GET 重定向的 `Location` 共用同一套无网络解析策略；宿主只保留 origin、窗口替换、网络
I/O 和失败提示。该 API 使用调用者提供的 UTF-8 缓冲区，支持目录相对、点段、query-only、
network-path、绝对 HTTP(S) 和 fragment stripping，并对 userinfo、IPv6、非法端口、非
HTTP(S)、无 origin 普通相对引用及容量不足 fail closed。TEST1065 是产品 DLL 的离线契约，
TEST1064 是宿主消费者回归；不新增人工视觉或输入门。
`tmp/device-runs/20260823-232147-next617-http-reference-contract-r5/device-gate-result.txt`
已记录 TEST1065/999 设备门 2/2 PASS，
无 ERROR/FAIL 且唯一 `TESTBENCH PASS`。C89、Debug/Release 构建和仓库审计均通过。

### 12. next618：WM6 native EDIT 完整 IME 结果提交（进行中）

next618 处理已观察到的 TEST65 平台缺口，而不扩张 browser composition ABI。部分 WinCE
EDIT 默认过程在 `WM_IME_COMPOSITION | GCS_RESULTSTR` 上只把候选词首字符交给 subclass；
宿主现在读取完整 `ImmGetCompositionStringW` 结果，转换为 UTF-8 后一次性以 `EM_REPLACESEL`
写入当前 composition selection，再复用既有 `EN_CHANGE` → Core value → browser input
事务。转换失败保留原 default-procedure fallback。TEST1066 用多字节多字符候选做可重复
自动断言；TEST123–125 继续覆盖 composition/InputEvent/KeyboardEvent 元数据。

自动部分已经完成：
`tmp/device-runs/20260824-105511-next618-native-ime-result-final-fixed/` 的
TEST1066、123–125、999 为 5/5 PASS、零 ERROR/FAIL、唯一 `TESTBENCH PASS`。此前阻塞自动门的
`CeRapiInitEx()` 超时已确认是 gate 的事件句柄所有权错误并已修复，不是 next618 产品失败或
需要继续恢复主机。当前只剩 TEST65 的真实 SIP 候选词人工门。

本批仍不把 OEM SIP 候选窗口、候选条视觉、真实设备字体/触摸或所有 IME 行为写成产品保证。
next618 整体完成条件现在只差：在同一构建的 TEST65 人工点选一个多字符候选词，确认输入框
一次出现完整词，并核对密码、readonly、disabled、maxlength 等相邻行为未退化。失败时先
保留 WM/Core/browser 边界，不通过重复 `WM_CHAR` 或放宽事件断言掩盖问题。

### 13. next619：native EDIT 完整 IME result 产品事务（已完成）

next619 将完整 `GCS_RESULTSTR` 的产品事件策略补入 `positron_browser.dll`，新增 additive
`PBrowser_ScriptSessionDispatchNativeEditResult()`。browser layer 要求 stable token 已有
活动 composition，校验不超过 255 字节的借用 UTF-8 result，派发
`beforeinput(insertCompositionText)` → `compositionupdate`，并把 result metadata 接入既有
native commit → input 事务；宿主仍拥有 `ImmGetCompositionStringW`、`EM_REPLACESEL`、原生
文本 mutation、WM_IME/SIP 窗口和平台视觉。

TEST1067 覆盖 NULL/未开始/已结束 result、容量边界、完整多字节 result、pending metadata、
native commit、composition end、reset 和 unregister；TEST1066、123–125、999 保持回归。
`tmp/device-runs/20260824-110948-next619-native-ime-result-transaction/` 已通过 6/6，零
ERROR/FAIL、唯一 `TESTBENCH PASS` 且 `test13_route_ok=True`。Debug/Release ARMV4I 构建、
C89 检查和仓库/文档 audit 均通过。本批没有新增视觉、触摸、旋转或 picker 人工门；next618
的 TEST65 真实 SIP 候选词仍需单独人工确认。

### 14. next620：native file-input selection 产品事务（已完成）

next620 将文件输入选择成功后的产品事件事务迁入 `positron_browser.dll`，新增 additive
`PBrowser_ScriptSessionDispatchNativeFileSelection()` 和 reset 入口。browser layer 只持有
每个 session 最多 16 个 stable token 的 BEGIN/COMMIT/CANCEL 状态，并在成功提交时严格派发
一次不可取消的 `input(insertFromFile)` → `change`；系统文件选择器、文件系统、权限、
路径写入、重绘和取消回滚仍由 `test_host`/平台宿主持有。宿主在打开 `GetOpenFileNameEx`
前 BEGIN，成功完成 `PCore_FileInputSetPath` 后 COMMIT，取消、失败或路径写入失败时 CANCEL；
没有活动脚本 session 时保留旧的无脚本 fallback。

TEST1068 覆盖 ABI 生命周期、非法 phase、重复 BEGIN/COMMIT、幂等 CANCEL、callback 错误、
reset/unregister 和 16-token 容量边界；TEST262 覆盖真实宿主文件输入回归，TEST230 与
TEST999 保持页面路由和退出提示音。`tmp/device-runs/20260824-112810-next620-native-file-selection-transaction/`
的定向设备门为 4/4 PASS、零 ERROR/FAIL；Debug/Release ARMV4I 构建、C89 检查和仓库
audit 均通过。TEST232/263 的真实文件选择、取消、权限、重入和视觉仍属于人工门，不由
该自动契约冒充覆盖。

### 15. next621：programmatic file-picker 请求仲裁（已完成）

next621 将 `file.click()` 的异步 picker 请求边界从 `test_host` 全局变量迁入
`positron_browser.dll`。新增 additive `PBrowser_ScriptSessionDispatchNativeFilePicker()`
和 reset 入口；每个脚本 session 只保留一个 pending/active request，REQUEST 合并重复
点击，OPEN/CLOSE/CANCEL 校验稳定 token 并清理生命周期。宿主仍持有 HWND、document、
file index、`PostMessage`、WM6 系统 picker、文件系统和路径写入；没有 picker handle 或
文件名进入公共 ABI。

TEST1069 覆盖非法 phase/NULL output、重复 request、错误 token、重复 OPEN、活动请求
抑制、CLOSE/CANCEL 幂等、reset 和多 session 隔离；TEST262 覆盖宿主 REQUEST→OPEN→CLOSE
接线、取消旧值保持和 stale document 回归。`tmp/device-runs/20260824-120418-next621-file-picker-arbitration/`
的定向设备门为 4/4 PASS、零 ERROR/FAIL；Debug/Release ARMV4I 构建、C89 检查和仓库
audit 均通过。本批没有新增视觉保证；TEST232/263 的真实系统对话框仍需人工验收。

### 16. next622：受信任物理锚点激活（已完成）

next622 将启用脚本时的物理链接默认动作从 `test_host` 的直接
`navigate_to` 接线迁入 `positron_browser.dll`。新增 additive
`PBrowser_ScriptSessionDispatchAnchorClick()`：browser layer 先复用可取消 click，
只有未被阻止时才以 ASSIGN 调用已注册导航适配器。宿主仍持有 `PCore_LinkAt` 命中
测试、网络 worker、窗口替换、文档生命周期和无脚本 fallback；href 只在同步调用中借用，
不把 core/link/window 类型带入公共 ABI。

TEST1070 覆盖接受、preventDefault、导航拒绝、适配器错误和宿主 helper 接线。
`tmp/device-runs/20260824-122629-next622-anchor-activation-r2/` 的定向设备门
通过 4/4（TEST1070、230、262、999），零 ERROR/FAIL，唯一 `TESTBENCH PASS`；
Debug/Release ARMV4I 构建、C89 和仓库 audit 均已通过。本批没有新增视觉保证；
程序化 anchor click、target/rel/window 和真实点击坐标仍需后续范围定义或人工观察。

### 17. next623：受信任 checkbox/radio 激活（已完成）

next623 将启用脚本时 checkbox/radio 的直接鼠标和键盘激活事务从宿主的 generic click +
即时事件接线迁入 `positron_browser.dll`。新增 additive
`PBrowser_ScriptSessionDispatchNativeToggle()` 与 reset 入口，为每个 session 保留最多 16
个 stable token 的 CLICK/COMMIT/CANCEL 状态：browser layer 先派发可取消 click，宿主执行
Core checked-state mutation 后再 COMMIT，只有状态确实变化时才派发一次不可取消的
`input` → `change`。禁用、preventDefault、取消、无状态变化、kind mismatch、回调失败和
reset 都有明确失败边界。

宿主仍拥有 hit-test、Core mutation、WM 鼠标/键盘默认动作、重绘和 label fallback；无脚本
路径保持原有 generic click 与 Core 事件。TEST1071 覆盖产品契约、状态/错误边界以及共享
session 的消费者 helper 接线。`tmp/device-runs/20260824-124858-next623-native-toggle-r5/`
的定向设备门通过 4/4（TEST1071、64、73、999），零 ERROR/FAIL，唯一 `TESTBENCH PASS`；
`python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建和
`python scripts/audit_repo.py` 均已通过，Release/Debug 只保留既有 libcss/fpmath 的 3 个
C4244 警告，产品 DLL 无新增警告。本批没有新增视觉保证；
真实触摸坐标、label 转发、OEM 控件视觉和旋转仍需累计人工观察。

### 18. next624：受信任 submit/reset 原生按钮激活（已完成）

next624 将启用脚本时 submit/reset 原生按钮的直接激活事务从宿主的 generic click +
即时 form-event 接线迁入 `positron_browser.dll`。新增 additive
`PBrowser_ScriptSessionDispatchNativeButton()` 与 reset 入口，为每个 session 保留最多 16
个 stable token 的 CLICK/COMMIT/CANCEL 状态：browser layer 先派发可取消 click，宿主在
click 回调之后查询 Core validation，再由 COMMIT 派发 submit 或 reset；CANCEL、禁用、
preventDefault、kind mismatch、回调错误、容量和 reset 都有明确边界。

宿主仍拥有 hit-test、Core validation/default action、导航、窗口、label fallback 和重绘；
无脚本路径保持原有 generic click 与宿主事件 fallback。TEST1072 覆盖产品契约、click/form
取消、无效校验、容量/生命周期边界以及共享 session 的消费者 helper 接线。
`tmp/device-runs/20260824-131847-next624-native-button-r4/` 的定向设备门通过 4/4
（TEST1072、64、73、999），零 ERROR/FAIL，唯一 `TESTBENCH PASS`；
`python scripts/test_c89ize.py`、Debug/Release ARMV4I 正式构建和
`python scripts/audit_repo.py` 均已通过，Release/Debug 只保留既有 libcss/fpmath 的 3 个
C4244 警告，产品 DLL 无新增警告。本批没有新增视觉保证；真实按钮坐标、label 转发、窗口
视觉和旋转仍需累计人工观察。

### 19. next625：受信任普通原生按钮激活（已完成）

next625 扩展 next624 的 browser-owned native button 事务，支持 Core 已识别的
`kind=9` 普通 `<button type="button">`。browser layer 对普通按钮仍先派发可取消 click，
再接受 bounded COMMIT；普通按钮不派发 submit/reset，宿主消费已接受的默认动作，不再让
generic click 后落入关闭窗口 fallback。submit/reset 的验证和 form-event 顺序保持 next624
契约不变。

TEST1073 覆盖普通按钮 click→commit、无 form 事件、无 form callback、click 取消、禁用、非法
kind 和共享 session helper；`tmp/device-runs/20260824-133502-next625-native-button-r3/` 的定向设备门
通过 5/5（TEST1073、1072、64、73、999），零 ERROR/FAIL，唯一 `TESTBENCH PASS`。
Debug/Release 正式 ARMV4I 构建、C89 和仓库/文档 audit 均已完成；普通按钮真实触摸
视觉、键盘焦点/激活和 label 转发仍保留为后续边界。

### 20. next626：受信任原生按钮键盘激活（已完成）

next626 在 `test_host` 中为 enabled 的 Core button 建立 document/index/kind 焦点状态，
把 WM `keydown`/`keyup` 交给 browser key-event callback，并在 Enter keydown、Space keyup
时复用 `PBrowser_ScriptSessionDispatchNativeButton()` 的 CLICK→COMMIT。重复 keydown 只
保持脚本可观察性，不重复 trusted activation；焦点失效、禁用、取消和 form-event policy
都不会落入关闭窗口 fallback。

TEST1074 使用真实 render window 消息队列覆盖 ordinary、submit、reset、重复键、keydown
取消、禁用焦点和窗口生命周期；`tmp/device-runs/20260824-135909-next626-native-button-keyboard-r4/`
的定向设备门通过 6/6（TEST1074、1073、1072、64、73、999），零 ERROR/FAIL，唯一
`TESTBENCH PASS`。Debug/Release ARMV4I 正式构建、C89、仓库/文档 audit 均已完成；OEM
键盘映射、焦点视觉、真实触摸和 label 的完整事务继续作为人工边界。

### 21. next627：label→native button 受信任转发（已完成）

next627 修正启用脚本时 label 命中 native button 的消费者接线：宿主仍按
`PCore_LabelTargetAt()` 取得 label/control 关系并先派发 label 自身 click，但 ordinary、
submit、reset target 不再走 generic form-button 路径，而是复用既有
`PBrowser_ScriptSessionDispatchNativeButton()` 的 CLICK→COMMIT。browser layer 因此继续持有
目标 click/form 取消与事件顺序，宿主只执行已接受的 Core 默认动作；stale 或 disabled target
fail closed，不合成目标 click，也不落入窗口关闭 fallback。

TEST1075 覆盖 ordinary、submit、reset、button click 取消、disabled 静默和 reset 值恢复；
`tmp/device-runs/20260824-141126-next627-label-button-r2/` 的定向设备门通过 5/5
（TEST1075、1074、1073、1072、999），零 ERROR/FAIL，唯一 `TESTBENCH PASS`。Debug 构建、
C89 和后续 Release/audit 必须保持既有告警基线；label 的真实触摸坐标、焦点视觉、OEM 按键
映射和其他 labelable 控件仍不由本批自动门保证。

### 22. next628：label→native toggle 受信任转发（已完成）

next628 继续补齐 label 的消费者接线：启用脚本时，label 命中 checkbox/radio 后复用
`PBrowser_ScriptSessionDispatchNativeToggle()` 的 CLICK→COMMIT，而不是先走 generic click
再由宿主直接派发 input/change。Core 仍负责 checked/radio mutation 和重绘，browser layer
继续负责目标 click 取消及一次 `input` → `change`；disabled/stale target 不合成目标 click。

TEST1076 覆盖 label→checkbox、radio 互斥、目标 click `preventDefault` 和 disabled 静默；
`tmp/device-runs/20260824-142420-next628-label-toggle-r1/` 的定向设备门通过 7/7
（TEST1076、1075、1074、1071、64、73、999），零 ERROR/FAIL，唯一 `TESTBENCH PASS`。
真实 label 触摸坐标、焦点视觉、OEM 行为以及 select/file/textarea 等其他 labelable 控件
仍保持人工/独立边界。

### 23. 建立真实页面驱动的兼容队列

在迁移工作之外，维护一个小而固定的页面/交互语料，用它选择下一项 DOM、CSS、表单或 JavaScript 能力。优先处理：

- 页面内容不可达或严重错位；
- 核心表单流程不能完成；
- 状态、事件或导航出现可重复错误；
- 当前产品边界迫使消费者复制语义。

只有不涉及上述真实缺口时，才考虑独立 Web API 补齐。

### 24. 安排新的全范围检查点

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

# `test_host`

`test_host.exe` 是 Positron 的回归宿主、设备验收程序和浏览器组合示例，不是产品
公共 DLL，也不是正式发布时的业务核心。它把 `positron_tls/json/http/image/script/
core/browser.dll` 与内部静态库、WM6 native EDIT/SELECT 控件、窗口消息、网络和
设备 gate 接起来。

浏览器脚本桥接使用 `PBrowser_ScriptSessionRegisterProgrammaticClickCallbacksEx()`。
宿主只实现 `get_target`、`validate_submit`、`perform_default` 和非表单
`dispatch_generic`：checkbox/radio/submit/reset/file 的 disabled 抑制、typed click、
submit/reset 事件顺序和验证由 `positron_browser.dll` 执行；WM 窗口、core 状态提交、
系统 picker 和导航副作用仍留在本宿主。这使 `test_host` 成为公共 ABI 的消费者/示例，
而不是程序化表单语义的所有者。

native EDIT 输入由 `PBrowser_ScriptSessionRegisterNativeEditCallbacksEx()` 接管事务策略：
宿主的 WM subclass 只拦截 beforeinput 相关消息、调用 `PCore_TextInputSetValue()` 同步值，
再把 token/几何交给 browser DLL；beforeinput 取消、pending metadata、input、dirty tracking
和 blur/change 顺序不再由宿主保存。宿主仍拥有 WM EDIT、文本 mutation、composition、SIP/IME、
焦点窗口和重绘。next618 处理 WM6 `GCS_RESULTSTR` 的平台差异：完整 UTF-8 候选词通过一次
`EM_REPLACESEL` 写入当前 composition selection，继续触发正常 `EN_CHANGE` 回写；转换失败
才回到 EDIT default procedure。TEST1056 是该公共 ABI 的 mock 契约，TEST1066 覆盖多字节
多字符结果，TEST65 仍需真实 SIP 候选词人工确认。

native SELECT 的 `input` → `change` 提交顺序由
`PBrowser_ScriptSessionRegisterNativeSelectCallbacksEx()` /
`PBrowser_ScriptSessionDispatchNativeSelectCommit()` 提供。宿主 subclass 仍处理 WM 键盘和
选择控件，成功后把 Core selection snapshot 与 token 交给 browser DLL；键盘取消仍沿 typed
key callback 的 default-allowed 返回值决定是否继续交给 WM 控件。宿主不保存 SELECT 事件
顺序状态，但负责控件销毁/重建前调用 reset。TEST1057 是 Ex ABI 契约，TEST67/71/118/999
覆盖单选、多选和键盘回归。

native SELECT 的焦点族由 `PBrowser_ScriptSessionDispatchNativeSelectFocus()` 提供。WM
`CBN/LBN_SETFOCUS` 或 `CBN/LBN_KILLFOCUS` 到达后，宿主只提交稳定 token、几何和 focused
状态；browser DLL 负责 `focus` → `focusin` / `blur` → `focusout` 顺序、重复通知幂等和
失败后的状态重试。宿主仍拥有窗口焦点、`PCore_InteractionSetAt()`、重绘、下拉展开/关闭、
键盘默认动作和 OEM SIP/IME；控件销毁/重建前继续调用 reset。TEST1058 是 ABI 契约，
TEST67/71/1057/999 覆盖相关设备回归。

单选 SELECT 的下拉事务由 `PBrowser_ScriptSessionDispatchNativeSelectInteraction()` 记录。
宿主把 `CBN_DROPDOWN`、`CBN_SELCHANGE`、`CBN_SELENDOK` 和 `CBN_SELENDCANCEL` 映射为
begin/candidate/confirm/cancel phase；候选阶段不调用 `PCore_SelectSetOptionSelected()`，
确认且 browser DLL 返回 `out_should_commit=1` 后才走既有 commit，取消则按 Core 快照调用
`CB_SETCURSEL` 回滚。多选、无脚本路径、COMBOBOX 下拉窗口/视觉、键盘默认动作、SIP/IME
和 OEM 副作用仍由宿主持有。TEST1059 是 ABI 契约，TEST67 的合成 WM 探针验证延迟 mutation、
取消回滚和确认提交。
`CBN_CLOSEUP` 只表示下拉窗口关闭，不作为事务终点；由于 WM 可能在它前后发送
`CBN_SELCHANGE`，事务只由 `CBN_SELENDOK` 或 `CBN_SELENDCANCEL` 结束。

文件输入的 picker 事务由 `PBrowser_ScriptSessionDispatchNativeFileSelection()` 提供：
宿主在 `GetOpenFileNameEx` 前提交 BEGIN，成功写入 `PCore_FileInputSetPath()` 后提交
COMMIT，取消、失败或 stale request 提交 CANCEL。browser DLL 只负责有界状态和一次
`input(insertFromFile)` → `change`；本宿主仍拥有系统对话框、文件路径、权限和重绘。
TEST1068 是产品 ABI 契约，TEST262 是自动消费者回归，TEST232/263 仍是需要真实 WM6
对话框的人工视觉/选择验收。

next621 又把 programmatic picker 的请求仲裁放入 browser DLL：本宿主在排队前发送
`PBrowser_ScriptSessionDispatchNativeFilePicker(REQUEST)`，重复点击由 browser layer
合并；处理宿主消息前发送 OPEN，模态 picker 返回后发送 CLOSE，投递失败或文档替换时
发送 CANCEL。宿主只保存 HWND、document 和 file index，仍负责 `PostMessage` 与系统
picker；TEST1069 是产品契约，TEST262 验证该消费者路径。

next622 的受信任物理链接路径在 WM6 命中 href 后调用
`PBrowser_ScriptSessionDispatchAnchorClick()`；browser DLL 负责一次 click、
preventDefault 和 ASSIGN 导航适配，宿主不再直接为启用脚本的链接调用 navigate_to。
宿主仍拥有 `PCore_LinkAt` 命中测试、网络 worker、窗口替换和无脚本 fallback。
TEST1070 同时验证产品契约与该 helper 的消费者接线。

next632 继续沿用该链接路径：fragment-only href 由 browser DLL 标记为
`PBROWSER_SCRIPT_NAVIGATION_FRAGMENT`，宿主的 navigation adapter 将 `#id` 绑定到当前页面
URL，再通过 Core 的片段查询取得已布局目标并移动自己的 scrollbar；history/hashchange、
native-child reposition 和窗口/网络副作用仍归宿主，未知目标不移动视口。跨页 href 仍走
ASSIGN。next635 又在进入 Core 前加入有界 `%HH` 解码，并让 `PCore_FragmentInfoByToken()`
按 id 优先、兼容旧式 `<a name>`；`+` 保持字面，非法编码保持原视口。TEST1080 覆盖产品
分类、Core 几何、fragment URL 绑定和目标滚动，TEST1083 覆盖 token 解析及失败边界；
target/rel/window 和真实视觉仍不由本宿主宣称支持。

next633 补齐同文档 history traversal 的宿主副作用：`history.back()`、`history.forward()`
和 `history.go()` 返回 fragment 条目后，宿主重新查询当前布局文档的目标几何并恢复 scrollbar
位置；unknown-id 保持当前位置，不启动 GET 或窗口替换。跨文档 history entry 继续进入既有
导航 worker。TEST1081 与 TEST1080、1079、1070、999 一起作为窄设备门；真实页面视觉仍需
累计人工检查。

next634 为同一宿主 history mirror 增加跨文档视口状态：当前条目在滚动或导航离开前记录
scroll offset，目标文档完成布局后，back/forward/go 恢复对应条目；新导航条目为零，短页面
按可滚动最大值裁剪。该数组只属于窗口宿主，不改变 `positron_browser.dll` 的公共 history
ABI，也不提供持久历史或跨进程恢复。TEST1082 覆盖 A→B→back/forward、new-entry zero
和短页面 clamp；与 TEST1081、1080、1079、1070、999 一起作为定向设备门。

next635 的自动门使用 `1083,1082,1081,1080,1079,1070,999`，验证产品 Core 的 id 优先、
legacy name anchor、宿主 percent-decoding 以及 malformed/unknown token 的 no-scroll
不变式。该门不替代真实页面视觉、触摸、SIP、旋转或系统 picker 人工验收。

next636 的 anchor metadata 路径使用 `PCore_LinkInfoByIdEx()`/
`PCore_LinkAtEx()` 读取 href、target、rel，并调用
`PBrowser_ScriptSessionDispatchAnchorClickEx()`；`test_host` 的 navigation adapter 只记录
并消费这些借用元数据，仍由宿主决定 URL 解析、网络、`_self`/`_blank`/named window 和
文档生命周期。`TEST1084` 覆盖 id/坐标查询、缺失/容量边界、`HTMLElement.rel`、脚本
programmatic click、metadata 传播和 preventDefault；定向门使用
`1079-1084,999`，累积回归使用 `1064-1084,999`。该自动门不宣称真实多窗口创建或视觉。

next637 复用 browser DLL 提供的 `PBrowserScriptNavigationInfo.target_kind`：当前单窗口
宿主接受 default、`_self`、`_parent`、`_top`，而对 `_blank` 与 named target 在导航回调
和窗口消息边界都 fail-closed，不把新窗口请求静默改成当前文档替换。窗口创建、复用、
跨窗口 history 和视觉仍需未来的窗口宿主；`TEST1085` 自动覆盖分类、大小写/空白、fragment
传播和拒绝策略。定向设备门使用 `1079-1085,999`，不修改 tracked INI。

next638 消费 browser DLL 的 `PBROWSER_SCRIPT_NAVIGATION_OPEN`：只有显式 `_self`、
`_parent`、`_top` 才进入当前单窗口的普通导航队列，窗口消息边界再次检查该策略；省略
target、`_blank`、不匹配的 named target 和空 URL 返回 `null`，不覆盖当前页面。宿主仍负责
URL resolver、网络请求、文档替换、窗口/控件生命周期和视觉；features 不被解释为窗口特性。
`TEST1086` 覆盖脚本 callback 的 target_kind/原始 target、注销后的 null 结果以及宿主
admission，定向设备门使用 `1080-1086,999`；本批回归门共 8/8 通过。

next639 让 `test_host` 消费 browser DLL 的 `context_name`：当前单窗口的
`window.name` 在文档替换前被快照，并在新的 script session 中恢复。`window.open(url,
当前 window.name)` 进入普通当前文档导航；其他 named target、`_blank` 和空名称仍在回调
与窗口消息边界 fail-closed。这里没有真实窗口 manager，也不创建第二个 HWND/global；
`TEST1087` 覆盖快照、恢复、匹配 named reuse 和拒绝边界，定向设备门使用
`1080-1087,999`，本批回归门共 9/9 通过。

next623 的 checkbox/radio 直接鼠标和键盘激活路径，在启用脚本且 Core 命中 toggle 时先调用
`PBrowser_ScriptSessionDispatchNativeToggle(CLICK)`；允许后宿主执行 `PCore_FormActivateAt()`，
再以 COMMIT 或 CANCEL 告知 browser DLL。产品层负责 click 取消、禁用抑制和一次
`input` → `change`，宿主仍负责命中、Core checked-state mutation、重绘、WM 默认动作以及
label fallback。无脚本路径保持原有 generic click 与宿主事件 fallback。TEST1071 验证产品
契约、无状态变化/取消/错误边界和共享 session helper 接线。

next624 的 submit/reset 原生按钮路径，在启用脚本且 Core 命中按钮时先调用
`PBrowser_ScriptSessionDispatchNativeButton(CLICK)`；click 被接受后宿主重新查询 Core
validation，再以 COMMIT 或 CANCEL 告知 browser DLL。产品层负责 click 取消、禁用抑制和
submit/reset 事件顺序，宿主仍负责 hit-test、Core validation/default action、导航、窗口、
重绘和 label fallback。无脚本路径保持原有 generic click 与宿主事件 fallback。TEST1072
验证产品契约、click/form 取消、无效校验、容量/生命周期边界和共享 session helper 接线。
next625 让同一 native button 路径也识别 Core `kind=9` 的普通
`<button type="button">`：browser DLL 仍执行 CLICK→COMMIT，但不派发 form 事件；宿主在
COMMIT 成功后消费普通按钮默认动作，避免 generic click 后关闭窗口。TEST1073 验证普通
按钮的接受、取消、禁用、非法 kind 和共享 session helper 路径；普通按钮键盘焦点/激活和
label 转发在 next625 本批仍未纳入，随后由 next626 单独补齐键盘路径。
next626 在宿主窗口增加 native button 焦点生命周期和 WM 键盘路由：enabled button 被命中
后保存 document/index/kind，Enter 在 keydown 激活，Space 在 keyup 激活，重复 keydown 只
派发脚本 key 事件而不重复 click。宿主调用既有 `PBrowser_ScriptSessionDispatchNativeButton()`
并保留 Core 坐标、默认动作和窗口生命周期；TEST1074 覆盖普通、submit/reset、取消、禁用
焦点和激活后不误关闭窗口。next626 本身不覆盖真实 OEM 按键映射、焦点视觉或 label 转发；
label→button 的自动事务见 next627，触摸和视觉仍需人工门。
next627 将 label 命中的 button 也接入这条 browser-owned 事务：label 自身 click 先由宿主
按物理坐标派发，目标 ordinary/submit/reset button 再由宿主用 control index/kind 调用
`PBrowser_ScriptSessionDispatchNativeButton()`，只有 CLICK/COMMIT 未取消才执行 Core 默认动作。
stale 或 disabled target 不合成按钮 click，也不落入关闭窗口 fallback。TEST1075 覆盖普通、
submit、reset、取消、disabled 和 reset 值恢复；真实 label 触摸坐标、焦点视觉和 OEM 行为仍
需要人工观察。
next628 将 label 命中的 checkbox/radio 也接入既有 native toggle 事务：label 自身 click
先由宿主派发，目标 control 再按 index/kind 调用
`PBrowser_ScriptSessionDispatchNativeToggle()` 的 CLICK→COMMIT；宿主调用
`PCore_FormActivateAt()` 负责 Core checked/radio mutation，browser DLL 负责取消和一次
`input` → `change`。TEST1076 覆盖 checkbox、radio 互斥、目标 click 取消和 disabled 静默；
真实 label 触摸坐标、焦点视觉、OEM 行为及其他 labelable 控件仍需人工观察。
next629 将同一 label 接线扩展到 text/password/textarea/select/file：启用脚本时，宿主先把
目标 click 交给既有 browser click adapter；目标未取消时才调用 native EDIT/SELECT 的
`SetFocus()`，或继续进入系统 file picker。目标 disabled/stale 时不派发合成 click。TEST1077
使用真实 native EDIT/SELECT 子窗口覆盖 text、textarea、select 的 click/焦点顺序、取消和
disabled 静默；系统 file picker、文件路径和真实 label 触摸仍由独立人工门负责。
next630 为脚本直接调用 `HTMLElement.click()` 的 text/password/textarea/select 增加真实宿主
接线：browser DLL 先按 target-kind 执行 disabled/typed-click 事务，宿主 bridge 保存同步的
DOM id 并按身份调用 Core 事件传播，避免 native child window 遮挡时的坐标误命中；default
callback 再对对应 EDIT/SELECT 执行焦点操作。TEST1078 在 render window 已附加真实子窗口后
验证四类控件的 click/focus/focusin 顺序、select click 取消和 disabled 静默。select 下拉
弹窗、系统 picker、SIP/IME、真实触摸和 OEM 视觉仍不由自动门保证。
next631 为脚本直接调用 `HTMLElement.click()` 的 `<a href>` 增加独立 anchor target adapter：
宿主通过 `PCore_LinkInfoById()` 按 DOM id 提供已布局几何和 UTF-8 href，browser DLL 复用
cancelable click 与 ASSIGN navigation 事务；宿主仍只拥有网络请求、窗口替换和文档生命周期。
无 href 或未知 id 回到 generic click，`preventDefault()` 或导航适配器拒绝都不产生导航。
TEST1079 覆盖真实脚本 session 的按 id 解析、容量/缺失、接受/取消导航和无 href 边界；
TEST1070 保留导航适配器拒绝契约。

主文档导航和外部 CSS/图片资源通过 `PHttp_ResolveReference` 消费
`positron_http.dll` 的统一解析策略：WinINet 负责目录相对、`.`/`..`、query-only、
network-path 和绝对 URL 的合并，产品 DLL 随后只接受有界 HTTP(S) authority、端口和路径
并移除 fragment。HTTP 3xx Location 也复用同一入口；`test_host` 只保留当前页面 origin、
窗口替换、网络 I/O 和失败提示。`positron_browser.dll` 的 URL/history ABI 不因这项实现扩张。
TEST1064 覆盖宿主消费者路径，TEST1065 直接覆盖产品 resolver 的成功、失败和容量契约。

## 构建与部署

从仓库根目录使用正式工程配置：

```bat
scripts\build.bat Debug build
scripts\stage.bat Debug C:\WMShare\Positron-manual
```

自动设备门要求 WMDC/Device Emulator GUI 中已有一个连接设备，然后运行：

```bat
scripts\device_gate.bat -Candidate nextNNN
```

程序从与 `test_host.exe` 同目录的 `test_host.ini` 读取 `auto`、`javascript` 和
测试选择。当前默认 `javascript=0`；需要人工视觉/输入验收时才临时使用 `auto=0`。
`TEST999` 是一次性完成提示音，不替代日志判门。完整操作和失败标准见
[`../docs/TESTING.md`](../docs/TESTING.md)。

## 它验证什么

- Core 的 HTML/CSS/DOM、资源、布局、绘制、命中和表单控件；
- Browser 的 history/session、脚本 bootstrap、DOM/form/event/native bridge；
- HTTP/TLS、真实页面导航、WM6 高 DPI/旋转和 native SIP/控件行为；
- 公共 DLL 的 ABI、所有权、错误映射和正式设备部署。

宿主可以保留窗口、网络、设备和 typed adapter，但不得把宿主私有函数当成业务 DLL
公共 API。新产品能力应先落在对应 `positron_*` DLL，再由本程序增加回归消费者和
设备门；`test_host/main.c` 的测试编号、设备日志和临时截图属于验收基础设施。

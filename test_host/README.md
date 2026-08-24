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

next623 的 checkbox/radio 直接鼠标和键盘激活路径，在启用脚本且 Core 命中 toggle 时先调用
`PBrowser_ScriptSessionDispatchNativeToggle(CLICK)`；允许后宿主执行 `PCore_FormActivateAt()`，
再以 COMMIT 或 CANCEL 告知 browser DLL。产品层负责 click 取消、禁用抑制和一次
`input` → `change`，宿主仍负责命中、Core checked-state mutation、重绘、WM 默认动作以及
label fallback。无脚本路径保持原有 generic click 与宿主事件 fallback。TEST1071 验证产品
契约、无状态变化/取消/错误边界和共享 session helper 接线。

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

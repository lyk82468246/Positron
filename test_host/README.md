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
焦点窗口和重绘。TEST1056 是该公共 ABI 的 mock 契约，TEST228–230/1055/999 是相关设备回归。

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

主文档导航和外部 CSS/图片资源都在宿主边界使用同一个 `resolve_url_from`：WinINet 负责
目录相对、`.`/`..`、query-only、network-path 和绝对 URL 的合并，宿主随后只接受有界
HTTP(S) authority、端口和路径并移除 fragment。`positron_browser.dll` 的 URL/history
ABI 不因这项实现扩张；窗口替换、网络 I/O 和失败提示仍属于宿主。TEST1064 是该共享解析
契约的离线消费者测试。

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

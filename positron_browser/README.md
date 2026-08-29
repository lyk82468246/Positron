# `positron_browser.dll`

`positron_browser.dll` 是无窗口的浏览器会话组合层。它拥有 history、浏览器 script session、bootstrap、DOM/Event adapter 和 native 控件事务策略，但不创建窗口、不抓取网络、不持有 Core document，也不直接操作 WM 控件。

## 产物与依赖

- 工程：`positron_browser.vcproj`
- 公共头：`positron_browser.h`
- 输出：`bin\<Configuration>\positron_browser.dll` 与 import library
- 运行时依赖：`positron_script.dll`、`positron_json.dll`
- 典型组合：宿主另行使用 `positron_core.dll`、HTTP/TLS、窗口和 native 控件

其他项目链接 `positron_browser.lib` 并部署三个 DLL。不要复制 Browser 内部结构，也不要把 `test_host.exe` 当作运行时依赖。

## 能力分组

### History

`PBrowser_History*` 管理有界的进程内条目、当前位置、state、same-document 操作和 traversal。URL/state 查询返回借用字符串，在同一 history handle 的下一次 mutation 或 destroy 后失效。

History 只决定条目语义，不请求 URL、不保存文档、不创建窗口，也不持久化到磁盘。宿主只有在页面真正提交后才应 commit 新导航；失败候选不得污染 history。

### Script session

`PBrowser_ScriptSessionCreate` 创建有预算的浏览器脚本 context，`Destroy` 释放 bootstrap、队列、native function 和事务状态。浏览器脚本使用 `positron_script.dll` 中同一 Duktape 引擎，但它的 Web host objects 由 Browser callbacks 提供。

典型生命周期：

```c
HANDLE history;
HANDLE session;

history = PBrowser_HistoryCreate();
session = PBrowser_ScriptSessionCreate(2500);
if (history == NULL || session == NULL) {
    /* handle allocation failure */
}

/* Register size-tagged callback tables, then bootstrap/evaluate scripts. */

PBrowser_ScriptSessionDestroy(session);
PBrowser_HistoryDestroy(history);
```

精确预算、返回码和 callback 结构以 `positron_browser.h` 为准。

### DOM、表单与 validation adapters

Browser 不认识 libdom 节点。宿主注册 size-tagged UTF-8 callbacks，把当前 Core document 的受限查询和 mutation 映射为：

- element/text/attribute/value/checked 读写；
- parent/child/sibling、attributes、childNodes、form owner 和 label/control 关系；
- form properties、control collection 与 selected state；
- validity、custom validity、report validity 和 validation message；
- event listener、navigation 和平台默认动作。

Browser 负责 JSON 参数解析、脚本对象形状、错误映射与同步 dispatch；Core/宿主负责真实文档状态。callback 参数和输出缓冲只在调用期间借用，不得缓存。

### `dialog` 生命周期

启用浏览器 JavaScript 后，`<dialog>` 元素提供一个有界的生命周期接口：

- `show()` 和 `showModal()` 要求元素已连接且当前未打开；同一 session 同时只允许一个由 `showModal()` 打开的对话框；
- `close(value)` 移除 `open`、更新 `returnValue` 并同步派发非冒泡的 `close`；不带参数时把返回值重置为空字符串；
- `requestClose(value)` 先派发可取消的非冒泡 `cancel`，只有未被 `preventDefault()` 阻止时才执行 `close(value)`；
- `open` 继续反映 DOM 属性，属性变化通过宿主的 DOM callback 进入正常 restyle/layout 调度，`oncancel`/`onclose` 与 `addEventListener` 均可使用。
- 宿主收到平台 Escape 等关闭手势后，可调用 `PBrowser_ScriptSessionRequestDialogClose` 将该手势交给当前 modal；有活动 modal 时即使 `cancel` 阻止关闭也会报告已处理，无活动 modal 时则由宿主决定后续行为。
- 宿主处理顺序 Tab 时，可调用 `PBrowser_ScriptSessionGetActiveDialogId` 读取当前 modal 的 UTF-8 DOM id；返回空字符串表示没有 modal。再把这个 id 交给 Core 的 `PCore_FocusTargetInfoWithin`，即可将 Tab/Shift+Tab 限定在该 dialog 子树内。该 API 只报告 Browser 状态，不改变焦点，也不创建或绘制窗口。
- 宿主处理指针时，可把 client 坐标换算为 document 坐标，用活动 id 对应的 Core 几何做有界命中测试：dialog 外部调用 `PBrowser_ScriptSessionRequestDialogClose`，内部继续交给普通控件命中；即使 `cancel` 阻止关闭，宿主也应消费这次 backdrop 点击。Browser 只提供生命周期桥，不自行读取窗口坐标。

这些方法属于 Browser 的脚本语义，不创建 HWND，也不替宿主绘制 top layer、backdrop 或系统模态窗口。宿主必须自己决定初始焦点、native HWND 切换、滚动可见性和焦点视觉；背景点击的几何命中和平台消息仍由宿主完成，`method="dialog"` 提交和跨文档 modal 生命周期仍未覆盖。

### Event 与平台事务

Typed callback families 覆盖 input、keyboard、focus、EDIT、SELECT、click、form、invalid 和 navigation。对于 native 控件，推荐使用相应的 `Ex` 注册和 transaction dispatch：

- native EDIT：beforeinput、composition/result、commit→input、dirty、blur→change；
- native SELECT：focus、key、dropdown candidate/confirm/cancel、commit→input/change；
- checkbox/radio：click、Core mutation 后的 input/change；
- button：click、validation、submit/reset/default action；
- file input：picker request/open/close/cancel 与 selection input/change；
- anchor/disclosure/programmatic click：可取消 click 与有界默认动作。

通用顺序由 Browser 决定。宿主仍拥有 WM 消息、控件窗口、Core mutation、picker、SIP/IME、HDC、网络和页面生命周期。

每个事务使用稳定非零 token，并受固定容量限制。控件销毁、文档替换或 session reset 前，宿主必须调用相应 reset/unregister 入口。stale token、非法 phase、几何变化或 adapter error 会 fail closed，不允许部分默认动作。键盘焦点顺序由 Core 的 `PCore_FocusTargetInfo`（或 modal 场景的 `PCore_FocusTargetInfoWithin`）提供；Browser 负责报告活动 modal id，并把宿主的 WM key transaction 按取消和默认动作规则分发给当前目标。

### Navigation 与 target

Browser 可把 anchor/programmatic navigation 分类为 assign、replace、fragment、reload、history traversal 或 open，并把 target 分类为默认、`_self`、`_parent`、`_top`、`_blank` 或 named。

它不解析完整 URL、不连接网络、不创建 HWND，也不决定下载和外部协议。单窗口宿主可以接受当前-context target，并对 `_blank` 或不匹配的 named target 保守返回失败。target、rel、URL 和 context name 都是同步借用快照。

### 队列与生命周期

Browser 提供受限 timer、animation frame、microtask、idle callback、message、visibility 和 page lifecycle 运行入口。队列由宿主在 UI 消息循环中按预算驱动；DLL 不建立自己的线程或无限 event loop。

页面替换时应先停止新平台回调，再清理队列和 native transaction，销毁 script session，最后释放宿主持有的 Core document。不得从 Browser callback 内重入或销毁当前 session。

## 典型 Core 组合

一个浏览器宿主通常：

1. 用 `positron_core.dll` 创建、style、layout 当前文档；
2. 为该文档构造 callback context；
3. 把 DOM/Event/form/navigation callback tables 注册到 Browser session；
4. 显式 bootstrap，并按文档顺序执行允许的 classic script；
5. 把 WM 输入转换为 Browser typed transaction；
6. 只在 Browser 允许默认动作后修改 Core/native 控件；
7. mutation 后重新 layout/paint；
8. 导航候选成功后提交 history，再销毁旧 session/document。

完整组合示例见 [`../test_host/`](../test_host/README.md)，但产品应用应根据自己的窗口、网络和安全策略实现 callbacks。

## 所有权与错误

- History 和 script-session handle 由 Browser 创建/销毁，不使用 `CloseHandle`。
- History 返回的 entry/state 字符串是借用值，调用方不得 free。
- callback table 和 `pw` 由宿主持有，必须活到 unregister 或 session destroy。
- callback 中的字符串、event info 和输出缓冲只在同步调用期间有效。
- 结构体必须设置正确 `cbSize`；较小旧结构保持兼容，未知布局应拒绝。
- `PBROWSER_OK` 以外的稳定错误区分参数、容量、origin、状态、范围和方法问题。

## 当前边界

- 浏览器 JavaScript 是显式 opt-in 的有限组合，不是完整 DOM/Web API 或安全沙箱。
- History 有界且不持久；多窗口、第二个 global、opener 和跨窗口 history 未实现。
- `dialog` 只有上述有界脚本生命周期、活动 modal id 查询、宿主驱动的 Escape→`requestClose()` 桥接和参考宿主的有界 backdrop 点击策略；宿主可组合 Core 的 scoped focus snapshot 实现顺序 Tab/Shift+Tab 焦点范围，但 Browser 不自动接管平台焦点。top-layer/backdrop 视觉、`method="dialog"` 提交和跨文档 modal 生命周期未实现。`contenteditable` 与其他浏览器焦点策略也未实现。
- 系统 picker、OEM SIP/IME、真实触摸、旋转和焦点视觉必须由宿主和设备验收。
- 公共 ABI 的精确能力、常量和结构布局只以 [`positron_browser.h`](positron_browser.h) 为准。

整体分层见 [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)，当前限制见 [`../.agents/KNOWN_LIMITATIONS.md`](../.agents/KNOWN_LIMITATIONS.md)。

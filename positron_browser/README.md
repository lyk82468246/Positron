# `positron_browser`

`positron_browser.dll` 是浏览器 session/history 与浏览器脚本 bridge 的产品组合层。
它不创建窗口、不抓取网络、不拥有 Core document，也不代替宿主处理 WM 控件；它把
浏览器状态、脚本 context、bootstrap 和稳定 typed callback ABI 组合成可复用入口。

## 输出与依赖

- 工程：`positron_browser.vcproj`
- 输出：`bin\Debug\positron_browser.dll`、对应 `.lib`
- 公共头：`positron_browser.h`
- 运行时依赖：`positron_script.dll`、`positron_json.dll`
- 典型组合：调用者另行持有 `positron_core.dll`、网络和 native WM 控件

其他项目链接 `positron_browser.lib`，部署 DLL 依赖，并以 callback 形式提供宿主的
document、DOM、navigation、event、input、keyboard、focus、EDIT change/post-change input 和 SELECT input/change 适配。
`test_host.exe` 是一个完整的组合示例，但不是私有 API 的唯一消费者。

## 其他项目如何调用

历史状态和脚本 session 是两个明确的 opaque 生命周期。脚本 session 的典型顺序是：

```c
#include "positron_browser.h"
#include "positron_script.h"  /* PSCRIPT_DEFAULT_BUDGET_MS */

HANDLE session;

session = PBrowser_ScriptSessionCreate(PSCRIPT_DEFAULT_BUDGET_MS);
if (session == NULL) {
    return 1;
}
/* 先注册调用者自己的 DOM/navigation/event/native callbacks。下面两个
 * 变量代表已填好的 PBrowserScript*Callbacks 结构体。 */
PBrowser_ScriptSessionRegisterDomReadCallbacks(session, &dom_read);
PBrowser_ScriptSessionRegisterNavigationCallbacks(session, &navigation);
PBrowser_ScriptSessionEvaluateBootstrap(session);
PBrowser_ScriptSessionEvaluate(session, "document.title", -1);
/* PBrowser_ScriptSessionGetResult/GetError 返回借用字符串。 */
PBrowser_ScriptSessionDestroy(session);
```

主要公共能力包括：

- `PBrowser_History*`：opaque history、同源判断、commit/replace、push/replaceState、
  back/forward/go 和同文档导航投影；
- `PBrowser_ScriptSession*`：创建/销毁 PScript context、求值、JSON global、bootstrap、
  DOM read/write/attribute/value/checked/form-property、navigation/location/history 事件、
  Event JSON 和 native input/keyboard/focus/EDIT change/post-change input/SELECT input/change typed dispatch；
- `PBrowser_ScriptSessionRuntime` 仅是迁移期只读诊断借用句柄，不转移所有权。

回调结构体是 size-tagged，字符串和事件信息只在同步 callback 内借用。产品层只管理
session 与脚本对象；宿主必须管理 document、窗口、网络、控件默认行为、core 事件传播
以及导航提交/回滚，并在 session 销毁前注销或保证 callback `pw` 仍有效。

## 边界与验证

浏览器 JavaScript 仍由显式开关控制，默认 `javascript=0`。本 DLL 不是完整浏览器、
不是 URL Standard parser，也不应暴露 Duktape、libdom 或窗口对象。公共 ABI 变更必须
保持 UTF-8、opaque handle、明确所有权和 VS2008/ARMV4I 兼容；修改后运行正式构建、
脚本/设备门和相应人工门。

# `test_host`

`test_host.exe` 是 Positron 的回归宿主、设备验收程序和浏览器组合示例，不是产品
公共 DLL，也不是正式发布时的业务核心。它把 `positron_tls/json/http/image/script/
core/browser.dll` 与内部静态库、WM6 native EDIT/SELECT 控件、窗口消息、网络和
设备 gate 接起来。

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

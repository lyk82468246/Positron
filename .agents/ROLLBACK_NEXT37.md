# next37 稳定性回退

更新时间：2026-07-15

## 决定

`main` 的产品源码已完整恢复到 `9c5c7c7`，并打包为 `C:\WMShare\Positron-next44`；用户已于 2026-07-15 确认 TEST13 全流程完全正常。恢复提交本身不是兼容模式或局部修补：当时 `positron_core.dll`、`test_host.exe`、`positron_http.dll` 的重编尺寸分别为 2,432,512、1,434,112、19,968 字节，与 next37 完全一致。

next37 之后的实验历史保存在远端分支 `codex/post-next37-experiments`，不丢失，但不得直接合回 `main`。

## 发生过什么

1. `c01a349` / next38：加入 stylesheet rel/type/disabled/media 语义。
2. `f8ca7ab` / next39：加入首个 `<base>`、图片 URL 别名和链接绝对化。
3. `e00251b` / next40：加入 `PHttpResponse.effective_url`、WinInet timeout options、资源总预算和加载期滚动重绘变更。
4. `1e06105` / next41：尝试非阻塞 TLS deadline。
5. next42/43 分别恢复 host/core 语义和 HTTP ABI，TEST13 仍无法完成。

这证明故障不能安全归因于单一 HTTP 改动；边界只能确定在 next37 之后。继续在主线上二分会消耗设备验收时间，因此整批挂起。

## 冻结范围

- stylesheet metadata 接入真实 Browse。
- `<base>`、图片别名和链接绝对化接入真实 Browse。
- redirect final origin / `effective_url`。
- WinInet/TLS deadline 和资源总时间预算。

重新启动任何一项时，必须在独立分支一次只改一个变量，并以 TEST13 的 `Start page -> Open example -> Learn more` 完整成功、旋转和滚动不崩溃作为合并门槛。离线测试通过不能替代该门槛。

## 接下来

- 短期：冻结 Browse 导航路径；next49 已确认静态 libjpeg-turbo 4:4:4 后端修复大面积色带，next50 已确认独立图片 DLL ABI 1.3 原始像素与 alpha 路径，next51 已确认 ABI 1.4 BMP/GIF 输出，但也证明 Shell X 只会 Smart Minimize。next52 仅把示例换成原生标题栏 OK/`IDOK` 真退出，不要求重跑 Browse，也不增加左右软键。
- 中期：完成图片 DLL 分层后，再选择一个冻结项做单变量实验；失败即放弃该实验分支。
- 长期：继续以可复用 WM 现代基础设施和 JavaScript runtime 为目标，不因本次回退改变总体方向。

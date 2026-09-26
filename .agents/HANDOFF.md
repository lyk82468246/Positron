# 当前交接

本文件只保留接管当前工作所必需的事实、证据、风险和唯一下一步。稳定能力合同见
[`docs/CAPABILITIES.md`](../docs/CAPABILITIES.md)、[`docs/TESTING.md`](../docs/TESTING.md)、
[`docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md) 及各组件 README；逐批历史不在这里重复。

## 项目使命

Positron 为 Windows Mobile 6 / Windows CE 5.2 ARMV4I 提供模块化的 TLS、JSON、HTTP、图像、
媒体、脚本、Core 与 Browser DLL，并提供正式的 `positron.exe` 消费者。公共接口保持稳定 C ABI、
UTF-8、opaque handle、固定资源预算和明确所有权。`test_host.exe` 只负责平台接线、fixture 和
断言，不拥有产品语义。

## 当前里程碑

当前中期里程碑是把 Core、Browser、HTTP/TLS 与 WM6 应用接线收束为可由真实页面驱动的有界
运行时。当前短期纵切是 HTTP(S) 导航和资源事务：地址解析、重定向后的最终 URL、旧页保留、
取消/stale 门控以及资源相对 URL 必须在公共 DLL 与正式应用中保持同一语义。

## 当前源码事实

- `positron_http.dll` 新增了 additive 的 `PHttp_ResponseGetFinalUrl()`；未改变 `PHttpResponse`
  公共结构或旧 `PHttp_Get`/`PHttp_Post`/`PHttp_ResolveReference` ABI。
- URL resolver 只接受有界、无控制字符的输入，保留显式 scheme 和非标准端口，分片不进入
  网络请求；HTTPS 不允许静默降级，也不允许重定向到 HTTP。非法 scheme、userinfo、IPv6、
  控制字符、超长输入和超限端口均 fail closed。
- TLS/WinInet body 读取对已知长度、chunked、截断、读取错误、分配失败和超过 1 MiB 的结果
  统一 fail closed；部分 body 不会交给消费者，失败响应的状态码为 0。chunk framing 缓冲也
  有独立上限。
- `positron_app` 和真实网络路径的 `test_host` 已改用 `PHttp_GetUrlEx`/`PHttp_PostUrlEx`；
  主文档与资源在成功后查询 final URL，并用它解析 CSS、图片、脚本和 `@import` 的相对引用。
  应用已移除旧的 host/path/port 二次 scheme 推断；`test_host` 保留这些字段仅用于旧 fixture
  和 ABI 回归。
- 旧页、旧资源和旧 history 在失败、取消或 stale 导航时保留；final URL 查询失败不会继续
  使用原始 URL 伪装成功。产品实现未移入 `test_host`。

## 文档与路线图

本批同步更新了 HTTP、应用、能力矩阵和测试文档。已复核 `.agents/ROADMAP.md`；当前没有由
源码、消费者或失败证据支持的新候选，因此本批不修改路线图。下一轮仍须重新复核路线图，
不得用“继续寻找”掩盖缺少证据。

## 已验证的自动证据

- `python scripts/test_c89ize.py`：通过。
- 本批 `positron_tls`/`positron_http`、`positron_app`、`test_host` 的定向 Release 正式构建：
  通过；完整 Release 增量构建报告 18/18 成功。此前完整 Debug/Release 重建也有通过记录。
- 本批源码的选定路径 `git diff --check`：通过。
- 仓库审计只剩工作区既有的 `positron_media/` 未跟踪源文件被其工程引用，以及本文件角色大小
  门；媒体工程不属于本批，角色大小问题已通过本次重写消除。审计结果不得被解释为 HTTP
  逻辑失败，也不得把媒体改动混入本批提交。

## 设备证据与限制

此前 `1064,1065,999` 窄门已通过，包含双空间预检、日志回收、crash check 和无新 dump。
本批最近一次门记录为 `tmp/device-runs/20260926-155007-http-url-aware-final`：构建、staging、
外置卡优先和空间预检均完成，但复制 `positron_script.dll` 时 WMDC/RAPI 返回 `0x80072746`，
尚未启动测试程序，因此没有新的 HTTP 产品断言。这是环境阻塞，不是回归结果。

设备纪律保持不变：用户先在 WMDC/Device Emulator GUI 手动连接恰好一个设备；gate 只复用当前
会话，不连接、选择、cradle、重置或强杀设备。外置卡 Temp 优先，内置 Temp 回退；完整回收
日志后才清理旧部署。`tmp/` 只保存本地截图、日志和设备证据。

## 当前未决边界

- 脚本自行构造的 File/Blob 尚未形成 Browser FormData 到 Core multipart 的公共转换；native
  picker 的源码接线不能写成已完成的设备上传基线。
- OEM SIP/IME、真实触摸、旋转/DPI、复杂 CSS/布局、完整现代 Web API 和浏览器安全沙箱仍受
  [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md) 约束，合成按键不能替代人工设备验收。
- 失败网络、重定向、资源相对 URL 和应用输入仍需设备门确认；稳定合同和测试矩阵见
  [`docs/TESTING.md`](../docs/TESTING.md)。

## 唯一下一步

用户确认 WMDC/Device Emulator 已重新连接恰好一个目标后，重试窄门：

```text
scripts\device_gate.bat -Candidate http-url-aware-final -Configuration Release -TestSelection "1064,1065,999" -TimeoutSeconds 300
```

完成标准是 staging 成功、三项通过、日志完整回收、`crash_check=PASS` 且无新 dump。若仍为
`RAPI=0x80072746`，保留源码和现有自动证据，报告 WMDC 环境阻塞，不继续修改 HTTP 代码。

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

当前中期里程碑仍是把 Core、Browser、HTTP/TLS、媒体与 WM6 应用接线收束为有界运行时。本批
新增的短期纵切是 `positron_media.dll`：稳定 C ABI、host-driven source/pump、FFmpeg ARMV4I
软解，以及设备接受时的 WAV PCM WaveOut native 路径。DirectShow callback source filter 和
native 视频生命周期仍是后续边界；此前 HTTP(S) 导航与资源事务的源码事实保持不变。

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
- `positron_media.dll` 新增稳定 C ABI：`pm_probe`、`pm_open/close`、`pm_pump`、暂停/恢复/停止/
  seek、stream/capability/backend/error 查询；输入由同步 `read/seek/tell/size` callback 提供，
  session 保留最多 16 MiB，回调缓冲只在同步回调期间有效，关闭后清空所有回调入口。
- WAV PCM/IMA ADPCM 走便携 C 音频路径；`AUTO` 对 WAV PCM 先尝试设备 WaveOut，并保留同步
  S16LE callback，WaveOut 拒绝时回退软件。FFmpeg 3.4.14 的固定 ARMV4I archive 通过 custom
  memory AVIO 覆盖有界 AVI/MP4/MOV/MPEG-PS/MPEG-TS/FLV/WAV/裸流及 H.264 Baseline/Main、
  MPEG-4 Part 2、MPEG-1/2、MJPEG、H.263、AAC-LC、MP2/MP3、AMR-NB/WB、PCM/IMA ADPCM。
- FFmpeg 的 255 个源文件清单、configure 生成配置、必要的 generated list 和 VS2008/WM6
  ARMV4I 重建配方已分别放入 `third_party/ffmpeg-3.4.14/` 与
  `scripts/build_ffmpeg_armv4i.bat`；配方只在 `tmp/` 工作副本中打补丁和生成中间文件，
  不联网、不改写 vendored source。最终 `positron_ffmpeg_armv4i.lib` 已因全局 `*.lib` 忽略规则
  而显式纳入版本控制，ARMV4I archive SHA-256 为
  `9a0615f9ceee423145a3495466511197af6385f61db92e8a212f80b117069797`。
- DirectShow 目前只做 WM6 graph 创建性探测；没有 callback source filter 或 native 视频 renderer
  生命周期，非 PCM WAV 的 `PMEDIA_BACKEND_NATIVE` fail closed。软视频输出只承诺 I420、默认
  640×480 上限，软音频只承诺双声道 S16LE。

## 文档与路线图

本批同步更新了媒体组件 README、架构/能力/限制/第三方说明、构建 staging 与 nightly 清单，
并复核 `.agents/ROADMAP.md`：Media 候选已改为“软解与 WAV PCM WaveOut 已接入，DirectShow
source filter/native 视频与设备门待完成”。下一轮仍须重新复核路线图，不得用“继续寻找”掩盖
缺少证据。

## 已验证的自动证据

- `python scripts/test_c89ize.py`：通过。
- `scripts/build_ffmpeg_armv4i.bat`：离线重建 255 个 FFmpeg 源对象和 runtime object 成功；
  归档 SHA-256 为 `9a0615f9ceee423145a3495466511197af6385f61db92e8a212f80b117069797`。
- `scripts\build.bat Debug` 与 `scripts\build.bat Release`：完整解决方案成功；媒体 DLL 的
  ARMV4I Debug/Release 编译、WaveOut 源文件和链接均为 0 错误、0 警告。
- `tmp\media_host_test.exe`：离线 WAV callback/暂停/恢复/seek/EOF 回归通过，输出
  `media_host_test: PASS blocks=2 samples=8 bytes=16`；该 fixture 在 `tmp/`，不属于产品工程。
- `test_host` 已按消费者边界加入 WAV PCM `pm_probe`/soft/AUTO/pause/resume/seek/EOF fixture；
  它只链接 `positron_media.lib`，不编译媒体产品源文件。
- `python scripts/audit_repo.py`：通过；项目路径的 staged/working-tree `git diff --check` 通过。
  FFmpeg 原始测试资产保留其上游空白，不为 diff 门改写；临时 `tmp/` 产物未加入仓库。

## 设备证据与限制

此前 `1064,1065,999` 窄门已通过，包含双空间预检、日志回收、crash check 和无新 dump。
本批最近一次门记录为 `tmp/device-runs/20260926-155007-http-url-aware-final`：构建、staging、
外置卡优先和空间预检均完成，但复制 `positron_script.dll` 时 WMDC/RAPI 返回 `0x80072746`，
尚未启动测试程序，因此没有新的 HTTP 产品断言。这是环境阻塞，不是回归结果。

设备纪律保持不变：用户先在 WMDC/Device Emulator GUI 手动连接恰好一个设备；gate 只复用当前
会话，不连接、选择、cradle、重置或强杀设备。外置卡 Temp 优先，内置 Temp 回退；完整回收
日志后才清理旧部署。`tmp/` 只保存本地截图、日志和设备证据。

## 当前未决边界

- `positron_media.dll` 尚未在 WM6 Emulator/真实 ARMV4I 设备上验证 WaveOut 格式接受、音频
  underrun、FFmpeg 帧率/时间戳、峰值内存和关闭耗时；DirectShow callback source filter/native
  视频播放仍未实现。AV1/HEVC/VP9、编码、DRM、字幕和直播协议明确排除。
- 脚本自行构造的 File/Blob 尚未形成 Browser FormData 到 Core multipart 的公共转换；native
  picker 的源码接线不能写成已完成的设备上传基线。
- OEM SIP/IME、真实触摸、旋转/DPI、复杂 CSS/布局、完整现代 Web API 和浏览器安全沙箱仍受
  [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md) 约束，合成按键不能替代人工设备验收。
- 失败网络、重定向、资源相对 URL 和应用输入仍需设备门确认；稳定合同和测试矩阵见
  [`docs/TESTING.md`](../docs/TESTING.md)。

## 唯一下一步

扩展 `test_host` 的媒体 fixture（损坏/截断、非 seek、`WOULD_BLOCK`、read/seek error、FFmpeg
H.264/AAC/MP3/AMR 以及 AUTO fallback），再在用户手动连接恰好一个 WM6 Emulator/真实
ARMV4I 目标后验证 WaveOut 与软解设备门。设备门必须记录启动延迟、帧率、丢帧、underrun、
峰值内存、时间戳和关闭耗时；若 RAPI 仍返回现有 `0x80072746`，保留源码与自动证据并报告
环境阻塞，不把未运行写成通过。

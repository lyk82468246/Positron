# 当前交接

本文件只保留接管当前工作所必需的事实、证据、风险和唯一下一步。稳定能力合同见
[`docs/CAPABILITIES.md`](../docs/CAPABILITIES.md)、[`docs/TESTING.md`](../docs/TESTING.md)、
[`docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md) 及各组件 README；逐批历史不在这里重复。

## 项目使命

Positron 为 Windows Mobile 6 / Windows CE 5.2 ARMV4I 提供模块化的 TLS、JSON、SQLite DB、HTTP、图像、
媒体、脚本、Core 与 Browser DLL，并提供正式的 `positron.exe` 消费者。公共接口保持稳定 C ABI、
UTF-8、opaque handle、固定资源预算和明确所有权。`test_host.exe` 只负责平台接线、fixture 和
断言，不拥有产品语义。

## 当前里程碑

当前中期里程碑仍是把 Core、Browser、HTTP/TLS、媒体与 WM6 应用接线收束为有界运行时。Core
图片 pending/retry、CSS data URI、Image 的有限 SVG 样式/渐变/alpha、HTTP URL-aware 和
Browser history/lifecycle/document click 纵切已有源码与离线/设备证据；DB 目前只有主机
contract。`positron.exe` 的网络页面、真实输入、旋转/DPI、媒体播放和 DB worker 仍须独立门。
本批新增的焦点是 WinWorld 外部 classic script 的固定内存 profile，以及设备门在必要时清理
同名 `positron.exe` 的独立、有界前置步骤；不扩大其他产品边界。

## 当前源码事实

- `positron_http.dll` 新增了 additive 的 `PHttp_ResponseGetFinalUrl()`；未改变 `PHttpResponse`
  公共结构或旧 `PHttp_Get`/`PHttp_Post`/`PHttp_ResolveReference` ABI。
- URL resolver 只接受有界、无控制字符的输入，保留显式 scheme 和非标准端口，分片不进入
  网络请求；HTTPS 不允许静默降级，也不允许重定向到 HTTP。非法 scheme、userinfo、IPv6、
  控制字符、超长输入和超限端口均 fail closed。
- TLS/WinInet body 读取对已知长度、chunked、截断、读取错误、分配失败和超过 1 MiB 的结果
  统一 fail closed；部分 body 不会交给消费者，失败响应的状态码为 0。chunk framing 缓冲也
  有独立上限。
- `positron_db.dll` 使用固定 SQLite 3.53.4，公开稳定 C ABI 不暴露 `sqlite3*`；本地完整 SQL
  与同步数据库模式均为单 owner thread、无 DLL 内线程。同步模式通过版本化 migration、注册
  表触发器、`__pdb_row_state`/`__pdb_dirty`/`__pdb_outbox`/`__pdb_conflict` 保存离线状态，
  v1 只接受单列 INTEGER/TEXT 主键并以 typed row JSON 编码，不向服务器发送 SQL。
- DB worker 仍属于宿主：宿主从 `PDb_SyncBuildRequest()` 取得有界 body，经 `positron_http.dll`
  发送 HTTPS + Bearer Token，再把 2xx 响应交给 `PDb_SyncApplyResponse()`；非 2xx、超限、
  malformed 或 schema mismatch 不推进本地状态。TEST1321 覆盖本地/同步 contract；独立 REST
  fixture 已加入 `scripts/db_sync_fixture.py`，宿主 worker、真实 401/5xx/分页和设备 journal/断电恢复尚未完成。
- `positron_app` 和真实网络路径的 `test_host` 已改用 `PHttp_GetUrlEx`/`PHttp_PostUrlEx`；
  主文档与资源在成功后查询 final URL，并用它解析 CSS、图片、脚本和 `@import` 的相对引用。
  应用已移除旧的 host/path/port 二次 scheme 推断；`test_host` 保留这些字段仅用于旧 fixture
  和 ABI 回归。
- 设备门默认不结束设备进程；显式传入 `-ForceTerminatePositron` 时，先运行
  `device_tools/positron_process_cleanup.exe`。该 helper 不链接任何 Positron DLL，只精确匹配
  `positron.exe` 和本设备门生成的 `test_host-run-*`，记录 PID/结果摘要；摘要缺失或失败会
  阻断宿主启动。宿主随后再次记录同一精确检查，避免把模块持有者诊断误写成仍有应用进程。
- 旧页、旧资源和旧 history 在失败、取消或 stale 导航时保留；final URL 查询失败不会继续
  使用原始 URL 伪装成功。产品实现未移入 `test_host`。
- 当前工作树在已验证的导航启动路径上重新实现了 EXE 私有 UI 快照回滚：每个候选在显示
  Loading/候选地址前保存已提交地址和窗口标题；当前候选失败时恢复快照，stale 候选不触碰
  当前 UI，连续导航从仍在运行候选的快照继承而不会把 Loading 状态当成旧页。该替代实现
  尚未设备验收，不能覆盖此前已撤回的候选 UI 控制流。
- 本轮完成了 `positron_core` 的异步图片 pending/retry 契约，并保留此前 `positron_app` 的导航接线：异步脚本、样式和图片资源等待现在分别保存并恢复
  对应的 commit stage；图片 pending 会按 `test_host` TEST13 的顺序回到 STYLE，再重新扫描可选
  图片，避免 worker 完成后继续停在错误的阶段。EXE 另加入 image-state 调试摘要，记录 Core
  扫描、Browser 资源终态、Core box/image 统计和 SVG 创建计数；图片 worker 现在也会像 TEST13
  一样先把 Core 给出的原始引用解析到文档最终 URL，再交给 HTTP。Core 新增 additive 的
  `PCore_FetchImageResourcesEx()` 与 READY/PENDING/TERMINAL_FAIL 状态；旧的同步入口和
  `PCoreFetchFn` ABI 不变。PENDING 不再写入终态失败 cache，后续扫描会重新调用 callback。
  `test_host` 新增 TEST1313 离线回归，证明 pending SVG 第二次扫描成功解码、生成 image box，
  而终态失败不重试。图片在设备上可见性仍未形成设备证据。
- 本批新增了 Core-owned CSS data URI image path：`data:image/svg+xml,` 的 percent-encoded
  和 Base64 body 在资源发现阶段分别解码，写入同一份 document image cache，再复用
  `positron_image.dll` 和既有 background paint；`positron_app`、HTTP 接线和公共 ABI 均未改动。
  URL、decoded bytes、SVG 元素/path 采用独立预算，坏 MIME/编码、控制字符和超限内容 fail
  closed，不调用宿主 HTTP callback。TEST1318 的离线 CSS→Core→Image→GDI fixture 同时覆盖
  两种编码、损坏 URI 和 65-path 复杂度超限；`tmp/device-runs/20260929-094957-css-data-uri-complexity-20260929`
  的 `1318,999` Debug ARMV4I 设备门已通过，日志完整、Core module 匹配、双空间预检通过且无新增 dump。
- `positron_image.dll` 的 `PImage_CreateSvgFromMemory()` 增加了有界、class-only 的 `<style>`
  兼容预处理：简单 `.name { paint-property: value; }` 规则会合并为行内 style，再交给
  libsvgtiny；不实现通用 CSS cascade，也不改变任何公开 ABI。它覆盖 IANA 首页/页眉 SVG
  使用的 class fill、渐变引用、viewBox/viewport 和重复 retained draw；超预算、复杂选择器或
  无法安全规范化的输入 fail closed。对于调用者传入 `viewport_w/viewport_h <= 0` 的 viewBox-only
  SVG，Image DLL 只扫描根 viewBox；两边 viewport 都省略时用圆整 viewBox 宽高作为自然画布，
  只省略一边时按比例补齐，非法或超预算 viewBox 仍回退 300x150，旧 ABI 和显式 viewport 行为
  不变。`test_host` TEST1314 的诊断先复现了两个 SVG 全黑，修复后采样到绿色/蓝色/非黑渐变且
  坏 SVG 句柄为空；TEST1315/1317 还要求真实 IANA 文件的 `450x175`/`128x50` 自然尺寸和
  Core/Image 直接绘制像素一致，定位默认重复背景的尺寸缺口属于 Core 重复绘制链而非
  HTTP/应用接线；Core 现在会把 SVG 的 CSS 像素 tile 按活动设备 DPI 转为物理重复尺寸，
  不改变非重复图片或 Image DLL 的自然尺寸。
- `positron_image.dll` 现还在 libsvgtiny 的 bounded paint state 中保留 `stroke_opacity` 与
  `stroke_linecap`：直接属性和 inline/class style 都可解析受限 `rgba(r,g,b,a)`、
  `stroke-opacity` 以及 `butt/round/square`，超出颜色/alpha 范围保持原状态并 fail closed。
  `pimage_raster.cpp` 把 stroke alpha 写入 NanoSVG paint，利用既有 premultiplied GDI
  `AlphaBlend` 合成；不改变 `PImage_*` 公共 ABI。TEST1319 使用精确 WinWorld 30x30 fixture，
  检查三条半透明灰线、round cap 延伸和端点外白像素，已在匹配 Debug ARMV4I 包上通过。
- `positron.exe` 的 retained-pixel 滚动、页面裁剪、动态 scrollbar 和 nested overflow pointer
  接线已经通过 C89/Debug 代码门；它们仍需要用户在匹配新进程上观察闪屏、横向拖动、旋转和
  DPI。历史部署目录、PID 和 SHA-256 只保存在 `tmp/device-runs/`，不在此重复。
- `positron.exe` 现在补齐了 `test_host` TEST42 的 nested retained-overflow 输入接线：页面窗口
  先把 WM6 的按下坐标换算为 Core document 坐标并调用 `PCore_OverflowPointer(DOWN)`，随后用
  `SetCapture` 将 MOVE/UP 保持在同一窗口；每次 Core pointer 更新后读取
  `PCore_OverflowDirtyRect` 做局部失效，并通过 EXE 私有 `AppScript_NotifyElementScroll`
  转发 `PCore_OverflowScrollSnapshot` 到 Browser 的 `scrollLeft`/`scrollTop` 状态。Core 继续
  拥有命中、拖动和滚动条几何，EXE 不自绘滚动条、不重做布局，也没有修改公共 ABI。该路径已
  通过 C89、仓库审计和 Debug 编译；设备上的表格横向拖动仍待关闭旧进程后的人工验收。
- 最新截图中的地址栏 stale 空隙已在 `positron_app/main.c` 移除 EXE inset：原生 EDIT 现在从
  客户区 x=0 铺满宽度，显式使用 `SYSTEM_FONT`，并从 `WM_GETFONT`/`GetTextMetrics().tmHeight`
  读取实际字体高度，并把客户区行高和少量 DPI 缩放 padding 通过 `AdjustWindowRectEx` 换算为
  native EDIT 外框高度，不再把固定的 28 个逻辑像素直接放大；文本格式矩形仍由系统控件管理。
  截图中绿色箭头对应的 footer 覆盖则定位为 Core NetSurf 纵向 auto-height flex 的父高度塌陷；
  `layout_flex.c` 已按实际 `pos_main` 修正，TEST39 增加离线 section/footer 几何断言。两项及
  本轮字体度量修正均已通过 C89、审计和 Debug/Release 构建；最新 Debug 完整包已随设备门部署，
  用户已确认地址栏字体和外框视觉符合预期，但完整网络页面仍缺少应用视觉证据。针对最新截图的修正版已经移除 `EM_GETRECT`/
  `EM_SETRECTNP` 内部上移操作，改为用 `AdjustWindowRectEx` 把客户区行高转换为 native EDIT 外框；
  该版本已进入最新远端包；设备门只证明包完整、Core 模块匹配和启动回归通过，网络页面的资源、脚本
  和滚动视觉仍需单独验收。
- 最新 WinWorld 截图确认页眉中的 30x30 CSS/SVG 图标属于 Core 绘制的普通 `button`，而不是独立
  的 SVG 控件。EXE 的指针路径现与 `test_host` 对齐：按下先设置 Core 的 `FOCUS|ACTIVE`，按钮
  的 focus/focusin 仅负责焦点状态和视觉反馈，即使焦点事件投影失败也不会吞掉可信的
  Browser native-button click transaction；抬起时清除 `ACTIVE`。没有加入 WinWorld 特判或新的
  公共 ABI。
- 该 EXE 修正版复测后菜单仍无变化。`tmp/device-runs/20260929-112947-button-native-contract-20260929`
  的 ARMV4I gate 中 TEST1073/999 均为 OK、无 ERROR/FAIL，但整体结果被
  `core_module_check=STALE_MODULE` 拒绝：设备 PID `2733577874` 的旧 `positron.exe` 仍持有
  `\\Storage Card\\Temp\\Positron-device-gate\\button-click-20260929\\positron_core.dll`，
  因此不能把这次 gate 当作当前 EXE+DLL 的匹配设备证据。
- 源码审计确认普通 `document` click listener 的缺口属于 Browser/ScriptSession，而不是 EXE
  的 SVG 命中。当前批次已在 `positron_browser` 增加独立的 bounded document delegated-click
  bootstrap，在 Core 复用 `PCORE_DOCUMENT_ELEMENT_TOKEN`；TEST1320 覆盖普通 button 的
  target/currentTarget、capture/bubble、class/aria mutation、重复注册、移除、once 和 64 项
  listener 预算。没有加入 WinWorld 特判、没有提高脚本预算，也没有修改公共 Browser ABI。
- `tmp/device-runs/20260929-132600-next1320-document-click-final2` 已用同一 Debug staging 在
  当前唯一 ARMV4I 目标上完成 `1320,999` 设备门。日志显示 `core_module_check=PASS`、
  `TEST1320 OK`、`TEST999 OK`、selected/observed `2/2`、唯一 `TESTBENCH PASS`、零
  ERROR/FAIL、`crash_check=PASS`、无新增 dump，外置卡和内部对象存储预检均通过；完整日志已
  回收，远端当前部署目录已在回收后清理。第一次最终尝试只有 fixture PROBE 多写了一个分隔符，
  已在本次通过前修正；此前旧进程/旧 DLL 的日志不作为证据。
- 为诊断 WinWorld 外部 Bootstrap 脚本状态，`positron_app/app_script.c` 现在增加了 EXE 私有的
  Debug-only 脚本逐项日志和汇总日志：记录文档、脚本 URL、类型、可用性、各类字节数以及
  inspect/fetch metadata/ignored/session/runtime/executed 终态；`main.c` 现有 image-state
  摘要也由同一 `_DEBUG` 边界保护。Release 预处理路径不包含这些日志函数、调用和缓冲区，
  不改变公共 ABI、Browser/Core 或 `test_host`。WM6 目标使用已验证可链接的
  `OutputDebugStringW` 输出，避免依赖不存在的 `OutputDebugStringA` coredll 导出。
- 最新 Debug 完整包已由 `scripts\stage.bat Debug` 生成并部署到
  `\Storage Card\Temp\Positron-device-gate\debug-script-diagnostics-20260929`，19/19 文件复制
  成功，设备返回 `positron.exe` PID `2522227582`。远端回读的 `positron.exe`、`positron_core.dll`
  和 `positron_browser.dll` SHA-256 均与本地 stage 一致；该目录保留供用户在同一实例中打开
  WinWorld 并采集 Debug 脚本诊断，目前尚无页面脚本状态的人工结果。
- `positron_script.dll` 的 source 上限为 128 KiB，独立 context 默认 heap 为 512 KiB。Browser
  旧 `PBrowser_ScriptSessionCreate()` 仍为 1.5 MiB；新增 `PBrowser_ScriptSessionCreateEx()`
  与 Browser-owned GC/heap telemetry 入口，固定允许范围为 1.5–3 MiB。`positron_app` 选择
  3 MiB profile，并在每个 classic script 前回收短命对象；未提供无界 profile。
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

本轮复核了 `.agents/ROADMAP.md`。IANA class-style、viewBox 固有比例、Core 高 DPI 重复背景
tile、WinWorld rgba/round-cap Image 纵切以及 document delegated-click 合同均已完成；
`20260929-132600-next1320-document-click-final2` 已取得匹配 DLL 的 ARMV4I
`1320,999` 设备门通过证据。阶段 B 候选继续只保留 `positron.exe` 应用图片可见性、滚动/旋转/DPI、
普通按钮脚本反馈人工门、外部 Bootstrap 脚本状态和原有 Media 未完成边界。新增的 EXE 动态顶层
滚动条接线尚未设备验收，不得把离线 decode、Core 背景门或桌面构建证据写成真实应用视觉通过。

## 已验证的自动证据

- `python scripts/test_c89ize.py`：通过。
- `scripts/build_ffmpeg_armv4i.bat`：离线重建 255 个 FFmpeg 源对象和 runtime object 成功；
  归档 SHA-256 为 `9a0615f9ceee423145a3495466511197af6385f61db92e8a212f80b117069797`。
- `scripts\build.bat Debug rebuild` 与 `scripts\build.bat Release rebuild`：完整解决方案各 18
  个项目成功；`positron_app` 的本轮导航、retained-pixel 滚动接线、资源编译和链接均为 0
  错误、0 警告。第三方
  libcss/libsvgtiny/Core 的既有转换警告仍存在，但未产生构建错误。
- `tmp\media_host_test.exe`：离线 WAV callback/暂停/恢复/seek/EOF 回归通过，输出
  `media_host_test: PASS blocks=2 samples=8 bytes=16`；该 fixture 在 `tmp/`，不属于产品工程。
- `test_host` 已按消费者边界加入 WAV PCM `pm_probe`/soft/AUTO/pause/resume/seek/EOF fixture；
  它只链接 `positron_media.lib`，不编译媒体产品源文件；TEST1312 现在显式写出成功/失败标记，
  供设备门验证选中的断言确实执行。
- `python scripts/audit_repo.py`：通过；项目路径的 staged/working-tree `git diff --check` 通过。
  FFmpeg 原始测试资产保留其上游空白，不为 diff 门改写；临时 `tmp/` 产物未加入仓库。
- 本轮 EXE 滚动条修订的 `python scripts/test_c89ize.py` 与 `python scripts/audit_repo.py`
  均通过；`scripts\build.bat Debug build` 的 Core、应用和 test_host 均为 0 错误（Core 保留
  3 个既有 libcss 转换警告）。`scripts\build.bat Release build` 中三个 ARMV4I 工程也均为
  0 错误，但 `positron_cab` 的 CabWiz 在生成数据文件时失败；该失败不影响已生成的 Release
  `positron.exe`，但本批不把完整 Release 解决方案记为通过。
- 本轮 EXE Debug-only 诊断改动的 `python scripts/test_c89ize.py` 已通过；修订后的
  `scripts\build.bat Debug build` 中 `positron_app` 为 0 错误、0 警告，`Release build` 中
  `positron_app` 同样为 0 错误、0 警告。完整 Release 命令仍只在既有 `positron_cab` CabWiz
  阶段失败；该失败与诊断条件编译无关，未把完整 Release 解决方案写成通过。Debug/Release
  二进制字符串检查分别确认 `script-state` 与 UTF-16 `image-state` 诊断字符串只存在于
  Debug `positron.exe`，Release 均不存在。
- 提交 `d4f17d28` 的 `app_script.c/.h` 与 `main.c` 通过 `python scripts/test_c89ize.py`、
  `python scripts/audit_repo.py` 和 `scripts\build.bat Debug build`；`positron_app` 为 0 错误、
  0 警告。随后独立尝试的完整 Release rebuild 触发了 VS2008 并行 PDB/CABWiz 工具链竞争，
  `positron_core`/依赖库未能完成，不能把这次 Release 命令记为通过，也没有用它生成设备包。
- `test_host` TEST1313：源码已加入 Debug/Release 正式工程，离线 fixture 覆盖首次 PENDING、
  第二次 READY+SVG decode/layout 和 terminal failure 不重试；本批
  `tmp/device-runs/20260927-165241-image-pending-retry` 在 Microsoft DeviceEmulator 上以
  `1313,999` 定向门通过，selected/observed `2/2`、零 ERROR/FAIL、唯一 `TESTBENCH PASS`、
  `core_module_check=PASS`、双空间预检通过且无新增 dump。
- `test_host` TEST1314：直接调用 `PImage_CreateSvgFromMemory()`/`PImage_DrawSvg()`，覆盖 IANA
  风格 class paint、渐变、viewBox/viewport、无显式尺寸、重复绘制和坏 SVG 句柄；诊断门先在
  `tmp/device-runs/20260927-185713-image-iana-diagnostic` 复现 class 样式导致的全黑，再由
  `tmp/device-runs/20260927-190652-image-iana-final` 以 `1314,999` 通过，selected/observed
  `2/2`、零 ERROR/FAIL、唯一 `TESTBENCH PASS`、双空间预检通过、`core_module_check=PASS`、
  `crash_check=PASS` 且无新增 dump。
- 随后的匹配 DLL 定向回归 `tmp/device-runs/20260927-191419-image-iana-regression` 以
  `13,1314,999` 通过，selected/observed `3/3`、`test13_route_ok=True`、零 ERROR/FAIL、
  `TESTBENCH PASS`、双空间预检通过、`core_module_check=PASS`、`crash_check=PASS` 且无新增
  dump；这证明 IANA 子页导航和 Image 直接绘制门没有回归，但仍不等于首页 CSS background
  在真实 `positron.exe` 页面上的视觉验收。
- 上一轮 `tmp/device-runs/20260927-224216-iana-svg-intrinsic-20260927b` 以同一批 Debug
  ARMV4I DLL 运行 `1315-1317,999`：四项均为 OK，selected/observed `4/4`，零 ERROR/FAIL，
  唯一 `TESTBENCH PASS`、双空间预检、`core_module_check=PASS`、`crash_check=PASS` 且无新增
  dump；它确认当时的 300x117 比例回退、真实完整 SVG 背景适配和响应式 header 选择，但最新
  截图证明该尺寸仍会在默认重复背景中裁成残片，因此不能作为本轮自然尺寸修复的证据。
- 本轮 `tmp/device-runs/20260928-095531-iana-svg-natural-core-20260928` 以匹配的 Debug
  ARMV4I DLL 运行 `1315-1317,999`：四项均为 OK，selected/observed `4/4`，零 ERROR/FAIL，
  唯一 `TESTBENCH PASS`、双空间预检、`core_module_check=PASS`、`crash_check=PASS` 且无新增
  dump。TEST1317 将 128x50 CSS 像素的 header tile 与 128 DPI 下的 171x67 物理布局逐像素
  对照，证明 Core 重复 SVG 背景已经使用设备 DPI；这仍是 test_host 的自动纵切，不替代
  `positron.exe` 的真实页面截图。
- 本轮 `tmp/device-runs/20260929-103635-next1319gate` 以当前 Debug ARMV4I Image/Core/宿主
  匹配包运行 `1319,999`：TEST1319 精确 rgba stroke/round-cap 像素回归和完成提示均为 OK，
  selected/observed 为 `2/2`，唯一 `TESTBENCH PASS`，零 ERROR/FAIL、双空间预检通过、
  `core_module_check=PASS`、`crash_check=PASS` 且无新增 dump。部署日志完整回收后按策略删除当前
  远端目录；这证明 DLL/Image raster 路径，不替代最终 `positron.exe` WinWorld 页面视觉观察。
- 本批 TEST1320 离线 fixture 已加入正式工程，并随 `python scripts/test_c89ize.py`、
  `python scripts/audit_repo.py` 和正式 `scripts\build.bat Debug build` 通过静态/编译门；
  Browser/Core/test_host 均为 0 错误。匹配 Debug ARMV4I 门已在
  `tmp/device-runs/20260929-132600-next1320-document-click-final2` 通过，覆盖
  `document` capture/bubble、target/currentTarget、collapse mutation、重复/移除/once 和
  64 listener 预算，并完成日志回收、双空间预检和 crash 检查。
- `scripts\build.bat Release build` 本批的 Browser/Core/app/test_host 均完成（Core/test_host
  保留 3 个既有 libcss 转换警告），但解决方案的既有 `positron_cab` CabWiz 报
  `Data files could not be created`，因此不能把完整 Release 解决方案写成通过；该工具链失败
  与本批 Browser/Core 改动无关。
- TEST1322 已通过匹配 Debug ARMV4I 包的定向门：`tmp/device-runs/20260929-221805-next1322-script-heap`
  中 `1322,999` 为 selected/observed `2/2`，唯一 `TESTBENCH PASS`，零 ERROR/FAIL，
  `core_module_check=PASS`、`crash_check=PASS`、无新增 dump。门同时验证旧 1.5 MiB wrapper、
  3 MiB Ex profile、非法范围 fail closed、bootstrap 后约 120 KiB retained script 和 GC。
- 本轮设备门功能回归 `tmp/device-runs/20260930-103934-force-cleanup` 使用
  `-ForceTerminatePositron -TestSelection "1319,999"` 通过：独立 helper 的
  `process-cleanup.log` 为 `target_count=0 failed=0`，说明重启后的目标上没有精确匹配的
  `positron.exe`；宿主再次记录 `force_termination_check=PASS`，TEST1319/999 为 `2/2`，
  `core_module_check=PASS`、`crash_check=PASS`、零 ERROR/FAIL，日志完整回收并清理当前目录。
- 同一清理路径的复跑 `tmp/device-runs/20260930-110654-force-cleanup-prefix2` 也通过：
  前置 helper 与宿主内置检查均为 `target_count=0 failed=0`/`PASS`，覆盖
  `positron.exe` 以及本设备门生成的 `test_host-run-*` 精确匹配；TEST1319/999 为 `2/2`，
  `core_module_check=PASS`、`crash_check=PASS`、零 ERROR/FAIL，日志完整回收并清理当前目录。

## 设备证据与限制

此前 `1064,1065,999` 窄门已通过，包含双空间预检、日志回收、crash check 和无新 dump。
本批 `tmp/device-runs/20260927-130307-media-host-smoke` 通过 `1312,999` 定向门：完整日志包含
`TEST 1312 OK`、`TEST 999 OK` 和唯一 `TESTBENCH PASS`；selected/observed 为 `2/2`，
`ERROR`/`FAIL` 为 0，`crash_check=PASS` 且新增 dump 为 0。该门验证了 WM6 Emulator 上的 WAV
PCM soft callback、暂停/恢复、seek/EOF、AUTO backend 选择和 session 生命周期；不等同于
WaveOut underrun、FFmpeg 视频/压缩音频实时播放或真实设备验收。
更换仿真器后的 HTTP/TLS 重试记录在 `tmp/device-runs/20260927-145748-tls-network-retry`：
`TEST1,3-5,999` 全部通过，`TEST3` HTTPS GET、`TEST4` HTTPS POST 和 `TEST5` 的有效证书接受、
过期/自签名证书拒绝均有完整日志；`selected/observed=5/5`、`ERROR`/`FAIL` 为 0、唯一
`TESTBENCH PASS`、`core_module_check=PASS`、`crash_check=PASS` 且无新增 dump。该目标为
320x320、DPI 128 的新 WM6 Emulator，外置卡部署和双空间预检均通过；先前仿真器的
`WSA=10061` 是设备网络环境问题，不再作为当前 HTTP/TLS 基线。
本批 `tmp/device-runs/20260927-165241-image-pending-retry` 同样复用该 320x320、DPI 128
目标和外置卡 Temp；`TEST1313` 的图片 pending/retry/terminal-failure 合同通过，日志完整
回收后远端当前目录删除。该门尚未证明 `positron.exe` 真实 IANA 页面上的网络图片可见性。
本轮 `tmp/device-runs/20260927-172004-image-exe-test` 用 Debug 完整包重新部署同一目标，
`TEST1313,999` 通过，`core_module_check=PASS`、`crash_check=PASS`、新增 dump 为 0；
远端保留在 `\Storage Card\Temp\Positron-device-gate\image-exe-test-20260927-172004`，
并已从该目录启动 `positron.exe`。当前只有启动和无崩溃证据，尚未取得 EXE 页面截图或
`image-state` 摘要，因此不能把真实网络图片写成已验收。
本轮 retained-pixel 滚动修正已在移除上一轮 `svg-scroll*` 部署后，随 Debug 完整包重新部署到同一
320x320、DPI 128 目标：最新远端目录为
`\Storage Card\Temp\Positron-device-gate\svg-scroll-final-20260927-20260927-213538`，共复制 19 个
运行时/字体/fixture/config 文件；本地证据为
`tmp/device-runs/20260927-213538-svg-scroll-final-20260927/device-gate-result.txt`。最小
`test_host` `999` 门的 selected/observed 为 `1/1`，`core_module_check=PASS`、`crash_check=PASS`、
新增 dump 为 0，部署目录已保留。设备 gate 对缺少完整日志或非 gate 命名的历史诊断目录仍按安全
策略保留；它们不参与新包运行。该包包含页面子窗口 `WS_CLIPCHILDREN` 和 `PAINTSTRUCT.rcPaint`
绘制边界；其后的工作树版本还包含页面脏区双缓冲，但尚未部署。部署后 `positron.exe` 的
SVG 可见性和 retained-pixel 滚动仍待用户手测：长页面连续拖动时应无整页闪烁/重排、暴露
区域应正常补绘、native 控件应保持相对位置；旋转或真实 viewport 改变仍应触发 layout。

本轮地址栏字体度量修正版 Debug 包随后部署为
`\Storage Card\Temp\Positron-device-gate\address-font-20260928-20260928-141835`，
完整复制 19/19 文件并保留远端目录；设备门只启动了同包的 `test_host` `TEST999`，没有替代
用户对 `positron.exe` 的手动视觉验收。

本轮最新地址栏外框换算修正 Debug 包已部署为
`\Storage Card\Temp\Positron-device-gate\address-outer-20260928-20260928-201447`，
完整复制 19/19 文件；本地证据为
`tmp/device-runs/20260928-201447-address-outer-20260928/device-gate-result.txt`，EXE 与本地
`positron_app/bin/Debug/positron.exe` SHA-256 均为
`EDC5694CC55413D23318FEF77DE3DABF9D2DA1C539E467977B6B78D60961165B`。设备门 `TEST999`
selected/observed 为 `1/1`、`core_module_check=PASS`、`crash_check=PASS`、新增 dump 为 0；
用户已在设备上手动启动该目录中的 `positron.exe` 并确认文本上下边界符合预期；本轮开始前工作区已
推送干净，没有需要代为保留的并行改动。本轮 Debug/Release 正式增量构建均已成功编译并链接
`positron_app`。

本轮诊断曾临时构建并部署 candidate UI 版本；用户确认它会使地址栏回车导航失去反应，
因此该未提交源码已恢复为 `9ec7d36a` 的已验证行为。设备上的 `TEST1`、真实 HTTPS
`TEST3` 以及 URL 合同 `TEST1064/1065` 均通过，说明 HTTP/TLS 和公共 URL resolver 本身
仍可工作。恢复版 Debug 完整包随后重新部署到
`\Storage Card\Temp\Positron-device-gate\app-baseline-redeploy-20260928-20260928-210918`，
本地证据为 `tmp/device-runs/20260928-210918-app-baseline-redeploy-20260928/device-gate-result.txt`；
19/19 文件复制完成，`TEST999` selected/observed 为 `1/1`，`core_module_check=PASS`、
`crash_check=PASS`、新增 dump 为 0。该目录才是当前应启动的 `positron.exe` 包。

随后重试的替代实现没有复用上述失效控制流：它保留 Loading、候选地址和 worker 启动顺序，
只增加候选级已提交 UI 快照，并在当前候选失败时恢复地址和窗口标题。该实现已通过
`python scripts/test_c89ize.py`、`python scripts/audit_repo.py` 和 Debug ARMV4I 编译；
Release 之前已确认 `positron_app` 编译链接为 0 错误/0 警告，但完整动作仍在 CabWiz
`Data files could not be created` 处失败。随后 Debug 完整包已部署到
`\\Storage Card\\Temp\\Positron-device-gate\\app-ui-snapshot-20260928-20260928-213123`；
19/19 文件复制完成，`TEST999` selected/observed 为 `1/1`，`test13_route_ok=True`、
`core_module_check=PASS`、`crash_check=PASS`、新增 dump 为 0。该目录等待
`positron.exe` 的网络页面人工验收，不能与已撤回的旧 candidate UI 包混用。

本轮页面脏区兼容位图/单次 `BitBlt` 修正版已用正式 `scripts\stage.bat Debug` 生成完整本地包，
stage 为 `tmp/device-runs/20260928-222355-app-scroll-buffer-deploy/stage`；增量 Debug 构建报告
18 个工程为最新，随后通过 32 位 WMDC RAPI 复制 19/19 个运行时、字体、fixture 和配置文件到
`\Storage Card\Temp\Positron-device-gate\app-scroll-buffer-20260928`，并从该目录启动
`positron.exe`（设备返回 PID `586819102`）。这次是 standalone EXE 部署，不是 `test_host` 设备门，
因此尚没有 Core module log 或页面视觉通过证据；设备任务管理器不会被脚本强杀，若仍显示旧窗口，
必须先真正退出旧 `positron.exe`，再从上述新目录启动后验收滚动闪屏/撕裂。

本轮 `d4f17d28` 的 Debug 完整包已复制到
`\Storage Card\Temp\Positron-device-gate\app-overflow-pointer-20260928-20260928-214547`，
本地证据为 `tmp/device-runs/20260928-214547-app-overflow-pointer-20260928/`；19/19 文件复制完成，
`TEST999` 本身为 `1/1`、日志完整、`crash_check=PASS` 且无新增 dump。但设备门将整体结果标为
`STALE_MODULE`：日志显示远端 test_host 仍解析到正在运行的旧实例所持有的
`\Storage Card\Temp\Positron-device-gate\app-ui-snapshot-20260928-20260928-213123\positron_core.dll`，
而不是本候选目录的 Core。当时默认设备门没有强制清理，因此这次只算“文件已部署、包内启动
回归通过”，不算新 EXE/Core 的设备验收；现在如确需清理可显式使用
`-ForceTerminatePositron`，它不会结束任意其他进程。

最新消费者日志 `tmp/device-runs/debug-capture-20260929-214139/positron-debug.log` 已确认
jQuery、Bootstrap 和 bootstrap-multiselect 的 `-6` 是旧 1.5 MiB heap 失败，而不是 128 KiB
source gate；新的应用 profile 尚未取得真实页面日志。设备纪律保持不变：用户先在 WMDC/Device Emulator GUI 手动连接恰好一个设备；gate 只复用当前
会话，不连接、选择、cradle 或重置设备，默认不结束进程；显式 `-ForceTerminatePositron`
时只运行独立 helper 精确清理 `positron.exe`。外置卡 Temp 优先，内置 Temp 回退；完整回收
日志后才清理旧部署。`tmp/` 只保存本地截图、日志和设备证据。
- 新 profile 的 `positron.exe` 已部署并启动，远端根为
  `\Storage Card\Temp\Positron-device-gate\debug-capture-20260929-222012`，本地日志根为
  `tmp/device-runs/debug-capture-20260929-222012`；启动快照只有 session 头，真实 WinWorld
  导航与 hamburger 点击仍等待用户 GUI 操作，不能把启动成功写成脚本已执行。

- 本轮把 EXE Debug 诊断从仅依赖 `OutputDebugStringW` 扩展为有界设备文件镜像：
  `positron_app/app_debug.c/.h` 在 `_DEBUG` 下把脚本逐项/汇总状态和图片摘要同时写入
  `\Temp\positron-debug.log` 与 debugger sink，文件达到 128 KiB 后停止增长；Release
  通过同一头文件宏裁掉日志实现和调用。`scripts\debug_capture.bat`/`.ps1` 现在可用
  32 位 WMDC RAPI 正式 stage、复制 19/19 文件、启动 `positron.exe`、清理这个固定日志文件，
  并用 `-PullOnly -RemoteRoot ... -FollowSeconds N` 在用户操作期间回收日志；脚本不选择设备、
  不 cradle、不重置、不强杀进程。
- 本轮修订了诊断可靠性：`AppDebug_BeginSession()` 在每次 Debug 进程启动时先写
  `debug-session pid/tick`，后续每条记录带 PID；EXE 现在记录 network navigation 的
  `reject/start/finish-commit/finish-rollback/finish-stale`、generation、requested/visible URL、
  commit stage、HTTP status/failure class，并在脚本数量为 0 时也写 `script-scan`。因此固定日志
  每次重启只代表最新 session，但能区分进程，也能区分“未开始、失败回滚、成功提交”和旧页面保留。
- 修订后的 Debug EXE 已用上一份正式 19 文件 stage 保留的 DLL/字体/fixture 包重新部署；本地运行根为
  `tmp/device-runs/debug-capture-20260929-154540-diagnostics-v2`，远端目录为
  `\Storage Card\Temp\Positron-device-gate\debug-capture-20260929-154540-diagnostics-v2`，
  19/19 文件复制成功，启动 PID 为 `4133935414`。首次回收日志为
  `tmp/device-runs/debug-capture-20260929-154608/positron-debug.log`，已确认包含当前 PID 的
  `debug-session` 标记；页面操作后应再次用 `-PullOnly` 回收同一远端根目录。
- `positron_db`/SQLite/JSON/TEST1321 纵切已通过 C89 和仓库审计；Debug 已进入 DB ARMV4I
  编译，但本机既有无标题 VS2008 `devenv.exe` 占用使全量返回 1，不能写成全量通过。Release
  仍须单独记录公共 DLL 编译结果和既有 CabWiz 数据文件失败。
- 本轮复核 `.agents/ROADMAP.md`：DB 的独立 REST fixture 已落地，宿主 worker、真实设备网络/
  断电门仍进入准备取舍；既有 WinWorld 页面脚本状态和应用视觉人工门仍不能被桌面构建或 DB
  contract 证据替代。

## 当前未决边界

- `positron_media.dll` 仅完成 WM6 Emulator 上 WAV PCM 定向 callback/AUTO smoke；WaveOut 格式接受、
  音频 underrun、FFmpeg 帧率/时间戳、峰值内存和关闭耗时，以及真实 ARMV4I 设备仍未验证；
  DirectShow callback source filter/native 视频播放仍未实现。AV1/HEVC/VP9、编码、DRM、字幕和
  直播协议明确排除。
- 脚本自行构造的 File/Blob 尚未形成 Browser FormData 到 Core multipart 的公共转换；native
  picker 的源码接线不能写成已完成的设备上传基线。
- EXE 图片资源事务的恢复阶段、相对 URL 解析、Core pending/retry、Core CSS data URI、
  Image class-style 和 viewBox-only intrinsic-ratio 契约已按 TEST13 的宿主顺序修正；TEST1313
  和 TEST1318 已在离线 Core 中
  证明 pending SVG 第二次扫描可缓存、解码并布局，terminal failure 不重试，TEST1314/1315
  已在 Image DLL 直接绘制门证明 IANA 风格 SVG 不再因 class paint 变黑，TEST1317 又证明
  Core 的高 DPI 重复 SVG tile 与 Image 直接绘制一致。最新完整包仍需要在 `positron.exe`
  真实网络页面上确认；自动 Core 背景门和 `test_host` 日志不能替代应用截图或 image-state
  摘要。当前若消费者仍报告视觉无明显变化，应先确认启动的是本轮同一批
  `positron_image.dll`、`positron_core.dll` 和 `positron.exe`，再在应用中打开 IANA 首页；
  不要把旧包的截图归因给新 Image 代码。
- OEM SIP/IME、真实触摸、旋转/DPI、复杂 CSS/布局、完整现代 Web API 和浏览器安全沙箱仍受
  [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md) 约束，合成按键不能替代人工设备验收。
- 失败网络、重定向、资源相对 URL 和应用输入仍需设备门确认；稳定合同和测试矩阵见
  [`docs/TESTING.md`](../docs/TESTING.md)。
- 地址栏字体度量、外框高度和文本下行完整性已由用户在当前设备确认；窄视口 IANA 首页的
  panels/footer 不互相覆盖、旋转后的完整几何以及纯滚动不因窗口尺寸未变而重新 layout/paint
  仍属于待完成的应用人工门。nested overflow 的横向/纵向滚动输入也必须在新 Core 模块实际
  加载后确认；旧进程复用的截图不能作为本轮接线证据。
- WinWorld 窄视口曾复现页面级横向滚动条几乎铺满轨道的现象；当前仍未归属到 Core extent、
  EXE client rect/样式事务或页面 CSS。该问题按当前决策暂缓，不作为本轮 SVG 部署验收条件，
  也不通过隐藏原生滚动条规避；后续重启时需先采集同一页面同一 DPI 的 document/page 尺寸、
  client rect、scroll range 与样式变更时序。
- WinWorld `/home` 位图缺失：`tmp/device-runs/debug-capture-20260929-214139/positron-debug.log`
  记录 module `ignored-type`、`resources=0`；Shadow DOM 图片未创建，需 Script/Browser/Core，
  非 Image/HTTP 单独故障，当前暂缓。
- DB 的 REST fixture 已有，但宿主 HTTP worker、真实 401/5xx/分页/重试以及设备 journal/强制重启
  恢复尚未验收；当前 TEST1321 只是本地 contract，不能宣布双向联网同步已在设备上完成。

## 唯一下一步

建立原版 jQuery 3.5.1 / Bootstrap 4.6.2 的离线初始化与 collapse 集成回归，再按完整合同修复
detached DOM。最新设备日志 `tmp/device-runs/debug-capture-20260929-230751/positron-debug.log`
已经否定“只需再次手测”的判断：jQuery 在 `__pcoreFormProperty` 抛出 -3，Bootstrap 因缺少
jQuery 停止初始化；本次峰值约 2.31 MiB，低于 3 MiB profile。空 id 的 detached option
selected 查询可由本地 bootstrap 探针复现，createHTMLDocument 等后续缺口也已确认。

前几轮未提交 Browser checked/value/innerHTML 实验存在状态和所有权问题，不能提交为正式
修复；三份源码改动暂保留供审查，本次调查未部署新候选。详细证据和探针局限见
[调查记录](../docs/history/WINWORLD_SCRIPT_INITIALIZATION.md)。本轮复核 ROADMAP 后确定
优先项应改为真实库初始化纵切；自动断言成功以前不再要求用户重复点击，不扩大 heap 或加入站点特判。

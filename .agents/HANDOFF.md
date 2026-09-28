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
继续收束图片纵切：`positron_core.dll` 的异步 pending/retry 已完成，`positron_image.dll` 现在
能在不破坏旧 ABI 的前提下消费 IANA 风格的简单 SVG class paint/gradient，并按 viewBox 保留
没有显式宽高的 SVG 固有比例；Core 的高 DPI 重复 SVG 背景 tile 也已按设备 DPI 缩放。真实
`positron.exe` 图片页面仍待人工观察。Media 的 DirectShow callback source filter/native 视频
生命周期仍是独立后续边界；此前 HTTP(S) 导航与资源事务的源码事实保持不变。
本轮又处理了同一 IANA 页面截图暴露的两处几何问题：EXE 地址栏不再使用私有 inset，并改为
使用 native EDIT 实际 `SYSTEM_FONT` 的 `TEXTMETRIC.tmHeight` 加少量 DPI 留白计算外框高度；Core
纵向 auto-height flex 在子项 `flex-basis:0` 时改用已布局实际高度贡献父容器。字体度量后的地址栏
外框已进入上一批 Debug/Release 构建；最新截图又发现 native EDIT 内部文本格式矩形偏低；本轮已
撤销按 DPI 上移内部格式矩形的临时修正，改为以实际 EDIT 字体的客户区行高加对称 DPI 留白，再用
`AdjustWindowRectEx` 按控件实际边框换算外框高度。文本继续由 native EDIT 自己垂直居中，不再调用
`EM_SETRECTNP`。该修正版已通过 C89、审计和 Debug/Release 增量构建，并已部署到新的设备隔离目录；
`TEST999` 已通过，用户已确认新修正版 `positron.exe` 的地址栏文字上下边界和外框高度符合预期。

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
- `positron.exe` 的顶层物理页面滚动已沿 `test_host` 的 retained-pixel 路径接线：纯滚动只更新
  系统滚动条位置、用 `ScrollWindowEx` 移动已有像素并重绘暴露条带，再重定位同一窗口体系下的
  native 子控件；不会在每个滚动步重新执行 Core layout，也不会重复同步 SELECT/toggle 状态。
  `WM_SIZE`、Core/DOM mutation 和真正的 viewport 变化仍可触发完整 layout；该 EXE 路径已通过
  C89、审计及 Debug/Release ARMV4I 构建，但尚未取得设备上的流畅性人工证据。与 TEST13
  对比发现，参考宿主的实际渲染 HWND 带 `WS_CLIPCHILDREN` 并按 `PAINTSTRUCT.rcPaint` 清理；
  EXE 的页面子窗口此前缺少该样式且按完整 client 矩形清理。本轮已补齐页面子窗口裁剪和
   `rcPaint` 绘制边界；EXE 的 ScriptSession 仍保留 scroll 事件语义，scroll listener 修改 DOM
   时触发 layout 属于合法内容变化，不以关闭脚本事件来掩盖。当前工作树又把页面脏区的
   白底清理、Core 绘制和焦点框改为 EXE 私有兼容位图中的一次性 `BitBlt`；这只解决可见
   窗口 DC 暴露中间帧，不改变 Core/Browser 滚动或脚本事件语义。Debug ARMV4I 正式构建
   已通过，设备上的闪屏/撕裂人工结果仍待部署后确认。
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

本批更新了 `positron_image` 与 `positron_core` README、能力矩阵、测试合同和当前状态，并复核
`.agents/ROADMAP.md`：IANA class-style、viewBox 固有比例和 Core 高 DPI 重复背景 tile 的 Image/Core
纵切已完成，阶段 B 候选只保留 `positron.exe` 应用图片可见性设备门和原有 Media 未完成边界；
不得把离线 decode、Core 背景门或桌面构建证据写成真实应用视觉通过。

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
- 同一批 Debug staging 已复制到外置 `Storage Card\Temp\Positron-device-gate\iana-app-20260927`，
  并通过 RAPI 成功启动 `positron.exe`；启动成功只证明匹配的 EXE/DLL 能运行，IANA 首页的
  实际网络图片仍需用户在该应用窗口中导航后进行视觉确认。
- 本轮 Debug 完整包已部署到当前 WMDC 设备：本地 stage 为
  `tmp/device-runs/20260928-141835-address-font-20260928/stage`，EXE SHA-256 为
  `69FA22C72ABBBAC3405F466C8D7FC801175570CBCFDA11F15583E5F24E100CD4`，与
  `positron_app/bin/Debug/positron.exe` 一致；远端目录为
  `\Storage Card\Temp\Positron-device-gate\address-font-20260928-20260928-141835`。
  设备门 `TEST999` selected/observed 为 `1/1`、`core_module_check=PASS`、
  `crash_check=PASS`、新增 dump 为 0；目录按诊断保留，但这仍不等于 `positron.exe` 页面视觉
  验收。

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

本轮 `d4f17d28` 的 Debug 完整包已复制到
`\Storage Card\Temp\Positron-device-gate\app-overflow-pointer-20260928-20260928-214547`，
本地证据为 `tmp/device-runs/20260928-214547-app-overflow-pointer-20260928/`；19/19 文件复制完成，
`TEST999` 本身为 `1/1`、日志完整、`crash_check=PASS` 且无新增 dump。但设备门将整体结果标为
`STALE_MODULE`：日志显示远端 test_host 仍解析到正在运行的旧实例所持有的
`\Storage Card\Temp\Positron-device-gate\app-ui-snapshot-20260928-20260928-213123\positron_core.dll`，
而不是本候选目录的 Core。设备纪律不允许 gate 强杀设备进程，因此这次只算“文件已部署、包内
启动回归通过”，不算新 EXE/Core 的设备验收；必须先在设备任务管理器中真正退出旧
`positron.exe`，再从本候选目录启动。

设备纪律保持不变：用户先在 WMDC/Device Emulator GUI 手动连接恰好一个设备；gate 只复用当前
会话，不连接、选择、cradle、重置或强杀设备。外置卡 Temp 优先，内置 Temp 回退；完整回收
日志后才清理旧部署。`tmp/` 只保存本地截图、日志和设备证据。

## 当前未决边界

- `positron_media.dll` 仅完成 WM6 Emulator 上 WAV PCM 定向 callback/AUTO smoke；WaveOut 格式接受、
  音频 underrun、FFmpeg 帧率/时间戳、峰值内存和关闭耗时，以及真实 ARMV4I 设备仍未验证；
  DirectShow callback source filter/native 视频播放仍未实现。AV1/HEVC/VP9、编码、DRM、字幕和
  直播协议明确排除。
- 脚本自行构造的 File/Blob 尚未形成 Browser FormData 到 Core multipart 的公共转换；native
  picker 的源码接线不能写成已完成的设备上传基线。
- EXE 图片资源事务的恢复阶段、相对 URL 解析、Core pending/retry、Image class-style 和
  viewBox-only intrinsic-ratio 契约已按 TEST13 的宿主顺序修正；TEST1313 已在离线 Core 中
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

## 唯一下一步

下一步先在设备任务管理器中真正退出旧的 `positron.exe`（关闭窗口可能只是 Smart Minimize），
再从 `\Storage Card\Temp\Positron-device-gate\app-overflow-pointer-20260928-20260928-214547`
启动本轮 `positron.exe`。打开 `https://www.iana.org/numbers`，拖动表格内部横向滚动条的 thumb，
并点击左右箭头；预期是表格内容移动、thumb 位置更新、页面不整页重新排版或闪烁，内层纵向滚动
仍可用。然后再检查顶层页面滚动、窄视口 flex、旋转和 retained-pixel 绘制。只有确认新 Core
实际加载且该人工门通过后，才继续阶段 B 的 CSS/script/image/失败回滚门；若要验收旧页标题与
地址回滚，再单独使用 `app-baseline-redeploy-20260928`，不要混用候选目录。

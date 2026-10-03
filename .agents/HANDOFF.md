# 当前交接

本文件只保存当前事实、候选、风险和唯一下一步。稳定边界见 [架构](../docs/ARCHITECTURE.md)、[能力矩阵](../docs/CAPABILITIES.md) 和 [测试合同](../docs/TESTING.md)。

## 使命与当前目标

Positron 为 WM6 / CE 5.2 ARMV4I 提供九个资源有界、稳定 C ABI 的公共 DLL 及独立消费者 positron.exe。产品语义属于 DLL，test_host 只拥有 fixture、平台接线和断言。用户要求补齐 Media 本阶段缺口；输入错误、WAV PCM8/16 与短小 H.264/AAC/MJPEG/MP3 解码/守卫合同已通过 Debug/Release SD 设备门，下一条纵切为 TS/MPEG fixture。Fragment CSS 坐标和 DB 离线契约验收继续有效，用户更换设备后 SD 卡部署、回读与 DB Debug 正式验收均完成。既有 EXE 人工验收仍为 About 章节位置、Debug 构建时间和 Release CAB 安装版本，分别验收，不改变公共 ABI 或打包版本规则。

用户已确认 WinWorld 菜单展开/收起、SVG 与作者按钮外观正常；性能修正后的实际展开/收起不超过约 2 秒，期间地址栏与菜单可响应。不要恢复旧的“按钮无响应/图标仍损坏”假设；Release 点按耗时对照仍未完成，不把 Debug 体验扩大为全部设备的保证。

## 当前代码与所有权

分支 main。并行性能变更已由 df205495 提交，后续 DB 测试提交以 Git 为准；内部页面提交只包括 positron_app、专用检查脚本及其文档。共享 main.c 中原有性能计时与退出日志已保留，不纳入重复修正。本批新增的 device_tools 模块审计属于设备门前置能力，已与内部页面改动分开。

内部注册表区分 newtab/about/history/downloads/settings、version/system 别名、quit 命令及原 welcome/controls。默认入口为 newtab；内部页使用双语嵌入资源、Core 渲染、24 项有界焦点目录，无 ScriptSession 或外部请求。history 只读 Browser 导航栈；动态 HTML 上限 128 KiB。quit 的导航来源检查仅允许地址栏直接提交，沿既有 WM_CLOSE 关闭流程。设计与后端进入条件见 [接线计划](../positron_app/INTEGRATION_PLAN.md)。

## 候选与验证证据

### EXE 地址栏复用与加载标题自动门通过

EXE 私有 AppAddressBar 在同一矩形交替显示 GDI 展示层和原生 EDIT；非编辑时加载显示网址与
循环填满/清空、结束显示标题，长标题按实际字体往返滚动，无标题回退网址。点按/Tab/菜单
切回 EDIT；原生 EDIT 始终存网址，提交仍走原地址路由。编辑期间后台提交不覆盖输入或选区，
Esc/失焦后显示最新有效地址。80 ms timer 只重画地址栏，短标题和编辑状态停止；复用有界
双缓冲，不触发页面 layout/paint 或脚本 checkpoint。DLL、ABI 和导航资源语义未改。

新增 EXE 私有 AppLoading：原生标题最前面按 180 ms 循环 ASCII `| / - \`，后接双语请求/
DNS/连接/TLS/发送/等待/响应头/正文/重定向，以及 HTML/脚本/样式/图片/排版/绘制阶段。
主文档 GET/POST 订阅公开 PHttp_*UrlEx2 observer；子资源保留宿主资源阶段，WinInet 合并
通知只显示通用请求文字。COMPLETE/FAILED 不冒充页面完成、不改变 Browser gate 或重试。
用户明确接受比例字体位移，不用
全角缺字、不改系统字体。worker 只写自身原子展示字段，UI timer 只显示当前 generation，
不触发 layout 或脚本 checkpoint；成功首次绘制后恢复文档标题，失败沿原快照回滚。
Debug 自检覆盖 observer 映射/合并/终态与 size/version 拒绝、前缀帧和容量；独立候选夹具
验证正文进度、stale 隔离、标题更新不增加布局和成功标题恢复。诊断与夹具不编入 Release。

C89、仓库审计与串行正式 Debug/Release build 通过，EXE 零错误/警告、Release CAB 成功；
Release ASCII/UTF-16 检查不含 loading-title/loading-phase/fragment-pending 诊断。
首轮 Debug 预构建与 Release 编译提前退出无错误诊断，保留失败日志后正式重试通过；
`tmp/app-loading-build-failures/` 的旧 TLS observer 草稿错误不代表最终 DLL，未改 DLL 源码。
当前完整 Debug 包 `tmp/device-runs/20261003-152005-app-address-bar/` 共 34 文件，SD 目标空间
与内部缓存预检通过，正式 module-audit 及 EXE 门前复审均 holders=0 unavailable=0。
EXE/九 DLL SHA256 回读 10/10；address-bar/loading-title/fragment-pending/history/pointer/
system-info/internal-pages 自检 OK，crash_check=PASS。门后 PID 2110370946 留在 newtab，入口为
`\Storage Card\Temp\Positron-device-gate\app-address-bar-20261003-152005\positron.exe`。
本轮地址栏 Debug/Release 正式 build 均零错误/警告，CAB 成功，Release 不含 address-bar
夹具。地址栏自检覆盖显示/编辑分离、填充/滚动周期、点按、输入/选区保留与 resize/释放；
真实回车、中文 IME、动画耗时及滚动响应仍待人工。上一加载标题包已由用户正常退出。
未强杀、重置或修改 WMDC；真实网络阶段显示、窄标题截断、双语及比例字体效果仍待人工。
本批只提交 EXE、专用检查脚本和对应文档；Media 变更已独立提交，不重复纳入应用批次。

HTTP/TLS owner 的先行定向
`3,4,999` 设备门在用户当前 WMDC RAPI 会话中通过；证据位于
`tmp/device-runs/20261003-102633-http-observer/`，外置 Temp 空间预检、模块审计、日志
完整回收和 crash_check 均 PASS，匹配的 `positron_http.dll`/`positron_tls.dll` 已在目标上运行。
该门不代表 EXE 自检。其先行 Release 的产品 DLL/EXE 均编译成功，但包含
`positron_cab` 的整套解决方案在 CabWiz 创建数据文件时失败；这是现有打包环境边界，未把它
误归因于 observer 源码，也未修改 CAB 工程。
打包边界已由本轮正式 Release 重试恢复，不重写先行失败日志。staging、截图和日志仍只在 tmp。

### EXE 系统信息证据与暂缓边界

About 系统章节使用中性标签、GetVersionEx 完整内核版本与实际平台/OEM 名称；只读
`HKLM\System\Versions\Aku` 原样展示更新包，可选 ProductName/OSVersion 独立显示。
不做 build/AKU→WM/WEH 查表。已纠正旧 platform version 方向：WM6.5.3 SDK 明确该 SPI
返回 CE 主/次版本，不能当成营销版本，因此删除私有 action 查询和重复版本行。
缺失、API 不可用、查询失败、非法数据分开显示，空内核扩展不占行；Debug 记录失败阶段与
错误码，Release 不含诊断。仅 EXE 私有查询与展示改动，不修改 DLL、ABI 或导航语义。

C89、仓库审计、串行 Debug/Release 正式 build 与 Release CAB 通过，EXE 零错误/警告；
Release ASCII/UTF-16 检查不含本批诊断或系统启动夹具。用户正常退出旧应用并暂停其他构建。
系统信息完整 Debug 包 `tmp/device-runs/20261003-091819-app-system-registry-final/` 共 31 文件；正式
module-audit 与 EXE 门前复审均 holders=0 unavailable=0，EXE/九 DLL SHA256 回读 10/10。
`app-history/positron-debug.log` 为 system-info/pointer/history/internal-pages 自检 OK、
单次提交 about#system（history=1、script=0、scroll=0,545），crash_check=PASS。
实际读取为 Windows CE 5.2.23090（platform ID=3）、PocketPC、AKU `.5.3.0` 与
Microsoft DeviceEmulator；ProductName/OSVersion 查询为 error=2（未提供）。coredll/aygshell
版本函数导出均存在，但 size 阶段返回 error=1814（资源名称不存在），不再笼统归因 API 不可用。
本轮 RAPI 只读枚举的四个系统版本专用键也没有提供营销版本元数据；未写设备注册表。
设备 320×320、128 DPI；该包 PID 2643158474 已由用户正常退出，当前人工入口见加载标题证据。
首轮 registry 包也通过门；复核删除误拒绝 OEM 实际 WM 产品名的夹具断言后，用户正常退出，
重新部署并验证最终源码。未强杀、重置、回退内置存储或改变 WMDC；旧包不是当前人工入口。
自动夹具覆盖双语标签、非 CE/未知 ID、中文 UTF-8、原样 AKU、registry 类型/长度/终止/
嵌入 NUL、非法 UTF-16/容量拒绝、版本资源切片边界和 HTML 转义。系统章节视觉及真实中文
设备仍待人工复核；缺失产品元数据时不宣称已识别 WM/WEH 发行版本。稳定规则见接线计划。

OS 产品名称、发行版本和 edition 的进一步动态识别已按用户决定暂时挂起；现有查询与缺失
状态保留，不撤回 AKU 或加入版本查找表。重新开启条件见 ROADMAP 的暂缓队列；本轮导航修正
不重新开启系统信息取证，以上细项仍来自对应系统信息包。

### EXE 页面内容拖动自动门通过

用户要求无需抓滚动条即可拖动页面。EXE 私有 AppInput 以 WM6 pointer/capture 和 DPI 阈值
分类 tap/pan；页面 click 延迟到未拖动的抬起，纯双轴拖动复用 retained pixels、native 重定位、
extent clamp 与 Browser scroll 通知。取消、capture 丢失、失焦、隐藏、resize、旧页 teardown
和关闭清除待定输入；Core 内部滚动条优先，native EDIT/SELECT/toggle 原消息路径保留。
不修改 DLL 或 ABI，不引入 WAG、惯性、回弹或 nested 内容区滚动链。

C89、仓库审计与最终 Debug/Release 正式 build/CAB 通过，EXE 零错误/警告；Release ASCII/
UTF-16 检查不含 pointer 夹具。全量 rebuild 的既有 LNK1181/CAB 依赖顺序失败与 VS2008
msenv.dll 主机异常仍保存在 `tmp/app-pan-build-failures/`，串行正式补建恢复，不计作 rebuild
成功。用户已正常退出旧应用并暂停其他构建，未竞争编译。

相邻正式 Debug SD 门 `tmp/device-runs/20261003-041056-app-pan-adjacent/` 选择
`42,1080,1109,1327,999`，selected/observed 5/5、唯一 TESTBENCH PASS、零 ERROR/FAIL、
Core 路径及 crash_check=PASS；完整日志回收后本轮设备目录已清理。
最终完整包 `tmp/device-runs/20261003-041442-app-pan-final/` 含 31 文件，正式 module-audit
及 EXE 门前复审均 holders=0 unavailable=0；`app-history/positron-debug.log` 记录 pointer/
history/internal-pages 自检 OK、newtab history=1。实际页面消息覆盖链接 down 不激活、tap
一次、双轴拖动及返回起点不误点、合并 MOVE、capture/cancel/失焦/stale、clamp、Browser
坐标同步和 layout 计数不变。EXE/九 DLL SHA256 回读 10/10、crash_check=PASS。设备为
320×320、128 DPI DeviceEmulator；该包 PID 534774266 已由用户正常退出，当前人工包见加载标题证据。
未强杀、重置或回退内置存储。设备门的静默启动失败未产生设备证据；WMDC AuditOnly 健康、
changed=0，显式 PS32 入口诊断运行原正式 gate 后恢复，不修改注册表或安全设置。

早期 `20261003-041159-app-pan-delivery` 的 pointer phase=2 失败属于夹具误用旧 LinkAt
命中返回值（1，而 Ex 成功为 0）；按公开合同修正探针，原点击/拖动断言保留并加强 clamp。
失败包/日志保留，不转为通过。真实触笔/手指跟手性、从按钮开始拖动、EDIT 拖选、SELECT
popup、旋转/DPI 仍待人工门；本轮 ROADMAP 已复核并明确这些 backlog，Media/DB 状态不覆盖。

### EXE 同文档导航与滚动恢复自动门通过

EXE 已建立当前 GET 文档的 Browser identity 绑定，同文档 target 保留 DOM/session/
native 控件，调用 Browser traversal/hash 通知；pushState/离页提交前保存实际双轴位置，网络
history/刷新在 layout 后恢复并尊重 manual。刷新取得新 identity，POST 不绑定旧导航栈。
字面 UTF-8/空/缺失/legacy fragment 由 Core ByToken 提供 CSS 几何并只换算一次 DPI；百分号
编码片段和 HTTP 跨文档初始片段仍未接入，不能复制 URL 引擎来补齐。

用户复现 A 的片段跳转会打断正在加载的 B；原因是 EXE 同文档入口无条件取消当前候选。
现仅修正 EXE 调度/UI：fragment 不取消 B、不增加 generation，保留 A 的 DOM/session，
地址栏与加载标题继续表示 B；同步 B 的已提交地址快照，失败恢复 A 最新片段与原标题。
push/replaceState 也同步该快照；外链 C 仍取消/退休 B，B 迟到完成不覆盖 C；B 成功替换 A。
URL 同文档分类、history、资源终态和提交资格仍由原公共 DLL 提供，无 ABI 或 DLL 改动；
明确的 Back/Forward/go 取消策略未扩大修改。

Debug 独立启动夹具用 event 暂停真实 worker，不联网，随后通过 Browser resource 数据和原
parse/commit 路径控制交错。设备断言覆盖 fragment/full URL/repeated/missing、generation/
取消状态/旧页保留、replaceState 后回滚、C 替换及 stale B 的 UI 隔离、B 最终提交。
`fragment-pending selftest OK phase=4` 已纳入 app_history_gate 必需条件，不以夹具冒充真实
HTTP transport 或人工点击验收。最终 C89、审计、正式 Debug/Release build/CAB 通过，EXE
零错误/警告；Release ASCII/UTF-16 不含 fragment-pending/pending-b/replacement-c 夹具。
用户正常退出并暂停其他构建后串行执行；Debug 曾在 prebuild 提前退出，无编译错误线索，
日志留在 `tmp/app-fragment-pending-build-failures/debug-prebuild.log`，正式重试通过，失败不转为通过。

相邻 Debug 正式设备门 `tmp/device-runs/20261003-002348-app-history-adjacent/` 选择
`136,407,1080-1083,1134,1330,999`，selected/observed 9/9、唯一 TESTBENCH PASS、零 ERROR/FAIL、
Core 路径匹配、双空间预检和 crash_check=PASS，完整回收日志后清理目录；该门不代表本轮重跑。
此前完整 Debug 包 `tmp/device-runs/20261003-093754-app-fragment-pending/` 共 31 文件；正式
module-audit 与 EXE 门前复审均 holders=0 unavailable=0。`app-history/positron-debug.log`
记录 fragment-pending/history/pointer/system-info/internal-pages 自检 OK、newtab history=1；
EXE/九个 DLL SHA256 回读匹配 10/10、crash_check=PASS。设备为 320×320、128 DPI
Microsoft DeviceEmulator；该包原 PID 3190149122 已由用户正常退出，包保留在
`\Storage Card\Temp\Positron-device-gate\app-fragment-pending-20261003-093754\positron.exe`。
未强杀、重置、回退内置存储或修改 WMDC。真实 A/B 加载中片段点击、C 替换与网络失败回滚
仍待人工复核；新部署前重新检查 guest holder，不沿用先前运行状态。

早期 scroll/diagnostic 两包的 phase=7 legacy 锚点失败仍保留；夹具 inline anchor 没有可用
box，最终按 TEST1083 改为 block 并预查 geometry，原滚动断言、卸载不派发与刷新 identity
隔离断言全部通过。`tmp/device-runs/20261002-231242-app-history-final/` 的提前构建失败未部署，
不能追认为通过。本轮 ROADMAP 已移除完成的导航候选并保留 URL/人工边界；Media/DB 状态不覆盖。
真实地址栏 Enter、HTTP 跨页/刷新恢复、native 输入状态、焦点与旋转仍待人工复核。

另发现 Debug 增量构建的时间头已更新到 00:26:35，但 app_version.obj 未重编，运行仍显示
`2026-10-02 23:25:27`；完整包哈希已匹配，故不是旧 EXE 混包。后续须单独修正生成头的正式
工程依赖，再验证增量构建时间与 Release 排除；本导航纵切不把该版本显示写成已修复。

### Media 输入、PCM 与 H.264/AAC/MJPEG/MP3 解码合同已验收

产品 DLL 保留 I/O/WOULD_BLOCK 错误、seek/probe 恢复与失败不改输出合同；PCM8 callback
统一为 S16LE，负预算与停止态拒绝。压缩路径修正 AVSEEK_SIZE 标志和 AVIO buffer 释放，
补齐 ARM CRT 的 float/double/64-bit 返回原型及 expf/logf bridge，正式归档重建把 C4013
设为错误。SPS 检查必须提供有效 AVCodecContext，不能把 FFmpeg 3.4 的 logctx 当作可空
日志指针；所有 extradata SPS 与实际帧均检查 profile、8-bit/4:2:0、隔行和硬 VGA 边界。
原隐式声明导致的整数除零与新增 SPS 空指针异常已修复，失败日志/截图保留在 tmp，不能转为通过。

固定的十一个 procedural red/sine 夹具、CC0 内容来源、生成器版本/参数/哈希进入
test_host/fixtures/media；scripts/media_fixtures.py 默认只离线核对 pin。TEST1332 消费公共
DLL，验证 MP4 Constrained Baseline/AVCC + AAC-LC stereo、VGA Main/B 帧、ADTS AAC-LC
mono 的 I420 像素/stride、S16LE、时间戳/时长、不可 seek 打开、暂停/resume、EOF 一次性与
seek 重播；High/4:2:2/隔行/超限/非 LC 拒绝不改 probe、不留 session；截断头后十二次独立
打开/解码/关闭。TEST1333 验证 AVI/MJPEG 4:2:0 + MP3 stereo、44.1 kHz mono MP3 裸流，
包括全范围像素/flags、PCM 样本数、PTS/时长、7-byte source 分块、不可 seek 打开、音频
callback STOP/resume、EOF/seek 重播、停止态、独立关闭与 4:2:2/截断头/应用尺寸拒绝。
FULL_RANGE 是既有 flags 的追加，不改 ABI 布局；不把 YUVJ420P 误当有限范围。原八个
夹具字节/pin 未改；新 AVI 的独立桌面解码证明 PTS 为 0/400000/600000 µs，修正了首轮
等间隔测试预期，未放宽帧数、样本数或像素断言。失败日志留在 tmp，不转为通过。
TEST1312/1331 保留原 WAV/I/O 与 WaveOut 回归，不测试已释放 handle 的重复 close。

最终正式 Release 门 `tmp/device-runs/20261003-151653-media-mjpeg-mp3-release-retry/` 与
Debug 门 `tmp/device-runs/20261003-151422-media-mjpeg-mp3-debug-final/` 均选择
`1312,1331,1332,1333,999`，PASS、selected/observed 5/5、唯一 TESTBENCH PASS、零 ERROR/FAIL、
crash_check=PASS、无新增 dump。设备为 240×320、96 DPI Microsoft DeviceEmulator；两门
明确使用 `\Storage Card\Positron`，空间预检、guest holders=0 unavailable=0 和 Core 匹配
路径通过。Release 当前目录清理部分失败并保留，Debug 当前目录完整清理；Release 传输中的
一次 RAPI 写失败由正式门重开当前会话并用 1 KiB 重试成功。没有强杀、重置、
回退内置存储或更改共享设置，旧失败目录仍保留。

正式 Debug/Release、C89 与仓库审计通过；本轮不重建 FFmpeg archive，媒体编译保留既有 bswap/
get_bits 警告，宿主保留 libcss fpmath 警告。首轮 Release 的 CAB 依赖顺序失败没有部署，
串行正式重试成功，不把初次构建计为通过。夜间 ZIP 的本地 SkipUpload 检查包含全部夹具/
来源/pin，不发布 nightly。其他已编译 codec/container、截断 payload、非零压缩 seek、输入容量、
原生完整生命周期、underrun、性能/峰值内存及真实设备仍待门。clock_us 被忽略，同步/按时输出/
迟到丢帧未实现；解码合同不等于播放器验收。并行应用源码与交接改动保留，不纳入媒体提交。

### Core Fragment DPI 已验收与应用人工门

消费者报告 About 版本/系统章节跳转过远，源码确认 `PCore_FragmentInfoById/ByToken` 直接返回设备布局坐标，与公开 CSS px 契约矛盾。修复复用文档自身的 layout DPI 转换 x/y/w/h，不改变 ABI、EXE 或其他几何 API。测试宿主对物理滚动、modal hit/paint 与 native geometry 探针显式转换；逻辑视口 fixture 明确使用 96 DPI。组件调用说明已同步。

正式 Release 门 `tmp/device-runs/20261002-102935-fragment-dpi-acceptance/` 选择 `13,1080-1083,1101,1105,1106,1108,1311,1330,999`，结果 PASS、selected/observed 12/12、唯一 TESTBENCH PASS、零 ERROR/FAIL、无新增 dump；设备为 480×640、192 DPI，空间与无 DLL holder 审计通过，日志完整回收。TEST13 三段真实导航均完成；TEST1311 保护文字裁剪。TEST1330 在同一设备分别布局 96/144/192 DPI，覆盖章节 CSS 坐标与实际滚动后顶部像素、负坐标、ID/name 优先、隐藏/缺失/未布局失败不改输出、文档 DPI 快照隔离、native 控件/链接物理命中与重复释放。C89、仓库审计、设备门配置单测和正式 Debug/Release build 均通过。当前验收目录清理部分失败，门记录 `preserved_cleanup_failed`；不把剩余文件写成已删除。

恢复设备门时另确认两个宿主前置问题：Release `/O2` 将 fixture 内联到 `run_configured_tests`，入口栈帧约 181 KiB，超过宿主 64 KiB；仅该控制分发器关闭优化后正式 Release 恢复执行，DLL 与 fixture 优化不变。TEST1106 的结果文字 mutation 会使 retained layout 失效，旧同步探针未处理已排队的 WM 重排就继续点击；探针现在在 JS 返回后有界处理该消息，再取几何，原 cancel/close 断言保持不变。此前失败日志仍在本地，不把它们转为通过。

本仿真器的外置目录可复制完整包，但 Release 启动仍返回 device=126，早期 Debug 零日志不能证明执行；本轮有效测试使用内置 Temp。大文件 16 KiB RAPI 写入遭遇 `0x80072746` 后，正式门重开当前会话并仅重试一次 1 KiB 写入，已成功完成多次部署。没有重置设备、杀 WMDC 或强杀应用。闲置 `internal-pages-1002` 的 25 个文件及稳定日志已完整存入 `tmp/device-runs/internal-pages-1002-archive-20261002-102443/`，再精确清理以恢复空间；无法删除的剩余文件保留，备份可恢复。排错规则见 [TROUBLESHOOTING](../docs/TROUBLESHOOTING.md)。

剩余门只是真实应用 About 章节视觉复核，不重新要求 TEST232/263。ROADMAP 已复核并移除完成的 Core 候选，把 About 位置确认保留为人工 backlog。应用代码未修改；部署时必须使用同一正式 stage 的 EXE/DLL，不能沿用仍返回设备坐标的旧 Core。

About 人工复核包的本地证据在 `tmp/device-runs/20261002-103209-fragment-about-review/`。
当时完整部署后的只读模块审计和 EXE/九个 DLL 回读 SHA256 均通过；应用启动请求返回 PID，
但随后的 Toolhelp 快照不可用，不能凭 PID 宣称界面可见。设备上的该旧包已按用户清空旧部署的
授权删除，仅余无法删除的传输临时文件；继续人工复核必须重新部署，不能沿用旧路径或 PID。
这不否定此前独立宿主的匹配路径和 12 项 PASS。

### 内部页面自动门

Debug 私有启动自检使用独立 Browser history，不污染实时页面：检查所有来源的 quit 策略、别名只提交一次、16 项及前进栈、反向顺序、HTML 转义、自身过滤、容量拒绝、Core 解析/布局和 history 的 23 个焦点；另读取并解析十份中英文 RCDATA，确认 UTF-8 无 BOM、中文标题非 ASCII。夹具与诊断全部由 _DEBUG 编译隔离。

最终源码的导航门证据在 tmp/device-runs/internal-pages-delivery/navigation/：15/15，通过默认入口、各页面、大小写、末尾斜杠、about 片段与别名章节滚动、旧关键词/controls 查询参数、未知启动地址及启动 quit 拒绝；每次仅一个 history 提交、无脚本，焦点数量符合预期，crash_check=PASS。设备为 480x640、DPI192。包包含最终 Media/TLS ABI 显示；Debug 私有自检在每次启动通过，最终 PID 3811239850 留在 newtab。

外置卡候选 tmp/device-runs/internal-pages-verified/ 曾完整复制 23 文件并启动，但随后同目录文件可枚举却 RAPI 回读返回 device=5，清理工具启动返回 device=2；未归因于页面代码或声称文件已被删除。内部页面原验收使用备用内置目录 \Temp\Positron-device-gate\internal-pages-1002；首次部署前对象存储可用约 23 MiB，最终更新前仍约 8 MiB。最终完整 stage 在 tmp/device-runs/internal-pages-delivery/stage/，23 个文件全部经设备回读 SHA256 核对；旧候选二进制仅在精确清理成功后替换，未变字体因 OS 持有而不删除，直接回读确认匹配。旧性能验收 PID 已被后续清理，不再让用户寻找旧进程。

相邻 test_host 门 tmp/device-runs/20261002-000923-internal-pages-adjacent/ 选择 136,1081,1082,1064,999：预检通过，但 test_host 日志一直为零字节，180 秒超时，清理摘要 failed=0；不是测试 PASS，也没有足够证据归因于特定 DLL。保留诊断目录，不放宽断言。

### 构建、审计与待验收门

C89、最终仓库审计与 diff 空白检查通过。最终 About 版本补全的正式 Debug build 通过，EXE 零错误/警告；正式 Release build 也已通过并生成 CAB。二进制检查确认 Release EXE 不包含内部自检诊断文本。

清洁 Debug/Release rebuild 均暴露 VS2008 解决方案依赖顺序问题：消费者在依赖库尚未生成时链接，出现 LNK1181；补跑正式 build 恢复。较早 Release 尝试还遇到 devenv/msenv.dll 自身异常，最后串行补建已恢复；不使用 SafeMode 或绕过正式工程，不把失败 rebuild 写成成功。本批不发布 nightly。

正式 stage Debug 已恢复成功并完成上述完整包部署。模块审计 helper 通过正式 ARMV4I Debug build；本批未绕过正式工程，也未启用强制清理。

### 设备门 DLL 引用审计

设备门现在使用无 Positron DLL 依赖的 `positron_process_cleanup.exe --audit-modules`，在启动
`test_host` 前通过设备端 Toolhelp 枚举所有进程及模块，覆盖 stage 中的 9 个 Positron DLL；
`holders=0 unavailable=0` 才能继续，快照不可用也 fail closed。`-ModuleAuditOnly` 可只做部署和
审计，不启动 `test_host`，也不能和 `-ForceTerminatePositron` 同时使用。该门只读，不会替用户
终止进程；针对 guest 的 `ERROR_NOT_ENOUGH_MEMORY` 仅做三次短重试，重试后仍不可用仍 fail
closed。

较早 About 版本只读审计证据在 `tmp/device-runs/20261002-084845-app-version-audit/`，
`module-audit-result.txt` 为 PASS，`holders=0 unavailable=0`；未使用强制清理。该门完整
部署 23 文件后才审计，之后直接启动同包 Debug EXE，PID 3056936870 留在 About。该次部署的设备
目录是 `\Storage Card\Temp\Positron-device-gate\app-version-audit-20261002-084845`，不代表当前人工包。
重复复制第一份 fixture 时 CeMoveFile 仍返回 device=5，未替换 EXE，改为复用已成功部署包。
About 日志在 `tmp/device-runs/app-version-delivery/verification/positron-debug.log`：版本
自检、内部页面自检、About 单次提交及 crash_check 均通过。嵌入时间为
`2026-10-02 08:48:11`；Debug/Release 正式增量构建均零错误/警告，Release 不含版本自检
文本或 Debug 时间。注册表格式/容量/终止符验证由 Debug 私有夹具覆盖；Release 在真实 CAB
安装后的读取显示仍待人工检查，独立复制缺少有效安装版本时“未提供”是预期。
较早 error=8 和宿主零日志失败不转为 TEST1321 通过；应用运行时会持有 DLL，下一次
DLL/DB 门仍须在应用正常退出后重新获取无 holder 审计证据。
guest helper 的进程快照使用 Windows Mobile SDK 的
`TH32CS_SNAPNOHEAPS`，避免包含进程 heap 时耗尽快照内存；模块枚举仍保持
fail closed。Debug 完整包部署后的
`tmp/device-runs/20261002-125344-module-audit/module-audit-result.txt`，以及最终
Release 完整包部署后的
`tmp/device-runs/20261002-131031-module-audit-final/module-audit-result.txt`，均记录
`module_audit holders=0 unavailable=0`；后者覆盖最终 `/O2` 产物和全部 9 个 Positron DLL，
部署目录保留用于诊断，未使用强制清理。定向 Debug/Release TEST1321 启动前审计也均得到
`holders=0 unavailable=0`，但 `CeCreateProcess` 分别返回 device=126，进程没有启动，
没有 `test_host.log` 或 TEST1321 结果；因此它们都是宿主加载前置失败，不计为 TEST1321
通过或测试失败。所有结果均来自 guest helper 的 Toolhelp 模块枚举，不以桌面模拟器进程
存在与否推断 DLL 引用。

审计等待函数现在忽略 RAPI 成功回读的空日志，沿原有有界等待继续读取，只有完整摘要才参与
holder 校验。`scripts/test_device_gate_audit.ps1` 直接抽取正式函数并以合成 RAPI 覆盖完整摘要、
空/部分日志后完成、读取失败后完成、非零 holder、不可用快照和无摘要超时，共 7 项通过。
内置 Temp 的实际验证见 `tmp/device-runs/20261002-175542-db-internal-audit/`：正式 Release
helper 回读 SHA256 匹配，`module_audit holders=0 unavailable=0`。这是 guest 引用审计通过，
不代表 TEST1321 已运行。外置卡曾再次出现空日志、文件回读不可用和 device=5 写入失败；
历史成功宿主在该外置卡候选中也返回 device=126，尚不足以把失败归因到某个 DLL。

### DB 主机契约测试当前切片

`test_host/db_tests.c` 的 TEST1321 fixture 覆盖本地完整 SQL 的 DDL/DML/SELECT、事务提交与
回滚、嵌套事务拒绝、取消、错误复制、NULL/INTEGER/REAL/UTF-8 TEXT/BLOB bind/column、
INT64_MIN/MAX、空 BLOB、ColumnCount，以及 handle/SQL/bind/列索引参数守卫。SQL 长度、
表达式深度、变量数和 typed bind 大小均有预算断言。完整 fixture 已取得 Debug/Release ARMV4I
设备 PASS；取消另断言短 SELECT、prepared INSERT 和直接 Exec 的一次性取消、无写入及后续可用。

local/sync migration fixture 覆盖版本幂等、倒退和事务中拒绝、失败脚本不留残表，以及已注册表
被删除时的整批回滚。同步注册覆盖单列 INTEGER/TEXT 主键、复合主键拒绝、非法/保留/过长
标识符、缺少主键、重复列、非法类型、列数和状态冲突；产品 schema 校验已统计实际主键列总数。
同步模式 fixture 拒绝 ATTACH/DETACH、直接 DDL/事务/savepoint、多语句、非法 PRAGMA 和扩展，
并检查同步文件不能以本地完整 SQL 模式重开。

同步协议 fixture 覆盖配置参数、schema version/hash、固定 envelope、无 SQL 请求、typed
INT64/REAL/NULL/TEXT/BLOB、Base64 和 JSON 转义、outbox 事务合并与回滚、请求分页、连续
多页 pull、cursor 推进，以及远端 upsert/delete 不产生本地 outbox。失败矩阵检查 accepted
后续 change 错误时整批回滚、错误 client/op/version、key 与 typed 主键不一致、未注册实体、
malformed tombstone、非 bool deleted、非法/倒退 cursor、缺少数组、响应项超限与非法 Base64；
未知顶层字段保留兼容性断言。

冲突 fixture 覆盖服务器权威行、retry-local、接受/丢弃、服务器删除与本地编辑冲突，以及冲突
列表/单项复制的 size-probe、容量、索引和 action 守卫。实体/key 与 outbox 不匹配的响应须拒绝。
文件重开 fixture 检查 outbox/cursor/tombstone/conflict 持久化、重建行采用服务器删除版本和
接受服务器结果不生成 outbox；正常关闭重开已在当前设备通过，不代表进程重启、journal/断电门通过。

正式 Debug/Release build、C89 和仓库审计通过。用户明确授权
强行清空旧部署后，精确处理两个 Positron-device-gate 根下 98 个旧目录：73 个完整删除，
25 个仅余字体和 `.part-*` 文件，清属性后删除及再次复查仍失败。本地既有证据保留；本轮
清理证据在 `tmp/device-runs/20261002-200329-db-force-clear/`，不把残留目录写成已清空。
内置可用空间恢复到 23,083,008 字节，原空间阻塞已解除，没有强杀进程或重置设备。

首次文件打开的零字节/无终态问题已定位：SQLite 的 no-WAL/no-mmap Win32 syscall 表没有
初始化 `CreateFileMappingW`，WinCE 文件锁却仍调用它，产生地址零的访问异常。DB 端通过 VFS
系统调用接口恢复匿名锁状态映射，不修改上游快照、不启用 WAL 或数据库 mmap。另修正短 SQL
取消、空 BLOB 绑定和 authorizer 拒绝的公共错误码。宿主只新增阶段及继续传播异常的日志；分页
夹具的 32 KiB 缓冲移出 64 KiB 栈，容量和 65 行断言不变。夹具先注册表再验证 DROP 回滚；模拟
响应修正 BLOB bytes 和 retry-local 的新 op_id，并加强新 ID/服务器 base_version 断言。

正式 Release 门 `tmp/device-runs/20261002-205124-db-test-1321-final-release/` 使用匹配的
23 文件 stage，选择 `1321,999`，完整日志为 selected/observed 2/2、唯一 TESTBENCH PASS、
零 ERROR/FAIL，Core 路径与当前包一致、crash_check=PASS、无新增 dump；部署前 guest 审计
`holders=0 unavailable=0`。较早诊断超时/异常/断言失败日志仍在本地，不转为通过。该设备包在
日志完整回收后已按用户授权精确删除可删除部分，仅余删除失败文件；当前有效产物/日志在本地
stage 和证据目录，不让用户沿用已清理的设备 EXE 路径。

Debug 正式 build/stage 成功。用户更换设备后，新会话先由独立 helper 获取 guest 审计
`holders=0 unavailable=0`，未沿用旧设备结论。诊断包
`tmp/device-runs/20261002-223440-db-test-1321-new-device-sd-debug/` 完整部署到 SD 并运行
`1321,999`，日志 PASS；因显式保留部署，门状态为 DIAGNOSTIC_ONLY。该包的 23 文件随后
逐一从设备回读 SHA256，与正式 stage 全部一致，证据为 `device-roundtrip-sha256.txt`；
宿主按设备门规则部署为 `test_host-run-20261002-223440.exe`，不是原始 basename。

正式 Debug 验收 `tmp/device-runs/20261002-223747-db-test-1321-new-device-sd-acceptance/`
明确使用 `\Storage Card\Temp\Positron-device-gate`，门状态 PASS、selected/observed 2/2、
唯一 TESTBENCH PASS、零 ERROR/FAIL、Core 路径匹配、完整稳定日志、crash_check=PASS 且
无新增 dump。设备为 320×320、128 DPI；目标卷与内部缓存余量预检通过，部署后 guest 审计
再次为 holders=0 unavailable=0。正式门回收日志后完整删除本轮目录及上一诊断包，产物、
回读和日志保存在本地。未强杀、重启、修改共享设置或回退内置存储；本轮没有产品代码变化。

旧设备的空间不足、SD 文件可枚举但打不开与手动启动失败仍是失败证据，不转为通过：
`tmp/device-runs/20261002-205953-db-test-1321-sd-debug/`、`tmp/sd-read-probe.txt` 和
`tmp/QQ20261002-220552.png` 保留。具体共享驱动原因尚未确认，不要求新设备重复重挂卡；
旧字体及 `.part-*` 残留也不能写成已全部清空。

自动门不替代人工验收：地址栏直接输入/未知地址恢复原标题和地址、菜单、history 点击与刷新、直接 quit 和加载中 quit、中英文实际显示、触摸、键盘焦点、软键、滚动、旋转及 DPI 尚待确认。页面能力不应写成全部人工门通过的正式设备基线。

### 保留的性能基线证据

性能包 tmp/device-runs/debug-capture-20261001-234236/ 的自动 jQuery/Bootstrap 展开→收起→再展开终态 height=404/0/404、ok=true；style=8/7/9、layout=3/3/5、controls-refresh=13/15/16、paint=1/1/2 guest ms。普通点按的 style 约 79–103 guest ms，不能用自动探针耗时替代用户墙钟。

最终 resolver 切换门 tmp/device-runs/20261001-234106-css-cache-resolver-handoff-final/ 为 24,45,1327,999 的 4/4、唯一 PASS、零 ERROR/FAIL、无新增 dump；此前相邻 11 项门不代表后续源码全量重跑。TEST45 fixture context 错配的失败候选仍在 tmp/device-runs/20261001-230333-css-parse-cache-perf/，失败已经修正，不恢复旧 callback 接法。

## 有效边界与设备纪律

HTTP final URL、Core 资源终态和现有 SVG 能力继续有效；bootstrap-multiselect 语法边界、module/Shadow DOM、横向滚动条暂缓、SIP/IME/OEM 等见 [限制](KNOWN_LIMITATIONS.md)。内部 settings/downloads 只是诚实的只读说明，不代表配置或下载管理已经实现。DB 已通过 Debug/Release 文件关闭重开与完整离线契约；真实 HTTPS worker、应用持久化、HTTP 流式下载及断电恢复仍未进入本批。

WMDC 连接由用户手动完成，只使用当前唯一目标；新部署不覆盖诊断包。精确清理必须取得 helper 成功摘要，不能杀 WMDC、VS GUI 或其他程序。外置卡失败时可检查空间后使用内置 Temp；日志回收前不删除目录。只在用户告知新截图时查询截图，不以旧截图推断新运行。

## 路线图复核与唯一下一步

ROADMAP 与当前限制已复核：地址栏复用自动门通过，动画、输入/IME 和页面响应进入人工矩阵；
仅 EXE 展示变化，不扩大 DLL 能力或现有限制。加载标题与 HTTP observer 接线退出待实现候选，真实阶段/截断/
字体效果进入人工矩阵；同文档 fragment、片段 URL 公共接口
与人工矩阵仍有效。系统信息保留 AKU
与可选 OEM 字段，进一步 OS 产品/发行版本识别继续暂缓，营销版本不保证识别的限制不变。
Debug 增量版本时间依赖需独立修正。Media 移除已完成的 H.264/AAC/MJPEG/MP3 初始夹具、解码守卫和 EOF 重播候选，保留其他
格式、时钟同步和 DirectShow source 等缺口。DB 已移除已完成的 Debug 复测缺口：Debug/Release 完整离线合同和正常文件关闭
重开已通过；空间不足/跨进程锁/进程重启/journal 恢复、HTTPS worker 和应用持久设置/访问日志/
下载记录仍待完成。C89、仓库审计通过；性能人工通过事实保留，
Release 性能对照仍是可选后续门。

EXE 下一步人工复核当前 newtab 包的标题滚动、点按编辑/回车、加载填充、输入期间后台提交
与取消/失败回滚，并观察动画是否影响页面拖动或输入；必要时降刷新率或撤去动画。
同时复核前缀动画、窄标题截断及真实 HTTP/HTTPS；短阶段可以
转瞬即逝，同步脚本/排版期间动画可暂停，不重入消息泵。A 显示时加载 B，A 页内跳转
仍滚动且 B 随后提交；外链 C 替换 B；B 失败保持 A 新片段地址和原标题。不继续 OS 版本取证。
随后复核内容拖动/点按、native 拖选、旋转及地址栏回车，
并继续同文档/跨页滚动恢复矩阵；随后独立
修正 Debug 增量构建时间依赖，不同时扩大键盘/IME 或关闭生命周期接线。
并行 Media 下一步：按同一 pin/许可规则补 TS/MPEG 的实际帧/PCM、时间戳及生命周期夹具，
再取舍 AMR 与 IMA；AVI/MJPEG + MP3 初始合同退出待实现候选，所有产品修正留在
positron_media，宿主只做断言。媒体路线图与现有限制已复核，时钟/同步/DirectShow 缺口仍有效。
DB 的空间不足、跨进程锁及进程重启 journal/outbox/cursor 恢复仍为独立候选；
不把正常关闭重开写成断电恢复，不在媒体纵切中新增应用 worker。
执行新设备门前仍须重新检查编译竞态和 guest DLL 引用；破坏性恢复测试须另行明确授权。
遵守用户的 SD 目标，不自行回退内置、重置、强杀或改共享设置；不把可枚举文件或已通过的
Release 合同改写为完整 Debug/生产设备基线。
About 章节位置、Debug 时间、Release CAB 安装版本，以及内部页面的地址栏/菜单/history/quit
与语言、旋转人工验收仍为独立 backlog；旧 About 包已清理，复核须部署匹配的新包。

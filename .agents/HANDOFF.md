# 当前交接

本文件只保存当前事实、候选、风险和唯一下一步。稳定边界见 [架构](../docs/ARCHITECTURE.md)、[能力矩阵](../docs/CAPABILITIES.md) 和 [测试合同](../docs/TESTING.md)。

## 使命与当前目标

Positron 为 WM6 / CE 5.2 ARMV4I 提供九个资源有界、稳定 C ABI 的公共 DLL 及独立消费者 positron.exe。产品语义属于 DLL，test_host 只拥有 fixture、平台接线和断言。用户要求补齐 Media 本阶段缺口；输入错误、WAV PCM8/16 与 IMA、短小 H.264/AAC/MJPEG/MP3、TS/PS MPEG/MP2 与 AMR-NB/WB、MPEG-4 Part 2 解码/守卫合同已通过 Debug/Release SD 设备门，下一条纵切为 AAC seek PCM 保真性。Fragment CSS 坐标和 DB 离线契约验收继续有效，用户更换设备后 SD 卡部署、回读与 DB Debug 正式验收均完成。既有 EXE 人工验收仍为 About 章节位置、Debug 构建时间和 Release CAB 安装版本，分别验收，不改变公共 ABI 或打包版本规则。

用户已确认 WinWorld 菜单展开/收起、SVG 与作者按钮外观正常；性能修正后的实际展开/收起不超过约 2 秒，期间地址栏与菜单可响应。不要恢复旧的“按钮无响应/图标仍损坏”假设；Release 点按耗时对照仍未完成，不把 Debug 体验扩大为全部设备的保证。

WinWorld operating-systems 图片尺寸修复已通过双配置自动门及用户实页视觉验收；本项关闭。脚本长调用、Debug 默认预算及失败策略仍待处理，Media 并行改动不纳入本批交付。

## 当前代码与所有权

分支 main。并行性能变更已由 df205495 提交，后续 DB 测试提交以 Git 为准；内部页面提交只包括 positron_app、专用检查脚本及其文档。共享 main.c 中原有性能计时与退出日志已保留，不纳入重复修正。本批新增的 device_tools 模块审计属于设备门前置能力，已与内部页面改动分开。

内部注册表区分 newtab/about/history/downloads/settings、version/system 别名、quit 命令及原 welcome/controls。内部页使用双语嵌入资源、Core 渲染、24 项有界焦点目录，不请求外部资源；只有可信 settings 创建最小服务 ScriptSession。默认启动/主页读取固定起始页偏好，新标签仍为 newtab。history 只读 Browser 导航栈；动态 HTML 上限 128 KiB。quit 仅允许地址栏直接提交，沿既有 WM_CLOSE 关闭流程。设计与后端进入条件见 [接线计划](../positron_app/INTEGRATION_PLAN.md)。

## 候选与验证证据

### EXE 固定起始页持久化实际门通过

普通 Debug/Release 启动已消费 EXE 同目录 positron.db；可信 settings 只编辑
newtab/welcome/controls，UI 分批 bootstrap/作者代码和服务 Pump，单所有者 DB worker
异步读取/保存，COMMIT 后更新缓存，退出先撤销服务再排空。公共 DLL/ABI、CAB 配置和
其他 agent 文件未改；存储不可用不删除文件或切换数据库。

`tmp/device-runs/20261006-143251-app-settings-live-fixed/app-settings-live/result.txt`
为 PASS：实际页面保存 welcome、新进程恢复、接受写入后退出排空、再次启动恢复 newtab
四项通过；每项退出后 guest holders=0 unavailable=0，EXE/九 DLL 回读 10/10、crash 门通过。
应用包和测试数据库保留在 SD 门根的 `app-settings-live-fixed-20261006-143251`，四个
测试进程均已退出；同包 app-history 门通过，回读 10/10、crash_check PASS。
PID 2006665102 留在 newtab，供人工设置页验收；不沿用旧包/PID。
Debug `20261006-144626-app-settings-adjacent-debug` 与 Release
`20261006-144728-app-settings-adjacent-release` 的 `136,1321,1341,999` 各 4/4，
唯一 PASS、完整日志、零 ERROR/FAIL、空间/引用/crash 门通过；日志回收后清理这两个测试目录。

初始设置页刷新改用稳定 URL 副本，防止替换页面时同一地址缓冲区被清空；Debug 门新增
实际地址断言。终态日志先于异步退出，门仅对该测试 PID 有界等待，其他 holder/不可用
仍立即拒绝。store 14 项、services 11 项、守卫/等待 13 项、审计工程范围 3 项、C89 与
正式双配置/Release 夹具排除通过。构建日志在 `tmp/app-settings-live-builds/`。
早期 device=5、脚本函数缺失、立即审计误判与审批超时的证据保留，不追认为 PASS。
ROADMAP 已复核；访问日志/下载独立纵切，触摸/双语/键盘/旋转进入人工门；用户授权先接线
不等于 FULL/I/O/迁移提交/断电故障通过，暂缓项不重开。

### EXE 多标签自动门通过，人工入口已更新

EXE 单窗口最多四个独立 AppTab，左软键 Tabs/标签页包含后退、条件前进、页面目录、条件
New tab、刷新和关闭；右菜单不重复后退/前进/刷新。前进只在当前页 Browser history 有目标
时显示，刷新紧邻关闭上方。每页独立 DOM/styles/session/history/native 控件、输入选区和滚动位置。后台仅继续
网络，隐藏页解析/脚本/提交待激活；共享窗口消息携带不复用 tab ID，候选 timer ID 进程唯一。
关闭先协作取消并等待 worker，slot 排空后释放；最后一页关闭先创建 newtab，应用退出关闭全部。
页面 teardown 先于 native/session 释放，关闭期间拒绝 queued timer。未改 DLL/ABI 或增加 IPC。

截图 `tmp/QQ20261004-094635.png` 的旧页 EDIT 透出像素源于全局 context 覆盖。
现按父 HWND 查找 owner；固定槽位覆盖 count 提交前的初始化消息，销毁子控件后注销 owner。
不靠重建页面或额外 repaint 绕过；诊断/夹具仅进 Debug。

当前完整 Debug 包为 `tmp/device-runs/20261004-175348-app-tab-menu/`，49 文件、两次 guest
holders=0 unavailable=0、EXE/九 DLL SHA256 10/10、全部 required 自检及 crash_check PASS。
PID 3172118362 留在 newtab，入口
`\Storage Card\Temp\Positron-device-gate\app-tab-menu-20261004-175348\positron.exe`。
tab-menu 自检 OK、tabs phase=8：验证左右菜单归属与顺序、重复重建无残项、后退出现/前进
隐藏、跨标签条件隔离、刷新不增加 history、分支导航清除前进项。保留双 controls 页切回及
关闭另一页后的实际 EDIT 编辑、SELECT 数量、toggle 和九次 native paint，以及原状态隔离、
容量、beforeunload、worker/timer/stale/失败/关闭断言。菜单触摸和切回绘制须人工复测；
网络、旋转/SIP、内存压力及多页退出仍待人工，不以夹具替代。
先行 native-edit 包的启动门超时不转为 PASS；其 readonly-verification 通过，旧 PID 已由用户退出。

失败探针现按 Core 几何滚动并显式发送 WM_PAINT、按文本长度设置末尾光标；精确断言保留。

相邻 Debug 门 `tmp/device-runs/20261004-093134-app-tabs-adjacent/` 为
`136,407,1080-1083,1134,1327,1336,1338,999` 的 11/11 PASS，完整日志、唯一 TESTBENCH PASS、
零 ERROR/FAIL、Core 路径及 crash_check PASS；回收后本轮远端目录已删除。
串行正式 Debug/Release build/CAB、C89、审计及 Release 诊断排除通过。VS2008 msenv.dll
自身崩溃的失败日志在 `tmp/app-tabs-build-failures/`，重试恢复，不计作 rebuild PASS；用户授权
后只终止已核对身份的崩溃 devenv PID30144，未终止其他构建或设备进程。
失败包保留，未扩大预算；并行 Media 文件不纳入本批提交。

### Core HTML 图片尺寸自动与实页视觉验收通过

Core 以有界 presentational hints 处理 img width/height，作者 CSS 仍可覆盖；修正直接 flex
图片丢失 replaced object、自然宽度覆盖声明尺寸、192 DPI auto 半尺寸，以及尺寸属性 mutation
后旧布局未失效。公共 ABI、EXE、Browser、Image/HTTP 接线不变，cache 自然尺寸不变。
稳定合同见 [Core README](../positron_core/README.md)，根因和夹具边界见
[尺寸取证](../docs/history/CORE_IMAGE_DIMENSION_REVIEW.md)。

Debug `tmp/device-runs/20261004-004226-core-image-dimensions-debug-final/` 为
`19,20,39,42,1313,1315-1319,1330,1340,999` 的 13/13 PASS；Release
`tmp/device-runs/20261004-004331-core-image-dimensions-release-final/` 加 TEST13 为 14/14 PASS。
唯一 TESTBENCH PASS、零 ERROR/FAIL、完整日志、Core 路径及 crash_check PASS；240×320、96 DPI
DeviceEmulator 上 TEST1340 独立覆盖 96/192 DPI。两个配置均在 SD 部署，双空间预检与
guest holders=0 unavailable=0 通过；Debug 本轮目录删除，Release 删除失败保留。
C89、审计和串行正式 Debug/Release build 通过，既有上游警告保留。

WinWorld 视觉证据在 `tmp/device-runs/20261004-004714-core-image-winworld-app-review/`：
完整 Debug 包、module audit、EXE/九 DLL SHA256 10/10、实际 operating-systems 提交、
image scan=7/7、resources=6/6/0/0 与 crash dumps=0。辅助 smoke 误匹配字段的超时经
只读复核修正，未重复启动。用户已明确确认四张 PNG 实页视觉通过，不由离线尺寸断言替代，
也不外推到所有页面/OEM/高 DPI；该应用已为标签开发正常退出，不再作为当前人工入口。

### EXE 地址栏复用与加载标题自动门通过

EXE 私有 AppAddressBar 在同一矩形交替显示 GDI 展示层和原生 EDIT；非编辑时加载显示网址与
循环填满/清空、结束显示标题，长标题按实际字体往返滚动，无标题回退网址。点按/Tab/菜单
切回 EDIT；原生 EDIT 始终存网址，提交仍走原地址路由。编辑期间后台提交不覆盖输入或选区，
Esc/失焦后显示最新有效地址。160 ms 系统 timer 只重画地址栏，填充周期为 4 秒，短标题和编辑状态停止；复用有界
双缓冲，不触发页面 layout/paint 或脚本 checkpoint。DLL、ABI 和导航资源语义未改。

EXE 私有 AppLoading 的原生标题现仅显示双语请求/
DNS/连接/TLS/发送/等待/响应头/正文/重定向，以及 HTML/脚本/样式/图片/排版/绘制阶段。
主文档 GET/POST 订阅公开 PHttp_*UrlEx2 observer；子资源保留宿主资源阶段，WinInet 合并
通知只显示通用请求文字。COMPLETE/FAILED 不冒充页面完成、不改变 Browser gate 或重试。
标题栏前缀动画已移除；worker 只写自身原子展示字段，250 ms UI timer 只显示当前 generation，
仅阶段文字变化时更新。成功首次绘制后显示本地化应用品牌 `Positron Browser` / `Positron 浏览器`，
文档标题仍显示于地址栏，失败沿原快照回滚。不触发 layout 或脚本 checkpoint。
Debug 自检覆盖 observer 映射/合并/终态与 size/version 拒绝、纯阶段文字和容量；独立候选夹具
验证正文进度、stale 隔离、标题更新不增加布局和成功标题恢复。诊断与夹具不编入 Release。

用户确认填充动画观感正常，但点按编辑会回到未刷新的旧页，并非仅动画暂停。EXE 消息分派
现将地址栏 EDIT/展示层和私有 worker 消息与 CommandBar/IsDialogMessage 过滤隔离，原生 EDIT
通知不作菜单命令。Debug 新增 focus-only 与 explicit-submit 诊断；候选夹具经生产消息分派验证
B 加载中点按编辑但不提交，generation/history/旧文档/session 保留，B 完成且输入和选区不变；
只有显式 Enter 提交 C 才替换 B。真实触摸回滚原因仍待新日志确认，不以自动消息证明已根治。

英文品牌已修正为单数 Browser，先行品牌包已由用户退出；构建失败日志保留在
`tmp/app-brand-build-failures/`。真实点按后 B 继续提交、中文 IME、动画开销和旋转仍待人工。
日志脚本误传 Action 曾重启旧包并删除日志，用户已退出；只读抓取必须用 PullOnly，
不从旧启动日志推断回滚。

HTTP/TLS `3,4,999` 的先行门证据为 `tmp/device-runs/20261003-102633-http-observer/`，
空间、引用审计、完整日志和 crash_check PASS，不代表 EXE 自检；其 CabWiz 失败已由正式
Release 重试恢复，失败记录不改写。并行 Media 改动不纳入应用提交，tmp 不入 Git。

### EXE 系统信息证据与暂缓边界

系统信息稳定查询/缺失规则见接线计划；不从 CE build、AKU、PocketPC 或组件产品名猜测
WM/WEH。证据 `tmp/device-runs/20261003-091819-app-system-registry-final/` 为完整 31 文件包、
双次 holders=0 unavailable=0、EXE/九 DLL SHA256 10/10、全部所需自检、about#system 单次
提交及 crash_check PASS。正式 Debug/Release/CAB、C89 与诊断排除通过；旧进程已正常退出。

320×320、128 DPI DeviceEmulator 实读 Windows CE 5.2.23090（platform ID=3）、PocketPC、
AKU `.5.3.0`；ProductName/OSVersion error=2，四个专用键也无营销版本。组件版本函数存在，
coredll/aygshell 在 size 阶段 error=1814，不是 API 缺失。注册表未写；SDK platform-version
SPI 只报告 CE 主/次，不用作发行版本。夹具保留双语/未知 ID、UTF-16、registry 类型/容量/
终止、版本切片与 HTML 转义守卫；误拒绝 OEM 实际 WM 名称的断言已删除，最终包重新验收。
系统章节视觉和中文设备仍待人工，不沿用旧路径/PID。

OS 产品名称、发行版本和 edition 的进一步动态识别已按用户决定暂时挂起；现有查询与缺失
状态保留，不撤回 AKU 或加入版本查找表。重新开启条件见 ROADMAP 的暂缓队列；本轮导航修正
不重新开启系统信息取证，以上细项仍来自对应系统信息包。

### EXE 页面内容拖动自动门通过

用户要求无需抓滚动条即可拖动页面。EXE 私有 AppInput 以 WM6 pointer/capture 和 DPI 阈值
分类 tap/pan；页面 click 延迟到未拖动的抬起，纯双轴拖动复用 retained pixels、native 重定位、
extent clamp 与 Browser scroll 通知。取消、capture 丢失、失焦、隐藏、resize、旧页 teardown
和关闭清除待定输入；Core 内部滚动条优先，native EDIT/SELECT/toggle 原消息路径保留。
不修改 DLL 或 ABI，不引入 WAG、惯性、回弹或 nested 内容区滚动链。

先行正式 Debug SD 门 `tmp/device-runs/20261003-041056-app-pan-adjacent/` 为
`42,1080,1109,1327,999` 的 5/5 PASS，完整日志和 crash_check PASS，已清理远端目录。
先行完整包 `tmp/device-runs/20261003-041442-app-pan-final/` 的 31 文件、模块审计、
EXE/九 DLL SHA256 10/10、pointer/history/internal-pages 与 crash_check 均通过；覆盖 tap
一次、双轴拖动不误点、capture/cancel/stale、clamp、Browser 同步和 layout 计数不变。
320×320、128 DPI 的旧进程已正常退出，不是当前人工入口。正式 Debug/Release/CAB、C89、
审计及 Release 夹具排除通过；失败 rebuild/主机异常保留在 `tmp/app-pan-build-failures/`。
静默门无设备证据；只读 WMDC 健康、changed=0，显式 PS32 原 gate 恢复，未改注册表。

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

### Media 有界压缩解码合同已验收

公共 ABI、borrowed-buffer 所有权、host-driven pump 与 I/O/probe 失败不变性保持；
产品语义只在 positron_media，宿主只接 I/O、fixture 与断言。WAV PCM/IMA、短小
H.264/AAC/MJPEG/MP3、TS/PS MPEG/MP2、AMR-NB/WB 与 MPEG-4 Part 2 已有双配置
设备合同；具体调用和 codec 限制见 [Media README](../positron_media/README.md)、
[能力矩阵](../docs/CAPABILITIES.md)。clock_us 仍忽略，同步/迟到丢帧未实现；
callback DirectShow source/native 视频与完整 ACM 枚举、性能/underrun/峰值内存及真机仍待门。

固定输入现为二十六个 procedural red/sine 文件与独立 PCM 参考，来源、CC0 和工具 pin
在 test_host/fixtures/media。本批仅增加五个 MPEG-4 Part 2 文件，原二十一个记录及生成器
不变；离线 checker 以 pinned 桌面 FFmpeg 校验内容，不代替 WM6 门。不重建固定 archive，
SHA256 仍为 df47e94961e86380e10d301918cbfd4c4db8364af595ab8931cf7474d9d498ee。

TEST1344 覆盖 MP4 Simple 320×240、ASP VGA/B 帧与 AVI Simple + MP3 stereo：
三 session 各两次解码，实际有限范围 I420/flags、PTS/duration、26 块/每声道 29952 sample
PCM 和 EOF 后 seek 零的视频取样/PCM 校验值一致。7-byte 短读、不可 seek AUTO、probe
恢复、显式暂停、音视频 STOP/恢复及负 callback、EOF 一次性、停止态和独立关闭通过。
隔行、656×480、较小应用上限与截断头拒绝且不输出。FFmpeg 3.4 未把 VOL 隔行声明
发布到 codecpar.field_order；DLL 新增每个 extradata VOL 的有界头前缀守卫，实际帧
守卫保留。无 extradata、码流内参数变化、Qpel/GMC 及坏 VOL 可选分支仍未充分验收。

最终 Debug 门 tmp/device-runs/20261006-073432-media-mpeg4-debug-final/ 与 Release 门
tmp/device-runs/20261006-073654-media-mpeg4-release-verified/ 均选择
1312,1331,1332,1333,1334,1335,1337,1344,999：selected/observed 9/9、唯一 TESTBENCH
PASS、零 ERROR/FAIL、crash_check=PASS、无新增 dump。240×320、96 DPI DeviceEmulator，
50 文件匹配正式 stage；SD、双空间、新 guest holders=0 unavailable=0 与 Core 路径匹配
通过。日志完整回收后两配置当前设备包均 removed_after_complete_log，旧残留未清理；
没有强杀、重置或改连接。构建/设备串行窗口已释放，下一门须重新审计。

初门 tmp/device-runs/20261006-001709-media-mpeg4-debug/ 在隔行打开拒绝处失败，
有效三组已解码但不是整门 PASS；原断言保留，VOL 守卫修复后重跑。尝试调用 FFmpeg
私有 MPEG4 decoder header 暴露 C99/原型不兼容，已撤回该默认路径，未改上游或 archive。
Release 两次受限构建因 CabWiz 无法创建数据文件停止；未部署，不算设备测试。
经批准在沙箱外重跑正式 build/stage 成功，未修改现有 DB/CAB 配置。
C89、仓库结构/宿主边界、固定 pin 与独立桌面内容校验均通过，tmp 不入 Git。

相邻 TEST1332 仍 PASS，但 AAC seek PCM 保真未覆盖：两配置 MP4 首次/重播幅度总和
105979027/104081756（29696 sample），ADTS 为 75047942/75047495（30720 sample）。
样本数/时间戳正确不足以证明 PCM 相同，根因尚未确定。下一条 Media 纵切先增加保真
诊断并确认 decoder 重置/priming 合同，再补 H.263/FLV、非零压缩 seek 与其他拒绝分支。
本轮 ROADMAP 已复核，移除 MPEG-4 初始 fixture 候选；KNOWN_LIMITATIONS 的 native/
时钟边界仍有效，不把此次短媒体通过升级为实时播放器完成。

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

### DB 本地/同步与结构化错误合同已验收

`test_host/db_tests.c` 的 TEST1321 保留本地 SQL/typed 数据/取消/预算/migration 和同步
authorizer、单列主键、typed JSON/Base64、outbox/分页、accepted/pull/cursor、服务器权威
冲突/retry-local/模式隔离回归。失败响应不部分修改行、outbox 或 cursor；正常文件重开
覆盖同步状态持久化，不等于 journal/断电恢复。

公共 DLL 的 size/version `PDb_OpenUtf8Ex`、`PDb_GetErrorInfo`、`PDb_ClearError` 和
`PDb_GetConnectionState` 已验收，旧返回码保持。调用者拥有 sticky 错误快照，实际事务
状态来自 SQLite autocommit；失败 COMMIT 不声称回滚，owned migration 尝试 rollback
并保留根错误。详细字段、所有权和重试边界见 [DB README](../positron_db/README.md)。

错误 fixture 覆盖 ABI/canary/UTF-8、sticky/Clear、CANTOPEN/NOTADB、FK COMMIT 重试、
RETURNING COMMIT BUSY、原生事务、READONLY 与内存页 FULL；两模式 migration 覆盖
脚本/提交失败回滚、重试及 metadata 错误拒绝。内存 FULL 不证明文件配额或卷不足，
I/O 和 rollback 本身失败仍待专用故障门，未填满 SD/object store。

结构化错误合同的 Debug 门
`tmp/device-runs/20261004-190523-db-error-contract-utf8-debug/` 与 Release 门
`tmp/device-runs/20261004-190703-db-error-contract-utf8-release/` 已通过正式 SD 包哈希、
`1321,999`、新 guest 无引用审计及无新增 crash 门。Debug 包已清理，Release 清理失败
保留残留；没有重置设备。该合同不改 EXE、应用表、HTTP worker 或 SQLite 上游。

错误测试的失败包 `tmp/device-runs/20261004-185732-db-error-contract-new-device-debug/`
与 `tmp/device-runs/20261004-185844-db-error-transaction-diagnostic/` 保留：建表准备期 FULL
仍 active，改为 INSERT 执行期 FULL 后保持自动回滚断言通过。首次文件打开的 NULL 异常
已由恢复 WinCE VFS 的 CreateFileMappingW 匿名锁映射修复；no-WAL/no-mmap 不变。
旧设备 SD 不可读、字体/.part 删除失败证据保留，不追认为 PASS。

独立文件 probe 已通过 Debug/Release 正式工程与 SD 部署门：二进制在 SD，文件分别在
SD 映射卷和内置 object-store；中文 UTF-8 路径/TEXT、typed BLOB、commit/rollback、正常
重开及 A→B→C 三个独立进程冷重开验证通过，并覆盖 migration 脚本失败的文件原子性。
两配置 `1321,999` 相邻回归通过。完整包哈希、前后九 DLL 无引用审计、无新 crash 和证据
回收均通过，验收夹具/包已清理；失败包仍保留。证据和六项门状态集中在
[DB 文件验收清单](DB_FILE_ACCEPTANCE.md)，不等于物理断电或全部文件可靠性验收。
跨进程读写/写写六进程门也已通过 Debug/Release 的 SD 映射与内置 object-store 四组：
COMMIT BUSY 保留 active/write，BEGIN BUSY 不开启事务，旧/新可见性、释放后重试和冷重开
通过；两配置 `1321,999` 相邻门、前后九 DLL 审计、哈希、空间/crash 与精确清理通过。
完整证据统一见上述清单；用户授权归档清理一个旧内置包后空间已恢复，失败包不追认为成功。
受控终止自建写进程后的 hot rollback journal 门已通过双配置、两种存储：实际未提交
spill 与有效 journal 快照在恢复前保留；新进程恢复精确 TEXT/64 行 BLOB、schema 和
integrity，继续提交后由另一新进程冷复核。完整证据及边界仍集中在上述清单。
本轮 ROADMAP 已复核，DB 下一纵切为文件页配额 FULL；I/O、迁移提交故障/版本拒绝与
HTTPS worker 仍待完成，不恢复应用正常启动或 CAB DB 发布依赖，不宣称真机断电通过。
journal 门交付后串行构建/设备窗口释放，下一使用者仍须获取新 guest 无引用审计。

### HTTP 流式 GET 与跨线程取消已验收

HTTP 新请求族及 TLS controlled transport 保持旧 ABI/1 MiB 正文限制；产品实现只在
HTTP/TLS，宿主只增 loopback/线程/sink/断言。固定预算、metadata、encoding、安全和
join/取消/DNS 边界见 [HTTP README](../positron_http/README.md#流式-get-与跨线程取消)。
这不是 EXE 下载 UI/文件保存接线，OS resolver 饱和与真实硬件峰值仍未验证。

最终 Debug `tmp/device-runs/20261005-014248-http-stream-final-debug/` 和 Release
`tmp/device-runs/20261005-014449-http-stream-final-release/` 均为 `3,4,1341,1342,1343,999`
的 6/6、唯一 TESTBENCH PASS、零 ERROR/FAIL、完整日志、Core 路径与 crash_check PASS。
240×320、96 DPI DeviceEmulator，SD 49 文件、双空间与新 guest holder 审计通过；日志完整
回收后删除本轮目录，没有强杀/重连。TEST1342 的确定性覆盖见能力矩阵；TEST1343 为实际
verified IANA HTTPS，TEST3/4 保护旧 GET/POST observer，TEST1341 保护 Browser 桥接。

截断 chunk 的 fixture 误判已修正，PROTOCOL 断言保持；失败证据在
`tmp/device-runs/20261004-230901-http-stream-contract-diagnostic/`。
正式双配置通过；无诊断 Debug 退出与 CAB 提前读取正在链接 TLS 的失败保留在
`tmp/http-stream-final-debug-vs-failed.log`、`tmp/http-stream-final-release-cab-failed.log`，
同一正式入口串行重试恢复。C89、审计和空白门通过；不纳入并行 App/DB/Media/CAB 改动。

### Browser 受控异步应用服务合同

Browser 拥有默认关闭的受控服务桥接，Script 只提供 native JSON 结果容量 Ex 原语；
可信身份由宿主授权，不按 URL/scheme 推断。容量、owner-thread 交付和撤销合同见
[Browser README](../positron_browser/README.md#受控异步应用服务)，不表示 EXE 已接线。

最新双配置合同基线是上述包含 TEST1341 的 HTTP 相邻门。Release 较宽回归
`tmp/device-runs/20261004-222549-browser-services-final-release/` 为 10/10 PASS。
完整日志、唯一 PASS、零 ERROR/FAIL、Core/crash、SD 双空间/无 holder 均通过并清理本轮目录。
隔离、失败不变性、线程/撤销及 finalizer 守卫保留，不以服务桥接代替业务接线。

宽 Debug `tmp/device-runs/20261004-221323-browser-services-final-debug/` 的 TEST1338
profile2 jQuery 超时仍为 FAIL（4649 ms、rc=-4）；隔离
`tmp/device-runs/20261004-222826-browser-services-bootstrap-debug-isolated/` 虽为 2/2 PASS，
不覆盖该失败。未改预算；单次编译成本仍待处理。其他已定位 fixture/正式构建失败由 Git
保留；服务合同不承诺任意 UI 响应。

### 脚本同步阻塞与保留的交互基线

有界初始化的状态、线程、冻结/取消/销毁合同见 Browser README。TEST1338 用实际阶段
验证每个空闲边界、非法/stale/跨线程与 callback 重入、timeout/heap 终态和旧页保留；
完整路径保留原版 jQuery/Bootstrap 的 Core/native-button 布局断言。作者脚本、GC 和任务
没有 continuation，Debug 默认预算问题没有因接口分步而消失。

既有初始化基线见 Debug
`tmp/device-runs/20261003-225011-browser-bootstrap-restored-debug-final/`（6/6）及 Release
`tmp/device-runs/20261003-224128-browser-bootstrap-step-release/`（11/11）。完整日志、
唯一 PASS、零 ERROR/FAIL、crash/空间/holder 门及正式双配置通过；Release 清理失败保留。
旧门不冒充本轮重跑，先行探针与耗时明细由 Git 和脚本阻塞审查保存。

分步门只证明安全调用边界，不保证真实 UI 响应或总耗时改善。Debug 最大 Step 1262 ms、
Release 339 ms；作者 jQuery 单次调用曾为 3130/1003 ms。TEST203 的 Debug 默认预算失败
在 `tmp/device-runs/20261003-224553-browser-bootstrap-sync-control/` 的旧同步序列
对照仍复现；这不是完整旧 DLL 二进制对照，临时代码已撤回。保持原断言，后续处理编译热点。

函数计时与失败对照见 [脚本阻塞审查](../docs/history/SCRIPT_BLOCKING_REVIEW.md)。DLL
诊断默认关闭，无 I/O/消息泵；EXE Debug 测量 Step/作者调用，Release 排除诊断/fixture。
旧 ABI、预算、程序顺序及 timeout 不变；真实硬件的单调用停顿仍待验证。

EXE 使用 generation 绑定的 16 ms 系统 timer；pending 禁止普通入口，取消/关闭在空闲边界停止调度并释放无 worker 的候选，旧页保留到提交。作者按顺序分批执行，普通异常继续，预算/fatal 关闭脚本能力；视口变化在初始化完成后同步。稳定调用及 GC 策略见 [接线计划](../positron_app/INTEGRATION_PLAN.md#阶段-2browser-scriptsession)。

先行完整 Debug 包 `tmp/device-runs/20261003-233918-app-script-steps-viewport/` 共 49 文件；双空间预检及两次模块审计 holders=0 unavailable=0。全部 required 自检 OK；script-scheduling 覆盖真实 timer/窗口消息、pending 拒绝、cancel/close、stale、旧页/history、作者顺序/异常后 DOM、视口变化与输入选区保留。EXE/九 DLL SHA256 10/10、crash_check=PASS。该包不是当前人工入口；正式 Debug/Release/CAB、C89 与 Release 诊断排除通过。用户已反馈 running-script 阻塞基本解决，单次长调用不可抢占的 DLL 边界不变。

EXE 调度的早期 phase=4 失败是 fixture 使用未接线 document.title setter，改用既有
textContent/Core 终态后通过；VS2008 失败日志保留在 `tmp/app-script-build-failures/`。
Core CSS cache/resolver 基线与失败 context 接线边界以 Git 和脚本阻塞审查为准；不恢复旧
callback 接法，也不把自动 guest 毫秒代替用户墙钟或写成整站性能保证。

## 有效边界与设备纪律

HTTP final URL、Core 资源终态和现有 SVG 能力继续有效；bootstrap-multiselect 语法边界、module/Shadow DOM、横向滚动条暂缓、SIP/IME/OEM 等见 [限制](KNOWN_LIMITATIONS.md)。settings 仅能持久化固定起始页，downloads 仍为未实现说明。HTTP DLL 流式 GET/取消已验收；应用下载文件保存、访问日志、DB HTTPS worker 和真实断电恢复不属于本批。

WMDC 连接由用户手动完成，只使用当前唯一目标；新部署不覆盖诊断包。精确清理必须取得 helper 成功摘要，不能杀 WMDC、VS GUI 或其他程序。外置卡失败时可检查空间后使用内置 Temp；日志回收前不删除目录。只在用户告知新截图时查询截图，不以旧截图推断新运行。

## 路线图复核与唯一下一步

已复核 ROADMAP，消费者授权的 DLL 前置能力分 Browser 桥接与 HTTP 流式 GET 两条纵切；
Browser 桥接与 HTTP 流式 GET/取消均已通过双配置门，DLL 前置委托完成；不继续扩大 DLL
范围。唯一下一步交回 EXE，按应用私有 worker/可信 session/文件策略接入这些公开入口，
分别验收设置与下载，不把 test_host 门当成 EXE 完成。构建/设备窗口已释放，下一会话仍
须重新审计 guest 引用。EXE 设置候选、菜单/多标签人工门和 Media/CAB 改动由各会话
维护，不纳入本批；WinWorld 图片视觉已关闭。

原路线保持：DLL 的脚本编译复用先测产物体积、峰值内存和冷/重复成本，再审查预算与
所有权；单次长调用、Debug 默认预算失败和错误后 session 策略仍待处理，不承诺冷启动抢占。
EXE 的地址编辑不提交时 B 继续加载、fragment 不打断 B、外链 C 替换 B、失败恢复 A，以及
标题、输入/IME、native 拖选、跟手滚动、旋转/语言、About/CAB 与 Debug 时间仍按各自人工门
或独立候选验收；系统营销版本识别继续暂缓。不要沿用已清理的旧包或 PID。

Media 保留 AAC seek PCM 保真、其他格式/IMA/AMR、时钟同步和 DirectShow 候选；
DB 跨进程锁与受控异常退出 journal 恢复已通过；保留文件 FULL/I/O、迁移故障、HTTPS worker
和应用持久化，不把进程终止恢复写成断电恢复。新增设备门仍须协调串行构建、重新审计 guest DLL 引用；破坏性
恢复需另行授权，日志回收前不清理，删除失败不写成已清空。

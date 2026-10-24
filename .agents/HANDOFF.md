# 当前交接

本文件只保存当前事实、候选、风险和唯一下一步。稳定边界见 [架构](../docs/ARCHITECTURE.md)、[能力矩阵](../docs/CAPABILITIES.md) 和 [测试合同](../docs/TESTING.md)。

## 使命与当前目标

Positron 为 WM6 / CE 5.2 ARMV4I 提供十个资源有界、稳定 C ABI 的公共 DLL 及独立消费者 positron.exe。Runtime 已具备元数据/app 引用、开发目录/只读资源及受限开发存储，JSON 提供严格解析/有界标量编码；产品语义属于 DLL，test_host 只拥有 fixture/接线/断言。旧 ABI/消费者及应用 v4 表不变；通用 WM provider、页面服务与完整目录应用仍未接入。

Media 的 WAV PCM8/16 与 IMA、短小 H.264/AAC/MJPEG/MP3、TS/PS MPEG/MP2 与 AMR-NB/WB、MPEG-4 Part 2、AVI H.263、FLV H.264/AAC 和 H.264 Annex-B 解码/守卫合同已有双配置设备证据；单流时钟、固定 ADTS 变化拒绝与有界 MP4 混合源时钟保持，实际设备音频时钟同步仍未实现。Fragment CSS 坐标和 DB 契约验收继续有效。既有 EXE 人工门仍为 About 章节位置、Debug 构建时间和 Release CAB 安装版本，分别验收，不改变打包版本规则。

用户已确认 WinWorld 菜单展开/收起、SVG 与作者按钮外观正常；性能修正后的实际展开/收起不超过约 2 秒，期间地址栏与菜单可响应。不要恢复旧的“按钮无响应/图标仍损坏”假设；Release 点按耗时对照仍未完成，不把 Debug 体验扩大为全部设备的保证。

WinWorld operating-systems 图片尺寸修复已通过双配置自动门及用户实页视觉验收；本项关闭。脚本长调用、Debug 默认预算及失败策略仍待处理，不由媒体门替代。

## 当前代码与所有权

分支 main；Core 自然高度及应用输入框字体/内边距修正已获用户“基本正常”确认。
HTTP 预检/命名/匹配属于 HTTP DLL；下载任务、用户确认、文件策略与 v4 应用表属于 EXE。Media 独立验证见下方。

内部注册表区分 newtab/about/history/downloads/settings、version/system 别名、quit 命令及原 welcome/controls。内部页使用双语嵌入资源、Core 渲染、48 项有界焦点目录，不请求外部资源；只有可信 settings 创建最小服务 ScriptSession。启动/主页读取完整偏好，新标签仍为 newtab。history 展示持久 GET 访问记录与独立的 Browser 导航栈文本；动态 HTML 上限 128 KiB。quit 仅允许地址栏直接提交，沿既有 WM_CLOSE 关闭流程。设计与后端进入条件见 [接线计划](../positron_app/INTEGRATION_PLAN.md)。

## 候选与验证证据

### Runtime 公共合同与目录资源组合；存储间歇失败未关闭

Runtime 只链接 JSON/DB 公共库；目录与 data provider、身份/迁移、typed SQL 及预算/释放合同见
[Runtime README](../positron_runtime/README.md)。fixture 仅处理私有已知根，不是通用 WM 适配器。
原生队列及 Core/Browser app 资源组合已有自动证据，不需新增 Core/Browser ABI；EXE provider、
页面授权/worker/SDK/示例未接线。当前 ROM 未发现 ZIP 命名文件或所查六个 Shell 注册入口，
未取得合格系统 ZIP provider；PXI/安装身份/关联暂缓，不外推所有 ROM，不引解压 fallback。

当前目标为 240×320、96 DPI、OS 5.2.318 DeviceEmulator，不沿用旧 ROM 通过结论。
Debug `tmp/device-runs/20261024-152041-db-native-diagnostics-debug-final2/` 与 Release
`tmp/device-runs/20261024-152405-db-native-diagnostics-release-paths-full/` 的
`1321,1363,1366,1368-1373,999` 均为 10/10。TEST1373 对照 UI/worker 原生文件循环、
DB 迁移/重开及双线程 64 次失败打开，验证诊断隔离；不是故障注入、FULL 或断电门。
DB 只增强既有错误文本：线程/调用作用域捕获实际 VFS 操作、原生错误和路径，不改 ABI、
返回码、回滚或重试策略。合同见 DB README；TEST1371/1372 原有边界不变。
最终路径诊断的 Debug 定向门 `20261024-152517-db-native-scope-final` 为 2/2。

存储故障仍未关闭：`20261024-145452-runtime-storage-io-release` 的 TEST1370 在
schema=1 返回 Runtime=-11/DB=-1/category=12/SQLite=14。
`20261024-152138-db-native-diagnostics-release-final` 的前八项通过，但 TEST1373
直接 CreateDirectoryW 返回183、随后属性不可取得；该路径不经过 DB/Runtime。
同源独立 Release 对照 `20261024-152324-db-native-diagnostics-release-paths/` 2/2，
不能抵消组合失败。旧 mkdir183/CANTOPEN 证据见 424a8d40，未归属 provider、VFS 或映射卷。
正式两配置构建、C89/审计有效；所列设备门完整回收、双空间/零引用/crash 检查通过，无新 dump。
两个既有 dump 属于九月旧脚本宿主；145 文件 SD 包回收后清理，四个旧不完整目录保留。
不归因缺空间、不要求重连、不改 EXE/用户 DB/WMDC；旧 WriteFile 差异与有界复制见 Git，
无失败后重试。下一步保留失败路径与父目录属性，区分组合顺序、路径和原生卷行为。

### Core 单行文本输入框自动高度已验收

旧 Core 空 input 在设备上为 140×0：text/password replaced gadget 没有文本子树，
auto height 未获自然高度。Core 的 inline-block/block 路径现使用 computed line-height，
保留作者 height/min/max、box-sizing 与零值；ABI/所有权不变，无 EXE 固定高度。
调用、TEST1346 与字体验收边界见
[Core README](../positron_core/README.md#单行文本控件尺寸)。

正式 Debug `tmp/device-runs/20261007-211227-core-height-debug-pass/` 与 Release
`tmp/device-runs/20261007-211519-core-height-release-accepted/` 各 23/23：
65–69、100–104、1056、1101、1105–1108、1172–1175、1330、1346、999。
唯一 PASS、零 ERROR/FAIL、Core 路径/完整日志/crash PASS，无 dump；320×320、128 DPI
DeviceEmulator。SD 匹配 stage、双空间及新 guest 零引用通过，回收后清理当前包，无强杀、
重连或删库。C89/审计与串行两配置 build 通过；App/Media 未提交代码未纳入提交。


### Core 选择性长文本断行的设备边界

合同见 [Core README](../positron_core/README.md#选择性长文本断行)。原 480×640、
192 DPI 双配置 11/11 证据保留于 7596ddf1 与本地日志，不扩大为全部 ROM/字体保证；
EXE 历史页人工结果见下方。

当前 128 DPI 设备 TEST1345 的中文/非 BMP 文本在合成 96 DPI 布局返回零 fragment。
含修正的 `tmp/device-runs/20261007-210628-core-height-debug-accepted/` 与撤回修正的
独立 `tmp/device-runs/20261007-211003-core-height-wrap-baseline/` 同样失败（text=60、
count=0）；完整日志无 dump，根因未定，不直接归因为缺字体。本批未改断行产品/断言，
此失败不在最终 23 项高度门中，独立待取证；按 ROADMAP 定位解析/样式/布局与字体。

### EXE 访问历史与滚动像素修正通过

持久 GET 记录最多 500 条/每页 16 条，读取按 tab/请求/generation 隔离，确认清除全局失效
缓存。存储、真实导航与固定右菜单已获设备/用户验收；历史操作只在页内，不删用户文件。
稳定策略见 [应用数据设计](../positron_app/APPLICATION_DATA_PLAN.md)。
单删已人工通过；绑定ID/COMMIT后全标签失效保持，不影响导航栈。
历史搜索消费原DB接口，文字匹配/预算/分页/输入与应用查询分离合同见应用数据设计，不改schema。
`tmp/device-runs/20261021-195333-app-downloads-name-review/` 五进程PASS：中文/ASCII/字面SQL与通配符、
查询复制/过滤游标/超限及队列、原生回车/键盘链接/旧结果隔离/编辑选区保留/显示全部；旧单删与下载门保留。
双语44焦点/断行与字段门通过，固定48容量不变；Debug/Release/CAB、C89、离线17项与Release夹具排除通过。
最新用户闭库为 `tmp/device-runs/20261021-history-fixture-user-data/`，原远端用户库未修改，不得恢复旧库复活已删记录。
历史操作键盘来源仍限当前可信文档。
用户已确认隔离80条历史夹具测试通过并退出；范围/预期见[历史验收](../docs/APP_HISTORY_ACCEPTANCE.md)，
不外推全部SIP/旋转/OEM。闭库/零引用证据在 `tmp/device-runs/20261022-history-fixture-accepted/`；
库仍为80条。全局日志却来自旧settings包，不作为本次操作日志；原用户库/备份未覆盖。

旧完整UI的两次scroll-paint失败保留：window2/dpi96/move0/pixel0,0为2701493而非16777215。
当前未改产品的基线 `tmp/device-runs/20261022-143952-app-scroll-probe-baseline/` 已通过像素段，
但完整门超90秒，用户正常退出；不追认为全套PASS。旧普通启动误等newtab的脚本错误已修正。
诊断包 `tmp/device-runs/20261022-144342-app-scroll-scene/` 只改Debug夹具：失败在隐藏前记录
visible/foreground/命中/WM_PAINT次数/DC clip，独立 `--selftest-scroll-paint` 不联网、不读DB。
app-history-scroll-two/three均PASS：96/192 DPI、九组精确像素、10/10哈希、退出零引用、无新dump。
当前设备320×320/128 DPI；旧间歇像素失败未复现、根因未定，未改正式绘制或放宽断言。
首个定向门抢在清理前取快照而FAIL，现仍在原30秒内等待退出；旧失败不抹除。无应用留运行。
正式Debug/Release及C89通过，Release无新诊断；CAB首轮抢读失败的日志保留，正式重试通过。

Core选择性断行已落实[请求](../positron_app/CORE_TEXT_WRAP_REQUEST.md)，EXE不改写文字/隐藏横轴。
旧CE双轴ScrollWindowEx报ERROR，已改单轴顺序移动、失败全视口补绘，不增加layout；
无证据的位图原点假设已撤回，旧截图/失败细节见Git。

旧 `tmp/device-runs/20261006-235934-app-scroll-axis-final/` 像素/IANA历史/crash门及用户双轴/斜向
拖动无花屏通过，断行/固定菜单复测通过，不外推全部OEM。旧失败见Git。
500条裁剪仅离线SQL证据；FULL/I/O/断电暂缓不重开，新设备门重审引用。

### EXE 完整设置与启动策略

可信 settings 通过原生 EDIT/SELECT/toggle 原子保存 v3 完整偏好；
URL/语言/脚本策略、worker 与失败保留合同见应用数据设计及接线计划。
启动激活空窗体，异步读取至多两秒后确定语言与首页，显式 URL 优先，无默认页闪现；
不可用/超时关闭网页 JS，迟到结果只更新缓存。语言重启生效，JS 开关按候选固定，
不影响现存 session 或 CSS/图片，可信设置页仍可保存。EDIT 值/disabled 投影已补；
仅成功 value 写入同步原生内容，普通 mutation 保留输入/选区。

升级/恢复、URL 预算和 JS 策略已有独立门；语言用 selectedIndex 初始化，不改 DLL setter。
Core [输入框请求](../positron_app/CORE_TEXT_INPUT_HEIGHT_REQUEST.md) 已落实，EXE 继承字号、
line-height:1/上下 2 CSS px 内边距，不设固定高度或偏移。旧客户区度量不能证明格式区完整。
`tmp/device-runs/20261007-224358-app-settings-live-font-padding/` 四进程 PASS：保存/空值/填值
均为客户区 197×23、EM_GETRECT (2,2)-(195,21)、字体 19（ascent16/descent3），双语重启、
JS 关闭保存、单 history、退出/零引用、哈希/crash 通过。用户确认字符基本正常；SIP/保存/
旋转矩阵仍待门。旧人工 PID1594425138 按用户授权由 Media 清理器终止，不再作为入口。
padding 高 30、linebox 格式区 17 的失败保留并撤回，不删断言；增量资源/pre-build 失败与
CabWiz 正式重试边界见 Git/本地日志。两配置/C89/审计/诊断排除通过，Unicode 取证保持。

### EXE 下载预检与共享记录通过自动门

消费 HTTP `9f282b56`；隐式Probe/一次UI保存续接保持。传入完整建议名，仅所有文件筛选、
NULL默认后缀，应用不限制/自动补全后缀，无bin猜测；系统Name实际隐藏后缀，修正暂缓。
其余代际、取消、失败、退出、额度/v4/journal/不覆盖合同见应用数据设计，不改DLL或存储。

已结束行新增重新下载/确认单条删除，稳定ID匹配当前快照；DB绑定ID并保护活动行，缺失ID幂等。
重下复用原URL但重新Probe/选新路径，旧记录/文件不变；COMMIT后全局失效缓存，不依赖发起页存活。
仅EXE/私有store接线，不改DLL/ABI/v4表；固定48焦点容纳16行32动作和14页面链接。

先行重下/单删五进程门及失败证据见Git；本轮相邻门保留新预检/取消无任务、活动/非法ID保护、
文件保留、46焦点和多DPI断言。Debug确认/保存替身不等于GUI；Toolhelp仍须完整零引用，容量/预算不变。

用户已验证单端点HTTPS图片保存。系统Name隐藏后缀证据在
`tmp/device-runs/20261013-140646-app-downloads-native-probe/native-picker/native.log`；
完整file.arbitrarysuffix显示file，所有文件/NULL filter均隐藏。OFN_PROPERTY探针整体FAIL。
用户暂缓替代，不恢复默认类型/自动补后缀/CE不支持hook，临时探针已撤回。
用户已确认删除与重新下载均通过，PID3603669138正常退出；不再作为未解阻塞。
手动重下日志已进入Probe，旧闭库路径见Git；Debug点按/分段日志保留，Release排除；用户专用探针已撤回。
旧输入/构建/源码模式失败保留于Git，不追认为通过；触摸/SIP/旋转/文件故障门保持。

### EXE 多标签自动门通过，人工入口已更新

EXE 单窗口最多四个独立 AppTab，左软键 Tabs/标签页包含后退、条件前进、页面目录、条件
New tab、刷新和关闭；右菜单不重复后退/前进/刷新。前进只在当前页 Browser history 有目标
时显示，刷新紧邻关闭上方。每页独立 DOM/styles/session/history/native 控件、输入选区和滚动位置。后台仅继续
网络，隐藏页解析/脚本/提交待激活；共享窗口消息携带不复用 tab ID，候选 timer ID 进程唯一。
关闭先协作取消并等待 worker，slot 排空后释放；最后一页关闭先创建 newtab，应用退出关闭全部。
页面 teardown 先于 native/session 释放，关闭期间拒绝 queued timer。未改 DLL/ABI 或增加 IPC。

旧页EDIT透出源于全局context覆盖，现按父HWND找owner并保护初始化/销毁顺序；证据见Git。

先行多标签包 `tmp/device-runs/20261004-175348-app-tab-menu/` 的完整自检、哈希/引用/crash门通过，
旧PID已退出。覆盖菜单顺序/条件前进、跨标签状态、刷新/分支history、切回原生控件编辑及绘制、
容量/beforeunload/worker/timer/stale/失败/关闭；详见Git。真实菜单、网络、旋转/SIP、内存压力
及多页退出仍待人工；先行native-edit超时不追认通过。

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

### Media 视频参数恢复已验收

ABI 1.3、产品 DLL/上游/archive 与既有合同不变；本轮只补测试和证据。调用与范围见
[Media README](../positron_media/README.md)、[能力矩阵](../docs/CAPABILITIES.md)。120 个固定输入，
旧 116 条记录不变；新增四条完整 Baseline→Main VGA/B 帧→Baseline 与三段重复控制，
分别使用 AVC3 包内 SPS/PPS 和 AVC1 原描述 1→2→1。压缩内容/AAC 不变、不重编码；
九帧 PTS 为 400..2000 ms、各 200 ms，AAC 仍 29 块。独立只读 oracle 与来源见
[媒体夹具](../test_host/fixtures/media/README.md)。

TEST1362 复用原消费端合同：默认九帧/29 块、恢复后三帧尺寸/flags/有效字节校验值
匹配初始 Baseline，所有 PCM 与重复控制相同；混合调度逐个比较内容/时间戳、两个切换点
背压/暂停、截止前 1 µs、近极大时钟、两种 seekability、7-byte 短读、禁读源及缓存重播。
参考区由八帧改为九帧以容纳新输入；原六帧/负控制前缀数量、产品容量与 ABI 不改。

带内/外带/NAL 长度守卫基线保持；既有新参数验证错误锁存 STATE，Annex-B 不变。

Debug tmp/device-runs/20261022-095919-media-return-debug-final/ 与 Release
tmp/device-runs/20261022-100732-media-return-release-final/ 均为
1312,1331,1332,1333,1334,1335,1337,1344,1347,1348,1349,1352–1362,999 的 23/23 PASS；
唯一 PASS、零 ERROR/FAIL，双配置正文相同，旧 21 项媒体及完成标记与 86f0ec35 基线相同。
两配置完整日志/Core/crash PASS，无新增 dump。
当前 320×320、128 DPI DeviceEmulator；SD 144 文件，双空间和新 guest 零引用审计通过。
两配置日志回收后清理当前目录，42 个旧不完整目录保留；object-store 可用 9469952 bytes。
无强杀/重置/WMDC 改动、普通 EXE 或用户 DB 改动；RAPI 无 guest 退出码。

媒体 DLL/stage SHA256 一致：Debug 0b7349322bacfcfb36f3860f90ff453ef6ba8b42c352f01de48a148641452cec，
Release bce903444627d63316f082471aa4119ee547d24e0654b1b1a711d78d21df7c9b。
上游与 archive 未改/重建，archive SHA256 为 df47e94961e86380e10d301918cbfd4c4db8364af595ab8931cf7474d9d498ee。

失败包 tmp/device-runs/20261022-095239-media-return-debug-probe/：TEST1359–1361 通过，
TEST1362 首个重复控制在第九帧被旧八帧参考区拒收，rc=-10、8 帧/29 块/errors=1；
完整日志/Core/crash PASS、无新 dump，不是产品参数恢复结论。修正仅在宿主参考容量，
不减少九帧预期。中断的 Release 构建无完成结果；确认无遗留构建进程后正式重试已成功。

C89/审计、120 pin、旧三组及两组恢复 oracle 和正式双配置构建通过。python 别名不可执行，
使用 C:\Program Files\Python312\python.exe，未改 OS 配置。ROADMAP/限制/架构已复核；
下一步取证 MP4 坏 NAL 长度/截断 sample。其他恢复形状、音频 PTS、故障/峰值和 DirectShow/
设备同步仍待门；tmp 不入 Git。

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

### 构建与审计边界

正式 Debug/Release build 可用；清洁 rebuild 曾因依赖顺序 LNK1181 失败，补建恢复不追认
rebuild PASS。VS/msenv 异常日志保留，不使用 SafeMode 或绕过工程。Release 排除 EXE
自检；正式 stage/helper 与匹配完整包仍须逐轮核验，不启用默认强制清理或发布 nightly。

### 设备门 DLL 引用审计

设备门以无产品 DLL 依赖的 guest helper/Toolhelp 枚举十个 DLL，仅
`holders=0 unavailable=0` 允许继续；快照不可用仍 fail closed，空/部分日志有界等待。
`TH32CS_SNAPNOHEAPS` 避免枚举 heap；只读审计不强杀，正常退出后须重新获取快照。
本轮有效证据见上方 Core 双配置门，旧 device=126/零日志不能追认为测试通过。
稳定排障见 [TROUBLESHOOTING](../docs/TROUBLESHOOTING.md)，审计等待函数的七项合成回归
保留在 `scripts/test_device_gate_audit.ps1`。旧 PID、安装时间与失败实验只代表原包，详见 Git
上一版交接；CAB 安装版本和 Debug 增量时间依赖仍是独立未决门。

### DB 受限请求与受限迁移前置合同

`PDb_ExecuteRestricted` 与新的 `PDb_GetMigrationVersion/ApplyMigrationRestricted` 是独立 DB
前置，调用/允许的 schema 子集、预算、借用、错误及回滚合同见
[DB README](../positron_db/README.md)。新迁移在 BEGIN IMMEDIATE 后比较严格版本，
SQL authorizer 保护私有表与连接策略；每文件的 DDL/DML/version 原子提交，失败不发布 report。
旧 local-full/sync/migration 不变。Runtime 已有上方开发存储与 SQL JSON，
原生异步撤销合同见上方，EXE 页面授权仍待接线；不能把快照、新原语或旧完整 migration 当作网页 SQL 授权。

当前双配置复验见 Runtime 门，TEST1366 继续保护建表/索引/DML、私有对象/非支持 SQL 拒绝、
严格版本类型/长度/内容、DDL/DML/COMMIT 回滚、预算/取消/reentry 与输出不变性。
首次 DB 门及三次 devenv 访问冲突/正式恢复证据归 Git 和本地日志，不追认失败或改变工具链。

TEST1321 的 local/sync、sticky 错误/事务状态及内存 FULL 仍通过；不改变 no-WAL/no-mmap。
文件冷重开、双存储跨进程锁与受控终止后的 journal 恢复，及 FULL/I/O/故障未验收边界统一见
[DB 文件验收清单](DB_FILE_ACCEPTANCE.md)，不在此重复每进程证据；正常回滚不等于 VFS 故障或
物理断电恢复。映射 SD 的 SQLITE_IOERR_TRUNCATE 仍使文件 FULL 整体 FAIL，用户已挂起，
不重跑或要求换设备；迁移文件故障/HTTPS worker 仍待专门门，旧清理失败不追认为成功。

### HTTP 下载预检与旧流式合同已验收

独立 Probe 请求发送真实 GET，最终头完成后关闭，不等 GUI、不读完整正文、不调用 sink。
UTF-8 建议 basename、验证器快照及真实 GET 的 header matching 由 HTTP 拥有；旧 Stream
ABI/行为、完整正文 1 MiB 与 TLS 验证不变。合同、预算、示例及元数据匹配不等于字节身份的
边界见 [HTTP README](../positron_http/README.md#先预检响应再确认下载)。EXE 自动接线门见上方。

最终 Debug `tmp/device-runs/20261013-093436-http-probe-debug-accepted/`、Release
`tmp/device-runs/20261013-093737-http-probe-release-accepted/` 均为
`3,4,1342,1343,1350,1351,999` 的 7/7、唯一 PASS、零 ERROR/FAIL、完整日志与 Core 路径 PASS。
TEST1350 覆盖名称预算/错误不变性、最终重定向头、非 2xx、未知长度、大文件预检、取消/
超时、handle 族和重复关闭，以及两次 GET 的精确正文或变化前拒收；TEST1351 为真实
verified IANA HTTPS。TEST1342/1343 与 TEST3/4 保护旧流式/TLS 和 GET/POST observer。

正式串行 Debug/Release stage、C89/审计通过。SD 71 文件与部署/内部双空间、新 guest
holders=0 unavailable=0 均通过；crash PASS，无新增 dump（运行前已有两个）。完整回收后
清理本轮目录，无强杀/重连，旧日志不完整目录保留。OS resolver 饱和、真实低资源峰值、
GUI 命名/保存、权限/取消仍需应用人工门；旧流式失败对照保留于 Git/本地日志。

### Browser 受控异步应用服务合同

Browser 拥有默认关闭的受控服务桥接，Script 只提供 native JSON 结果容量 Ex 原语；
可信身份由宿主授权，不按 URL/scheme 推断。容量、owner-thread 交付和撤销合同见
[Browser README](../positron_browser/README.md#受控异步应用服务)，不表示 EXE 已接线。

本轮 TEST1341 双配置相邻门见上方 Runtime 证据。Release 较宽回归
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

早期调度 fixture 修订与构建失败经过保留于 Git 和脚本阻塞审查，不再作为当前候选；
Core CSS resolver 的既有接线保持。自动 guest 毫秒不代替用户墙钟或整站性能保证。

## 有效边界与设备纪律

HTTP final URL、Core 资源终态和现有 SVG 能力继续有效；bootstrap-multiselect、module/Shadow DOM、横向滚动条暂缓、SIP/IME/OEM 等见 [限制](KNOWN_LIMITATIONS.md)。完整设置、持久 GET 访问记录与有界下载已接入，用户验证单端点 HTTPS 图片；新自动保存框、DB HTTPS worker 与真实断电恢复仍待门。

WMDC 连接由用户手动完成，只使用当前唯一目标；新部署不覆盖诊断包。精确清理必须取得 helper 成功摘要，不能杀 WMDC、VS GUI 或其他程序。外置卡失败时可检查空间后使用内置 Temp；日志回收前不删除目录。只在用户告知新截图时查询截图，不以旧截图推断新运行。

## 路线图复核与唯一下一步

ROADMAP/限制已复核：HTTP 预检与 EXE 接线、下载和历史既有验收保持；保存框后缀方案
按用户决定挂起，不以参数自检替代 GUI 或自动重开。具体证据与边界见各主题，避免在此重复。
设置/SIP/旋转、菜单/多标签、Core 断行设备差异和脚本性能待办保持；WinWorld 图片视觉已关闭。
ROADMAP 已复核：目录公共合同/资源组合退出待实现队列，PXI 暂缓；当前失败见 Runtime 节。
原生/DB 最小对照及调用级诊断已建立；尚未触发应用会话。唯一下一步是在实际失败中取得
确切路径、父目录属性和 VFS 原生操作，定位183/CANTOPEN，不以独立通过覆盖组合失败；之后再
投递 EXE provider/授权/worker/SDK/目录示例。不重开 FULL/断电或保存框暂缓门，不改用户 DB。

脚本编译复用仍须先测体积/内存/冷重复成本；单次长调用、默认预算及失败策略未解决。
EXE的并行导航/回滚、标题、输入/IME、拖选/滚动、旋转/语言、About/CAB与Debug时间仍按各自
人工门验收；系统营销版本继续暂缓，不沿用已清理的包/PID。

Media 下一纵切取证 MP4 坏 NAL 长度/截断 sample；其他长度切换/参数恢复形状仍待门。未知 PTS/音频侧异常、其他 edit-list/Main B 帧偏移仍待取证，不得把源时钟交付写成声卡同步。其他格式调度、MP3/其他 AAC 配置变化、极限块、非零 AAC、故障与 DirectShow 仍待门；
DB 跨进程锁与受控异常退出 journal 恢复已通过；文件 FULL 按用户决定挂起，I/O、迁移故障、HTTPS worker
仍待专门门，不把应用正常重开或进程终止恢复写成断电恢复。新增设备门须协调串行构建、重新审计 guest DLL 引用；破坏性
恢复需另行授权，日志回收前不清理，删除失败不写成已清空。

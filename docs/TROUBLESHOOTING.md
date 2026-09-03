# 故障排查

## 先确定运行的是什么

设备问题首先排除环境和二进制身份，再改源码：

1. 查看当前 Git 分支、未提交 diff 和候选目录。
2. 确认使用 `scripts\build.bat` 或 `scripts\stage.bat` 的正式配置。
3. 确认旧 `test_host.exe` 已真正退出，而不是 Smart Minimize。
4. 确认七个 DLL 与 EXE 来自同一次构建。
5. 读取完整 `test_host.log`，不要只依赖弹窗、提示音或截图。
6. 记录设备 screen、DPI、方向、系统时间和操作步骤。

Windows CE 可能在系统范围继续复用已加载 DLL。只替换一个文件、从多个 stage 目录拼包或保留旧进程，都可能造成“新 EXE + 旧 DLL”的假回归。最可靠的做法是关闭进程、使用新的隔离 stage 目录，必要时重启模拟器。

## 构建失败

查看根目录 `vs2008-build.log` 中最早的错误。

常见类别：

- `devenv.com` 找不到：安装 VS2008，或正确设置 `VS90COMNTOOLS`。
- platform/configuration 不存在：使用正式 `Windows Mobile 6 Professional SDK (ARMV4I)` 配置。
- 大量类型或语法错误从某个 NetSurf 文件开始：先检查缺少的前置头和 C99 声明位置，不要逐条修复后续连锁错误。
- unresolved external：核对 vcproj 是否包含对应转换后源文件、静态库和 WinCE shim。
- 中间块声明或 C99 初始化：修改对应移植脚本并运行 `python scripts\test_c89ize.py`。

不要用现代桌面编译器“证明”VS2008 工程可以工作；目标编译器本身就是兼容性边界。

## Stage 失败或文件无法覆盖

- 关闭设备上的测试窗口和任务管理器中的宿主进程。
- 使用 `scripts\stage.bat Debug C:\WMShare\Positron-candidate` 隔离新包。
- 不要在 build 失败后手工复制部分产物。
- 如果设备仍加载旧 DLL，重启模拟器后再运行隔离目录。
- 比较二进制时应比较同一配置和同一批构建，不以文件时间单独判定。

### 设备空间预检与旧目录清理

`scripts\device_gate.bat` 在第一次复制远端文件前查询 `-RemoteBase` 的可用空间。
RAPI 优先调用 `CeGetDiskFreeSpaceEx`；旧设备没有该导出时才回退到
`CeGetStoreInformation`。后者是对象存储的粗粒度数字，不代表 `\Storage Card` 等外部卷；
如果目标是外部卷而没有路径级结果，设备门会停止并明确报告
`storage_check=UNAVAILABLE_PATH_SCOPE`。有效结果必须至少容纳 staging 文件总大小和
1 MiB 运行余量；`storage_check=INSUFFICIENT` 或 `UNAVAILABLE` 都表示尚未部署。

预检前会枚举门自己生成的时间戳目录。目录只有在 `test_host.log` 已完整复制、连续两次
内容稳定且包含最终 `TESTBENCH PASS`/`TESTBENCH FAIL` 标记后才会删除；未知名称、没有
终态或复制不完整的目录始终保留。当前目录同样要在完整日志回收到本机后才尝试删除；超时
或日志不完整时绝不清理，便于诊断。删除失败只记录警告并保留目录，不能被当成通过条件。
本机证据中的 `prior-logs` 保存被检查过的旧日志，结果文件还会列出
`storage_*`、`prior_cleanup_*` 与 `current_cleanup` 字段；若预检在部署前停止，仍可从
`device-gate-preflight.txt` 读取同一组空间和清理摘要。

## WMDC 自动设备门：不要混淆 CoreCon 与 RAPI

WMDC 显示 USB 真机或 DMA emulator 已连接，只能证明 WMDC 当前会话可用；它不保证 VS2008 Core Connectivity（CoreCon）已经存在一个可由 `EnumerateConnections2()` 枚举的活动 `ICcConnection`。两者是不同的主机端通道，不能用 CoreCon 枚举为空推断“设备未连接”，也不应要求用户因此重新 Cradle 或重启已经正常的 WMDC 会话。

旧 CoreCon gate 能工作，是因为它没有复用 WMDC 当前会话：脚本从正在运行的 `DeviceEmulator.exe` 命令行读取 VMID，在 CoreCon datastore 中匹配目标，然后显式调用 CoreCon `Connect()`，结束时再 `Disconnect()`。这条路径完全没有调用 RAPI，所以当时无需修复 RAPI COM 注册，也无需管理员权限。它只证明“按 emulator VMID 主动建立 CoreCon 部署通道”可行，不能证明 WMDC 当前连接能够被 CoreCon 直接枚举；它也不满足随意更换当前 USB/DMA 设备且不绑定目标身份的要求。

正式 gate 改用 32 位 RAPI 1。`CeRapiInitEx()` 只是让本地客户端打开 WMDC 已有的当前会话；gate 不选择设备、不读取 VMID，也不执行启动、Cradle、断开或重置。更换设备时，只需先在 GUI 中让所需设备成为 WMDC 当前连接，再运行：

```bat
scripts\device_gate.bat -Candidate nextNNN
```

gate 对 `CeRapiInitEx()` 使用 30 秒有界事件等待。按微软 [`CeRapiInitEx` API 契约](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/aa513385(v=msdn.10))，调用者只填写 `RAPIINIT.cbSize`，然后等待 API 写回 `heRapiInit` 的完成事件；不得自行 `CreateEvent()` 并把句柄当作输入。若等待超时，gate 会调用 `CeRapiUninit()` 清理本地 RAPI 状态并退出，不会留下无限等待的 PowerShell，也不会把设备端进程当作已启动。WinSock 10101/远端主动关闭通常表示当前会话中断；先确认 GUI 的唯一连接和设备端遗留进程，再重试一次。单独的 30 秒超时还应检查 gate 是否遵守上述事件所有权，不能自动归因于主机。

### `RapiMgr`/`WcesComm` 报无效句柄

服务控制台显示 `RapiMgr` 为 Running 不能单独证明 RAPI 通道健康，但也不能据此管理或重启用户已经连接的 WMDC。本机 2026-08-24 的 WER 曾记录 `RapiMgr`/`WcesComm` 以 `0xc0000008`（`ntdll.dll` 无效句柄）连续崩溃；最终取证发现每次崩溃都与错误 gate 探针对应：当时的脚本自建、等待并关闭了本应由 `CeRapiInitEx()` 返回的事件句柄。改为等待 API 写回的 `heRapiInit` 后，最小和完整设备门均通过，期间没有新增服务崩溃。

因此遇到同类现象时，先审计调用方句柄所有权并查看 Application log 的时间关联；不要直接杀死 WMDC、修改产品代码、恢复 VMID 绑定，或为非 `0x8007007E` 错误运行 `repair_wmdc_rapi.bat`。只有正确客户端仍失败且用户确认 GUI 连接异常时，才由用户手动重建连接；连接动作不属于 gate 自动化。

### `CeRapiInit` 报 `0x8007007E`

2026-08-22 的本机取证确认：WMDC UI、DMA 会话、`RapiMgr` 和 `WcesComm` 服务都可以正常，但旧 WMDC 安装写入的五个 RAPI 相关 COM 类仍把 DLL 路径保存为字面量 `%windir%\system32\...`。现代 Windows 的 32 位 COM 加载路径没有按旧安装器的预期展开这些值，Process Monitor 可见进程在 SysWOW64 路径下继续寻找含字面 `%windir%` 的不存在文件，最终使 `CeRapiInit()` 返回 `0x8007007E`。因此，这个错误不等于设备没有连接。

现场复现时，五个 in-process RAPI COM 类的直接激活也返回 `0x8007007E`，而 out-of- process `RAPIMgr` 仍可用；这解释了“WMDC 看起来完全正常但 gate 不能启动”的表象。正式修复脚本将 Registry32/Registry64 中共 10 个已知值从旧的 `%windir%` 展开字符串规范化为现有 SysWOW64/System32 DLL 的绝对路径，报告 `changed=10`、`status=PASS`，之后定向、兼容和累计门均通过。不要因为 UI 正常就跳过 RAPI 取证，也不要手工修改未知 COM 类；若同一 HRESULT 再次出现，先确认设备仍由 GUI 连接，再运行下面的幂等修复入口并重新执行设备门。

2026-08-26 再次出现同一 HRESULT 时，Registry32/Registry64 的十个值确实全部恢复成上述旧 `REG_EXPAND_SZ` 路径；修复报告 `changed=10`，随后使用与 gate 相同结构和位数的最小 `CeRapiInitEx()` 探针得到 `S_OK`。这证明本次是主机注册状态发生回退，而不是当前 gate 的 `RAPIINIT` 布局或事件等待错误。Application 日志随后给出了更直接的来源：2026-08-25 21:38:05 的 `MsiInstaller` 1033 记录重新安装了 Windows Mobile Device Center 6.1.6965.0，时间晚于先前修复，足以解释安装器默认值为何重新出现。目前没有证据证明设备切换动作本身写了这些 HKLM 项；重启或换设备更可能只是触发新的 COM 激活，从而暴露已经回退的注册状态。若未来在没有 MSI 安装/修复的情况下再次回退，才需要跨该动作记录注册值或使用注册表写入跟踪。

正式修复入口是：

```bat
scripts\repair_wmdc_rapi.bat
```

脚本先以普通用户权限核对 DLL、注册键和原值；健康时直接报告 `changed=0`，不再请求 UAC。只有发现精确匹配的旧值时，它才请求真正的 Windows UAC 管理员令牌，只修复已知的 `wcescommproxy.dll`、`rapistub.dll`、`rapi.dll`、`rapiproxystub.dll` 相关五个 COM 类，并同时验证 32/64 位注册视图。它是幂等修复，但不能假定旧 WMDC 环境永远不会把值写回；复跑应得到 `changed=0` 和 `status=PASS`。自动化宿主的“退出沙箱”不等于取得 Windows 管理员令牌；HKLM 写入必须由实际 UAC 提升完成。

只读审计入口是：

```bat
scripts\repair_wmdc_rapi.bat -AuditOnly
```

健康时退出码为 0、`status=PASS`；发现已知旧值时不提权、不修改，退出码为 2 并报告 `status=REPAIR_REQUIRED`。`scripts\device_gate.bat` 每次启动时会执行同一严格预检：健康时静默继续，发现已知旧值时才请求 UAC 修复，然后以新进程进入正式 gate。未知值或缺失 DLL 仍会让 gate 在构建和部署前停止。

只在错误确为 `0x8007007E` 且脚本的严格前置检查通过时使用该修复。Windows 对该 HRESULT 的定义是 [`ERROR_MOD_NOT_FOUND`](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-)；它也可能指已注册 DLL 的依赖项缺失；如果 `-AuditOnly` 已经报告 `status=PASS`，不得继续反复改这十个值。其他 HRESULT、未知注册值或缺失 DLL 必须继续取证，不能手工套用注册表修改。WMDC 重装、系统更新或其他主机端修复若恢复旧式路径，可以在同一错误再次出现时重跑正式脚本。

本次迁移前尝试过“既不绑定 VMID，也不调用 CoreCon `Connect()`，只消费 `EnumerateConnections2()` 的现有连接”；即使 WMDC 已连接，该枚举仍为空。该方案已经撤回，禁止恢复边界见[失败实验索引](../.agents/FAILED_EXPERIMENTS.md)。

## 日志没有通过

按最早异常分类：

- 没有日志：确认 `auto=1`、配置文件与 EXE 同目录、目录可写。
- 只有部分测试：检查配置解析、进程异常退出、超时或前序失败。
- 有 `ERROR` 但后面继续：确认它是否是测试明确预期的失败路径；不能擅自忽略。
- 有 `FAIL`：候选不能成为基线，不通过放宽断言掩盖。
- 没有最终 `TESTBENCH PASS`：即使听到系统提示或窗口关闭，也不算完整通过。
- TEST999 没响：确认显式选择了 999、前序未失败，并检查设备系统声音设置。

## 网络和证书

先检查模拟器系统时钟。WM6 镜像常带有多年前的日期，会造成证书尚未生效或已过期的假象。

再区分：

1. DNS 是否解析；
2. TCP 是否连接；
3. TLS handshake 是否完成；
4. 证书链和 hostname 是否通过；
5. HTTP status/redirect 是否符合预期；
6. 页面是否成功提交；
7. 外部 CSS、图片和脚本资源是否完成。

不要把服务器暂时失败、网络波动或页面内容变化直接归因到布局。需要稳定断言时使用离线 fixture；真实网页仍保留为集成哨兵。

## 页面“偶尔正常、偶尔崩坏”

先确认两次操作是否真的落在同一个页面、viewport、DPI、方向和滚动位置。网络导航中旧页、待提交页和失败回滚页可能同时存在，截图需要配合日志中的最终 URL 和页面提交遥测。

若自动几何通过但截图明显错误，以人工结果为准。记录：

- 初始 URL 和点击目标；
- 最终 URL；
- screen/DPI/方向；
- 导航前后滚动位置；
- 容器和关键 box geometry；
- 对应候选目录和完整日志。

不要用全局偏移、统一清零 scrollbar 或放宽像素断言处理尚未定位的真实页面问题。先查 [失败实验索引](../.agents/FAILED_EXPERIMENTS.md)，避免恢复已知高风险方案。

## 高 DPI 与旋转

设备物理像素不等于 CSS px。Browse、重排和交互后的 restyle 都必须重新声明当前物理 viewport 和 DPI；不能把 96 DPI 固定成产品前提。

排查时至少记录：

- `GetSystemMetrics` 得到的 screen/client 尺寸；
- 实际 DPI；
- portrait/landscape；
- CSS viewport；
- 旋转前后内容位置和滚动比例；
- 交互后是否重新 layout。

只在单一 96-DPI 模拟器通过，不能证明高 DPI 路径正确。

## SIP、键盘和 IME

自动发送 `WM_CHAR`、`WM_KEYDOWN` 或 synthetic event 只能验证桥接数据，不能替代真实 SIP。

人工复核应区分：

- 单字符按键；
- SIP 候选词一次性提交；
- UTF-16 代理对；
- composition start/update/end；
- `beforeinput`、`input`、`change` 和 key event 的次序；
- EDIT、textarea、SELECT 的不同 native control 路径。

候选词只能输入一个字符时，先记录 `WM_IME_COMPOSITION` 的 `GCS_RESULTSTR`、实际 control 文本和 `EN_CHANGE` 次数。当前宿主会把完整结果一次性写入 composition selection；若仍只出现首字符，优先检查设备是否运行了包含完整 IME result 支持的 `test_host.exe`，以及 WMDC 是否部署了同一 stage 目录的 DLL。只有在完整结果已经到达 control、而页面事件仍异常时，才继续区分 composition bridge 与测试 oracle；不要用重复发送 `WM_CHAR` 掩盖真实 IME 路径。

## JavaScript

先确认当前运行的是：

- 独立 `positron_script.dll` 测试；还是
- `javascript=1` 的浏览器绑定。

浏览器绑定使用有界 native callback 槽位和有预算的共享 context。出现 native function limit、source limit、memory limit 或 timeout 时：

- 保留原有限制作为产品边界；
- 查重复注册、重复 bootstrap 和不必要源码；
- 用实际完整 bootstrap 做探针；
- 不通过提高预算或删断言掩盖生命周期错误。

默认 `javascript=0` 的 Browse 路径不应因为新增绑定而多抓脚本或改变页面行为。

## 收集一个可复现问题

最小诊断包应包含：

- commit 和 `git status --short`；
- 候选 stage 目录；
- `test_host.ini`；
- 完整 `test_host.log`；
- screen/DPI/方向和系统时间；
- 精确操作步骤；
- 必要截图；
- 预期行为与实际行为；
- 是否能在重启模拟器和全新 stage 后复现。

截图和日志留在本地 `tmp/`，不要提交。稳定结论再写入测试、限制或失败实验索引。

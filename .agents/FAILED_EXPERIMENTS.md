# 失败实验与禁止恢复边界

更新时间：2026-09-02

这里只保留未来可能重复踩坑的失败、环境陷阱和重启门槛。普通已修复 bug 由 Git 和测试保存；当前仍存在的能力缺口见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 状态

- **已撤回**：默认代码或配置已删除，不能从旧包直接恢复。
- **暂挂**：方向可能有价值，但旧方案停止；只有满足门槛才可重启。
- **已替代**：后续方案已解决原问题，记录用于防止回退。
- **环境误报**：失败来自旧进程、DLL 混用或设备环境，仍需保留流程护栏。

## 失败与暂挂

### next720 首轮设备门：pointer-interaction selector 桥超过脚本槽位 — 已替代

问题：在 Browser 已同时启用 DOM、validation、contenteditable、导航、activeElement
和 focus request 桥的标准组合上，next720 新增 `:active`/`:hover` 的 interaction
callback 后，首轮 `1164,1165,999` 设备门在 TEST1164 的 Browser bootstrap 阶段失败。
设备部署已完成，日志明确为 `native function limit exceeded`；没有证据表明 WMDC、RAPI
或设备连接异常，后续测试因 TEST1164 失败而停止。

替代方案：将 `positron_script` 的公共 `PSCRIPT_MAX_NATIVE_FUNCTIONS` 从 26 精确提升
到 27，保留固定槽位和注册失败的 fail-closed 语义；同步修订架构、脚本和限制文档，
然后用同一批 Debug 产物重跑设备门。首轮目录
`tmp/device-runs/20260902-163124-next720-interaction-pseudos/` 只作为失败取证，
不是产品基线。

决定：后续新增 Browser 全局 callback 前必须先核对完整注册组合和槽位余量。遇到同类
失败时，先依据脚本错误与门日志判断资源预算，不要杀掉或重连已经工作的 WMDC，也不要
通过跳过 callback、删减断言或扩大无界预算来掩盖问题。

### 2026-09-02 nightly 发布：受限进程误报 GitHub CLI 凭据失效 — 环境误报

问题：在受限 Codex 进程中执行 `gh auth status` 时，进程身份为
`laptop-li\codexsandboxoffline`；虽然 `USERPROFILE` 指向 `C:\Users\Joe`，它仍无法读取
Joe 用户的 Windows keyring，于是把 `lyk82468246` 报为 `invalid`。普通 PowerShell 的
用户身份 `laptop-li\joe` 显示同一 keyring 登录正常。首次 nightly 发布因此在脚本的认证
前置检查处停止，没有修改 `nightly` tag 或 release。

修复证据：在 `laptop-li\joe` 上确认 `gh auth status` 后运行
`scripts\package_nightly.bat`，成功覆盖 `nightly` pre-release 和
`positron-nightly.zip`（源提交 `989c3276`、Debug、19 个 `ZIP_STORED` 条目）。

决定：发布前必须以实际执行发布脚本的用户身份核对 `whoami` 和 `gh auth status`；仅
`codexsandboxoffline` 上的 `invalid` 不能判定 Joe 的登录失效。不要据此反复要求用户登录，
也不要在无法访问用户 keyring 的上下文中继续尝试 release API；先切换到
`laptop-li\joe`，让脚本的认证检查在任何 tag/release 修改前完成。

### next696 首轮设备门：新增 focus request bridge 超过脚本槽位 — 已替代

问题：在 next695 已占用 24 个 native function 槽位的标准 Browser 组合上新增
`HTMLElement.focus()`/`blur()` 请求桥后，首轮 `1141,1140,1078,999` 设备门在
TEST1078 的 activeElement/bootstrap 阶段失败，日志为 `native function limit
exceeded`。源码、正式 Debug 构建、staging 和 RAPI 部署均已完成；失败发生在
脚本注册预算，不是 WMDC、Core 文档或焦点断言。

替代方案：将 `positron_script` 的公共 `PSCRIPT_MAX_NATIVE_FUNCTIONS` 从 24 精确
提升到 26，保留注册失败的 fail-closed 语义，并在资源预算文档中说明 Browser 同时
启用两个可选焦点桥时会占满槽位。第二轮同一设备门使用同批 Debug payload，通过
TEST1078、1140、1141、999（4/4，唯一 `TESTBENCH PASS`，零 `ERROR`/`FAIL`）。

决定：后续新增 Browser 全局 callback 前必须核对实际注册数量；若扩容，只能调整
`positron_script` 公共预算并同步文档/设备门，不能跳过 activeElement/focus 注册、
删除断言或把 bootstrap 超时误判为 WMDC 故障。首轮目录
`tmp/device-runs/20260831-205814-next696-focus-request/` 只保留为失败取证，不是
产品基线。

### next663 首轮设备门：Browser native callback 槽位不足 — 已替代

问题：为 contenteditable 增加两个 selection callback 后，首轮 `1111,1110,1109,999` 设备门在 TEST1109 的 DOM bootstrap 阶段失败。源码和 staging 构建均成功，RAPI 部署也完成；失败发生在设备脚本 session 注册阶段，而不是 WMDC、Core 文档或测试断言。根因是 Browser 组合原本恰好占满 `PSCRIPT_MAX_NATIVE_FUNCTIONS=21` 个槽位，新 selection getter/setter 使注册超过上限。

替代方案：把脚本 DLL 的公共上限提升到 23，保留固定槽位和 fail-closed 注册错误，不通过跳过 selection callback 或删减 Browser bridge 来绕过。第二轮同一设备门使用完整 Debug staging，通过 TEST1109、1110、1111、999（4/4，唯一 `TESTBENCH PASS`，零 `ERROR`/`FAIL`）。

决定：后续再增加 Browser 全局 callback 前，必须先核对当前实际注册数量和脚本槽位余量；若需要扩容，优先在 `positron_script` 公共常量中明确调整并更新资源预算文档，不能让 DOM bootstrap 在设备上静默失败。

### 2026-08-27 TEST 13 IANA 深链资源收集崩溃 — 已替代，禁止恢复隔离绕过

现象：TEST 13 从 `example.com` 进入 IANA `help/example-domains` 时，在第二页样式/资源阶段失败；设备可能崩溃、停滞或需要用户手动结束进程。第一跳可完成，第三跳未到达。

已验证边界：替换为最小 CSS、跳过外部样式表挂载、禁用逐节点诊断、回退旧 `rel` 比较等实验都没有形成通过；只有完全跳过 `pcore_collect_resources()` 的 r40 通过了三跳，但该实验没有验证真实资源样式，不能作为产品修复。r48 在用户中断后没有完整结果，必须视为不可判定。当前工作区中的探针和短路代码也没有任何合并资格。

替代方案：干净主线复现后，源码审查确认 `pcore_collect_resources()` 的每个递归帧都保留 1024 字节 reference 和 2048 字节 URL 自动数组，深层 IANA DOM 会放大 WM6 UI 线程栈占用。next650 将这套有界 scratch 改为单次 style transaction 共享的 heap 分配，保留完整的 fetch/cache/parse/attach/free 路径；TEST1098、TEST13 三跳、21 项相关回归及人工设备复核通过。

决定：不要恢复或合并 nocollect、resource-skip、element-only、minimal-CSS 等绕过方案，也不要把低频计数约第 96 次访问当作稳定 DOM 根因。诊断分支的逐节点探针、IANA URL/CSS hook 仍没有合并资格。完整时间线、解决证据和 WMDC 前提见 [`IANA_NAVIGATION_CRASH_20260827.md`](../docs/history/IANA_NAVIGATION_CRASH_20260827.md)。

### next342 `className` raw bridge 尝试 — 已撤回

问题：在现有 bootstrap 中追加 `PDefineString('className','class')` 会与产品已有的 `PElement.prototype.className` 描述符冲突，设备启动 TEST309 时立即报告 `TypeError: not configurable`；这不是 JavaScript 执行预算或设备环境噪声。

决定：删除该重复定义，不得通过放宽 descriptor 或提高预算掩盖冲突。若未来扩展 `className`，必须沿用已有 class/classList bridge 并单独验证其动态样式语义；本批改用不冲突的 `HTMLElement.htmlFor` raw 反射完成 next342。

### next38-next43：Browse 稳定性实验 — 暂挂

问题：stylesheet metadata、`<base>`/URL alias、redirect origin、deadline 和预算整批进入后，TEST13 无法完成，不能安全归因到单一 HTTP 或样式改动。

决定：旧实验保留在远端 `codex/post-next37-experiments`，不得整体合并。每项必须独立重启，并通过 TEST13 三段导航、旋转、滚动和失败回滚。详见 [`NEXT37_ROLLBACK.md`](../docs/history/NEXT37_ROLLBACK.md)。

### next54：固定高度 overflow 全局预留 — 已替代

问题：TEST42 几何为零、箭头偏移，二次 layout 改变旧几何。

决定：只保留后续 auto-height 收窄方案；禁止恢复全局二次预留。

### next60：counter/list marker 首次 staging — 环境误报

问题：新 `test_host.exe` 与旧 `positron_core.dll` 混用，设备误报资源计数。

决定：`stage.bat` 必须先同配置构建再整体复制；使用隔离目录，不手工拼包。

### next69-next72：table-row 百分比与 inline style — 环境误报/已替代

问题：多个共享目录导致 TEST56 结果漂移，随后又暴露 inline `style=` 未进入正式选择。

决定：先排除 WM 全局 DLL 复用；保留后续 inline sheet 和 universal ancestor 修复，不放宽 TEST56/57。

### next78：布局末尾统一清零 scrollbar callback — 已撤回，高风险

问题：横屏 TEST13 从局部偏移扩大为整表异常，TEST56 失败并触发系统异常。

决定：禁止恢复 `scrollbar_set(...,0)` 的全局递归方案；必须从真实 overflow 所有权定位。

### next105/107/108：required/reset 与空控件 geometry — 已替代

问题：默认值冻结和无 CSS 尺寸夹具造成 TEST72/73 假失败。

决定：保留后续默认值和测试前提修复；不能据此宣称完整 intrinsic size。

### next115/116：普通与 block-level float 构盒 — 已撤回，方向暂挂

问题：inline probe 零宽、TEST13 导航扁平化、正文回归，TEST79 最终失败。

决定：重启前必须完成完整 box construction/normalisation，并同时通过 TEST79、TEST13 深链、旋转和人工截图。

### next140：把 TEST62 强制为 96 DPI — 已替代

问题：在 192 DPI 设备上用固定 96 DPI 绕过控件几何断言，违反动态 DPI 原则。

决定：必须使用实际设备 DPI，范围按 `dpi/96` 换算；禁止恢复固定 DPI。

### next155：BMP `WM_CHAR` 首包 — 已替代

问题：旧安全过滤器清空合法 UTF-8 高位字节，`key` 和 `beforeinput.data` 丢失。

决定：保留 JSON 字符串转义和 Unicode 断言；不能恢复 ASCII-only 过滤。

### next157-next159：代理对输入桥首轮 — 已替代

问题：标量合并正确，但测试 oracle 把先注册的 target listener 错认为已经取消。

决定：保留 target `false`、bubble `true` 的事件次序和 non-BMP JSON 修复。

### next165：六个表单属性 native bridge — 已替代

问题：注册入口从 16 增到 18，TEST110 报 native function limit，后续测试没有运行。

决定：保留 next166 的单入口 op 分发；16 槽位上限不能靠提高配额绕过。

### next219 首包：重复 URL classifier bootstrap — 已替代

问题：20,991 字符 bootstrap 在 TEST162 超过 1000ms，后续 TEST186/999 没有运行。

决定：使用共享 `ppartial` 后降到 19,735 字符并通过。禁止把超时首包写成结果，也禁止提高预算代替去重。

### next222 前置 gate：只枚举活动 CoreCon 连接 — 已替代

问题：为了取消 emulator VMID 绑定和显式 CoreCon `Connect()`，曾尝试只消费 `ConManClass.EnumerateConnections2()` 返回的现有连接。WMDC 已有可用 DMA 会话时该枚举仍为空，因为 WMDC 当前 RAPI 会话不等于活动 CoreCon `ICcConnection`。旧 next221 gate 的成功来自“VMID 匹配 datastore 后主动建立 CoreCon 通道”，不能作为活动连接可枚举的证据。

决定：正式 gate 改用 RAPI 1 直接消费 WMDC 当前唯一会话。禁止把 CoreCon 枚举为空报告成 “WMDC 未连接”，也禁止为绕过空枚举恢复默认 VMID 绑定、自动选择设备或隐式 Connect/Cradle。若未来重启 CoreCon 方向，必须先证明同一个接口能在不绑定设备身份、不改变 GUI 连接状态的情况下同时复用 USB 真机和 DMA emulator 当前会话；仅在 emulator 上成功不算通过。完整通道区别和 RAPI 环境修复见 [`../docs/TROUBLESHOOTING.md`](../docs/TROUBLESHOOTING.md)。

### next616 首轮设备门：Release/遗留进程组合 — 环境误报，已确认

问题：首轮 `next616-url-resolution-final` 选择 `13,43,1064,999` 时使用了 Release payload，真实外网导航在 1200 秒内没有写出任何测试完成标记；RAPI gate 按安全契约没有远程强杀设备进程。随后同一会话上的 Release 离线/999 探针也只留下启动头，不能据此判定 URL 解析断言失败。切回项目既有 Debug gate 后，TEST999 及 TEST43/1064/999 均正常通过；Release 停滞只保留为配置/设备兼容观察。

决定：设备门默认使用 Debug 配置；Release 结果不得替代既有 Debug 基线。若 Debug 仍在启动头停住，才在设备/模拟器 GUI 关闭所有遗留 `test_host.exe`，必要时重启设备并重新建立 WMDC 当前连接；顺序为 `999` → `43,1064,999` → 网络可用时 `13,43,1064,999`。超时日志不得写成通过证据；不要修改 gate 去强杀未知设备进程。

### next618 首轮窄门：远端关闭与错误的 RAPI 事件所有权 — 已修复

最终结论（替代本节较早的临时归因）：首次 `CeCreateDirectory` 的 WinSock 10101 确实是当时远端关闭，但后续稳定的 30 秒初始化超时来自 gate 在 `d2e33d42` 引入的实现错误。微软契约要求 `CeRapiInitEx()` 把完成事件写回 `RAPIINIT.heRapiInit`；旧实现却调用 `CreateEvent()` 并把自建句柄当作输入，随后等待和关闭错误的句柄。Application log 中 `WcesComm`/`RapiMgr` 的 `0xc0000008` 无效句柄崩溃与这些错误探针逐次对应，不能再用来证明 “主机环境自身已坏”。修复为等待 API 返回的句柄后，TEST999 最小探针及 `TEST1066,123-125,999` 完整窄门均通过，修复后的 gate 期间没有新增服务崩溃。以下失败记录保留为事故时间线，不再代表当前恢复步骤。

问题：在 next618 的本地 Debug 增量构建、完整 staging 和 Release 全量重建均成功后，窄门 `1066,123-125,999` 在 RAPI `CeCreateDirectory(\Temp)` 处失败，返回 `RAPI=0x80072775`（WinSock 10101，远端已主动关闭）。该错误发生在设备收到 `test_host.exe` 之前，因此不能归因于 TEST1066、IME 适配或测试断言。

决定：本次只保留本地构建/静态验证，不能把该目录或启动头写成设备通过证据。不要让 gate 增加 VMID 绑定、自动连接或远程强杀；应在设备/模拟器 GUI 关闭遗留 `test_host.exe`，确认 WMDC 只保留一个当前连接后重新断开/连接，再原样重跑 next618 窄门。若仍在 `CeCreateDirectory` 失败，只能说明该次会话在进入 TEST1066 前中断，不得放宽 TEST1066；它也不能解释随后由错误事件等待造成的初始化超时。

重连后的第二次尝试 `20260824-001001-next618-native-ime-result-final` 完成了同一份 Debug staging，但在打开当前 RAPI 会话后约 90 秒没有进入目录操作、部署或设备进程阶段；本地 gate 进程已安全停止，目录中没有 `test_host.log` 或结果文件。该启动头同样不是设备测试证据，后续仍应先恢复 WMDC/RAPI 会话再重跑。

第三次短探测 `20260824-001630-next618-native-ime-result-final` 在相同的 RAPI 会话建立处约 30 秒无进展后停止，仍没有设备日志；在 WMDC GUI 重新建立独占连接前不得继续重复。

随后把 gate 的同步 `CeRapiInit()` 改为 `CeRapiInitEx()`，但当时错误地自建并传入事件；`20260824-002343-next618-rapi-timeout-probe` 在 30 秒后明确返回连接超时并清理本地状态；该探针只验证 gate 的有界失败行为，不构成设备测试证据，也不改变“先恢复 WMDC 独占连接”的重试门槛。

最新完整窄门 `20260824-002732-next618-native-ime-result-final` 同样在 30 秒后明确超时，仍未部署设备程序或生成日志；它只能证明没有进入 next618 断言，不能区分主机故障与 gate 初始化实现错误。

进一步的本机 Application Error/WER 取证显示 `svchost.exe_RapiMgr` 在 2026-08-23 23:00、23:59:59 等时刻发生 APPCRASH，异常码为 `0xc0000008`，故障模块为 `ntdll.dll`。服务随后可能仍显示 `Running`，但这不能证明 RAPI 会话健康。当时将崩溃归因到主机环境，后续时间关联和 API 审计已经推翻该结论：错误的 gate 事件句柄才是这些无效句柄崩溃的直接触发因素。VMID 路径和只针对 `0x8007007E` 的注册修复仍与本次问题无关。

在一次最小化恢复重试 `20260824-003941-next618-rapi-retry` 中，Debug 构建和 staging 均成功，但 `CeRapiInitEx()` 仍在 30 秒后超时，设备端没有启动进程或日志。随后尝试按依赖顺序停止/启动 `WcesComm` 与 `RapiMgr`，被当前会话的服务 ACL 拒绝；两个服务仍显示 `Running` 且共享 PID 38056。该操作没有改变仓库、注册表或设备状态；下一次设备门应在用户以管理员权限重建服务/主机连接后进行，不要在相同权限和相同会话上循环重试。

随后在未见服务 PID 变化的情况下再次运行最小探针 `20260824-004241-next618-rapi-retry2`，构建和 staging 仍成功，但同样在 `CeRapiInitEx()` 处 30 秒超时；因此没有新增设备侧证据，也没有改变恢复门槛。

按用户要求再次运行 `20260824-101131-next618-rapi-retry3`，结果仍为构建/staging 成功后在 `CeRapiInitEx()` 处 30 秒超时，设备侧证据仍为空；在 WMDC/RAPI 主机状态改变前不再继续重复该探针。

随后按文档重启了 `C:\Windows\WindowsMobile\wmdc.exe`（旧 PID 20536，新 PID 4208），但托盘 UI 重启不会自动恢复设备连接。未进行 GUI 手动连接就运行的 `20260824-101622-next618-after-wmdc-restart` 仍在 `CeRapiInitEx()` 处超时；这次结果再次确认 gate 必须以用户在 WMDC GUI 中完成的唯一设备连接为前置条件。

用户随后确认已在 WMDC GUI 中手动连接成功；在不再触碰 WMDC 进程的前提下运行 `20260824-103254-next618-native-ime-result-final-after-manual-reconnect`，构建和 staging 成功，但 `CeRapiInitEx()` 仍在 30 秒处超时，设备端没有进程或日志。GUI 连接因此不能被误写成 RAPI 健康证据。当时结合 `WcesComm`/`RapiMgr` 的 `0xc0000008` 崩溃，曾误判下一步应是主机级恢复；后续句柄取证和修复后的成功门已经推翻该决定。

随后以管理员权限运行同一窄门 `20260824-103501-next618-native-ime-result-elevated-retry`，构建和 staging 成功但仍在 `CeRapiInitEx()` 30 秒处超时。提权不能绕过当前 RAPI 会话故障，因此不应继续以切换调用者权限作为修复策略。后续 API 审计又证明，提权当然也不能修复 gate 自身的错误事件句柄。

修复证据：`20260824-105452-next618-rapi-returned-event-probe` 的 TEST999 为 1/1 PASS；`20260824-105511-next618-native-ime-result-final-fixed` 的 TEST1066、123–125、999 为 5/5 PASS，均为零 ERROR/FAIL、唯一 `TESTBENCH PASS`。当前不再要求主机级恢复；只要用户已在 WMDC GUI 中建立唯一连接，即可直接运行 gate。脚本永远不替用户完成 GUI 连接，也不应管理已连接的 WMDC 进程。

### next668 首轮 `WM_COPY` 拦截：破坏原生 `WM_CUT` — 已替代

问题：为 contenteditable 增加外部 `WM_COPY` 处理后，首轮设备门的 TEST1115 粘贴仍通过，但剪切只留下原文；WinCE 的 EDIT 默认 `WM_CUT` 会在删除前对同一 HWND 内部重入 `WM_COPY`，直接拦截该重入会让原生默认动作提前中止。该问题不是 Browser 事务或剪贴板格式失败。

替代方案：宿主在外层 `WM_CUT` 调用原生默认过程期间设置短暂的 per-edit guard，仅放行同一 HWND 的嵌套 `WM_COPY`；外部 `WM_COPY` 仍执行有界选区读取、折叠 no-op 和超长/非 Unicode fail-closed 规则。修复后的 next668 定向门已通过 TEST1112–1116 与 TEST999（6/6，唯一 `TESTBENCH PASS`）。

决定：不得删除 guard 后把所有 `WM_COPY` 都交给原生控件，也不得对所有消息全局放行；若未来扩展剪贴板消息，必须先验证 WinCE 原生默认过程的重入顺序，并保持外部消息与内部重入边界分离。

### 早期 loading 条 — 已替代/部分暂挂

问题：父窗口绘制和 `ScrollWindowEx` 产生滚动残影或卡顿，独立 `STATIC` 在 WM6 不可见。

决定：保留 `PROGRESS_CLASS`。整页聚合进度和首屏 layout 卡顿只在实际遥测证明需要时处理。

## 重启暂挂方向的共同门槛

1. 在独立分支或独立 stage 目录一次只改变一个变量。
2. 使用同一次 ARMV4I 构建的完整 EXE/DLL 集合。
3. 保留导致旧失败的测试和真实 TEST13 深层导航。
4. 涉及布局、滚动或输入时加入旋转和人工观察。
5. 失败即撤回候选默认路径，不通过删测试、扩大预算或放宽断言继续推进。

旧的按日期调试流水见 [`../docs/history/DEBUGGING_INCIDENTS.md`](../docs/history/DEBUGGING_INCIDENTS.md)。它只用于追查历史，其中的基线和“下一步”均已过期。

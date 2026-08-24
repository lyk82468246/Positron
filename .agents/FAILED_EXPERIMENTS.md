# 失败实验与禁止恢复边界

更新时间：2026-08-23

这里只保留未来可能重复踩坑的失败、环境陷阱和重启门槛。普通已修复 bug 由 Git 和测试保存；
当前仍存在的能力缺口见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 状态

- **已撤回**：默认代码或配置已删除，不能从旧包直接恢复。
- **暂挂**：方向可能有价值，但旧方案停止；只有满足门槛才可重启。
- **已替代**：后续方案已解决原问题，记录用于防止回退。
- **环境误报**：失败来自旧进程、DLL 混用或设备环境，仍需保留流程护栏。

## 失败与暂挂

### next342 `className` raw bridge 尝试 — 已撤回

问题：在现有 bootstrap 中追加 `PDefineString('className','class')` 会与产品已有的
`PElement.prototype.className` 描述符冲突，设备启动 TEST309 时立即报告
`TypeError: not configurable`；这不是 JavaScript 执行预算或设备环境噪声。

决定：删除该重复定义，不得通过放宽 descriptor 或提高预算掩盖冲突。若未来扩展
`className`，必须沿用已有 class/classList bridge 并单独验证其动态样式语义；本批改用
不冲突的 `HTMLElement.htmlFor` raw 反射完成 next342。

### next38-next43：Browse 稳定性实验 — 暂挂

问题：stylesheet metadata、`<base>`/URL alias、redirect origin、deadline 和预算整批进入后，
TEST13 无法完成，不能安全归因到单一 HTTP 或样式改动。

决定：旧实验保留在远端 `codex/post-next37-experiments`，不得整体合并。每项必须独立重启，
并通过 TEST13 三段导航、旋转、滚动和失败回滚。详见
[`NEXT37_ROLLBACK.md`](../docs/history/NEXT37_ROLLBACK.md)。

### next54：固定高度 overflow 全局预留 — 已替代

问题：TEST42 几何为零、箭头偏移，二次 layout 改变旧几何。

决定：只保留后续 auto-height 收窄方案；禁止恢复全局二次预留。

### next60：counter/list marker 首次 staging — 环境误报

问题：新 `test_host.exe` 与旧 `positron_core.dll` 混用，设备误报资源计数。

决定：`stage.bat` 必须先同配置构建再整体复制；使用隔离目录，不手工拼包。

### next69-next72：table-row 百分比与 inline style — 环境误报/已替代

问题：多个共享目录导致 TEST56 结果漂移，随后又暴露 inline `style=` 未进入正式选择。

决定：先排除 WM 全局 DLL 复用；保留后续 inline sheet 和 universal ancestor 修复，
不放宽 TEST56/57。

### next78：布局末尾统一清零 scrollbar callback — 已撤回，高风险

问题：横屏 TEST13 从局部偏移扩大为整表异常，TEST56 失败并触发系统异常。

决定：禁止恢复 `scrollbar_set(...,0)` 的全局递归方案；必须从真实 overflow 所有权定位。

### next105/107/108：required/reset 与空控件 geometry — 已替代

问题：默认值冻结和无 CSS 尺寸夹具造成 TEST72/73 假失败。

决定：保留后续默认值和测试前提修复；不能据此宣称完整 intrinsic size。

### next115/116：普通与 block-level float 构盒 — 已撤回，方向暂挂

问题：inline probe 零宽、TEST13 导航扁平化、正文回归，TEST79 最终失败。

决定：重启前必须完成完整 box construction/normalisation，并同时通过 TEST79、
TEST13 深链、旋转和人工截图。

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

决定：使用共享 `ppartial` 后降到 19,735 字符并通过。禁止把超时首包写成结果，也禁止提高
预算代替去重。

### next222 前置 gate：只枚举活动 CoreCon 连接 — 已替代

问题：为了取消 emulator VMID 绑定和显式 CoreCon `Connect()`，曾尝试只消费
`ConManClass.EnumerateConnections2()` 返回的现有连接。WMDC 已有可用 DMA 会话时该枚举仍
为空，因为 WMDC 当前 RAPI 会话不等于活动 CoreCon `ICcConnection`。旧 next221 gate 的成功
来自“VMID 匹配 datastore 后主动建立 CoreCon 通道”，不能作为活动连接可枚举的证据。

决定：正式 gate 改用 RAPI 1 直接消费 WMDC 当前唯一会话。禁止把 CoreCon 枚举为空报告成
“WMDC 未连接”，也禁止为绕过空枚举恢复默认 VMID 绑定、自动选择设备或隐式
Connect/Cradle。若未来重启 CoreCon 方向，必须先证明同一个接口能在不绑定设备身份、不改变
GUI 连接状态的情况下同时复用 USB 真机和 DMA emulator 当前会话；仅在 emulator 上成功不算
通过。完整通道区别和 RAPI 环境修复见
[`../docs/TROUBLESHOOTING.md`](../docs/TROUBLESHOOTING.md)。

### next616 首轮设备门：Release/遗留进程组合 — 环境误报，已确认

问题：首轮 `next616-url-resolution-final` 选择 `13,43,1064,999` 时使用了 Release payload，
真实外网导航在 1200 秒内没有写出任何测试完成标记；RAPI gate 按安全契约没有远程强杀设备
进程。随后同一会话上的 Release 离线/999 探针也只留下启动头，不能据此判定 URL 解析断言
失败。切回项目既有 Debug gate 后，TEST999 及 TEST43/1064/999 均正常通过；Release 停滞
只保留为配置/设备兼容观察。

决定：设备门默认使用 Debug 配置；Release 结果不得替代既有 Debug 基线。若 Debug 仍在启动
头停住，才在设备/模拟器 GUI 关闭所有遗留 `test_host.exe`，必要时重启设备并重新建立 WMDC
当前连接；顺序为 `999` → `43,1064,999` → 网络可用时 `13,43,1064,999`。超时日志不得
写成通过证据；不要修改 gate 去强杀未知设备进程。

### next618 首轮窄门：WMDC 远端主动关闭 — 环境阻塞，未形成基线

问题：在 next618 的本地 Debug 增量构建、完整 staging 和 Release 全量重建均成功后，
窄门 `1066,123-125,999` 在 RAPI `CeCreateDirectory(\Temp)` 处失败，返回
`RAPI=0x80072775`（WinSock 10101，远端已主动关闭）。该错误发生在设备收到
`test_host.exe` 之前，因此不能归因于 TEST1066、IME 适配或测试断言。

决定：本次只保留本地构建/静态验证，不能把该目录或启动头写成设备通过证据。不要让 gate
增加 VMID 绑定、自动连接或远程强杀；应在设备/模拟器 GUI 关闭遗留 `test_host.exe`，
确认 WMDC 只保留一个当前连接后重新断开/连接，再原样重跑 next618 窄门。若仍在
`CeCreateDirectory` 失败，应继续按 WMDC/RAPI 环境阻塞处理，而不是放宽 TEST1066。

重连后的第二次尝试 `20260824-001001-next618-native-ime-result-final` 完成了同一份
Debug staging，但在打开当前 RAPI 会话后约 90 秒没有进入目录操作、部署或设备进程阶段；
本地 gate 进程已安全停止，目录中没有 `test_host.log` 或结果文件。该启动头同样不是
设备测试证据，后续仍应先恢复 WMDC/RAPI 会话再重跑。

第三次短探测 `20260824-001630-next618-native-ime-result-final` 在相同的 RAPI 会话建立处
约 30 秒无进展后停止，仍没有设备日志；在 WMDC GUI 重新建立独占连接前不得继续重复。

随后把 gate 的同步 `CeRapiInit()` 改为官方 `CeRapiInitEx()` 事件等待后，
`20260824-002343-next618-rapi-timeout-probe` 在 30 秒后明确返回连接超时并清理本地状态；
该探针只验证 gate 的有界失败行为，不构成设备测试证据，也不改变“先恢复 WMDC 独占连接”的
重试门槛。

最新完整窄门 `20260824-002732-next618-native-ime-result-final` 同样在 30 秒后明确超时，
仍未部署设备程序或生成日志；这确认阻塞仍在 WMDC/RAPI 会话，而不是 next618 断言。

进一步的本机 Application Error/WER 取证显示 `svchost.exe_RapiMgr` 在 2026-08-23
23:00、23:59:59 等时刻发生 APPCRASH，异常码为 `0xc0000008`，故障模块为 `ntdll.dll`。
服务随后可能仍显示 `Running`，但这不能证明 RAPI 会话健康。该证据把当前超时归因到
WMDC/RapiMgr 主机环境；不要为此修改产品代码、恢复 VMID 绑定或套用只针对 `0x8007007E`
的注册修复。恢复前提是用户在 GUI 中重建连接，必要时重启 WMDC/RapiMgr 或主机后再重跑。

在一次最小化恢复重试 `20260824-003941-next618-rapi-retry` 中，Debug 构建和 staging
均成功，但 `CeRapiInitEx()` 仍在 30 秒后超时，设备端没有启动进程或日志。随后尝试按
依赖顺序停止/启动 `WcesComm` 与 `RapiMgr`，被当前会话的服务 ACL 拒绝；两个服务仍显示
`Running` 且共享 PID 38056。该操作没有改变仓库、注册表或设备状态；下一次设备门应在
用户以管理员权限重建服务/主机连接后进行，不要在相同权限和相同会话上循环重试。

随后在未见服务 PID 变化的情况下再次运行最小探针
`20260824-004241-next618-rapi-retry2`，构建和 staging 仍成功，但同样在
`CeRapiInitEx()` 处 30 秒超时；因此没有新增设备侧证据，也没有改变恢复门槛。

按用户要求再次运行 `20260824-101131-next618-rapi-retry3`，结果仍为构建/staging 成功后
在 `CeRapiInitEx()` 处 30 秒超时，设备侧证据仍为空；在 WMDC/RAPI 主机状态改变前不再
继续重复该探针。

### 早期 loading 条 — 已替代/部分暂挂

问题：父窗口绘制和 `ScrollWindowEx` 产生滚动残影或卡顿，独立 `STATIC` 在 WM6 不可见。

决定：保留 `PROGRESS_CLASS`。整页聚合进度和首屏 layout 卡顿只在实际遥测证明需要时处理。

## 重启暂挂方向的共同门槛

1. 在独立分支或独立 stage 目录一次只改变一个变量。
2. 使用同一次 ARMV4I 构建的完整 EXE/DLL 集合。
3. 保留导致旧失败的测试和真实 TEST13 深层导航。
4. 涉及布局、滚动或输入时加入旋转和人工观察。
5. 失败即撤回候选默认路径，不通过删测试、扩大预算或放宽断言继续推进。

旧的按日期调试流水见
[`../docs/history/DEBUGGING_INCIDENTS.md`](../docs/history/DEBUGGING_INCIDENTS.md)。它只用于追查历史，
其中的基线和“下一步”均已过期。

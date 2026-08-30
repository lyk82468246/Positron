# 测试与验收

Positron 的测试由主机静态检查、VS2008 ARMV4I 构建、自动设备门和必要的人工设备验收组成。单一层级通过不能替代其他层级：桌面脚本不能证明 ARM 二进制可用，自动首帧也不能证明真实触摸、SIP 或视觉正确。

逐测试实现以 [`test_host/main.c`](../test_host/main.c) 为准；当前候选、测试选择和最新设备证据见 [`.agents/HANDOFF.md`](../.agents/HANDOFF.md)。本文只说明长期有效的操作和判定方法。

## 测试宿主的角色

`test_host.exe` 是公共 DLL 的回归宿主和示例消费者。它覆盖基础库、渲染、真实网页、表单、事件、history/session 和显式启用的浏览器 JavaScript。

测试编号是宿主实现细节，不是公共 ABI。增加或删除测试时，应修改源码中的 dispatch 和断言，而不是在稳定文档中维护一份容易漂移的逐编号目录。

## `test_host.ini`

INI 必须和 `test_host.exe` 位于同一目录。最小配置如下：

```ini
auto=1
javascript=0
tests=13,20,27,999
```

`tests` 接受逗号或空格分隔的编号和范围，也接受源码明确支持的特殊编号：

```ini
tests=1-5 7b 13 20,999
```

不存在 INI 时，宿主进入内置的交互分组选择。文件存在但为空、不可读或格式错误时，宿主会提示并退回分组选择，不会静默扩大为全量测试。

### 自动模式

`auto=1` 时：

- 直接运行 `tests=` 选择；
- 测试确认和结果 MessageBox 被抑制；
- 同目录 `test_host.log` 每次覆盖写入；
- 自动可视测试至少绘制一帧后自行关闭；
- 任一失败令批次失败，最终必须出现唯一 `TESTBENCH PASS` 才算通过。

自动模式只证明断言、资源计数、消息路径和首帧没有失败。它不能证明字体、边距、抗锯齿、触摸命中、系统 picker 或 OEM 输入法体验。

涉及导航的自动日志还会给出 Browser 资源事务的 `queued/ready/failed/cancelled/pending` 计数、resolve/transport/HTTP/budget/memory 失败分类，以及 `attempts/retries/exhausted` 计数和 `gate/required-failed/optional-failed` 结果。宿主负责 worker、网络 I/O、取消和何时重试，Browser 负责按 URL 去重、资源字节、终态、预算、gate、hash-only 摘要和 fallback observation；宿主只读取这些结果写日志，不复制第二份资源状态。候选本身通过 `PBrowser_NavigationCandidate*` handle 由 Browser 拥有 generation、取消/退休状态、committed/failed 终态和 pending/committed/failed/cancelled/stale 结果摘要；宿主只保留线程、消息、退休队列和页面提交策略，并直接记录 `PBrowser_NavigationCandidateGetResult` 的快照。提交边界再调用 `PBrowser_NavigationCommitGetInfo`，一次读取 candidate result 与独立 resource gate；只有组合 `decision=READY` 且 `can_commit=1` 才允许 layout/swap，最终仍需 `MarkCommitted` 重检。transport 失败每项最多重试 2 次（最多 3 次尝试）；HTTP、resolve、budget、memory 和取消不重试。style pass 发现新的 pending 时会回到 worker；layout/swap 前必须重新检查组合快照，required stylesheet/`@import` 失败、未收敛、候选过时或取消会保留旧页，optional image/script 失败可在 Core fallback 下继续。完成的候选应没有未处理的 `pending` 项；`cancelled` 是独立终态，不应被当作网络失败。日志还可输出最多 4 项不含原始 URL 的 `role/failure#hash` 失败摘要，以及 image/script/other/observed fallback family 的粗粒度计数；它们是 Browser 提供给宿主的有界观测，不是逐资源视觉保证。TEST1123 以离线夹具覆盖重复资源、三层 `@import`、摘要脱敏和 fallback observation；TEST1124 覆盖 candidate handle 的 generation admission、取消、退休幂等、过时 generation 隔离和 committed/failed 终态；TEST1125 覆盖 Browser 派生的 pending、committed、failed、cancelled 和 stale 结果分类；TEST1126 覆盖资源 gate 与 candidate result 的组合 decision、可提交标志、取消/过时/终态优先级和非法参数。

### 手动模式

`auto=0` 时保留启动确认、测试说明和人工关闭流程。可视页面通常停留在设备上，验收者按说明操作后用 `Esc`、页面空白处或测试明确提供的关闭入口继续。

交互模式不保证生成完整自动日志；截图、设备信息和操作记录应保存在本地 `tmp/`。需要机器判门时，完成观察后再用相同范围运行一轮 `auto=1`，但自动日志不能替代人工结果。

### 浏览器 JavaScript

- `javascript=0`：默认产品路径，不执行页面 classic script。
- `javascript=1`：显式启用实验性的浏览器 script session、受限 DOM/Event/input/navigation bridge 和 inline/external classic script。

独立 `positron_script.dll` 测试不依赖此开关。开启浏览器 JavaScript 也不表示支持完整 DOM、Web API、ECMAScript host environment 或浏览器安全沙箱。

### 完成提示音

TEST999 是专用完成提示音。只有显式选中、且前序批次未令进程失败时，宿主退出前才请求一次系统提示音。听到声音、进程退出或看到部分 `OK` 都不能替代最终日志判定。

## 四种配置方式

| 目标 | 配置 |
|---|---|
| 部分测试、自动断言 | `auto=1`，`tests=` 写所需编号；通常保留 `javascript=0` |
| 部分测试、人工模式 | `auto=0`，`tests=` 写所需编号；脚本 fixture 按需要启用 JavaScript |
| 所有自动安全测试 | 使用 nightly 包自动生成的 INI，或由设备门显式生成当前源码 dispatch 清单 |
| 所有测试、人工模式 | 在全量自动清单中加入发布说明列出的 manual-only fixture，并设 `auto=0` |

移走或改名 INI 只会进入旧式分组选择，不等于“自动运行所有测试”。manual-only fixture 不得放入 `auto=1` 默认清单；它们会主动拒绝自动运行，以免伪造人工验收结果。

Nightly 包会从当前 `run_configured_tests` dispatch 动态生成全量自动清单，不在脚本里硬编码每天变化的测试目录。详情见 [NIGHTLY_RELEASE.md](NIGHTLY_RELEASE.md)。

## 本机验证

### C89 转换回归

修改产品 C、移植代码或 C89 转换脚本后先运行：

```bat
python scripts\test_c89ize.py
```

对受转换器管理的源码再次执行转换应为幂等，不得靠 VS2008 恰好接受错误输出掩盖脚本回归。

### 仓库审计

提交前运行：

```bat
python scripts\audit_repo.py
```

审计覆盖工程输入、版本 pin、许可证、Git 跟踪、UTF-8、Markdown 链接、文档结构和 `test_host` 产品边界（禁止编译产品 `.c` 或定义产品公共入口）。审计成功不代替目标构建、设备行为或对宿主 helper 语义归属的人工审查。

### ARMV4I 构建

使用正式入口：

```bat
scripts\build.bat
scripts\build.bat Debug rebuild
```

局部低风险修改通常先做增量构建；工程依赖、生成规则、静态库或无法解释的混包问题使用 clean rebuild。不要直接调用 ARM `cl.exe` 拼装一部分目标。

## 自动设备门

### 前提

先由用户在 WMDC 或 Device Emulator GUI 中手动建立唯一目标连接。USB 真机和 DMA emulator 都可以。设备门使用 RAPI 1 的“当前 WMDC 会话”，因此它：

- 不枚举或选择设备；
- 不绑定 emulator VMID；
- 不启动、cradle、断开、重置或杀死设备；
- 不能替代 GUI 连接操作。

### 运行

从仓库根目录执行：

```bat
scripts\device_gate.bat -Candidate feature-name
```

默认测试选择来自 tracked `test_host/test_host.ini`。定向批次使用 staging override，不修改 tracked INI：

```bat
scripts\device_gate.bat -Candidate feature-name ^
  -TestSelection "1095-1102,999"
```

需要实验性浏览器脚本时显式加：

```bat
scripts\device_gate.bat -Candidate feature-name ^
  -EnableJavaScript -TestSelection "1095-1102,999"
```

脚本执行正式构建、隔离 staging、整包部署、启动、有限等待、日志回收和自动判门。每次运行使用唯一设备目录，本地证据保存在 `tmp/device-runs/`，不会纳入 Git。

RAPI 没有安全的通用远端终止语义。超时会保存可取得的日志并返回非零，但不会强杀设备进程；重试前应在设备 GUI 确认真正结束遗留 `test_host.exe`。WMDC/RAPI 错误按 [故障排查](TROUBLESHOOTING.md#wmdc-自动设备门不要混淆-corecon-与-rapi)处理。

### 自动通过标准

一次设备门只有同时满足下列条件才通过：

1. 构建和整包 staging 成功；
2. 启动日志来自本次唯一候选目录；
3. 每个所选测试都有预期完成记录；
4. `ERROR` 和 `FAIL` 计数为零；
5. 最终只有一个 `TESTBENCH PASS`；
6. 涉及真实 Browse 时，路由和最终页面序列符合该 fixture 的要求；
7. 没有旧 EXE/DLL 混包或遗留进程证据。

只有启动头、部分 `OK`、提示音、窗口关闭或 RAPI 成功都不是通过证据。

## 回归范围

每批测试范围应与风险相称，不需要每次都从头跑到尾。

定向门通常包括：

- 本批新增契约；
- 直接共享的数据结构或默认动作；
- 一个真实页面/导航哨兵（若变更触及页面组合）；
- TEST999。

出现以下情况时运行更宽回归或全量门：

- 多个低风险批次已经累计；
- 修改公共 ABI、所有权或生命周期；
- 修改 layout/paint、输入基础设施、网络/TLS 或资源缓存；
- 准备里程碑交付或 nightly 基线；
- 出现崩溃、超时、数据错误、混包或无法解释的行为。

全量清单从当前源码生成，不复制到本文。上一次全量与当前累计风险由 handoff 记录。

## 人工验收

以下风险必须由真实设备观察，或明确进入允许累计的人工清单：

- 字体 fallback、字形、抗锯齿、颜色和渐变；
- 左右边距、居中容器、换行、表格、列表和滚动条观感；
- 真实触摸、链接命中、键盘焦点、滚动和后退；
- SIP 候选词、IME composition、Unicode 与代理对输入；
- 旋转、screen/DPI 差异和滚动位置保持；
- 系统文件选择器、窗口返回和取消后的状态；
- OEM 剪贴板与其他应用的 copy/paste、CF_TEXT 或富文本格式互操作；自动门只覆盖宿主自备的有界 `CF_UNICODETEXT` contenteditable paste/cut/copy，折叠复制 no-op 和超长/非 Unicode 拒绝也只在该宿主契约内成立；
- loading、失败网络、旧页保留和深层真实导航。

contenteditable 的自动 fixture 证明有效 editing host 的 WM EDIT 代理、`beforeinput` 取消、Core 文本提交、`input` 顺序、脚本 `selectionStart`/`selectionEnd`/`selectionDirection` 的范围语义，以及只对实际变化分发一次的非冒泡、不可取消 `selectionchange`；设备门还验证这些范围可同步到原生 EDIT，并由原生消息触发同一 Browser 事件。TEST1113 用 WM EDIT 的 `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` 序列验证无修饰拖选的连续 forward/backward 方向和重复范围抑制；TEST1114 覆盖 Shift/方向键方向保持，以及 `WM_CAPTURECHANGED`、`WM_CANCELMODE`、`WM_KILLFOCUS`/`WM_SETFOCUS` 中断时的收尾和去重；TEST1115 覆盖宿主有界 `CF_UNICODETEXT` paste/cut 的精确 `beforeinput.data`、取消回滚、Core 单次提交、折叠选区同步，以及空或不支持格式时的 fail-closed；TEST1116 覆盖 `WM_COPY` 的非空选区复制、折叠选区 no-op、超长 UTF-8 与非 Unicode 格式拒绝，并确认 WinCE 原生 `WM_CUT` 的内部 `WM_COPY` 重入不会破坏剪切。由于 WinCE 直接 `SendMessage` 不会更新键盘状态表，TEST1114 在 key-up 前注入有界原生范围来验证宿主通知路径；真实 OEM 默认键盘、SIP 候选词、完整 IME composition、硬键盘和跨应用剪贴板格式仍应在设备人工矩阵中观察。

低风险视觉或输入变化可以累计若干批次后集中验收。崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即复核，不能等待累计窗口。

### 人工记录

每组观察至少记录：

- commit/候选名和精确 `tests=`；
- 设备型号、screen、DPI、方向和系统时间；
- 初始页面、操作步骤和最终 URL/状态；
- 预期行为与实际行为；
- 必要截图和同批自动日志。

截图和日志只放 `tmp/`。比较两张截图前先确认 viewport、DPI、方向、滚动位置和二进制身份一致，避免把网络波动、旧页、旧 DLL 或不同滚动位置误判为渲染回归。

## 网络测试

WM6 镜像的系统时间经常过旧。证书测试前先校准时间，并把失败分为：DNS、TCP、TLS handshake、证书/hostname、HTTP status、redirect、资源获取、页面解析和最终提交。

离线 fixture 用于稳定契约；真实网页用于集成哨兵。真实端点的暂时不可达不能通过放宽离线断言解决，离线契约通过也不能证明任意互联网网站兼容。资源失败应依据日志分类处理；宿主只对 transport 失败提供每项最多 2 次的有界重试，取消、预算拒绝或 HTTP/resolve/memory 错误不会重试。样式表/`@import` 属于 required，失败或未收敛时不提交候选；图片/script 属于 optional，失败时可在 Core fallback 下继续。Browser 资源事务提供最多 4 项、不含原始 URL 的 `role/failure#hash` 摘要，以及 `resource fallback image/script/other/observed` 粗粒度计数；这些结果由宿主写入日志，不提供逐资源 UI 或视觉保证。TEST1123 在离线夹具中覆盖重复资源、三层 `@import`、摘要脱敏和 fallback observation。不能把资源事务的 gate 或摘要当成完整浏览器降级体验。

## 候选成为基线

只有满足以下条件，候选才能写入当前 handoff：

1. 修改范围、ABI 和所有权清楚；
2. C89 回归与仓库审计通过；
3. ARMV4I 正式构建通过；
4. staging 文件来自同一批构建；
5. 风险相称的设备日志完整通过；
6. 必要人工验收完成，或风险明确进入允许累计清单；
7. handoff、限制、路线图和稳定文档分别只更新自身职责。

逐批设备日志不追加到本文。需要追溯时使用当前 handoff、本地证据、专用历史文档和 Git 历史。

# Positron 当前交接

更新时间：2026-08-15

稳定使命、架构和公共边界见
[`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。本文件只记录当前仓库基线、最近设备证据、
当前边界和唯一下一步。

## Git 与仓库基线

- 分支：`main`，跟踪 `origin/main`。
- 最新已验证产品基线：next235。
- next235 批次在 `positron_browser/` 产品 DLL 中加入浏览器脚本 session 所有权与 host JSON callback
  注册；`test_host` 适配与工程接线继续使用当前
  WMDC/RAPI 会话的 `scripts/device_gate.bat`、`scripts/device_gate.ps1`，环境修复脚本为
  `scripts/repair_wmdc_rapi.*`。
- 当前工作区的 `test_host/test_host.ini` 保持自动模式：`auto=1`、`javascript=0`、
  `tests=13,20,27,56,58,62,64-67,73,75,999`。这是窄的自动 smoke 选择，不是完整基线；
- 150 项自动 next235 证据已经通过；人工视觉/输入包若需要弹窗，必须临时把 `auto` 改为 0，
  验收结束后恢复为 1。
- 本地设备证据位于 `tmp/device-runs/20260815-095055-next235-script-session-final/`；脚本回归定向证据位于
  `tmp/device-runs/20260815-094818-next235-script-session-regression/`。`tmp/` 不跟踪，干净 clone
  中没有该日志，不能据此假定新环境也已经连接或通过。

接管时仍须重新运行 `git status --short --branch` 和 `git diff`；以上列表不是 Git 的替代品。

## 最近已验证设备证据

### 最新全量检查点：next235

- 配置：`TEST13/20/27/43/44/56/58-77/80-202/999`，共 150 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：150 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST202 直接验证 product script session 的创建、求值、host JSON callback 注册/注销和销毁。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context 及其 callback 注册/调用生命周期；
  `test_host` 的 bridge 只保留 core/窗口/导航适配和一个只读诊断 runtime 借用句柄，产品 session
  是唯一销毁者。bootstrap 文本和 DOM/Event/form/input/location callback 实现仍在宿主，下一批迁移。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、15 项隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接
  `TEST202/999` 证据位于 `tmp/device-runs/20260815-094753-next235-script-session/`，
  脚本回归 `TEST112-135/137-152/189-202/999`（55 项）位于
  `tmp/device-runs/20260815-094818-next235-script-session-regression/`，全量证据位于
  `tmp/device-runs/20260815-095055-next235-script-session-final/`。

### 上一全量检查点：next234

- 配置：`TEST13/20/27/43/44/56/58-77/80-201/999`，共 149 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：149 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`。
- 产品边界：新增 `positron_browser.dll`，以独立 opaque history/session handle 拥有
  history entries、state、document identity、same-origin/default-port 判断、文档导航提交、
  push/replace state、traversal target 和 pending-navigation projection。`test_host` 的旧
  history 调用现在只通过该 DLL，固定数组仅作断言镜像；TEST201 直接调用公共 API。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、15 项隔离 staging/部署、SHA-256 清单和日志自动判门均通过。定向
  `TEST201/999` 证据位于 `tmp/device-runs/20260814-234503-next234-browser-history/`，
  `TEST149-201/999` 证据位于 `tmp/device-runs/20260814-234517-next234-history201/`，
  参数边界定向证据位于 `tmp/device-runs/20260814-235415-next234-browser-history-contract/`，
  修订后全量证据位于 `tmp/device-runs/20260814-235642-next234-browser-history-final2/`。

### 更早全量检查点：next233

- 配置：`TEST13/20/27/43/44/56/58-77/80-200/999`，共 148 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：148 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`。
- 能力终点：`history.pushState`/`replaceState` 的第三个 URL 参数显式为 `undefined` 时使用当前
  document URL；显式空字符串仍表示当前 URL 的同 URL history entry。两种写法都不发 GET，保留
  state/length、traversal、popstate/hashchange 顺序和后续 replace/push 行为；既有安全
  absolute/root-relative/document-relative pathname、query/fragment 和拒绝规则不变。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、14 文件隔离 staging/部署、SHA-256 清单和日志自动判门均通过。定向
  `TEST149-200/999` 证据位于 `tmp/device-runs/20260814-231852-next233-history200/`，
  全量证据位于 `tmp/device-runs/20260814-232021-next233-final/`。

### 更早全量检查点：next232

- 配置：`TEST13/20/27/43/44/56/58-77/80-199/999`，共 147 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：147 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`。
- 能力终点：`history.pushState`/`replaceState` 支持安全的同源 absolute/root-relative pathname，以及同源根相对
  path/query/fragment、query-relative URL、裸单段/多段 sibling 和显式 `./` 单段/多段 sibling URL；显式
  `./?query`/`./#fragment` 会落到当前目录的 trailing-slash URL，且同文档 traversal
  恢复 URL/state 并按 popstate 后 hashchange 排序；裸 `./`、`../`、dot segment、
  重复分隔符、编码 dot segment、protocol-relative 和跨源 URL 仍拒绝；普通 percent-encoded
  pathname segment 可以保留；同源 absolute/root-relative URL 在
  path 完全相同的前提下可以更新 query/fragment，HTTP 默认端口 80 与 HTTPS 默认端口
  443 在同源比较中按无端口形式等价处理。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、14 文件隔离 staging/部署、SHA-256 清单和日志自动判门均通过。定向
  `TEST149-199/999` 证据和默认全量 gate 均已保存到上述 `tmp/device-runs/` 路径。
- gate 会在设备端只回收自己命名的旧候选目录；本次因旧目录占满 `\Temp` 暴露并验证了该
  回收路径。WMDC 旧 COM 注册的 5 个 RAPI 类已由正式修复脚本做 32/64 位幂等验证。

### 最近定向检查点：next219 修正版

- 配置：`TEST13/151-186/999`，共 38 项。
- 结果：37 条标准数字 `OK`、1 条 TEST13 overview、零 `ERROR`、零 `FAIL`、最终
  `TESTBENCH PASS`。
- 能力终点：根相对 URL 的末尾半编码 double-dot segment。
- 首包 `C:\WMShare\Positron-next219` 因 20,991 字符 DOM bootstrap 在 TEST162 超过既有
  1000ms 预算而失败；修正版使用共享 `ppartial` helper 将 bootstrap 降到 19,735 字符，
  没有提高预算。旧首包不得作为基线。

### 仍有效的人工证据

- next167 已人工确认 example.com `Learn More` 后页面容器边距正常。
- next167 已人工确认真实 SIP 候选词可以完整提交，不再只输入下一个字符。
- 2026-08-14 的 TEST75 纵向/横向截图（`tmp/QQ20260814-195629.png`、
  `tmp/QQ20260814-195643.png`）显示灰色定位父框和四个彩色元素均在预期坐标内；
  小色块内文字的溢出是 fixture 的预期内容，不是布局回归。
- 视觉、真实触摸、SIP、旋转和失败网络允许累计后集中复核；崩溃、数据损坏、严重布局
  破坏或核心交互阻塞必须立即检查。

## 已关闭批次：next232

目标：让根相对 history state URL 与既有安全 absolute/document-relative pathname 使用同一套
路径段规则；继续在不发 GET 的前提下保留普通 percent-encoded pathname segment，并保持
state、length、traversal、popstate/hashchange 行为可预测。编码 dot segment、重复分隔符、
跨源和 protocol-relative URL 仍明确拒绝。

实现边界：

- JS bootstrap 的 `phistoryRelativePath` 保留 raw dot segment、重复分隔符和父路径拒绝；
  根相对 URL 先剥离 query/fragment，再复用同一校验，普通 percent-encoded segment（例如
  `%2Ebook`、`file%2Ejson`、`%2Fencoded`）仍可保留，但完整编码/混合编码的 `.`、`..`
  segment 仍拒绝。
- C 侧 history bridge 未扩大 URL parser，只沿用已验证的同源判定和 HTTP `:80`、HTTPS `:443`
  默认端口等价规则，把安全 pathname 结果写入现有历史条目。
- TEST149、TEST189–194、TEST195、TEST196 的旧“任意 absolute path 都拒绝”负例继续覆盖
  真正不安全的 dot/repeated-separator path；TEST197 覆盖安全 pathname replace/push；
  TEST198 新增普通 percent-encoded segment replace/push、无 GET、state/length、traversal、
  popstate/hashchange 和编码 dot/cross-origin 拒绝覆盖；TEST199 覆盖根相对与
  document-relative 普通编码段、根相对不安全路径拒绝和同样的 traversal 事件门。
- 默认 `javascript=0`、TEST13 行为、公共 ABI 和 callback 总上限不变。

自动化同步完成：

- `device_gate.ps1` 使用 WMDC 当前 RAPI 会话，不依赖 CoreCon 活动连接或 VMID；支持
  `-TestSelection` 只覆盖隔离 staging 的测试选择。
- gate 只回收 `\Temp\Positron-device-gate` 下符合自身 candidate/timestamp 规则的旧目录；
  未识别目录保留。
- `repair_wmdc_rapi.bat/.ps1` 自行请求 UAC，幂等修复 5 个已知 WMDC RAPI COM 类的 32/64
  位旧 `%windir%` 路径；未知注册值拒绝修改。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- PowerShell 解析、修复脚本 `changed=0/status=PASS` 幂等门；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST149-199/999` 和默认 147 项全量 WMDC/RAPI 设备门；
- TEST13 三段真实导航、零 `ERROR`、零 `FAIL`、唯一 `TESTBENCH PASS`。

## 已关闭批次：next233

目标：补齐 `history.pushState`/`replaceState` 可选 URL 参数的一个明确边界：调用者显式传入
`undefined` 时默认当前 document URL，同时保持显式空字符串的同 URL entry 语义，并证明两者
都不会触发网络请求或破坏 history traversal。

实现边界：

- 浏览器脚本 bridge 只在第三个参数确实不是 `undefined` 时字符串化 URL；缺省/显式
  `undefined` 走当前 URL，显式 `''` 继续走当前 URL 的同 URL entry；未扩大 URL parser 或
  放宽既有安全路径拒绝规则。
- TEST200 覆盖 replace/push 返回值、location/document.URL、history length/state、bridge
  URL/state、无 GET、back/forward 的 popstate 顺序，以及 traversal 后再次 replace/push；
  TEST198/199 和上一批测试继续覆盖普通编码段、根相对路径与不安全 URL 拒绝。
- 默认 `javascript=0`、TEST13 行为、公共 ABI 和 callback 总上限不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST149-200/999`（53 项）和全量 `TEST13/20/27/43/44/56/58-77/80-200/999`
  （148 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、零 `ERROR`、零 `FAIL`、唯一 `TESTBENCH PASS`。

## 已关闭批次：next234

目标：建立正式浏览器产品组合层，先把无窗口、无网络、无 JavaScript 依赖的 history/session
状态机从 `test_host/main.c` 迁入 `positron_browser.dll`，并让宿主通过公共 C ABI 消费它。

实现边界：

- 新增 `positron_browser.dll`、`positron_browser.h` 和 VS2008 ARMV4I 工程；它只依赖
  `positron_json.dll` 验证 JSON state，不依赖 DOM、脚本、网络或 WM 控件。
- 公共 ABI 使用 UTF-8、opaque history handle、明确销毁函数和借用字符串生命周期；覆盖
  document commit/replace/traversal、push/replace state、same-origin/default-port、
  history length/index/state 投影和独立 document identity。
- `test_host` 的旧 history 函数保留为兼容适配与断言镜像，产品状态不再由宿主数组维护；
  TEST201 绕过适配层直接验证 DLL。JS bootstrap、DOM/Event/form bridge 仍未迁移，下一批处理。
- stage、device gate、test_host 工程和所有面向读者文档已包含第七个产品 DLL；默认
  `javascript=0`、TEST13、公共 core/script ABI 和既有人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST201/999`（2 项）、`TEST149-201/999`（54 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-201/999`（149 项）WMDC/RAPI 设备门；
- 参数边界/非法 JSON 的最新定向 `TEST201/999` 证据位于
  `tmp/device-runs/20260814-235415-next234-browser-history-contract/`；
- TEST13 三段真实导航、零 `ERROR`、零 `FAIL`、唯一 `TESTBENCH PASS`。

## 已关闭批次：next235

目标：把浏览器 JavaScript session 的 PScript context 所有权、host JSON callback 注册/调用和
销毁生命周期从 `test_host/main.c` 收拢到 `positron_browser.dll`，同时保持现有宿主适配边界。

实现边界：

- `positron_browser.dll` 新增 `PBrowser_ScriptSession*` opaque API，持有一个 PScript context，
  负责求值、全局值、JSON callback 注册/注销/调用、结果/错误读取和 native function count；
  `PBrowser_ScriptSessionRuntime` 仅作为迁移期只读诊断借用句柄。
- `test_host` 的 bridge 通过 product session API 驱动既有脚本路径；窗口、core document、导航、
  DOM/Event/form/input/location callback 实现和 bootstrap 文本仍在宿主，下一批再迁。
- TEST202 直接调用 product script session API，覆盖 context 持久求值、host callback、注销、
  参数错误和产品所有权；默认 `javascript=0`、TEST13、公共 core/script ABI 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST202/999`（2 项）、`TEST112-135/137-152/189-202/999`（55 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-202/999`（150 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST202 product session、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 唯一下一步

在 next235 基线之上，把 bootstrap 文本以及 DOM/Event/form/input/location/navigation callback
实现从 `test_host/main.c` 迁入 `positron_browser.dll`，先保持现有 public core/script ABI 和
宿主回调边界，不把窗口、网络或完整 URL parser 一起迁入；继续保留 TEST13 和人工视觉/输入
累计门。

完成标准：

- next235 的 150 项自动 gate、TEST202/999 和脚本回归定向 gate、C89、审计和正式构建均保持通过；
- 最新 TEST75 纵向/横向截图已核对无异常，其余人工包由用户报告正常；人工验收若切换为
  `auto=0` 不会创建 `test_host.log`，这部分仍以截图/操作记录为人工证据，不替代自动日志；
- 下一批为 bootstrap 与 DOM/Event/form/input/location/navigation 产品边界增加正反例、资源
  关闭、state/length、traversal 和事件顺序断言，并通过定向后全量设备门；
- 若出现崩溃、数据损坏、严重布局破坏或核心交互阻塞，立即停止累计并进入 debug；
- 候选通过后覆写本文件，并从路线图中选择下一个单一代码能力。

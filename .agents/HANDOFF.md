# Positron 当前交接

更新时间：2026-08-23

本文件只保存接手下一批工作所需的当前快照。已完成批次、旧故障和旧验收记录以 Git 历史、`docs/history/` 与本地 `tmp/device-runs/` 为准，不在这里累计。

## 权威来源

- 项目使命、DLL 边界、ABI 和所有权：`docs/ARCHITECTURE.md`
- 当前未解决边界：`.agents/KNOWN_LIMITATIONS.md`
- 未来工作及优先级：`.agents/ROADMAP.md`
- 失败路线及重试前提：`.agents/FAILED_EXPERIMENTS.md`
- agent 工作规则：根目录 `AGENTS.md` 与 `.agents/README.md`
- “当前”结论必须由 Git、源码、构建、测试和设备日志交叉验证。

## 当前仓库基线

- 分支：`main`，已与 `origin/main` 同步。
- 交接提交：`1179b5df next605: add bounded RadioNodeList`。
- 交接前工作区：干净；后续 agent 必须重新检查，不能沿用这一结论。
- 当前能力批次：next605。
- 测试编号上限：`TEST_MAX_NUMBER 1053`。
- 跟踪的 `test_host/test_host.ini` 保持默认自动模式：
  - `javascript=0`
  - 默认选择 `13,20,27,56,58,62,64-67,73,75,999`
- 最近一次全范围自动设备基线仍为 next255；其后的批次使用针对性门和相关回归门验证。

## 项目使命与当前里程碑

Positron 的目标是在 Windows Mobile 6 / Windows CE 设备上提供可嵌入、稳定 C ABI 的轻量应用与浏览器运行时。可发布能力必须归属于 `positron_core`、`positron_browser`、`positron_script` 等产品 DLL；`test_host.exe` 只负责回归编排、平台窗口、网络接入和示例消费。

当前中期里程碑是：在默认关闭 JavaScript 的安全基线不变的前提下，使浏览器会话中的 HTML、CSS、表单、DOM 与单一 Duktape 引擎组合成可预测、资源有界、可由产品 DLL 复用的轻量 Web 运行时。

当前短期方向不是继续堆叠零散 API 数量，而是把仍滞留在 `test_host` 中、实际属于产品语义的桥接代码迁回正确 DLL，并用真实页面驱动兼容性选择。

## 已验证的产品状态

- 公共接口遵循稳定 C ABI、UTF-8、opaque handle 和明确内存所有权。
- 浏览器 JavaScript 与独立脚本 API 共用 `positron_script` 中的 Duktape，不存在第二套浏览器 JS 引擎。
- JavaScript 默认关闭；启用是显式的会话配置。
- 浏览器会话的脚本 heap 上限为 624 KiB；独立脚本会话默认上限为 512 KiB。
- next605 在产品侧增加了有界的 `RadioNodeList` 表单集合语义；`test_host` 仍只是消费者和断言宿主。
- `test_host` 中仍有需要逐批审计和迁移的浏览器桥接胶水；窗口、原生控件、设备网络等平台副作用继续由宿主持有。

## 最近验证证据

next605 已完成与风险相称的本地和设备验证：

- C89 审计、正式构建与仓库审计通过。
- 针对性设备门：
  `tmp/device-runs/20260823-142518-next605-r2/device-gate-result.txt`
  — PASS，19/19，错误与失败均为 0，唯一 PASS，路由为 TEST13。
- 相关回归设备门：
  `tmp/device-runs/20260823-142642-next605-regression-r2/device-gate-result.txt`
  — PASS，252/252。
- 首次候选曾以 608 KiB 在 TEST901 暴露内存不足；修复是把浏览器会话预算提高到实测所需的 624 KiB，并重新通过两道门，没有放宽断言。
- next605 没有引入新的人工验收门。历史文件选择器与 TEST75 视觉门已经完成验收，不应因旧文字再次被标记为待验。

`tmp/` 不跟踪，以上路径只用于本机证据定位；长期可追溯结论必须落在提交、源码和跟踪文档中。

## 当前已知边界

需要继续面对而不能用断言掩盖的边界包括：

- DOM、表单集合、历史、存储、请求响应和异步模型仍是资源有界的子集，不是完整现代浏览器。
- 布局仍缺少 Grid、sticky、复杂包含块及完整表格/列表行为；float 路线已撤回。
- SIP/IME、候选词、旋转、文件选择器和视觉几何仍可能需要真实设备人工验收。
- Mbed TLS 2.16.12 已停止维护；HTTP/TLS、证书、设备时钟和 OEM 网络栈均有限制。
- 更新批次的针对性回归很强，但不能被表述为 TEST1–1053 的最新全范围覆盖。

详细的当前边界与解除条件见 `.agents/KNOWN_LIMITATIONS.md`。

## 当前工作区与候选状态

- 当前没有待晋升的设备候选。
- 当前没有已知需要立即 debug 的失败门。
- tracked INI 不应为了下一批开发永久改成人工模式或扩大默认测试集。
- 接手者必须先检查工作区；任何未提交改动默认属于用户，不能覆盖。

## 唯一下一步

next606 只选取一个边界清楚、相对完整的纵向能力：审计并迁移一组仍由 `test_host` 持有的产品级浏览器语义，优先选择表单/输入桥中能形成完整用户行为的一组。语义和状态归入 `positron_browser` 或适当产品 DLL；宿主只保留 WM 窗口、网络、原生控件和测试编排。

不要为了填充 next 编号加入互不相关的小 API，也不要顺便重构其他模块。

## next606 完成标准

- 产品级语义不再由 `test_host` 独占，宿主通过公共 API 消费它。
- 公共 ABI、UTF-8、opaque handle、内存所有权及 VS2008 / WM6 ARMV4I / C89 兼容性不退化。
- `python scripts/test_c89ize.py`、正式工程构建和 `python scripts/audit_repo.py` 通过。
- 通过覆盖新行为的针对性自动设备门，以及包含相关既有能力和 TEST999 的回归门。
- 只有出现视觉、真实触摸、SIP、旋转、文件选择器或失败网络风险时才累计人工门；崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即人工复核。
- 跟踪的默认 INI 恢复为自动模式且选择集不被无意扩大。
- 用当批事实覆盖本文件的当前快照，更新限制和路线图；只提交本批 tracked 文件并推送 `main`。

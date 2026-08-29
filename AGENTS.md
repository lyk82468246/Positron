# Positron Agent Instructions

本文件是仓库根目录的稳定指令入口。不要在这里记录当前 next 编号、测试数量或临时状态；动态项目状态维护在 `.agents/`，历史材料进入 `docs/history/` 或 Git 历史。

## 接管顺序

开始修改前：

1. 先读 `.agents/README.md`，再按其中顺序读取本任务需要的交接文档。
2. 至少核对 `docs/ARCHITECTURE.md`、`.agents/HANDOFF.md`、`.agents/KNOWN_LIMITATIONS.md` 和 `.agents/ROADMAP.md` 中与当前里程碑直接相关的部分。
3. 检查当前分支、`git status`、未提交 diff 和最近提交；现有改动默认属于用户。
4. 用源码、构建、测试和设备日志验证 handoff，不盲目接受文档中的“当前”结论。
5. 只读取和修改当前纵切直接需要的文件，不顺便扩大范围或重构无关代码。

## 权威边界

- 项目使命、公共 DLL 边界、ABI、所有权和第三方移植原则以 `docs/ARCHITECTURE.md` 为准。
- 当前设备基线和下一步以 `.agents/HANDOFF.md`、`.agents/KNOWN_LIMITATIONS.md`、`.agents/ROADMAP.md` 为线索，但必须和 Git、源码及日志交叉验证。
- `test_host.exe` 是回归宿主和示例消费者，不是公共 API 的所有者。
- `.agents/` 是唯一 agent 交接目录；不要重新创建 `.agent/` 或 `PROJECT_STATE.md`。
- `docs/history/` 中的 Phase、调试事故和旧 next 材料只代表当时语境，不自动代表当前计划。

## 修改纪律

- 保留所有不属于当前任务的未提交改动；禁止覆盖、清理或顺手格式化它们。
- `tmp/` 只保存本地截图、日志和诊断材料，永不加入 Git。
- 每批只推进一个边界清楚、可验证的纵向能力；优先补齐能力，再做观感和性能优化。
- 公共接口保持稳定 C ABI、UTF-8、opaque handle 和明确的内存所有权。
- 代码必须兼容 VS2008 / WM6 ARMV4I 与项目的 C89 约束。
- 构建只能通过 `scripts\build.bat` 或 `scripts\stage.bat` 的正式工程配置完成，不绕过解决方案直接拼装工具链。

## 文档纪律

- 修改 Markdown 前先以 UTF-8 整篇读取目标文件，确认标题结构和职责；不能只搜索关键词后在附近追加。PowerShell 读取使用 `Get-Content -Raw -Encoding UTF8` 或等价的不截断方式。
- 新事实应改写已有主题段落。除 `docs/history/`、失败实验和 Git 历史外，不按日期、提交或 next 编号记录流水账。
- 根 README 只讲项目入口；架构/构建/测试/排障只讲稳定规则；组件 README 只讲调用和边界；`.agents/` 只讲当前状态、当前限制、未来路线与可复用失败。
- 候选目录、`tmp/device-runs/`、逐测试 OK 数量和提交清单只进入当前 handoff 或专用历史，不进入面向读者的稳定文档。
- 每次文档修改都检查整篇是否出现重复章节、同一段落混入多个主题或批次、职责失控和失效链接。单行长度和固定列宽不是质量指标；禁止为了通过审计机械折行。若文件职责已经失守，应先重构主题与段落，再继续记录当前事实。
- 提交前必须通过 `python scripts/audit_repo.py` 的文档结构门；不得通过放宽规则掩盖流水账。

## 验证与交付

- 相关 C 改动先运行 `python scripts/test_c89ize.py`；提交前运行 `python scripts/audit_repo.py` 和与风险相称的构建/测试。
- 每个能力批次都经过自动设备门。视觉、真实触摸、SIP、旋转和失败网络风险可累计后集中人工检查；崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即人工复核。
- 设备日志通过前不得把候选写成正式基线。通过后按职责更新 handoff、限制和路线图；只有稳定且面向读者的能力变化才更新 README/`docs/`。只提交本批 tracked 文件并推送当前分支。
- 失败实验必须记录边界并撤回默认路径；不得通过放宽断言掩盖回归。

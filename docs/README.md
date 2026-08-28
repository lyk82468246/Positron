# Positron 文档索引

本目录保存面向使用者和维护者、可以长期成立的工程文档。它不记录逐批开发进度；当前工作状态
只在 `.agents/` 中维护，旧阶段和事故记录只进入 `docs/history/`。

## 按任务选择

| 需要做什么 | 阅读 |
|---|---|
| 了解项目和快速开始 | [根 README](../README.md) |
| 理解 DLL 职责、所有权、数据流和非目标 | [ARCHITECTURE.md](ARCHITECTURE.md) |
| 配置 VS2008/WM6 工具链并构建 | [BUILDING.md](BUILDING.md) |
| 配置 INI、运行自动设备门或做人工验收 | [TESTING.md](TESTING.md) |
| 修复构建、stage、WMDC/RAPI、网络或输入问题 | [TROUBLESHOOTING.md](TROUBLESHOOTING.md) |
| 生成和发布固定 nightly 包 | [NIGHTLY_RELEASE.md](NIGHTLY_RELEASE.md) |
| 核对依赖来源和许可证 | [THIRD_PARTY.md](../THIRD_PARTY.md) |
| 接管当前开发任务 | [`.agents/README.md`](../.agents/README.md) |
| 查阅旧阶段或已发生的调试事故 | [`history/README.md`](history/README.md) |

每个公共 DLL 的子目录 README 说明如何由其他项目链接和调用它。精确 API 仍以同目录公开头
文件为准；README 不复制完整声明，避免文档和 ABI 漂移。

## 文档职责

- 根 `README.md`：项目定位、产物、快速开始和导航。
- `ARCHITECTURE.md`：稳定设计、公共边界、所有权和平台原则。
- `BUILDING.md`、`TESTING.md`、`TROUBLESHOOTING.md`：可重复的操作与判定方法。
- 组件 `README.md`：单个 DLL/工程的用途、依赖、调用流程和限制。
- `.agents/HANDOFF.md`：当前提交、最新证据、短期目标和唯一下一步。
- `.agents/KNOWN_LIMITATIONS.md`：仍然存在的限制，不保存已解决问题。
- `.agents/ROADMAP.md`：未来工作，不保存已完成批次。
- `history/`：允许按日期或阶段记录，但其内容不是当前权威结论。
- Git 历史：逐批实现和测试证据的最终流水账。

## 维护纪律

修改非历史文档前，必须以 UTF-8 整篇读取并先确认其职责。新增事实应改写现有主题段落，而不是
在文件末尾追加“本批完成了什么”。完成批次、候选目录、设备运行路径和逐测试结果只保留在
当前 handoff、专用历史记录或 Git 中。

提交前运行：

```bat
python scripts\audit_repo.py
```

仓库审计会检查 UTF-8、相对链接和关键文档的结构约束，包括稳定文档中的批次流水、设备临时
路径、超长行和失控体积。

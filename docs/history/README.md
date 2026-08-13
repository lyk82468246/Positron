# Positron 历史里程碑

本目录保存项目早期阶段的设计和移植记录。它们解释“为什么当时这样做”，但不是当前构建、
API、能力或下一步的权威来源。

当前信息请使用：

- 项目概览：[`README.md`](../../README.md)
- 稳定架构：[`docs/ARCHITECTURE.md`](../ARCHITECTURE.md)
- 当前 agent 交接：[`.agents/HANDOFF.md`](../../.agents/HANDOFF.md)
- 当前限制：[`.agents/KNOWN_LIMITATIONS.md`](../../.agents/KNOWN_LIMITATIONS.md)
- 当前路线图：[`.agents/ROADMAP.md`](../../.agents/ROADMAP.md)

## Phase 记录

| 文档 | 当时的主题 | 阅读注意 |
|---|---|---|
| [PHASE1.md](PHASE1.md) | TLS 1.2 最小连接与 WM6 部署 | 部署问题有历史价值；API 和证书策略已继续演进 |
| [PHASE2.md](PHASE2.md) | JSON、HTTP 和早期组合验证 | 早期不验证证书的描述已被 Phase 3 取代 |
| [PHASE3.md](PHASE3.md) | CA bundle、verified TLS、熵源 | 正文包含早期少量根证书设计，文件开头已注明后续变化 |
| [PHASE4.md](PHASE4.md) | NetSurf HTML/CSS/layout 移植 | 是阶段性快照，不代表当前浏览器能力清单 |

## 工程事故与迁移记录

- [M7 Flex/Table 移植记录](M7_FLEX_TABLE_NOTES.md)：前置头、C89 转换、box 层次和 UA CSS
  的早期移植结论。
- [next37 稳定性回退](NEXT37_ROLLBACK.md)：一组无法安全归因的 Browse 实验为何整批冻结。
- [调试事故归档](DEBUGGING_INCIDENTS.md)：旧设备日志、混包和定位过程；其中动态状态均已过期。

Phase 文档尽量保持原始叙述，只修复因目录迁移造成的链接。若历史正文与当前共享文档冲突，
以当前共享文档和源码为准。

## Next 编号

`nextNNN` 是长期开发过程中的内部候选/验收编号，不是发布版本。过去把每个 next 的完整叙述
同时追加到 README、handoff、limitations 和 roadmap，最终造成文档失去可读性。现在采用：

- Git commit 记录已提交的具体变化；
- `.agents/HANDOFF.md` 只记录当前候选和最近证据；
- `.agents/FAILED_EXPERIMENTS.md` 保留未来可能重复踩坑的失败；
- 公开文档只描述已经稳定、对读者有意义的能力。

需要调查旧 next 时，优先使用 Git log、对应测试源码和设备证据，不从旧文档中的“当前”
措辞推断状态。

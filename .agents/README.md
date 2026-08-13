# Positron Agent 交接索引

`.agents/` 只保存 agent 续接开发所需的动态状态。稳定架构、构建、测试和排错方法统一放在
`docs/`，不要把它们再次复制到本目录。

## 接管顺序

1. [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)：使命、产品分层、公共 ABI 和所有权。
2. [`HANDOFF.md`](HANDOFF.md)：当前分支、已验收基线、未验收候选和唯一下一步。
3. [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)：当前仍存在的能力边界与解除条件。
4. [`ROADMAP.md`](ROADMAP.md)：短期、中期、长期目标；只保留未来工作。
5. [`FAILED_EXPERIMENTS.md`](FAILED_EXPERIMENTS.md)：若任务触及旧失败方向，先查禁止恢复边界。
6. [`DEBUGGING.md`](DEBUGGING.md)：agent 取证纪律；通用操作见
   [`../docs/TROUBLESHOOTING.md`](../docs/TROUBLESHOOTING.md)。

随后检查 `git status`、未提交 diff、最近提交、源码、正式构建和设备日志。任何文档中的
“当前”都必须与工作区交叉验证。

## 文件职责

- `HANDOFF.md`：一页式动态快照。候选通过或工作区变化时覆写，不追加历史。
- `KNOWN_LIMITATIONS.md`：只保留现在仍未完成的边界；能力完成后删除或收窄对应项。
- `ROADMAP.md`：只写尚未完成的目标；已完成批次退出路线图。
- `FAILED_EXPERIMENTS.md`：保留未来可能重复踩坑的失败和重启门槛。
- `DEBUGGING.md`：只写 agent 取证和状态更新纪律，不积累日期流水。

不要创建 `.agent/`、`PROJECT_STATE.md`、工具专属 last-context 文件或第二份架构文档。

## 其他权威来源

- 人类可读项目入口：[`../README.md`](../README.md)
- 构建与部署：[`../docs/BUILDING.md`](../docs/BUILDING.md)
- 测试与验收：[`../docs/TESTING.md`](../docs/TESTING.md)
- 第三方版本和许可证：[`../THIRD_PARTY.md`](../THIRD_PARTY.md)
- 历史记录：[`../docs/history/README.md`](../docs/history/README.md) 和 Git 历史

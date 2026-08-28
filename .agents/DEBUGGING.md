# Agent 调试纪律

通用构建、stage、设备、网络、布局、SIP 和 JavaScript 排错步骤见
[`../docs/TROUBLESHOOTING.md`](../docs/TROUBLESHOOTING.md)。本文件只规定 agent 如何取证、
保护工作区和更新动态状态。

## 修改前

1. 读取 [`HANDOFF.md`](HANDOFF.md) 的候选和唯一下一步。
2. 运行 `git status --short --branch`、查看未提交 diff 和最近提交。
3. 现有改动默认属于用户；不 reset、不 checkout、不顺手格式化。
4. 用源码、配置和日志验证文档，不根据 next 编号推断代码状态。
5. 若触及旧失败方向，先读 [`FAILED_EXPERIMENTS.md`](FAILED_EXPERIMENTS.md)。

## 设备证据

- 读取完整 `test_host.log`，确认 screen/DPI、所有选择项、`ERROR`、`FAIL` 和最终状态。
- “跑完了”、窗口关闭、提示音和部分 `OK` 都不是完整通过证据。
- 截图必须和 URL、方向、viewport、DPI、滚动位置、操作步骤及候选目录对应。
- `tmp/` 只保存本地证据，不加入 Git。
- 自动数值与人工截图冲突时，明显视觉/交互回归优先，先定位而不是放宽断言。

## 失败处理

- 找最早异常，不从末尾连锁错误倒推。
- 先排除旧进程、全局 DLL 复用、混包、设备时间和网络变化。
- 保持失败候选可辨认，不把它写成基线。
- 不提高预算、删除测试或扩大容差来掩盖生命周期、布局或输入问题。
- 若实验造成真实页面回归，撤回默认路径，并把根因、禁用边界和重启门槛写入
  `FAILED_EXPERIMENTS.md`。

## 文档更新

- 更新前用 UTF-8 Raw 整篇阅读目标文件和标题树；关键词搜索只用于定位，不能代替整体审阅。
- `HANDOFF.md` 始终覆写为当前快照，不在顶部或底部追加一份历史。
- 候选只有在设备日志完整通过后才能改称基线。
- `ROADMAP.md` 只保留未来；完成项删除，不转成开发日记。
- `KNOWN_LIMITATIONS.md` 只保留仍存在的能力边界。
- 面向读者的稳定能力才更新根 README 或 `docs/`。
- next 细节由 Git、测试源码和必要的失败记录保存，不复制到四份文档。
- 同一主题已有章节时原地改写并删除旧结论；发现重复标题、批次堆叠或数百行单段时，先做
  整体重构，再继续记录当前事实。

旧的日期化调试流水已归档到
[`../docs/history/DEBUGGING_INCIDENTS.md`](../docs/history/DEBUGGING_INCIDENTS.md)，仅供调查旧问题，
其中的“当前”不再有效。

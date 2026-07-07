# Positron Agent Handoff

这个目录是给后续 agent 接手用的项目交接层。先读这里，再读根目录文档和源码。

兼容入口：仓库里也有 `.agent/README.md`，它只做跳转，权威内容仍维护在 `.agents/`。

## 阅读顺序

1. `HANDOFF.md`：当前真实状态、架构、构建/运行方式、下一步。
2. `ROADMAP.md`：短期 / 中期 / 长期规划，以及建议执行顺序。
3. `CLAUDE_LAST_CONTEXT.md`：Claude Code 网络出错前最后几段对话的整理，包含 M7-flex/M7-table 的真实过程。
4. `DEBUGGING.md`：WM6/WinCE 设备调试纪律，尤其是先查环境再改代码。
5. 根目录 `PHASE*.md` / `README.md`：阶段脉络。注意它们有些内容落后于 git main。

## 权威性

- 最新代码状态以 `git log`、源码和本目录为准。
- `README.md` / `PHASE4.md` 有历史价值，但截至 2026-07-07 已落后于 `main` 最新提交。
- Claude Code 本地 transcript 在 `C:\Users\Joe\.claude\projects\c--Users-Joe-Documents-C--Positron\`，但以后应优先维护本目录，避免下一位 agent 找不到交接信息。

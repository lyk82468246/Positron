# Positron 文档索引

本目录保存对仓库读者、调用者和维护者长期有效的文档。当前候选编号、设备日志位置和
下一步开发动作不放在这里，而由 `.agents/` 中的动态交接文件维护。

## 使用与维护

- [架构与公共边界](ARCHITECTURE.md)：项目分层、DLL 职责、ABI、所有权和浏览器组合方式。
- [构建与部署](BUILDING.md)：VS2008/WM6 工具链、正式构建脚本、stage 和模拟器共享目录。
- [测试与验收](TESTING.md)：`test_host` 配置、自动门、人工门和通过条件。
- [故障排查](TROUBLESHOOTING.md)：构建、staging、设备、网络、布局、SIP 和脚本问题。
- [历史里程碑](history/README.md)：早期 Phase 决策记录及其适用边界。

公共 API 的精确签名和内存规则以各组件头文件为准：

- [`positron_tls.h`](../positron_tls/positron_tls.h)
- [`positron_json.h`](../positron_json/positron_json.h)
- [`positron_http.h`](../positron_http/positron_http.h)
- [`positron_image.h`](../positron_image/positron_image.h)
- [`positron_script.h`](../positron_script/positron_script.h)
- [`positron_core.h`](../positron_core/positron_core.h)

第三方版本、来源和许可证见根目录
[`THIRD_PARTY.md`](../THIRD_PARTY.md) 及各组件的 `UPSTREAM.md`/许可证文件。

## Agent 文档

Agent 从根目录 [`AGENTS.md`](../AGENTS.md) 接管，并按
[`.agents/README.md`](../.agents/README.md) 的顺序读取动态状态。`.agents/` 中的内容
面向续接开发，不应复制到项目首页。

文档职责遵循以下规则：

- 稳定架构和操作方法写入 `docs/`；
- 普通读者需要的摘要写入根 `README.md`；
- 当前基线、候选、短期下一步和未解决限制写入 `.agents/`；
- 已结束的推进过程由 `docs/history/`、失败实验索引和 Git 历史保存；
- vendored 上游文档保持原样，不改写成 Positron 项目说明。

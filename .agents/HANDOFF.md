# Positron 当前交接

更新时间：2026-08-13

稳定使命、架构和公共边界见
[`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。本文件只记录当前工作区、最近设备证据、
尚未验收的候选和唯一下一步。

## Git 与仓库基线

- 分支：`main`，跟踪 `origin/main`。
- 最近产品提交：`4bf243c4 支持根相对末尾半编码双点段导航`。
- next217-next219 已经提交并推送。
- 本次文档迁移只改变文档、文档审计和一个历史注释，不包含新的产品能力。
- 干净 clone 中没有 next220 实现或候选包；若工作区存在额外改动，必须以 `git diff` 为准。

接管时仍须重新运行 `git status --short --branch` 和 `git diff`；以上列表不是 Git 的替代品。

## 最近已验证设备证据

### 最近全量检查点：next216

- 配置：`TEST13/20/27/43/44/56/58-77/80-183/999`，共 131 项。
- 环境：`screen=240x320 dpi=96`。
- 结果：130 条标准数字 `OK`、1 条 TEST13 overview、零 `ERROR`、零 `FAIL`、最终
  `TESTBENCH PASS`。
- 能力终点：绝对 URL 中单个内嵌完整 `.%2E`/`%2E.` double-dot segment 的受控同文档
  fragment 导航。

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
- 视觉、真实触摸、SIP、旋转和失败网络允许累计后集中复核；崩溃、数据损坏、严重布局
  破坏或核心交互阻塞必须立即检查。

## 唯一下一步：next220

实现一个窄纵切：绝对 `location.href`、`location.assign()` 和 `location.replace()` URL 中，
允许多个完整 `%2E%2E` double-dot segment 按路径顺序分别折叠其前驱非空目录，再使用既有
origin/path/query 规则判定同文档 fragment 导航。

必须保持：

- 根相对重复完整 `%2E%2E`、完整/半编码混合和字面父目录仍走普通导航；
- 不同 query/path、越过 origin 根和没有非空前驱目录仍是反例；
- 三个入口、fragment clear、same-value、state/length、hashchange 和无 GET 都有断言；
- 默认 `javascript=0`、TEST13、core 公共 ABI 和 native callback 上限不变；
- 不在同一批加入无关 DOM、布局或性能改动。

完成标准：

- C89 回归、实际完整 bootstrap 探针和 ARMV4I 正式构建通过；
- 候选包整体 stage，EXE/DLL 哈希一致；
- 因上批发生过 bootstrap timeout，本批运行全量自动设备门；
- 日志零非预期 `ERROR`、零 `FAIL`、最终 `TESTBENCH PASS`；
- TEST13 完成既定三段真实导航。

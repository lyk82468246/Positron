# EXE 的 Notes 资源更新验收

本文保存一次隔离设备验收的证据。应用接线和恢复入口见[宿主合同](../../positron_app/RUNTIME_HOST.md#pxi-资源更新)，当前待办见[交接](../../.agents/HANDOFF.md)。

正式 Debug 包 `tmp/device-runs/20261112-213634-app-pxi-update` 在内置 Temp 隔离部署。产品 EXE 和十一 DLL 回读哈希 12/12 一致。两个进程调用与 `--install` 相同的 EXE PXI 调度器，用真实 Notes v1/v2 包和私有 SQL 库，原生确认由 Debug 夹具代答。

| 进程 | 操作与证据 |
| --- | --- |
| 1991568826 | 首装 revision 1，写入标题 `笔记'`、正文 `keep me`。提交 PREPARING 后模拟响应丢失及读回失败，返回 UNRESOLVED。独立重开当前 revision 1 并查询到该笔记。 |
| 3282187934 | 重新提交相同 PXI，恢复原尝试。模拟最终提交响应丢失，读回确认 PUBLISHED，重新核验并打开 revision 2。再用 installed 入口重开并查询到相同笔记。 |

独立读取两份闭库 SQLite，完整 base 和 candidate 快照一致，安装与尝试 ID 保持相同。current generation 由 2 到 3，revision 由 1 到 2，尝试 generation 由 1 到 2。两套资源树逐文件匹配生成夹具的 SHA256。数据路径仍只有该安装身份的数据库，闭库查询精确匹配原笔记。两个进程正常退出，零 DLL 引用、零新增 dump。

C89、更新夹具、设置/访问存储离线测试、仓库审计、正式 Debug/Release ARMV4I 构建通过。首轮隔离 staging 因构建进程停滞中止，后续内部空间门保留了原用户目录。初次自动门的测试缓冲区容量不符公开 SQL API，修正后用全新隔离包验收，未修改产品预算或断言。

Release 人工验收使用内置 Temp 的独立 Notes 实例。用户确认拒绝更新后普通启动仍进入 v1，原笔记仍在。第一次接受更新的重试在确认框前失败，登记保持 revision 1、无尝试记录，设备仅剩约 618 KiB。释放旧二进制占用后，同一 Release 包成功显示确认并提交 revision 2，登记 generation 为 3，尝试为 PUBLISHED。空间是复现线索，具体失败点未确认。

用户在 v2 编辑并保存原笔记，正常退出后通过普通 `--app` 冷重开，确认直接进入 v2、免确认且编辑结果仍在。验收结束后核验零 DLL 引用，删除隔离测试目录及其中日志。

权限、schema 或迁移历史变更由 Runtime 的资源更新资格拒绝。真实断电、FULL、卸载及产品资源回收另行立项。

# Notes 原包恢复验收

Runtime 双配置验收见 [Runtime 恢复记录](RUNTIME_RESTORATION_ACCEPTANCE.md)。

应用 Debug 四进程门 `tmp/device-runs/20261114-083425-app-pxi-update` 通过。真实 Notes 依次首装、恢复中断更新、升级、卸载。冷进程确认原启动被拒绝，同 ID 异包返回 INVALID，原 v2 包恢复同一安装并保留原笔记。恢复清除了终态更新记录，正常重开可查询原笔记。完整包哈希、正常退出零引用和无新增 dump 通过。

Release 正式构建通过。隔离设备目录使用匹配 Release 二进制和该次门的撤销登记快照，保留同一原私有库。通过正式 `--install "resources-v2.pxi" --approve-pxi` 启动，无需点按即提交 READY。RAPI 回读核对安装 ID 与 revision 2 保持，generation 4 变为 5，终态尝试及其快照均清除，原库仍有一条笔记。此 Release 检查验证 CLI 和持久提交，页面编辑与冷重开的数据路径由 Debug 门覆盖。Release 进程由测试清理工具结束。

# 已安装 Notes 卸载验收

设备基线与使用说明见[宿主合同](../../positron_app/RUNTIME_HOST.md#已安装应用)。

Debug 正式包 `tmp/device-runs/20261113-232637-app-pxi-update` 的三个进程通过首装、PREPARING
恢复、更新、活动安装租约拒绝、撤销和重开拒绝。撤销注入已提交但回复丢失与首次读回不可用，
由只读观察确认。冷主库确认 revision 2 保持，generation 从 3 到 4，状态为 REVOKED。
原更新尝试及两份快照完整保持，两版资源逐文件匹配原夹具，私有库保留原中文和引号笔记。
包哈希 12/12、零引用及无新增 dump 通过。

Release 隔离包 `tmp/uninstall-manual-20261113-233006` 使用独立 Notes v1。
用户确认取消卸载后列表和原笔记仍在。确认卸载后列表移除 Notes，
闭库登记为 REVOKED、generation 3、revision 1，更新表为空，原私有库仍有一条笔记。
随后通过普通 `--app` 冷启动，用户确认显示启动失败并退出。

正式 Debug/Release 构建通过。早期 VS2008 构建退出的尝试保留在本地日志。
完成的设备测试目录和其中日志在零引用核验后删除，原 Notes 使用包恢复。

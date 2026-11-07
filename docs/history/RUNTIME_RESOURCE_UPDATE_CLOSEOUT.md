# Runtime 资源更新资格阶段收尾

本文保存本阶段证据，不是当前正式基线；当前状态见 [HANDOFF](../../.agents/HANDOFF.md)，稳定合同见 [安装接口](../../positron_runtime/INSTALLED.md)。用户要求停止扩展并收尾，未启动后续更新发布工作。

## 范围与状态

候选新增 `PRuntime_OpenInstalledEx` 与 `PRuntime_ResourceUpdatePrepare/Step/GetInfo/Cancel/Close` 六个入口。前者显式核验期望 revision/generation，旧 opener 仍限制 revision1；后者只读核验相同安装身份、递增应用版本、下一资源 revision、相同能力/schema/迁移历史以及既有 READY 存储。保有父实例、候选和存储，关闭资格句柄前阻止冲突生命周期操作；不写 SQL、不迁移、不发布、不授予服务。

源码检查保留旧 55 个公共函数声明和 37 个公共结构定义。组合预算静态门为两个 revision、存储、资格与 128 KiB 余量合计不超过 1280 KiB，不包括 provider 快照、CSP 或 SQLite。宿主两个 4 MiB 快照的总峰值、OOM、真实设备压力不在本次证明内；没有完整二进制 ABI 兼容性保证。

## 最终源码与验证

收尾发现公共 `InstalledReadFile` 可在分步间因 I/O 失败退休旧父实例。资格 Step 现检查旧实例/候选仍 COMPLETE、存储仍 READY；检查不放入查询/关闭前置，确保失败后仍可清理。既有第 17 个模式以真实 provider 读失败验证 STATE、输出不变和终态清理，保留总计 18 个模式及原断言。

- 最终 Debug：`tmp/device-runs/20261107-100112-runtime-update-qualification-debug-final`，TEST1369、1377、1384–1386、999 共 6/6；唯一 TESTBENCH PASS，零 ERROR/FAIL，匹配 Core 路径，完整日志及 crash_check PASS，无新增 dump，完成后删除本轮远端目录。
- Release：`20261107-100532`、`20261107-100640`、`20261107-100815` 三个 `runtime-update-qualification-release-final` 目录，均在正式 stage 的 VS2008 构建阶段中止，未部署/执行测试。事件日志确认 devenv.exe / msenv.dll `0xc0000005`；第二轮 Runtime 已链接无错误不等于整包构建通过。禁止把 Debug 结论扩大为 Release 或正式基线。
- `20261107-015130-runtime-update-qualification-debug-device` 为 PreserveDeployment 的 DIAGNOSTIC_ONLY；`20261107-095617-runtime-update-qualification-debug-closeout` 属父退休修正前证据，均不替代最终源码门。

本地源码 SHA256 固化于 `tmp/runtime-update-qualification-wrapup/final-source-hashes.json`；收尾再核对四个源文件一致。C89 与仓库审计单独执行。未测试的独立 schema-count 增长、generation 溢出、真实 revision2→3 组合不冒认为通过；文件 FULL/断电门保持用户挂起状态。

## 设备恢复与后续边界

原本设备 Notes 位于 `\Temp\Positron-device-gate\app-pxi-20261106-222650`。为释放内部空间，仅在零 holder 和归档哈希核对后移走已知 exe/dll；数据库、apps/data 和资源保留。最终从本地该门的 `archived-before-final-closeout` 恢复完整原二进制包，`restored-final-closeout/result.txt` 为 PASS，所有文件回读 SHA256 一致。未自动启动作者应用，未恢复或覆盖任何旧数据库。

该候选保存为未完成双配置验收的工作，不宣称可升级应用。恢复开发时先解决正式 Release 构建崩溃并完成同源码六项门；随后由 Runtime 负责 current/pending、原子发布、未知结果恢复，EXE 再接线。能力/schema 变化、数据副本迁移与卸载另行立项。此收尾不自动启动任何后续批次。

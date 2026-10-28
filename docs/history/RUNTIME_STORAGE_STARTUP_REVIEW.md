# Runtime SQL 启动失败取证

本文保存 2026-10-28 的应用侧诊断证据，不是故障已修复的结论。当前状态以
[HANDOFF](../../.agents/HANDOFF.md) 为准；稳定调用见
[Runtime 宿主](../../positron_app/RUNTIME_HOST.md)。

## 原故障与取证缺口

原包位于 `tmp/device-runs/20261026-150410-app-runtime-sql/`，远端同名目录在
`\Storage Card\Temp\Positron-device-gate` 下。其 `app-runtime-sql/two-1.log` 只到
`runtime-arguments PASS checks=8`，用户报告“开发应用程序无法被打开，文件已保留”，
原双目录门超时失败。one 的成功不能覆盖这一失败。

源码中的参数自检发生在目录 Accept 成功、显式 SQL grant 之前；随后等待 StorageOpen。
因此失败范围是授权后的存储准备，但旧调用传 NULL，未保留已有的
`PRuntimeStorageError`，不能从通用错误框判定 Runtime、DB 或平台的实际原因。

新增诊断只在 Debug owner worker 初始化完整 v1 快照，以 result=-32767 标记尚未写入，
传入原 API，返回后立即记录实际 rc 和完整字段，再执行原 Interlocked 状态发布。
没有新增 DLL ABI、修改 provider/存储策略、把 DB 调用搬到 UI 或向 JS 暴露私有诊断。
Debug SQL 自检的存储失败改为记录 FAIL 后沿 WM_CLOSE 正常退出，普通启动错误框不变。

## 原输入、镜像与数据保留

使用正式 `scripts\stage.bat Debug tmp\runtime-storage-diag-stage` 的产物。
[探针脚本](../../scripts/app_runtime_storage_probe.ps1) 在零引用及双空间检查后：

- 回读原 EXE/十 DLL，全部与原正式 stage 一致；诊断 stage 十 DLL 也必须相同。
- 验证原 `fixture-two` 的 manifest、迁移、HTML/CSS/JS，未替换资源或修改身份。
- 冷备份原 data 树及 SHA256。诊断镜像另名放在原 EXE 父目录，原 EXE 不覆盖，
  所以原目录应用和 storage location 不变；没有切换卷、删库、手工重建或重放失败 SQL。
- 只执行 `--selftest-runtime-sql "<原目录>\fixture-two"`，没有运行 one 或完整隔离门。
  结束后等正常退出零引用，再做 data 冷备份、哈希与 crash 检查。

本次目标卷报告可用 28478210048 bytes，内部 object store 29652992 bytes；
日志中的页面 viewport 为 240×248。旧故障会话为 454×495，不能假定环境状态完全相同，
也不能把空间或 viewport 差异认定为根因。本轮没有选择/重置设备或强杀进程。

## 两次结构化结果

以下目录都在原包的本地证据目录中，分别保存 `two.log`、`result.txt`、原镜像/诊断镜像、
模块审计、`space.json`、`data-before/after` 和对应 SHA256 清单：

| 证据子目录 | 进程 PID | owner worker | 结果 |
|---|---:|---:|---|
| `runtime-storage-20261028-144744` | 1990845862 | 3597022586 | PAGE_PASS |
| `runtime-storage-20261028-two-reopen` | 2006127654 | 3063583242 | PAGE_PASS |

两次均先记录目录接受后的 `runtime-arguments PASS`、`open-begin`，再记录 `open-end`。
公开 StorageOpen 的返回及快照相同：

| 字段 | 两次实测值 |
|---|---|
| rc、error.result | 0、0 |
| db_result、db_category | 0、0 |
| sqlite_code、sqlite_extended_code | 0、0 |
| failed_schema_version、cleanup_result | 0、0 |
| message | 空 |

上述字段来自既有公开快照，不是逐个内部 DB 函数的额外 tracing；没有取得旧失败时的非零值。
两进程均完成 seed=two、32 条、typed int64/中文/引号、分页、回滚/预算、真实异步 HTML 列表、
旧页服务保留、撤销和关闭自检，正常退出零引用，未新增 dump。结果均明确 isolation=NOT_RUN。

首轮前，`v1/dev/e8986cfb6d2b42b1/` 已存在但没有 app.db；正常启动成功后创建并填入 two 测试库。
原 `e898dc53cb39df07/app.db` 哈希未变。第二轮重开前后全部 data 文件哈希相同。
这不是通过删除旧库恢复，也不证明此前失败的原因已消除。

## 结论与后续边界

应用侧诊断链路已补齐，原 two 在当前会话未复现启动失败；根因仍未知。
没有依据修改 Runtime/DB/VFS、扩展 ABI、关闭旧 mkdir183/CANTOPEN 问题或宣布隔离通过。
下次出现失败时，先用同一探针取得非零快照；若 Runtime rc 没有 DB 明细，再针对对应 provider
原始失败点补最小 tracing，然后由主线判断 owner。不要通过重新建库、切卷或宽松断言恢复通过。

正式 Debug/Release 构建、C89 和现有 Runtime 接线检查通过；Release 二进制不含新日志或
自检失败关闭标记。VS 预构建/Release 编译中断证据保留于
`tmp/runtime-storage-debug-prebuild-failed.log`、`tmp/runtime-storage-release-interrupted.log`，
确认无残留后经正式配置重试成功，不把中断写作成功。

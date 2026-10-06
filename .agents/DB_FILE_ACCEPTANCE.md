# DB 文件可靠性验收清单

这是 DB 文件纵切的验收清单，不替代 [HANDOFF](HANDOFF.md) 的当前基线、
[KNOWN_LIMITATIONS](KNOWN_LIMITATIONS.md) 的能力边界或 [ROADMAP](ROADMAP.md) 的候选规划。
结构化错误合同与下列各门分别验收；内存页预算 FULL、正常关闭和独立进程冷重开
不能替代锁或 journal 证据。应用启用和 CAB 打包按其独立授权与交付维护，不能据此宣称
FULL/I/O 等未验收边界已经通过。

## 必须分别验收

- [x] 中文 UTF-8 目录/文件、中文 TEXT、typed bind、commit/rollback、精确字节、正常重开。
  Debug/Release 各自通过 SD 映射卷与内置 object-store 文件门。
- [x] A 提交关闭退出后 B 冷启动重开，B 提交退出后 C 复核。每种存储、每个配置均为三个
  不同 PID、wait/exit=0，实际 EXE/DLL 路径、schema/data/integrity 和关闭状态检查通过。
- [x] 读写/写写进程锁、实际 COMMIT BUSY、读可见性、锁释放后恢复；Debug/Release 的
  SD 映射卷与内置 object store 四组六进程门分别通过，证据如下。
- [x] 只对夹具自建子进程做有界异常终止，取得实际 hot rollback journal，再由新进程恢复、
  integrity 检查并继续写入；Debug/Release 的 SD 映射与内置存储四组通过。
  受控进程终止不能写成物理断电保证。
- [ ] 真实文件页配额 FULL 与确定性的 I/O、COMMIT、rollback 故障注入；取得根错误、实际
  事务状态、重新打开后的 schema/data/integrity。FULL 双配置正式构建通过、内置存储
  四进程门通过，但当前映射 SD 卷截断失败，整体门仍 FAIL；用户明确挂起该 FULL 门，
  不再安排重跑或要求更换设备，详见下方失败边界。
  I/O/COMMIT/rollback 注入未实现，不得填满磁盘/SD。
- [ ] 文件 migration 脚本/提交失败后，由新进程证明 schema/data/version 原子性与重试；
  未来/损坏版本拒绝。脚本中途失败的 schema/data/version 原子性已随上述四组文件门通过；
  提交故障、迁移重试和未来/损坏版本拒绝尚未验收。
  版本检查必须使用公共合同，不对 DLL 私有元数据执行 SQL。

## 已验收文件纵切

第一条纵切为 `device_tools/db_file_probe/`、`scripts/db_file_gate.ps1`、离线测试及 solution/stage 接线。
协调器不打开 DB，子进程只消费 `PDb_*`；没有产品源编入 test_host、应用 schema、网络或
DB DLL 内线程。二进制只部署 SD，fixture 分别创建于专用 SD 子目录和内置 Temp。
每个 DB 上限 2 MiB，每个子进程 60 秒；至少保留 5 MiB 文件系统余量。目录/日志 CREATE_NEW，
不覆盖用户文件；数据库先以 CREATE_NEW 保留空文件，再交公共 DB API 打开。
超时只能结束本协调器仍持有 CreateProcess handle 的测试子进程。

用户释放主线窗口后，正式串行 Debug/Release build/CAB/stage 均通过。当前设备为
240×320、96 DPI DeviceEmulator；SD 是主机映射卷，不是物理 SD/真机断电证明。
两包各 50 文件，probe/cleanup/九 DLL 的同配置哈希回读、前后重新执行的 guest 无引用审计、
双空间余量、完整日志及无新增 crash 检查通过；四个数据库回读哈希一致。通过包的 SD/内置
夹具和部署目录均在证据取回后精确删除，本地日志/数据库留存，可复查。

- 文件 Debug：`tmp/device-runs/20261005-020026-db-file-unicode-debug-fixed/`。
- 文件 Release：`tmp/device-runs/20261005-020334-db-file-unicode-release/`。
- 相邻 `1321,999` Debug：`tmp/device-runs/20261005-020537-db-file-adjacent-debug/`。
- 相邻 `1321,999` Release：`tmp/device-runs/20261005-020727-db-file-adjacent-release/`。

两次相邻回归均 selected/observed=2/2、唯一 TESTBENCH PASS、零 ERROR/FAIL、完整日志、
正确 Core 路径、无新 crash，部署目录已清理。C89、仓库审计、九项 probe 离线检查和三十项
门验证器检查通过；离线 SQLite oracle 不替代以上 WM6 证据。

## 已验收跨进程锁纵切

`db_lock_probe.h` 只消费公共 API。创建进程退出后，读写及写写两组并发进程通过新建、
flush 后的专用文件标记握手，最后由第六个进程冷重开复核。双方 Open 完成后才争锁，
peer 等待上限 15 秒；读写 COMMIT 返回原生 BUSY，写事务仍 active/write、cleanup=0，
读者检查旧快照后释放锁，写者重试成功，读者的新事务可见新行。写写 BEGIN IMMEDIATE
返回原生 BUSY 且没有开启事务，第一写者提交后第二写者重试成功。冷重开复核四行、
公共 schema 版本、完整性以及 idle/零 statement，所有子进程关闭 DB 后退出。

- 锁 Debug：`tmp/device-runs/20261005-220011-db-file-locks-debug-space-recovered/`。
- 锁 Release：`tmp/device-runs/20261005-220230-db-file-locks-release-space-recovered/`。
- 相邻 `1321,999` Debug：`tmp/device-runs/20261005-220449-db-file-locks-adjacent-debug/`。
- 相邻 `1321,999` Release：`tmp/device-runs/20261005-220538-db-file-locks-adjacent-release/`。

两配置的 SD 与内置数据库均通过六个独立 PID、并发顺序、native/category BUSY、实际事务
状态、旧/新可见性、释放后的重试和冷重开检查。完整正式 SD 包、probe/九 DLL 哈希回读、
前后新 guest holders=0 unavailable=0、至少 5 MiB 双空间预检、无新增 crash 及完整日志
回收通过。四个数据库哈希一致，验收夹具/包已精确清理；证据仍在本地，不入 Git。
两次相邻门均 selected/observed=2/2、唯一 TESTBENCH PASS、零 ERROR/FAIL、正确 Core 路径、
无新增 crash，完整日志取回后部署目录已清理。正式双配置 build/stage 通过，Release probe
零错误/警告；仓库审计与 diff 空白门通过。
十二项 probe 离线检查、五十四项日志验证器检查与 C89 工具检查通过；离线 oracle 不替代
以上 WM6 六进程证据。锁夹具不是 hot journal、故障注入或物理断电测试。

## 已验收受控异常退出 journal 纵切

`db_journal_probe.h` 只消费公共 DB API，协调器只编排五个独立进程并只读复制夹具文件。
创建与 seed 进程退出后，写者用四页 cache、cache spill 和未提交事务产生真实落盘变化。
flush 后的 ready 标记证明 active/write、零 statement、未 commit/rollback/close；协调器
只终止本轮 CreateProcess 返回且仍存活的写者 handle，确认专用退出码和 signalled 状态。
在恢复进程启动前保留原已提交 DB、hot DB/journal 三份 CREATE_NEW 快照。

四组实际 journal 均为 333,312 字节，magic 有效，首段 records=1、原 DB=69 页、sector=512、
page=4096；已提交与 hot DB 哈希不同，排除了只有 journal 而未 spill 的假通过。
新进程恢复原中文 TEXT、64 行逐字节 2048-byte BLOB 和 schema=2，要求较低版本先被公共
migration API 拒绝，再检查当前版本，避免空 migration 补写丢失的版本。integrity、idle/零
statement、恢复后提交及下一独立进程冷复核通过。DB 上限 128 页，journal 快照限 1 MiB，
双空间预检仍保留 5 MiB，不填满卷或终止用户进程。

本轮证据入口：

- journal Debug：`tmp/device-runs/20261005-223421-db-file-journal-final-debug/`。
- journal Release：`tmp/device-runs/20261005-223629-db-file-journal-final-release/`。
- 锁回归 Debug：`tmp/device-runs/20261005-222846-db-file-journal-locks-debug/`。
- 锁回归 Release：`tmp/device-runs/20261005-223159-db-file-journal-locks-release/`。
- 相邻 `1321,999` Debug：`tmp/device-runs/20261005-222710-db-file-journal-adjacent-debug/`。
- 相邻 `1321,999` Release：`tmp/device-runs/20261005-222805-db-file-journal-adjacent-release/`。

两次锁回归四组通过；两次相邻门均 selected/observed=2/2、唯一 TESTBENCH PASS、零
ERROR/FAIL、Core 路径正确、无新增 crash，完整日志取回后精确清理部署目录。
正式 Debug/Release build/stage、九 DLL 同配置哈希回读、前后新 guest holders=0
unavailable=0、完整日志、无新增 crash 和夹具/包精确清理通过。十四项离线 probe 检查、
八十一项门验证器检查、C89 与仓库审计通过；离线 host SQLite oracle 不替代 WM6 证据。
当前仍为 DeviceEmulator/SD 主机映射卷，进程终止不模拟物理断电或存储 flush 故障。

## 保留失败与恢复边界

首次 Debug 门 `tmp/device-runs/20261005-015637-db-file-unicode-debug/` 在打开 DB 前失败：
WM6 对不存在文件的属性检查未返回预期 LastError，夹具误拒绝新文件。改为原子 CREATE_NEW
后重跑通过；原失败包/日志保留，未放宽目录独占或改写旧结果。Release/CAB 两次沙箱内文件
创建失败日志与同一正式入口受审执行后的成功日志保存在 `tmp/db-file-builds/`，不改 CAB 实现。

锁候选的四次早期 SD 部署在首个 JS 文件的 CeWriteFile 返回 device=5，未启动锁测试；
失败包保留于
`tmp/device-runs/20261005-213717-db-file-locks-debug/`、
`tmp/device-runs/20261005-213832-db-file-locks-debug-retry/`、
`tmp/device-runs/20261005-213911-db-file-locks-debug-reviewed/` 与
`tmp/device-runs/20261005-214646-db-file-locks-debug-after-user/`，工具错误转录在
`tmp/db-lock-deploy-failures.txt`。失败不当作 DB 断言失败或锁门通过。停滞的早期 Release
构建只在用户明确授权并核对身份后取消 PID 22364/14904，返回 -1；随后正式 Release 重试
零错误/警告通过，不把取消写成成功，未结束其他 VS、WMDC 或用户设备进程。

`tmp/device-runs/20261005-215115-db-file-locks-debug-reconnected/` 的 SD 整包及引用审计
虽通过，但内置 object store 仅余 2,469,888 字节，在启动测试前被原有 5 MiB 门阻断。
用户明确允许后，旧内置部署
`\Temp\Positron-device-gate\next756-picture-source-walk-release-20260907-013247`
的 16 个文件已完整双读 SHA256 校验归档于
`tmp/db-lock-old-package-archive-20261005-215943/`，新 guest 无 holder 审计后精确删除，
可由归档恢复。内置空间恢复到 8,374,272 字节，再取新包完成锁门；旧日志无完成标记，
所以不由普通自动清理绕过保留规则。未删除其他目录或 crash dump，未降低 5 MiB 门。

### 文件 FULL 的映射卷截断限制

当前 DeviceEmulator 的映射 SD 卷在 FULL 后的数据复核返回原生 10、extended=1546
（SQLITE_IOERR_TRUNCATE），Win32=50（ERROR_NOT_SUPPORTED）。根 FULL 快照为 native=13、
active=0、txn=0、statements=1，Finalize 后零 statement；这些状态不能代替数据恢复成功。
独立 CREATE_NEW 小文件的 SetEndOfFile(2048) 同样失败，4096 字节未缩短，排除了只由 SQL
或日志断言造成的失败。SQLite 上游 WinCE VFS 使用该原生调用，未修改上游或伪造成功。

同配置、同 SD 二进制包中的内置存储截断成功；两个配置各自完成四个新进程的 FULL 回滚、
原中文 TEXT/BLOB/schema/integrity、小写入重试及冷复核，最终 DB 为 20,480 字节且双配置
回读哈希一致。这仅证明内置存储该夹具，SD 失败使整个门保持 FAIL。Quota 改为先内置后 SD
只保留独立证据，两个存储仍都必须通过；保留 5 MiB 余量，没有真实卷耗尽或新增 crash。

- 首次 FULL Debug：`tmp/device-runs/20261005-225044-db-file-quota-debug/`。
- 截断诊断 Debug：`tmp/device-runs/20261005-225458-db-file-quota-truncate-debug/`。
- 截断诊断 Release：`tmp/device-runs/20261005-225703-db-file-quota-truncate-release/`。

相邻 Debug `tmp/device-runs/20261005-230155-db-file-quota-adjacent-debug/` 与 Release
`tmp/device-runs/20261005-230301-db-file-quota-adjacent-release/` 的 `1321,999` 均为 2/2、
唯一 TESTBENCH PASS、零 ERROR/FAIL、完整日志、Core 路径/crash 检查通过，部署已精确清理。
Release 失败包的最终只读 guest 审计日志 `db-file-evidence/module-audit-final.log` 确认
holders=0 unavailable=0；本轮串行构建/设备窗口现已释放，下轮必须重新审计，不复用结论。

失败夹具与包均保留，不执行成功清理。用户明确挂起本测试；仅在用户重新开启，且具备
支持原生截断的 SD 卷/设备或另行批准的存储/VFS 方案后，才重新规划验收。
不把当前映射卷的结果外推为所有 SD/OEM 都不支持，也不以读写/锁/journal 的既有通过覆盖
此次失败。不将未通过候选写成正式基线，确定性 I/O 和迁移提交故障仍待独立实现。

## 挂起的文件 FULL 门与其余待验收项

本轮已复核 ROADMAP，文件页配额 FULL 按用户决定移入暂缓队列，不再作为当前下一步。
挂起不是通过，也不扩大内置存储的部分证据；确定性 I/O 与提交/rollback 故障仍另设独立门。
不覆盖 EXE/CAB 的既有交付，也不改变其独立授权与策略。
`db_quota_probe.h` 与 `-Suite Quota` 为未通过完整设备门的候选：创建→FULL 写者→冷重开/小写入重试→
冷复核四进程，仅消费公共 API。32 页/128 KiB 文件配额下，事务内先修改 TEXT、插入小 BLOB，
再由大 zeroblob INSERT 的 Step 触发原生 FULL；Finalize 前捕获根错误和 idle/一个 statement，
Finalize 后验证零 statement、先前修改均回滚。另两个新进程验证精确 schema/data/integrity、
重试提交及冷重开。此门不注入 I/O、COMMIT 或 rollback 失败，双空间余量仍为 5 MiB。
源码/C89、十六项 probe 离线检查及一百一十二项门验证器检查通过；它们不替代 ARM/WM6。
正式 Debug/Release build/stage 与 SD 部署已执行；夹具作为挂起诊断保存，不提升为完整验收。
夹具源码、日志和原断言保留，不自动重跑或继续调查替代 VFS。恢复须用户明确重新开启并
满足上述进入条件；不能默认跳过 SD、关闭 spill 或伪造 truncate 成功。
其余未勾选门保持待实现、待执行。真实卷耗尽和物理断电缺少安全实验条件，仍未验证；
不填满磁盘、不强杀用户进程，不把夹具提交等同于故障可靠性验收。

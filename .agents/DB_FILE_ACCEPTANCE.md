# DB 文件可靠性验收清单

这是 DB 文件纵切的验收清单，不替代 [HANDOFF](HANDOFF.md) 的当前基线、
[KNOWN_LIMITATIONS](KNOWN_LIMITATIONS.md) 的能力边界或 [ROADMAP](ROADMAP.md) 的候选规划。
结构化错误合同与下列各门分别验收；内存页预算 FULL、正常关闭和独立进程冷重开
不能替代锁或 journal 证据。禁止据此恢复应用正常启动的持久化或 CAB DB 依赖。

## 必须分别验收

- [x] 中文 UTF-8 目录/文件、中文 TEXT、typed bind、commit/rollback、精确字节、正常重开。
  Debug/Release 各自通过 SD 映射卷与内置 object-store 文件门。
- [x] A 提交关闭退出后 B 冷启动重开，B 提交退出后 C 复核。每种存储、每个配置均为三个
  不同 PID、wait/exit=0，实际 EXE/DLL 路径、schema/data/integrity 和关闭状态检查通过。
- [ ] 读写/写写进程锁、实际 COMMIT BUSY、读可见性、锁释放后恢复；SD 映射与内置文件系统
  分开记录。未实现夹具、未验收。
- [ ] 只对夹具自建子进程做有界异常终止，取得实际 hot rollback journal，再由新进程恢复、
  integrity 检查并继续写入。未实现夹具、未验收；不能写成物理断电保证。
- [ ] 真实文件页配额 FULL 与确定性的 I/O、COMMIT、rollback 故障注入；取得根错误、实际
  事务状态、重新打开后的 schema/data/integrity。未实现夹具、未验收；不得填满磁盘/SD。
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

## 保留失败与下一步

首次 Debug 门 `tmp/device-runs/20261005-015637-db-file-unicode-debug/` 在打开 DB 前失败：
WM6 对不存在文件的属性检查未返回预期 LastError，夹具误拒绝新文件。改为原子 CREATE_NEW
后重跑通过；原失败包/日志保留，未放宽目录独占或改写旧结果。Release/CAB 两次沙箱内文件
创建失败日志与同一正式入口受审执行后的成功日志保存在 `tmp/db-file-builds/`，不改 CAB 实现。

已复核 ROADMAP 并移除中文文件/独立新进程待验收条件。下一条独立纵切为两种文件系统的
读写/写写锁竞争与恢复；其余未勾选门保持待实现、待执行。真实卷耗尽和物理断电缺少安全
实验条件，仍未验证；不填满磁盘、不强杀用户进程、不提前启用应用持久化。

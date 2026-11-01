# Archive 首版 reader 验收

本页保存独立 ZIP reader 的首批验证证据，不是后续实现计划。
公共合同见 [Archive README](../../positron_archive/README.md)，未来工作见
[路线图](../../.agents/ROADMAP.md)。

## 实现与验证范围

miniz 3.1.2 固定提交 `77d0dce8627735138c51770d1799a1ef48f2117d` 静态编入
`positron_archive.dll`。正式 VS2008/WM6 Professional ARMV4I Debug/Release 构建通过；
DLL 只导出七个 PArchive reader 入口，导入 COREDLL，无 Runtime/Core/Browser 依赖。
COFF machine 0x1c2 为 ARM/Thumb，不是 x86。第三方声明、版本、许可证、边界审计已扩展。

最终设备证据：

- Debug：`tmp/device-runs/20261101-185426-archive-reader-debug-accepted/`。
- Release：`tmp/device-runs/20261101-185457-archive-reader-release-accepted/`。

均在当前 240×320、96 DPI 设备运行 TEST1374/1375/999，3/3 OK、唯一 TESTBENCH PASS、
零 ERROR/FAIL；模块审计 holders=0/unavailable=0、内部及 SD 目标空间、完整日志回收及
crash_check 通过，无新增 dump。当前部署在日志完整回收后清理，未强杀或重连设备。
TEST1375 独立比较 Archive 实际加载路径与测试 EXE 目录，避免把其他同名 DLL 当成新包。

TEST1375 用 Python zipfile 生成的离线字节验证 STORE/DEFLATE、空归档/目录/文件、中文、
data descriptor、逐块完整内容与 CRC、IO 错误、Abort/重新 Begin、两个 reader 隔离和
16 次重复 teardown。负例覆盖路径逃逸、反斜杠、设备名、大小写/Unicode 冲突、文件前缀冲突、
重叠条目、局部头不一致、未知压缩方法、加密、ZIP64、非零条目卷号、链接、NUL、坏 UTF-8、
长路径、数量/大小/总量和内存配额；同一 200 条目合法归档在默认预算通过、64 KiB 配额拒绝。
空输入/参数与同 reader callback 重入拒绝亦有断言。夹具只提供 source 和断言，不编译产品实现。

## 失败与修正边界

首轮危险路径夹具在 Windows 上被 Python zipfile 自动从反斜杠规范化为正斜杠，产品接受的
实际是合法归档。原拒绝断言未改变，生成器改为写完 ZIP 后直接修改局部头/中央目录的实际名称。
失败保留于 `20261101-184300-archive-reader-debug` 和 `20261101-184410-archive-reader-debug-review`
设备目录；后续通过不改写这两次 FAIL。

沙箱正式 Debug 构建曾返回 PDB C1902；提权后同一工程继续，未关闭调试信息规避。
一次 Release 构建在编译阶段无源码诊断提前退出，随后同一正式入口重试成功。
日志在 `tmp/archive-debug-sandbox-build-failed.log`、`tmp/archive-release-build-early-exit.log`
及 `tmp/archive-gate-*.txt`，均不入 Git。上游校验函数名称接错导致的链接失败也已修正，
最终两配置没有 Archive 新警告；既有 libcss 警告保持。

## 不由此证明的能力

设备夹具不写解压文件，因此不证明物理目录包含性、空间故障、短写、发布失败或磁盘清理。
CRC 不是签名。ZIP writer、PXI manifest/安装/更新、真实文件系统事务及 HTTP gzip 均未实现，
不能由 reader 或 Runtime 相邻门追认。分块限制输出量，不承诺 Deflate 或原生读取硬毫秒期限。

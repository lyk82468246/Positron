# Archive ZIP writer 验收

本页记录独立 writer 首版的验证，不代表 PXI 安装或 HTTP 压缩已完成。
调用及预算见 [Archive README](../../positron_archive/README.md)；reader 原始基线见
[reader 验收](ARCHIVE_READER_ACCEPTANCE.md)。

## 公共能力与兼容性

追加独立 PArchiveWriter handle、版本化 sink/info 和 Create/Add/Finish/GetInfo/Abort/Close。
旧 reader 的函数、结构和语义不变。STORE 与 level-1 FAST 只创建新 ZIP，微小项允许 STORE；
每次源/sink callback ≤16 KiB，I/O 由宿主提供。一次 Add 仍同步处理一个完整条目，取消在块间
检查，不声称可恢复 yield 或硬毫秒期限。实际写入后失败锁定整份归档，宿主丢弃全部临时输出。

启用 miniz tdef，TDEFL_LESS_MEMORY=1；保留请求级配额、NO_MALLOC/STDIO/TIME/ZLIB_APIS，
关闭非对齐和 64-bit-register 优化。NO_TIME 原默认 DOS 日期为零，Python 可读但月份/日为零；
最小移植补丁将两个 writer 默认 dos_date 改为 33，输出固定合法 1980-01-01。
补丁与上游来源在 [UPSTREAM](../../third_party/miniz/UPSTREAM.md)，未改 Deflate 算法。

## 最终证据

- Debug：`tmp/device-runs/20261101-191215-archive-writer-debug-final/`。
- Release：`tmp/device-runs/20261101-191249-archive-writer-release-final/`。

同一 240×320、96 DPI 目标，TEST1375/1376/999 各 3/3 OK、唯一 TESTBENCH PASS、零 ERROR/FAIL；
正式双配置构建、C89 检查、Archive 实际模块路径、零 holder、双空间预检和 crash_check 通过，
无新增 dump。设备门在日志与 ZIP 完整回收、PC 校验通过后清理本轮部署，未强杀或重连。

TEST1376 覆盖两方法完整 bytes/CRC、中文路径、目录/空文件/微小项、八次创建/销毁、空归档、
独立 writer、重入拒绝及完成后拒绝。非法路径/冲突/目录输入与数量/输入配额拒绝不写 sink，
原 writer 可完成；工作内存/输出配额、源错误、短写、Finish 失败、块间及 Finish 前取消
会锁定 FAILED/CANCELLED。Close 不隐式 Finalize，也不声称删除或回滚宿主文件。

两配置各生成 51376-byte ZIP，五个条目共 180249-byte 原始内容。设备门回拉后调用
`scripts/verify_archive_writer.py`，Python zipfile 独立验证名称、方法、逐字节内容、CRC、
UTF-8/data descriptor 与固定合法日期；`archive-writer-interop.txt` 为 PASS。
桌面已安装 7-Zip 的 `t` 命令也返回 Everything is Ok。另将 STORE payload 翻转一字节，
校验器返回 FAIL/退出码 1，负向检查未进入设备基线。

首次诊断包 `tmp/device-runs/20261101-190621-archive-writer-debug/` 只做 DLL roundtrip，
PreserveDeployment 令其状态为 DIAGNOSTIC_ONLY，不替代最终门。早期 interop 包验证了内容，
但未覆盖合法日期；最终门增加该断言后重新验证。临时材料留在 tmp，不加入发布包。

## 仍未覆盖的边界

测试宿主在自身隔离部署目录保存 ZIP，仅证明 fixture 的完整写入/Flush/Close 与库合同，
不证明通用源文件锁定、目标目录权限、磁盘故障或安装事务。源/sink 原生调用不可被抢占，
工作内存上限不包含宿主 buffer/CRT 内部开销；实际 WM6 真机速度另需测量。
ZIP64、加密、多卷、原地修改、权限/时间戳保真、PXI 安装及 HTTP gzip 不在本批范围。

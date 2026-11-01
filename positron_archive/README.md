# positron_archive.dll

供 WM6 ARMV4I 原生应用独立调用的有界 ZIP 归档边界。当前提供读取、枚举和分块解压；
构建/设备验收状态见 [.agents/HANDOFF.md](../.agents/HANDOFF.md)。压缩创建尚未实现。
仅依赖系统 CRT/字符转换，miniz 静态编入 DLL；消费者不需要 Runtime、Core 或 Browser。

## 支持范围与预算

接受单卷普通 ZIP 的 STORE/DEFLATE、空归档、空文件、目录和 data descriptor。
文件名接受 ASCII 或 general-purpose bit 11 明确标记的严格 UTF-8；不猜 OEM/ANSI 编码，
不采用 extra field 中另一个路径作为输出名称。未知 extra field 不授予新功能。
不支持 ZIP64、加密、多卷、符号链接及其他特殊文件类型、其他压缩算法或原地修改。

| 预算 | 上限 |
|---|---|
| 归档输入 | 32 MiB |
| 条目 | 1024（目录也计数） |
| 单项 / 全部条目声明解压量 | 16 MiB / 64 MiB |
| 路径 / 深度 | 240 UTF-8 bytes / 16 层 |
| 每次 Read 的调用方 buffer | 1..16384 bytes |
| reader 工作集 | 最高 1 MiB；包括索引、名称、解压状态和瞬时 realloc 双份 |

`PArchiveLimits` 可以降低条目、解压量和内存上限，不能超过上述硬上限；默认使用上限。
工作集不包含调用方持有的输入/输出 buffer、CRT 分配器内部开销或宿主文件缓存。
目录在 Open 时整体准入，CRC 和实际输出量在各条目读取时检查；Open 成功不表示正文完好。
累计预算针对归档内唯一条目的声明大小，不是多次重读的会话配额。

## 调用与所有权

包含 [positron_archive.h](positron_archive.h)，链接同配置 `positron_archive.lib`，部署对应 DLL。
接口不暴露 miniz 结构或分配器，不需要单独部署第三方压缩 DLL。

1. 以 `PArchiveSource` 提供已知大小、不可变、可随机读取的 `read_at` callback。
2. `PArchive_Open` 完整准入后发布 opaque reader；失败保持原输出。
3. `PArchive_Count` / `PArchive_Entry` 获取复制的目录信息，Entry 结构先填 size/version。
4. `PArchive_Begin(index)` 选择单个条目，然后重复 `PArchive_Read`。
5. OK 表示返回暂定 bytes；DONE 才表示条目长度/CRC 完整验证，最后一次也可能带 bytes。
6. 失败或取消丢弃该条目的全部输出。`PArchive_Abort` 退休当前读取，可重新 Begin。
7. `PArchive_Close` 释放全部 DLL 内存，NULL 安全；释放后不得继续使用该指针。

```c
PArchiveReader reader = NULL;
unsigned char block[PARCHIVE_CHUNK_BYTES];
unsigned int bytes;
int result;
/* source has size/version, archive_bytes, read_at and user_data. */
result = PArchive_Open(&source, NULL, &reader);
if (result == PARCHIVE_OK) {
    result = PArchive_Begin(reader, index);
    while (result == PARCHIVE_OK) {
        result = PArchive_Read(reader, block, sizeof(block), &bytes);
        if (result >= 0) {
            /* Write bytes to an isolated temporary sink. */
            /* Sink failure: Abort, discard output and stop. */
        }
    }
    /* Publish temporary output only if result == PARCHIVE_DONE. */
    PArchive_Close(reader);
}
```

source/callback/user_data 必须保留到 Close；callback 的 buffer 仅在同步调用内借用。
每个 reader 在同一 owner 线程顺序使用，不并发、不重入，不在调用过程中 Close。
DLL 不创建线程、不 DispatchMessage；宿主在 Read 返回后让步或 Abort。
每次输出字节有界，但同步 I/O、目录准入和 Deflate 扫描没有硬毫秒保证；需要 UI 响应时
由 worker 独占 reader，不能在另一线程释放它。

## 文件系统与失败语义

库不创建、覆盖、删除或重命名文件。它拒绝绝对/盘符/反斜杠/穿越路径、空段、尾随点/空格、
保留设备名、重复名称和文件/目录前缀冲突；名称比较使用目标 WM 的大小写忽略比较。
同时拒绝局部头与中央目录不一致及重叠条目。宿主仍须验证物理目标包含性、文件系统别名、
绝对路径总长、已有目标和实际空间；词法准入不等于磁盘权限或安装授权。

错误区分 ARGUMENT、INVALID、UNSUPPORTED、LIMIT、MEMORY、IO、STATE、PATH 与 INTEGRITY。
Read 的负结果将 written 清零，但 buffer 及此前输出可能已有暂定内容；必须丢弃，不能声称
文件级回滚。Abort 也不会删除宿主文件。CRC 不能证明发布者可信。

PXI manifest、身份、批准、版本发布和安装恢复属于 Runtime/EXE；Archive 不识别 PXI。
HTTP Content-Encoding 仍属于 HTTP，本接口不提供 gzip/zlib/raw stream 公共合同。

## 验证与后续

`TEST1375` 消费公共 import library，覆盖离线 ZIP、路径/冲突、CRC、IO、预算、逐块内容、
data descriptor、加密/ZIP64/多卷拒绝、Abort、请求隔离、callback 重入拒绝及重复 teardown。夹具由
`python scripts/generate_archive_fixtures.py` 生成，生产解析代码不在测试宿主中。
设备测试不写解压文件，不替代 WM 文件系统临时目录/提交失败或 PXI 安装验收。

下一条归档纵切是有界 STORE/DEFLATE 新 ZIP writer：顺序输出、源 callback、完成/取消及
PC 解包互操作；不原地修改归档。选型与最小兼容配置见
[miniz 移植说明](../third_party/miniz/UPSTREAM.md)。

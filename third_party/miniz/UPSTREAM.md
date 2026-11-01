# miniz 3.1.2

上游：[richgel999/miniz](https://github.com/richgel999/miniz)，标签 `3.1.2`，
提交 `77d0dce8627735138c51770d1799a1ef48f2117d`。MIT 许可证原文见 [LICENSE](LICENSE)。
本目录保留该提交的 miniz、common、tinfl、tdef、ZIP 源码/头文件及 ChangeLog；
不在构建时下载依赖。本地修改仅为 miniz.h/ChangeLog 尾随空白、下述 ZIP 默认日期补丁和
静态导出配置 miniz_export.h；Deflate/Inflate 算法不变，上游符号不导出。

正式 ARMV4I 工程编译 miniz.c、miniz_tinfl.c、miniz_tdef.c、miniz_zip.c，使用已有 compat/stdint.h。
定义 MINIZ_NO_MALLOC、MINIZ_NO_STDIO、MINIZ_NO_TIME、MINIZ_NO_ZLIB_APIS 和
TDEFL_LESS_MEMORY=1；reader/writer 的分配均经过 Archive 请求级配额分配器。
关闭非对齐访问及 64-bit-register 优化，明确 little-endian；不引入汇编或现代 CPU 指令。
NO_MALLOC 同时禁止上游辅助路径绕开配额分配。公共 writer 只提供 STORE/level-1 FAST，
不开放上游全部压缩等级或私有追加/修改功能。

miniz_zip.c 的两个 writer 局部默认 dos_date 从 0 改为 33（1980-01-01），避免无时钟构建
输出月份/日期为零的时间。此为明确的本地元数据补丁，不改变 Deflate/Inflate 算法；
上游源码升级时须保留或核验等效行为，不能用 PC 工具容错代替合法字段。

消费者只链接 positron_archive.lib，不直接使用 mz_* 或依赖本目录布局。
路径、容量、状态和宿主失败清理合同由 [Archive](../../positron_archive/README.md) 拥有。
不能把保留的上游 ZIP64/追加代码当成 Positron 已支持的公共能力；HTTP 压缩亦未接线。

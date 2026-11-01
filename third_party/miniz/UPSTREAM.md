# miniz 3.1.2

上游：[richgel999/miniz](https://github.com/richgel999/miniz)，标签 `3.1.2`，
提交 `77d0dce8627735138c51770d1799a1ef48f2117d`。MIT 许可证原文见 [LICENSE](LICENSE)。
本目录保留该提交的 miniz、common、tinfl、tdef、ZIP 源码/头文件及 ChangeLog；
不在构建时下载依赖。miniz.h 和 ChangeLog 仅清理原有尾随空白，算法源码未修改；`miniz_export.h` 是本地静态嵌入配置，令上游符号不导出。

正式 ARMV4I 工程只编译 miniz.c、miniz_tinfl.c、miniz_zip.c，使用已有 compat/stdint.h。
定义 MINIZ_NO_MALLOC、MINIZ_NO_STDIO、MINIZ_NO_TIME、MINIZ_NO_ZLIB_APIS 和
MINIZ_NO_DEFLATE_APIS；所有有效 reader 分配经过 Archive 请求级配额分配器。
关闭非对齐访问及 64-bit-register 优化，明确 little-endian；不引入汇编或现代 CPU 指令。
NO_MALLOC 同时禁止上游辅助路径绕开配额分配。tdef 源码保留供后续 writer 评估，当前不编译。

消费者只链接 positron_archive.lib，不直接使用 mz_* 或依赖本目录布局。
路径、容量、状态和宿主失败清理合同由 [Archive](../../positron_archive/README.md) 拥有。
不能把保留的上游 ZIP64/写入代码当成 Positron 已支持的公共能力；HTTP 压缩亦未接线。

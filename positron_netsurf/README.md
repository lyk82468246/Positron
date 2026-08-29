# `positron_netsurf`

`positron_netsurf` 是 Positron 对 NetSurf 的基础静态移植工程，输出 `bin\Debug\positron_netsurf.lib`。它把 libwapcaplet、libparserutils、字符集、输入流和 WM6/C89 兼容代码编译成 Core/Image 可以链接的内部库。

## 作用与消费者

该库不生成顶层 DLL、没有 Positron 应用公共头，也不是可以独立启动的浏览器。`positron_core.dll` 使用它处理 parserutils、样式/HTML 数据结构的底层支持；`positron_image.dll` 使用其中的共享基础设施。普通项目不应直接链接它或包含 NetSurf 内部头文件，应使用 `positron_core.h` / `positron_image.h`。

## 构建与来源

工程从仓库内 `netsurf-all-3.11` 快照编译，并纳入 `compat\positron_crt.c` 等 WM6 适配。必须通过 `scripts\build.bat Debug build` 的正式 VS2008 ARMV4I 配置构建；上游许可证、版本和本地补丁见 `netsurf-all-3.11` 及根 `THIRD_PARTY.md`。不要把这里的静态库当作稳定独立 ABI 发布。

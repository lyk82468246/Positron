# `positron_libcss`

`positron_libcss` 是移植后的 NetSurf libcss 静态库工程，输出 `bin\Debug\positron_libcss.lib`。它包含 CSS tokenizer、parser、属性解析、级联和 computed-style 选择所需的生成源码与适配，不生成顶层 DLL。

## 作用与消费者

`positron_core.dll` 链接该库，用它完成 `PCore_ParseCSS`、`PCore_StyleDocument`、媒体条件、继承和布局前的 style tree 构造。普通应用不能把 libcss 的 stylesheet、select context 或 NetSurf 对象当作公共 ABI；应使用 `positron_core.h` 的 `PCore_ParseCSS`、`PCore_StyleDocument[Ex]` 和 `PCore_FreeStylesheet`。

## 构建与来源

工程使用 `netsurf-all-3.11/libcss`、libwapcaplet、libparserutils 和 `compat`，通过 `scripts\build.bat Debug build` 随根解决方案生成。修改生成属性文件或上游版本时，保留上游许可证、项目审计和 C89/ARMV4I 约束；来源和许可证见 NetSurf 快照及根 `THIRD_PARTY.md`。

WM6 的样式选择及 computed-style 初始化所需的选择状态，均按调用独立分配在堆上，避免大型自动变量跨过 ARMV4I 线程的栈保护页。分配失败返回 `CSS_NOMEM`，失败和正常清理均释放状态；不能改回大栈对象或共享静态状态。该适配隐藏在 Core 内，不改变应用的公共 ABI 或 computed-style 所有权。

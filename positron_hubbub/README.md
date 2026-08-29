# `positron_hubbub`

`positron_hubbub` 是 NetSurf Hubbub HTML tokenizer/tree-builder 的 WM6 静态库工程，输出 `bin\Debug\positron_hubbub.lib`。它不生成顶层 DLL，也没有 Positron 应用公共头。

## 作用与消费者

它负责把 HTML 字节流按 Hubbub 规则分词并驱动树构建，和 `positron_libdom` 的 DOM binding、`positron_netsurf` 的 parserutils 支持一起被 `positron_core.dll` 链接。`PCore_ParseHTML` 是应用应使用的入口；应用不应直接包含 `netsurf-all-3.11/libhubbub` 头文件或依赖内部符号。

## 构建与边界

工程固定使用仓库内 NetSurf 3.11 快照、parserutils headers 和 `compat` C89 适配。通过根解决方案和 `scripts\build.bat Debug build` 构建，不能单独拼装另一套 Hubbub。上游许可证和来源见 `netsurf-all-3.11/libhubbub/COPYING`、根目录 `THIRD_PARTY.md`；若需要 HTML 解析，应调用 `positron_core.dll` 并由它管理 DOM 生命周期。

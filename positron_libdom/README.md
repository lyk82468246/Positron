# `positron_libdom`

`positron_libdom` 是移植后的 NetSurf libdom DOM、HTML 元素和事件实现静态库工程，
输出 `bin\Debug\positron_libdom.lib`。它不生成 DLL，也不向应用暴露 libdom 对象。

## 作用与消费者

该库把 Hubbub/Expat parser binding 接到 DOM 树，并提供 Core 内部使用的节点、表单、
事件和 HTML 元素实现。`positron_core.dll`（以及图像/SVG 的内部组合路径）链接它；
应用应使用 `positron_core.h` 的 `HANDLE`、`PCore_Node*`、`PCore_Event*` 和
`PCore_Free*` API，而不是直接调用 libdom。

## 构建与所有权

工程依赖 `positron_expat`、NetSurf 的 libhubbub/libparserutils headers 和 WM6
兼容层，必须通过根解决方案正式构建。DOM 节点的生命周期由 Core document 管理，
任何内部 refcount、vtable 或上游类型都不能跨公共 DLL 边界。上游版本和许可证见
`netsurf-all-3.11/libdom`、`positron_expat/UPSTREAM.md` 与根 `THIRD_PARTY.md`。

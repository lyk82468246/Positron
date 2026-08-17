# `positron_expat`

`positron_expat` 是 Positron 为 WM6 固定的 Expat 静态库工程，不是运行时 DLL。
它把 `expat/` 中固定版本的 XML tokenizer/parser 与 `random_wince.c` 平台适配编译
为 `bin\Debug\positron_expat.lib`。

## 作用与消费者

该库提供上游 Expat 的静态 XML C ABI，主要被 `positron_libdom` 的 XML binding 和
图像/SVG 组合路径使用；它最终被产品 DLL 静态链接进去。普通应用不应直接链接它，
更不应把 Expat 类型穿过 `positron_core.dll` 或 `positron_image.dll` 公共边界。

工程使用 `XML_STATIC`、UTF-8、namespace processing，并关闭 DTD/general entity 等
不需要的能力，以适配 WM6 和降低不受信任 XML 的攻击面。精确版本、补丁和许可证见
[`UPSTREAM.md`](UPSTREAM.md) 与 `COPYING`。

## 构建

从仓库根目录执行 `scripts\build.bat Debug build`，解决方案会在构建依赖产品前先生成
该 `.lib`。修改上游源或 C++ 兼容层时，必须保留上游 ABI/许可证并运行仓库审计；应用
调用 XML/DOM 时应改用 `positron_core.h` 的公共 opaque API。

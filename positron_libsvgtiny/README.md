# `positron_libsvgtiny`

`positron_libsvgtiny` 是 NetSurf libsvgtiny 的 WM6 静态库工程，输出
`bin\Debug\positron_libsvgtiny.lib`。它不生成 DLL，不提供应用级 SVG API。

## 作用与消费者

`positron_image.dll` 使用它解析 SVG 的 XML/形状数据，再转成 Positron 自己拥有的
保留式绘制对象；应用应调用 `positron_image.h` 的
`PImage_CreateSvgFromMemory`、`PImage_SvgGetInfo`、`PImage_DrawSvg` 和
`PImage_FreeSvg`。不要直接包含 libsvgtiny 头文件或管理其 diagram/list 对象。

## 构建与边界

工程固定引用 `netsurf-all-3.11/libsvgtiny`、libdom、libparserutils、libwapcaplet
和 `compat`，由根解决方案正式构建。它只覆盖当前图像服务支持的 SVG 子集，并不
意味着完整浏览器 SVG/CSS 实现；上游许可证见 NetSurf 快照和根 `THIRD_PARTY.md`。

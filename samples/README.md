# `samples`

这里放面向普通 WM6 应用的最小消费者示例。示例不属于浏览器宿主，也不应反向成为产品 DLL 的实现依赖。

当前示例：

- [`positron_image_demo`](positron_image_demo/README.md)：只链接公共 `positron_image.dll`，演示 ABI 检查、位图/SVG 创建、绘制、编解码和所有权。

每个示例目录都包含自己的 `.vcproj` 和运行说明。构建时使用根解决方案的 VS2008 ARMV4I 配置；部署时只复制该示例 README 所列的公共 DLL，不要把内部静态库或 NetSurf 头文件交给示例消费者。

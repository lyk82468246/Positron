# `positron_image`

`positron_image.dll` 是可被普通 WM6 应用复用的位图与 SVG 服务。它提供保留式
opaque 图像对象、GDI 绘制、原始像素导入、BMP/PNG/JPEG/GIF 编解码和受限 SVG
解析/绘制；不拥有 DOM、布局或窗口消息循环。

## 输出与依赖

- 工程：`positron_image.vcproj`
- 输出：`bin\Debug\positron_image.dll`、对应 `.lib`
- 公共头：`positron_image.h`
- 静态实现依赖：`positron_libjpeg`、`positron_libsvgtiny`、`positron_libdom`、
  `positron_expat`、`positron_netsurf`；设备侧还使用 WM Imaging

其他项目只应包含 `positron_image.h`，链接 `positron_image.lib` 并部署 DLL，
不要包含 NetSurf、libdom、libsvgtiny 或 WM Imaging 私有头。

## 其他项目如何调用

调用者可以先检查 ABI，再创建、绘制和释放保留对象：

```c
#include "positron_image.h"

PIMAGE_BITMAP bitmap;
int width;
int height;

if (PIMAGE_ABI_VERSION_GET_MAJOR(PImage_GetAbiVersion()) !=
        PIMAGE_ABI_VERSION_MAJOR) {
    return 1;
}
if (PImage_CreateBitmapFromMemory(bytes, byte_count, &bitmap) == PIMAGE_OK) {
    PImage_BitmapGetInfo(bitmap, &width, &height);
    PImage_DrawBitmap(bitmap, hdc, 0, 0, width, height);
    PImage_FreeBitmap(bitmap);
}
```

主要功能包括：

- `PImage_CreateBitmapFromMemory`：从编码字节创建 BMP/PNG/JPEG/GIF 对象；
- `PImage_CreateBitmapFromPixels`：导入 top-down BGR24/BGRA32 像素；
- `PImage_EncodeBitmap[Ex]`：导出编码缓冲，结果用 `PImage_FreeBuffer` 释放；
- `PImage_CreateSvgFromMemory` / `PImage_DrawSvg`：解析并绘制受限 SVG；
- `PImage_*Info` 与 `PImage_BitmapLastError`：读取尺寸、统计和设备错误。

输入缓冲由调用者拥有，DLL 在需要时复制；返回对象和输出缓冲必须使用匹配的
`PImage_Free*`。保留对象具有创建线程亲和性，创建、查询、绘制和释放应在同一线程。
编码器能力受设备 WM Imaging 安装情况影响，SVG 不是完整浏览器 SVG 实现。

## 示例与验证

`samples\positron_image_demo` 是只依赖此 DLL 的完整示例，覆盖 raw pixels、PNG、
JPEG、BMP、GIF、SVG、stride、alpha 和生命周期。根解决方案构建后可运行
`scripts\stage_image_demo.bat Debug <共享目录>` 部署到模拟器。

# `positron_image`

`positron_image.dll` 是可被普通 WM6 应用复用的位图与 SVG 服务。它提供保留式 opaque 图像对象、GDI 绘制、原始像素导入、BMP/PNG/JPEG/GIF 编解码和受限 SVG 解析/绘制；不拥有 DOM、布局或窗口消息循环。

## 输出与依赖

- 工程：`positron_image.vcproj`
- 输出：`bin\Debug\positron_image.dll`、对应 `.lib`
- 公共头：`positron_image.h`
- 静态实现依赖：`positron_libjpeg`、`positron_libsvgtiny`、`positron_libdom`、`positron_expat`、`positron_netsurf`；设备侧还使用 WM Imaging

其他项目只应包含 `positron_image.h`，链接 `positron_image.lib` 并部署 DLL，不要包含 NetSurf、libdom、libsvgtiny 或 WM Imaging 私有头。

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

输入缓冲由调用者拥有，DLL 在需要时复制；返回对象和输出缓冲必须使用匹配的 `PImage_Free*`。保留对象具有创建线程亲和性，创建、查询、绘制和释放应在同一线程。编码器能力受设备 WM Imaging 安装情况影响，SVG 不是完整浏览器 SVG 实现。

SVG 的样式边界需要特别注意：`PImage_CreateSvgFromMemory` 会在解析前把有界的、简单的
`<style>` class 规则（例如 `.st0 { fill: #11A14E; }`）转换为元素的行内 style，从而覆盖
libsvgtiny 不读取 class 样式导致的黑色路径。此适配支持最多 8 个 style 块、64 字节 class
名、2 KiB 单元素声明和 256 KiB 规范化输入；只承诺 libsvgtiny 已支持的 paint 属性、渐变引用、
viewBox 和 viewport 尺寸；简单 class 规则得到的 `display:none` 子树会在解析前被跳过，
`display:inline` 保持默认可见。它不是 CSS cascade：复杂选择器、`@media`、伪类、完整继承
和其他 SVG 样式语义仍不支持；超预算或无法安全规范化的输入 fail closed。完整 IANA 首页与
header fixture 的路径数量、颜色边界、隐藏 Text_Paths 和重复释放/重解析由 TEST1315 覆盖。
当调用者把 viewport 宽高都留空时，根 `viewBox` 会在有界扫描中提供圆整后的自然尺寸；只留空
其中一项时按同一比例计算缺失尺寸。无效或超预算的 viewBox 仍回退到 300x150。这样没有显式
width/height 的 IANA header SVG 会以约 `128x50` 而不是过大的 `300x117` 背景 tile 进入 Core，
不会被默认的 `background-repeat` 裁成残片。CSS background-image 的定位和页面资源生命周期
仍由 Core/宿主负责，不能由此接口推断。

SVG stroke 还支持有界的 `rgba(r, g, b, a)` 颜色、`stroke-opacity` 和
`stroke-linecap="butt|round|square"`。alpha 会随 stroke paint 进入 NanoSVG 的 GDI 合成，
不会把半透明颜色误当作不透明黑色；round/square cap 由同一 raster path 处理。非法或超出
`0..255`/`0..1` 范围的 rgba 值保持 fail closed，不会创建伪 stroke。精确的 WinWorld
navbar-toggler SVG（3 条 `rgba(0,0,0,0.5)`、2px、round-cap 路径）由 TEST1319 在
`PImage_CreateSvgFromMemory`→`PImage_DrawSvg` 的离屏像素回归中覆盖；这仍不意味着 Image DLL
实现完整 SVG/CSS cascade，也不替代 Core data-URI 或最终应用页面的视觉验收。

## 示例与验证

`samples\positron_image_demo` 是只依赖此 DLL 的完整示例，覆盖 raw pixels、PNG、JPEG、BMP、GIF、SVG、stride、alpha 和生命周期。根解决方案构建后可运行 `scripts\stage_image_demo.bat Debug <共享目录>` 部署到模拟器。

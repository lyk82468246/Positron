# `positron_libjpeg`

`positron_libjpeg` 是为 WM6/VS2008 编译的 libjpeg-turbo 静态库工程，输出
`bin\Debug\positron_libjpeg.lib`。它不生成顶层 DLL，也不应该由应用直接链接。

## 作用与消费者

`positron_image.dll` 使用该库提供 JPEG 解码和显式质量编码，外部应用通过
`positron_image.h` 的 `PImage_CreateBitmapFromMemory`、`PImage_EncodeBitmap[Ex]`
等接口间接使用 JPEG。应用不应包含 `jpeglib.h`、依赖 `jpeg_*` 符号或自己释放
内部 codec 对象。

## 构建与许可证

工程编译 `third_party/libjpeg-turbo` 的固定源文件，并保留 IJG、BSD 和 zlib
通知。使用 `scripts\build.bat Debug build` 生成；上游版本、补丁和发行义务见
`third_party/libjpeg-turbo/README.md`、`README.ijg`、`LICENSE.md` 和根
`THIRD_PARTY.md`。若需要设备支持的 JPEG 能力，请以 `PImage_*` 返回码判断，
不要假定所有 WM Imaging codec 都安装。

# Positron Architecture

更新时间：2026-07-15

## 项目定位

Positron 是 Windows Mobile 6 / WinCE 5.02 的现代基础设施集合，同时在这些基础设施上建设浏览器内核和 Electron-like 应用运行时。浏览器不是全部产品，也不是其他 DLL 存在的唯一理由。

项目有两个同等重要、相互复用的产品面：

1. **可独立调用的现代原生库**：为任意 WM C/C++ 程序提供 TLS、HTTP、JSON、图片、存储等旧平台缺失或过时的能力。
2. **浏览器与应用运行时**：组合上述 DLL，并移植 NetSurf、后续 JavaScript runtime 和 Native bridge，形成网页渲染及 Positron App 能力。

`test_host.exe` 是设备回归宿主和示例消费者，不是这些 API 的所有者。公共 DLL 必须能够脱离 test_host 和浏览器场景单独使用。

## 分层规则

### 公共 DLL

- `positron_tls.dll`：现代 TLS 与证书验证。
- `positron_json.dll`：稳定的 JSON C API。
- `positron_http.dll`：HTTP/HTTPS，复用 TLS 和系统网络能力。
- `positron_core.dll`：HTML/CSS/DOM/layout/redraw 的产品级引擎边界。
- `positron_image.dll`：统一 WM Imaging 位图与 SVG 能力，可被 core 或其他 WM 程序独立调用。公共 API 同时提供 opaque bitmap/SVG create/info/draw/free、ABI 查询、编码图片或原始 BGR24/BGRA32 到 retained bitmap，以及 retained 位图到 PNG/JPEG/BMP/GIF 内存编码。PNG/BMP/GIF 和默认 JPEG 使用 WM Imaging；ABI 1.2 的显式 quality JPEG 使用内部静态链接的 libjpeg-turbo 1.5.3 压缩器并固定 4:4:4。ABI 1.3 原始像素入口先验证长度/stride，再复制到 DLL 自有 WM 对齐行；ABI 1.4 增加系统 BMP/GIF encoder 路由，缺失时返回 unsupported。编码结果由 DLL 分配并以 `PImage_FreeBuffer` 释放，不向调用方暴露 COM、libjpeg、NetSurf 对象或跨 CRT 所有权。`positron_core` 的旧 `PCore_Image*` 保留为兼容转发。

公共 DLL 使用稳定 C ABI、UTF-8 字符串和 opaque handle。不得向调用者暴露 C++ ABI、NetSurf 内部结构或第三方库的易变类型。

### 内部静态库

NetSurf、libdom、libcss、libsvgtiny、mbedTLS、cJSON 等上游源码可以各自构建为静态 `.lib`，再藏在相应公共 DLL 后面。静态工程用于隔离移植补丁和构建依赖，不等于对外 API。

缺少能力时按以下顺序处理：

1. 检查仓库已 vendoring 的上游实现。
2. 联网检索成熟、许可证兼容、可移植到 VS2008/WM6 的开源实现或历史 WinCE port。
3. 移植、裁剪并用脚本处理可重复的 C89 兼容问题。
4. 只为平台胶水、ABI 包装或确实没有成熟实现的极小功能编写自有代码。

解析器、协议栈、密码学、图片编解码和 JavaScript 引擎不得在已有合适开源实现时自行重写。

## ABI 与所有权

- 创建对象的 DLL 负责提供对应释放函数；调用方不能跨 CRT 直接 `free` DLL 内部分配的对象。
- 缓冲区所有权、生命周期、线程约束和错误码必须写入公共头文件。
- 公共 DLL 应尽量减少传递依赖。只需要图片的程序不应被迫加载 DOM/CSS/浏览器核心。
- 现有公共 API 若需拆层，优先保留兼容转发，再逐步迁移调用者。
- WM6 资源有限，DLL 边界不能以明显重复缓存、重复解码或大量常驻内存为代价。
- ABI 版本高 16 位为 major、低 16 位为 minor；major 变化可能不兼容，minor 只能追加兼容 API。消费者应接受相同 major 且不低于其编译需求的 minor。

## 浏览器边界

`positron_core.dll` 负责 transport-agnostic 的解析、样式、布局和绘制；网络由 embedder 通过 fetch callback 提供，URL 合并/安全策略由可选 resolve callback 提供。`PCore_StyleDocumentEx2` 只扩展旧 ABI，不把 WinINet 或具体传输层引入 core；WM 宿主可用 `InternetCombineUrlA`，其他调用者可替换自己的 URL 服务。浏览器宿主负责窗口、消息循环、导航事务和设备交互。

图片拆分后的目标调用关系：

```text
WM application ---------> positron_image.dll
                              ^
                              |
browser host -> positron_core.dll

positron_image.dll -> WM Imaging API + NanoSVG rasterizer + positron_libjpeg.lib
                     + positron_libsvgtiny.lib
                     -> positron_libdom.lib -> positron_expat.lib
positron_core.dll  -> positron_image.dll + NetSurf/libdom/libcss static libraries
```

这样图片能力既服务浏览器，也成为 WM 平台可复用的现代基础设施。

`samples/positron_image_demo` 是边界验证样例：其 PE 导入表只含 `positron_image.dll` 和系统 `COREDLL.dll`，不链接 `positron_core`、`test_host` 或 NetSurf。它同时演示 ABI 协商、retained 位图/SVG，以及 PNG/JPEG 编码结果的配套释放与重新解码。

# Positron Architecture

更新时间：2026-07-14

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
- `positron_image.dll`：统一 WM Imaging 位图与 SVG 能力，可被 core 或其他 WM 程序独立调用。现有公共 API 提供 opaque SVG create/info/draw/free；解析对象和内存始终由 DLL 持有，位图 API 迁移与浏览器 `<img>` 接入仍在后续。

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

## 浏览器边界

`positron_core.dll` 负责 transport-agnostic 的解析、样式、布局和绘制；网络由 embedder 通过 fetch callback 提供，URL 合并/安全策略由可选 resolve callback 提供。`PCore_StyleDocumentEx2`、`PCore_FetchImageResourcesEx2` 与 `PCore_LinkAtEx2` 是旧 ABI 的兼容扩展，共享首个 `<base href>` 语义，但不把 WinInet 或具体传输层引入 core；WM 宿主可用 `InternetCombineUrlA`，其他调用者可替换自己的 URL 服务。图片缓存以规范 URL 去重并保留原始 `src` 别名，避免为了 URL 规范化修改 DOM。浏览器宿主负责窗口、消息循环、导航事务和设备交互。

HTTP 响应 ABI 当前保持 `9c5c7c7`/next37：状态、正文、长度和错误信息，不暴露最终重定向 URL。曾加入的 1536 字节 `effective_url` 字段、WinInet timeout options 和 TEST48 随 TEST13 长期不提交问题一起撤回；若重新设计，优先使用不扩大每个响应对象的独立查询/导航结果 API，并必须完整验收 TEST13。超时和资源数量/字节预算属于具体宿主及传输层策略，不是 `positron_core` ABI。

图片拆分后的目标调用关系：

```text
WM application ---------> positron_image.dll
                              ^
                              |
browser host -> positron_core.dll

positron_image.dll -> WM Imaging API + NanoSVG rasterizer + positron_libsvgtiny.lib
                     -> positron_libdom.lib -> positron_expat.lib
positron_core.dll  -> positron_image.dll + NetSurf/libdom/libcss static libraries
```

这样图片能力既服务浏览器，也成为 WM 平台可复用的现代基础设施。

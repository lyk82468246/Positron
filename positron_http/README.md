# `positron_http`

`positron_http.dll` 是 Positron 的同步 HTTP/HTTPS 客户端公共 DLL。它提供
HTTP/1.1 GET、POST、响应进度回调、统一响应对象，以及不执行网络 I/O 的有界
HTTP(S) reference/Location 解析；HTTPS 通过 `positron_tls.dll`，明文 HTTP 使用
WM6 WinInet 路径。

## 输出与依赖

- 工程：`positron_http.vcproj`
- 输出：`bin\Debug\positron_http.dll`、对应 `.lib`
- 公共头：`positron_http.h`
- 运行时依赖：`positron_tls.dll`；链接依赖 `positron_tls.lib`

应用链接 `positron_http.lib`，并把 `positron_http.dll` 与
`positron_tls.dll` 一起部署。页面调度、缓存、DOM 和导航不属于本模块。

## 其他项目如何调用

初始化一次即可；`PHttp_Init` 会初始化 TLS，应用不需要再单独调用 `PTls_Init`：

```c
#include "positron_http.h"

const char *headers[] = { "Accept: application/json", NULL };
PHttpResponse *response;

if (!PHttp_Init()) {
    return 1;
}
response = PHttp_Get("api.example.com", 443, "/v1/status", headers);
if (response != NULL && response->status_code == 200) {
    /* response->body 是 response 所有的、以 NUL 结尾的字节串。 */
}
PHttp_FreeResponse(response);
PHttp_Cleanup();
```

页面宿主或其他需要自行维护导航的消费者，可以复用同一套解析策略，而不用复制
重定向或目录相对拼接逻辑：

```c
char host[256];
char path[1024];
int port;

if (PHttp_ResolveReference("api.example.com", 443, "/v1/page.html",
        "../status?full=1#fragment", host, sizeof(host), path,
        sizeof(path), &port) == 0) {
    /* host="api.example.com", path="/status?full=1", port=443 */
}
```

`PHttp_ResolveReference` 只写入调用者提供的 UTF-8 缓冲区，不分配内存，也不发起
请求。它支持目录相对、`.`/`..`、query-only、network-path、绝对 HTTP(S) 和
fragment stripping；userinfo、IPv6、非法端口、非 HTTP(S)、无 origin 的普通相对
引用和容量不足都会失败。返回成功后 `path` 总是以 `/` 开头。HTTP GET 的 3xx
`Location` 自动跟随后也使用此函数；POST 不自动跟随。

需要响应进度时使用 `PHttp_GetEx` / `PHttp_PostEx`；回调同步发生在请求线程，
应保持短小，不能在回调中调用 `PHttp_Cleanup`。POST 的 `body` 是原始字节，
`body_len` 为负数时按 NUL 结尾字符串处理，`Content-Type` 由调用者通过 headers
设置。响应对象无论 HTTP 状态还是传输失败都应由 `PHttp_FreeResponse` 释放；
传输失败时通常 `status_code == 0` 且 `error_msg` 非空。

默认校验证书链和主机名。`PHttp_SetInsecure(TRUE)` 会对后续请求关闭验证，
只适合自签名诊断，不能作为生产默认值。当前连接采用短连接，响应体有设备侧上限；
具体限制以 `positron_http.h` 为准。

## 构建与验证

从仓库根目录运行 `scripts\build.bat Debug build`。修改 TLS/HTTP 边界后同时检查
`positron_tls` 的部署、错误路径和设备网络门；不要直接把 WinInet 或 Mbed TLS
对象暴露给业务项目。

# `positron_http`

`positron_http.dll` 是 Positron 的同步 HTTP/HTTPS 客户端公共 DLL。它提供 HTTP/1.1 GET、POST、响应进度回调、统一响应对象，以及不执行网络 I/O 的有界 HTTP(S) reference/Location 解析；HTTPS 通过 `positron_tls.dll`，明文 HTTP 使用 WM6 WinInet 路径。

协议和端口属于同一个 URL origin，不能只靠端口猜测。推荐使用 URL 入口：显式写出 `http://` 或 `https://`，端口可以省略（分别默认为 80/443），也可以写成非标准端口。省略协议的 URL 只默认 HTTPS，不会在 TLS 失败后静默降级到 HTTP；需要明文时必须显式写 `http://`。HTTPS 请求也拒绝跟随降级到 `http://` 的重定向；应用必须把这个响应视为失败并向用户说明。

URL-aware GET/POST 会在响应对象之外保留最后一个实际请求 URL。通过新增的 `PHttp_ResponseGetFinalUrl()` 查询它，不要读取或扩展 `PHttpResponse` 结构；旧结构和旧入口的 ABI 不变。应用可以用这个 URL 作为 CSS、图片和 `@import` 相对资源的解析基准。fragment 始终被剥离，因为它属于浏览器 history，不会发送到网络。

## 输出与依赖

- 工程：`positron_http.vcproj`
- 输出：`bin\Debug\positron_http.dll`、对应 `.lib`
- 公共头：`positron_http.h`
- 运行时依赖：`positron_tls.dll`；链接依赖 `positron_tls.lib`

应用链接 `positron_http.lib`，并把 `positron_http.dll` 与 `positron_tls.dll` 一起部署。页面调度、缓存、DOM 和导航不属于本模块。

## 其他项目如何调用

初始化一次即可；`PHttp_Init` 会初始化 TLS，应用不需要再单独调用 `PTls_Init`：

```c
#include "positron_http.h"

const char *headers[] = { "Accept: application/json", NULL };
PHttpResponse *response;

if (!PHttp_Init()) {
    return 1;
}
response = PHttp_GetUrl("https://api.example.com/v1/status", headers);
if (response != NULL && response->status_code == 200 &&
        response->error_msg[0] == '\0') {
    char final_url[PHTTP_URL_MAX];
    if (PHttp_ResponseGetFinalUrl(response, final_url,
            sizeof(final_url)) == 0) {
        /* final_url is the canonical base for dependent resources. */
    }
    /* response->body 是 response 所有的、以 NUL 结尾的字节串。 */
}
PHttp_FreeResponse(response);
PHttp_Cleanup();
```

需要保留非标准端口的协议时，使用 URL 入口。旧的 host/port 入口保持 ABI 和历史约定：80 选择明文 HTTP，443 或其他正端口选择 HTTPS，0 表示 HTTPS 默认端口。

```c
response = PHttp_GetUrl("http://device.local:8080/status", headers);
/* 或：PHttp_GetUrl("https://api.example.com/status", headers); */
```

页面宿主或其他需要自行维护导航的消费者，可以复用同一套解析策略，而不用复制重定向或目录相对拼接逻辑：

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

`PHttp_ResolveReference` 只写入调用者提供的 UTF-8 缓冲区，不分配内存，也不发起请求。它支持目录相对、`.`/`..`、query-only、network-path、绝对 HTTP(S) 和 fragment stripping；userinfo、IPv6、非法端口、非 HTTP(S)、无 origin 的普通相对引用和容量不足都会失败。返回成功后 `path` 总是以 `/` 开头。HTTP GET 的 3xx `Location` 自动跟随后也使用此函数；POST 不自动跟随。

需要让解析结果继续携带协议时，使用 `PHttp_ResolveReferenceUrl`。它返回完整的绝对 URL，因此 `http://device.local:8080/dir/` 的相对链接不会被改写为 HTTPS。`base_url` 为空时，`reference` 可以是带协议的 URL，也可以是省略协议的 host/path（按 HTTPS 解释）。外围 ASCII 空格可以被忽略；C0 控制字符、DEL、userinfo、IPv6、非法 scheme/端口和超出容量的输入一律失败，不会留下部分输出。

需要响应进度时使用 URL-aware 的 `PHttp_GetUrlEx` / `PHttp_PostUrlEx`；旧的 `PHttp_GetEx` / `PHttp_PostEx` 只为 ABI 兼容保留。回调同步发生在请求线程，应保持短小，不能在回调中调用 `PHttp_Cleanup`。POST 的 `body` 是原始字节，`body_len` 为负数时按 NUL 结尾字符串处理，`Content-Type` 由调用者通过 headers 设置。响应对象无论 HTTP 状态还是传输失败都应由 `PHttp_FreeResponse` 释放；响应体读取、Content-Length 截断、分块解码、分配或 1 MiB 容量失败时，body 会被丢弃，`status_code == 0` 且 `error_msg` 非空。应用不应把部分 body 当作成功。

默认校验证书链和主机名。`PHttp_SetInsecure(TRUE)` 会对后续 HTTPS 请求关闭验证，只适合自签名诊断，不能作为生产默认值。当前连接采用短连接，响应体有设备侧上限；具体限制以 `positron_http.h` 为准。显式 `http://` 请求不经过 TLS，也不会因为端口不是 80 而被错误送入 TLS。

## 构建与验证

从仓库根目录运行 `scripts\build.bat Debug build`。修改 TLS/HTTP 边界后同时检查 `positron_tls` 的部署、错误路径和设备网络门；不要直接把 WinInet 或 Mbed TLS 对象暴露给业务项目。

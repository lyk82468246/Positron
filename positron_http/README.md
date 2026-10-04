# `positron_http`

`positron_http.dll` 是 Positron 的同步 HTTP/HTTPS 客户端公共 DLL。它提供 HTTP/1.1 GET、POST、响应进度回调、统一响应对象，以及不执行网络 I/O 的有界 HTTP(S) reference/Location 解析。完整正文接口的 HTTPS 通过 `positron_tls.dll`，明文 HTTP 使用 WM6 WinInet；独立的流式 GET 请求族使用可取消的直连 TCP/TLS，不改变旧接口。

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

### 流式 GET 与跨线程取消

下载消费者可使用独立的 `PHttp_StreamCreateGet` / `PHttp_StreamGet` 请求族；旧完整 body
入口与 1 MiB 上限不变。流式入口复用原 URL resolver 和 TLS 验证，只把最多 4096 字节的
借用块同步交给 sink，不访问文件、DB、UI，也不累计整份响应。它使用直连 IPv4 TCP/TLS，
不消费 WinInet 系统代理、cookie jar 或缓存；需要这些平台能力的旧消费者继续使用旧入口。

```c
PHttpStreamOptions options;
PHttpStreamResult result;
HANDLE request;

memset(&options, 0, sizeof(options));
options.size = sizeof(options);
options.version = PHTTP_STREAM_VERSION;
options.timeout_ms = 60000;
options.body_limit = 64UL * 1024 * 1024;
options.headers = accept_final_headers; /* 可选；最终响应一次，非重定向中间响应。 */
options.sink = accept_block;            /* 零为接受，非零立即停止。 */
options.user_data = download;
if (PHttp_StreamCreateGet(url, headers, &options, &request) == PHTTP_STREAM_OK) {
    /* 在 worker 中执行；UI 只持有取消所需的请求映射。 */
    PHttp_StreamGet(request);
    memset(&result, 0, sizeof(result));
    result.size = sizeof(result);
    result.version = PHTTP_STREAM_VERSION;
    PHttp_StreamGetResult(request, &result);
    /* worker 返回并 join 所有 Get/Cancel 调用后再 Close。 */
    PHttp_StreamClose(request);
}
```

`Cancel` 是唯一允许跨线程的请求操作；重复取消在终态前幂等，取消与完成由请求锁确定
先后，终态后返回 STATE 且不覆盖结果。不能在执行中 Close 或 Cleanup，不得通过另一个
线程释放 socket/context。取消打断非阻塞 socket 的等待；不能抢占宿主 callback 或 TLS
计算。总期限为 1–120000 ms，包含 DNS、重定向和传输；同步 DNS 由 TLS 的最多四项
纯数据任务隔离，超时/取消不等待 OS 解析完成，迟到解析只释放自己的资源，不接触请求。

请求头最多 32 项/8192 字节；响应头及 trailer 分别最多 16 KiB/64 项、单行 1023 字节。
头文本只接受 ASCII/合法 UTF-8，其他 obs-text 编码返回 UNSUPPORTED，不冒充 UTF-8；
文件名不做字符集猜测或 RFC 5987 解码。
最终 metadata 包含 URL、status、Content-Type/Disposition/Encoding、64-bit received/total；
终态后 `GetHeader` 可按 occurrence 查询其他头，重复普通字段按原顺序保存。重复 framing
或下载 singleton 字段、TE/CL 冲突、非法 chunk 及超预算明确失败。Content-Disposition
只是元数据，不是已验证的文件名。Authorization/Cookie 可显式提供，但携带这些字段的
跨 origin 重定向在连接新目标前拒绝；不把凭据转发给不同服务器。

`received` 只计 sink 接受的 transfer-decoded bytes；已知 Content-Length 为 total，chunked
和 close-delimited 为 -1。首版只支持 identity Content-Encoding，其他编码在交付正文前返回
UNSUPPORTED，不把压缩字节误称解压数据。宿主显式选择 1 byte–1 GiB body quota；没有
默认无限模式。204/304 不交付正文；304 的 Content-Length 是表示 metadata，不消费正文预算。
HTTP 请求/头/块/wire 缓冲合计不到 48 KiB，不随 body 增长；另有
allocator、线程栈、TLS 和系统 socket 开销，不把固定缓冲预算冒充整进程峰值。
Sink、协议、预算、timeout、取消及 transport 错误有独立机器分类，保留底层 transport 分类。
失败可能已有部分 sink bytes，宿主须丢弃或明确标为未完成；OK 只表示传输完成，不代表
2xx、保存文件成功或页面提交。HTTPS 始终校验 chain/hostname，流式入口不受诊断用
`PHttp_SetInsecure` 影响，不降级、不默默回退；首版没有 Range、POST stream 或下载管理。

新 HTTP DLL 静态导入 `PTls_Transport*`，必须部署同一次正式 stage 的 HTTP/TLS DLL。
消费者不直接链接 Winsock/TLS 或解析英文错误；应用 worker/消息、文件命名及原子保存策略
仍由 EXE 拥有。实际门及未验证边界见 [当前交接](../.agents/HANDOFF.md)，不能把 DLL 门当成应用下载接线通过。

### 请求阶段观察（`Ex2`）

需要给标题栏或诊断面板显示真实网络阶段时，使用 `PHttp_GetEx2`、
`PHttp_GetUrlEx2`、`PHttp_PostEx2` 或 `PHttp_PostUrlEx2`，并传入一个
`PHttpObserver`。旧入口和 `NULL` observer 路径保持原有行为；observer 是每次请求的
配置，不是进程级全局对象，也不改变取消、重试、超时或导航策略。

```c
static void observe_http(const PHttpObserverEvent* event, void* user_data)
{
    (void)user_data;
    /* event->phase / event->status_code / event->received 可立即复制；
       event 及其中的字符串只借用到本次回调返回。 */
}

PHttpObserver observer;
PHttpResponse* response;

memset(&observer, 0, sizeof(observer));
observer.size = sizeof(observer);
observer.version = PHTTP_OBSERVER_VERSION;
observer.callback = observe_http;
observer.user_data = NULL;
response = PHttp_GetUrlEx2("https://example.com/", NULL, NULL, NULL,
                           &observer);
```

事件的 `scheme`、`host`、`port` 和 `redirect_hop` 表示当前实际 hop；所有文本都是
UTF-8 的固定容量快照。`status_code` 在响应头解析前为 `-1`，`received`/`total` 只在
`RECEIVING_BODY` 中有进度意义，`total == -1` 表示 chunked 或 close-delimited 的未知总量。
响应体进度是解码后的字节数，与旧 `PHttpProgressCallback` 相同。

| 阶段 | 语义 |
| --- | --- |
| `RESOLVING_NAME` | TLS 适配器开始解析 DNS 名称；IPv4 数值地址不会伪报此阶段。 |
| `CONNECTING` | 开始服务器连接。WM6 WinInet 将 DNS、代理选择和 socket 边界隐藏时只报告一次，并设置 `PHTTP_OBSERVER_FLAG_PHASE_MERGED`。 |
| `TLS_HANDSHAKE` | HTTPS socket 已连接，开始现有 mbedTLS 握手；证书链和 hostname 校验规则不变。HTTP 不产生此阶段。 |
| `SENDING_REQUEST` | 请求头以及可选 POST body 进入发送操作。 |
| `WAITING_RESPONSE` | 发送操作完成后等待响应；底层 provider 可能已合并部分等待。 |
| `RECEIVING_HEADERS` | 开始或完成响应头处理；可能先有未知状态的一次事件，再有带状态码的一次事件。 |
| `RECEIVING_BODY` | 读取并解码响应体；事件可以重复。 |
| `REDIRECTING` | 已接受当前 GET 的 Location，下一事件会属于新的 hop；HTTPS 降级仍拒绝。 |
| `COMPLETE` | HTTP 传输和 body 处理完成；404/500 等非 2xx 仍是 COMPLETE，由消费者解释状态码。 |
| `FAILED` | 传输、解析、容量、TLS 或重定向策略失败；`failure_phase` 是最近实际阶段，错误码和短消息为有界快照。 |

事件同步发生在调用线程，回调不得重入 HTTP/TLS、调用 `PHttp_Cleanup`、释放事件或把观察当作取消。
有效调用在函数返回前只产生一次终态（`COMPLETE` 或 `FAILED`），返回后不再回调；并发请求各自携带
自己的 `user_data`，不会串线。未能从 WinInet 分辨 DNS/TCP 的地方会明确合并，不通过延迟或错误字符串
推测阶段，也不暴露请求头、body 或敏感数据。

默认校验证书链和主机名。`PHttp_SetInsecure(TRUE)` 会对后续 HTTPS 请求关闭验证，只适合自签名诊断，不能作为生产默认值。当前连接采用短连接，响应体有设备侧上限；具体限制以 `positron_http.h` 为准。显式 `http://` 请求不经过 TLS，也不会因为端口不是 80 而被错误送入 TLS。

## 构建与验证

从仓库根目录运行 `scripts\build.bat Debug build`。修改 TLS/HTTP 边界后同时检查 `positron_tls` 的部署、错误路径和设备网络门；不要直接把 WinInet 或 Mbed TLS 对象暴露给业务项目。

带 observer 的新版 `positron_http.dll` 依赖同时导出 `PTls_ConnectEx`/
`PTls_ConnectVerifiedEx` 的匹配 `positron_tls.dll`；部署时必须从同一 Debug 或 Release 构建目录成对复制
这两个 DLL。只替换 HTTP DLL 而保留旧 TLS DLL 可能在加载阶段缺少导入符号。只使用旧 HTTP 入口的旧应用
仍可继续调用旧 `PHttp_*` ABI，但发布包不应混用不同构建批次的 DLL。

# Phase 2 — JSON + HTTP/1.1 Stack on Top of positron_tls

把 framework 的 stdlib 底层做扎实：把任意 WM6 app 都需要的 JSON 解析与 HTTPS 请求能力做成独立的两个 DLL。

---

## 目标与非目标

**目标**：
- `positron_json.dll`：cJSON 1.7.18 的 opaque-HANDLE 包装，让上层不需要 `#include "cJSON.h"`
- `positron_http.dll`：HTTP/1.1 客户端，跑在 `positron_tls.dll` 之上，支持 GET / POST / chunked Transfer-Encoding / Content-Length
- 重写 `test_host.exe`：4 步自检覆盖完整链路

**显式不做的**（留给后续 Phase）：
- 证书校验：仍 `MBEDTLS_SSL_VERIFY_NONE`，所以 HTTPS 连接当前**可被 MITM**
- HTTP keep-alive / connection pool / pipelining
- 重定向跟随（3xx 自动 follow）
- HTTP/2、HTTP/3
- 进度回调 / 分块上传 / 流式响应
- 多线程并发请求

---

## 公共 API

### positron_json.h

```c
HANDLE      PJson_Parse        (const char* json_string);
void        PJson_Free         (HANDLE hObj);
const char* PJson_GetString    (HANDLE hObj, const char* key);
int         PJson_GetInt       (HANDLE hObj, const char* key);
HANDLE      PJson_GetObject    (HANDLE hObj, const char* key);
HANDLE      PJson_GetArrayItem (HANDLE hObj, int index);
int         PJson_GetArraySize (HANDLE hObj);
char*       PJson_Serialize    (HANDLE hObj);
void        PJson_FreeString   (char* str);
```

生命周期约定：
- `PJson_Parse` 返回的 **顶层 HANDLE 必须** 用 `PJson_Free` 释放
- `PJson_GetObject` / `PJson_GetArrayItem` 返回**子节点 HANDLE，不要单独 free**——它们的生命周期随父节点
- `PJson_Serialize` 返回的字符串用 `PJson_FreeString`（cJSON 的 `cJSON_free`，跟我们自己的 heap 实现可能不同）

### positron_http.h

```c
typedef struct PHttpResponse {
    int    status_code;
    char*  body;
    int    body_len;
    char   error_msg[256];
} PHttpResponse;

BOOL            PHttp_Init        (void);
void            PHttp_Cleanup     (void);
PHttpResponse*  PHttp_Get         (host, port, path, headers);
PHttpResponse*  PHttp_Post        (host, port, path, headers, body, body_len);
void            PHttp_FreeResponse(PHttpResponse* resp);
```

`PHttp_Get` / `PHttp_Post` **永远返回 non-NULL**（除非 OOM）。transport 失败时 `status_code == 0` 且 `error_msg` 非空。

---

## 4 步测试 (test_host)

| 测试 | 内容 | 验证 |
|---|---|---|
| 1 | `PTls_Init` + `PJson_Parse("{}")` + `PHttp_Init` | 3 个 DLL 都加载、关键导出函数地址可解析 |
| 2 | `PJson_Parse("{\"key\":\"value\",\"num\":42}")` 提字段 | JSON 解析与读取正确 |
| 3 | `GET https://api.ipify.org/?format=json` | TLS+HTTP+chunked+JSON 完整链路（GET） |
| 4 | `POST https://httpbin.org/post`，body `{"hello":"positron"}` | POST + Content-Length + JSON 嵌套对象提取 |

Test 4 在 `response.json.hello == "positron"` 时通过——证明请求体确实送到了服务器、被回声、被我们重新解析。

---

## 关键决策

### 直接链接 `positron_tls.lib`，而非 `LoadLibrary`/`GetProcAddress`

最初规划里建议 positron_http 用 `LoadLibraryW(L"positron_tls.dll")` + 一堆 `GetProcAddress` 做延迟加载，目的是"避免部署顺序问题"。

我们的部署是**手动共享文件夹**（VS deploy 已废，详见 PHASE1.md），所有 DLL 一起被搬到设备，没有任何顺序问题。直接链接 `.lib` 让代码少 150 行胶水，且让缺失 DLL 时 process 直接 fail-fast，便于诊断。

### Test 4 用 httpbin 而非 Claude API

最初设计是用 Claude API + 硬编码 `TEST_API_KEY`，提取 `content[0].text` 显示。但 Positron 已从 chatbox 升级为 framework，"必须能调通 Claude API" 不再是核心指标。改用 **httpbin.org/post** 这样无 auth、行为可预测、不依赖任何账号的公共 echo 服务，更适合 stdlib 测试。

副作用收益：
- 不需要 secret key 管理（`.gitignore` 里也不需要排 `secret_apikey.h`）
- 仓库可以公开 push，不怕泄露
- 任何人 clone 后都能直接跑测试

### `MBEDTLS_ERR_NET_*` 在 positron_tls.c 本地补宏

Phase 1 已经做过的决定：因为 `MBEDTLS_NET_C` 整模块被禁掉（WinCE 没 `getaddrinfo`），那两个 error code 不会被 mbedTLS 编出来。我们在 positron_tls.c 顶部按 mbedTLS 2.x 原值本地 `#define`。Phase 2 没动这一点。

### Accept-Encoding: identity

`positron_http.c` 的 `build_request` 默认带 `Accept-Encoding: identity`，明确告知服务器**不要发** gzip/deflate/brotli。Phase 2 没有压缩解码器，万一服务器忽略 identity 给压缩内容会破。Phase 3+ 加 zlib 之后再去掉这个头。

### `Connection: close`

Phase 2 每个 HTTP 调用建一条新 TLS 连接、用完关掉。简化了响应体读取（"读到 EOF"等于"读到末尾"），代价是每次请求多一次握手。Phase 3 加 keep-alive 时改。

---

## 工程结构变化

新增：
- `positron_json/` —— DLL 工程 + cJSON 1.7.18 源（`cjson/`）
- `positron_http/` —— DLL 工程
- `compat/stdint.h`、`compat/inttypes.h` —— Phase 1 从 `positron_tls/` 移过来共享
- `scripts/stage.bat` —— 一键把 4 个二进制拷到 `C:\WMShare\`

修改：
- `Positron.sln` —— 登记 `positron_http`，更新 test_host 的 ProjectDependencies 涵盖三个 DLL
- `test_host/test_host.vcproj` —— include path / lib deps / deploy AdditionalFiles 涵盖三个 DLL
- `test_host/main.c` —— 完全重写为 4-step 测试

---

## 构建

打开 `Positron.sln` → 顶部确认 Debug + WM6 Pro ARMV4I → 生成 → 生成解决方案 (F7)。

产物：
```
positron_tls/bin/Debug/positron_tls.dll
positron_json/bin/Debug/positron_json.dll
positron_http/bin/Debug/positron_http.dll
test_host/bin/Debug/test_host.exe
```

## 部署 + 跑

VS 的 deploy 链路 Phase 1 起就不可用，沿用共享文件夹方案：

```cmd
scripts\stage.bat        :: 默认 Debug
scripts\stage.bat Release
```

把四个文件拷到 `C:\WMShare\`，模拟器里 Storage Card 双击 `test_host.exe`。

预期：依次弹 4 个 TEST OK 框，最后一个 "All tests passed"。

---

## 已知风险点

- **httpbin.org 偶尔挂**。如果 Test 4 报 "PTls_Connect failed (gethostbyname...)"，先在模拟器 IE 里访问 `https://httpbin.org/post` 看服务是否在线。备选 echo 端点：`https://postman-echo.com/post`。
- **chunked 解码器内存占用**：响应体目前硬 cap 在 1 MB，cap 之上直接报错。这对 stdlib 而言够用，将来 Phase 3 流式 API 再放宽。
- **ipify 偶尔限速**。我们仅做一次请求，正常情况下不会被限。
- **HTTP cert hostname**：`MBEDTLS_SSL_VERIFY_NONE` 仍然在用。Phase 3 嵌入 CA bundle 之后会强制 hostname + chain 校验。
- **WMDC 网络共享会静默断**：模拟器跑久了 / host 待机后，WMDC 可能卡住不报错，但 emulator 失去网络。表现是 `PTls_Connect` 出 `-0x004C / BIO: recv WSA=...`。修法：宿主机重启 WMDC（任务栏图标右键 → 退出 → 重新启动 Windows Mobile 设备中心），重新挂上后立刻就好。运行测试前先用 IE Mobile 打开任意 HTTPS 站点验证一下。

---

## 验证状态

2026-05-28 在 WM6 Pro Emulator 上跑通 4 步全套，含 ipify GET 和 httpbin POST 真实网络往返。第三方依赖：mbedTLS 2.16.12、cJSON 1.7.18、WMDC bridge。

---

## Phase 3 / 4 候选清单

| 任务 | 复杂度 | 优先级 |
|---|---|---|
| 嵌入 Mozilla CA bundle + 启用 `MBEDTLS_X509_USE_C` 全套链验证 | 中 | 高（安全） |
| `CryptGenRandom` 替换熵源 jitter | 低 | 中 |
| keep-alive 连接池 | 中 | 中 |
| 3xx 重定向 follow | 低 | 中 |
| gzip 解码（嵌入 miniz / zlib） | 中 | 低 |
| `positron_core.dll` 初步：app event loop、widget 抽象、HTML 渲染入口（封 IE Mobile WebBrowser ActiveX） | 高 | 高（framework 价值） |
| Per-thread `g_last_error` via `TlsAlloc` | 低 | 低 |

# Phase 3 — Verified TLS

让 `positron_tls.dll` 第一次提供可信任的连接。Phase 1/2 都跑在 `MBEDTLS_SSL_VERIFY_NONE` 之下，任何中间人都能伪装成 ipify / httpbin。Phase 3 嵌入一份 CA bundle、加上链验证 + hostname 校验、把熵源从 jitter 升到 CryptGenRandom，并让 `positron_http` 默认走 verified 路径。

---

## 目标与非目标

**目标**：
- 嵌入 5 根 Mozilla CA 证书到 `positron_tls.dll`（~7 KB PEM）
- 新增 `PTls_ConnectVerified(host, port)`——完整 chain + hostname 校验
- 新增 `PTls_AddRootCA(pem)`——运行时追加企业内网根
- 保留 `PTls_Connect`（VERIFY_NONE）做自签 / 诊断用途
- `mbedtls_hardware_poll` 升级到 `CryptGenRandom`（acquire-once，jitter 兜底）
- `positron_http` 默认 verified，加 `PHttp_SetInsecure(BOOL)` 给特殊场景留口子
- `test_host` 加 TEST 5：badssl.com 正负样本三连测

**显式不做的**（留给后续 Phase）：
- 完整 Mozilla bundle（200 KB+ 在 WM6 上太重）
- 自动证书 pinning / cert transparency
- TLS 1.3（mbedTLS 2.16 不带，且本项目用不到）
- HTTP keep-alive / redirect follow / gzip（Phase 4+ 候选）
- WebBrowser ActiveX（Phase 4）

---

## 关键决策

### 不嵌入全量 Mozilla bundle，挑 5 根

完整 cacert.pem ~200 KB；嵌进 DLL 太重。挑的根基于实际探测各测试端点：

| 根证书 | 用途 |
|---|---|
| GTS Root R4 | `api.ipify.org`、`api.anthropic.com`（Google Trust Services） |
| Amazon Root CA 1 | `httpbin.org`（AWS-fronted） |
| ISRG Root X1 | Let's Encrypt 全家，含 `badssl.com` |
| DigiCert Global Root G2 | 防御性，仍服务大量 SaaS |
| USERTrust RSA Certification Authority | 防御性，Sectigo 旧路径、企业内 CA 常见 |

约 7 KB PEM。当新增端点用了不认识的根，重跑 `positron_tls/gen_ca_bundle.py` 更新即可。

`PTls_AddRootCA` 提供运行时口子：企业内网自签 CA、LocalSend 跨设备自签证书都可以走这里，无需重编。

### 必须开 `MBEDTLS_PLATFORM_TIME_ALT` + `GMTIME_R_ALT`

最初赌"WinCE 5 coredll 有 `time()`，直接用"——输了。链接时报：
- `unresolved external symbol time`（被 `ssl_cli.c` 和 `x509.c` 引用）
- `unresolved external symbol gmtime_s`（被 `platform_util.c` 引用）
- `unresolved external symbol GetSystemTimeAsFileTime`（这个是桌面 Win32，WinCE 也没有）

WinCE 5 coredll 缺的不止这些 ANSI 变体，连基础 C 时间函数都不全。最终方案：
- `MBEDTLS_PLATFORM_TIME_ALT` + `MBEDTLS_PLATFORM_STD_TIME = positron_time`——后者让 mbedTLS 静态初始化函数指针时就捕获我们的函数，避免引用 libc `time`
- `MBEDTLS_PLATFORM_GMTIME_R_ALT`——符号直接替换 `mbedtls_platform_gmtime_r`
- `positron_time` 用 `GetSystemTime` + `SystemTimeToFileTime` 算 Unix 秒（不能用 `GetSystemTimeAsFileTime`）
- `mbedtls_platform_gmtime_r` 反向用 `FileTimeToSystemTime` 填 `struct tm`

教训：WinCE 5 coredll 的缺失 pattern——一旦看见 mbedTLS / 任何依赖 libc 的代码报 unresolved external，**默认假设是 coredll 没那个符号**，写 shim 顶替。

### `CryptGenRandom` provider 全进程持有

`PTls_Init` 时 `CryptAcquireContextW(PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)` 拿一个，`PTls_Cleanup` 时还。`mbedtls_hardware_poll` 直接用这个 provider，不再 per-call acquire/release。

`CRYPT_VERIFYCONTEXT` = 不需要持久 key container；`CRYPT_SILENT` = WM6 上某些 CSP 配置会弹 UI，禁掉。

如果 acquire 失败（CSP 不可用），`g_crypt_prov` 留 0，hardware_poll 退回 Phase 1/2 的 QPC + GetTickCount + tid/pid jitter 路径。不是 fatal——CTR_DRBG 在 jitter 上能跑。

### `positron_http` 默认 verified，老路径要显式 opt-in

`PHttp_Get / PHttp_Post` 默认走 `PTls_ConnectVerified`。需要绕过的场景（比如 LocalSend 自签）调一次 `PHttp_SetInsecure(TRUE)` 翻成 VERIFY_NONE 路径。返回前一个值，便于调用方 push/pop。

副作用：Phase 2 的 Test 3 / 4（ipify、httpbin）现在自动跑在 verified 路径上，等于已有用例顺带升级。

### 直接复用 mbedtls verify_info，不写自己的解释器

握手失败且 verify_mode 是 REQUIRED 时，调 `mbedtls_ssl_get_verify_result` 取 flags，再 `mbedtls_x509_crt_verify_info` 渲染成人类可读的多行字符串，写进 `g_last_error`。mbedTLS 已经把 "CN mismatch"、"cert has expired"、"cert is not trusted" 这些情况描述好了，没必要自己解析 flags 位。

---

## API 变更

### positron_tls.h

```c
/* 新增 */
PTLS_API BOOL   PTls_AddRootCA(const char* pem);
PTLS_API HANDLE PTls_ConnectVerified(const char* host, int port);

/* 不变 */
PTLS_API BOOL   PTls_Init(void);
PTLS_API void   PTls_Cleanup(void);
PTLS_API HANDLE PTls_Connect(const char* host, int port);
PTLS_API int    PTls_Write(...);
PTLS_API int    PTls_Read(...);
PTLS_API void   PTls_Close(...);
PTLS_API const char* PTls_LastError(void);
```

### positron_http.h

```c
/* 新增 */
PHTTP_API BOOL PHttp_SetInsecure(BOOL insecure);
```

`PHttp_Get / PHttp_Post` 签名不变，但**默认行为**改为 verified。这是 Phase 2 → Phase 3 的隐式行为升级；如果某个老调用方依赖 VERIFY_NONE，需要显式 `PHttp_SetInsecure(TRUE)`。

---

## TEST 5（test_host 第 5 步）

跑完 Phase 2 的 4 步后，新加 TEST 5 verified TLS：

| 子步 | 目标 | 期望 |
|---|---|---|
| A | `PTls_ConnectVerified("badssl.com", 443)` | 成功（valid LE 证书） |
| B | `PTls_ConnectVerified("expired.badssl.com", 443)` | 失败，错误包含 "expired" 字样 |
| C | `PTls_ConnectVerified("self-signed.badssl.com", 443)` | 失败，错误包含 "not trusted" 类信息 |

三个子步结果合并到一个 MessageBox 显示。badssl.com 是专为 TLS 行为测试设立的服务，状态稳定。

**前置条件**：在跑 TEST 5 之前**必须先在模拟器里把系统时钟设到当前**。WM6 Emulator 默认是 2005 或 2007 年某个时间，会让所有现役证书都看着像"尚未生效"——TEST 5A 会无谓地失败。设法：
- 开始 → 设置 → 系统 → 时钟和闹钟（Clock & Alarms）→ 设到今天

---

## 工程结构变化

新增：
- `positron_tls/gen_ca_bundle.py` —— Python 脚本，从 curl.se 拉 cacert.pem，提 5 根写出 ca_bundle.h
- `positron_tls/ca_bundle.h` —— 脚本输出，5 个 PEM 块拼成的 C 字符串字面量

修改：
- `positron_tls/mbedtls_config.h` —— 开启 `MBEDTLS_HAVE_TIME` / `HAVE_TIME_DATE` / `X509_CHECK_KEY_USAGE` / `X509_CHECK_EXTENDED_KEY_USAGE` / `PEM_PARSE_C` / `BASE64_C`
- `positron_tls/positron_tls.h` —— 加 `PTls_ConnectVerified` / `PTls_AddRootCA`
- `positron_tls/positron_tls.c` —— g_cacert / g_crypt_prov 全进程持有；hardware_poll 升级；连接逻辑抽 internal helper
- `positron_http/positron_http.h` —— 加 `PHttp_SetInsecure`
- `positron_http/positron_http.c` —— 默认 verified
- `test_host/main.c` —— 加 TEST 5

无新 DLL，vcproj 已经把所有 mbedTLS 源（含 x509_crt.c / pem.c / base64.c）都列了，不用动 vcproj 文件清单。

---

## 构建 + 运行

```cmd
:: 生成 / 刷新 CA bundle（一次性，或证书过期时再跑）
python positron_tls\gen_ca_bundle.py

:: 构建（VS2008 内 F7）
:: stage
scripts\stage.bat

:: 模拟器内 Storage Card → test_host.exe
```

预期 6 个对话框：TEST 1 OK → TEST 2 OK → TEST 3 OK（ipify，现在走 verified）→ TEST 4 OK（httpbin，verified）→ TEST 5 OK（badssl 正负三测）→ All tests passed。

---

## 已知风险点

- **模拟器时钟必须先校正**。设置 → Clock & Alarms。否则 TEST 5A 会无脑失败、TEST 3/4 也可能受影响。
- **curl.se cacert.pem 漂移**。如果将来更新脚本时某个 WANTED 根被 Mozilla 移除，脚本会直接报错——人工换个等价根即可。
- **CryptAcquireContext 在某些 WM6 image 上不存在 PROV_RSA_FULL**。我们 fallback 到 jitter 路径，不会 fail-fast；但 hardware_poll 质量降级（CTR_DRBG 仍可用）。
- **mbedTLS verify_info 字符串是英文**。MessageBox 显示时不会本地化。
- **badssl.com 偶尔抖动**。如果 TEST 5 任何子步因网络问题失败，先确认 IE Mobile 能打开 https://badssl.com。

---

## Phase 4 候选

| 任务 | 复杂度 | 优先级 |
|---|---|---|
| `positron_core.dll` 起步：WebBrowser ActiveX 封装、JS↔Native bridge | 高 | 高（framework 价值） |
| 之前要先做 capability spike：~100 行 minimal WebBrowser test 探一遍 IE Mobile 真实 DOM/JS 支持范围 | 低 | 高（决定 Phase 4 可行性） |
| HTTP keep-alive 连接池 | 中 | 中 |
| 3xx 重定向 follow | 低 | 中 |
| gzip / deflate 解码（嵌入 miniz） | 中 | 低 |
| Per-thread `g_last_error` via TlsAlloc | 低 | 低 |

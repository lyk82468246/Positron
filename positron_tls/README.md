# `positron_tls`

`positron_tls.dll` 是 Windows Mobile 6 / Windows CE 5.2 上可独立集成的 TLS 1.2
公共基础设施。ABI v2 同时覆盖两种不同的信任模型：

- 普通 HTTPS 客户端：使用内置或调用方追加的 CA，校验证书链和 DNS 主机名；
- 局域网 peer：持久保存自签名设备身份，客户端发送证书，服务端接受 TLS，调用方以
  证书 DER 的 SHA-256 指纹识别对端。

Mbed TLS 静态链接进 DLL。消费者只需要公共头、导入库和 `positron_tls.dll`，不需要
包含 Mbed TLS 头文件，也不需要部署另一个 Mbed TLS DLL。

## 输出和兼容性

- 工程：`positron_tls.vcproj`
- 公共头：`positron_tls.h`
- Debug 输出：`bin\Debug\positron_tls.dll` 和 `positron_tls.lib`
- Release 输出：`bin\Release\positron_tls.dll` 和 `positron_tls.lib`
- ABI 查询：`PTls_GetAbiVersion()`，当前返回 `PTLS_ABI_VERSION`（2）

ABI v1 的 `PTls_Init`、`PTls_Cleanup`、`PTls_AddRootCA`、`PTls_Connect`、
`PTls_ConnectVerified`、`PTls_Read`、`PTls_Write`、`PTls_Close` 和
`PTls_LastError` 均保留。旧程序不必重新编译；若程序也允许加载旧 DLL，应先用
`GetProcAddress` 探测 `PTls_GetAbiVersion`，不能假设旧 DLL 存在这个符号。

所有公共字符串和文件路径都是 UTF-8，句柄是不透明值。谁创建句柄，谁就必须用对应
的 `PTls_*Close` 释放，不能跨 DLL/CRT 直接释放内部内存。

## 普通 CA HTTPS 客户端

产品代码应使用 `PTls_ConnectVerified`。`PTls_Connect` 不验证证书，只保留给明确的
诊断或兼容场景，不能作为互联网请求的默认路径。

```c
#include "positron_tls.h"

HANDLE connection;
char response[1024];
char error[256];
int count;

if (!PTls_Init()) {
    return 1;
}
connection = PTls_ConnectVerified("example.com", 443);
if (connection == NULL) {
    PTls_CopyLastError(error, sizeof(error));
    PTls_Cleanup();
    return 1;
}
PTls_Write(connection,
           "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n",
           56);
count = PTls_Read(connection, response, sizeof(response));
PTls_Close(connection);
PTls_Cleanup();
```

`PTls_AddRootCA` 接受 NUL 结尾 PEM，并修改进程级 CA 链；必须在并发连接开始前调用。
该 DLL 提供 TLS 字节流，不负责 HTTP 解析、重定向、cookie、页面导航或业务授权。

## 持久 peer 身份

`PTls_IdentityLoadOrCreate(cert_path, key_path)` 的规则是确定的：

1. 两个文件都存在：解析 PEM 并验证证书/私钥匹配；
2. 两个文件都不存在：生成 ECDSA P-256、SHA-256 的自签名证书和私钥，先完整写入同
   目录临时文件并刷新，再提交目标文件；
3. 只存在一个文件、内容损坏或密钥不匹配：明确失败，不覆盖现有文件。

生成的证书同时可作 TLS client/server 证书。`PTls_IdentityFingerprint` 返回叶证书
DER 的 SHA-256：64 个大写十六进制字符和 NUL，因此输出容量至少为
`PTLS_FINGERPRINT_HEX_CAPACITY`（65）。重新加载同一文件对会得到同一指纹。

私钥是未加密 PEM。WM6 没有由本 DLL 提供的密钥库或 ACL 抽象；应用必须把身份文件
放在其可保护的持久目录，避免共享存储、备份泄露和其他进程读取。复制、重置或删除
身份文件都会改变设备身份，这应由上层配对/迁移策略处理。

生成身份依赖可信设备时钟；当前实现接受 2020–2037 的 UTC，并将证书有效期限制在
2037 年末以内。已有身份仍会由 TLS 校验其有效期。

## peer 客户端和服务端

下面是最小的双向证书、指纹钉扎流程。实际程序通常在独立线程里阻塞等待
`PTls_ServerAccept`。

```c
HANDLE identity;
HANDLE listener;
HANDLE incoming;
HANDLE outgoing;
char own_fingerprint[PTLS_FINGERPRINT_HEX_CAPACITY];
char remote_ip[16];
int remote_port;

PTls_Init();
identity = PTls_IdentityLoadOrCreate("\\Application Data\\MyApp\\cert.pem",
                                     "\\Application Data\\MyApp\\key.pem");
PTls_IdentityFingerprint(identity, own_fingerprint,
                         sizeof(own_fingerprint));

listener = PTls_ServerListen(identity, 53317,
                             PTLS_SERVER_REQUIRE_CLIENT_CERT, 10000);
incoming = PTls_ServerAccept(listener, remote_ip, sizeof(remote_ip),
                             &remote_port);

outgoing = PTls_ConnectPeer("192.168.1.20", 53317, identity,
                            "64_HEX_DIGITS_FROM_PAIRING", 10000);

PTls_Close(outgoing);
PTls_Close(incoming);
PTls_ServerClose(listener);
PTls_IdentityClose(identity);
PTls_Cleanup();
```

服务端规则：

- `PTLS_SERVER_REQUIRE_CLIENT_CERT` 要求客户端提供证书；适合已知的设备协议；
- flags 为 0 时，未提供证书的客户端也可完成握手；若客户端提供了证书，仍校验证书
  的结构、时间和算法。这适合明确允许普通浏览器访问的入口；
- listener 不拥有上层配对数据库。强制客户端证书只证明对端持有该证书的私钥；accept
  返回后，服务端仍须先读取 `PTls_PeerFingerprint`、与已配对记录比较，再处理协议数据；
- 一个 listener 同时只允许一个进行中的 `PTls_ServerAccept`；已接受的连接彼此独立；
- `PTls_ServerClose` 会中断正在阻塞的 accept/handshake，等待该调用退出，再释放
  listener；此前已返回的连接继续有效。

peer 客户端规则：

- 本地 identity 作为客户端证书发送；
- 非空 expected fingerprint 必须正好是 64 个十六进制字符，比较不区分输入大小写；
- 指纹在 `PTls_ConnectPeer` 返回连接、应用发送协议数据以前完成校验；
- NULL 或空指纹只适合 discovery/首次配对（TOFU），它能建立加密通道，却不能证明回答
  者是哪台设备；上层必须展示、交换或确认 `PTls_PeerFingerprint` 的结果后才能信任；
- peer 模式按证书指纹标识设备，不按 IP 地址或 DNS 名称标识，因此不会做 hostname
  匹配；除自签名“不受信任”外，证书过期、尚未生效和不安全算法等错误不会被忽略。

`timeout_ms` 覆盖 TCP connect 和 TLS handshake。WinCE 5.2 的同步
`gethostbyname` 不能可靠取消，所以 DNS 调用本身可能在期限外才返回；返回后仍会检查剩余
时间。监听和 peer 传输当前只支持 IPv4。

### LocalSend/rustls 兼容性边界

截至 2026-08-23 的上游源码核对显示，官方 LocalSend core 使用 rustls 0.23.43 的 `ring` 与
`tls12` feature，并安装 ring default provider；该 provider 默认包含
`TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256`，也支持 `ECDSA_NISTP256_SHA256`。Positron
明确启用了同一 TLS 1.2 suite 所需的 ECDHE-ECDSA、P-256、SHA-256、AES-128-GCM，生成证书
不写限制性 EKU，因此两端存在明确的算法交集。参考
[LocalSend core 依赖](https://github.com/localsend/localsend/blob/main/packages/core/Cargo.toml)、
[LocalSend client TLS 配置](https://github.com/localsend/localsend/blob/main/packages/core/src/http/client/mod.rs)
和 [rustls ring provider](https://docs.rs/rustls/latest/src/rustls/crypto/ring/mod.rs.html)。

这只是上游源码/配置兼容性验证，不是实际 LocalSend↔WM6 跨栈互操作测试。本仓库设备门使用
两个 Positron endpoint 完成真实 WM6 loopback；具体消费者在宣称端到端兼容前，仍应运行
rustls client→Positron server 和 Positron client→rustls server 两个方向的集成测试。

## 生命周期、线程和错误

推荐的销毁顺序是：所有 connection → listener → identity → `PTls_Cleanup`。

- `PTls_Init`/`PTls_Cleanup` 幂等但不引用计数；同一进程应由一个明确所有者配对调用；
- identity 必须活得比使用它的 listener 和 peer connection 更久；
- 不同 connection 可以在不同线程使用；同一 connection 的并发读写顺序由调用方串行化；
- 同一 listener 只允许一个 active accept；另一个线程可以调用 `PTls_ServerClose` 取消它；
- 不能在仍有 TLS 对象时调用 `PTls_Cleanup`。

`PTls_LastError` 是为 ABI v1 保留的进程级借用指针，并发时不安全。新代码应在失败后立即
调用 `PTls_CopyLastError`，把受锁保护的最新错误快照复制到调用方缓冲。错误槽仍是整个
进程共享的，快照安全不等于错误自动归属于某个线程。

## C# / .NET Compact Framework P/Invoke

.NET Compact Framework 不应依赖系统 ANSI 编码代替 UTF-8。将输入显式编码成带 NUL 的
`byte[]`，并把 opaque handle 映射为 `IntPtr`：

```csharp
using System;
using System.Runtime.InteropServices;
using System.Text;

static class PTls
{
    [DllImport("positron_tls.dll", EntryPoint="PTls_GetAbiVersion")]
    internal static extern int GetAbiVersion();

    [DllImport("positron_tls.dll", EntryPoint="PTls_Init")]
    internal static extern int Init();

    [DllImport("positron_tls.dll", EntryPoint="PTls_Cleanup")]
    internal static extern void Cleanup();

    [DllImport("positron_tls.dll", EntryPoint="PTls_IdentityLoadOrCreate")]
    internal static extern IntPtr IdentityLoadOrCreate(byte[] cert, byte[] key);

    [DllImport("positron_tls.dll", EntryPoint="PTls_ConnectPeer")]
    internal static extern IntPtr ConnectPeer(byte[] host, int port,
        IntPtr identity, byte[] fingerprint, int timeoutMs);

    [DllImport("positron_tls.dll", EntryPoint="PTls_Close")]
    internal static extern void Close(IntPtr connection);

    [DllImport("positron_tls.dll", EntryPoint="PTls_IdentityClose")]
    internal static extern void IdentityClose(IntPtr identity);

    internal static byte[] Utf8Z(string value)
    {
        return Encoding.UTF8.GetBytes(value + "\0");
    }
}

// GetAbiVersion 的入口不存在时捕获 EntryPointNotFoundException，并提示部署 ABI v2 DLL。
if (PTls.GetAbiVersion() < 2 || PTls.Init() == 0)
    throw new InvalidOperationException("Positron TLS ABI v2 is unavailable");

IntPtr identity = PTls.IdentityLoadOrCreate(
    PTls.Utf8Z("\\Application Data\\MyApp\\cert.pem"),
    PTls.Utf8Z("\\Application Data\\MyApp\\key.pem"));
string knownFingerprint =
    "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
// 上面的示例值必须替换为发现/配对记录中的真实证书指纹。
IntPtr connection = PTls.ConnectPeer(
    PTls.Utf8Z("192.168.1.20"), 53317, identity,
    PTls.Utf8Z(knownFingerprint), 10000);

// 按实际控制流放入 try/finally；这里仅展示所有权顺序。
PTls.Close(connection);
PTls.IdentityClose(identity);
PTls.Cleanup();
```

示例把 Win32 `BOOL` 声明成 4-byte `int`，避免 Compact Framework 的布尔 marshaling 差异。
输出文本可传固定 `byte[]` 缓冲，再从第一个 NUL 前按 UTF-8 解码。生产代码必须检查每个
句柄是否为 `IntPtr.Zero`，并在 `finally` 中只释放非零句柄。

## 构建和验证

只使用仓库正式工程配置：

```bat
scripts\build.bat Debug
scripts\build.bat Release
python scripts\test_c89ize.py
python scripts\audit_repo.py
```

TLS peer 回归由 `test_host` 的自动设备门覆盖身份创建/重载、指纹、损坏组合、双向证书、
钉扎失败、可选/强制客户端证书、并发连接、listener 恢复和关闭取消。第三方版本、许可证
和仍存在的安全基线限制见根目录 `THIRD_PARTY.md` 与 `.agents/KNOWN_LIMITATIONS.md`。

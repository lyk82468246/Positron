# `positron_tls`

`positron_tls.dll` 是面向 Windows Mobile 6 / Windows CE 5.2 的 TLS 1.2
客户端公共 DLL。它把 Mbed TLS、嵌入式 CA bundle、主机名校验和 WM6
套接字封装在稳定的 C ABI 后面；调用者不需要包含 Mbed TLS 头文件。

## 输出与依赖

- 工程：`positron_tls.vcproj`
- 输出：`bin\Debug\positron_tls.dll`、对应 `.lib`
- 公共头：`positron_tls.h`
- 主要依赖：仓库固定的 `mbedtls/`；不依赖其他 Positron DLL

把 `positron_tls.lib` 加入应用链接，把 `positron_tls.dll` 放在应用可搜索的
目录中。正式工程必须使用 VS2008 的
`Debug|Windows Mobile 6 Professional SDK (ARMV4I)` 或对应 Release 配置。

## 其他项目如何调用

所有字符串都是 UTF-8。进程启动阶段初始化一次，连接完成后成对清理：

```c
#include "positron_tls.h"

HANDLE conn;
const char request[] =
        "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
char buffer[1024];
int n;

if (!PTls_Init()) {
    /* PTls_LastError() 是借用字符串，只读到下一次 API 调用前。 */
    return 1;
}
conn = PTls_ConnectVerified("example.com", 443);
if (conn != NULL) {
    PTls_Write(conn, request, sizeof(request) - 1);
    n = PTls_Read(conn, buffer, sizeof(buffer));
    PTls_Close(conn);
}
PTls_Cleanup();
```

常用入口：

- `PTls_ConnectVerified`：验证 CA 链和证书主机名，产品路径应优先使用；
- `PTls_Connect`：跳过证书验证，仅用于自签名诊断；
- `PTls_AddRootCA`：在任何连接开始前追加 PEM 根证书；
- `PTls_Read` / `PTls_Write` / `PTls_Close`：进行字节流收发和释放。

连接句柄必须由 `PTls_Close` 释放。追加根证书会修改进程级信任链，不是线程安全
操作；不能在并发连接开始后调用。该 DLL 只提供 TLS 字节流，不负责 HTTP、证书
业务策略或页面导航。

## 构建与验证

从仓库根目录运行 `scripts\build.bat Debug build`。修改 Mbed TLS、CA bundle 或
公共 ABI 后，应运行 `python scripts\audit_repo.py`，并按项目风险运行正式 ARMV4I
构建和设备门。版本与许可证见 `UPSTREAM.md`（如有）及根目录 `THIRD_PARTY.md`。

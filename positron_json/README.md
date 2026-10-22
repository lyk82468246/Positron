# `positron_json`

`positron_json.dll` 是 Positron 的 UTF-8 JSON 公共边界。它把固定版本 cJSON 封装成 opaque `HANDLE` C ABI，避免应用直接依赖 cJSON 对象布局或跨 CRT 释放内存。

## 输出与依赖

- 工程：`positron_json.vcproj`
- 输出：`bin\Debug\positron_json.dll`、对应 `.lib`
- 公共头：`positron_json.h`
- 实现：目录内固定版本的 `cjson/`

应用链接 `positron_json.lib`，运行时部署 `positron_json.dll`。所有 JSON 输入输出均为 UTF-8；该 DLL 不负责网络、DOM、schema 校验或 JavaScript 执行。

## 其他项目如何调用

顶层解析句柄由调用者释放；对象/数组子句柄只是顶层树的借用视图：

```c
#include "positron_json.h"

HANDLE root;
HANDLE user;
const char *name;
char *encoded;

root = PJson_Parse("{\"user\":{\"name\":\"Ada\"},\"age\":37}");
if (root == NULL) {
    return 1;
}
user = PJson_GetObject(root, "user");       /* 不要单独 PJson_Free。 */
name = PJson_GetString(user, "name");       /* 指针借用 root 的生命周期。 */
encoded = PJson_Serialize(root);
if (encoded != NULL) {
    PJson_FreeString(encoded);
}
PJson_Free(root);
```

`PJson_GetInt`、`PJson_GetObject`、`PJson_GetArrayItem` 和 `PJson_GetArraySize` 用于读取树；序列化结果必须用 `PJson_FreeString` 释放，不能使用应用自己的 `free`。缺失字段和类型不匹配按头文件约定返回空指针或零，需要业务层自行区分“缺失”和“合法零值”。

## 有界严格解析

安全敏感的配置/manifest 消费者使用 additive `PJson_ParseEx`，旧 `PJson_Parse` 行为不变。
调用方初始化 `PJsonParseLimits.size/version`，显式给出输入字节数，以及 `max_bytes`
（1–65536）、`max_depth`（1–16 个容器层级）、`max_tokens`（1–1024，含键名/容器）。
预检在 cJSON 分配树前限制深度和词元，实际语法/转义仍由同一 cJSON 解析。

新入口拒绝 BOM、尾随内容、非法 UTF-8、原始/解码后 NUL、重复键（含转义等价键）、
非有限数字和非法控制字符；不会改变全局 allocator hooks。成功根仍由 `PJson_Free` 释放，
失败不修改输出句柄。`PJSON_PARSE_INVALID` 也包含上游无法区分的 parser 分配失败，
不能把它解释为系统内存一定充足。ARGUMENT 与 LIMIT 分别表示 API 契约和固定容量错误。
新 `PJson_GetStringValue` 只借用字符串节点值，便于读取数组中的字符串；生命周期仍跟随根。
schema、版本取舍和权限声明不属于 JSON DLL，Runtime 消费者合同见
[Runtime README](../positron_runtime/README.md)。

## 有界标量编码

`PJson_EncodeScalar` 为服务协议提供通用标量编码，不要求消费者复制 JSON 转义器。
初始化 `PJsonScalar.size/version`，类型选择 NULL、FALSE、TRUE、NUMBER 或 STRING。
数字必须有限；字符串使用显式 UTF-8 长度，最多 4096 字节，不接受 NUL，空字符串可使用 NULL/0。
字符转义和有限实数格式由固定 cJSON 实现；int64 应先保留为十进制字符串，而不是转成 double。

`buffer=NULL, capacity=0` 是无副作用的成功 size probe，`written` 返回不含 NUL 的长度。
实际输出须有额外一个 NUL 字节；容量不足、格式非法、分配失败均保留 buffer 和 written。
最坏字符串结果为 24578 字节加 NUL；输入拷贝、cJSON 节点与编码缓冲的暂存受此预算约束，
没有遗留句柄或 callback。错误区分 ARGUMENT、LIMIT、INVALID 和 MEMORY；旧解析/序列化 ABI 不变。
Runtime typed SQL 结果使用这一入口，在事务提交前完成字符串/实数编码与结果容量判定。

## 构建与验证

使用根解决方案构建，不要把 `cJSON` 源码重新编译进应用。公共头中的生命周期规则是 ABI 的一部分；改动后运行 `python scripts\audit_repo.py` 和正式 ARMV4I 构建。

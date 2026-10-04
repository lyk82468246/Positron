# `positron_script`

`positron_script.dll` 是独立 JavaScript 执行服务，把 Duktape 2.7.0 封装为 opaque C ABI。它不是浏览器：不创建窗口、不抓取资源、不拥有 DOM，也不提供 `window` 或页面对象。

## 输出与依赖

- 工程：`positron_script.vcproj`
- 输出：`bin\Debug\positron_script.dll`、对应 `.lib`
- 公共头：`positron_script.h`
- 实现来源和许可证：本目录 `UPSTREAM.md` 与根 `THIRD_PARTY.md`

普通 WM6 C/C++ 程序链接 `positron_script.lib`，部署 `positron_script.dll`，只包含公共头。浏览器绑定由 `positron_browser.dll` 和宿主组合，不能反向把浏览器私有对象加入本 DLL。

## 其他项目如何调用

一个最小持久 context 的生命周期如下：

```c
#include "positron_script.h"

HANDLE script;
const char *result;

script = PScript_Create(PSCRIPT_DEFAULT_BUDGET_MS);
if (script == NULL ||
        PScript_Evaluate(script, "1 + 2", -1) != PSCRIPT_OK) {
    PScript_Destroy(script);
    return 1;
}
result = PScript_GetResult(script); /* 借用；不要 free 或修改。 */
/* result == "3" */
PScript_Destroy(script);
```

可用能力：持久 global（字符串、数字、布尔和 JSON）、JSON 参数的全局函数调用、同步 native JSON callback、CommonJS 风格模块、同步模块 source provider，以及预算、内存和模块计数诊断。长生命周期消费者可调用 `PScript_CollectGarbage()` 显式回收并 compact Duktape 临时对象；它不改变 global/module 状态，也不会提高固定 heap ceiling。`PScript_GetError` 同样返回借用字符串。

context 不支持并发调用；执行中的 host callback 不得重入或销毁当前 context。源码、结果、模块数、native function 数和堆内存都有上限，具体常量以 `positron_script.h` 为准。当前 `PSCRIPT_MAX_NATIVE_FUNCTIONS` 为 29：浏览器组合层的 DOM、validation、contenteditable、导航、焦点、pointer-interaction selector、FormData 和有界 direct-element DOM removal 桥接会占用这些槽位，宿主若注册额外的全局 native 函数仍必须检查 `PScript_GetNativeFunctionCount()`，并在达到上限时保守失败。模块 provider 的源代码和释放回调由宿主拥有，DLL 只在同步调用约定内使用。

### native JSON 结果容量

旧 `PScript_RegisterGlobalJsonFunction()` 保持 256-byte 临时缓冲（最多 255 JSON bytes）。
需要更大结果的组合层可以使用 additive 的 `PScript_RegisterGlobalJsonFunctionEx()`，以
`PScriptJsonFunctionOptions` 的 size/version 和 result_capacity 显式选择 256–8192 bytes。
Ex 临时缓冲由 Duktape 管理并计入原 context heap，回调/异常退出后不留 CRT allocation；
不增加 source、native function 数或执行预算。回调仍只同步写入一个完整 JSON 值，不能
跨线程、重入或保存 buffer。参数错误不替换已有注册；较大的 JS 值仍受原 GetResult 的
诊断字符串大小约束，可在 JS 中读取成员后返回小型断言结果。

Browser 的应用服务使用这个容量原语传送有界 JSON，pending、身份、撤销与 JS callback
生命周期仍归 Browser；本引擎扩展本身不拥有请求、应用权限、文件或 DB 方法。

## 同步耗时诊断

`PScript_SetPerformanceEnabled(script, 1)` 在空闲边界开启当前 context 的计时；默认关闭，传入 `0` 关闭。每次设置都会清零计数，但保留 context 创建耗时，不改变 global、模块、错误或执行预算。查询使用调用方拥有的固定快照：

```c
PScriptPerformanceInfo timing;

memset(&timing, 0, sizeof(timing));
timing.size = sizeof(timing);
timing.version = PSCRIPT_PERFORMANCE_VERSION;
if (PScript_GetPerformanceInfo(script, &timing) == PSCRIPT_OK) {
    /* last_total_ms / last_compile_ms / last_execute_ms */
    /* last_gc_ms / max_sync_ms / max_callback_ms */
}
```

计时使用设备 `GetTickCount()`，小于时钟粒度的调用可能为零；这是经过时间，不是 CPU 时间。`Evaluate` 分别记录受保护的编译/解析与执行，保留旧全局 `this`、错误、timeout 和 heap ceiling 语义；未开启时仍走旧求值路径。两个阶段可能包含引擎自动 GC，不能从它们中单独扣除自动 GC；`CollectGarbage` 的显式 GC 和 `CallGlobalJson` 则独立记录。`max_sync_ms` 覆盖这些已计时入口，不代表所有 API 或整个页面的最大阻塞。

native 计时包含 JSON dispatch 和 Browser 桥接，callback 计时只围绕宿主函数；它们嵌套于求值而非额外耗时，内部嵌套调用也可能重叠，不能相加成页面百分比。快照只保留计数和有界函数名，不保留源码、参数或结果。DLL 不打印日志、不分配诊断缓冲、不回调观察者，也不运行消息泵；固定诊断结构属于 wrapper/session 元数据，不是 Duktape heap 使用量。开启、查询和关闭均须遵守同线程、不可重入及 handle 生命周期；非法 size/version 不改输出。

计时不提供暂停、续执行或任意 JS 抢占。timeout 仍终止当前求值，不能把重跑源码当成恢复执行。

## 浏览器关系与验证

浏览器开启 `javascript=1` 时仍使用这套 Duktape 引擎，但浏览器对象和 DOM 适配属于 `positron_browser.dll`/宿主，不是第二套引擎。修改 wrapper 或上游 Duktape 后应保留 ABI、预算和所有权规则，并运行 C89 检查、正式 ARMV4I 构建及脚本设备回归。

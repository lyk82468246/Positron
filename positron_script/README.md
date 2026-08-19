# `positron_script`

`positron_script.dll` 是独立 JavaScript 执行服务，把 Duktape 2.7.0 封装为
opaque C ABI。它不是浏览器：不创建窗口、不抓取资源、不拥有 DOM，也不提供
`window` 或页面对象。

## 输出与依赖

- 工程：`positron_script.vcproj`
- 输出：`bin\Debug\positron_script.dll`、对应 `.lib`
- 公共头：`positron_script.h`
- 实现来源和许可证：本目录 `UPSTREAM.md` 与根 `THIRD_PARTY.md`

普通 WM6 C/C++ 程序链接 `positron_script.lib`，部署 `positron_script.dll`，
只包含公共头。浏览器绑定由 `positron_browser.dll` 和宿主组合，不能反向把浏览器
私有对象加入本 DLL。

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

可用能力：持久 global（字符串、数字、布尔和 JSON）、JSON 参数的全局函数调用、
同步 native JSON callback、CommonJS 风格模块、同步模块 source provider，以及
预算、内存和模块计数诊断。`PScript_GetError` 同样返回借用字符串。

context 不支持并发调用；执行中的 host callback 不得重入或销毁当前 context。
源码、结果、模块数、native function 数和堆内存都有上限，具体常量以
`positron_script.h` 为准（当前 `PSCRIPT_MAX_NATIVE_FUNCTIONS` 为 17，浏览器组合层的
validation/custom-validity typed callbacks 也计入这个受控槽位）。模块 provider 的源代码和释放回调由宿主拥有，DLL 只在
同步调用约定内使用。

## 浏览器关系与验证

浏览器开启 `javascript=1` 时仍使用这套 Duktape 引擎，但浏览器对象和 DOM 适配属于
`positron_browser.dll`/宿主，不是第二套引擎。修改 wrapper 或上游 Duktape 后应保留
ABI、预算和所有权规则，并运行 C89 检查、正式 ARMV4I 构建及脚本设备回归。

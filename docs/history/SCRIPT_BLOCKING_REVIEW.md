# 脚本同步阻塞：测量与改进方案

本材料记录 DLL 侧函数级取证、有界初始化协议验证及后续方案，不代替当前路线图或能力合同。已交付可选计时和产品程序之间的返回边界，**不是已完成的整页卡顿修复**。没有修改应用调度、Core、执行预算或站点逻辑。

## 原始现象与证据边界

消费者的 `tmp/device-runs/app-script-blocking-review/positron-debug.log` 中，PID 3714729834 从 phase=4 的 tick=10627101 到 phase=5 的 tick=10639354 相隔 12253 ms。首个 ignored-size 日志为 tick=10633899，前面的 6798 ms 包括 session 创建、callback 注册、Browser bootstrap 与宿主检查；不能把该差值直接命名为某个函数耗时。

源码有两层独立阻塞：应用在 UI 线程执行整批脚本并逐脚本完整 GC；Browser 的旧 bootstrap 入口在一次调用中依次求值多段产品程序及 installer，最后 GC。此次只测量公共 DLL 路径；应用消息分批和 GC 策略由 EXE owner 处理，不在 DLL 中复制消息调度。

## 计时方法及公共合同

Script 新增 `PScript_SetPerformanceEnabled` / `PScript_GetPerformanceInfo`，Browser 新增 `PBrowser_ScriptSessionGetBootstrapPerformanceInfo`。旧函数签名和结构没有变化；新快照具有 size/version，函数名和阶段标签有固定容量。组件 README 与公共头文件是调用合同的权威位置。

诊断默认关闭、按 context/session 隔离，不做日志 I/O、动态诊断分配、observer callback 或消息泵。开启后按 Duktape 原有受保护求值步骤测编译与执行，保留显式 global this；未开启仍使用原 `duk_peval_lstring`。对照依据是仓库 Duktape 的 `duk_eval_raw` 以及官方 [受保护编译](https://duktape.org/api.html#duk_pcompile_lstring) 与 [受保护求值](https://duktape.org/api.html#duk_peval_lstring) 合同。

这是 guest GetTickCount 经过时间：编译/执行包含自动 GC，native 与 callback 是嵌套子区间，不能叠加成总量。显式 GC 独立计时，诊断不区分它内部两个 GC pass。模块内部编译没有独立分项；最大同步值覆盖 Evaluate、CallGlobalJson 和显式 GC，不覆盖每个公共入口。源码检查、宿主创建/接线及日志打印不能冒充引擎解析时间。夹具 bridge-setup 数值还包含少量夹具 bookkeeping 和一条 context 日志，不作为纯 callback 初始化结论。

## ARMV4I 测量结果

设备为 Microsoft DeviceEmulator，240×320、96 DPI。固定离线夹具使用仓库原版 jQuery 3.5.1（89476 bytes）及 Bootstrap 4.6.2（62563 bytes），保留 Core 命中、native-button transaction 和 collapse 展开→收起→再展开终态断言。未精简脚本、放宽断言或提高现有预算。

| 已开启诊断的调用，guest ms | Debug | Release |
| --- | ---: | ---: |
| Script context 创建 | 17 | 5 |
| 完整 Browser bootstrap 公共调用 | 6820 | 2392 |
| bootstrap 各段编译时间之和 | 6497 | 2216 |
| bootstrap 各段执行时间之和 | 228 | 130 |
| bootstrap 显式 GC | 90 | 44 |
| 最慢 bootstrap 内部阶段 | 927 | 314 |
| jQuery 完整求值 | 2468 | 904 |
| jQuery 编译 / 执行 | 1714 / 754 | 601 / 303 |
| Bootstrap JS 完整求值 | 962 | 387 |
| Bootstrap JS 编译 / 执行 | 819 / 143 | 321 / 66 |
| jQuery native / callback 总时间 | 27 / 11 | 14 / 6 |
| 单次宿主 callback 最大耗时 | 1 | 1 |
| 批次末显式 GC | 209 | 101 |

计时关闭的相邻夹具：Debug bootstrap/jQuery/Bootstrap JS 为 6601/2472/1013 ms；Release 为 2440/954/376 ms。单次相邻运行不是严格 overhead benchmark，也不能以 Debug→Release 配置差异宣称本批优化收益。Release 内部阶段之和与完整调用相差 1 ms，符合计时粒度与边界差异。

先行测量结论：初始化主要耗时在产品源码编译/解析，context 创建和当前 DOM callback 不是首要热点。当时最大不可返回宿主的公共调用是整个 bootstrap，Debug 6.82 秒、Release 2.39 秒，而不是其最慢内部段。jQuery 单次调用有 Debug 2.47 秒、Release 0.90 秒阻塞；仅在多个脚本之间让步不足以解决它。全站其他脚本、完整页面 DOM 规模、真实低资源硬件与当前 EXE 的 GC 频率还需消费者接入诊断后测量。

## 验证与失败记录

新增 TEST1336 验证错误 version 输出不变、非法 enable、活动 callback 中查询/切换拒绝、计数、全局/严格 this、语法错误、timeout 后再次求值、heap 压力错误及恢复、显式 GC、关闭诊断不清 global，以及三次独立创建/销毁；随后运行上述原版 Browser/Core 夹具。它没有验证尚不存在的分步取消接口，也不证明真实 UI 已响应。

正式 Debug 与 Release 门均选择 `80-82,86,1327,1336,999`：selected/observed 7/7、唯一 TESTBENCH PASS、零 ERROR/FAIL、crash_check=PASS、无新增 dump。匹配正式 stage 含 40 文件，SD Temp 与内部缓存空间预检通过，guest DLL holder 审计均为 0/0。完整证据分别位于：

- `tmp/device-runs/20261003-180313-script-performance-baseline/`
- `tmp/device-runs/20261003-180527-script-performance-release-final/`

Debug 当前部署目录已完整清理；Release 清理失败，残留保留，日志已完整回收。未强杀进程、重连设备、修改 WMDC/安全/注册表。先前选择不存在 TEST79 的门在 RAPI 前 fail closed；静默 wrapper 启动没有设备结果，均不计通过。最终使用 PS32 `-Command` 调用同一正式 gate，未绕过 stage 或构建工程。

C89、仓库审计及串行正式 Debug/Release 构建通过。Release 曾提前退出及遭遇 CabWiz 数据文件错误；失败保留于 `tmp/script-performance-builds/`，提权重跑正式入口及最终 stage/CAB 成功，不把先行失败改写成成功。

## 已交付：有界初始化，而非任意 JS 抢占

Browser 新增 `PBrowser_ScriptSessionBootstrapBegin/Step/Cancel/GetState`，保留旧同步入口及 ABI。精确调用和借用生命周期以 [Browser README](../../positron_browser/README.md) 与公共头文件为准。

1. 宿主先完成全新 session 的 callback/document 接线，Begin 冻结接线并进入私有 PENDING；size/version 和非零 generation 必须匹配。generation 是宿主提供的候选标识，不自动校验 Core 文档代次。
2. 固定目录为 37 个槽，公开上限 40；不适用的 installer 有界跳过，每次 Step 至多执行一个完整产品程序或一次显式 GC。31 段基础程序与修改前同步路径的调用顺序逐项核对一致，document.write 和可选 installer 保持原顺序。旧同步入口驱动同一目录。
3. PENDING/FAILED/CANCELLED 拒绝普通求值、事件、任务、接线更改和 runtime 外借；只有内部初始化代码可访问候选 context。全部 ScriptSession 入口由静态边界审计检查，COMPLETE 才开放新 session。Begin 前已取得的裸 runtime 别名无法撤销，消费者必须遵守禁止继续使用的合同。
4. Step、查询、Cancel 和 Destroy 限创建线程；控制和诊断只在空闲边界可用。正在 Step 或 callback 中调用 Destroy 无操作，宿主必须在返回后重新销毁。Cancel 是幂等终态、保留存储直到 Destroy，不运行作者取消代码。失败或取消不可在同一 session 重新初始化，旧已提交 session 保持独立。
5. 新协议不把 timeout 当成 yield，不保存执行栈，也不分割作者程序。单个 Step 没有固定毫秒承诺，宿主仍需每步返回消息循环并退休 stale 候选；持续循环调用不会自动获得响应性。

## 分步协议的设备证据与剩余失败

TEST1338 验证基础路径每个空闲边界取消、非法 size/version、输出不变、stale generation、错误线程、callback 内访问与销毁拒绝、timeout 和 heap 失败终态、旧 session 状态不变及重复 teardown。完整可选接线路径复用原版 jQuery/Bootstrap 与 Core/native-button 展开/收起最终几何断言；没有声称每个可选 installer 的取消位置或真实硬件输入已经验收。

同一门中的计时对照如下。新接口在测试循环中连续推进，没有插入消息泵；累计时间随 guest 负载变化，不能把它解释为总耗时优化。

| 调用边界，guest ms | Debug | Release |
| --- | ---: | ---: |
| 同步完整 bootstrap（诊断开启） | 6622 | 2627 |
| 分步 bootstrap 累计 active | 8652 | 2536 |
| 分步 bootstrap 总经过时间 | 8654 | 2537 |
| 最大单次 Step | 1262 | 339 |
| 分步路径后 jQuery 单次求值 | 3130 | 1003 |
| 分步路径后 Bootstrap JS 单次求值 | 1370 | 408 |

Release 从一次约 2.6 秒的同步初始化开放为最大约 339 ms 的 Step 边界；Debug 的最大 Step 仍约 1.26 秒。作者 jQuery 求值仍同步阻塞，旧完整入口仍整段阻塞，不能把这批结果报告为最终 EXE 已不卡顿。

新合同 Debug 门 `tmp/device-runs/20261003-223043-browser-bootstrap-contract-probe/` 的 `1174,1175,1327,1336,1338,999` 为 6/6 PASS；Release 门 `tmp/device-runs/20261003-224128-browser-bootstrap-step-release/` 的 `80-82,86,203,1174,1175,1327,1336,1338,999` 为 11/11 PASS。临时对照撤回后的 Debug 正式复验 `tmp/device-runs/20261003-225011-browser-bootstrap-restored-debug-final/` 的 `80-82,86,1327,999` 为 6/6 PASS。均完整回收日志、唯一 TESTBENCH PASS、零 ERROR/FAIL、crash_check=PASS；匹配正式 stage，SD/内部余量和 guest holders=0 unavailable=0 通过。Release 当前目录清理失败，残留保留；未重置设备或改 WMDC。

相邻 Debug 门在 TEST203 默认 1 秒预算下初始化超时，重新正式构建仍复现，没有放宽原成功断言。临时恢复修改前的同步程序序列、去掉 Step 推进状态与计时元数据后，同样超时；证据在 `tmp/device-runs/20261003-224553-browser-bootstrap-sync-control/`。这是旧同步序列对照，不是完整旧 DLL 二进制基线，不能据此声称已排除所有回归。临时代码已撤回并重新正式构建，Release TEST203 通过；Debug 默认预算失败保留为编译热点边界，不归为新 Step 已通过项。

C89、全 ScriptSession 入口审计、仓库审计及串行正式 Debug/Release/CAB 通过。重试前的 CabWiz 数据文件失败及 VS2008 msenv.dll 主机崩溃仍保存于 tmp；只终止了身份核对后的本批隐藏崩溃构建进程，没有终止 GUI、WMDC 或其他会话。未绕过正式工具链，也没有修改 Core/Script 预算来适配此门。

Task checkpoint 的 timer/frame/message/idle phase 已有掩码边界，但微任务 drain 与事件顺序不能简单拆开。进一步 step 需要冻结参数、阶段游标和原子 drain 合同；某个作者 callback 或微任务本身长执行仍无法被它抢占，暂不与初始化纵切合并。

## 编译热点优化的可行性与审查门槛

产品 bootstrap 在每个新 session 重复编译，值得调查固定预算的内部编译结果复用，但不能简单保留旧 context/闭包。候选缓存只能持有 DLL 自己生成、已验证且与源程序及引擎配置精确匹配的编译产物，不公开任意 bytecode 输入；每个 session 仍产生独立 global、callback 和对象。须先测产物体积、峰值内存、复用次数和失败收益，再确定预算、淘汰、并发隔离和旧路径回退。

Duktape dump/load 不是跨版本稳定的公共 ABI，也不校验恶意 bytecode；其格式可以跨平台，但必须满足相同兼容版本与配置条件，不能笼统当成安全通用缓存文件。[官方 load 合同](https://duktape.org/api.html#duk_load_function) 因而要求只使用可信产物。缓存不解决冷启动首次编译，不能据此承诺 WinWorld 第一次打开不阻塞。

若冷启动作者源码解析仍是关键阻塞，可另行评估“独立 compiler context → DLL 生成的 opaque 编译产物 → UI context 加载执行”。它不是把现有 session/Core document 放进 worker：compiler 不执行作者代码、不访问 DOM/callback，宿主拥有 worker 和消息交接。需要新的编译/加载所有权、预算、引擎一致性和取消合同；VM/allocator 的线程隔离也须证明。此项是较大架构调整，当前只提出可行性，不在未经审查时实现或引入额外无界内存。

单个 classic script 的执行 continuation 需要 VM 和 C/native 栈层面的设计，远大于消息分批。现有 timeout 只终止，不保存 continuation；不能超时后重跑源码、在 DLL 内 DispatchMessage、并发访问同一 session，或提高 budget 来冒充修复。EXE 可接入已验证的 Begin/Step/Cancel，在安全边界处负责 WM 调度及 GC 策略。DLL 下一步只测量可信编译产物体积、峰值内存和冷启动/重复成本；证据齐备后才取舍固定预算的复用方案，不直接宣布缓存或作者程序 continuation 已实现。

# 脚本同步阻塞：测量与改进方案

本材料记录一次 DLL 侧取证及尚待实施的方案，不代替当前路线图或能力合同。此次交付是可选的函数级计时与回归证据，**不是已完成的卡顿修复**。没有修改应用调度、Core、执行预算或站点逻辑。

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

结论：初始化主要耗时在产品源码编译/解析，context 创建和当前 DOM callback 不是首要热点。最大不可返回宿主的公共调用仍是整个 bootstrap，Debug 6.82 秒、Release 2.39 秒，而不是其最慢内部段。jQuery 单次调用仍有 Debug 2.47 秒、Release 0.90 秒阻塞；仅在多个脚本之间让步不足以解决它。全站其他脚本、完整页面 DOM 规模、真实低资源硬件与当前 EXE 的 GC 频率还需消费者接入诊断后测量。

## 验证与失败记录

新增 TEST1336 验证错误 version 输出不变、非法 enable、活动 callback 中查询/切换拒绝、计数、全局/严格 this、语法错误、timeout 后再次求值、heap 压力错误及恢复、显式 GC、关闭诊断不清 global，以及三次独立创建/销毁；随后运行上述原版 Browser/Core 夹具。它没有验证尚不存在的分步取消接口，也不证明真实 UI 已响应。

正式 Debug 与 Release 门均选择 `80-82,86,1327,1336,999`：selected/observed 7/7、唯一 TESTBENCH PASS、零 ERROR/FAIL、crash_check=PASS、无新增 dump。匹配正式 stage 含 40 文件，SD Temp 与内部缓存空间预检通过，guest DLL holder 审计均为 0/0。完整证据分别位于：

- `tmp/device-runs/20261003-180313-script-performance-baseline/`
- `tmp/device-runs/20261003-180527-script-performance-release-final/`

Debug 当前部署目录已完整清理；Release 清理失败，残留保留，日志已完整回收。未强杀进程、重连设备、修改 WMDC/安全/注册表。先前选择不存在 TEST79 的门在 RAPI 前 fail closed；静默 wrapper 启动没有设备结果，均不计通过。最终使用 PS32 `-Command` 调用同一正式 gate，未绕过 stage 或构建工程。

C89、仓库审计及串行正式 Debug/Release 构建通过。Release 曾提前退出及遭遇 CabWiz 数据文件错误；失败保留于 `tmp/script-performance-builds/`，提权重跑正式入口及最终 stage/CAB 成功，不把先行失败改写成成功。

## 下一纵切：有界初始化，而非任意 JS 抢占

建议先实施 Browser-owned bootstrap 的版本化分步协议，再取舍编译热点优化。以下是方案，不是本批新增的可调用接口：

1. Begin 冻结完整 callback table 和文档绑定，创建私有候选初始化状态；Step 每次执行一个有明确依赖的完整产品程序；查询返回 initializing/pending/complete/failed，取消在空闲边界生效。固定阶段数、借用文档寿命、owner 线程和内存计费必须写入头文件。
2. pending 时拒绝作者求值、事件、任务、runtime 外借和半初始化对象访问；只有内部初始化代码可使用候选 context。必须审计全部外部入口，不能只给新函数加检查。完整成功后才开放 session；失败或取消销毁候选，不污染旧页。
3. Cancel/Destroy 只允许没有同步调用或 callback 正在使用 context 的边界；不宣称能中断正在解析或执行的段。宿主通过 generation 退休过时候选，关闭须等正在使用者返回再释放。
4. 旧完整 bootstrap 入口同步驱动相同顺序，保持对象 identity、document.write、installer 依赖及 GC 行为。测试对比旧/分步路径、每段取消/失败、过时文档、半初始化入口拒绝、重复 teardown、heap 压力及最终事件/DOM 状态。
5. 最慢内部段当前仍可阻塞 Debug 927 ms；若继续拆分，只能在 Browser-owned 源码的真实依赖边界拆，不能切作者源码或破坏闭包、作用域和顺序。分步协议本身不等于解决作者脚本单次停顿。

Task checkpoint 的 timer/frame/message/idle phase 已有掩码边界，但微任务 drain 与事件顺序不能简单拆开。进一步 step 需要冻结参数、阶段游标和原子 drain 合同；某个作者 callback 或微任务本身长执行仍无法被它抢占，暂不与初始化纵切合并。

## 编译热点优化的可行性与审查门槛

产品 bootstrap 在每个新 session 重复编译，值得调查固定预算的内部编译结果复用，但不能简单保留旧 context/闭包。候选缓存只能持有 DLL 自己生成、已验证且与源程序及引擎配置精确匹配的编译产物，不公开任意 bytecode 输入；每个 session 仍产生独立 global、callback 和对象。须先测产物体积、峰值内存、复用次数和失败收益，再确定预算、淘汰、并发隔离和旧路径回退。

Duktape dump/load 不是跨版本稳定的公共 ABI，也不校验恶意 bytecode；其格式可以跨平台，但必须满足相同兼容版本与配置条件，不能笼统当成安全通用缓存文件。[官方 load 合同](https://duktape.org/api.html#duk_load_function) 因而要求只使用可信产物。缓存不解决冷启动首次编译，不能据此承诺 WinWorld 第一次打开不阻塞。

若冷启动作者源码解析仍是关键阻塞，可另行评估“独立 compiler context → DLL 生成的 opaque 编译产物 → UI context 加载执行”。它不是把现有 session/Core document 放进 worker：compiler 不执行作者代码、不访问 DOM/callback，宿主拥有 worker 和消息交接。需要新的编译/加载所有权、预算、引擎一致性和取消合同；VM/allocator 的线程隔离也须证明。此项是较大架构调整，当前只提出可行性，不在未经审查时实现或引入额外无界内存。

单个 classic script 的执行 continuation 需要 VM 和 C/native 栈层面的设计，远大于消息分批。现有 timeout 只终止，不保存 continuation；不能超时后重跑源码、在 DLL 内 DispatchMessage、并发访问同一 session，或提高 budget 来冒充修复。EXE 可先使用诊断精确定位整页各调用，并负责已有安全边界处的 WM 调度及 GC 策略；DLL 侧下一步仅实现上述有界初始化合同。

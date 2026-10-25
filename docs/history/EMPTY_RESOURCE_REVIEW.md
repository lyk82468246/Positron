# 空资源合同与相邻脚本夹具事故

本文件保存本次失败取证，不是当前发布或设备基线。当前结果以 `.agents/HANDOFF.md` 为准。

## 消费者与候选

目录应用发现 Runtime 已支持零字节读取，但 Browser 的旧资源提交要求正长度，Core 的
CSS/script 缓存也拒绝空内容。候选新增 Browser SetDataEx 和 Core script fetch Ex2，保留
旧入口正长度合同；真实 CSS 空表不补入占位正文。公开调用说明在组件 README。

## 构建失败与恢复

`tmp/empty-resource-debug-host.log` 中新夹具误写 PCore_Cleanup，正确释放函数为
PCore_Shutdown。`tmp/empty-resource-debug-accepted-host.log` 的 VS 在编译前退出；
`tmp/empty-resource-debug-final-host.log` 则遇到并行 EXE 源码中尚未声明的循环变量 i。
前者由本会话修正测试，后者由应用会话修正自己的源文件；未修改工具链或绕过整套构建，
失败产物均未 staging。恢复后的正式 Debug/Release 日志为
`tmp/empty-resource-{debug,release}-build-final.log`。

## 相邻门崩溃

`tmp/device-runs/20261025-144703-empty-resource-debug-restored/` 选择
43、45、77、111、1118、1371、1374、999，只有前两项记录 OK，不能追认为门通过。
发现新转储后中止主机轮询，保留远端包；转储完整回收到
`tmp/empty-resource-debug-crash.kdmp`，未删除设备原件，也没有读取未宣布的新截图。

该转储记录相同 test_host-run basename/PID，C0000005，异常 PC 0x2e9d4、读取地址
0x2400000d。EXE 槽位基址对应的 RVA 为 0x1e9d4；匹配 PDB 解析为
pcore_navigation_resource_base+0x6c，test_host/main.c:14965。
CE 异常记录与模块表的解释参照微软的
[异常流](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms939638%28v%3Dmsdn.10%29)
和[元素列表](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms939636%28v%3Dmsdn.10%29)
格式，不以转储中的字符串猜测调用栈。

源码证据：TEST77 的 pw 是三个整数的 script fixture ctx，却把它传给 wm_combine_url；
后者现按 production navigation request 遍历 resources/effective_url，越界解引用。
TEST111 使用另一小型脚本 ctx，也存在同样误接。TEST45 已有忽略 fixture pw、直接调用
公开 HTTP resolver 的薄适配器。修正将该适配器明确命名为 test_host_fixture_resolve，
让这三项共享；原 URL、cache、缺失资源和作者执行顺序断言保留。不改生产 resolver、
EXE 或 DLL，不跳过崩溃用例；这是平台测试接线修正，不是宿主重建 URL 业务语义。

## 恢复边界

用户已确认点击 Done/旧程序关闭；重跑取得新模块快照零引用。
后续 `20261025-145433-empty-resource-debug-fixed` 的 TEST77/111 通过，但 TEST1118 的额外
诊断 native function 在已有 29 槽上限触发 -15，仍是失败门。探针改为显式绑定宿主实例的
文本 mutation observer：隐藏 fixture 元素经原 Core setter 后记下 timer/visibility/pagehide/
unload 等标签，不额外注册 JS native function。普通宿主实例不启用 observer，初始化为 NULL，
没有测试编号分支。原 teardown 顺序、旧页/队列和计数断言全部保留，另断言槽数不变；
未扩大预算或删除产品 callback。

匹配 Debug `20261025-145800-empty-resource-debug-probes` 和 Release
`20261025-145914-empty-resource-release` 的原八项已通过，无新 dump，保留旧失败与转储。
最终探针零新增槽断言在 `20261025-150041-empty-final-debug` 与
`20261025-150125-empty-final-release` 各 3/3 通过，仍无新 dump。
应用侧仍需独立接入新 API 并验收 positron.exe；DLL 测试
不能替代 EXE 接线、原生输入或 SQL 页面服务的验收。

# EXE 首次安装接线验收

本文保存冻结目录首次安装消费者的证据与失败尝试，不代替当前交接或公共合同。
调用和预算见[宿主](../../positron_app/RUNTIME_HOST.md#首次安装与普通installed启动)，
结构化主库表见[应用数据设计](../../positron_app/APPLICATION_DATA_PLAN.md#安装登记与持久授权)。

## 接线范围

EXE以主库schema v5增加app_installs，同一DB owner执行字段化持久CAS和新鲜读回。
Runtime保有意图/数据/提交及租约语义；EXE提供平台快照、逻辑ID/安装ID协调、原生确认和worker调度。
成功关闭准备链后单独打开installed实例及require-existing私有库，再授权Browser队列。
入口为`--install-dir`和`--app`；`--app-dir`每次确认不变。无PXI输出、安装关联、更新或卸载。
公共头文件只补可信不可变视图说明，结构、签名、导出和DLL产品实现未改。

## 自动证据

首个通过包为`tmp/device-runs/20261106-200344-app-runtime-install/`，
明确使用内置`\Temp\Positron-device-gate`，不移动已绑定数据。
EXE及十一DLL回读SHA-256为12/12；原生phase10通过，随后两个独立进程运行真实installed页面。
原生门覆盖无SQL、schema0、丢失READY响应、不可读读回后普通重开、迁移后缀恢复、取消、
完整字段CAS拒绝、租约冲突、缺库不CREATE、错误数据拒绝且字节不变、不可变视图及捕获变化、
128文件和4MiB精确/超限。故障为明确的Debug受控注入，不冒充VFS/断电测试。

installed页面门复用实际SQL SDK夹具，验证32行typed结果、有界队列、事务回滚、失败候选保留旧页服务、
离页撤销/重返新队列及关闭owner。两个进程均正常退出至holders=0/unavailable=0，无新增dump。
冷读主库八条结构化记录及初始资源摘要8/8通过；prefix为READY generation4、取消项为CANCELLED
generation2，未知响应项仍为READY generation2，未重放CAS。独立检查器自行按公开framing计算SHA-256，
包含未引用文件和空文件，不以Runtime PASS日志代替数据验证。

最终包`tmp/device-runs/20261106-201232-app-runtime-install/`收紧断言，要求初次迁移失败确为
PRUNTIME_LIMIT，避免其他SQL错误冒充预算失败；原生phase10、两进程页面、12/12哈希、八条冷读摘要、
退出零引用和crash门均再次通过。相邻设置存储/服务门另在同包自动运行，验证旧schema升级、
完整偏好/回滚、未来schema6拒绝和服务隔离；结果保存在app-runtime-install-adjacent。
原生确认/拒绝、保存及免提示重开的人工结果见下节；双语和Release普通启动仍待验收。

## Notes 首次安装人工验收

用户在最终Debug包的原生安装风险框先选择“否”。原PID376866126日志确认返回newtab且无Notes页面提交，
用户菜单正常退出后模块审计为零引用；冷读主库没有Notes登记，原八条登记逐字段完全未变。
证据在同包manual-installation的declined-final-debug.log、declined-exit-modules.log、declined-cold.db及declined-result.txt。

重新打开同一冻结目录，用户选择“是”、保存一条笔记并正常退出。PID4129644506的日志确认
InstallCommit完成后单独OpenInstalled成功；冷读主库新增唯一Notes READY记录，generation2、schema1，
完整资源摘要匹配原始示例，其余八条登记不变。私有库完整性检查通过且有一条已保存笔记，未打印笔记内容。
accepted-final-debug.log、accepted-cold.db、notes-accepted-cold.db及accepted-result.json保留了这一证据，
退出审计仍为零引用，未强杀或重建数据。

随后以普通`--app org.positron.notes`入口重开，PID1452334954的日志确认实际app页面提交，
没有再次进入安装/确认/提交流程。用户明确确认未弹权限框且刚保存的笔记仍在列表中。
本轮关闭这条Debug首次安装确认/拒绝、持久保存与普通重开人工门；当前实例留给用户，不自动退出。
未覆盖恢复状态的原生确认UI、双语、Release普通启动、PXI或所有OEM/存储故障。

## 构建与离线检查

正式Debug清洁rebuild遇到既有解决方案依赖顺序LNK1181（DB/TLS/libdom/Image/libcss/Runtime
导入库尚未生成），未算作PASS；随后正式Debug build补建通过，EXE零错误/警告。
正式Release build及CAB通过，Release正式stage生成。五项安装诊断/自检字符串ASCII与UTF-16均排除，
普通安装入口保留。C89四项与Runtime服务六项通过；最终提交前再次核对审计。
不使用临时工具链、SafeMode或修改构建配置。

## 保留的失败与空间处理

`20261106-195207-app-runtime-install`停在原生phase3：测试夹具错误使用DB合同禁止的
CREATE TABLE AS SELECT，造成SQL_REJECTED。修为独立CREATE与INSERT SELECT，未放宽DB策略。
原失败库、资源和日志保留，不删除或重放。此前两次VS预生成中断经Application事件确认devenv崩溃，
正式重试通过，不作为编译PASS或产品失败根因。

内置卷不足以同时容纳多份完整包。仅对已正常退出的本轮专用测试包，把原始EXE/DLL/重命名测试宿主
回读并与对应stage逐个SHA-256核对后，归档到本地`archived-remote-binaries`，再精确删除远端重复二进制。
已通过包另归档未登记bounds夹具的4MiB填充文件；注册资源、安装主库和私有数据均原位保留。
新包仍执行正式空间门，不把空间不足写成产品失败或降低边界预算。
沙箱内RAPI访问拒绝与正常宿主的实际设备结果分开；无WMDC重连、设备重置或进程强杀。

## 仍未证明

外置映射卷CANTOPEN/native2、旧设备mkdir183及早期SQL启动失败未归因，内置通过不能关闭它们。
不重开用户挂起的FULL/断电门。完整可信原生快照不宣称任意恶意写入的原子冻结或安全沙箱；
宿主快照最多128文件/4MiB，另计内存。主库原有单owner策略仍限制同部署并发启动。
PXI、更新/卸载、恢复确认UI及Release普通启动不由上述Debug门冒充通过。

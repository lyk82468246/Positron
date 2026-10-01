# 当前交接

本文件只保存当前事实、候选、风险和唯一下一步。稳定边界见 [架构](../docs/ARCHITECTURE.md)、[能力矩阵](../docs/CAPABILITIES.md) 和 [测试合同](../docs/TESTING.md)。

## 使命与当前目标

Positron 为 WM6 / CE 5.2 ARMV4I 提供九个资源有界、稳定 C ABI 的公共 DLL 及独立消费者 positron.exe。产品语义属于 DLL，test_host 只拥有 fixture、平台接线和断言。本轮目标是 EXE 私有内部页面与命令地址；不修改公共 DLL ABI，不引入持久化或真实下载。

用户已确认 WinWorld 菜单展开/收起、SVG 与作者按钮外观正常；性能修正后的实际展开/收起不超过约 2 秒，期间地址栏与菜单可响应。不要恢复旧的“按钮无响应/图标仍损坏”假设；Release 点按耗时对照仍未完成，不把 Debug 体验扩大为全部设备的保证。

## 当前代码与所有权

分支 main。并行性能变更已由 df205495 提交，后续 DB 测试提交以 Git 为准；内部页面提交只包括 positron_app、专用检查脚本及其文档。共享 main.c 中原有性能计时与退出日志已保留，不纳入重复修正。本批新增的 device_tools 模块审计属于设备门前置能力，已与内部页面改动分开。

内部注册表区分 newtab/about/history/downloads/settings、version/system 别名、quit 命令及原 welcome/controls。默认入口为 newtab；内部页使用双语嵌入资源、Core 渲染、24 项有界焦点目录，无 ScriptSession 或外部请求。history 只读 Browser 导航栈；动态 HTML 上限 128 KiB。quit 的导航来源检查仅允许地址栏直接提交，沿既有 WM_CLOSE 关闭流程。设计与后端进入条件见 [接线计划](../positron_app/INTEGRATION_PLAN.md)。

## 候选与验证证据

### 内部页面自动门

Debug 私有启动自检使用独立 Browser history，不污染实时页面：检查所有来源的 quit 策略、别名只提交一次、16 项及前进栈、反向顺序、HTML 转义、自身过滤、容量拒绝、Core 解析/布局和 history 的 23 个焦点；另读取并解析十份中英文 RCDATA，确认 UTF-8 无 BOM、中文标题非 ASCII。夹具与诊断全部由 _DEBUG 编译隔离。

最终源码的导航门证据在 tmp/device-runs/internal-pages-delivery/navigation/：15/15，通过默认入口、各页面、大小写、末尾斜杠、about 片段与别名章节滚动、旧关键词/controls 查询参数、未知启动地址及启动 quit 拒绝；每次仅一个 history 提交、无脚本，焦点数量符合预期，crash_check=PASS。设备为 480x640、DPI192。包包含最终 Media/TLS ABI 显示；Debug 私有自检在每次启动通过，最终 PID 3811239850 留在 newtab。

外置卡候选 tmp/device-runs/internal-pages-verified/ 曾完整复制 23 文件并启动，但随后同目录文件可枚举却 RAPI 回读返回 device=5，清理工具启动返回 device=2；未归因于页面代码或声称文件已被删除。当前使用备用内置目录 \Temp\Positron-device-gate\internal-pages-1002；首次部署前对象存储可用约 23 MiB，最终更新前仍约 8 MiB。最终完整 stage 在 tmp/device-runs/internal-pages-delivery/stage/，23 个文件全部经设备回读 SHA256 核对；旧候选二进制仅在精确清理成功后替换，未变字体因 OS 持有而不删除，直接回读确认匹配。旧性能验收 PID 已被后续清理，不再让用户寻找旧进程。

相邻 test_host 门 tmp/device-runs/20261002-000923-internal-pages-adjacent/ 选择 136,1081,1082,1064,999：预检通过，但 test_host 日志一直为零字节，180 秒超时，清理摘要 failed=0；不是测试 PASS，也没有足够证据归因于特定 DLL。保留诊断目录，不放宽断言。

### 构建、审计与待验收门

C89、最终仓库审计与 diff 空白检查通过。最终 About 版本补全的正式 Debug build 通过，EXE 零错误/警告；正式 Release build 也已通过并生成 CAB。二进制检查确认 Release EXE 不包含内部自检诊断文本。

清洁 Debug/Release rebuild 均暴露 VS2008 解决方案依赖顺序问题：消费者在依赖库尚未生成时链接，出现 LNK1181；补跑正式 build 恢复。较早 Release 尝试还遇到 devenv/msenv.dll 自身异常，最后串行补建已恢复；不使用 SafeMode 或绕过正式工程，不把失败 rebuild 写成成功。本批不发布 nightly。

正式 stage Debug 已恢复成功并完成上述完整包部署。模块审计 helper 通过正式 ARMV4I Debug build；本批未绕过正式工程，也未启用强制清理。

### 设备门 DLL 引用审计

设备门现在使用无 Positron DLL 依赖的 `positron_process_cleanup.exe --audit-modules`，在启动
`test_host` 前通过设备端 Toolhelp 枚举所有进程及模块，覆盖 stage 中的 9 个 Positron DLL；
`holders=0 unavailable=0` 才能继续，快照不可用也 fail closed。`-ModuleAuditOnly` 可只做部署和
审计，不启动 `test_host`，也不能和 `-ForceTerminatePositron` 同时使用。该门只读，不会替用户
终止进程；针对 guest 的 `ERROR_NOT_ENOUGH_MEMORY` 仅做三次短重试，重试后仍不可用仍 fail
closed。

本批证据必须分开解释：`tmp/device-runs/20261002-003229-dll-audit/module-audit.log` 在
启动宿主前记录 `module_audit holders=0 unavailable=0`，随后宿主没有产生日志，TEST1321 不能
记为通过；中断该等待后，`tmp/device-runs/20261002-004031-module-audit/module-audit.log`
记录 `module_audit_unavailable scope=process_snapshot error=8` 和
`holders=0 unavailable=1`，因此不能证明当前设备仍无 DLL holder。没有调用
`-ForceTerminatePositron`，不把 `DeviceEmulator.exe` 的桌面进程存在当作 guest 结论。
本次只读重试 `tmp/device-runs/20261002-005621-module-audit/module-audit.log` 仍记录
`module_audit_unavailable scope=process_snapshot error=8` 和 `holders=0 unavailable=1`；
门在启动 `test_host` 前停止，未生成有效的 `module-audit-result.txt`，也没有使用强制清理。
随后 helper 对 `ERROR_NOT_ENOUGH_MEMORY` 增加三次短重试并完成正式 ARMV4I Debug 构建；
`tmp/device-runs/20261002-010145-module-audit/module-audit.log` 仍返回相同的
`process_snapshot error=8` / `holders=0 unavailable=1`，所以本次也未启动宿主。

### DB 主机契约测试当前切片

TEST1321 本批补齐了 schema 迁移失败后的残留表检查，并修正/断言同步注册拒绝复合主键；
`positron_db` 的 schema 校验现在统计实际主键列总数，v1 只接受单列 INTEGER/TEXT 主键。
同时补充 `INT64_MIN/MAX`、非空与空 BLOB 的 bind/column 读取、`ColumnCount`，以及同步请求
BLOB Base64 编码和服务端响应解码断言；新增未知顶层响应字段兼容性、重复/倒退/事务中迁移
拒绝、schema 配置版本拒绝和事务中禁止构造同步请求的断言。正式 `scripts\build.bat Debug
build`、C89 和仓库审计均通过；由于当前设备模块审计不可用，本切片尚未由设备上的
TEST1321 执行确认。

自动门不替代人工验收：地址栏直接输入/未知地址恢复原标题和地址、菜单、history 点击与刷新、直接 quit 和加载中 quit、中英文实际显示、触摸、键盘焦点、软键、滚动、旋转及 DPI 尚待确认。页面能力不应写成全部人工门通过的正式设备基线。

### 保留的性能基线证据

性能包 tmp/device-runs/debug-capture-20261001-234236/ 的自动 jQuery/Bootstrap 展开→收起→再展开终态 height=404/0/404、ok=true；style=8/7/9、layout=3/3/5、controls-refresh=13/15/16、paint=1/1/2 guest ms。普通点按的 style 约 79–103 guest ms，不能用自动探针耗时替代用户墙钟。

最终 resolver 切换门 tmp/device-runs/20261001-234106-css-cache-resolver-handoff-final/ 为 24,45,1327,999 的 4/4、唯一 PASS、零 ERROR/FAIL、无新增 dump；此前相邻 11 项门不代表后续源码全量重跑。TEST45 fixture context 错配的失败候选仍在 tmp/device-runs/20261001-230333-css-parse-cache-perf/，失败已经修正，不恢复旧 callback 接法。

## 有效边界与设备纪律

HTTP final URL、Core 资源终态和现有 SVG 能力继续有效；bootstrap-multiselect 语法边界、module/Shadow DOM、横向滚动条暂缓、SIP/IME/OEM 等见 [限制](KNOWN_LIMITATIONS.md)。内部 settings/downloads 只是诚实的只读说明，不代表配置或下载管理已经实现。DB 文件数据库设备门、HTTP 流式下载与持久化依赖仍未进入本批。

WMDC 连接由用户手动完成，只使用当前唯一目标；新部署不覆盖诊断包。精确清理必须取得 helper 成功摘要，不能杀 WMDC、VS GUI 或其他程序。外置卡失败时可检查空间后使用内置 Temp；日志回收前不删除目录。只在用户告知新截图时查询截图，不以旧截图推断新运行。

## 路线图复核与唯一下一步

ROADMAP 已复核：内部页面已实现的入口退出未来实现清单，仍保留人工验收；新增应用本地 SQL 的 ARMV4I 文件生命周期设备门、持久设置/访问日志/下载记录以及 HTTP 流式取消前提。性能人工通过事实保留，Release 性能对照仍是可选后续门。

唯一下一步：由用户在设备端确认异常启动的 guest 进程已结束或重启设备，然后运行
`scripts\device_gate.bat -Candidate module-audit -ModuleAuditOnly -PreserveDeployment`，必须取得
`module_audit holders=0 unavailable=0` 才能继续任何 DLL/DB 设备门；随后用同一批产物定向运行
TEST1321。不使用强制清理替代证据。内部页面的地址栏/菜单/history/quit 与语言、旋转人工验收仍是
独立 backlog。ROADMAP 已复核，本批只补 DB 主机断言和设备门前置审计，没有改变未来产品候选。

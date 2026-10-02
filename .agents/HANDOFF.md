# 当前交接

本文件只保存当前事实、候选、风险和唯一下一步。稳定边界见 [架构](../docs/ARCHITECTURE.md)、[能力矩阵](../docs/CAPABILITIES.md) 和 [测试合同](../docs/TESTING.md)。

## 使命与当前目标

Positron 为 WM6 / CE 5.2 ARMV4I 提供九个资源有界、稳定 C ABI 的公共 DLL 及独立消费者 positron.exe。产品语义属于 DLL，test_host 只拥有 fixture、平台接线和断言。Fragment CSS 坐标合同已验收；当前低优先级 DB 契约门已取得完整 Release PASS，Debug 包已按用户要求放到 SD 卡映射目录，但设备端读取/启动仍失败。既有 EXE 人工验收仍为 About 章节位置、Debug 构建时间和 Release CAB 安装版本，分别验收，不改变公共 ABI 或打包版本规则。

用户已确认 WinWorld 菜单展开/收起、SVG 与作者按钮外观正常；性能修正后的实际展开/收起不超过约 2 秒，期间地址栏与菜单可响应。不要恢复旧的“按钮无响应/图标仍损坏”假设；Release 点按耗时对照仍未完成，不把 Debug 体验扩大为全部设备的保证。

## 当前代码与所有权

分支 main。并行性能变更已由 df205495 提交，后续 DB 测试提交以 Git 为准；内部页面提交只包括 positron_app、专用检查脚本及其文档。共享 main.c 中原有性能计时与退出日志已保留，不纳入重复修正。本批新增的 device_tools 模块审计属于设备门前置能力，已与内部页面改动分开。

内部注册表区分 newtab/about/history/downloads/settings、version/system 别名、quit 命令及原 welcome/controls。默认入口为 newtab；内部页使用双语嵌入资源、Core 渲染、24 项有界焦点目录，无 ScriptSession 或外部请求。history 只读 Browser 导航栈；动态 HTML 上限 128 KiB。quit 的导航来源检查仅允许地址栏直接提交，沿既有 WM_CLOSE 关闭流程。设计与后端进入条件见 [接线计划](../positron_app/INTEGRATION_PLAN.md)。

## 候选与验证证据

### Core Fragment DPI 已验收与应用人工门

消费者报告 About 版本/系统章节跳转过远，源码确认 `PCore_FragmentInfoById/ByToken` 直接返回设备布局坐标，与公开 CSS px 契约矛盾。修复复用文档自身的 layout DPI 转换 x/y/w/h，不改变 ABI、EXE 或其他几何 API。测试宿主对物理滚动、modal hit/paint 与 native geometry 探针显式转换；逻辑视口 fixture 明确使用 96 DPI。组件调用说明已同步。

正式 Release 门 `tmp/device-runs/20261002-102935-fragment-dpi-acceptance/` 选择 `13,1080-1083,1101,1105,1106,1108,1311,1330,999`，结果 PASS、selected/observed 12/12、唯一 TESTBENCH PASS、零 ERROR/FAIL、无新增 dump；设备为 480×640、192 DPI，空间与无 DLL holder 审计通过，日志完整回收。TEST13 三段真实导航均完成；TEST1311 保护文字裁剪。TEST1330 在同一设备分别布局 96/144/192 DPI，覆盖章节 CSS 坐标与实际滚动后顶部像素、负坐标、ID/name 优先、隐藏/缺失/未布局失败不改输出、文档 DPI 快照隔离、native 控件/链接物理命中与重复释放。C89、仓库审计、设备门配置单测和正式 Debug/Release build 均通过。当前验收目录清理部分失败，门记录 `preserved_cleanup_failed`；不把剩余文件写成已删除。

恢复设备门时另确认两个宿主前置问题：Release `/O2` 将 fixture 内联到 `run_configured_tests`，入口栈帧约 181 KiB，超过宿主 64 KiB；仅该控制分发器关闭优化后正式 Release 恢复执行，DLL 与 fixture 优化不变。TEST1106 的结果文字 mutation 会使 retained layout 失效，旧同步探针未处理已排队的 WM 重排就继续点击；探针现在在 JS 返回后有界处理该消息，再取几何，原 cancel/close 断言保持不变。此前失败日志仍在本地，不把它们转为通过。

本仿真器的外置目录可复制完整包，但 Release 启动仍返回 device=126，早期 Debug 零日志不能证明执行；本轮有效测试使用内置 Temp。大文件 16 KiB RAPI 写入遭遇 `0x80072746` 后，正式门重开当前会话并仅重试一次 1 KiB 写入，已成功完成多次部署。没有重置设备、杀 WMDC 或强杀应用。闲置 `internal-pages-1002` 的 25 个文件及稳定日志已完整存入 `tmp/device-runs/internal-pages-1002-archive-20261002-102443/`，再精确清理以恢复空间；无法删除的剩余文件保留，备份可恢复。排错规则见 [TROUBLESHOOTING](../docs/TROUBLESHOOTING.md)。

剩余门只是真实应用 About 章节视觉复核，不重新要求 TEST232/263。ROADMAP 已复核并移除完成的 Core 候选，把 About 位置确认保留为人工 backlog。应用代码未修改；部署时必须使用同一正式 stage 的 EXE/DLL，不能沿用仍返回设备坐标的旧 Core。

About 人工复核包的本地证据在 `tmp/device-runs/20261002-103209-fragment-about-review/`。
当时完整部署后的只读模块审计和 EXE/九个 DLL 回读 SHA256 均通过；应用启动请求返回 PID，
但随后的 Toolhelp 快照不可用，不能凭 PID 宣称界面可见。设备上的该旧包已按用户清空旧部署的
授权删除，仅余无法删除的传输临时文件；继续人工复核必须重新部署，不能沿用旧路径或 PID。
这不否定此前独立宿主的匹配路径和 12 项 PASS。

### 内部页面自动门

Debug 私有启动自检使用独立 Browser history，不污染实时页面：检查所有来源的 quit 策略、别名只提交一次、16 项及前进栈、反向顺序、HTML 转义、自身过滤、容量拒绝、Core 解析/布局和 history 的 23 个焦点；另读取并解析十份中英文 RCDATA，确认 UTF-8 无 BOM、中文标题非 ASCII。夹具与诊断全部由 _DEBUG 编译隔离。

最终源码的导航门证据在 tmp/device-runs/internal-pages-delivery/navigation/：15/15，通过默认入口、各页面、大小写、末尾斜杠、about 片段与别名章节滚动、旧关键词/controls 查询参数、未知启动地址及启动 quit 拒绝；每次仅一个 history 提交、无脚本，焦点数量符合预期，crash_check=PASS。设备为 480x640、DPI192。包包含最终 Media/TLS ABI 显示；Debug 私有自检在每次启动通过，最终 PID 3811239850 留在 newtab。

外置卡候选 tmp/device-runs/internal-pages-verified/ 曾完整复制 23 文件并启动，但随后同目录文件可枚举却 RAPI 回读返回 device=5，清理工具启动返回 device=2；未归因于页面代码或声称文件已被删除。内部页面原验收使用备用内置目录 \Temp\Positron-device-gate\internal-pages-1002；首次部署前对象存储可用约 23 MiB，最终更新前仍约 8 MiB。最终完整 stage 在 tmp/device-runs/internal-pages-delivery/stage/，23 个文件全部经设备回读 SHA256 核对；旧候选二进制仅在精确清理成功后替换，未变字体因 OS 持有而不删除，直接回读确认匹配。旧性能验收 PID 已被后续清理，不再让用户寻找旧进程。

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

较早 About 版本只读审计证据在 `tmp/device-runs/20261002-084845-app-version-audit/`，
`module-audit-result.txt` 为 PASS，`holders=0 unavailable=0`；未使用强制清理。该门完整
部署 23 文件后才审计，之后直接启动同包 Debug EXE，PID 3056936870 留在 About。该次部署的设备
目录是 `\Storage Card\Temp\Positron-device-gate\app-version-audit-20261002-084845`，不代表当前人工包。
重复复制第一份 fixture 时 CeMoveFile 仍返回 device=5，未替换 EXE，改为复用已成功部署包。
About 日志在 `tmp/device-runs/app-version-delivery/verification/positron-debug.log`：版本
自检、内部页面自检、About 单次提交及 crash_check 均通过。嵌入时间为
`2026-10-02 08:48:11`；Debug/Release 正式增量构建均零错误/警告，Release 不含版本自检
文本或 Debug 时间。注册表格式/容量/终止符验证由 Debug 私有夹具覆盖；Release 在真实 CAB
安装后的读取显示仍待人工检查，独立复制缺少有效安装版本时“未提供”是预期。
较早 error=8 和宿主零日志失败不转为 TEST1321 通过；应用运行时会持有 DLL，下一次
DLL/DB 门仍须在应用正常退出后重新获取无 holder 审计证据。
guest helper 的进程快照使用 Windows Mobile SDK 的
`TH32CS_SNAPNOHEAPS`，避免包含进程 heap 时耗尽快照内存；模块枚举仍保持
fail closed。Debug 完整包部署后的
`tmp/device-runs/20261002-125344-module-audit/module-audit-result.txt`，以及最终
Release 完整包部署后的
`tmp/device-runs/20261002-131031-module-audit-final/module-audit-result.txt`，均记录
`module_audit holders=0 unavailable=0`；后者覆盖最终 `/O2` 产物和全部 9 个 Positron DLL，
部署目录保留用于诊断，未使用强制清理。定向 Debug/Release TEST1321 启动前审计也均得到
`holders=0 unavailable=0`，但 `CeCreateProcess` 分别返回 device=126，进程没有启动，
没有 `test_host.log` 或 TEST1321 结果；因此它们都是宿主加载前置失败，不计为 TEST1321
通过或测试失败。所有结果均来自 guest helper 的 Toolhelp 模块枚举，不以桌面模拟器进程
存在与否推断 DLL 引用。

审计等待函数现在忽略 RAPI 成功回读的空日志，沿原有有界等待继续读取，只有完整摘要才参与
holder 校验。`scripts/test_device_gate_audit.ps1` 直接抽取正式函数并以合成 RAPI 覆盖完整摘要、
空/部分日志后完成、读取失败后完成、非零 holder、不可用快照和无摘要超时，共 7 项通过。
内置 Temp 的实际验证见 `tmp/device-runs/20261002-175542-db-internal-audit/`：正式 Release
helper 回读 SHA256 匹配，`module_audit holders=0 unavailable=0`。这是 guest 引用审计通过，
不代表 TEST1321 已运行。外置卡曾再次出现空日志、文件回读不可用和 device=5 写入失败；
历史成功宿主在该外置卡候选中也返回 device=126，尚不足以把失败归因到某个 DLL。

### DB 主机契约测试当前切片

`test_host/db_tests.c` 的 TEST1321 fixture 覆盖本地完整 SQL 的 DDL/DML/SELECT、事务提交与
回滚、嵌套事务拒绝、取消、错误复制、NULL/INTEGER/REAL/UTF-8 TEXT/BLOB bind/column、
INT64_MIN/MAX、空 BLOB、ColumnCount，以及 handle/SQL/bind/列索引参数守卫。SQL 长度、
表达式深度、变量数和 typed bind 大小均有预算断言。完整 fixture 已取得 Release ARMV4I
设备 PASS；取消另断言短 SELECT、prepared INSERT 和直接 Exec 的一次性取消、无写入及后续可用。

local/sync migration fixture 覆盖版本幂等、倒退和事务中拒绝、失败脚本不留残表，以及已注册表
被删除时的整批回滚。同步注册覆盖单列 INTEGER/TEXT 主键、复合主键拒绝、非法/保留/过长
标识符、缺少主键、重复列、非法类型、列数和状态冲突；产品 schema 校验已统计实际主键列总数。
同步模式 fixture 拒绝 ATTACH/DETACH、直接 DDL/事务/savepoint、多语句、非法 PRAGMA 和扩展，
并检查同步文件不能以本地完整 SQL 模式重开。

同步协议 fixture 覆盖配置参数、schema version/hash、固定 envelope、无 SQL 请求、typed
INT64/REAL/NULL/TEXT/BLOB、Base64 和 JSON 转义、outbox 事务合并与回滚、请求分页、连续
多页 pull、cursor 推进，以及远端 upsert/delete 不产生本地 outbox。失败矩阵检查 accepted
后续 change 错误时整批回滚、错误 client/op/version、key 与 typed 主键不一致、未注册实体、
malformed tombstone、非 bool deleted、非法/倒退 cursor、缺少数组、响应项超限与非法 Base64；
未知顶层字段保留兼容性断言。

冲突 fixture 覆盖服务器权威行、retry-local、接受/丢弃、服务器删除与本地编辑冲突，以及冲突
列表/单项复制的 size-probe、容量、索引和 action 守卫。实体/key 与 outbox 不匹配的响应须拒绝。
文件重开 fixture 检查 outbox/cursor/tombstone/conflict 持久化、重建行采用服务器删除版本和
接受服务器结果不生成 outbox；正常关闭重开已在当前设备通过，不代表进程重启、journal/断电门通过。

正式 Debug/Release build、C89 和仓库审计通过。用户明确授权
强行清空旧部署后，精确处理两个 Positron-device-gate 根下 98 个旧目录：73 个完整删除，
25 个仅余字体和 `.part-*` 文件，清属性后删除及再次复查仍失败。本地既有证据保留；本轮
清理证据在 `tmp/device-runs/20261002-200329-db-force-clear/`，不把残留目录写成已清空。
内置可用空间恢复到 23,083,008 字节，原空间阻塞已解除，没有强杀进程或重置设备。

首次文件打开的零字节/无终态问题已定位：SQLite 的 no-WAL/no-mmap Win32 syscall 表没有
初始化 `CreateFileMappingW`，WinCE 文件锁却仍调用它，产生地址零的访问异常。DB 端通过 VFS
系统调用接口恢复匿名锁状态映射，不修改上游快照、不启用 WAL 或数据库 mmap。另修正短 SQL
取消、空 BLOB 绑定和 authorizer 拒绝的公共错误码。宿主只新增阶段及继续传播异常的日志；分页
夹具的 32 KiB 缓冲移出 64 KiB 栈，容量和 65 行断言不变。夹具先注册表再验证 DROP 回滚；模拟
响应修正 BLOB bytes 和 retry-local 的新 op_id，并加强新 ID/服务器 base_version 断言。

正式 Release 门 `tmp/device-runs/20261002-205124-db-test-1321-final-release/` 使用匹配的
23 文件 stage，选择 `1321,999`，完整日志为 selected/observed 2/2、唯一 TESTBENCH PASS、
零 ERROR/FAIL，Core 路径与当前包一致、crash_check=PASS、无新增 dump；部署前 guest 审计
`holders=0 unavailable=0`。较早诊断超时/异常/断言失败日志仍在本地，不转为通过。该设备包在
日志完整回收后已按用户授权精确删除可删除部分，仅余删除失败文件；当前有效产物/日志在本地
stage 和证据目录，不让用户沿用已清理的设备 EXE 路径。

Debug 正式 build/stage 成功，DB 零错误/警告；宿主仅有原 libcss fpmath 的三条 C4244。
内置门 `tmp/device-runs/20261002-205254-db-test-1321-final-debug/` 因空间不足未部署。
用户改为 SD 卡后，正式门的外部卷容量检查通过，但首个 fixture 的 RAPI 写入及一次重试均
返回 device=5。随后通过正式 `stage.bat Debug` 写入已确认的共享映射
`C:\WMShare\Temp\Positron-device-gate\db-test-1321-sd-debug-20261002-205953`，对应设备
`\Storage Card\Temp\Positron-device-gate\db-test-1321-sd-debug-20261002-205953`；23 个文件
在桌面映射侧 SHA256 匹配原 stage，INI 仍选择 `1321,999`。设备枚举也可见 EXE/九个 DLL
及匹配尺寸，但 RAPI 无法回读宿主，`CeCreateProcess` 返回 device=2；无 PID 或测试日志，
不能把文件存在或桌面 hash 当作设备运行验证。包保留，证据在
`tmp/device-runs/20261002-205953-db-test-1321-sd-debug/` 的 `sd-shared-check.txt` 和
`sd-guest-inventory.txt`。启动前内部 helper 审计仍为 `holders=0 unavailable=0`。
未强杀、重置、改共享设置或回退内置存储；旧字体及 `.part-*` 删除失败事实仍有效。

自动门不替代人工验收：地址栏直接输入/未知地址恢复原标题和地址、菜单、history 点击与刷新、直接 quit 和加载中 quit、中英文实际显示、触摸、键盘焦点、软键、滚动、旋转及 DPI 尚待确认。页面能力不应写成全部人工门通过的正式设备基线。

### 保留的性能基线证据

性能包 tmp/device-runs/debug-capture-20261001-234236/ 的自动 jQuery/Bootstrap 展开→收起→再展开终态 height=404/0/404、ok=true；style=8/7/9、layout=3/3/5、controls-refresh=13/15/16、paint=1/1/2 guest ms。普通点按的 style 约 79–103 guest ms，不能用自动探针耗时替代用户墙钟。

最终 resolver 切换门 tmp/device-runs/20261001-234106-css-cache-resolver-handoff-final/ 为 24,45,1327,999 的 4/4、唯一 PASS、零 ERROR/FAIL、无新增 dump；此前相邻 11 项门不代表后续源码全量重跑。TEST45 fixture context 错配的失败候选仍在 tmp/device-runs/20261001-230333-css-parse-cache-perf/，失败已经修正，不恢复旧 callback 接法。

## 有效边界与设备纪律

HTTP final URL、Core 资源终态和现有 SVG 能力继续有效；bootstrap-multiselect 语法边界、module/Shadow DOM、横向滚动条暂缓、SIP/IME/OEM 等见 [限制](KNOWN_LIMITATIONS.md)。内部 settings/downloads 只是诚实的只读说明，不代表配置或下载管理已经实现。DB 已通过 Release 文件关闭重开与完整离线契约；真实 HTTPS worker、应用持久化、HTTP 流式下载及断电恢复仍未进入本批。

WMDC 连接由用户手动完成，只使用当前唯一目标；新部署不覆盖诊断包。精确清理必须取得 helper 成功摘要，不能杀 WMDC、VS GUI 或其他程序。外置卡失败时可检查空间后使用内置 Temp；日志回收前不删除目录。只在用户告知新截图时查询截图，不以旧截图推断新运行。

## 路线图复核与唯一下一步

ROADMAP 已复核并收窄 DB 设备缺口：Release 完整离线合同和正常文件关闭重开已通过；Debug
设备复测、空间不足/进程重启/journal 恢复、HTTPS worker 和应用持久设置/访问日志/下载记录仍待
完成。本轮 SD 部署未产生 Debug 运行结果，这些候选不退出 ROADMAP；性能人工通过事实保留，
Release 性能对照仍是可选后续门。

唯一下一步：由用户在设备 File Explorer 检查上述 SD 目录中的 `test_host.exe` 是否可打开，
或确认共享卡映射已恢复可读/可执行，再重新检查编译竞态和 guest DLL 引用补 Debug 门。
遵守用户的 SD 目标，不自行回退内置、重置、强杀或改共享设置；不把可枚举文件或已通过的
Release 合同改写为完整 Debug/生产设备基线。
About 章节位置、Debug 时间、Release CAB 安装版本，以及内部页面的地址栏/菜单/history/quit
与语言、旋转人工验收仍为独立 backlog；旧 About 包已清理，复核须部署匹配的新包。

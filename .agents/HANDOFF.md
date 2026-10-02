# 当前交接

本文件只保存当前事实、候选、风险和唯一下一步。稳定边界见 [架构](../docs/ARCHITECTURE.md)、[能力矩阵](../docs/CAPABILITIES.md) 和 [测试合同](../docs/TESTING.md)。

## 使命与当前目标

Positron 为 WM6 / CE 5.2 ARMV4I 提供九个资源有界、稳定 C ABI 的公共 DLL 及独立消费者 positron.exe。产品语义属于 DLL，test_host 只拥有 fixture、平台接线和断言。当前 DLL 修正目标是 Fragment CSS 坐标契约；既有 EXE 验收目标仍为 About 的 Debug 构建时间和 Release CAB 安装版本。两者分别验收，不改变公共 ABI 或打包版本规则。

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

人工复核包在 `\Temp\Positron-device-gate\fragment-about-review-20261002-103209`，对应本地 `tmp/device-runs/20261002-103209-fragment-about-review/`。完整包部署后只读模块审计 PASS，EXE 与九个 DLL 回读 SHA256 匹配 stage；随后请求 `positron.exe --url positron://about`，返回 PID 2519828882。启动后的 Toolhelp process snapshot 两次返回 error=8，故没有取得应用已加载 Core 的路径证据，也不凭 PID 宣称界面可见；由用户确认 About 是否显示及章节位置。该启动后快照限制不否定此前独立宿主的匹配路径和 12 项 PASS。

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
最新只读模块门 `tmp/device-runs/20261002-122228-module-audit/module-audit.log` 仍记录
`module_audit_unavailable scope=process_snapshot error=8` 和
`holders=0 unavailable=1`；部署完成后在启动 `test_host` 前停止，未使用强制清理。

### DB 主机契约测试当前切片

TEST1321 本批补齐了 schema 迁移失败后的残留表检查，并修正/断言同步注册拒绝复合主键；
`positron_db` 的 schema 校验现在统计实际主键列总数，v1 只接受单列 INTEGER/TEXT 主键。
同时补充 `INT64_MIN/MAX`、非空与空 BLOB 的 bind/column 读取、`ColumnCount`，以及同步请求
BLOB Base64 编码和服务端响应解码断言；新增未知顶层响应字段兼容性、重复/倒退/事务中迁移
拒绝、schema 配置版本拒绝、事务中禁止构造同步请求，以及同步 REAL/NULL outbox 编码和
pull 解码、同步文件拒绝本地完整 SQL 模式重开和 `load_extension` 拒绝的断言。正式
`scripts\build.bat Debug build`、C89 和仓库审计均通过；由于当前设备模块审计不可用，
本切片尚未由设备上的 TEST1321 执行确认。另补 dirty queue/outbox 事务合并、最终
upsert/delete 选择和回滚不留 outbox 的主机断言。
失败响应还覆盖了 accepted 后续 change 类型错误时的整批回滚，确认 outbox、cursor 和本地行
不会被部分提交。随后继续补充了同步表注册边界（非法标识符、保留名、缺少主键、重复列、
非法类型、非 INTEGER/TEXT 主键、重复注册和注册状态冲突）、冲突列表/单项复制的
size-probe、过小缓冲、越界索引和非法 resolve action，以及同步模式 `DETACH`、多语句和
直接 `BEGIN`/`COMMIT`/`ROLLBACK` 的 SQL 拒绝断言。最新正式 Debug 构建、C89、仓库审计均通过；这些仍是 host contract
证据，设备模块快照不可用期间不计入设备 TEST1321 通过。
冲突失败 fixture 另覆盖实体名和主键分别不匹配时拒绝响应且保留本地 outbox；服务器权威冲突
和 retry-local 的既有断言不受影响。
另外补充公共 C ABI 的参数守卫：空 handle/SQL、无效 bind/列索引，以及本地 SQL 模式调用
同步入口的拒绝结果均由 host fixture 断言。
同步迁移还补充了已注册表被迁移脚本删除时的 schema 校验失败与原表保留断言，覆盖了
注册同步表之后的迁移原子回滚边界。
同步配置守卫也覆盖空/负参数以及 client ID、schema hash 的固定长度上限，非法配置不会
写入同步元数据。
同步表注册守卫继续覆盖表名/主键长度、空列列表、零列和超过最大列数的拒绝。
本地完整 SQL fixture 还补了独立的 UPDATE/DELETE/SELECT COUNT round-trip，覆盖完整 SQL
模式的删除路径。
pull response fixture 还覆盖了 `changes` 中 key 与 typed 主键值不一致时的整批拒绝，确认
远端行、outbox 和 cursor 不会部分推进。
随后又加入 SQL 32 KiB 和 typed bind 1 MiB 预算、固定 sync request envelope，以及服务器
tombstone 在关闭/重开后作为重新创建行 `base_version` 的断言；仍未把 host 构建或静态证据
当作设备运行通过。另补本地完整 SQL 模式的 migration 版本幂等、失败脚本回滚和事务中
拒绝断言；路线图中的宿主 worker、HTTPS 与设备 journal/断电门没有因此提前标记完成。
同步响应随后增加连续多页 pull、游标推进和远端行落库且不产生 outbox 的 host 断言；设备
模块审计未恢复前仍不启动该宿主门。
最新 host 批次还把本地 typed text fixture 改为 UTF-8 字节串，并通过 bind/column round-trip
验证；不代表设备上的字体、编码显示或数据库文件恢复已通过。
随后补充服务器删除与本地编辑冲突：host 断言 `server_values:null`、本地 tombstone、接受
服务器结果不生成 outbox，以及同主键重建使用服务器删除版本。
响应失败矩阵又加入 conflict entity/key 与本地 outbox 不匹配时的拒绝，确认 outbox、cursor
和本地行均保持不变。

自动门不替代人工验收：地址栏直接输入/未知地址恢复原标题和地址、菜单、history 点击与刷新、直接 quit 和加载中 quit、中英文实际显示、触摸、键盘焦点、软键、滚动、旋转及 DPI 尚待确认。页面能力不应写成全部人工门通过的正式设备基线。

### 保留的性能基线证据

性能包 tmp/device-runs/debug-capture-20261001-234236/ 的自动 jQuery/Bootstrap 展开→收起→再展开终态 height=404/0/404、ok=true；style=8/7/9、layout=3/3/5、controls-refresh=13/15/16、paint=1/1/2 guest ms。普通点按的 style 约 79–103 guest ms，不能用自动探针耗时替代用户墙钟。

最终 resolver 切换门 tmp/device-runs/20261001-234106-css-cache-resolver-handoff-final/ 为 24,45,1327,999 的 4/4、唯一 PASS、零 ERROR/FAIL、无新增 dump；此前相邻 11 项门不代表后续源码全量重跑。TEST45 fixture context 错配的失败候选仍在 tmp/device-runs/20261001-230333-css-parse-cache-perf/，失败已经修正，不恢复旧 callback 接法。

## 有效边界与设备纪律

HTTP final URL、Core 资源终态和现有 SVG 能力继续有效；bootstrap-multiselect 语法边界、module/Shadow DOM、横向滚动条暂缓、SIP/IME/OEM 等见 [限制](KNOWN_LIMITATIONS.md)。内部 settings/downloads 只是诚实的只读说明，不代表配置或下载管理已经实现。DB 文件数据库设备门、HTTP 流式下载与持久化依赖仍未进入本批。

WMDC 连接由用户手动完成，只使用当前唯一目标；新部署不覆盖诊断包。精确清理必须取得 helper 成功摘要，不能杀 WMDC、VS GUI 或其他程序。外置卡失败时可检查空间后使用内置 Temp；日志回收前不删除目录。只在用户告知新截图时查询截图，不以旧截图推断新运行。

## 路线图复核与唯一下一步

ROADMAP 已复核：内部页面已实现的入口退出未来实现清单，仍保留人工验收；新增应用本地 SQL 的 ARMV4I 文件生命周期设备门、持久设置/访问日志/下载记录以及 HTTP 流式取消前提。性能人工通过事实保留，Release 性能对照仍是可选后续门。

唯一下一步：在匹配 Core 修复包的 About 页面分别点击版本/系统章节链接，确认标题位于视口顶部附近而不跳过目标；页面底部不足一屏时允许正常滚动钳制。Debug 时间和 Release CAB 安装版本显示仍是独立应用验收，不把 Fragment 自动门视为它们通过。

若继续 DLL/DB 门，先正常退出应用并重新运行只读模块审计，再用匹配产物定向运行 TEST1321；本轮已有无 holder 结果，但不跨应用启动复用，DB 测试仍未执行。不使用强制清理替代证据。内部页面的地址栏/菜单/history/quit 与语言、旋转人工验收仍是独立 backlog。

# Runtime 的 WM 平台与目录应用宿主

本文只说明 EXE 私有 `app_runtime_platform.c/.h`、`app_runtime_app.c/.h` 的平台接线与边界；应用身份、manifest、
资源引用、迁移、SQL 和页面服务合同由公共
[Runtime](../positron_runtime/README.md)拥有。目录应用的整体设计见
[运行时计划](../docs/RUNTIME_PLAN.md)，实际进度和设备证据见
[HANDOFF](../.agents/HANDOFF.md)，不能用本文的调用说明代替设备验收。

## 目录应用入口

```text
positron.exe --app-dir "\Storage Card\Apps\Example"
```

目录必须独立放置根 `manifest.json` 及资源；路径支持引号和 Unicode，不使用工作目录。
该入口与位置 URL、`--url`、`--eval`、`--click` 互斥。原有无参数/URL 启动不变。
`permissions:[]` 只运行资源页；声明 `storage.sql` 时，确认框另外说明独立 SQL 库及迁移。
用户拒绝不打开存储或执行迁移，不忽略所需能力后继续执行作者代码。

UI 调用私有 `AppRuntimeApp_Start` 创建独立 owner worker；它打开目录实例，从根引用
`/manifest.json` 复制已准入字节。UI 用这份字节创建自己的不可变 metadata resolver，
不接收 live 目录 handle。UI 显示名称、规范 ID、版本、未验证发布者及启用 JavaScript 的
确认，默认选择否；用户同意后才在原窗口首标签加载入口。缺失/非法/所需能力未接通时不
运行作者代码、不执行迁移、不删除文件；取消后仍可普通浏览。打开期间 UI 正常处理消息，
不在 DLL/callback 内泵消息；用户已导航或换标签则不迟到覆盖该操作。

确认只绑定该原始标签和存活实例，不按 URL 授权新标签或普通网页。目录页面的资源引用
统一走 Runtime 的同 ID 解析；HTTP(S) 仍走原网络路径，应用远端资源不转交 HTTP。
用户在原标签显式切换 HTTP(S)/内部页可以普通浏览，外部网页脚本或链接不能反向进入应用；
其他标签和未显式启动的进程不能仅凭 `app://` 获得实例。跨 ID、逃逸和不支持引用拒绝。
无 SQL grant 的页面不安装原生服务或 SDK；有 grant 的页面只安装 `storage.sql`，仍不是安全沙箱。

页面继续使用原 Browser candidate/resource transaction、Core stylesheet/import/image cache、
ScriptSession、history 和控件。目录应用 JS 在可信确认后显式启用，不改变网页 JS 偏好，
也不扩大 heap/source/执行预算。必需 CSS 失败保留旧页；可选脚本/图片失败沿既有 fallback。
query/fragment 只保留为 URL，不拼入物理文件名；嵌套 entry 不改变根 manifest 位置。
目录资源使用 Browser `SetDataEx`，READY/0 按状态而非正长度处理。Core fetch 取得至少一字节
存储空间但正文长度仍为零；CSS/import 缓存和 ScriptResourcesEx2 接受空内容，脚本按明确长度
零执行为 no-op，不补空格/NUL。空图片终态 fallback，空 HTML 主文档仍不接入；原 HTTP 正长度
策略不变。合同背景见[空资源请求](RUNTIME_EMPTY_RESOURCE_REQUEST.md)。不新增跨文档初始片段定位或百分号解码。

导航 worker 向 owner 发送单项有界复制读取请求，串行等待结果；只有 owner 调用目录 API，
UI fetch callback 只读取资源事务已完成的字节。style/layout/paint 与脚本保持 UI owner，
resize 重用已缓存资源及 UI 的纯 metadata resolver，不做文件 I/O。原标签关闭后撤销实例
入口，导航/退休 worker 收敛后关闭目录 owner，UI 非阻塞确认退出再释放状态；不强杀线程。
原窗口、地址栏、软键、native 控件与滚动条不因目录页面换成第二套 UI。

## SQL 授权、页面队列与 SDK

确认 SQL 后，UI 只发出 grant 并处理正常窗口消息；owner worker 通过
`PRuntime_DevelopmentStorageOpen` 打开 EXE 旁隔离存储并运行声明迁移。失败不启动作者代码、
不删库、不切换目录、不重放 SQL。live storage/DB 不离开 worker，取消回调只读取原子关闭状态。

每个候选有独立 tab/generation 绑定；worker 创建 Runtime 队列，UI 在 bootstrap COMPLETE
之后、作者脚本之前仅注册 `storage.sql`，再执行 EXE 内嵌的 `positron-sdk-v1.js`。
队列准备期间返回消息循环，不在 submit callback 等 DB。SQL 必需能力初始化失败保留旧页，
不会降级成无服务页面；普通 HTTP(S)/内部页/新标签不因 URL 获权。

SDK 提供 `Positron.apiVersion === 1`，以及：

```javascript
Positron.db.query('SELECT body FROM notes WHERE id=?',
    [{type: 'int64', value: '9007199254740993'}], function (ok, result) {
        // result 使用 Runtime typed rows 或 sanitized error，不转 int64 为 Number。
    });
Positron.db.execute(sql, typedParams, callback);
Positron.db.transaction([{kind: 'execute', sql: sql, params: typedParams}], callback);
```

callback 为 `(ok, result)`，不要求 Promise；同步参数/容量/队列拒绝抛错，不自动重试。
SDK 只映射固定方法，不解析 SQL、不选择数据库或身份。Runtime/DB 拥有 JSON、authorizer、
事务、预算及结果；请求/结果 4096 字节、八个未完成请求、每请求百万 VM 指令等限制不扩大。

owner 使用 event 唤醒，每轮处理各队列一项；有进展再自唤醒，空闲不轮询。
UI 的原脚本 timer 每次取一份结果，核对 tab/generation/token 后 Complete/Pump，才执行 callback；
每个 context 固定一份约 4 KiB 复制缓冲，不在空闲 tick 分配。最多四份 Runtime 页面队列，
约 70 KiB/份，旧页保持到实际替换。候选启动本身不撤销旧页；离页/失败销毁/关闭先撤销
Browser 桥和 Runtime 队列，停止 UI 访问后由 worker 关闭队列，再关闭 storage/directory。
取消与 COMMIT 竞争可能丢弃已提交结果，不能通过重放 SQL 恢复；普通页面失败不撤销已提交 SQL。

[Notes 示例](examples/notes/README.md)只包含作者资源，复用普通页面与 native 控件。
SDK 是内嵌资源，不需要额外 staging 语言或 JS 文件；示例须单独复制，不自动安装或扫描磁盘。

ScriptSession 使用公开 DOM write Ex8 表：innerHTML 及其前置 Text/CharacterData 写入
均薄接对应 Core 原语，成功后请求既有延迟重排；Browser 负责 wrapper 失效与重建。
SQL callback 内生成列表也走同一路径，不在 EXE 解析 HTML 或替作者维护 DOM。
服务结果/回调失败的原生阶段、返回码和 Browser 错误只写 Debug 日志，Release 不包含诊断。

## 目录页面自动门

用正式 `device_gate.ps1` 的 `app-runtime-page` Debug 完整包运行
`scripts/app_runtime_page_gate.ps1 -RunRoot <本轮正式目录>`（32 位 PowerShell）。
显式 Debug `--selftest-runtime-app` 只接受该专用部署中的 `fixture` 根，跳过确认仅供隔离
自检；Release 没有此入口。夹具不进入普通 staging 或 CAB。

测试通过生产参数解析器验证八项 Unicode/引号/重复/互斥参数；真实页面验证嵌套 entry、
inline/external 顺序、相对 CSS/import、SVG cache、可选失败、必需失败保留旧页、query/fragment、
新标签拒绝、取消/退休迟到隔离和 owner 关闭。EXE/十 DLL 哈希、正常退出零引用及 crash 检查
仍必需；另外验证三个真实零字节资源 READY/0，以及空外部脚本确为 available/no-op。
它不代替原生确认的人工 Yes/No、中英文、实际触摸、旋转或 DPI，也不证明 SQL SDK。

SQL 另用正式 `app-runtime-sql` Debug 完整包与 `scripts/app_runtime_sql_gate.ps1`：
原生 `--selftest-runtime-sql` 只接受其精确 `fixture-one/two` 根，Release 排除诊断。
两根同 ID 目录各两个独立进程，每根 32 条记录，验证重开/数据隔离、typed int64/中文/引号、
16 行分页、事务回滚、超限/DDL 拒绝、八项队列、旧页失败后服务仍有效、离页撤销/返回新队列、
有请求时关闭 owner 和正常退出零引用；真实 SQL callback 还生成带按钮和转义文本的 HTML 列表。
默认两根四进程不变；显式 `-Roots one` 或 `two` 可单独取证，结果标注所选根、两进程及
`same_id_roots=NOT_RUN`，不能替代失败的双目录隔离门。每次使用新 EvidenceName，保留旧失败。
该门不修改用户库，也不代替 Notes 真正触摸/编辑验收。

真实 Notes 另用同类正式 Debug 完整包与 `scripts/app_runtime_notes_gate.ps1 -RunRoot <正式目录>`。
`--selftest-runtime-notes` 仅接受该包中精确的 `notes-one/two` 根，Release 排除入口和断言。
脚本要求全新根，将原 Notes 资源复制到独立目录，仅为测试 HTML 追加驱动脚本；
不修改示例源码或已有用户数据。夹具通过原按钮事件驱动增删改查、标题/正文搜索、
四页及 Previous，种入 25 条并保留 26 条记录；两根同 ID 按 one/two/one/two 正常退出重开，
同时核对各自内容和另一根数据不可见，配合 11/11 哈希、双空间、零引用和 crash 门。
这是一组新的 Notes 集成证据，不追认旧 SQL 启动失败已修复。
结束后恢复原始示例 HTML，测试脚本不再被引用；`-OpenManual` 普通启动第一根供
[集中人工验收](examples/notes/README.md#集中人工验收)，仍需用户确认权限。脚本触发事件
不证明真实触摸、SIP、旋转或 Release 普通启动已通过。
该自检另写一份不足 448 字节的原生终态文件 `\Temp\positron-notes-result.log`，
脚本清除已保留的旧文件并严格匹配新 PID，正常退出后再收取原 Debug 日志。
通用日志仍保持 128 KiB 上限，不以大量控件事件挤掉终态或扩大日志预算；
所有进程退出后等待系统 dump writer，再核对 crash 清单。

## 调用与所有权

在拥有目录实例和存储的 worker 上调用 `AppRuntimePlatform_Init`，再通过
`AppRuntimePlatform_DirectoryProvider`、`AppRuntimePlatform_StorageProvider` 取得公共
callback table。`AppRuntimePlatform` 必须活到全部回调结束；Runtime 负责按合同释放
借用字节、存储 location、目录 root 和独占 lease，EXE 不接管 Runtime handle 的语义。
先关闭页面队列和存储，再关闭目录实例；不能把这些 handle 交给 UI 或另一个 worker。

适配器不解析 manifest、不维护应用 ID 白名单，不拼 SQL，不重新计算开发身份或 storage key。
不在普通启动时扫描磁盘、授予权限或运行测试；Release 不提供诊断命令。

## 目录与读取

- 只接受有界的本地绝对根路径；逐级枚举真实长文件名，拒绝相对路径、空/dot 段、尾部点或
  空格、通配符、原生 stream/device/UNC 形状、别名及可检测的 reparse/link。
- 准入时递归扫描，保留每个普通文件的只读句柄，不分享写入/删除；读取来自保留句柄，
  不按作者路径重新打开。读取前后核对原生文件身份、大小及修改时间，新增文件不进入本实例。
- 包内 key 已由 Runtime 解码并验证；平台只做宽字符路径转换和真实条目核对，不再次解码 URL。
  bytes 是明确长度的二进制数据，不追加 NUL；超限或 I/O 失败释放临时缓冲，不发布部分结果。
- 当前 EXE provider 最多保留 128 个文件，目录递归最多 16 层，原生路径受 `MAX_PATH` 约束；
  超限拒绝整个目录。该宿主上限小于未来 PXI 的 1024 文件设计上限，不代表安装预算已验收。
  单资源仍受 Runtime 和 Core/Browser 更严格预算约束。
- 独占锁使用 Runtime 给出的完整不透明名字创建原生 named mutex；已存在就返回 BUSY，
  不加 PID 后缀、不等待，也不因重复启动而绕过身份。

这些规则适用于明确确认的可信开发目录，不是恶意代码沙箱，也不证明所有 ROM、映射卷或
不可检测的文件系统别名都具有相同保证。原生能力不足或身份无法验证时拒绝，不退回词法前缀检查。

## 存储 location

从实际 EXE 路径取得同目录 `data` 根，逐层创建和核对 Runtime 提供的相对 storage key。
`ERROR_ALREADY_EXISTS` 不能单独证明目录存在；不得重试失败 mkdir、切换卷、删除数据库或重放 SQL。
保留根 anchor 的只读句柄至 location 关闭；不预先打开或创建 `app.db`，DB 的完整身份核对和
创建/迁移仍由 Runtime/DB 完成。已存在叶必须是实际包含的普通文件；父目录验证后，只在精确
枚举明确报告未找到时允许创建。CE 的无效属性值加错误码 0 不足以分类为文件不存在。

原生路径和诊断只写入 Debug 日志，不交给作者 JS；浏览器的 `positron.db` 不参与此映射。
正常重开不证明文件 FULL、I/O 故障、真实断电恢复或旧 ROM 的间歇存储故障已经解决。

Debug owner worker 为 `PRuntime_DevelopmentStorageOpen` 初始化完整 v1 错误快照，
`result=-32767` 表示尚未被 API 写入；记录 open-begin，返回后立即记录实际 rc、Runtime/DB
分类、SQLite 主/扩展码、失败迁移版本、cleanup 结果及最多 255 字节 message，再发布原状态。
不能由清理后的 GetLastError 猜测 DB 错误，也不能把未写快照的零字段当作成功。
Release 不含这些诊断。仅显式 Debug SQL/Notes 自检在存储启动失败时记录 FAIL 并走正常 WM_CLOSE，
普通启动仍显示原本地化错误框；两者都不启动作者代码或自动重试存储。

[app_runtime_storage_probe.ps1](../scripts/app_runtime_storage_probe.ps1) 用 32 位 PowerShell 接受原正式 SQL `-RunRoot`、
正式 Debug `-DiagnosticStage` 和新的 `-EvidenceName runtime-storage-...`，只复测原 `fixture-two`。
工具先检查双空间/零引用、原 EXE 与十 DLL 哈希及夹具，再冷备份 data 树（深度 5、总条目 256）。
诊断 EXE 另名放在原父目录，保留原镜像与存储位置；结束后正常退出审计、冷备份与 crash 检查。
结果区分 PAGE_PASS 和 CAPTURED_FAILURE，二者均不代表跨目录隔离通过；原失败证据不覆盖，
不删除/替换数据库、不改身份或卷、不强制结束用户应用。

## 显式 Debug 平台门

通过正式 `scripts/device_gate.ps1 -Candidate app-runtime-platform -Configuration Debug
-ModuleAuditOnly -PreserveDeployment` 建立全新完整包后，用 32 位 Windows PowerShell 运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/app_runtime_platform_gate.ps1 -RunRoot <本轮正式目录>
```

脚本调用 EXE 的 `--selftest-runtime-platform "<目录>"`，在独立 worker 消费公共 Runtime API；
两份中文目录各运行两个独立进程，每进程关闭重开两次，覆盖中文资源、重复实例 BUSY、文件写入
拒绝、路径逃逸失败不改输出、无授权拒绝、显式 SQL grant、受限 SQL 和正常恢复。该重复实例检查
发生在同进程，不冒充两个同时运行进程的 lease 验收；原生 mutex 的跨进程场景仍需专门组合门。

包内 EXE/十个 DLL 必须回读哈希一致。终态后只允许精确测试 PID 在 30 秒内正常卸载，其他
holder、不可用快照和超时均失败，最终要求零引用且无新 dump。测试证据不可覆盖；可用
`-EvidenceName app-runtime-platform-<标识>` 保留新的尝试，已存在夹具只读核验，不覆盖旧数据。
脚本只改变隔离部署目录与专用 Debug 日志，不强杀、不操作 WMDC、不改用户库；失败日志保留。
`scripts/test_app_runtime_platform_gate.ps1` 离线覆盖等待退出的成功、拒绝和超时路径，不访问设备。

这不是作者页面、Core 资源事务、跨页面队列、真实输入或 SDK 的端到端门；它们必须在目录
应用接线后独立验收，不能用此平台门代替。

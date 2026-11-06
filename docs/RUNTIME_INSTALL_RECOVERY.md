# 首次安装存储与登记恢复合同

本文件定义revision 1首次安装与存储恢复合同；公共DLL和EXE冻结目录/PXI首次安装接线均已提供。
公共入口、状态和预算见[Runtime安装合同](../positron_runtime/INSTALLED.md)，平台调用见
[EXE宿主](../positron_app/RUNTIME_HOST.md)，实际自动/人工证据归[交接](../.agents/HANDOFF.md)。
本阶段不提供更新、数据降级、自动恢复旧安装数据或物理断电保证；更新的后续合同见
[更新计划](RUNTIME_UPDATE_PLAN.md)，旧ABI和开发目录行为不变。

## 为什么需要独立安装意图

现有 `PRuntime_DevelopmentStorageOpen` 绑定完整 development 身份，读取全部 pending 来源后按文件调用 `PDb_ApplyMigrationRestricted`。每个文件的 DDL、DML 和 schema version 在一个事务中提交；不同文件不是一个事务。`PDb_BindLocalIdentity` 本身也有独立提交点。它们不能与 EXE 登记库组成一个原子事务。

现有 `PRuntime_OpenInstalled` 从可信 provider 读取有效记录，并核验完整资源和批准快照。它没有“正在安装”的身份，也不能在迁移失败时自动撤销外部登记。先填有效记录再准备数据库，会给半完成安装留下可启动窗口；直接用 development opener，则会使用错误的数据身份。

采用单向首次安装协议：**先提交不可启动意图，再准备安装数据库，最后提交 READY 登记。** 安装意图和有效记录存于同一登记数据库，由原生 provider 原子比较并提交；Runtime 拥有状态与核验规则。EXE 只接主库事务、文件 I/O、用户决定和 worker 调度，不在宿主复制恢复分类。

## 身份与持久记录

可信原生安装流程分配 installation ID，沿用非零 32 位小写 hex；已占用的逻辑ID拒绝重复安装；未来经明确卸载后的重新安装须分配新身份，不自动采用旧库。首次安装 revision 固定为 1，app version 取 manifest。批准仍是完整接受或拒绝，未知能力/API/策略拒绝。即使不申请 SQL，也必须明确确认发布者未验证及执行代码的风险。

安装意图需要持久化以下已核验字段：规范 app ID、installation ID、revision、app version、API/digest/approval-policy、requested/approved bits、content ID、资源规范根、目标 schema version、记录状态与 generation。目标 schema 是 manifest migration 数量，不是 EXE 猜测的字段。保存结构化字段，不落库 C struct padding 或句柄。

每个 installation ID 至多一条当前记录；规范 app ID 在 pending/READY 间也排他，防止两个安装竞争同一逻辑名称。记录 generation 在每次状态变更时递增，范围 1..2147483647，溢出拒绝，不回绕。CAS 必须比较预期状态、generation 和完整身份/批准快照，不只比较 app ID。失败/撤销记录作为墓碑保留 installation ID，不能通过删除记录重新认领旧数据。

### 安装数据库映射

Runtime 生成 native-only 相对 key：`v1/installed/<installation-id>/app.db`。可信 storage provider 将其绑定到 EXE 邻近的真实 `data` 根，证明包含性并保有位置；失败不回退浏览器库、development 目录或其他卷。

DB 的完整 TEXT stamp 固定为 `Positron.storage.v1:installed:<canonical-app-id>:<installation-id>`，不含 NUL。ID 的现有语法不含冒号，字段无歧义。通过 `PDb_BindLocalIdentity` 在写锁内绑定/比较；不是有限 bucket 或 URL。revision、app version、根路径、content ID 与 generation 不进入稳定数据身份。错误 stamp、非空未认领库、未来/损坏 schema 均拒绝，不删库或重绑。

绑定只用于 PREPARING 的数据准备。READY 普通启动需要纯比较且禁止首次认领：现有 `PDb_BindLocalIdentity` 会为 pristine DB 写入缺失 stamp，不能直接拿它作为 require-existing 核验。DB已提供VerifyExistingLocalUtf8的只读快照，以及OpenExistingLocalUtf8的可写既有句柄准入。后者保留只读快照，在无CREATE的实际可写连接取得BEGIN IMMEDIATE后重新核验身份与精确schema，再返回空闲句柄；不调用元数据初始化或首次认领。原生provider须贯穿句柄寿命保有物理路径与协调租约，DB核验不能替代它。旧OpenUtf8仍有CREATE/元数据初始化，不能在探针后用它冒充既有库准入。stamp缺失失败，不能因文件存在且为空就补写。原生位置 provider 还须保有已有文件，避免检查存在后被替换；SQL 身份比较不能替代文件包含性。

没有 `storage.sql` 的安装不创建数据库，也不产生 SQL 服务。无迁移但声明 SQL 的安装仍绑定独立库，目标 schema 为 0。首版不恢复同 ID 墓碑中的旧数据；未来恢复或更新必须另有确认与数据协议。

## 登记状态与转换

下列是持久登记状态，区别于只读 job 的 INITIALIZING/PENDING/COMPLETE，也区别于一次 worker 调用的成功/失败。

| 状态 | 含义 | 是否可启动 |
| --- | --- | --- |
| PREPARING | 确认绑定的资源已核验，安装意图已 COMMIT；数据尚未证明就绪 | 否 |
| READY | 资源与批准一致，所需数据库身份及目标 schema 已证明；最终登记已 COMMIT | 仍须本次资源/存储核验成功 |
| FAILED | 已确认未完成，保留原错误和可能存在的已提交迁移前缀 | 否 |
| CANCELLED | 已确认取消且未进入 READY；资源/数据仍可能需要保留或清理 | 否 |
| REVOKED | 已撤销授权的墓碑 | 否 |

首次意图只能从“该身份不存在且逻辑名称未被占用”进入 PREPARING。正常路径为 PREPARING→READY。已知失败/取消可从 PREPARING 转到 FAILED/CANCELLED；不能把 READY 通过取消回调改成 CANCELLED。首次安装的显式修复重试只允许 FAILED→PREPARING，必须重新取得用户决定、核验**同一** installation ID、资源内容与批准，递增 generation；不提供自动循环重试。CANCELLED/REVOKED 不自动复活。

READY 是唯一有效安装指针，没有第二个竞争的 `active` 文件。现有 v1 registry adapter 只有遇到 READY 才能返回 `PRuntimeInstalledRecord.active=1`；其他状态返回 DENIED，不通过修改旧 `active` 含义兼容 pending。后续安装协调使用独立版本化 provider，不向现有结构插入字段。

## 首次安装执行顺序

1. 在隔离输出中完成 Archive CRC/长度校验、路径/资源预算与物理包含性，冻结资源根。Archive/PXI 输出尚有独立前置门；本协议不替代它。
2. 驱动 `PrepareRevision` 完成完整清单及 manifest 核验。原生 UI 确认绑定该候选的内容、版本和能力，生成批准快照。页面 JS、裸快照或 URL 不能创建安装事务。
3. 取得规范 app ID 与 installation ID 的原生独占协调租约。重新核对名称未占用，并 CAS COMMIT PREPARING 意图。只有确认该提交后才允许创建应用数据库。所有后续操作保有同一不可变 root 和租约。
4. 如有 SQL，绑定独立数据位置并打开 local-full DB，精确 BindLocalIdentity，读取严格 schema。先复制、准入全部尚未执行的 manifest migration 来源，然后按 schema 的下一项执行受限迁移；不向作者开放完整 SQL。无 SQL 则跳过数据准备。
5. 再读严格 schema，确认等于目标；确认没有遗留事务。保有 DB/位置与资源租约，再刷新 PREPARING 记录，比较完整快照和 generation。任何变化均停止，不自动扩大批准。
6. CAS COMMIT PREPARING→READY，读回精确 READY 记录。只有确认最终提交才报告安装成功。提交/读回结果不明按下表处理，不能仅凭数据库准备成功给用户“安装成功”。
7. 首次执行作者代码是独立启动流程：取得 READY 的 installed handle，完成资源核验，打开并验证 installed storage，随后才注册授权服务、创建作者页面。安装协调句柄不直接转成网页服务或借出 DB。

最终提交后、任何用户代码开始前若进程退出，下次照常重新核验资源/数据。安装成功不代表 HTML、脚本或页面已运行成功。启动验证失败可报告修复需求，但不能将数据删除、降级或重新执行已提交 SQL作为自动修复。

## 中断与结果不明的恢复表

恢复必须先取得同一协调租约，读取 committed 记录并重新验证实际资源；不依据上次进程日志猜状态。数据库读失败不等于“记录不存在”。无失败状态写入能力时，保留 PREPARING 与原文件，关闭后等待显式恢复，不能伪报清理完成。

| 观察到的事实 | 处理 |
| --- | --- |
| 尚无意图；仅有隔离资源目录 | 无授权，不创建/认领安装库；仅在确认无任何记录引用时列入待清理 |
| 意图提交回调失败或结果不明 | 在新可用登记事务中精确读回；匹配 PREPARING 才继续，同身份的匹配 READY 表示最终提交已发生；缺失或不同快照不重试写入。不可读则阻断 |
| PREPARING；尚无 DB | 只有意图身份、资源、批准均重核验成功后，原生显式恢复才能按该身份创建；普通启动不得创建 |
| PREPARING；DB 只有正确身份、schema 0 | 显式恢复可执行全部 pending migrations；不是成功安装 |
| PREPARING；正确身份、schema k，0<k<target | 已提交前缀保留，重新准入全部剩余来源，只执行 k+1..target；不重放前缀 |
| PREPARING；正确身份、schema=target | 不再执行 migration；验证后可以尝试最终 READY CAS |
| PREPARING；错误身份、非空未认领库、schema>target 或损坏版本 | 拒绝/标 FAILED，保留诊断及数据，不重绑/降级/删除 |
| READY 提交或其响应丢失 | 精确读回 READY 完整记录：匹配则完成；仍是原 PREPARING 则保留“未完成”，可在显式恢复中再次 CAS；其他 generation/状态/内容拒绝；不可读则保持结果不明 |
| READY；数据或资源缺失/损坏 | 启动失败并要求修复；普通 opener 必须 require-existing，不借创建模式生成空库或运行安装迁移 |
| FAILED/CANCELLED/REVOKED | 无启动权；FAILED 的显式重试按前述规则处理，其他不自动复活 |

回调错误之后不得在原生事务尚活跃或状态不明时重入读回。先由 provider 收敛事务、释放活动 DB 操作，再返回可安全调用的状态；无法收敛即关闭安装 job 并保留结果不明，下一次恢复重新取租约。不能返回失败同时让后台写入继续。

### 取消与失败的区别

取消只在安全边界检查，不能抢占 provider I/O 或撤回已成功 COMMIT。取消发生在迁移 k 提交之后，k 仍保留；最终 READY 尚未提交时才可以提交 CANCELLED。如果取消与 READY 提交交错，必须先精确读回，READY 已确认则返回已完成，撤销需要另一次明确操作，不能把已完成安装当未完成清理。

迁移预算、SQL/授权、I/O 或资源核验失败会阻止 READY；如果错误已知且登记可用，CAS 到 FAILED 并保留结构化 Runtime/DB/native/cleanup 诊断。写 FAILED 又失败不能覆盖原错误，应另记登记结果不明。失败不保证磁盘“完全没写过”，只保证没有可用作者实例或伪成功。

## 公共接口与所有权

已实现入口以[公共头](../positron_runtime/positron_runtime.h)为准；下表记录实际职责，不声明新的占位导出。
EXE持久登记和资源输出不改变Runtime对状态/批准/读回的所有权。

| 入口族 | 输入与所有权 | 边界 |
| --- | --- | --- |
| InstallIntentBegin / Step / GetInfo / Cancel / Close | 活的COMPLETE prepare、显式原生决定、版本化登记provider | 保有父项/租约；NEW、RESUME与RETRY_FAILED按既有合同，不是更新 |
| InstallDataBegin / Step / GetInfo / Cancel / Close | 活的意图、独立数据位置、完整pending来源 | 逐文件迁移与后缀恢复，不发布作者SQL句柄 |
| InstallCommitBegin / Step / GetInfo / Cancel / Close | 相符意图/数据、资源和协调租约 | 最终READY CAS与精确读回；unknown不重放，取消不反转READY |
| InstalledStorageOpen / StorageExecuteJson / ServiceQueueCreate | COMPLETE installed实例及require-existing存储provider | 精确身份/schema、显式Browser授权，不创建/认领或借用development身份 |

Native provider 提供原子登记读取/CAS 和规范物理位置，不决定状态迁移、迁移后缀或权限。每次登记结果区分 committed、definitely-not-committed、unknown，返回成功只能表示确切 COMMIT；unknown 必须走核对，不能当普通可重试 IO。持久 schema/SQL 仍由 EXE 所有，provider 事务不调用 Runtime/DB callback 回入同一 job。

租约顺序固定为 app-ID 协调→installation-ID 协调→不可变资源 root→数据位置/DB；不等待其他活动安装，冲突返回 BUSY。prepare 已持有 root 时只能尝试非阻塞协调租约，失败立即返回，不构成反向等待。启动/修复/撤销均使用相同 installation-ID 协调身份。安装完成后先释放协调 job，再走普通启动；若需要保留租约，须另做原子转移接口，不能重复获取而绕过 BUSY。

父子关系必须显式保有：存在安装 job 时父 revision Cancel/Close 返回 STATE；存在 installed storage 或队列时父 revision Cancel/Close 同样拒绝。子项先关闭。当前意图子job先释放其登记租约再解除父保有，随后调用者才可关闭父资源root；安装队列在UI停止访问、已撤销且处理空闲后由worker关闭；再关DB与数据位置，最后解除资源/登记父项保有。活动调用不可释放；跨线程只有既有明确复制队列路径可用。不自建 worker、不 DispatchMessage、不跨线程共享 live DB/Core/Script 句柄。

SQL 继续使用 100..1000000、100 倍数的每文件/请求 VM budget，最多16个来源、每项32768 bytes/合计512 KiB；不因安装扩大现有 DB/SQL/JSON 配额。准备来源不复制整个资源包。安装协调仅持有一个迁移来源计划、一个 DB 与固定元数据；实现前编译期核对新增工作集，不能把只读 revision 的512 KiB与迁移512 KiB相加后仍称总512 KiB。组合预算应单独公开，最坏 Runtime-owned 同时工作集上限定为1280 KiB（DB/native provider内部内存另计）。

## 实施顺序与验收门

登记读回及PREPARING意图协调已实现，精确入口/状态/预算见组件合同；前者只观察，后者通过可信原生provider执行一次CAS并读回，支持显式恢复与取消，该意图入口不写READY，最终提交另走InstallCommit。测试模拟原生登记，不证明EXE持久表。后续存储只接受活的PREPARED意图，不接受裸批准快照或开发句柄。所有转换与恢复分类仍属DLL，test_host只准备事实、故障与断言。

安装准备数据已有独立stamp/key、完整pending来源准入、逐文件提交及后缀恢复合同，回归使用真实PDb文件；无READY或作者SQL服务。DB的只读快照和可写既有句柄准入均有独立合同。Runtime已接READY InstalledStorageOpen，保有installed父项/位置租约，刷新READY记录并经既有句柄准入复用受限SQL/JSON及撤销/关闭；安装队列已复用有界复制及显式Browser授权，区分页面取消与实例退休；最终READY提交由独立InstallCommit子job接受活意图/相符数据，复查登记及空闲DB精确schema，一次CAS后读回；取消不撤回READY，结果不明不重放。提交前核验失败保留PREPARING与数据供显式恢复，不隐式写FAILED。EXE已接主库迁移、确认/恢复UI、Archive隔离输出、安装入口和独立installed启动；实际消费者证据与未完成人工矩阵分别见交接，不把源码接入等同全部设备验收。

每个功能批次均须 C89、审计、串行正式 Debug/Release 和匹配 ARMV4I 定向设备门。至少覆盖无SQL/零migration/多migration、两安装与开发数据隔离、错误stamp/非空库/未来版本、每个提交点前后中断、提交成功但响应丢失、读回失败、CAS竞争、取消与最终提交交错、资源变化/撤销、关闭/重入/错误线程及重复恢复。迁移回归使用非幂等插入计数证明前缀没有重放，不能只检查 schema 数字。

当前文档审查不等于这些设备门通过，不重开已挂起的 FULL/物理断电验收。更新、卸载清理、数据恢复和硬件断电各自仍需后续合同；本阶段不实现跨库回滚，也不以两次 COMMIT 宣称跨库原子性。

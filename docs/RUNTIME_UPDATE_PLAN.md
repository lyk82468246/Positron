# 已安装资源应用的更新计划

本文记录已安装应用更新的分阶段方案。Runtime 发布与恢复已通过双配置设备门。EXE 通过 PXI 入口接入持久登记与资源更新，Notes 两进程证据见[应用更新验收](history/APP_PXI_UPDATE_ACCEPTANCE.md)。
首次安装继续使用[现有恢复合同](RUNTIME_INSTALL_RECOVERY.md)，整体方向见[运行时计划](RUNTIME_PLAN.md)。
Runtime拥有更新资格、批准、状态转换、读回分类及生命周期；EXE拥有主库表、原生租约、文件I/O、
风险确认和worker。公共DLL工作交主线与DLL会话，接口不足时EXE停止对应接线。

## 首装与更新接口的分工

现有[公共头](../positron_runtime/positron_runtime.h)和[安装合同](../positron_runtime/INSTALLED.md)
原有首装/批准/opener路径只允许revision 1。OpenInstalledEx显式核验期望revision，ResourceUpdate资格保有活current/candidate及既有存储，但二者都不发布更新。原登记读回仍要求完整旧/新approval一致，首装Intent和Commit只协调单一PREPARING记录；不能只把EXE中的revision改成2。

主库 [app_installs](../positron_app/app_install_store.c) 每个 installation ID 一行、app ID 唯一。
schema v6 增加独立尝试表及登记快照，原表继续保存 current。InstallData 在稳定安装身份的原数据库逐文件提交迁移，
而普通InstalledStorageOpen要求精确schema；原地迁移失败后，旧READY也可能无法启动。
因此禁止同ID覆盖文件、把旧READY改PREPARING，或以新installation ID伪装更新并继承旧数据。

## 阶段与完成边界

| 阶段 | 交付及进入下一阶段的条件 |
|---|---|
| 合同与夹具 | 固化current/pending、租约、恢复分类、版本化opener及预算；不新增占位导出 |
| 读取与资格前置 | 显式revision opener及只读资格已通过双配置定向门；接受活父项、相同历史/schema/能力，固定保有与组合预算；候选验证不等于发布 |
| 公共资源更新 | Runtime新增版本化资格/批准、update job、登记provider、读回/恢复和普通启动全链；不执行迁移，DLL双配置设备门通过 |
| EXE资源更新 | 主库迁移与原子provider、独立资源根、明确确认、owner调度；Notes实际更新及失败旧页/旧数据保留门通过 |
| 数据迁移更新 | 先新增DB一致性复制与Runtime数据generation合同，再在副本迁移，最终描述符原子切换资源和数据 |
| 应用卸载 | Runtime 在安装租约内持久撤销授权，EXE 从已安装应用页发起，保留应用数据 |
| 能力变化及回收 | 基于完整新声明重新确认，回收前核验资源归属及全部持久引用 |

首个产品纵切只交付资源更新；schema/能力变化继续明确拒绝，不以该阶段冒充完整更新。
后续迁移和能力阶段仍是目标，不通过自动重试或静默忽略提前放行。全程不引入自动/热更新。

## 首个资源更新的准入

规范app ID和installation ID相同，candidate revision等于current+1且不溢出，manifest app version
严格增加；revision、应用版本和登记generation相互独立。API、digest及approval policy保持受支持且相同。
permissions、SQL存在与否及目标schema相同，已提交migration前缀的版本、路径和文件摘要全部相同。
无SQL与同schema SQL应用都要覆盖；新增能力、权限增减、schema增加或修改迁移历史返回UNSUPPORTED。

批准必须绑定活的已核验旧installed实例、完整候选、旧generation/content及新完整快照，
不能只接两个裸结构。每次更新仍明确确认旧/新版本、能力及发布者未验证；不扩大旧批准位。
候选写独立新资源根，不覆盖current资源，不迁移旧库、不执行候选作者代码。
成功切换后关闭更新链，再通过新版本化opener独立核验并启动；新版开始写作者DML后的回滚不在此承诺内。

## current、pending与提交结果

新版本provider必须fresh-read current与pending，并提供精确expected/proposed原子持久化操作。
EXE保留旧表/行语义，新增显式pending结构：唯一尝试ID、base current完整指纹/generation、
candidate完整资源/批准/root/schema以及pending自身generation/状态，每installation至多一项pending。
公共布局及调用见[发布合同](../positron_runtime/INSTALLED.md#publishing-a-resource-only-update)。主库schema仍由EXE迁移。

准备期间current仍为旧READY。最终在同一个主库事务中完成current替换与pending终态，
不另建active文件。Runtime决定状态转换；EXE仅逐字段比较/持久化，返回前事务收敛，没有后台写入。
每个逻辑写入最多尝试一次，响应丢失先读回，不能重放CAS。

| 可核验事实 | 启动与恢复规则 |
|---|---|
| 切换前已知失败/取消，精确旧current仍在 | 关闭更新协调后可以正常启动旧版；旧批准、资源和DB保持 |
| 精确新current及匹配终态 | 更新已发生，取消不反转；新opener重新核验后启动 |
| 登记不可读、冲突、缺失或不匹配 | 阻断启动，由Runtime分类；不得自选旧目录/批准启动 |
| current或未解析pending仍引用资源 | 保留，不能因本次UI失败猜测可以清理 |

旧资源仍在不等于有权运行旧版。“旧版可启动”指状态未破坏且协调租约已安全释放；
不保证更新持锁期间另一个实例能同时运行，也不保证未知提交时立即回退。

## 所有权、退出与预算

正式更新前正常退出旧实例、撤销Browser服务并收敛/关闭队列和SQL；冲突返回BUSY，不强杀。
旧installed实例已持独占installation租约，新job不能再acquire同一租约然后伪造成功。
只读资格采用显式借用：保有current父项及其已经取得的登记租约，不再次acquire，SQL时同时保有存储/位置；两父项关闭/取消及存储执行/新队列在子项关闭前拒绝。发布job消费活资格并在同一协调下刷新current/pending；复制Info没有授权能力，不能提前释放租约后信任旧快照。parent退休后资格不得完成，终态仍须可查询和关闭。

旧v1入口继续只接受revision 1；通过新增Ex/新handle/版本化provider开放更新及rev2普通启动，
包括refresh、storage和queue组合，不给旧API伪造revision1。所有调用保持创建worker所有权，
Step仅在安全边界让步，不泵消息、不自建线程，不把单次原生I/O/SQL当作可抢占操作。

保留单视图现有预算及EXE的128条目/4MiB输出上限；同一更新只允许一个job和pending。
当前资格已对两个实际revision、存储、资格元数据和128KiB reserve做编译期组合证明，Runtime-owned≤1280KiB。发布状态机加入后，编译期组合断言仍限定为1280KiB。两份原生4MiB快照可能同时保有8MiB，完整host峰值及OOM展开仍须独立测量。
不能借单job预算掩盖组合开销，不扩大资源/SQL/JS既有配额。

## 应用卸载与资源回收

从 `positron://apps` 的当前列表行发起卸载。原生确认显示应用 ID、版本和数据保留策略。
EXE 将安装身份、revision 和 generation 交给 Runtime 核对。Runtime 在独占租约内检查
已提交登记及更新尝试，活动实例或 PREPARING 更新阻止卸载。撤销通过持久事务保留原身份
和资源指纹，更新 generation。提交回复丢失时读回完整登记，无法确认时停止，后续只能观察。
已确认撤销后刷新应用列表，普通启动必须拒绝原登记。

数据仍由原安装身份持有。原包重装通过 Runtime 核验完整包摘要和原库身份后可恢复启动。
卸载与原包恢复分别有设备验收。见[卸载记录](history/APP_PXI_REVOCATION_ACCEPTANCE.md)和
[恢复记录](history/APP_PXI_RESTORATION_ACCEPTANCE.md)。

### 安全回收的实施顺序

1. EXE 为新解包目录持久保存所有权清单。清单来自本次实际创建对象，包含规范根、相对路径、
   对象类型、原生身份及用于识别变更的文件信息。EXE 已在首装、更新和原包恢复提交前发布
   版本化 `.ownership` 清单。先刷新临时文件，再改名发布，清单放在资源根旁。旧包没有
   清单时继续保留，随机目录名或登记中的路径不能补作所有权证明。
2. 回收在独立宿主任务中执行。持有与安装、更新和启动兼容的租约，并将检查引用和允许删除
   的状态变更纳入同一登记协调。检查全部安装登记及可恢复的更新尝试，包括可能位于目录内
   的子根。READY、安装恢复和 PREPARING 更新所需资源继续保留。REVOKED 的原包恢复依靠
   用户重新提供匹配摘要的包及原库，仍需确认旧目录没有其他使用者。
   主库 worker 已提供有界只读引用检查，覆盖父子目录重叠及待恢复尝试。检查结果是快照，
   须由后续回收协调把它与删除动作绑定。
3. 删除前先完整枚举，核对清单、物理包含性和对象身份。遇到未登记对象、替换、别名、枚举
   失败或占用就停止。只按清单逆序删除。保留任务记录以便中断后核验剩余对象再继续。回收
   不接触私有数据库及其目录。

设备门使用隔离 Notes 验证旧版资源释放、待恢复候选保留、卸载后原包可恢复原笔记。另覆盖
其他登记引用同一目录或子根、额外文件、对象替换、占用、旧清单缺失及中途失败后重启。

## 数据迁移的后续前置

原地第k次migration COMMIT之后，跨登记库回滚不能恢复旧版兼容。要保留失败时旧数据，
需要独立候选数据generation：持排他租约获取一致副本、仅迁移副本，最后由一个current描述符
同时指向新资源与新数据generation。DB当前无公开backup接口，须先新增有界一致性复制合同；
不能raw-copy活库、重绑旧stamp或让旧实例继续写源库。

空间预算包括旧库、候选库、journal；不足就拒绝，不增16MiB单DB配额或自动迁卷。
失败仅清确认无引用的候选，unknown先读回，不用旧库覆盖恢复。成功且新版写数据后不承诺无损回滚。
减少SQL权限也不顺便删除旧库；未知能力仍拒绝。物理断电/FULL门按既有暂停决定保持，不借正常重开关闭。

## 最小直接消费者与验收

使用纯资源夹具生成器准备独立目录和PXI：

```powershell
python scripts/generate_app_update_fixtures.py --output tmp/update-fixtures
python scripts/test_app_update_fixtures.py
```

输出目录须全新；工具只复制作者Notes资源，不修改示例、登记或用户库。固定测试ID为
`org.positron.updatenotes`，提供old、同schema resources-v2以及schema增加、历史迁移改字节、
SQL权限移除、版本未增加/零版本、其他ID、未来API和入口缺失候选。页面用
`update-fixture-version`标记资源版本，原作者JS保持不变。
`facts.json`只保存manifest字段、完整文件长度/SHA-256与相对路径，不计算Runtime content ID、
不执行SQL或编码更新状态。离线测试证明ZIP/目录字节、候选差异及可重复性；具体拒绝阶段、
返回码和current/pending转换必须由公共DLL设备门判定。

Notes revision1保存精确中文/引号记录；创建独立candidate，app version增加、同schema/权限与迁移历史，
更新后新进程显示v2资源并保留原记录，再冷重开免重复安装提示。更新确认与普通启动分别验收。

失败门覆盖坏候选、拒绝、取消、未来schema、历史migration改变及能力变化不改旧current/批准/DB；
pending持久化后中断、切换未提交/已提交响应丢失/不可读/竞争generation、旧实例未退出/租约冲突、
关闭/重入/stale、unknown不重放及成功切换不被取消反转。用独立冷库/资源字节证据核对，不能只看日志。

DLL 双配置门覆盖旧 revision 1 与新增版本化入口。EXE 两进程门消费真实 Notes PXI、注入登记回调失败并核对冷库。
Release 普通确认、笔记编辑与冷重开见[应用更新验收](history/APP_PXI_UPDATE_ACCEPTANCE.md)。

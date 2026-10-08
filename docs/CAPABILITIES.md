# 公共能力覆盖矩阵

本文件描述 Positron 九个顶层公共 DLL 的主干能力、当前边界和进入实现的条件。它是面向
应用开发者和维护者的稳定说明，不记录 next 编号、提交时间、设备运行目录或逐测试流水。
精确函数签名、结构体大小和错误码仍以对应的公开头文件为准。

## 状态与使用规则

| 状态 | 含义 |
| --- | --- |
| 已实现 | 公共入口已经存在，行为、所有权和固定预算有离线合同；直接相关的设备回归也已取得，或属于无需设备的基础 DLL 合同。 |
| 有界待扩展 | 已有公共边界或相邻能力，但完整消费者流程仍缺少证据或实现；不能把未列出的行为当作隐式支持。 |
| 宿主职责 | 由应用宿主按平台和产品策略提供，不应复制进公共 DLL 或测试宿主的产品语义。 |
| 暂缓 | 需要完整现代浏览器、安全沙箱、无界资源或尚未有真实消费者证据；不作为当前 ABI 承诺。 |

“有界待扩展”不是“已经支持”。只有同时具备所有者、固定预算、失败回滚、离线 fixture
和相称的设备/人工门，能力才能从该状态提升为“已实现”。

## 公共接口共同规则

- 公共边界使用稳定 C ABI、UTF-8、opaque handle 和明确的创建/销毁所有权。
- 新能力优先追加 size/version 结构或 `Ex` 入口，不改变旧字段和旧入口含义。
- 所有字符串、数组、body、listener、资源、脚本对象和 DOM wrapper 都必须有固定上限；
  size-probe 或容量不足不得部分写出或部分 mutation。
- 参数错误、容量不足、stale handle、缺失 callback 和未支持输入必须在改变状态前失败。
- 以后若公开一个暂未实现的入口，必须返回该 DLL 定义的稳定 unsupported 类错误；不能返回
  假成功、创建伪 handle、调用宿主 callback 或留下半完成状态。
- `test_host.exe` 只提供 fixture、断言、窗口/线程/网络和平台 callback；它不拥有下表中的
  URL、资源、DOM、Event、表单、图像、脚本或生命周期语义。

## 独立应用消费者的宿主边界

EXE 私有内部地址注册表提供 newtab、about、settings、下载未实现说明、持久 GET 访问记录
与独立 Browser 会话 history；version/system 规范化为 about 章节。模板双语内嵌，动态 HTML
上限 128 KiB，访问记录最多 500 条/每页 16 条，焦点最多 32 项。quit 只接受地址栏直接提交
并正常关闭，不改公共 DLL ABI。可信 settings 原子编辑 HTTP(S)/三个内部主页、语言和网页
JS 开关，使用 EXE 同目录 positron.db、公共 DB 与 Browser 受控异步服务；完整 URL 少于
1024 UTF-8 字节，仍服从公开 URL 解析器。首次导航前异步读取设置，语言重启应用，网页
候选接受时固定 JS 策略，已加载页面不变且可信设置代码独立获准。保存、新进程恢复及正常
退出排空已有应用设备门；真实下载、故障注入与人工 UI 矩阵不因此算通过。边界见
[内部页面设计](../positron_app/INTEGRATION_PLAN.md#内部页面与命令地址)。

应用把当前 GET 文档绑定到 Browser history 的公开 document identity；同文档
Back/Forward/go 保留 DOM、ScriptSession、native 控件与 retained layout，仅同步 URL、
traversal/hash 事件及 viewport，不重新请求网络或派发卸载。pushState 和离页提交前保存
page-level 双轴物理位置，网络 history/刷新在 layout 后按 extent clamp 并恢复；普通恢复
尊重 `scrollRestoration=manual`。刷新取得新 identity，未入栈的 POST 文档不借用旧栈。
字面 UTF-8、空片段与 legacy name 锚点通过 Core CSS fragment 几何换算一次 DPI；缺失目标
保持位置，百分号编码与跨文档 HTTP 初始片段仍待公共 URL 接口接入。Debug 私有 EXE
自检直接验证适配器的会话保留、事件、双轴与 identity 隔离，Release 不包含这些夹具；
真实网络 Back/Forward/刷新、native 焦点和旋转仍需人工验收。

`positron.exe` 的私有 `AppHostContext` 只收拢 WM6 窗口、页面句柄、导航候选、history、资源
和 DLL 初始化/清理的生命周期；Core/Browser/HTTP 仍拥有文档、URL、history、资源事务和
页面语义。阶段 0 保持离线页面、英语/简体中文 i18n 和主文档 HTTP(S) GET 不变；阶段 1
通过 EXE 私有适配层接入外部 CSS/`@import`、脚本发现和图片发现，沿用 Browser 的 required/
optional gate；阶段 2 再由 `app_script.c` 创建有界 ScriptSession，按 DOM 顺序执行网络
候选的 classic inline/external script，并把 DOM、事件、导航、滚动、焦点和生命周期回接到
当前窗口。脚本异常不回滚页面，bridge 初始化失败则 fail closed 为无脚本页面。
网络候选在完全配置新 session 后由候选 generation 的系统 timer 分步调用 Browser bootstrap，
初始化完成后每次调度最多执行一段完整作者脚本；显式 GC 仅在批次末独立调度一次。取消、
替换与关闭先停止调度，空闲销毁 session 后再释放 document；旧页保持到真正提交。
单个 Step、作者脚本与既有任务 checkpoint 仍同步，不承诺固定毫秒响应或 continuation；
Debug 计时与窗口调度夹具不编入 Release。阶段 3 的
第一条宿主纵切已由 EXE 私有 `app_controls.c/.h` 接入：`text`、`password` 和 `textarea`
映射为同一窗口体系下的 WM6 native `EDIT` 子控件；单选/多选 `SELECT` 映射为原生
`COMBOBOX`/`LISTBOX`；checkbox/radio 映射为同一窗口体系下的 WM6 native `BUTTON`。Core
负责控件 value、选项状态、checked/radio-group 状态与几何，Browser native-edit/native-select/
native-toggle bridge 负责输入、选择、trusted click、input/change、focus、单选下拉事务和重置；
EXE 另把 Core 绘制的普通 `type=button` 命中及 Space/Enter 激活接入 Browser 的 native-button
click transaction。Browser click callback 经 Core 按坐标派发；Core 绘制的按钮不额外创建 WM6
子窗口。`type=reset` 按钮也使用 Browser 可取消的 click/reset 事务，获准后调用 Core
`PCore_FormResetAt()`，再重建 native 控件以同步初值；该应用接线未新增 ABI。`type=submit`
按钮也已接入：Core 按坐标校验并生成成功控件数据，Browser 的 native-button transaction 在校验
通过后派发可取消 submit，EXE 私有 `AppForms` 适配层按 Core 返回的 method/enctype 组合
URL-encoded GET/POST、multipart POST 或 `method="dialog"` 默认动作并调用既有导航候选/关闭路径。
Core 负责 successful-control snapshot 与 multipart wire encoding；EXE 只提供同步文件 I/O、HTTP
body/Content-Type 调度和 dialog close。非法值、取消、容量错误或候选失败不会替换旧页，非 GET
提交不伪造可重放的 Browser history entry。内置 controls 页带 required
GET 表单；除可信 settings 的最小服务代码外，内置离线页面不创建 ScriptSession，其 inline script 不执行，因此脚本取消示例仍须在
网络 ScriptSession 页面验收。阶段 4 已由 EXE 接入脚本 `form.reset()`、native GET/POST submit、脚本
`requestSubmit()` 与 direct `form.submit()` 的 GET/POST/multipart/dialog 路径；direct 方法按
Core/Browser 合同跳过 validation、submit event 和 submitter。单行文本/密码 native EDIT 的 Enter
另调用 Core 隐式提交接口、派发可取消 submit，并按表单 method/enctype 进入相应默认动作；required 校验失败时 EXE 通过 Browser invalid callback 派发首个无效控件的
non-bubbling/cancelable `invalid` 事件，获准时按 Core 几何滚动并聚焦原生控件，取消时抑制默认反馈；
native submit 按钮复用该反馈。textarea Enter 保持换行。native `input type=file` 通过 WM6
系统选择器和 Browser file-selection transaction 接入 Core multipart 的同步 file read/free callback。
提交仍不新增 ABI；脚本自行构造的 Browser File/Blob pairs 没有转换为 Core-owned multipart snapshot
的公共入口。
有界 DOM mutation 返回 UI 消息泵后，宿主按 option 集合/标签
fingerprint 延迟重建 SELECT，同时保留 EDIT/SELECT 焦点。阶段 3 另接入带 id 且已布局的
`contenteditable` editing host：EXE 将其投影为同一窗口体系下的原生多行 EDIT，Core 保留有界
纯文本，Browser 处理 `beforeinput`/`input`；同一纵切还通过现有 Browser selection callbacks
在当前已提交且已物化的带 id EDIT 上同步 native range、selectionStart/End/Direction 与
`setSelectionRange()`，按逻辑 LF/UTF-16 偏移换算 WM EDIT 的 CRLF 索引，并把鼠标拖选、Shift+方向键
及焦点/捕获收尾通知为去重的 `selectionchange`。候选页、stale host 或未物化 surface 使用 Browser
有界脚本回退；该接线不新增 ABI，也不保留富文本子树或提供 Range/Selection 对象。宿主负责消息路由、
指针/按键接线、重排和 teardown。顶层物理页面滚动沿 `test_host` 的 retained-pixel 路径使用系统
滚动条和 `ScrollWindowEx`：纯滚动只移动已有像素、补绘暴露区域并重定位 native 子控件，不重复
执行 Core layout 或 SELECT/toggle 状态同步；WM_SIZE、Core/DOM mutation 和真实 viewport 变化仍
进入完整 layout。
页面内容可由 EXE 私有 `AppInput` 路由双轴跟手拖动：DPI 阈值区分 tap/pan，点击延迟至
未拖动的抬起，捕获丢失、取消、失焦、resize 与页面替换取消输入。Core 内部滚动条优先，
native EDIT/SELECT/toggle 不被页面捕获代理；纯拖动复用上述 retained-pixel 路径与 Browser
滚动通知，不增加公共 ABI。惯性、回弹与 nested 内容区滚动链未实现，真实触摸仍需人工门。
源码级 C89、Debug/Release 与仓库审计通过，但 WM6 仍须验收
native/script submit/reset、POST/multipart/dialog、file picker、网络导航/失败回滚及原生控件；
SIP/IME 不因该接线而宣称完成。地址栏 EDIT 也属于同一顶层窗口体系的子控件，外框按客户区
从 x=0 铺满宽度，高度经实际字体、客户区留白和边框换算并按 DPI 更新；EXE 不再额外加入 inset，
以免旋转或重绘后留下 stale 像素。地址栏外框和文字下行完整性已通过当前 WM6 设备人工验收；
旋转/DPI 的累计人工矩阵仍需继续覆盖。
后续
接线顺序与阶段门见
[`positron_app/INTEGRATION_PLAN.md`](../positron_app/INTEGRATION_PLAN.md)。

## TLS：`positron_tls.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| TLS 初始化、CA 和全局清理 | `PTls_Init`、`PTls_AddRootCA`、`PTls_Cleanup` | 已实现 | mbed TLS 配置固定；CA/初始化失败返回 false，不暴露内部对象 | TLS 组件 README、正式构建；新增证书策略须补离线证书 fixture |
| 客户端连接、读写、关闭 | `PTls_Connect`、`PTls_ConnectVerified`、`PTls_Read/Write/Close` | 已实现 | 连接由 opaque handle 管理；读写长度使用公开 `int`，错误经 `PTls_LastError`/copy 读取 | TLS 设备/集成门；真实端点只作集成哨兵，不能替代离线错误分类 |
| 客户端连接前阶段观察 | `PTls_ConnectEx`、`PTls_ConnectVerifiedEx` | 已实现但有界 | 仅同步报告真实的 DNS name、TCP connect、TLS handshake 开始阶段；IPv4 literal 不报告 DNS；不产生终态，不改变证书/hostname 校验；回调禁止重入/cleanup | 由 HTTP observer 转发并在 TEST3/4/999 Debug WM6 门验证；直接 TLS observer 仍只作为上层适配器接口 |
| 协议适配器可取消直连 transport | `PTls_TransportOpenEx/Read/Write/Close` | 已实现但有界 | 独立 handle 族；size/version、TCP/TLS、验证选项、1–120000 ms 整体期限及借用取消 event；owner worker 非阻塞 I/O，select 最多 50 ms 检查；DNS 最多 4 个独立引用/pin 的复制数据 job，取消不等 OS DNS，不抢占 crypto/callback | TEST1342 的取消/超时/DNS 生命周期与 TEST1343 真实 verified HTTPS，双配置 ARMV4I 门；无系统代理，旧 TLS 入口不变，OS resolver 饱和和真实低资源峰值另需门 |
| 对端身份和指纹 | `PTls_ConnectPeer`、`PTls_PeerFingerprint`、`PTls_IdentityFingerprint` | 已实现 | 指纹输出固定为 `PTLS_FINGERPRINT_HEX_CAPACITY`；缓冲不足不部分写出 | 证书/hostname/指纹合同；新增校验必须保持 fail-closed |
| 服务端 identity/listener/accept | `PTls_IdentityLoadOrCreate`、`PTls_ServerListen/Accept` | 已实现 | identity/listener 由创建者关闭；listener flags 和参数非法时拒绝 | listener 资源上限和错误分类已有组件合同；新增并发策略需独立证据 |
| DTLS、完整证书链策略和异步握手 | 当前没有稳定公共承诺 | 暂缓 | 不通过增加无界状态或绕过证书检查来“补齐” | 只有真实 WM6 消费者和可固定预算的协议合同出现后再立项 |

## JSON：`positron_json.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| 解析和释放 JSON 树 | `PJson_Parse`、`PJson_Free` | 已实现 | 非法/空输入返回空 handle；调用方必须释放成功树 | JSON 组件回归和正式构建；复杂输入仍受移植库资源约束 |
| 基础 object/array/string/int 读取 | `PJson_Get*` | 已实现 | 失效 handle、缺失键和越界索引按头文件约定返回安全值；不返回借用指针之外的所有权 | 现有 JSON 调用方和脚本桥；布尔/浮点/类型与 object 遍历保持 additive ABI |
| 有界序列化和字符串释放 | `PJson_Serialize`、`PJson_FreeString` | 已实现 | 分配失败返回空；字符串由 JSON DLL 分配并由对应入口释放 | 现有 JSON 合同；新增 size-probe 需保持旧序列化 ABI 不变 |
| schema、流式 parser、异步 DOM 映射 | 当前没有公共入口 | 暂缓 | 不引入无界 parser 状态或跨线程借用树 | 需要明确消费者、固定 token/深度预算和可回滚 fixture |

## DB：`positron_db.dll`

数据库 DLL 只拥有本地 SQLite、migration、事务、同步 shadow state、outbox 和 conflict
语义；宿主拥有 DB worker、HTTP 调度、Bearer Token、重试和 UI 消息。详细调用流程见
[`positron_db/README.md`](../positron_db/README.md) 与公开头文件。

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| 本地 SQLite 打开、typed bind/column、事务、取消和错误 | `PDb_OpenUtf8`、`PDb_Prepare`、`PDb_Bind*`、`PDb_Column*`、`PDb_Begin/Commit/Rollback/Cancel` | 已实现但有界 | 固定 SQLite 3.53.4；VS2008/WM6 ARMV4I；SQL ≤32 KiB、body ≤1 MiB；DB/statement handle 不跨线程，DLL 不创建线程 | TEST1321 覆盖 UTF-8 文本、SQL/bind 超限、类型读取、事务/取消/错误；正式工程已接入；断电、真实设备文件锁和峰值内存仍需设备门 |
| 本地完整 SQL 与原子 migration | `PDb_Exec`、`PDb_ApplyMigration` | 已实现但有界 | 完整 SQL 模式允许 SQLite 本地语法；同步模式禁止直接 DDL、ATTACH/DETACH、PRAGMA、扩展和事务 SQL，schema 只能由 migration 原子推进版本 | TEST1321 覆盖本地/同步创建、版本/事务状态、失败回滚、模式隔离和同步限制；journal 恢复仍需设备 fixture |
| 离线 outbox、游标、tombstone 与 typed row JSON | `PDb_SyncConfigure`、`PDb_SyncRegisterTable`、`PDb_SyncBuildRequest` | 已实现但有界 | 仅单列 INTEGER/TEXT 主键；NULL/整数/实数/文本/BLOB 显式标签；不发送 SQL；请求/响应 ≤1 MiB | TEST1321 覆盖主键约束、本地写入、事务合并、固定 request envelope/size-probe、整数边界、REAL/NULL/BLOB、文件重开和 tombstone 基准版本；复合主键/字段级 merge 不在 v1 |
| REST 响应原子应用与服务器权威冲突 | `PDb_SyncApplyResponse`、`PDb_SyncGetConflicts`、`PDb_SyncCopyConflict`、`PDb_SyncResolveConflict` | 已实现但有界 | 仅成功 2xx、schema hash/version 匹配且 JSON 完整时推进状态；op_id 幂等；冲突保存本地/服务器行并恢复服务器行 | TEST1321 覆盖 accepted、多页 pull、typed response、upsert/delete 冲突、冲突元数据不匹配拒绝、失败整批回滚、版本冲突、retry-local 和冲突列表；`scripts/db_sync_fixture.py` 提供独立 op_id/冲突/tombstone/分页/auth fixture；真实 HTTPS、宿主 worker、重启恢复和设备断电仍待集成门 |
| 加密数据库、WAL、远程 SQL、多主/复合主键和自动字段 merge | 当前没有公共承诺 | 暂缓 | 不通过 ODBC/远程数据库客户端或放宽 SQL 边界补齐 | 只有新的消费者、固定协议和可验证的 WM6 资源预算出现后再评估 |

## HTTP：`positron_http.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| reference 解析 | `PHttp_ResolveReference`、`PHttp_ResolveReferenceUrl` | 已实现 | host/port 入口保持兼容；URL 入口保留显式 scheme 和非标准端口，非法 scheme、端口、控制字符和截断安全失败 | TEST1064/1065/999 及 Core/应用 callback 复用证据 |
| HTTP/HTTPS GET/POST、进度、最终 URL 和 response 释放 | `PHttp_Get[Ex]`、`PHttp_GetUrl[Ex]`、`PHttp_Post[Ex]`、`PHttp_PostUrl[Ex]`、`PHttp_ResponseGetFinalUrl`、`PHttp_FreeResponse` | 已实现 | response body 受 1 MiB 上限；Content-Length 截断、分块/读取/分配失败均丢弃 body 并返回 `status_code=0`；redirect 有界；URL 省略协议只默认 HTTPS，HTTPS→HTTP 降级被拒绝；旧 `PHttpResponse` 布局不变 | TEST3 的 final-URL 断言、TEST1064/1065/999、应用资源接线；继续用受控重定向和超限 fixture 检查一致错误 |
| 请求网络阶段观察 | `PHttp_GetEx2`、`PHttp_GetUrlEx2`、`PHttp_PostEx2`、`PHttp_PostUrlEx2`；TLS 转发 `PTls_ConnectEx`/`PTls_ConnectVerifiedEx` | 已实现但有界 | request-scoped `size/version` observer；同步、固定 UTF-8 快照、一次终态；`received/total` 保持解码 body 语义，未知总量为 `-1`；WinInet DNS/代理/socket 不可分辨时以 `PHASE_MERGED` 报告，不伪造细分；回调禁止重入/cleanup，不暴露 header/body | TEST3/4/999 的 HTTPS GET/POST 阶段、非法 scheme fail-closed 与 WMDC Debug 设备门；重定向、受控 HTTP provider 失败和并发隔离继续按测试文档扩展 |
| 流式 GET 与跨线程取消 | `PHttp_StreamCreateGet/Get/Cancel/GetResult/GetHeader/Close` | 已实现但有界 | 4096-byte 借用块、64-bit transfer-decoded counts、显式 1 byte–1 GiB quota；响应头/trailer 各 16 KiB/64 项、行 1023 bytes、最多 5 redirects；final URL/status/metadata/重复普通头；identity encoding，TE/CL 冲突与压缩拒绝；Cancel 唯一跨线程操作，终态锁线性化，join 后释放；直连 IPv4、HTTPS 固定验证且不降级；旧完整正文 1 MiB 不变 | TEST1342 的 >1 MiB 精确字节、chunked/未知长度/non-2xx、容量/framing、sink 拒绝、取消/超时/并发及旧入口回归，TEST1343 verified HTTPS；双配置设备门。EXE 文件保存/下载 UI、代理、Range、流式 POST 和压缩另行取舍 |
| TLS 初始化和安全开关 | `PHttp_Init/Cleanup`、`PHttp_SetInsecure` | 已实现但默认安全 | insecure 只可由应用显式打开；证书/hostname 风险不能由 HTTP 静默吞掉 | TLS/HTTP 组合门；发布前继续审查旧 mbed TLS 风险 |
| CORS、referrer、loading/fetch-priority、缓存策略 | 当前没有稳定公共策略入口 | 有界待扩展 | 必须先确定 Browser/HTTP/Core owner、请求代际、旧页保留和取消语义；不能只按标准名称添加字段 | 需要真实页面或消费者证明阻塞，并提供 loopback/offline fixture |
| HTTP/2、WebSocket、完整代理和无限缓存 | 当前没有公共承诺 | 暂缓 | 超出 WM6 资源和当前请求模型 | 只有新的明确产品范围才重新评估 |

## Image：`positron_image.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| bitmap 解码、像素创建、信息读取和 GDI 绘制 | `PImage_CreateBitmapFromMemory/FromPixels`、`PImage_BitmapGetInfo`、`PImage_DrawBitmap` | 已实现 | 输入长度、像素尺寸和平台绘制失败返回 `PIMAGE_ERROR_*`；句柄由调用方释放 | Image 组件回归、Core paint 集成；native 像素观感仍需人工门 |
| SVG 解码、信息读取和绘制 | `PImage_CreateSvgFromMemory`、`PImage_SvgGetInfo/DrawSvg` | 已实现但有界 | libsvgtiny 错误映射到 `PIMAGE_ERROR_SVG_BASE`；解析前只把有界的简单 class 规则转换为行内 paint style；调用者没有提供 viewport 时，合法根 `viewBox` 的圆整宽高作为自然画布，只提供一边时按比例补齐；stroke 支持有界 `rgba()`/`stroke-opacity` 与 butt/round/square linecap，并在 GDI 前保持 alpha；复杂 CSS、超预算或不支持语法 fail closed | TEST1315 的真实 IANA class/gradient/viewBox/自然尺寸/重复绘制、TEST1316/1317 的 Core 背景回归、TEST1318 的 CSS data URI 链路和 TEST1319 的精确 rgba round-cap 像素回归均已通过；真实 `positron.exe` 页面视觉仍需应用门 |
| bitmap 编码 | `PImage_EncodeBitmap[Ex]` | 已实现但按格式裁剪 | 缺少 encoder 返回 `PIMAGE_ERROR_UNSUPPORTED`；输出 buffer 由对应 free 入口释放 | 格式能力以头文件和组件 README 为准，不扩大为无界格式集 |
| picture/source 选择、generation 和事件 | Core/Browser 公开 relation/notification 组合 | 有界待扩展 | Core 选择、Browser generation，宿主 I/O/decode；过时事件不得改变 current source | TEST1299/1300 已验证 generation/终态；完整 loading 仍需消费者证据 |
| 视频、canvas、动画图像和完整色彩管理 | 当前没有公共承诺 | 暂缓 | 需要额外线程、内存和绘制合同 | 不作为当前 WM6 主干目标 |

## Media：`positron_media.dll`

第三方应用的头文件、集成步骤、回调示例和状态合同见
[positron_media/README.md](../positron_media/README.md)；本节只保留能力矩阵和验收边界。

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| source callback、探测、opaque session 和 host-driven pump | `pm_probe`、`pm_open/close`、`pm_pump`、`pm_pause/resume/stop/seek` | 已实现但有界 | 输入一次性受 16 MiB 上限；无 seek 回调可顺序打开，提供但失败的 seek 拒绝；I/O/无进展错误不由 codec 尝试覆盖；probe 失败不改输出；回调 buffer 只在同步回调期间有效；无长期线程/网络 | TEST1312/1331–1335 的 Debug/Release 设备门覆盖 ABI、输入错误、恢复失败、WAV 与 H.264/AAC/MJPEG/MP3/TS/PS MPEG/AMR EOF/seek 重播、软解音视频回调暂停/恢复、回调错误、停止态守卫和独立 session 释放；输入容量、非零压缩 seek 及其他 codec 生命周期仍待门 |
| WAV PCM 与 IMA ADPCM 软件音频 | `pm_audio_block` S16LE callback | 已实现但有界 | PCM 8/16-bit、mono/stereo；IMA 只接受 4-bit、完整块、每声道四字节交错组，samples_per_block 与块长度相符且 ≤2048（最大合法值 2041）；有效 fact 裁尾，无 fact 保留 padding；错误 chunk/索引/保留字节/对齐拒绝，超容量返回 LIMIT，不回退绕过；`AUTO` 对 PCM 先尝试设备 WaveOut | TEST1312/1331/1337 的 Debug/Release 设备门验证 PCM8 mono/PCM16 stereo 与 8 kHz IMA mono/stereo 的实际样本、PTS/时长、块内/边界/EOF seek；IMA 逐字节参考比对、后置/缺失 fact、合法大块与超容量拒绝已覆盖；其他采样率、IMA 非 WAV 布局仍未验收，部分编码块明确不支持 |
| WM6 DirectShow/ACM/WaveOut 原生能力 | 内部 `CLSID_FilterGraphNoThread` 探测、PCM `WaveOut`；公共头不暴露 COM | 有界待扩展 | WaveOut 仅覆盖设备接受的 WAV PCM，8-bit 设备格式也统一发出 S16LE 借用回调；callback-backed DirectShow source filter、native 视频 renderer 和完整 ACM/filter 枚举尚未完成，不把 graph 存在误报为 codec 可用 | Debug/Release AUTO PCM8 已实际选择 NATIVE 并完成 WaveOut/PCM 回调断言；原生完整生命周期、underrun、其他设备格式与未来 source filter 需要独立门 |
| FFmpeg 软解 | 固定 `third_party/ffmpeg-3.4.14`、ARMV4I archive 和 `POSITRON_PORT.md` | H.264/AAC/MJPEG/MP3 与 TS/PS MPEG/MP2、AMR-NB/WB 短媒体解码合同已实现；其他格式有界待验证 | custom memory AVIO；编译集合为 AVI/MP4/MOV/MPEG-PS/MPEG-TS/FLV/WAV/选定裸流及 H.264、MPEG-4 Part 2、MPEG-1/2、MJPEG、H.263、AAC、MP2/MP3、AMR-NB/WB、PCM/IMA ADPCM；H.264 仅 Baseline（含 constrained）/Main、8-bit 渐进 I420，最多 640×480，即使 options 请求更大也不放开；全范围 I420 以现有 flags 的 FULL_RANGE 位标明，未公开完整色彩管理；源 PTS 不归零，缺失视频 PTS 可从已知前帧 PTS/时长有条件推导并以 PTS_INFERRED 标明，seek 清除推导状态；音频 AAC-LC/S16LE 最多双声道；SPS 与实际帧均有拒绝守卫 | TEST1332–1335 在 Debug/Release 设备上验证 MP4/AVCC H.264/AAC、VGA Main/B 帧、ADTS AAC mono、AVI/MJPEG+MP3 stereo、44.1 kHz mono MP3、TS/MPEG-2+MP2 和 PS/MPEG-1+MP2、单声道 AMR-NB 8 kHz/WB 16 kHz 的像素/范围、PCM/样本数、PTS/时长与尾帧推导标记、EOF/seek、回调暂停/恢复、profile/布局/隔行/尺寸拒绝、截断头及独立 session 关闭；AMR 每块 20 ms、PCM 幅度/过零与 seek 后重播校验值一致；AMR 其他码率/DTX/丢失帧/3GP、其他编译格式、截断 payload、帧率、underrun、峰值内存和真实设备仍需门；AVC 专利与 GPL 组合需发布前审查 |
| MPEG-4 Part 2 有界解码 | MP4 Simple/Advanced Simple、AVI Simple + MP3；`pm_probe/open/pump/seek` | 短媒体合同已验收 | extradata VOL 有界前缀检查矩形/4:2:0/渐进式及应用/VGA 上限；无 extradata 或码流内参数变化依赖实际帧守卫；不保证 Qpel/GMC 或全部 ASP 编码工具 | TEST1344 双配置 WM6 门覆盖实际 I420/flags、PCM、PTS/时长、三 session 各两次解码与 seek 零校验值一致、7-byte 短读、不可 seek AUTO、音视频 STOP/负 callback、EOF/停止态/关闭及隔行/超限/截断头拒绝；复杂工具、坏 VOL 分支和非零 seek 仍待门 |
| H.263 有界解码 | AVI H.263 CIF、H.263+ 自定义 VGA；`pm_probe/open/pump/seek` | 无音频短 AVI 合同已验收 | 8-bit 渐进有限范围 I420、最多 640×480；不代表裸流、3GP、FLV/Sorenson H.263 或复杂编码工具已验收 | TEST1347 双配置 WM6 门覆盖三帧 I/P/P、plane/stride/像素取样/flags、PTS/时长、三 session 各两次解码与 seek 零校验值一致、短读/不可 seek AUTO/SOFT、暂停/视频 STOP/负 callback、EOF/停止态/关闭、704×576/应用尺寸/截断头拒绝；音视频组合及非零 seek 仍待门 |

Media 首版的目标边界是 decoder/playback only；不包含编码、DRM、字幕、直播协议、长期工作线程、
AV1、HEVC/H.265、VP9、H.264 10-bit/4:2:2/4:4:4 或高于 640×480 的软件视频。DirectShow
当前只做 graph 可创建性探测，原生播放只落实设备接受的 WAV PCM WaveOut；其他
`PMEDIA_BACKEND_NATIVE` 输入仍 fail closed。桌面格式表不能替代设备运行时 filter/codec 探测。
FFmpeg archive 是离线固定构建输入，不在正式工程中联网下载。
`pm_pump` 目前忽略 `clock_us`，`budget_us` 只是处理量提示，不是严格墙钟预算；按时输出、
迟到丢帧、音视频同步和暂停时间基准尚未实现。短小 H.264/AAC/MJPEG/MP3/TS/PS MPEG/AMR/MPEG-4 Part 2/H.263 夹具的解码与 EOF 后 seek
合同不代表其他已编译格式、复杂画面或实时播放已经验收；夹具来源与固定哈希见
[媒体夹具](../test_host/fixtures/media/README.md)。

## Script：`positron_script.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| context 创建、销毁和 classic evaluation | `PScript_Create[Ex]`、`PScript_Evaluate`、`PScript_Destroy` | 已实现 | 默认 heap 512 KiB，source 128 KiB；超时、内存和 fatal 错误有稳定错误码 | Script 自动回归；任何新 host object 先检查 heap/native-function budget |
| global JSON bridge | `PScript_Set/Get/CallGlobalJson`、register/unregister | 已实现 | global name 128 字节；native function 上限 29；callback 同步不可重入 | JSON/script 组合门；失败不修改旧 global |
| 显式 native JSON 结果容量 | `PScript_RegisterGlobalJsonFunctionEx` | 已实现但有界 | size/version；256–8192-byte 临时 buffer 计入原 heap，旧入口保持 256 bytes，不提高 native/source/执行预算；非法配置保留既有注册 | TEST1341 双配置 ARMV4I 门覆盖旧/Ex 容量、非法版本与 Browser 4 KiB 结果 |
| module/source provider | `PScript_EvaluateModule`、`PScript_SetModuleSourceProvider`、`PScript_LoadModule` | 有界待扩展 | module 名 128 字节、最多 16 个；缺 source/callback/超限安全失败 | 需要明确模块生命周期和资源取消；当前 Browser 默认不启用完整 module |
| Browser classic session | 由 Browser 使用同一 runtime | 已实现但 opt-in | Browser page heap、native function、listener、字符串、任务和 DOM 对象均固定上限 | Browser/TEST1303–1309 及相邻设备门；`javascript=0` 仍是默认 |
| worker、完整 ECMAScript host、无限异步任务 | 当前没有公共承诺 | 暂缓 | 不引入独立线程 runtime 或无界队列 | 仅在目标应用给出可控资源模型后再评估 |

## Core：`positron_core.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| HTML/CSS parse、style、layout、page extent 和 GDI paint | `PCore_ParseHTML`、`PCore_ParseCSS`、`PCore_StyleDocument[Ex]`、`PCore_LayoutDocument`、`PCore_PaintDocument*` | 已实现但有界 | 资源、节点、字符串、layout 和 paint 使用项目固定上限；parse/style/layout 失败不泄漏 handle；纵向、自动主轴且子项 `flex-basis:0` 时，已布局实际高度计入父容器 | Core 离线 corpus、正式构建和设备视觉/自动门组合；TEST39 增加 footer 不得覆盖 column-flex 子项的几何断言 |
| 图片发现、异步 fetch/cache、解码与 image box | `PCore_FetchImageResources`、`PCore_FetchImageResourcesEx`、image relation/layout APIs | 已实现但有界 | 旧入口保持同步非零终态失败；Ex 用 READY/PENDING/TERMINAL_FAIL，PENDING 不写 failed cache；cache、SVG/PNG/JPEG/GIF 解码和布局受固定预算，未知状态、坏 body、超限和终态失败 fail closed | TEST18/20/27、TEST1313 离线与 `1313,999` 设备门；真实应用网络图片可见性仍需应用页面门 |
| HTML img 声明尺寸与设备投影 | 既有 style/layout、attribute mutation 和 geometry APIs；不新增 ABI | 已实现但有界 | width/height 每项 ≤128 字节、libcss 22:10 可表示；非法/超限属性不产生 hint，零值保留，作者 CSS 可覆盖；已解码比例、直接 flex image 与 DPI carrier 投影由 Core 拥有，cache 自然尺寸不变；完整现代 aspect-ratio/source 与 replaced-flex sizing 不承诺 | TEST1340 的离线 96/192 DPI cascade、比例、零值、预算、flex、paint/hit、mutation/cache 回归及双配置 ARMV4I 门；真实消费者页面仍须匹配包复核 |
| CSS `data:image/svg+xml` 背景资源 | Core 的 `PCore_FetchImageResources[Ex]` 内部 data-URI 解码；结果复用 Image DLL 和既有背景绘制 | 已实现但有界 | 仅支持 percent-encoded 与 Base64 SVG；URL ≤256 KiB、解码 bytes ≤64 KiB、SVG ≤128 元素/64 `path`；非法 MIME、控制字符、损坏编码和超限 fail closed（可建立 cache key 时记录终态失败），不调用宿主 HTTP callback；background-position/size/repeat 仍由既有 computed-style/paint 路径负责 | TEST1318 覆盖 CSS→Core→Image→GDI、坏 URI 和复杂度超限，`1318,999` Debug ARMV4I 设备门通过；普通网络 SVG/PNG/JPEG/GIF 由 TEST19/20/1316/1317 相邻回归；若需扩大 MIME/解码范围，先提供消费者和新的 bounded ABI |
| DOM/attribute/CharacterData/HTML mutation | `PCore_Node*ById`、`PCore_FormControl*ByIndex`、`PCore_EventDispatchFormControlEx`、relation、serialization 和 Ex mutation callbacks | 已实现但有界 | 失败前预检 id、节点形状、深度、child 数、UTF-8 和容量；native-button target token 只在同步 trusted event 中有效，不创建 DOM id；成功后 layout retained 失效 | TEST1284–1298、1325–1327 及设备门；通用 Node/Fragment mutation 仍不承诺 |
| form owner、validation、selection、reset、modal 和 successful-control snapshot | `PCore_Form*`、`PCore_NodeFormControl*`、interaction/focus APIs | 已实现但有界 | owner、listed controls、fieldset/option state 和提交快照有界；非法/stale target fail closed | TEST1170–1188、1301–1302 和设备门 |
| multipart/default submission 和 FormData encoding | `PCore_MultipartSubmissionEncode`、`PCore_FormDataEncode` | 已实现 | body 上限 1 MiB；file read/free callback 同步借用；缺 callback、读取失败或容量不足不部分写出 | TEST1301/1302；宿主只提供文件 I/O 和 HTTP 调度 |
| File/Blob 对象、异步文件读取和浏览器式上传策略 | native `input type=file` 已由 `positron.exe` 通过 WM6 picker、Core snapshot encoder 和宿主同步 file callback 接入；脚本 File/Blob 仍没有 Browser JS FormData→Core body 交接入口 | 有界待扩展 | Browser 不暴露本地路径或持久 byte handle；Core 只接受自己的 FormData snapshot；缺 callback、权限/大小失败和 stale handle 必须在 body 输出前拒绝 | native picker 进入应用人工门；只有脚本 pairs→Core snapshot 的真实消费者证明阻塞时才提升 |
| Range/Selection、MutationObserver、通用 live collection 和完整滚动树 | 当前没有公共承诺 | 暂缓 | 需要额外状态、事件队列和更大资源预算 | 保留在限制文档，不因标准名称直接立项 |

## Browser：`positron_browser.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| history、fragment、push/replace state、scroll snapshot | `PBrowser_History*` | 已实现但有界 | entry、URL、state 和 scroll snapshot 有界；失败保留旧 entry/旧页面 | next871、history/viewport fixtures 和设备门 |
| navigation candidate/resource transaction | `PBrowser_NavigationCandidate*`、`PBrowser_NavigationResource*`、commit/cleanup snapshot | 已实现但有界 | generation、required/optional gate、retry、fallback、cancel 和 cleanup 都固定；过时 worker 不能提交 | TEST1119–1127、设备门；宿主只调度网络/worker |
| script session、DOM/Event/form/input/lifecycle/viewport bridge | `PBrowser_ScriptSession*` callback tables and dispatch APIs；document delegated `click` 通过 Core `PCORE_DOCUMENT_ELEMENT_TOKEN`，无 id 的 native button 通过同步 target token 暴露属性 | 已实现但有界 | legacy `PBrowser_ScriptSessionCreate` 使用 1.5 MiB heap；`PBrowser_ScriptSessionCreateEx` 允许 0 或 1.5–3 MiB 的固定 profile，官方应用使用 3 MiB；垃圾回收和 used/peak/limit 查询均为同步有界入口；source 128 KiB、native functions、listeners、collections、strings、FormData/URLSearchParams、Storage、任务队列和 document click listener（64 项）仍固定；超过上限返回 memory error，不创建无界状态；target token 不跨回调保存 | TEST1138/1139、1152–1309、TEST1320/1322/1325–1329 及相邻门；脚本默认关闭；真实外部 Bootstrap 的网络 fetch/execute 仍需应用门，3 MiB 不是任意网站兼容承诺 |
| 有界产品初始化 | `PBrowser_ScriptSessionBootstrapBegin/Step/Cancel/GetState`；旧 `EvaluateBootstrap` 同步驱动相同目录 | 已实现但需宿主调度 | size/version、非零 generation、最多 40 阶段；Step 最多一个完整产品程序或 GC，不保证固定毫秒；pending/failed/cancelled 拒绝普通入口和 runtime 外借；同一创建线程、空闲取消/销毁，失败不可恢复；既有 heap/source/timeout 不变 | 离线及双配置设备门验证私有 session、取消、失败、错误 generation/线程与原版脚本终态；宿主须在 Step 返回后安排消息，真实应用响应仍须接线验收；单个作者脚本和 task checkpoint 无 continuation |
| File/Blob metadata and multipart consumer bridge | Browser `new FormData(form[, submitter])` 只通过 callback 取得 Core successful-control metadata；`PCore_FormDataEncode()` 只接受 Core-owned snapshot，二者没有 JS pairs 的公共转换入口；native picker→form multipart 已由 EXE 接线 | 有界待扩展 | Browser 不直接读文件、不暴露路径或 byte buffer；Core/宿主的同步 read/free、1 MiB body 和 stale/权限失败边界保持不变 | native picker 由应用人工门验收；脚本 pairs→Core snapshot 只有在真实消费者出现后才提议公共 ABI |
| 受控异步应用服务 | `PBrowser_ScriptSessionRegisterServices/RevokeServices/CompleteService/PumpServices` | 已实现但需显式授权/宿主调度 | 默认关闭、宿主可信授权；8 方法×63 ASCII bytes、16 pending、JSON 4096 bytes/16 层；C 结果副本 ≤16×4097 bytes，JS/dispatch 使用原 heap 与 2 个既有 native 槽；完成只复制、owner pump 按完成顺序交付，重复/stale/非法失败不消费请求，撤销/销毁取消等待并丢弃结果；旧 ABI 不变 | TEST1341 双配置 ARMV4I 门覆盖隔离、容量、错误不变性、owner/reentry、timeout 与重复销毁；EXE 业务/worker/消息调度、真实界面响应另行验收 |
| CORS/referrer、absolute URL policy、完整 image loading | 当前没有完整公共承诺 | 有界待扩展 | 必须为安全边界、旧页保留、generation 和取消建立独立合同；不能由宿主临时决定 | 需要真实页面/消费者和 loopback fixture |
| 多个窗口、bfcache 和跨窗口 history | 当前没有公共承诺 | 暂缓 | 需要额外 browsing context、持久状态和资源预算 | 只有新的明确产品范围才重新评估 |

## 从“有界待扩展”提升为“已实现”

一项能力只有在以下证据齐全后才更新为“已实现”：

1. 真实消费者、页面或可重复失败说明它为何阻塞目标流程；
2. 顶层 DLL 所有者、公开 C ABI、所有权、固定预算和 unsupported/error 分类已经写入头文件；
3. 离线 fixture 覆盖成功、参数错误、容量不足、stale/cancel、失败不变性和释放顺序；
4. 相关 C89 检查、正式 ARMV4I 构建、仓库审计和相称设备门通过；
5. 需要视觉、触摸、SIP/IME、picker、旋转或 DPI 时，人工结果已完成或明确进入允许累计的
   backlog；
6. `HANDOFF.md`、`KNOWN_LIMITATIONS.md`、`ROADMAP.md` 和稳定组件文档各自只更新自身职责。

矩阵不承诺完整现代浏览器标准。它的作用是让应用知道“现在能可靠使用什么、什么会安全失败、
下一步需要什么证据”，而不是用 API 数量替代产品完成度。

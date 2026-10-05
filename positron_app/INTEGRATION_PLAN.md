# `positron.exe` DLL 接线计划

## 范围与目标

`positron.exe` 以 `test_host` 的接线顺序、callback 注册和生命周期处理为参考，成为当前公共
Positron DLL 的真实 WM6 应用宿主。范围固定为 HTTP/HTTPS、EXE 内置离线页面和单窗口有界
多标签；不加入 `file://`、下载、外部协议、多窗口、Web worker、module、bfcache 或完整现代 Web API。

“全部功能”指当前公共 DLL 已实现且面向浏览器用户的有界能力，不要求应用为了覆盖导出符号而
直接调用 TLS、JSON、Image 或 Script 的低层诊断 API。Core/Browser/HTTP 继续拥有可复用的页面、
资源、DOM、事件、表单、history 和生命周期语义，EXE 只拥有窗口、消息、线程、native 控件、
输入和应用策略。

## 私有宿主分层

应用私有适配层按以下职责拆分；这些接口不导出给其他 DLL，也不复制公共 DLL 的产品语义：

- `AppHostContext`：主窗口、CommandBar、viewport、DPI、语言、消息循环和关闭状态。
- `AppPageContext`：Core document、stylesheet、Browser session、页面 URL、generation、滚动和
  native 控件集合。
- `AppNavigation`：candidate/resource transaction、worker、取消、stale、重试和提交状态机。
- `AppResources`：Core URL resolver、CSS/image/script 资源发现和 HTTP 回调。
- `AppScript`：Browser callback table、classic script、生命周期和任务 checkpoint。
- `AppControls` / `AppInput`：WM6 native 子控件、焦点、滚动、SIP/IME、剪贴板和触摸。
- `AppForms`：校验、submit/reset/formdata、GET/POST 和 multipart 提交。
- `AppSettingsStore`：应用设置 schema、单所有者 DB worker、有界请求/完成队列与关闭排空；不拥有公共 DB、脚本或导航语义。
- `AppSettingsServices`：可信设置页的最小业务方法、worker 请求与 Browser token 对应；桥接、脚本队列及撤销仍由公共 Browser 接口负责。

页面 callback 的 `pw` 指向对应页面上下文。网络 worker 只负责 transport 和消息投递，存储
worker 只拥有自己的 DB/statement，不读取页面；Core、
Browser 状态修改、style/layout/paint 和页面 swap 只在 UI 线程执行。CommandBar、软键、页面和
native 子控件保持同一顶层窗口体系，滚动使用 WM6 标准窗口滚动条，禁止自绘滚动条。

## 多标签与平台调度

EXE 私有 `AppTab` 最多拥有四个稳定槽位，每个槽位持有独立 `AppHostContext`、Core document/
stylesheet、Browser history/ScriptSession、候选/退休请求和 native 页面子窗口。顶层窗口、
地址栏、两侧软键、绘制缓冲与 DLL 初始化只创建一份。切换隐藏/显示页面子窗口，不重载文档
或重新执行作者脚本；保存滚动、native 控件、焦点及尚未提交的地址输入/选区。视口尺寸改变
时在激活该标签后按原 resize 路径重排，尺寸未变不因切换额外 layout。

原生控件子类过程按父页面 HWND 查找稳定的 `AppControlsContext`，不依赖当前标签或最后
创建的上下文；初始化中的已绑定控件也必须转发到保存的原生窗口过程。销毁时先恢复原
窗口过程并释放子控件，再注销上下文，避免新建/关闭其他页使旧页的绘制与输入失效。

左软键为 `Tabs` / `标签页`，使用 WM6 原生下拉菜单：后退、有前进目标时才显示的前进、
分隔符、标签列表、可选的新建标签、分隔符、刷新、关闭标签。右菜单不重复后退/前进/刷新。
当前项打勾；尾部已是 newtab 时不重复提供新建入口，否则在容量内可新建。
history/后退/前进/刷新只作用于当前标签，菜单状态读取该页的 Browser history。
关闭先询问现有 Browser beforeunload；
取消或调用失败保留该页。关闭最后一页先准备新 newtab，失败不丢原页。退出应用则取消全部
标签候选并等所有 worker 结束，再 teardown 页面、销毁 native 控件、释放共享资源与 DLL。

所有 live Core/Browser/Script 状态仍由 UI 线程拥有；各标签的网络 worker 可并行，只访问
其 request/resource transaction，不读取当前标签全局值。非活动标签的传输继续，但完成
消息只 join 并保存请求；解析、后续资源发现、脚本初始化/作者执行及页面提交等到激活后
继续。非活动已提交 session 不推进任务 checkpoint，并接收 hidden/blur 生命周期；它不是
多进程隔离或完整后台执行保证。退休请求额度跨标签统计，关闭中的槽位在 worker 全部 join
前不能复用；达到容量或分配失败保留已有标签。

平台消息绑定不复用的标签 ID，完成消息还须证明 request 属于该标签；地址提交、脚本导航、
控件刷新和 picker 的迟到消息在借用指针解引用前拒绝。脚本 timer 使用进程唯一 ID，再核对
Browser candidate generation。启动自动化同时绑定标签和候选，不能误执行到另一个同编号
候选。公共导航/history/资源/生命周期语义仍归 DLL，本批不新增 ABI、opener、named window、
`_blank` 自动开页、跨标签消息或持久恢复。后续进程隔离须另行设计 IPC、输入/渲染和内存
合同，不把现有 opaque handle、HWND 或 JS runtime 直接跨进程传递。

## 实施阶段

### 阶段 0：宿主分层与生命周期基线

第一批只做结构性迁移，不改变现有离线页面、i18n 和主文档 GET 行为：

- 建立统一的宿主上下文、页面所有权、generation、候选和 teardown 状态。
- 将当前 `main.c` 的全局窗口/页面状态收拢到 `AppHostContext`，并通过私有 API 初始化、绑定
  窗口、释放页面和关闭宿主。
- 保持旧页保留、stale/cancel、失败回滚、history 更新和资源释放顺序。
- 不编译或复制 `test_host/main.c`，不修改公共 ABI，不提前接入脚本、外部 CSS、图片、表单或
  native 控件语义。

阶段 0 必须通过 C89、仓库审计、Debug/Release 正式构建和现有离线页面回归后，才能进入阶段 1。

### 阶段 1：网络资源事务

按 `test_host` 的提交顺序接入：

`HTTP 响应 → ParseHTML → 外部脚本发现 → 必需 CSS → 可选图片 → Browser commit gate → layout`
`→ 旧页 teardown → 页面交换 → history/滚动恢复 → native 重建`

必需 CSS 失败阻止提交；图片和脚本失败不阻止页面显示但必须记录终态；HTTP/TLS 失败、取消、
过时和预算拒绝必须保留旧页面。HTTPS 默认保持证书链和 hostname 校验。

加载标题属于 EXE 私有展示层，不另造导航状态机或改变 Browser 提交资格。原生标题栏只显示
本地化阶段文字，动画只在地址栏展示；就绪时固定显示 `Positron Browser` / `Positron 浏览器`，
文档标题由地址栏显示。UI 线程用 250 ms timer 读取 worker 阶段，仅文字变化时更新标题；
宿主同步阶段在实际调用点更新，不触发额外 layout、paint 或脚本 checkpoint，不重入消息泵。
请求、接收正文、HTML 解析、脚本获取/执行、样式、图片、排版和首次绘制按实际宿主调用点
显示，不伪造百分比。DNS、连接、TLS 等 transport 细分只能消费 HTTP 的公开观测接口，
通过 `PHttp_GetUrlEx2/PostUrlEx2` 的 request-scoped observer 映射域名解析、连接、TLS 握手、
发送、等待响应、响应头、正文和重定向；不能从 URL 或经过时间猜测。WinInet 标为
`PHASE_MERGED` 的通知只显示“正在请求页面”，不冒充独立 DNS/TCP 边界。只有主文档
请求订阅 observer，子资源保持脚本/样式/图片阶段；COMPLETE/FAILED 不直接清除加载标题
或提交页面，原 Browser gate 和返回响应路径仍决定提交、重试与回滚。
worker 只原子更新自身 request 的展示字段；只有当前 generation 的 UI timer 可以读它并
更新标题，取消、完成和关闭停止 timer。迟到的旧请求不能覆盖新候选标题，旧页页内跳转
继续保留加载标题；成功提交并完成首次同步绘制后恢复本地化应用标题，失败恢复已提交页的标题快照。

### 阶段 2：Browser ScriptSession

注册 DOM、属性、mutation、事件、焦点、表单、输入、导航、滚动、图片、native 控件和生命周期
callback；按文档顺序执行 inline/external classic script；接入 bootstrap、timer、microtask、
message、visibility、focus、beforeunload 和 teardown。

JavaScript 默认启用，使用 `PSCRIPT_DEFAULT_BUDGET_MS * 8` 的固定应用预算与现有 Browser 脚本堆上限。脚本
异常不使已解析页面回滚；会话初始化、桥接、超时或超限失败时，该文档脚本能力关闭并 fail
closed。

网络候选先完成全部 callback/全局配置，再调用 Browser 的 BootstrapBegin；使用候选 generation
绑定的低优先级 `SetTimer/WM_TIMER`，每次窗口调度只执行一个完整 bootstrap Step 或一段完整
作者脚本，返回后才安排下一次。初始化完成前不借出 runtime，不派发事件或任务；旧页与其
session 在候选真正提交前保留。取消、替换和关闭在空闲边界停止 timer、Cancel/Destroy 候选
session，再释放候选 document；过期 timer 仅按 ID/generation 校验，不携带 request 指针。
初始化间发生旋转/SIP 时不调用 pending session；作者调度前按实际 viewport 同步变化，
尺寸不变不额外调用脚本桥。
作者脚本按 DOM 顺序推进，普通异常继续，timeout/heap/fatal 关闭该候选脚本能力。移除每段
脚本前的显式 full GC，改为自动有界 GC 和批次末独立调度的一次收集；预算不扩大。
不把 live session/Core 移到 worker，不在 DLL 调用或 callback 内重入消息泵。Step、单段作者
脚本、GC 与既有 task checkpoint 仍同步，不承诺固定毫秒的响应上限或任意 JS continuation。
Debug 可在 Begin 前启用 session 计时，记录慢 Step 与作者 compile/execute；Release 不含此
诊断、直接计时入口依赖或测试夹具。

### 阶段 3：原生交互

接入 EDIT、SELECT、toggle、button、dialog、contenteditable、file picker、SIP/IME、clipboard、
焦点和标准 WM6 滚动条。native 控件只是 Core DOM 的平台代理；DOM、事件默认行为和表单状态仍
由 Core/Browser 所有。支持 DPI、旋转、viewport 和 native 子控件重排。

地址栏由 EXE 私有 `AppAddressBar` 复用同一矩形：原生 EDIT 始终只存网址，负责选区、
键盘和 SIP/IME；非编辑状态使用同父窗口的 GDI 展示层。加载中显示候选网址并循环填满、
清空，填充部分使用系统 highlight/text 色，不表示完成百分比；加载结束显示已提交文档标题，
无标题回退网址，长文本按实际字体宽度往返滚动。点按、页面 Tab 或“打开地址”菜单切回
EDIT 并全选网址；回车沿原路由提交，Esc/失焦返回展示。后台提交或回滚只更新保存的网址，
不覆盖正在输入的 EDIT、选区或 IME；结束编辑后使用最新有效地址。
地址栏 EDIT/展示层及私有导航完成消息不经通用 CommandBar/IsDialogMessage 过滤；原生 EDIT
通知不作为菜单命令处理。仅显式回车提交地址才替换候选，点按、编辑、Esc 或失焦不取消网络。
展示层沿用实际系统字体和地址栏外框，不改变页面 viewport。160 ms 系统 SetTimer/WM_TIMER
只刷新地址栏，填充/满格/清空周期为 4 秒，不使用高精度或后台动画定时器；
短标题、编辑状态停止计时，不触发页面 layout、paint、网络或脚本 checkpoint，不重入消息泵。
仅缓存一个最多 2048×256 像素的兼容位图，尺寸变化替换，失败回退直接绘制，关闭释放。
低资源设备的跟手性仍需实测，必要时降低刷新率或撤去动画，不让动画影响输入和导航。

页面内容拖动使用 WM6.0/6.1 的 `WM_LBUTTONDOWN/MOUSEMOVE/LBUTTONUP` 与窗口 capture，
不增加 WM6.5 WAG 依赖。`AppInput` 只分类平台输入：超过按 DPI 换算的 4 个逻辑像素后成为
拖动；未越阈值的抬起才沿既有 Core/Browser click/default-action 路径激活。拖动一旦成立，
返回原按下位置也不产生 click；丢失 capture、取消、失焦、隐藏、resize、页面替换与关闭均
取消待定输入。原生 EDIT/SELECT/toggle 保留自己的消息过程，Core 内部滚动条命中优先。
顶层双轴拖动复用 extent clamp、`ScrollWindowEx`、native 重定位与 Browser CSS scroll 通知；
纯拖动不重新 layout，脚本 mutation 则仍按原刷新路径处理。暂不提供惯性、回弹、多点触摸或
nested overflow 内容区的滚动链，不自绘滚动条。

### 阶段 4：表单与导航

接入校验、`submit/reset/formdata`、GET/POST、URL encoded、multipart、fragment、push/replaceState、
Back/Forward、脚本导航和滚动恢复。提交取消、网络错误和候选失败必须保留旧页；native 文件
选择路径优先。JavaScript File/Blob 到 multipart 只有在真实消费者出现且公共 ABI 补齐后实现。

同文档遍历以 `PBrowser_HistoryIsSameDocumentTarget` 和公开 document identity 为准，不按 URL
相似度推断。当前显示的 GET 文档绑定 Browser identity；不写 history 的 POST 文档不绑定旧栈。
同文档 Back/Forward/go 只提交 Browser target、同步 host URL，再调用 Browser 的 traversal
事件入口；DOM、ScriptSession、native 控件和 retained layout 保留，不发起网络请求或卸载。
重新加载/跨文档遍历必须取得新 document identity，不能继续复用旧 pushState 组。

显示 A、等待跨文档候选 B 时，Browser 已判定的 A 页 fragment 跳转不取消或退休 B，也不增加
网络 generation；只更新 A 的 history、位置和候选的已提交 URL 回滚快照。地址栏和加载标题
继续表示 B；B 失败恢复 A 最新的片段地址及原标题，B 成功仍按既有提交路径替换 A。
push/replaceState 同样同步回滚地址，不覆盖加载 UI；跨文档链接 C 则走原候选替换路径，
取消/退休 B，B 的迟到结果不能覆盖 C 的 UI。明确的 Back/Forward/go 调度保持原策略。

滚动快照复用 Browser entry scroll 接口，在离页提交前及 pushState 前保存当前物理 viewport。
已布局页面恢复时只 clamp、移动 retained 像素、重定位 native 子控件并通知 Browser CSS 坐标；
普通 history 恢复尊重 `history.scrollRestoration=manual`，fragment reveal 独立执行。Core
fragment 几何为 CSS 像素，按当前 DPI 换算一次；空片段到原点，缺失/不可布局目标保持位置。
Core ByToken 的输入是已解码 UTF-8；请求 URL 的片段保留和百分号解码不能复制为 EXE URL 引擎，
需要对应公共 URL owner 的完整接口后再解除这部分限制。

### 阶段 5：发布验收

每阶段运行：

- `python scripts/test_c89ize.py`
- `python scripts/audit_repo.py`
- `scripts\\build.bat Debug rebuild`
- `scripts\\build.bat Release rebuild`
- 新鲜 stage 启动验证和 ARMV4I 设备门

自动测试覆盖成功、资源失败、取消、stale、旧页保留、脚本超限、DOM mutation、事件、history、
表单、multipart、控件销毁和重复 teardown；设备门覆盖 HTTP/HTTPS、脚本、控件、SIP/IME、旋转、
DPI、软键和失败回滚。

## 内部页面与命令地址

EXE 私有 `app_internal_pages.c/.h` 用固定注册表区分页面、别名和命令；scheme 分流仍由
`app_url_router` 完成，HTTP(S) 保留现有网络路径，不修改 DLL ABI。

| 地址 | 应用策略 |
| --- | --- |
| `positron://newtab` | 默认起始页；无参数、空地址和主页菜单均使用此页 |
| `positron://about` | 说明、可取得的版本/系统信息、内部页面目录 |
| `positron://version`、`positron://system` | 提交前规范化为 about 的 version/system 片段，仅提交一次 history |
| `positron://history` | Browser 当前导航栈的只读快照，包括仍存在的前进项，最新在前，最多 16 项 |
| `positron://downloads` | 明确说明下载管理未实现，不伪造任务或下载记录 |
| `positron://settings` | 只读显示当前语言、起始页与 JavaScript 策略 |
| `positron://quit` | 仅地址栏直接提交可以正常退出，不创建页面或 history 项 |

页面名大小写无关，接受一个可选末尾斜杠，显示规范小写地址；新页面只接受注册的片段。
`welcome`/`controls` 的原地址与关键词保留，controls 查询参数不改写。未知地址保留旧文档、
标题和已提交地址。别名章节由 Core fragment 几何定位，不把别名作为第二个页面提交。

英语/简体中文 UTF-8 模板无 BOM、以 RCDATA 嵌入 EXE；所有内部页复用 Core parse/style/layout/
paint 和统一 CSS，不创建 ScriptSession，不请求外部资源。模板沿用 i18n 预算，动态 HTML
不超过 128 KiB；调用方拥有生成缓冲区，按明确长度解析后释放。history 自身不展示；刷新/
重新进入时重新读取快照，文本和链接属性均转义，只为受支持地址生成链接。点击按 URL 发起
新导航，不依赖旧索引；不提供持久访问日志、清除、删除或搜索。新页链接使用 Core 焦点查询，
固定最多 24 个焦点槽位，并在最终布局后设置焦点目录。

导航来源由 EXE 私有枚举携带，分派处拒绝链接、脚本、启动参数、重定向、表单和历史重放
执行 quit；Debug 与 Release 一致。获准时投递现有 WM_CLOSE，沿原关闭流程取消并等待网络任务、
销毁 native 控件、释放页面与 DLL。页面切换仍先完成候选构建和 beforeunload，再提交 history、
teardown 旧页、交换页面和恢复滚动。restart/kill/hang 不属于当前实现。

About 的应用版本由 EXE 私有适配器提供：Debug 预构建时内嵌主机本地日期时间，精确到秒，
不在启动时生成；Release 只读 CAB 安装写入的 `HKLM\Software\Positron\Version`，缺失、
非字符串、超长、未终止或非四段数值时使用本地化“未提供”。注册表不作为 Debug 版本来源，
安装版本不冒充独立复制 EXE 的文件版本，公开 DLL ABI 的显示和 CAB 版本生成流程保持独立。

系统章节使用中性标签：`GetVersionEx` 的 platform ID 决定内核名称，完整显示 major/minor/build
与非空的 `szCSDVersion` 扩展信息；未知 ID 原样显示数值，不写死 CE。`SPI_GETPLATFORMTYPE`
保留实际平台类型，PocketPC/Smartphone 不自动改名为 WM；`SPI_GETOEMINFO` 独立显示设备/OEM。
WM6.5.3 SDK 明确 `SPI_GETPLATFORMVERSION` 返回 CE 主/次版本，不用于查询产品发行版本。
EXE 只读 `HKLM\System\Versions\Aku`，原样显示更新包字符串；可选的 OEM `ProductName`、
`OSVersion` 字段存在时独立展示，不把它们当作跨 ROM 保证。不从 AKU、SDK、CE build、CAB 或
PocketPC 字符串推断 WM/WEH 名称、发行版本或 edition，不维护版本映射表。

补充信息通过可选 coredll `GetFileVersionInfoSizeW/GetFileVersionInfoW/VerQueryValueW` 读取
固定 ROM 路径 coredll.dll 与 aygshell.dll 的产品名、四段文件/产品版本；明确标为内核/系统外壳
组件，不冒充整套 OS 的发行版本。单次版本资源不超过 64 KiB，heap 缓冲解析后释放，返回切片
与语言表有界检查。注册表字符串最多 128 WCHAR，检查类型、字节长度、终止、嵌入 NUL、
UTF-16 与输出容量，原始文本统一转义后交给 Core。缺失、API 不可用、查询失败和非法数据
分别显示本地化状态；Debug 记录注册表错误码及组件查询失败阶段，Release 不含这些诊断。
未提供元数据的 ROM 不保证能识别其营销版本，组件产品名也不填入 OS 产品字段。

### 后端进入条件

正式启用前不创建用户 SQLite/JSON 文件，不调整 DB 主线发布依赖。后续应用设置、访问日志和下载记录
优先使用 `positron_db.dll` 本地 SQL；表结构、migration 和存储策略归应用，Browser 会话栈保持
独立。接入前必须通过 ARMV4I 文件数据库创建、中文读写、事务回滚、关闭重开、migration 失败、
空间不足和 journal 恢复设备门，随后才加入 EXE 链接、解决方案依赖和 CAB 输入校验。未通过时
后续才考虑版本化、原子替换的本地 JSON，不在当前批次引入回退存储。

设置的第一条纵切只覆盖起始页，候选值为 newtab、welcome、controls；不执行 quit、不储存任意
SQL，也不提前承诺网络起始页或标签恢复。EXE 私有 `app_settings` 表为单例行，应用拥有版本、
约束和绑定参数；迁移只消费 `PDb_ApplyMigration`，不读取 DLL 私有 metadata。不存在表时
执行初始迁移，已有表先验证版本/值，再用空脚本检查迁移版本；异常、缺行和未来版本均拒绝，
不以默认值覆盖坏数据或重建数据库。

每个服务只有一个 DB 所有者 worker，加载匹配 DLL、Open/migration/SQL/Close 和卸载全部在
该线程执行。UI 只提交固定类型请求和非阻塞读取 POD 结果；最多八项包含排队、执行中和未读
完成，队列满不丢已提交写入的应答。每个实例的请求 ID 不回绕，结果复制请求的 tab/generation，
调用者再核对当前可信页面及服务身份；这些字段本身不授予权限。保存仅在 COMMIT 后报告成功，
失败保留原错误快照，另外查询实际事务并记录 rollback 结果；当前保守策略拒绝继续 SQL，要求
显式关闭/重开，不盲目重试、删库或清除原值。关闭停止接收但排空已接受请求；UI 轮询退出状态，
worker 关闭数据库后才释放服务，不阻塞窗口、不跨线程 Cancel、不强杀线程。

进入生产前，只有显式 Debug 设置夹具可加载同部署目录的 DB DLL；`--selftest-settings-storage` 在
新建专用 Temp 目录生成测试数据库；普通启动与 Release 没有此入口，不查询用户配置。自检
覆盖默认值、FIFO/容量、owner-thread 拒绝、写入后同进程新 worker 重开、关闭排空及打开失败，
只精确清理本次创建的数据库/journal/目录。桌面 SQL 检查不等于 DLL、WinCE 文件系统或进程
异常恢复验收。通过 DB 可靠性门及本服务正式构建/设备门后，才接入启动与设置页。

设置页服务消费公开 Browser Register/Complete/Pump/Revoke，不直接注册私有 JS native 桥。
仅 EXE 从嵌入资源创建的可信 settings 页面可获授权，不按 URL、scheme、重定向或 DOM 字段
推断身份；普通 HTTP(S) 页面不注册。配置好的 session 在 bootstrap COMPLETE 后、可信作者
代码前注册，稳定的页面服务记录保留到 session Destroy 完成；生产内置页的脚本例外须单独
验收，不因此让所有内部页面执行脚本或请求外部资源。

白名单只有 `settings.read({})` 与 `settings.write({startupPage: URL})`，参数最多 128 UTF-8
字节；写入仅接受三个固定规范地址，读取不接受额外字段。JSON 解析/生成消费公共 JSON DLL，
不增加宿主解析器；结果为 `{startupPage: URL}` 或固定错误标识，不向页面暴露 SQL、文件路径
及 DB 原始诊断。submit 只验证、复制有界数据并排队，不做 I/O 或同步完成。
显式 Debug `--selftest-settings-services` 使用内存 DB 和独立裸 session 验证适配器，不冒充
实际 Core 设置页或 bootstrap 验收。`scripts/app_settings_gate.ps1` 消费正式 Debug 完整部署，
以 PID/精确终态、包哈希、前后 guest 无引用审计和 crash 门验证两组自检；不启用普通启动。

UI 非阻塞轮询共享 store，核对活跃授权、tab/generation、请求 ID 和操作后 Complete；一次
Pump 至多尝试一个回调，回调异常关闭该页服务，不重试交付。离页先停用再 Revoke，之后才
Destroy；已接受的设置写入仍排空，但迟到结果不触碰旧 session，也不交付给复用槽位的新页。

真实下载另立纵切：HTTP 先提供有界流式响应与取消接口，应用负责文件保存和任务调度；不使用
现有 1 MiB 完整响应体模拟通用下载。

### 内部页面验收

Debug 私有自检使用独立 Browser history 验证路由来源、别名、16 项与前进栈、转义、自过滤、
容量失败、Core 解析和布局焦点；测试夹具与日志不编译进 Release。完整 Debug 包部署后，
`scripts/internal_pages_gate.bat` 复用已有 RAPI helper 自动检查实际启动导航、规范化、章节定位、
单次 history 提交、无脚本、焦点和新增 crash dump；需要显式同意精确进程清理，门后留下 newtab。
自动门不替代真实地址栏输入、菜单、history 点击/刷新、加载中 quit、中英文、触摸、键盘、
软键、滚动、旋转和 DPI 人工验收。所有构建串行，使用正式 Debug/Release 配置与匹配完整包。

同文档导航的 Debug 私有启动自检使用独立文档/history 和隐藏 native viewport，直接调用 EXE
适配器验证会话保留、popstate/hashchange、双轴恢复、manual、空/缺失/legacy 锚点及 document
identity 隔离。`scripts/app_history_gate.bat` 消费正式 module-audit 门保留的完整 Debug 包，
重新检查 guest DLL holder、回读 EXE/九个 DLL 的 SHA256、启动自检并检查新增 crash dump；
不构建、选择设备或强杀进程。它不替代真实 HTTP 跨页恢复、地址栏回车、native 焦点、旋转/DPI
和人工滚动验收，测试夹具及诊断不编入 Release。
多标签 Debug 自检使用生产菜单、页面子窗口和独立 history，覆盖四页容量、输入/选区/
滚动/session 保留、beforeunload 取消、错误标签消息隔离、后台完成停放、候选脚本 timer
暂停/恢复及 stale 拒绝、失败保留旧页、关闭中的 worker 退休和最后一页替换；结束后重建
干净的启动 history。两个带控件页面还须检查切回与关闭新页后的 EDIT 编辑、SELECT 选项、
toggle 状态及可见控件 WM_PAINT 转发；只保留 HWND/窗口文本不算通过。
菜单另验证左右归属和顺序、重复重建、前进条件随 history/标签更新，以及实际后退/前进/刷新命令。
`app_history_gate` 必须取得 tabs 自检日志；不代替多页真实网络并发、
软键触摸、SIP/IME、旋转/DPI、内存压力和加载中退出的人工门。
同一独立 Debug 夹具用有界 event 暂停真实宿主 worker，以离线响应驱动原 parse/commit 路径，
验证 B 加载中 A 的 fragment/repeated/missing 跳转不取消、generation/文档/session 保留、
replaceState 后失败回滚，以及 C 替换与 stale B 隔离、B 成功提交；门要求 fragment-pending
自检通过。它不替代真实网络耗时、取消 transport 或真实页面点按的人工检查。
该夹具还用真实系统 timer 与批次间的窗口消息探针验证脚本分步初始化、pending 普通入口
拒绝、取消/关闭清理、旧 timer 不推进新候选、旧文档/history 保留、作者顺序与普通异常后
DOM 终态、初始化期间视口变化同步，以及编辑输入/选区在后台提交后保留；门要求
script-scheduling 自检通过。
它不证明单段长脚本可抢占，真实加载期间菜单/滚动/地址栏响应须立即人工复核。
加载标题的 Debug 私有自检覆盖 observer 映射、合并阶段、终态/未知阶段与 size/version
拒绝，以及所有阶段的纯文字、精确容量与失败清空；同一
独立候选夹具另覆盖 worker 正文进度、stale 请求标题隔离、标题更新不增加 layout 和成功后
本地化应用标题恢复。app_history_gate 要求 loading-title 自检日志，不代替窄标题栏截断、字体、
中英文实际显示和真实网络阶段的人工观察；夹具与诊断不编入 Release。
门可用受限 `-StartupUrl positron://system` 留在规范化后的系统章节，默认仍为 newtab；
另要求 Debug 系统信息自检通过，覆盖双语中性标签、非 CE/未知平台 ID、原始平台类型、
UTF-16/容量失败、注册表类型/长度/嵌入 NUL 拒绝、AKU 原样保留、版本资源切片边界与 HTML
转义；运行日志另记录实际查询结果及失败阶段。

同一 Debug 独立夹具使用真实页面窗口消息验证 DPI 阈值、抬起 click、拖动不误点、合并 MOVE
后的抬起、捕获/取消收尾与布局计数不变；`app_history_gate` 同时要求 pointer 自检通过。
自动消息不代替真实触笔/手指、native EDIT 拖选、SELECT popup、旋转和 DPI 的人工检查。

地址栏 Debug 自检使用独立隐藏窗口，验证标题/网址分离、实际字体宽度、滚动/填充周期、
点按切换、加载期间不覆盖输入和选区、失焦后的最新网址、无标题回退、绘制/resize/释放。
候选夹具还经生产消息分派与真实地址栏控件验证：B 加载期间点按编辑但不提交，generation、
history、文档/session 不变，B 完成且保留输入/选区；原生 EDIT 的显式 Enter 提交 C 才替换 B。
`app_history_gate` 必须同时取得 address-bar 自检日志；夹具及诊断不编入 Release。
它不替代真实地址栏回车、中文 IME、动画观感、页面滚动响应与旋转/DPI 人工验收。

## 公共接口与文档规则

现有阶段 0–4 的宿主接线不修改公共 ABI。应用存储服务同样只消费公共 DB 接口；新的可信页面
异步桥接及 HTTP 流式响应由对应 DLL 先提供可加性公开合同，再接入 EXE，不在宿主复制。若需要
File/Blob bridge，必须先在正确的 Core/Browser owner 中设计版本化 `Ex` 接口、容量、所有权和
失败语义，再由宿主接入。每阶段完成后更新 `docs/CAPABILITIES.md`、`.agents/HANDOFF.md`、
`.agents/KNOWN_LIMITATIONS.md` 和 `.agents/ROADMAP.md`；动态进度不追加到本文件。

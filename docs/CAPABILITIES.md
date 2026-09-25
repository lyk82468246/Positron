# 公共能力覆盖矩阵

本文件描述 Positron 七个顶层公共 DLL 的主干能力、当前边界和进入实现的条件。它是面向
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

`positron.exe` 的私有 `AppHostContext` 只收拢 WM6 窗口、页面句柄、导航候选、history、资源
和 DLL 初始化/清理的生命周期；Core/Browser/HTTP 仍拥有文档、URL、history、资源事务和
页面语义。阶段 0 保持离线页面、英语/简体中文 i18n 和主文档 HTTP(S) GET 不变；阶段 1
通过 EXE 私有适配层接入外部 CSS/`@import`、脚本发现和图片发现，沿用 Browser 的 required/
optional gate；阶段 2 再由 `app_script.c` 创建有界 ScriptSession，按 DOM 顺序执行网络
候选的 classic inline/external script，并把 DOM、事件、导航、滚动、焦点和生命周期回接到
当前窗口。脚本异常不回滚页面，bridge 初始化失败则 fail closed 为无脚本页面。阶段 3 的
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
通过后派发可取消 submit，EXE 只组合 URL-encoded GET 目标并调用既有导航候选。非法值、取消、
不支持的 POST/multipart/dialog、容量错误或候选失败不会替换旧页。内置 controls 页带 required
GET 表单；内置离线页面不创建 ScriptSession，其 inline script 不执行，因此脚本取消示例仍须在
网络 ScriptSession 页面验收。阶段 4 已由 EXE 接入脚本 `form.reset()`、native GET submit、脚本
`requestSubmit()` GET 和 direct `form.submit()` GET；后者按 Core/Browser 合同跳过 validation、submit
event 和 submitter。单行文本/密码 native EDIT 的 Enter 另调用 Core 隐式提交接口、派发可取消 submit，
并只接 URL-encoded GET；required 校验失败时 EXE 通过 Browser invalid callback 派发首个无效控件的
non-bubbling/cancelable `invalid` 事件，获准时按 Core 几何滚动并聚焦原生控件，取消时抑制默认反馈；
native submit 按钮复用该反馈。textarea Enter 保持换行。提交仍不新增 ABI；POST/multipart、dialog 与提交期 FormData
仍未接入。
有界 DOM mutation 返回 UI 消息泵后，宿主按 option 集合/标签
fingerprint 延迟重建 SELECT，同时保留 EDIT/SELECT 焦点。阶段 3 另接入带 id 且已布局的
`contenteditable` editing host：EXE 将其投影为同一窗口体系下的原生多行 EDIT，Core 保留有界
纯文本，Browser 处理 `beforeinput`/`input`；同一纵切还通过现有 Browser selection callbacks
在当前已提交且已物化的带 id EDIT 上同步 native range、selectionStart/End/Direction 与
`setSelectionRange()`，按逻辑 LF/UTF-16 偏移换算 WM EDIT 的 CRLF 索引，并把鼠标拖选、Shift+方向键
及焦点/捕获收尾通知为去重的 `selectionchange`。候选页、stale host 或未物化 surface 使用 Browser
有界脚本回退；该接线不新增 ABI，也不保留富文本子树或提供 Range/Selection 对象。宿主负责消息路由、
指针/按键接线、重排和 teardown。源码级 C89、Debug/Release 与仓库审计通过，但 WM6 仍须验收
native/script submit 与 reset、网络导航/失败回滚及原生控件；SIP/IME 与 file picker 不因该接线而宣称完成。
后续
接线顺序与阶段门见
[`positron_app/INTEGRATION_PLAN.md`](../positron_app/INTEGRATION_PLAN.md)。

## TLS：`positron_tls.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| TLS 初始化、CA 和全局清理 | `PTls_Init`、`PTls_AddRootCA`、`PTls_Cleanup` | 已实现 | mbed TLS 配置固定；CA/初始化失败返回 false，不暴露内部对象 | TLS 组件 README、正式构建；新增证书策略须补离线证书 fixture |
| 客户端连接、读写、关闭 | `PTls_Connect`、`PTls_ConnectVerified`、`PTls_Read/Write/Close` | 已实现 | 连接由 opaque handle 管理；读写长度使用公开 `int`，错误经 `PTls_LastError`/copy 读取 | TLS 设备/集成门；真实端点只作集成哨兵，不能替代离线错误分类 |
| 对端身份和指纹 | `PTls_ConnectPeer`、`PTls_PeerFingerprint`、`PTls_IdentityFingerprint` | 已实现 | 指纹输出固定为 `PTLS_FINGERPRINT_HEX_CAPACITY`；缓冲不足不部分写出 | 证书/hostname/指纹合同；新增校验必须保持 fail-closed |
| 服务端 identity/listener/accept | `PTls_IdentityLoadOrCreate`、`PTls_ServerListen/Accept` | 已实现 | identity/listener 由创建者关闭；listener flags 和参数非法时拒绝 | listener 资源上限和错误分类已有组件合同；新增并发策略需独立证据 |
| DTLS、完整证书链策略和异步握手 | 当前没有稳定公共承诺 | 暂缓 | 不通过增加无界状态或绕过证书检查来“补齐” | 只有真实 WM6 消费者和可固定预算的协议合同出现后再立项 |

## JSON：`positron_json.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| 解析和释放 JSON 树 | `PJson_Parse`、`PJson_Free` | 已实现 | 非法/空输入返回空 handle；调用方必须释放成功树 | JSON 组件回归和正式构建；复杂输入仍受移植库资源约束 |
| 基础 object/array/string/int 读取 | `PJson_Get*` | 已实现 | 失效 handle、缺失键和越界索引按头文件约定返回安全值；不返回借用指针之外的所有权 | 现有 JSON 调用方和脚本桥；如需布尔/浮点/类型查询，先补 ABI 合同和容量规则 |
| 有界序列化和字符串释放 | `PJson_Serialize`、`PJson_FreeString` | 已实现 | 分配失败返回空；字符串由 JSON DLL 分配并由对应入口释放 | 现有 JSON 合同；新增 size-probe 需保持旧序列化 ABI 不变 |
| schema、流式 parser、异步 DOM 映射 | 当前没有公共入口 | 暂缓 | 不引入无界 parser 状态或跨线程借用树 | 需要明确消费者、固定 token/深度预算和可回滚 fixture |

## HTTP：`positron_http.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| reference 解析 | `PHttp_ResolveReference` | 已实现 | 仅接受头文件规定的 HTTP(S) 参考；非法 scheme、端口、控制字符和截断安全失败 | TEST1064/1065/999 及 Core callback 复用证据 |
| HTTPS GET/POST、进度和 response 释放 | `PHttp_Get[Ex]`、`PHttp_Post[Ex]`、`PHttp_FreeResponse` | 已实现 | response body 受实现中的 1 MiB 上限；redirect 有界；取消/进度 callback 不得重入 HTTP 状态 | HTTP 离线/集成合同；超限、TLS、HTTP status 和网络错误必须保持可区分 |
| TLS 初始化和安全开关 | `PHttp_Init/Cleanup`、`PHttp_SetInsecure` | 已实现但默认安全 | insecure 只可由应用显式打开；证书/hostname 风险不能由 HTTP 静默吞掉 | TLS/HTTP 组合门；发布前继续审查旧 mbed TLS 风险 |
| CORS、referrer、loading/fetch-priority、缓存策略 | 当前没有稳定公共策略入口 | 有界待扩展 | 必须先确定 Browser/HTTP/Core owner、请求代际、旧页保留和取消语义；不能只按标准名称添加字段 | 需要真实页面或消费者证明阻塞，并提供 loopback/offline fixture |
| HTTP/2、WebSocket、完整代理和无限缓存 | 当前没有公共承诺 | 暂缓 | 超出 WM6 资源和当前请求模型 | 只有新的明确产品范围才重新评估 |

## Image：`positron_image.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| bitmap 解码、像素创建、信息读取和 GDI 绘制 | `PImage_CreateBitmapFromMemory/FromPixels`、`PImage_BitmapGetInfo`、`PImage_DrawBitmap` | 已实现 | 输入长度、像素尺寸和平台绘制失败返回 `PIMAGE_ERROR_*`；句柄由调用方释放 | Image 组件回归、Core paint 集成；native 像素观感仍需人工门 |
| SVG 解码、信息读取和绘制 | `PImage_CreateSvgFromMemory`、`PImage_SvgGetInfo/DrawSvg` | 已实现但有界 | libsvgtiny 错误映射到 `PIMAGE_ERROR_SVG_BASE`；不支持语法 fail closed | SVG 离线门和真实视觉人工验收 |
| bitmap 编码 | `PImage_EncodeBitmap[Ex]` | 已实现但按格式裁剪 | 缺少 encoder 返回 `PIMAGE_ERROR_UNSUPPORTED`；输出 buffer 由对应 free 入口释放 | 格式能力以头文件和组件 README 为准，不扩大为无界格式集 |
| picture/source 选择、generation 和事件 | Core/Browser 公开 relation/notification 组合 | 有界待扩展 | Core 选择、Browser generation，宿主 I/O/decode；过时事件不得改变 current source | TEST1299/1300 已验证 generation/终态；完整 loading 仍需消费者证据 |
| 视频、canvas、动画图像和完整色彩管理 | 当前没有公共承诺 | 暂缓 | 需要额外线程、内存和绘制合同 | 不作为当前 WM6 主干目标 |

## Script：`positron_script.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| context 创建、销毁和 classic evaluation | `PScript_Create[Ex]`、`PScript_Evaluate`、`PScript_Destroy` | 已实现 | 默认 heap 512 KiB，source 64 KiB；超时、内存和 fatal 错误有稳定错误码 | Script 自动回归；任何新 host object 先检查 heap/native-function budget |
| global JSON bridge | `PScript_Set/Get/CallGlobalJson`、register/unregister | 已实现 | global name 128 字节；native function 上限 29；callback 同步不可重入 | JSON/script 组合门；失败不修改旧 global |
| module/source provider | `PScript_EvaluateModule`、`PScript_SetModuleSourceProvider`、`PScript_LoadModule` | 有界待扩展 | module 名 128 字节、最多 16 个；缺 source/callback/超限安全失败 | 需要明确模块生命周期和资源取消；当前 Browser 默认不启用完整 module |
| Browser classic session | 由 Browser 使用同一 runtime | 已实现但 opt-in | Browser page heap、native function、listener、字符串、任务和 DOM 对象均固定上限 | Browser/TEST1303–1309 及相邻设备门；`javascript=0` 仍是默认 |
| worker、完整 ECMAScript host、无限异步任务 | 当前没有公共承诺 | 暂缓 | 不引入独立线程 runtime 或无界队列 | 仅在目标应用给出可控资源模型后再评估 |

## Core：`positron_core.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| HTML/CSS parse、style、layout、page extent 和 GDI paint | `PCore_ParseHTML`、`PCore_ParseCSS`、`PCore_StyleDocument[Ex]`、`PCore_LayoutDocument`、`PCore_PaintDocument*` | 已实现但有界 | 资源、节点、字符串、layout 和 paint 使用项目固定上限；parse/style/layout 失败不泄漏 handle | Core 离线 corpus、正式构建和设备视觉/自动门组合 |
| DOM/attribute/CharacterData/HTML mutation | `PCore_Node*ById`、relation、serialization 和 Ex mutation callbacks | 已实现但有界 | 失败前预检 id、节点形状、深度、child 数、UTF-8 和容量；成功后 layout retained 失效 | TEST1284–1298 及设备门；通用 Node/Fragment mutation 仍不承诺 |
| form owner、validation、selection、reset、modal 和 successful-control snapshot | `PCore_Form*`、`PCore_NodeFormControl*`、interaction/focus APIs | 已实现但有界 | owner、listed controls、fieldset/option state 和提交快照有界；非法/stale target fail closed | TEST1170–1188、1301–1302 和设备门 |
| multipart/default submission 和 FormData encoding | `PCore_MultipartSubmissionEncode`、`PCore_FormDataEncode` | 已实现 | body 上限 1 MiB；file read/free callback 同步借用；缺 callback、读取失败或容量不足不部分写出 | TEST1301/1302；宿主只提供文件 I/O 和 HTTP 调度 |
| File/Blob 对象、异步文件读取和浏览器式上传策略 | 当前只有 Browser 的有界内存 metadata/文本对象、Core DOM snapshot encoder 和宿主同步 file callback；没有 Browser JS FormData→Core body 交接入口 | 有界待扩展 | Browser 不暴露本地路径或持久 byte handle；Core 只接受自己的 FormData snapshot；缺 callback、权限/大小失败和 stale handle 必须在 body 输出前拒绝 | 本轮仓库审计只找到 `test_host` 夹具，没有生产消费者；只有真实消费者证明“picker→FormData→multipart”阻塞时才提升 |
| Range/Selection、MutationObserver、通用 live collection 和完整滚动树 | 当前没有公共承诺 | 暂缓 | 需要额外状态、事件队列和更大资源预算 | 保留在限制文档，不因标准名称直接立项 |

## Browser：`positron_browser.dll`

| 主干能力 | 当前入口/边界 | 状态 | 预算与失败边界 | 证据与提升条件 |
| --- | --- | --- | --- | --- |
| history、fragment、push/replace state、scroll snapshot | `PBrowser_History*` | 已实现但有界 | entry、URL、state 和 scroll snapshot 有界；失败保留旧 entry/旧页面 | next871、history/viewport fixtures 和设备门 |
| navigation candidate/resource transaction | `PBrowser_NavigationCandidate*`、`PBrowser_NavigationResource*`、commit/cleanup snapshot | 已实现但有界 | generation、required/optional gate、retry、fallback、cancel 和 cleanup 都固定；过时 worker 不能提交 | TEST1119–1127、设备门；宿主只调度网络/worker |
| script session、DOM/Event/form/input/lifecycle/viewport bridge | `PBrowser_ScriptSession*` callback tables and dispatch APIs | 已实现但有界 | heap、native functions、listeners、collections、strings、FormData/URLSearchParams、Storage 和任务队列固定 | TEST1138/1139、1152–1309 及相邻门；脚本默认关闭 |
| File/Blob metadata and multipart consumer bridge | Browser `new FormData(form[, submitter])` 只通过 callback 取得 Core successful-control metadata；`PCore_FormDataEncode()` 只接受 Core-owned snapshot，二者没有 JS pairs 的公共转换入口 | 有界待扩展 | Browser 不直接读文件、不暴露路径或 byte buffer；Core/宿主的同步 read/free、1 MiB body 和 stale/权限失败边界保持不变 | 本轮仓库审计只找到 `test_host` 夹具，没有生产消费者；优先候选仍为 picker→FormData→multipart，但未有消费者证据前不实现 |
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

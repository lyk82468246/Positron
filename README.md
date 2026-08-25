# Positron

Positron 为 Windows Mobile 6 Professional / Windows CE 5.2 补齐现代网络、数据、
图像、脚本和网页运行能力。项目既提供可被普通 WM6 C/C++ 程序独立使用的 DLL，也在这些
DLL 之上建设轻量浏览器与应用运行时。

项目仍处于持续开发阶段。基础 DLL 和一批浏览器纵向能力已经在 WM6 ARMV4I 模拟器或设备上
验证，但 Positron 还不是完整、兼容现代 Web 标准的通用浏览器。

## 能力概览

| 组件 | 用途 | 当前边界 |
|---|---|---|
| `positron_tls.dll` | TLS 1.2 HTTPS client、持久 peer 身份、双向证书、指纹钉扎和 IPv4 listener | ABI v2；基于已停止维护的 Mbed TLS 2.16.12，安全限制见子项目 README |
| `positron_json.dll` | UTF-8 JSON 解析和序列化 | cJSON 1.7.18 的稳定 opaque-handle C ABI |
| `positron_http.dll` | HTTP/1.1 GET/POST、进度回调、重定向和有界 HTTP(S) reference 解析 | HTTPS 使用 Positron TLS，明文 HTTP 使用 WinInet；解析失败时 fail closed |
| `positron_image.dll` | BMP/PNG/JPEG/GIF、SVG、像素缓冲和编码 | 设备位图格式依赖 WM Imaging codec；SVG 是受限子集 |
| `positron_script.dll` | 独立 JavaScript 执行服务 | Duktape 2.7.0；有时间、内存、源码和 native callback 上限 |
| `positron_core.dll` | HTML/DOM、CSS、布局、绘制、命中、表单和资源发现 | 基于移植的 NetSurf 3.11 组件；网页兼容性仍在扩展 |
| `positron_browser.dll` | 浏览器 session、history、脚本 session/bootstrap、受控 DOM/Event/关系/Attr/childNodes/Node identity 集合、表单/输入、URL、storage、编码、Headers、同步 Request/Response、bounded Promise、AbortSignal、timer/message pump、端口/广播、性能快照和窗口别名数据模型 | 不拥有窗口、网络或原生校验提示；这些 Web API 是 session 内 bounded 兼容切片，Promise 需宿主显式 microtask pump，DOM/Attr/childNodes/Node position 关系只读且按 id 寻址，完整 DOM、fetch/stream、core 事件传播及控件副作用仍由宿主提供 |

所有公共接口都使用稳定 C ABI、UTF-8 字符串、opaque handle 和明确的释放函数。NetSurf、
Duktape、Mbed TLS 等实现细节不暴露给调用者。

### 浏览器运行时

`test_host.exe` 是回归宿主和示例浏览器消费者，不是发布时的浏览器运行时。它已经接通：

- verified HTTPS 与明文 HTTP 页面加载；
- HTML/CSS 解析、外部样式和图片资源、分阶段异步抓取；
- GDI 绘制、滚动、动态 viewport/DPI、横竖屏重排；
- 常见 block、inline、flex、table、list、图片和基础 positioning；
- 链接导航、有限历史、表单控件、文本输入和一组 DOM 事件；主文档、外部资源和 HTTP
  3xx Location 共享 `positron_http.dll` 的有界 HTTP(S) reference 解析，支持目录相对、
  点段、query-only 和 network-path 引用；
- 显式开启时的 classic inline/external JavaScript 与受限 DOM/Event/location/history bridge。

浏览器 JavaScript 默认关闭。`positron_script.dll` 是独立的 JavaScript 引擎封装；浏览器
运行时由 `positron_browser.dll` 与 `positron_core.dll`、`positron_script.dll` 及宿主回调
组合。目前 history/session、浏览器脚本 context 的所有权、host JSON callback 注册、browser
bootstrap、DOM 只读（按 id 查询与 textContent 读取）、textContent 写入、attribute、input value、checked、
disabled、表单属性（含 `name`/`action`/`method`/`enctype`/`target`/`autocomplete`/`acceptCharset`）与 submitter `formAction`/`formMethod`/`formEnctype` 反射、控件属性 `placeholder`/`autocomplete`/`inputMode`/`type`、约束属性（含 `pattern`/`minLength`/`maxLength` 反射）、控件与受限 form-level `checkValidity()`/`reportValidity()`/`willValidate`/`validity` 查询、
`setCustomValidity()`/`validationMessage`（含固定英文内置 fallback，不做本地化）、form property（defaultValue/defaultChecked/selectedIndex）、navigation JSON 分发、同文档
location/history 事件分发、event 回调分发、native input、EDIT composition lifecycle policy、
keyboard、focus-family、EDIT change/post-change input、click、programmatic `HTMLElement.click()`
（file input 只到 typed click，系统 picker 仍由宿主 GUI 负责）、submit/reset、invalid/reportValidity、
file-input input/change、checkbox/radio input/change、SELECT input/change 和 native SELECT key
dispatch/default-allowed policy typed entry 已迁入；WM_IME/SIP、候选词窗口、原生文本 mutation、
其余 form/input 适配、core 事件传播以及窗口、网络、控件和 history/navigation side effect 仍由
宿主提供。
`reportValidity()` 只执行当前受支持的约束查询并派发可寻址控件的 `invalid` 事件；它不显示
原生提示、不自动聚焦/滚动，也不提交表单。
当前脚本 session 还提供受限的 `dataset`、FormData/Headers/Storage/DOM iterator、同步
Request/Response JSON 与 one-shot body、clone ownership、Blob/File metadata/slice/JSON、URL
authority/default-port 与 URLSearchParams pair/delete-value/按值查询和 mutation-safe snapshot、
cookie Max-Age 删除、TextEncoder/TextDecoder 选项、AbortSignal 静态工厂和 tags、setImmediate、
MessagePort/BroadcastChannel、structuredClone、PerformanceObserver/EntryList 快照与选项校验、
navigator 方法、`screen.orientation`、window aliases/open-close no-op 和稳定 element wrapper
identity；这些能力不发起网络、不创建后台线程，并由宿主显式 timer/message pump 或同步 snapshot
驱动。
当前还提供 bounded Promise 构造器、`then`/`catch`/`finally`、`resolve`/`reject` 和四种组合器；
reaction 必须由宿主显式调用 `PBrowser_ScriptSessionRunMicrotasks()` 推进，handler 与组合器输入均受
64 项上限。
当前还提供按 DOM id 的父子/兄弟、tag/name、form owner、`children`、`contains()`、基础
`compareDocumentPosition()`、受限 `matches()`/`closest()`、元素作用域 querySelector、
`form.elements` collection，以及 bounded 的 `label.control`/控件 `labels` 关联；这些都是
同步、只读、session-scoped snapshot，不是完整 live DOM。
`childNodes`、`children`、`form.elements` 和元素作用域 `querySelectorAll()` 结果还提供
有界的 `forEach()`、`keys()`、`values()`、`entries()`、默认 iterator 与 `Symbol.toStringTag`；
`children`/`form.elements` 保留 `namedItem()`，不承诺 live 更新。
next585 又把 `document.documentElement`、`document.head` 和 `document.body` 接入同一受控
DOM snapshot：它们拥有稳定 wrapper identity、`parentNode`/`parentElement`、child/sibling、
`children`/`childNodes`、基础 root selector 和 Node position/contains 视图。core 通过三个保留
结构 token 识别这些没有 HTML `id` 的节点；这仍是同步、只读、session-scoped 边界，不是通用
DOM 创建、mutation、live collection 或完整 selector 引擎。
next586 又增加 browser-owned 的 `document.doctype` 只读 snapshot，并明确 document 的
`childNodes` 顺序为 `[doctype, documentElement]`、`children` 仍为 element-only；它不新增 core
ABI 或通用 doctype parser/mutation。对应 `TEST662–681` 已由自动设备门覆盖。
next587 又为 document、DocumentType、HTML element、CharacterData 和 Attr wrapper 增加受控的
`baseURI`、HTML/XML namespace、`prefix`、`lookupNamespaceURI()` 与 `isDefaultNamespace()`；
`baseURI` 随当前 session URL 更新，未知 namespace 请求 fail closed，不实现 namespace parser、
节点创建或 mutation。对应 `TEST682–701` 已由自动设备门覆盖。
next588 又在 document 与 HTML element wrapper 上增加受控的 `getElementsByTagName()` 和
`getElementsByClassName()`：结果按当前 snapshot 的 DFS 文档顺序返回 HTMLCollection，支持
大小写归一、`*`、多 class token、`item()`/`namedItem()` 与迭代协议；element 查询排除 owner，
document 查询包含 `documentElement`，空白/未知输入 fail closed。它仍是静态 snapshot，不是
live collection、通用 selector 或 mutation API；对应 `TEST702–721` 已由自动设备门覆盖。
next589 又把同一受限 matcher 接入 document 的 `querySelector()`/`querySelectorAll()`，支持
tag、`#id`、class、有限 attribute、compound、`*` 和 `:root`，按 DFS 顺序返回首个匹配或
NodeList snapshot；空白和组合器仍 fail closed。对应 `TEST722–741` 已由自动设备门覆盖。
next590 又补齐 document named collection projection：`document.getElementsByName()` 按显式
`name` 精确匹配返回 NodeList snapshot，`document.forms`、`document.images`、`document.scripts`
返回静态 HTMLCollection；四者复用 `item()`/`namedItem()`、迭代器和 wrapper identity，但不
提供 live collection、通用 named properties 或 DOM mutation。对应 `TEST742–761` 及兼容/回归门
已由自动设备门覆盖。
next591 又补齐 `document.links` 与 `document.anchors`：前者按 DFS 收集显式 `href` 的 `a`/`area`，
后者按 DFS 收集显式 `name` 的 `a`，均返回静态 HTMLCollection，并复用既有集合协议和
wrapper identity；属性变化只影响后续查询。对应 `TEST762–781` 及缩减兼容/回归门已由自动
设备门覆盖，本批未重复旧的全量回归。
next592 又补齐 document 与 element 的 `getElementsByTagNameNS()`：支持通配或精确 namespace、
通配或大小写敏感 localName，document 包含 root、element 排除 owner，返回静态
HTMLCollection 并复用集合协议和 wrapper identity；空/未知输入 fail closed，不引入 XML/SVG
parser、live collection 或 mutation。对应 `TEST782–801` 及兼容/缩减回归门已由自动设备门覆盖，
本批仍未重复旧的全量回归。
next593 又在同一属性 snapshot 上提供只读的 `getAttributeNS()`、`hasAttributeNS()`、
`getAttributeNodeNS()` 及 Attr 的 `namespaceURI`/`prefix`/`localName` 元数据；null/空 namespace
表示无 namespace，`xml`/`xmlns` 映射已知 XML/XMLNS namespace，未知输入 fail closed。它不引入
namespace mutation、XML/SVG parser、live collection 或节点创建；对应 `TEST802–821` 及兼容/缩减
回归门已由自动设备门覆盖，本批仍未重复旧的全量回归。
next594 又为既有 `NamedNodeMap` 增加只读的 `getNamedItemNS()`：它复用 null/空 namespace、
XML/XMLNS 已知前缀、未知输入 fail closed、大小写敏感 localName、coercion、Attr identity 和
属性增删后的 map 观察语义；不引入 namespace mutation、XML/SVG parser 或节点创建。对应
`TEST822–841` 及缩减回归门已由自动设备门覆盖。
next595 又补齐受控的 `lookupPrefix(namespace)`：document、DocumentType、HTML element、
CharacterData 和 Attr wrapper 都能读取已知 XML 的 `xml` 映射；只有对应 `xmlns:*` Attr 返回
XMLNS 的 `xmlns`，HTML default、null/空值和未知 namespace 均返回 `null`。参数只做有限
String coercion，不解析 namespace declaration，不提供 prefix mutation 或新的 core ABI；
`TEST842–861` 定向门与 `TEST389,390–448,540,549,642–861` 缩减回归均已通过。
next596 又补齐元素 wrapper 的有界 `setAttributeNS()`/`removeAttributeNS()`：null/空 namespace
只接受无前缀名称，XML/XMLNS 只接受对应 `xml`/`xmlns` 前缀；未知 URI、未知前缀、空名和多重
冒号安全无操作。成功写入复用既有 attribute bridge，不实现完整 NamespaceError、namespace
declaration、XML/SVG parser 或新的 core ABI；`TEST862–881` 定向门和
`TEST389,390–448,540,549,642–881` 缩减回归均已通过。
next597 又补齐 `NamedNodeMap.setNamedItemNS()`/`removeNamedItemNS()` 与元素
`setAttributeNodeNS()`：跨 owner Attr 只复制名称和值，不转移 source owner 或 wrapper identity；
未知/非法 namespace、qualified name、非 Attr 输入和缺失删除均 fail closed。该批不引入新的
core ABI、完整 NamespaceError、namespace declaration、XML/SVG parser、节点创建或 live
collection；`TEST882–901` 定向门和 `TEST802-901` namespace 缩减回归均已通过。
next598 又补齐 Attr 的 bounded leaf-node 语义：`isId`、live `textContent`、空
`childNodes`/`hasChildNodes()`、null parent/sibling/child relations，以及 identity-based
`isSameNode()` 和受控 name/value `isEqualNode()`。Attr 的 `ownerElement` 仍只是 owner metadata，
不伪造成 element tree parent；`TEST902–921` 定向门和 `TEST802-921` 缩减回归均已通过。
next599 又补齐 Attr 的 detached-node relation 语义：`isConnected=false`、`getRootNode()` 返回
自身、`contains()` 只包含自身，`compareDocumentPosition()` 对非自身对象返回固定的
`DISCONNECTED|IMPLEMENTATION_SPECIFIC`。这仍不把 `ownerElement` 伪造成 parent，也不引入新的
core ABI；`TEST922–941` 定向门和 `TEST802-941` 缩减回归均已通过。
next600 又让 `childNodes` 返回的文本、注释和无 id 子节点 wrapper 暴露 bounded `contains()`：
wrapper 自包含，父元素可包含直接子节点，owner/兄弟/document/非法对象均 fail closed；实现复用
受控 relation bridge，不引入文本 mutation、节点创建或新的 core ABI。`TEST942–961` 定向门和
`TEST802-961` 缩减回归均已通过。
next601 又为 `document.doctype` snapshot 补齐只读 `publicId`、`systemId`、`internalSubset`：
HTML doctype 默认分别为 `""`、`""`、`null`，且不可重定义或删除；它不改变既有
DocumentType 的关系、identity、namespace 或 baseURI。`TEST962–981` 定向门和
`TEST802–981` 缩减回归均已通过。
next602 又为既有 `document.doctype` snapshot 提供独立、稳定、冻结的空 `entities` 与
`notations` `NamedNodeMap`：length 为 0，indexed/named/namespace lookup 为空，iterator 立即
结束，mutation 不改变状态，并带有 `NamedNodeMap` branding。它不解析 DTD/实体、不创建节点、
不增加 core ABI；实现 lazy 复用既有 map helper 以保持 browser heap 预算。`TEST982–998` 定向门
与 `TEST802–998` 缩减回归均已通过（两次均保留 `TEST999`）。
next603 又为普通属性 `NamedNodeMap` 以及 doctype 的空 `entities`/`notations` map 补齐有界的
`forEach()`、`keys()`、`values()`、`entries()` 和默认 values iterator；迭代器自身可迭代，Attr
identity 保持稳定，非法 callback fail closed。它仍是当前属性名的同步 snapshot，不提供 live DOM、
节点创建、DTD/实体解析或新的 core ABI。`TEST1000–1017` 定向门和拆开 `TEST999` 的
`TEST802–998,1000–1017` 缩减回归均已通过。
next604 又为静态 `HTMLCollection` snapshot 增加只读、不可枚举的 `id`/`name` 直达属性；
`item()`、`namedItem()`、迭代器和 wrapper identity 保持不变，保留属性名冲突时的既有方法，
`NodeList` 不获得这组 HTMLCollection named projection。该能力不引入 live collection、节点
创建、mutation 或新的 core ABI。`TEST1018–1035` 定向门与拆开 `TEST999` 的
`TEST802–998,1000–1035` 缩减回归均已通过，本批不需要人工页面验收。
next605 又为 `form.elements` 的重复 `id`/`name` 增加有限的 `RadioNodeList` snapshot：唯一匹配
仍返回原 element，重复匹配返回可迭代、只读的 `RadioNodeList`，其 `value` 只读当前选中 radio、
写入匹配值只选择对应控件；普通 HTMLCollection 继续保留首匹配 `namedItem()`。该能力不引入 live
`HTMLFormControlsCollection`、fieldset/label 语义、节点创建、mutation 或新的 core ABI。
`TEST1036–1053` 定向门和拆开 `TEST999` 的 `TEST802–998,1000–1053` 缩减回归均已通过；
为容纳新增 bootstrap，browser session heap ceiling 提升到 624 KiB，独立 `positron_script`
默认堆仍为 512 KiB。本批不需要人工页面验收。
next614 又把 label/control 的最小关联放入产品 DLL：`label.control` 支持显式 `for` 指向或
无 `for` 时的第一个嵌套 input（排除 hidden）、select、textarea 或 button；这些控件的
`labels` 返回按文档顺序的静态 NodeList。无效目标、无 ID label、非控件、hidden 和越界访问
均 fail closed；不承诺 live labels 或完整 labelable 类型集合。`TEST1062` 以及相邻关系回归
已通过自动设备门，无新增人工页面验收。
next615 又把 disabled ancestor fieldset 的有效状态迁入 `positron_core.dll`：第一个 legend
后代豁免，嵌套 fieldset 逐层继承，并统一用于约束验证、successful controls、默认 submitter、
控件信息和交互闸门；原始 `control.disabled` 仍只反射自身属性。`TEST1063` 与表单/关系回归
已通过自动设备门；native 窗口样式、invalid UI、SIP/IME 和文件选择器仍由宿主负责。
next618 又修正 WM6 native EDIT 对多字符 IME 候选结果的宿主落地：完整 `GCS_RESULTSTR`
通过一次 `EM_REPLACESEL` 写入当前 composition selection，继续沿 `EN_CHANGE` 回写 Core，
避免部分设备只留下首字符。`TEST1066` 覆盖多字节完整值；真实 SIP 候选窗口视觉与 OEM
行为仍需设备人工确认，不能从自动门扩大为通用输入法保证。
next619 又把完整 result 的产品事件事务加入 `positron_browser.dll`：
`PBrowser_ScriptSessionDispatchNativeEditResult()` 校验活动 composition 和有界 UTF-8
result，派发 `beforeinput(insertCompositionText)` → `compositionupdate` 并衔接 pending
native commit；宿主仍拥有 `ImmGetCompositionStringW`、`EM_REPLACESEL`、SIP 窗口和平台
视觉。`TEST1067` 覆盖生命周期、容量、commit、reset 和注销边界。
next620 又把文件输入选择的产品事务加入 `positron_browser.dll`：
`PBrowser_ScriptSessionDispatchNativeFileSelection()` 以有界 token 状态统一
BEGIN/COMMIT/CANCEL，并在提交后派发一次 `input(insertFromFile)` → `change`；宿主仍拥有
WM6 系统 picker、文件系统、`PCore_FileInputSetPath()` 和重绘。`TEST1068` 覆盖回调、容量、
取消和错误边界，`TEST262` 覆盖真实消费者路径；TEST232/263 的系统对话框视觉仍需人工确认。
next621 又把 programmatic `file.click()` 的 picker 请求仲裁放入
`positron_browser.dll`：每个脚本 session 只允许一个 pending/active request，重复请求合并，
OPEN/CLOSE/CANCEL 和 reset 对 token/文档替换保持有界生命周期；WM6 `PostMessage`、系统
picker、文件系统和路径写入仍由宿主负责。`TEST1069` 覆盖 ABI 阶段、重复请求、错误、
reset 和 session 隔离；`TEST262` 覆盖真实消费者接线。
next622 又把受信任物理锚点点击的默认导航接线放入 `positron_browser.dll`：
`PBrowser_ScriptSessionDispatchAnchorClick()` 先派发一次可取消 click，只有未被阻止时才
通过已注册导航适配器提交 ASSIGN；命中测试、网络、窗口和文档替换仍由宿主负责。
`TEST1070` 覆盖接受、preventDefault、导航拒绝、适配器错误以及宿主 helper 接线。
next623 又把受信任 checkbox/radio 激活的产品事务放入 `positron_browser.dll`：
`PBrowser_ScriptSessionDispatchNativeToggle()` 先处理可取消 click，只有宿主报告 Core
状态提交后才按一次 `input` → `change` 派发；取消、禁用和无状态变化不伪造事件。
宿主仍负责命中、Core mutation、重绘和 WM 键盘/鼠标副作用，`TEST1071` 覆盖产品契约与
共享 session 的 helper 接线。
next624 又把受信任 submit/reset 原生按钮的产品事务放入 `positron_browser.dll`：
`PBrowser_ScriptSessionDispatchNativeButton()` 以有界 CLICK/COMMIT/CANCEL 持有 click
取消和 submit/reset 顺序；宿主在 click 之后查询 Core validation，再执行默认提交/重置、
导航和重绘。禁用、preventDefault、无效校验、回调错误和取消不放行默认动作，`TEST1072`
覆盖产品契约与共享 session 的 helper 接线。
next625 扩展同一入口支持普通 `<button type="button">`：browser layer 仍派发可取消 click
并接受 bounded COMMIT，但不产生 submit/reset；宿主消费已接受的普通按钮默认动作，避免
generic click 后误关闭窗口。`TEST1073` 覆盖普通按钮的成功、取消、禁用、非法 kind 和
共享 session helper 路径。
next626 为同一组受信任原生按钮补齐宿主键盘焦点与激活路由：直接命中的 enabled button
记录 Core 文档/控件焦点，Enter 在 keydown 激活，Space 在 keyup 激活；重复 keydown 仍可
交给脚本观察但不会重复执行 click→commit。事务继续复用 `positron_browser.dll`，宿主只
负责 WM 消息、焦点生命周期和 Core 坐标；`TEST1074` 覆盖普通、submit、reset、取消、
禁用和窗口不误关闭的消息级契约。next626 本身不覆盖真实 OEM 键盘视觉、硬件按键映射或
label 转发；label→button 的自动事务见 next627，触摸与视觉仍属于人工/独立边界。
next627 将 label 命中的 native button 转发到同一 browser-owned CLICK→COMMIT 事务：
ordinary、submit、reset、取消和 disabled 目标不再绕过产品按钮策略；label 自身的 click
仍先按物理命中派发，目标按钮的默认动作只在 browser layer 未取消时执行。`TEST1075`
覆盖 label 转发、事件顺序、reset 状态、取消和 disabled 静默；真实触摸坐标、焦点视觉、
OEM 按键映射和其他 labelable 控件仍需人工或独立边界确认。
next628 将同一条 label 转发边界扩展到 checkbox/radio：启用脚本时，label 自身 click 后，
宿主按稳定 control index/kind 调用既有 `PBrowser_ScriptSessionDispatchNativeToggle()` 的
CLICK→COMMIT；Core 仍负责 checked/radio 状态变更和重绘，browser layer 继续负责目标 click
取消及一次 `input` → `change`。`TEST1076` 覆盖 checkbox、radio 互斥、preventDefault 和
disabled 静默；真实 label 触摸坐标、焦点视觉、OEM 行为以及 select/file/textarea 等其他
labelable 控件仍需人工或独立边界确认。
next629 将同一条 label 转发边界扩展到 text/password/textarea/select/file：启用脚本时，
宿主先通过既有 browser click adapter 派发目标 click，未取消且目标有效时才执行 native
EDIT/SELECT focus 或系统 file picker 默认动作。`TEST1077` 在真实 native EDIT/SELECT 子窗口
上验证 text、textarea、select 的 click/焦点顺序、取消和 disabled 闸门；文件 picker 模态
对话框、真实 label 触摸、SIP/IME 和 OEM 视觉仍需独立验收。
next630 又把脚本直接调用 `HTMLElement.click()` 的 text/password/textarea/select 目标
接入同一条 browser-owned typed click 路径：`positron_browser.dll` 新增兼容的 target-kind
和 `PBROWSER_SCRIPT_CLICK_DEFAULT_FOCUS` 语义，负责 disabled 抑制、click 传播和取消；
宿主按 DOM id 派发目标事件，并在默认动作回调中聚焦真实 native 控件。select 下拉弹窗、
WM/OEM 视觉和文件 picker 仍由宿主负责。`TEST1078` 在真实 render window 上验证四类
控件的 click/focus/focusin 顺序、select 取消和 disabled 静默；不把下拉展开写成产品保证。
当前还提供按 DOM id 的属性 count/name/value，以及 `getAttributeNames()`、`attributes`/`Attr`
和受限 NamedNodeMap lookup/iterator；`Attr.value`/`nodeValue` 复用既有同步 attribute bridge，
同 owner 的普通 map 更新可用，普通 `setNamedItem()` 跨 owner 仍 fail closed；namespace-node
入口的跨 owner 行为是受控名称/值复制，indexed access 只保证 0–7。浏览器 bootstrap
当前还提供按 DOM id 的有界 `childNodes` 快照：它保留文本、注释和无 id 元素的直接子节点顺序，
暴露 `Node`/`CharacterData` 的只读元数据、父子/兄弟及 element-sibling 关系、`item()`/iterator、
`firstChild`/`lastChild`/`hasChildNodes()` 和稳定 wrapper identity；next583 又补充受控
`isSameNode()`/`isEqualNode()`、`getRootNode()`、文档节点元数据、位置常量以及同一快照树内的
`compareDocumentPosition()`/`contains()`。这些方法只读、session-scoped 且对未知对象 fail closed；
它不创建通用 DOM 节点，也不写回文本节点。浏览器 bootstrap 使用十三个顺序 IIFE 和 624 KiB
session heap ceiling；独立
`positron_script.dll` 默认堆仍为
512 KiB。它不是第二套引擎。

next631 又补齐脚本 `HTMLElement.click()` 对带 `href` 锚点的受控激活：
`positron_browser.dll` 通过独立的 programmatic-anchor callback 复用 cancelable click 与
ASSIGN navigation，`positron_core.dll` 的 `PCore_LinkInfoById()` 负责按 DOM id 提供已布局
几何和 UTF-8 href；网络、窗口替换、无 href generic click 和文档生命周期仍由宿主负责。
`TEST1079` 已在 WM6 设备门覆盖接受导航、`preventDefault()`、容量/缺失和无 href generic
边界；`TEST1070` 继续覆盖导航适配器拒绝。
两者的关系和所有权见
[架构说明](docs/ARCHITECTURE.md)。

next632 又补齐同页 fragment 锚点的受控激活：以 `#` 开头的 `<a href>` 在 browser layer
中走 fragment history/hashchange，宿主用 Core 的片段查询把已布局目标滚入视口；未知目标
保持当前滚动，跨页链接继续走 ASSIGN。`TEST1080` 已在 WM6 设备门覆盖该分类、几何和失败
边界。

next633 继续补齐真实页面的同页历史行为：fragment 产生的同文档条目在
`history.back()`、`history.forward()` 和 `history.go()` 返回时恢复对应目标滚动；未知目标
保持当前位置且不发起网络或文档替换。`TEST1081` 与相关锚点回归已在 WM6 Debug 设备门
通过。

next634 又补齐了跨文档 history 的宿主视口状态：离开页面时保存当前条目的滚动偏移，
back/forward/go 重新加载目标文档后恢复该条目偏移；新条目从零开始，较短文档按当前 viewport
上限裁剪。滚动数组是 `test_host` 的窗口状态，不扩张 `positron_browser.dll` ABI；持久历史、
跨进程恢复和真实页面视觉仍不在承诺范围。`TEST1082` 已与相关锚点回归在 WM6 Debug
设备门通过。

next635 收敛了 fragment 解析的一个真实兼容缺口：`positron_core.dll` 新增
`PCore_FragmentInfoByToken()`，先按 UTF-8 `id` 查找，再兼容 HTML 旧式 `<a name>` 锚点；
宿主在调用前只做有界的 `%HH` 字节解码，保留 `+` 为字面字符，非法编码或未知目标不改变
视口。该 API 仍不负责 URL、history、网络或窗口副作用。`TEST1083` 与 1082–1070、999
的 Debug 设备门已通过 7/7。

next636 补齐了锚点元数据的产品通路：`positron_core.dll` 的 `PCore_LinkInfoByIdEx()`/
`PCore_LinkAtEx()` 返回有界 href/target/rel 快照，`positron_browser.dll` 的
`PBrowser_ScriptSessionDispatchAnchorClickEx()` 将它们随受信任 click/navigation 传给宿主，
并提供 `HTMLElement.rel` 反射；旧 href-only 入口保持兼容。`test_host` 仍负责 URL 解析、
网络、`_blank`/named window 的创建与生命周期，不能把元数据契约误认为完整多窗口支持。
`TEST1084` 与 1079–1083、999 的窄门通过 7/7，相关累计回归通过 22/22。

next637 将 anchor 的 raw target 在 `positron_browser.dll` 内分类为 default、`_self`、
`_parent`、`_top`、`_blank` 或 named，并把 bounded `target_kind` 随导航 ABI 传给宿主。
当前单窗口 `test_host` 接受前四种当前上下文策略，对需要新窗口的 `_blank`/named 请求
明确 fail-closed，不会静默替换当前页面；实际多窗口创建、复用、生命周期和跨窗口 history
仍待后续窗口宿主。`TEST1085` 与 1079–1084、999 的 Debug 设备门通过 8/8。

## 快速开始

### 前置环境

- Visual Studio 2008 SP1
- Windows Mobile 6 Professional SDK
- WM6 Professional Emulator 或兼容 ARMV4I 设备
- Python 3（用于移植脚本和仓库审计）

微软工具链和设备镜像不能随仓库分发，必须单独安装。

### 构建

在仓库根目录运行：

```bat
scripts\build.bat Debug build
```

正式脚本会调用 VS2008 的解决方案配置：

```text
Debug|Windows Mobile 6 Professional SDK (ARMV4I)
```

不要绕过解决方案直接拼装工具链。完整参数、输出位置和常见故障见
[构建与部署](docs/BUILDING.md)。

### 手工部署到模拟器共享目录

关闭仍在运行的 `test_host.exe`，然后运行：

```bat
scripts\stage.bat
```

脚本会先增量构建，再把七个产品 DLL、`test_host.exe`、`test_host.ini` 和 fallback
字体复制到 `C:\WMShare\`。在模拟器中把该目录配置为 Shared Folder，然后从对应的
Storage Card 路径启动 `test_host.exe`。

如需隔离候选包：

```bat
scripts\stage.bat Debug C:\WMShare\Positron-candidate
```

### 自动设备门

先在 WMDC/Device Emulator GUI 中连接一个设备；USB 真机和 DMA emulator 均可，当前连接
必须已经可用。gate 通过 32 位 RAPI 直接消费 WMDC 的当前设备，不枚举或绑定 VMID，也不会
连接、选择、启动、Cradle、断开或重置设备。连接完成后，在仓库根目录运行：

```bat
scripts\device_gate.bat -Candidate nextNNN
```

脚本会使用正式工程配置增量构建，创建隔离 staging，部署完整候选包，启动
`test_host.exe`，有限等待并回收日志，最后检查所选测试、设备指标、`ERROR`/`FAIL`、
TEST13 导航和 `TESTBENCH PASS`。每次运行前只回收设备端由 gate 自己命名的旧候选目录；
完整本地证据保存在被 Git 忽略的 `tmp/device-runs/`。连接阶段使用 32 位 RAPI
`CeRapiInitEx()` 并有 30 秒超时；WMDC 会话未就绪时 gate 会清理并退出，不会无限挂起或
把未启动的设备进程写成通过证据。

如果 RAPI 初始化报告 `0x8007007E`，WMDC 的旧式 COM 路径可能未被现代 Windows 正确展开。
运行一次下列脚本并确认 UAC；脚本只修复已知 WMDC/RAPI COM 注册，遇到未知值会拒绝修改：

```bat
scripts\repair_wmdc_rapi.bat
```

`-Candidate` 只命名本次设备端目录和证据目录；它不会切换 Git 版本，也不会改变测试选择。
实际代码来自当前工作区，实际测试选择来自当前 `test_host/test_host.ini`。定向测试、失败行为
和人工验收边界见[测试指南](docs/TESTING.md)。

## 仓库结构

```text
Positron.sln              VS2008 主解决方案
positron_tls/             TLS 1.2 HTTPS client 与局域网 peer 基础设施
positron_json/            JSON 公共 DLL
positron_http/            HTTP 公共 DLL
positron_image/           位图和 SVG 公共 DLL
positron_script/          独立 JavaScript 公共 DLL
positron_core/            HTML/CSS/DOM/layout/paint 产品边界
positron_browser/         浏览器 session/history 产品组合层
test_host/                回归宿主和示例消费者
samples/                  独立 DLL 示例
scripts/                  正式构建、stage、移植和审计脚本
docs/                     面向维护者和使用者的文档
.agents/                  仅供 agent 接管的动态状态
third_party/              直接 vendoring 的第三方源码和资源
netsurf-all-3.11/         NetSurf 3.11 上游源码快照
```

解决方案还包含若干移植后的内部静态库（`positron_expat`、`positron_hubbub`、
`positron_libcss`、`positron_libdom`、`positron_libjpeg`、`positron_libsvgtiny`、
`positron_netsurf`）。调用者应只依赖产品 DLL 的公共头文件，不应直接链接或包含
NetSurf、Expat、libjpeg-turbo 或其他内部接口。

### 子项目 README

每个解决方案工程的目录 README 都说明了工程输出、边界和调用方式：

- 公共 DLL：[`positron_tls/`](positron_tls/README.md)、[`positron_json/`](positron_json/README.md)、
  [`positron_http/`](positron_http/README.md)、[`positron_image/`](positron_image/README.md)、
  [`positron_script/`](positron_script/README.md)、[`positron_core/`](positron_core/README.md)、
  [`positron_browser/`](positron_browser/README.md)；
- 内部静态库：[`positron_expat/`](positron_expat/README.md)、[`positron_hubbub/`](positron_hubbub/README.md)、
  [`positron_libcss/`](positron_libcss/README.md)、[`positron_libdom/`](positron_libdom/README.md)、
  [`positron_libjpeg/`](positron_libjpeg/README.md)、[`positron_libsvgtiny/`](positron_libsvgtiny/README.md)、
  [`positron_netsurf/`](positron_netsurf/README.md)；
- 消费者：[`test_host/`](test_host/README.md) 和 [`samples/positron_image_demo/`](samples/positron_image_demo/README.md)。

`netsurf-all-3.11/`、`positron_tls/mbedtls/` 和 `third_party/` 下的 README 是上游或
第三方说明，保持其来源语境；它们不是 Positron 公共 API 文档。

## 文档

- [文档索引](docs/README.md)
- [架构与公共边界](docs/ARCHITECTURE.md)
- [构建与部署](docs/BUILDING.md)
- [测试与验收](docs/TESTING.md)
- [故障排查](docs/TROUBLESHOOTING.md)
- [历史里程碑](docs/history/README.md)
- [第三方组件与许可证](THIRD_PARTY.md)

Agent 的工作纪律由 [AGENTS.md](AGENTS.md) 定义，动态交接只维护在 `.agents/`。这些文件
不是面向普通仓库读者的产品说明。

## 开发约束

- 目标编译器是 MSVC 9.0；项目源码和移植产物必须满足 C89/VS2008 约束。
- 公共 DLL 保持 C ABI、UTF-8、opaque handle 和跨 CRT 安全的内存所有权。
- Windows Mobile 已有能力优先复用 WinInet、GDI、WM Imaging 和 CryptoAPI。
- 新协议、解析器、编解码器或 runtime 优先移植成熟上游项目，并记录固定版本、来源和许可证。
- 自动测试不能替代字体、布局、触摸、SIP、旋转和真实网页的人工观察。

贡献前至少运行：

```bat
python scripts\test_c89ize.py
python scripts\audit_repo.py
```

涉及 C 源码时，还应使用正式解决方案构建，并按风险运行对应设备门。

## 许可证

Positron 自有代码采用 [MIT License](LICENSE)。仓库包含 GPLv2、Apache-2.0、MIT、OFL、
zlib/IJG 等不同许可证的第三方组件；分发前必须同时遵守
[THIRD_PARTY.md](THIRD_PARTY.md) 和各上游目录中的许可证文件。

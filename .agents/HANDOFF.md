# 当前交接

本文件只描述当前产品基线、最近有效证据、未决风险和唯一下一步。逐批实现过程由 Git 历史保存，历史事故见 `docs/history/`，未来方向见 `ROADMAP.md`。

## 项目使命

Positron 为 Windows Mobile 6 / Windows CE 5.2 ARMV4I 提供模块化 TLS、JSON、HTTP、图像、脚本、渲染与浏览器会话 DLL，并提供正式的 `positron.exe` 独立应用消费者。公共边界保持 C ABI、UTF-8、opaque handle 和显式所有权；`test_host.exe` 只是回归宿主与示例消费者。

## 当前 Git 与工作区

工作区在 `main`；本轮 `positron_app` 的 `app_script.c/.h`、宿主生命周期和导航接线已完成。
C89、Debug/Release 构建和文档审计已通过，但不宣称设备通过；设备门按用户决定暂缓，
`RAPI=0x80072746` 不重复尝试。

- 已验证基线由 Browser/Core 的 DOM、CharacterData、DocumentFragment、表单、资源、脚本
  session、Storage、Headers、FormData、图像 generation 和特殊键 registry 合同组成；固定
  容量、失败回滚、wrapper identity、旧页保留和生命周期边界集中记录在
  [`docs/TESTING.md`](../docs/TESTING.md)、组件 README 和公开头文件中。最近的 TEST1303–1308
  已覆盖 FormData/URLSearchParams 配额、Storage/Headers 特殊键和 Browser registry；TEST1309
  另覆盖参考宿主的 WM_SHOWWINDOW 可见性接线；本轮新增的 TEST1310 只提供真实文件选择器到
  Browser FormData metadata 的 manual-only 证据，不新增公共 DLL 入口；它已在当前手动包中
  完成 GUI 验收，逐批证据由 Git 历史与 `tmp/` 设备记录保存，本文件不复制时间线。
- 设备门复用 WMDC RAPI；超时进程需在设备端结束，`tmp/` 证据不入库。
- 设备门继续假定用户已在 WMDC/Device Emulator GUI 手动连接恰好一个目标；RAPI 只复用
  当前会话，不连接、选择、cradle、重置或强杀设备。

## 近期已完成能力摘要

最近的产品纵切已覆盖导航/资源、history/viewport、生命周期、脚本、焦点、滚动/几何、
表单、selector、图像和有界 DOM/CharacterData mutation。HTML parser mutation、
text-only 与 bounded Element/Text `DocumentFragment`、detached Text/Comment/Element、
属性 facade、cookie、document.write 和 document.title 的合同与预算集中在
[`docs/TESTING.md`](../docs/TESTING.md)；宿主仍只拥有平台接线、调度、fixture 和断言。

Browser script session 由宿主显式推进，不复制 URL、DOM、Event、表单、图像或生命周期
语义。设备门的外置优先/内置回退、双空间预检、日志回收、超时恢复和完成后清理集中在
`scripts\device_gate.ps1`；`tmp/` 只保存本地证据。

## 当前中期里程碑

在保持 VS2008/WM6 约束的前提下，把已经形成的 Core、Browser 和平台宿主能力整合为可由真实页面驱动的有界网页运行时。重点是完成用户可感知的纵向能力、把通用语义留在公共 DLL，并以小型真实页面/交互语料库防止只增加孤立 API。

## 当前短期目标

next871 已完成 history 所有权边界修订：`test_host` 不再在 `PBrowser_HistoryCreate()` 失败时
运行自有的 URL/state/document-id 算法；宿主数组只保存从 Browser DLL 同步的断言和平台快照，
无产品 history handle 时安全失败。next872 又收束了 Core 的 URL callback：`wm_combine_url`
不再直接调用 WinInet 或维护第二套解析规则，而是把 base/reference 交给
`PHttp_ResolveReference()`，只做 host/path 到绝对 URL 的薄转换。1064/1065/999 定向设备门
已通过，崩溃转储增量为 0。next873 补齐参考宿主顶层 `WM_SHOWWINDOW` 到
`PBrowser_ScriptSessionDispatchVisibility()` 的平台接线；1309/1138/1139/999 定向门验证
隐藏→显示顺序和重复消息去重，崩溃转储增量为 0。上一批 next870 的 Core multipart 返回码 3
契约校正、Browser registry/dataset snapshot、TEST1308 和 `1308,1307,1306,999` 设备门仍保持
有效；Headers/Request/Response、Storage、FormData 和 URLSearchParams 仍保持有界安全合同。
当前短期目标已推进到独立 `positron.exe` 阶段 B 的主文档网络纵切：应用通过公开
Core/Browser/HTTP import library 启动内置离线页面，并可在 worker 中发起绝对 HTTP(S) GET；
Browser candidate/resource transaction 负责 generation、取消、stale 和 required-document
commit gate，页面只有在 Core 完成 parse/style/layout 后才替换。EXE 私有资源支持英语/简体中文，
按 WM6 UI 语言选择，其他语言回退英语，stage 无外置语言文件。阶段 2 已加入 EXE 私有
`AppScriptContext`，网络候选按 DOM 顺序执行有界 classic script，并接回 DOM、事件、导航、
滚动、焦点、任务和 teardown；脚本异常不回滚，bridge 初始化失败则关闭脚本能力。
清单见 [`positron_app/README.md`](../positron_app/README.md)。Debug/Release 构建、C89 和仓库审计
已通过；阶段 1 已把外部 CSS/`@import`、脚本发现和图片发现接入同一候选资源事务，required
CSS 失败会阻止提交，optional 资源失败保留 Core fallback；阶段 2 已把网络候选的 classic
ScriptSession 接入同一提交前流程。阶段 1/2 网络、脚本、回滚及标题/窗口 UI
仍需设备验收，不能写成设备基线；最近一次传输在复制 `positron_script.dll` 时以
`RAPI=0x80072746` 失败，TEST999 未启动。native 表单、SIP/IME、picker、书签和持久设置仍
不在当前范围内。ROADMAP.md 已复核。稳定边界见 [`docs/TESTING.md`](../docs/TESTING.md)

与 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

阶段 0 已以 `app_host.h/.c` 收拢 EXE 私有 `AppHostContext` 的页面和 DLL 生命周期；阶段 1 新增
`app_resources.c/.h`，把资源解析、注册、下载、重试和回收留在 EXE 私有适配层，不改变公共 ABI。
C89/Debug/Release/审计已通过；设备门结果见上。

## 已验证产品事实

### 公共边界

- 顶层公共 DLL 为 TLS、JSON、HTTP、image、script、core 和 browser。
- `positron.exe` 是正式的独立应用消费者；它拥有 WM 窗口、native 控件、输入路由和应用策略，
  不编译公共 DLL 实现源文件。`test_host.exe` 仍只拥有回归 fixture、平台接线和断言。
- NetSurf/libcss/libdom/hubbub、Expat、libsvgtiny、libjpeg 等移植工程是内部实现依赖。
- 独立脚本和浏览器脚本共用 Duktape；浏览器 JavaScript tracked 默认仍为关闭。
- 通用 URL、history、DOM、Event、表单、图像和脚本 session 语义位于对应公共 DLL；宿主保留 WM 窗口、消息、控件、SIP/IME、picker、导航调度和资源 I/O。
- Core 的 URL resolver callback 由宿主接线，但解析规则来自 `positron_http.dll` 的
  `PHttp_ResolveReference()`；宿主不再直接链接 WinInet URL 合并函数，也不复制 HTTP(S)
  reference/fragment/authority 规则。
- Browser 的 visibility lifecycle 由公共 DLL 保持状态和事件顺序；参考宿主在顶层
  `WM_SHOWWINDOW` 中只传递 hidden/visible 值，重复值、`pagehide`/`pageshow` 顺序和 teardown
  仍由 Browser 决定，其他宿主必须自行完成等价的消息接线。
- 七个顶层 DLL 的主干能力状态、预算、错误边界和提升条件集中在
  [`docs/CAPABILITIES.md`](../docs/CAPABILITIES.md)；“有界待扩展”不等于已支持，未实现入口
  只能在不修改状态的前提下返回稳定 unsupported 类错误。
- Core 的 multipart wire encoder 也属于公共表单语义：
  `PCore_MultipartSubmissionEncode()` 负责 form default submission，
  `PCore_FormDataEncode()` 负责独立 FormData snapshot；两者共享 bounded boundary/CRLF/字段
  顺序/quoted metadata/binary file bytes。宿主只实现同步 file read/free callback、HTTP 调度
  和 buffer 生命周期，不再复制 multipart 拼装规则。

## 独立应用消费者接管结果

本轮新增 `positron_app/`，未新增公共 export。应用消费公开
`PCore_*`/`PBrowser_History*`/`PHttp_*` ABI；Core 负责 HTML/CSS style/layout/paint 与链接/焦点几何，
Browser 负责有界 history 和 navigation candidate/resource transaction，HTTP DLL 负责 transport，
窗口、native EDIT、WM6 Shell command bar、菜单、worker、消息泵、输入优先级和页面 swap 由应用拥有。
`test_host` 没有编译应用实现源文件，也没有承接应用 UI。

阶段 A 的 `welcome`、`controls` 及对应的 `https://positron.local/...` 地址继续离线工作；
阶段 B 当前支持绝对 HTTP(S) 主文档 GET，失败、取消或 stale 响应不会替换旧页面。菜单、softkey、
状态标题、错误框和两页离线内容由 EXE 私有英语/简体中文资源提供；README 已给出语言回退及
交互验收项。阶段 1 已接入外部 CSS/`@import`、脚本发现和图片发现：CSS 属于 required gate，
脚本/图片属于 optional fallback；阶段 2 的网络候选已按 DOM 顺序创建并执行有界 classic
ScriptSession，接入 DOM/属性/事件/导航/滚动/焦点/生命周期桥。Debug/Release 构建通过，
但网络页面、脚本和导航失败回滚仍需设备人工验收，不能写成设备基线。

原有 File/Blob→FormData→multipart 候选仍保持“待取证”：本应用当前没有表单或 picker，不能
把阶段 A 的离线导航消费者误写成上传消费者。只有阶段 B 的真实流程形成同步 file callback、
容量、权限、取消和失败回滚证据后，才提升该候选。

### 当前网页能力

- HTML/CSS/DOM、整树 style、NetSurf layout/redraw、GDI 绘制与资源缓存已形成正式 Core 路径。
- 常用 block/inline/flex/table、图片/SVG、背景、列表、有限定位、表单控件、验证、提交、reset 与 FormData successful-control snapshot（含可选 submitter、formdata 事件）已有设备回归；这不代表完整 CSS/HTML 或完整 Web API。
- Core 的有界 image-map 命中已与链接、area 几何、active/hover 和坐标事件 target 统一：
  已布局 `<img usemap>` 最多解析 64 个 linked `<area>` 和 64 个坐标，支持
  default/rect/circle/poly，并把自然坐标缩放到渲染尺寸；Browser 只接收 Core/宿主的
  click 事务，不复制 map 解析。
- `<img srcset>` 的 Core 选择最多接受 16 个、每个 URL 最多 2047 字节的同类候选：正
  密度 `x` 按 viewport DPI 选择，正宽度 `w` 按 `sizes` 解析出的源尺寸与 DPI 选择；
  `sizes` 仅支持 px/vw/vh 及单一 `(min-width|max-width: <length>)` 条件，缺失或不支持
  时按 100vw。图片发现、缓存、retained decode、布局自然尺寸、complete 和 Browser
  `currentSrc` 共用该结果；混合/畸形候选安全回退。绝对 URL、CORS/referrer 和完整
  loading 策略仍不支持。
- `textContent` 与非编辑元素的 `innerText` setter 通过 Browser 的既有 text callback
  调用 Core；空元素的 getter 返回成功的零字节字符串。成功 setter 后 Core 丢弃 retained
  layout，Browser 刷新目标的
  `children`/`childNodes`/query snapshot，并让旧的无 id 文本 wrapper 保留数据但变为
  detached。contenteditable 的 `innerText` 复用同一失效规则；Text 的
  `Text`/`Comment`/`CDATA` 的 `nodeValue`/`data`/`textContent` 与四个 CharacterData
  mutator 保持 child list 和连接中 wrapper 身份，非法/失效目标 fail closed。`Text`/`CDATASection.splitText()`
  通过 Ex3/`PCore_NodeSplitTextChildById` 对 direct CharacterData child 插入紧邻 Text sibling，保留
  原 wrapper、旧 NodeList snapshot，并对 UTF-16→UTF-8 不可表示边界 fail closed；关系 50 的
  `wholeText` 由 Core/libdom 拼接逻辑相邻 Text/CDATA，非文本边界和无效 child fail closed，
  detached wrapper 保留最近一次数据快照。Ex4/`PCore_NodeReplaceWholeTextChildById` 将一个
  有界相邻 Text/CDATA 段替换为目标 wrapper，目标移动到段首，其他 CharacterData wrapper
  变为 detached，成功后使 retained layout 失效。
  `Node.normalize()` 通过 Ex5/`PCore_NodeNormalizeById` 删除空 Text/CDATA，并将每段连续
  Text/CDATA 合并到第一个非空节点；Browser 对带 id 的后代 wrapper 按受控顺序递归并保持
  首个非空 wrapper 与旧 snapshot。write Ex6 的 `Element.append()`/`prepend()` 按零至四值为带 id 元素
  创建 Text child；mutation Ex6 另按未过滤位置移动 existing element。Ex2 的 `Text.remove()`
  与 `Element.removeChild(Text)` 删除连接中的
  direct Text child，Ex3 再为 Comment/CDATA 提供相同的 `remove()`/`removeChild()`
  路径。各路径都保留 detached wrapper、刷新父级 snapshot，并由宿主在成功后重排；
  Browser 现在另支持最多四个 primitive Text 的 text-only `DocumentFragment` staging，及
  最多四个 detached Element/Text/Comment/CDATA 根的 bounded staging；后者可由指定的 Element mutation
  一次性 parser-backed 消费并保留 staged wrapper identity，或在 Fragment 自身上与另一
  Fragment 做有界组合。Fragment-owned detached Element 的 sibling/element-sibling getter
  按当前 staging 顺序实时读取；未归属的 detached Element 不伪造关系。detached
  Browser-created Element 另支持最多四层、总计 64 个 Element、每个父级 64 个 child 的
  嵌套图；每个 Element 必须有唯一 id，clone、递归 textContent、attach、remove/reinsert
  和 alias lookup 保持 wrapper/owner identity，超限、重复 id、自引用和不支持节点在
  mutation 前 fail closed。嵌套 DocumentFragment、通用节点、超出预算的 reparent、其他
  删除、MutationObserver 与除 `Element.getElementsByTagName()` 外的完整 live collection
  仍未实现。detached Element/Fragment 的 `normalize()` 仍仅限最多 64 个 direct
  CharacterData（Text/Comment/CDATA）或四个 Fragment 根；嵌套 Element、溢出和不支持节点
  在 normalize 前 fail closed。
- Browser 的 `Node.cloneNode(deep)` 返回 Browser-owned detached snapshot：浅/深克隆保留
  有界 element 属性、子节点顺序、parent links 和独立数据，超限或不支持类型 fail closed；
  它不改变 Core 文档、retained layout 或事件 listener。
- `Element.innerHTML` setter 通过 Browser write Ex8 调用 Core 的
  `PCore_NodeSetInnerHTMLById`。Core 在同一 document 中用 UTF-8 fragment parser 预检节点、
  深度、direct-child 数量和 id 冲突后再替换子树；失败不改变原子树，成功保留目标身份并
  使 retained layout 失效。Browser 只在其 256 节点/64 层 wrapper reconciliation 预算内
  让旧 wrapper 变为 detached，再刷新目标的 `children`/`childNodes`/query snapshot；
  同一 parser 边界的 Ex9 `insertAdjacentHTML()` 在四个位置插入片段并刷新受影响
  target/parent snapshot；Ex10 `outerHTML` setter 则以单一 Element 根替换目标或以空字符串
  移除目标，并刷新父级/id cache；三条 HTML 路径都不是通用 `DocumentFragment`、mutation
  event 或资源执行 API。Browser 的 fragment staging 另有 text-only 与 bounded Element/Text
  子集，但不新增 Core fragment ABI。
- Browser 层还提供由宿主显式驱动的 viewport resize 合同：`PBrowser_ScriptSessionNotifyResize` 更新 CSS viewport/DPR 和动态 `screen` 方向，值变化时同步派发一次 window `resize`；同一 session 的 `screen.orientation` 对象保持身份稳定，方向翻转时在媒体列表刷新后先派发一次可信 `change`，再进入 visual/window `resize`；调用不负责 Core relayout 或 frame scheduling。
- 同一 Browser session 还提供布局视口对应的 `visualViewport`：`width`/`height` 与 CSS viewport 同步，`pageLeft`/`pageTop` 与 page scroll 同步，`scale` 为 1、offset 为 0；有效 resize/scroll 先派发 visual viewport 事件，再派发 window 事件，并对重复快照去重。TEST1133 覆盖该合同。
- Browser history entry 同时拥有非负的 `(scroll_x, scroll_y)` viewport snapshot；新 document entry 和同 URL 新 document 从零开始，`replaceState`/traversal 保留目标值，`pushState` 新 entry 从零开始，history 裁剪会同步搬移 snapshot。Browser 不访问窗口、不知道 Core 的页面 extent；宿主读取 `PCore_DocumentWidth/Height` 后保存/读取并对两个轴 clamp/apply。
- Browser script session 的 `PBrowser_ScriptSessionGetScrollRestoration` 暴露脚本的 `auto`/`manual` 策略。宿主在非 fragment history traversal 前只对 `AUTO` 自动读取并应用 entry snapshot；`MANUAL` 保留当前 viewport，查询失败按默认 `AUTO` 处理。fragment reveal 与显式脚本滚动不受该自动恢复门影响。
- Browser script session 的 `window.scrollTo`/`scrollBy` 经过 `PBrowserScriptScrollCallbacks` 交给活动宿主；宿主返回实际 page 坐标后，Browser 只派发一次 `scroll`。宿主的物理滚动路径用 `PBrowser_ScriptSessionNotifyScroll` 反向同步，重复坐标不派发事件，回调内不会重入 runtime。
- Core 的布局 relation 在成功 layout 后提供单元素 border-box union、最多 16 个
  inline 行片段以及 retained overflow 的滚动/scrollport 快照；Browser 用这些有界
  快照生成 viewport-relative `getBoundingClientRect()`/`getClientRects()`，并执行
  页面级或最近 addressable ancestor 的有限 `scrollIntoView()`，也支持显式
  `container:"all"` 的有界祖先链。未布局、无对应 box
  或没有正尺寸片段时分别返回全零/空集合；不承诺 transforms、Range/Selection、完整
  scroll tree、scroll chaining、pinch zoom、平滑滚动或视觉像素精度。
- Core 的 `PCore_DocumentWidth` 与 `PCore_DocumentHeight` 在最近一次 layout 后报告 page-level extent；宽度包含页面内容的水平溢出且不小于 layout viewport。宿主把同一 `(scroll_x, scroll_y)` 用于 paint、命中测试、fragment reveal、滚动条和 native child reposition；嵌套 overflow 的完整树、chaining/anchoring 和匿名目标仍未实现。
- next702–704 已补齐有界的元素 overflow 滚动：Core 对带 DOM `id` 的常见 block/replaced/flex box 保留 scrollbar offset，关系 38/39 返回/设置 CSS 像素并执行 clamp，关系 40–43 为 Browser 提供 axis availability 和 client-edge origin；`PCore_OverflowPointer`/`PCore_OverflowScrollSnapshot` 把 WM pointer 的目标和位置交给宿主。Browser 通过 `PBrowserScriptScrollInfo.element_id` 接入 `Element.scrollLeft`/`scrollTop`/`scrollTo()`/`scrollBy()` 和有限 nested `scrollIntoView()`；默认选择最近祖先，`container:"all"` 沿最多 64 层向外处理，`PBrowser_ScriptSessionNotifyElementScroll` 更新脚本状态并去重派发目标元素 scroll。完整 scroll tree、scroll chaining/anchoring、scroll-margin、smooth/inertia 和非 addressable/匿名目标仍不支持。
- 页面导航保留旧页到候选文档成功提交；主文档和资源网络阶段与 UI 文档操作分离。Browser candidate handle 拥有 generation、取消/退休、提交资格和结果分类，宿主用它门控 worker 完成/进度消息并在 worker 收尾后回收旧候选；旧候选不能越过 generation 门。layout/swap 前，宿主通过 `PBrowser_NavigationCommitGetInfo` 读取独立 candidate/resource 的组合 decision 与 `can_commit`，不在宿主复制失败/过时提交规则。
- Browser 资源事务按 URL 去重并拥有 `pending`、`ready`、`failed`、`cancelled` 四种终态、成功字节、失败分类、required/optional gate、transport 重试预算、最多 4 项 hash-only 摘要和 fallback family 计数。宿主负责网络 I/O、worker、取消/重试时机和页面提交，只保留 URL→resource-index 短引用；HTTP、resolve、budget、memory 和取消不重试，取消也不会重新暴露为可用缓存。
- 导航 request 在 worker join 后由宿主先收敛失败/过时资源，再调用 Browser 的 `PBrowser_NavigationCleanupGetInfo` 复制 cleanup decision、candidate/resource 终态、pending、`can_release`、hash-only failure summary 和 fallback 计数；复制值在 candidate/resource handle 销毁后仍可用于日志。TEST1127 同时覆盖 pending/terminal decision、required failure、optional fallback、取消、stale、清理前复制、释放后快照存活，以及成功/失败 `pcore_navigation_finish` 的真实回收路径。
- Core 的 form owner relation 对支持的 input、select、textarea、button、fieldset、img、object、output 解析
  最近祖先或显式 `form="id"` 目标；Browser 的 `Element.form` 与 `form.elements` 复用这条
  规则，后者从整棵文档按顺序返回跨树 listed form-associated 元素的有界 snapshot，img
  仅保留 owner、不进入 collection，空值/无效目标不回退祖先。fieldset/object/output 只进入
  relation collection（以及 output 的 labels），
  不进入 successful-control、提交或 FormData；Core validation、reportValidity、
  successful-control/multipart submission、
  dialog/default-submit、reset 和按坐标的 submit/reset 激活仍只消费可提交控件 owner。
- `<option>.form` 由 Browser 沿最多 64 层可寻址父链定位所属 select，再复用其 form owner；
  嵌套 optgroup、显式 `select form="id"` 和 attribute mutation 已由 TEST1185 及设备门验证，
  缺失或无效 owner 安全返回 `null`，不改变 Core ABI。
- `select.type` 依据 live `multiple` attribute 提供只读的 `select-one`/
  `select-multiple` 模式，`optgroup.label` 反映 label attribute 且缺失回退为空字符串；
  `option.label` 的文本 fallback 保持不变。TEST1186 及设备门已验证这组 Browser metadata，
  不创建 native SELECT 或改变 layout/paint。
- 支持的链接、summary、native EDIT/SELECT/button/file 等目标，以及带有效非负 `tabindex` 的普通布局元素按有界顺序响应 Tab/Shift+Tab：正值升序、同值 DOM 稳定排序，随后零/缺省组；负值、disabled/hidden/stale 目标和 file picker 仍被排除。Browser 报告活动 modal id 后，宿主可用 Core 的 scoped snapshot 将顺序焦点限制在 dialog 子树；宿主仍同步焦点事件、原生焦点和滚动可见性。
- `<dialog>` 的 show/showModal/close/requestClose、returnValue、cancel/close 事件、活动 modal id 查询、宿主驱动的 Escape 请求桥接、有界 backdrop 指针策略、`method="dialog"` 默认动作和 Core modal paint 已形成契约。显式点击、脚本 `click()` 和单行输入隐式 Enter 都遵循 validation→可取消 submit→直接 close/returnValue；CSS `::backdrop`、透明合成、多个 modal 和跨文档 modal 仍未实现。
- 单元素 `contenteditable` 已形成 Core/Browser/宿主边界：Core 解析祖先继承并限制合法 UTF-8 纯文本 mutation，Browser 暴露 `isContentEditable`/`innerText`、`selectionStart`/`selectionEnd`/`selectionDirection` 与 typed input 事务，宿主为带 id 且已布局的有效 editing host 创建有界 WM multiline EDIT 代理。允许的 `WM_CHAR` 在默认处理完成后回读最终文本并派发 `input`；取消的 `beforeinput` 不修改 Core，重复/提前 `EN_CHANGE` 不会制造空事件。Browser 选区偏移使用 UTF-16 code unit；宿主将 WM EDIT 的 CRLF 位置转换为逻辑 LF，并在可用时同步原生 HWND。无修饰鼠标拖选以及 Shift/方向键扩展由宿主短暂保存 anchor；捕获丢失、取消模式和焦点切换会先结束手势，再通过 Browser 的去重通知入口刷新范围。宿主对 `WM_PASTE`/`WM_CUT` 只接受有界 `CF_UNICODETEXT`，把规范化后的 UTF-8 data 交给 `beforeinput`，允许后执行 native default，再提交 Core/input 和折叠选区；`WM_COPY` 只写入非空的有界 Unicode 选区，折叠选区保持现有剪贴板不变；格式缺失、超长或读取失败时 fail closed。为兼容 WinCE 原生剪切的内部重入，宿主只在外层 `WM_CUT` 默认处理期间放行同一 HWND 的嵌套 `WM_COPY`。每页最多 16 个宿主、文本最多 8192 UTF-8 字节；嵌套继承后代不重复创建 host。
离线 compatibility corpus 已覆盖导航资源事务、候选提交与回滚、history/viewport、页面生命周期、脚本调度、焦点、滚动、Core/Browser 几何和显式 form-owner 组合。每项测试的 fixture 与断言说明统一见 [`docs/TESTING.md`](../docs/TESTING.md)；handoff 只保留当前门和仍未完成的边界。

### 当前测试入口

- `TEST_MAX_NUMBER`：1310。
- tracked `test_host/test_host.ini`：`auto=1`、`javascript=0`，选择 `13,20,27,56,58,62,64-67,73,75,1217-1308,999`；1309 是定向宿主可见性门，1310 是 manual-only FormData 证据夹具，两者都通过 `-TestSelection` 或专用 INI 显式加入，不改变窄 smoke 配置。
- `test_host/test_host_manual_picker.ini`：`auto=0`、`javascript=1`，选择 `263,1310,999`；
  `scripts\stage_manual_picker.bat Debug C:\WMShare\Positron-manual-test263-deferred-id` 已生成
  本轮手动包，TEST263/1310 已通过。TEST232 已单独验收，未重复加入；旧的
  `C:\WMShare\Positron-manual-next232-fix` 和 `C:\WMShare\Positron-manual-next1310` 不应再用于当前基线。
- tracked INI 是窄 smoke，不是全量目录；nightly 打包脚本从源码 dispatch 动态生成全量自动清单。
- 设备连接必须先由用户在 WMDC/Device Emulator GUI 手动完成；RAPI gate 只使用当前唯一会话。

## 最新有效设备证据

`tmp/device-runs/20260921-232526-dpi-clip-final` 的 `1311,999` 已正常 `PASS`：
`core_module_check=PASS`，路径等于本次 staging 目录，TEST1311 报告 14/27 行、
最低 14/21 行，999 PASS、dump=0；回收清理。
`tmp/device-runs/20260921-231158-dpi-clip-holders` 记录了旧 `positron.exe` 持有者；
设备门现在先检查路径并记录持有者。
`tmp/device-runs/20260920-213339-test263-pointer-repro` 是 TEST263 证据：
`263,999` 2/2 PASS；双空间预检、日志回收、清理和 `crash_check` 均 PASS，dump=0。
`C:\WMShare\Positron-manual-test263-deferred-id` 也由用户操作通过 TEST263
与 TEST1310；手动 picker 的视觉和 OEM 行为仍不应外推到其他 ROM。较早的 1302–1309 基线仍由
`tmp/device-runs/20260920-153441-next875-file-upload-baseline` 保存；失败实验日志保留在
`tmp/` 供审计，不作为产品基线。
## 当前人工验收状态

以下路径已有过真实设备确认，但后续触及相邻基础设施时仍需重新评估：

- example.com → IANA 的容器边距、深层导航和旧页保留；
- SIP 候选词整词提交；
- bitmap/SVG、表格、列表和常见布局的可见结果；
- native EDIT/SELECT、真实 file picker、旋转和 DPI 路径。
- 带 `tabindex` 的普通元素的设备焦点矩形、触摸命中和不同 DPI 视觉仍需人工观察；语义顺序已有自动断言。
- `<dialog>` backdrop 的整体色彩、边界、滚动/旋转下的视觉仍属于可累计的人工观察；Core 的绘制顺序和设备门像素契约已有自动断言。
- contenteditable 的 OEM 硬键盘/自动重复、SIP/IME 候选词、跨应用剪贴板互操作、滚动/旋转和不同 DPI 下的文本视觉仍属于可累计人工风险；1113 已在真实 WM EDIT 上验证无修饰鼠标拖选的连续范围/方向通知，1114 验证了 Shift/方向键、捕获丢失和焦点切换的有界通知收尾，1112 覆盖脚本 `selectionchange` 去重，1115 覆盖宿主自备的 `CF_UNICODETEXT` 连续 paste/cut（包括 retained layout 暂失时的按 id 事件派发），1116 覆盖宿主 `WM_COPY` 与格式/容量拒绝。完整 ClipboardEvent/async clipboard、CF_TEXT/富文本转换仍不在契约内。
- TEST1151 autofocus 夹具仅证明 DOM/焦点桥合同；初始焦点矩形、native HWND、触摸/SIP、滚动条裁剪和不同 DPI 仍需宿主观察。
- TEST1152–1169 是离线的 Browser selector、validation、焦点和 placeholder 夹具，
  自动门已证明各自的有界查询、mutation、顺序、callback 边界和非法输入 fail-closed；
  真实页面完整 Selectors、native 输入、SIP/IME、触摸、布局视觉和不同 DPI 仍由宿主观察。
- TEST1170–1188 是离线的 Core/Browser form-owner、validation、submission、reset、
  FormData、selector 默认状态、option 属性/collection、fieldset projection 与
  `form.elements` 夹具；自动门已证明跨树 owner、成功控件排除、默认动作顺序、snapshot
  隔离和有界错误回退。它们不保证完整 live collection、native 表单/SELECT 视觉、picker、
  键盘/触摸、SIP/IME 或不同 DPI 行为；逐测试合同见 [`docs/TESTING.md`](../docs/TESTING.md)。
- 低号 TEST118 是 native SELECT 键盘桥的例外：本批自动设备门覆盖关闭态 COMBOBOX 的
  ArrowDown `keydown`/`keyup`、target/bubble 顺序和 layout invalidation 后的 live
  selection 回退；它不覆盖展开 popup、触摸、SIP/IME、OEM 重复键或视觉保证。
- TEST1189–1199 的 form-owner、output/object/img metadata、image-map、srcset/picture
  选择和 source lifecycle 夹具均已有自动门证据；详细合同、边界和逐项结果统一见
  [`docs/TESTING.md`](../docs/TESTING.md)，这里不重复维护历史清单。
- TEST1201–1309 的 DOM/CharacterData、HTML parser、detached wrapper、属性 facade、
  body.text、session cookie、document.write、Core-backed document.title 与 bounded
  DocumentFragment lookup/clone/selector/relations/replace/composition/collection/normalize、
  detached Element HTML serialization、Fragment textContent 原子替换、Fragment-owned
  detached Element sibling 关系、Browser-owned Text/Comment element-sibling 关系、Fragment
  CharacterData 根 staging/textContent 投影及 Browser-created Text/Comment/Element 的
  CharacterData/relative primitive/现有 CharacterData source、Text/CDATA normalize、created
  Element element-child projection、四位置 `insertAdjacentText()`/`insertAdjacentHTML()`、
  `insertAdjacentElement()`、direct Element-child `appendChild()`/`insertBefore()`/`removeChild()`、
  primitive-only `replaceChildren()` wrapper reconciliation、嵌套 Browser-created Element
  staging、脱离后的 direct CharacterData 快照、image source generation/late-event
  rejection（TEST1300）、Core multipart wire encoder/host file callback contract（TEST1301）、
  independent FormData wire encoder（TEST1302）、Browser script FormData/URLSearchParams
  mutation budget（TEST1303–1304）、Storage quota（TEST1305）、object-property-safe
  Storage map（TEST1306）、Headers special-key snapshot（TEST1307）以及 prototype-safe
  DOM id/wrapper/event/dataset/BroadcastChannel registry（TEST1308），另有参考宿主
  WM_SHOWWINDOW 到 Browser visibility lifecycle 的消息接线（TEST1309）
  夹具均已有相邻设备门；TEST263 的 deferred-id picker 探针和 TEST1310 的 picker/FormData
  metadata 页面已构建，当前手动包中的两项均已完成需要人工选择文件的 GUI 验收；
  逐项合同、预算和选择集中在 [`docs/TESTING.md`](../docs/TESTING.md)，本文件不重复维护历史清单。
  通用节点、observer、除 TEST1297 外的完整 live collection、native/OEM 视觉和 SIP/IME 仍不在自动门范围。
- 允许累计的人工风险包括低风险视觉、触摸、SIP/IME、旋转、picker 和失败网络观察；
  崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即人工复核。

## 当前未决风险

- next700 的 `Element.getClientRects()` 已能把普通 inline flow 的实际行片段暴露为最多
  16 个 viewport-relative 矩形，并以同一集合计算 union。它不是完整的 CSSOM 几何算法：
  Range/Selection、transforms、nested overflow、pinch zoom、平滑滚动、复杂 inline
  嵌套、字体精确度量和视觉像素仍需宿主集成观察。TEST1145 只证明离线窄容器中的
  Core/Browser 一致性、顺序、identity 和 union。

- next701 的六个布局尺寸 getter 只消费最近一次 Core layout 的有界快照。支持范围是
  常见 block、replaced、table/flex box；完整 CSSOM box model、实时 reflow、transforms、
  pinch zoom、字体精确度量和真实滚动条视觉仍未实现。next702–704 只在带 id 的常见
  overflow box 上增加 retained 两轴滚动和有限 nested `scrollIntoView()`；默认选择最近
  ancestor，`container:"all"` 才沿最多 64 层向外处理，完整滚动容器树、scroll
  chaining/anchoring、scroll-margin、smooth/inertia 和匿名目标仍未实现。
  next705 又让 `HTMLElement.focus()` 复用同一条 Browser-owned 嵌套 reveal 路径，并由
  Ex `prevent_scroll` 让宿主延后 page-level reveal；next706 再增加宿主显式触发的
  `autofocus` 查询/设置和无 id 目标事件 dispatch，但 Browser 不自主执行初始焦点，
  完整滚动树和焦点导航仍未实现。TEST1146–1151 只证明离线 fixture 中 Core/Browser
  的整数值、clamp、事件、size-probe 和 fail-closed 回退一致。

- 已建立固定、小型、可重复的离线 corpus 流程，但它们仍不能代表任意真实网站；TEST13 仍只是单一网络哨兵。TEST1119–TEST1150 已覆盖导航事务、资源 gate、页面生命周期、滚动/几何、布局尺寸、元素 overflow、媒体/焦点和脚本调度的有界合同。取消仍是协作式的，脚本队列仍依赖宿主调度；任意真实站点的 fallback 视觉、复杂布局、Range/Selection、inline 嵌套、完整滚动容器树、scroll chaining、pinch zoom、精确逐元素归因和自定义 prompt 仍未保证。
- `<dialog>` 已有已验证的有界脚本生命周期、`method="dialog"` 默认动作、活动 modal id、Escape→`requestClose()` 桥接、宿主顺序 Tab/Shift+Tab 子树范围、有界 backdrop 指针策略和 Core 实体色 modal paint；当前表单桥要求最近祖先 dialog 有非空 id。CSS `::backdrop`、透明合成、多个 modal 和跨文档 modal 生命周期尚未实现，初始焦点、native 窗口视觉和非顺序平台焦点仍由宿主决定。
- `contenteditable` 具有单元素纯文本状态/mutation、Browser 的 bounded selectionStart/End/Direction、去重后的 `selectionchange` 和带 id、已布局 editing host 的有界 WM EDIT 代理；宿主在无修饰 `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` 以及键盘扩展后报告范围与 forward/backward 方向，捕获/取消/焦点中断会收尾而不重复派发，每页最多 16 个 host、文本最多 8192 UTF-8 字节，嵌套继承后代不重复代理。当前另有宿主级受限 `CF_UNICODETEXT` 粘贴/剪切/复制事务：`WM_COPY` 的非空选区才写入剪贴板，折叠选区是 no-op；不支持的格式和超长数据在 native mutation 前 fail closed。Range/Selection 对象、完整 ClipboardEvent/async clipboard、CF_TEXT/富文本转换、OEM 特有键盘自动重复与复杂行导航、designMode、完整 IME 组合尚未实现。
- float、复杂 table/position、现代 CSS 与任意畸形页面仍有明显边界。
- 浏览器 JavaScript 是有限组合，不具备完整 DOM/Web API 或现代浏览器安全沙箱。
- Browser selector 仍是有界子集：支持列表/关系/属性/结构伪类、表单状态、focus/link/visited/target/lang、`:not()`/`:is()`/`:where()`/`:has()`、可选 interaction 的 `:active`/`:hover`、Core validation 的 `:in-range`/`:out-of-range`、依据 readonly/effective-disabled 和可选 contenteditable callback 判定的 `:read-only`/`:read-write`、text-like input/textarea 的 `:placeholder-shown`、依据默认 checked/default-selected 与首个 submit control 的 `:default`，以及直接、无参数的 `:scope` context。TEST1152–1169、TEST1179–1183 已覆盖这些路径的查询、mutation、预算和非法输入回退。范围伪类只接受非空且受约束的 input number/range/date/month/week/time/datetime-local，underflow/overflow 才构成 out-of-range；空值、bad/type mismatch、disabled/readonly、无范围限制、非 input 和单独 stepMismatch 安全不匹配。显式 contenteditable 在 callback 缺失或查询失败时两种编辑伪类都不匹配；placeholder 伪类不匹配空 placeholder、其他 input 类型、普通元素或带参数形式。`:visited` 只由宿主 Ex callback 明确批准，Browser 不保存或推断 history；`:scope` 的 receiver/document owner 规则不扩展为嵌套参数或完整 Selectors；`:default` 不提供完整默认按钮算法，relation 45 缺失时保守不匹配。完整 CSS Selectors、visited 的持久化/隐私隔离/真实颜色、伪元素/namespace/shadow DOM、`:has()` 链式关系、`:target` reveal 以及复杂页面的 1.5 MiB Browser heap 预算边界仍未承诺；详细合同见 [`docs/TESTING.md`](../docs/TESTING.md)。
- 图片资源的候选选择覆盖 Core 的最多 16 个同类正密度 `x` 或正宽度 `w` 候选（每个
  URL 最多 2047 字节），以及每个 `<picture>` 最多 8 个 preceding `<source>`、16 层
  ancestor 和 64 个 direct-child 节点的有界扫描。source 先按 document order 过滤
  media/type，再用同一 `srcset`/`sizes` 选择器；结果由 fetch/cache/layout/currentSrc
  共用。`sizes` 只支持 px/vw/vh 和单一 min/max-width 条件，完整媒体查询、绝对 URL、
  CORS/referrer enforcement、完整 loading/fetch-priority 策略或 native 图像视觉仍未
  实现。Core 的 image-map 只有有界的 default/rect/circle/poly 命中和 area
  几何；不覆盖 transforms、完整 HTML image-map 算法或 pointer/touch 手势。`decode()`、
  `load`/`error` 只覆盖 Browser 的有界 Promise/事件桥，必须由宿主在 Core 的当前
  complete/natural-size relation 就绪后显式通知；它不提供后台加载、自动事件或完整图像
  生命周期。支持的 `Element.id` setter 改名会先回收旧终态 key，避免反复改名耗尽每个
  session 的 64 项终态预算；其他通用 id/加载语义仍不在契约内。
- `<option>` 的 `selected`/`defaultSelected`、`value`/`label`/`text` 与 select 的
  `options`/`selectedOptions`/`length`、option `index` 是可选的 Browser 扩展：前者由
  Core 维护 live 选择并执行单选互斥/多选规则，后三项复用通用 DOM attribute/text
  callback，显式属性优先、缺失时回退到 option 文本；集合是按可寻址 id 遍历得到的有界
  snapshot，selected mutation 会在下一次读取时反映，snapshot 自身的数组修改不回写 DOM。
  集合最多遍历 256 个节点并返回 64 个 option，缺失 id 的元素不可被当前 wrapper 寻址；
  不实现完整 live HTMLCollection、option form/disabled 全部算法、append/remove、native
  popup、键盘/触摸、SIP/IME 或视觉结果，缺失 callback、非目标和无效 id 均安全失败。
- 多窗口、持久 history、完整下载/外部协议策略仍属于宿主或未实现范围。
- mbed TLS 2.16.12 等依赖为旧平台兼容 pin，发布前必须审查当前安全风险。
- OEM SIP/IME、系统 picker、视觉和旋转不能仅凭 synthetic 自动测试保证。

Core/Browser form owner 目前覆盖 input、select、textarea、button、fieldset、img、object 和 output；这些元素
（包括显式 `form="id"` 的跨树元素）按文档顺序把 listed 项加入有界 `form.elements` snapshot；img
只提供 owner，不进入 form collections。fieldset、object 和 output 只属于 DOM relation enumeration，
仍不进入 successful-control visitor、submission 或 FormData；output 的 labels、descendant-text `value`、
独立 default override、只读 `type` 和 reset 恢复已覆盖。validation、
submission/multipart、dialog/default-submit、reset、按坐标的 submit/reset 激活和脚本
`HTMLFormElement.submit()` direct path 以及 `new FormData(form[, submitter])` snapshot 也
复用这条 owner 规则。direct path 和 FormData bridge 仅支持有 id form；前者跳过 validation、
submit event 和 submitter，后者的 Ex 路径只接受目标 form 的 enabled submit-type input/button，
最多返回 64 项且 Browser 对象仍只返回 filename/type metadata；脚本对象的 append、新键 set 和
数组构造也共享这 64 项预算，超限抛出 QuotaExceededError 并保留旧 pairs。应用可将 Core snapshot 交给
`PCore_FormDataEncode()` 生成 multipart body；Browser 构造成功后同步派发非冒泡、不可取消的
`formdata` 事件，监听器可修改返回对象。完整 live collection、File/Blob API、异步文件读取、
复杂 parser 重构和 native 表单视觉仍未实现。

完整列表见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 唯一下一步

本轮阶段 2 源码接线、C89、Debug/Release 和审计已通过；设备门仍因 `RAPI=0x80072746` 暂缓。
恢复传输后跑 TEST999，验收 classic script 顺序、DOM/事件/任务、生命周期、资源失败、取消/
stale、旧页保留、语言、history、旋转、DPI、软键、标准滚动条和清理；之后才进入阶段 3
native 表单、SIP/IME、picker，File/Blob 不提前接入。

阶段 0 的本批变更只收口 EXE 私有宿主状态和生命周期；阶段 B 的主文档行为、离线页面和
i18n 不变。TEST262/264 的自动失败仍在审查报告中隔离，不能混入本次判断；崩溃、数据损坏、
严重布局破坏或核心交互阻塞须立即人工复核。
新批次仍须把可复用语义放入公共 DLL，宿主只保留平台接线、调度、fixture 与断言，并附带
相邻回归和职责文档更新。超出 bounded Element/Text 子集的通用节点、混合/嵌套
DocumentFragment 插入、
超出有界元素约束的 reparent、其他删除、
Range/Selection、完整 live collection、MutationObserver、完整滚动容器树、pinch zoom、
transforms、scroll-margin、平滑/惯性滚动、完整媒体查询语法、bfcache、绝对 URL、CORS、
完整图像 loading 和 image-map 扩展仍是候选限制，不能在证据之前写成已支持行为。

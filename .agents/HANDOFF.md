# 当前交接

本文件只描述当前产品基线、最近有效证据、未决风险和唯一下一步。逐批实现过程由 Git 历史保存，历史事故见 `docs/history/`，未来方向见 `ROADMAP.md`。

## 项目使命

Positron 为 Windows Mobile 6 / Windows CE 5.2 ARMV4I 提供模块化 TLS、JSON、HTTP、图像、脚本、渲染与浏览器会话 DLL。公共边界保持 C ABI、UTF-8、opaque handle 和显式所有权；`test_host.exe` 只是回归宿主与示例消费者。

## 当前 Git 与工作区

工作区仍在 `main`。当前设备门基础设施使用唯一 `.part-*` 文件、同卷原子改名、32 KiB RAPI 写块和有界的超时后会话重开；日志复制期间的瞬时 `CeReadFile` 失败仍只视为可重试快照。产品侧的 Duktape Dragon4 数值转换上下文移出原生线程栈，参考宿主也在同步嵌套 `WM_SIZE` 期间暂缓 Browser 脚本通知和 native child 重建，并在最外层完成布局后按顺序发布 scroll/resize。

`20260908-151743-next765-native-resize-formal10` 已在当前 GUI 连接的 WMDC 目标上以正式 Debug ARMV4I、全新外部目录、无 `-PreserveDeployment` 运行 `TEST1113,1114,1115,1116,1199,999`：6/6 OK、唯一 `TESTBENCH PASS`、零 `ERROR`/`FAIL`，完整日志已回收且当前远端目录已删除；该目标卷约 32 MiB，预检仍通过。前一轮同目标的 `formal9` 曾在设备门等待窗口内被误判为超时，但随后重开 RAPI 会话取得的两份完整日志一致为 PASS；这一证据已促成当前的超时后最终日志回收路径。更早的 `formal3`–`formal8` 仅在传输阶段失败，宿主未启动，不能当作产品测试结果。随后 `20260908-153431-next766-whole-text` 在同一 GUI 连接上以 Debug ARMV4I 运行 `TEST1205,1206,1207,1208,999`：5/5 OK、唯一 `TESTBENCH PASS`、零 `ERROR`/`FAIL`，完整日志已回收且当前远端目录已删除；该证据确认 wholeText 纵切及其相邻 CharacterData/ splitText 回归。

- 当前代码 next766 延续 next761–765 的 Core child-data 与 Browser
  CharacterData bridge；新增 `Text.splitText()` 的 direct-child 有界路径，将 UTF-16
  边界映射到 UTF-8 code-point、插入紧邻 Text sibling，并保持原 wrapper 与旧 snapshot
  合同。`PCore_NodeSetCharacterDataChildById`、`PBrowserScriptDomWriteCallbacksEx2`
  和旧 Text-only ABI 均保持不变；Ex3 仅追加 `split_text_child`。成功 mutation 仍使
  retained layout 失效，Browser/宿主负责事件与输入策略、重新 style/layout/paint。
  next766 另外新增关系 50 的 `Text.wholeText`：Core 通过 libdom 拼接逻辑相邻 Text
  sibling，Browser 提供只读 getter，非 Text/边界/无效 child 返回 unavailable，detached
  wrapper 回退到最近一次数据快照；读取不改 DOM、不触发 layout 或资源 I/O。
  `test_host` 只增加 TEST1207/1208 fixture、边界断言与 callback 接线，没有承载产品 DOM 或
  布局语义。next760 的元素 textContent/innerText 与 next759 的 direct-element removal
  保持不变。设备门为每次远端启动使用
  唯一 executable basename，仍只复用 WMDC GUI 当前唯一 RAPI 会话，超时进程需在设备端
  正常结束。
  `tmp/` 中的本地证据未纳入版本控制。
- `TEST_MAX_NUMBER` 已为 1208。tracked `test_host/test_host.ini` 仍是窄 smoke：
  `auto=1`、`javascript=0`、选择 `13,20,27,56,58,62,64-67,73,75,999`；nightly/device
  tooling 从源码 dispatch 动态生成全量清单。
- 2026-09-08 nightly 已使用 `laptop-li\joe` 的 Windows keyring 成功覆盖固定
  `nightly` pre-release（源提交 `5236777c`、Debug、19 个不压缩条目）。受限 Codex 进程
  可能以 `laptop-li\codexsandboxoffline` 身份运行，即使用户目录仍显示为 Joe，也看不到
  该 keyring；这种进程中的 `gh auth status` 为 invalid 不是用户登录状态的可靠证据。发布
  前应在 keyring 可见的用户上下文核对身份和 `gh auth status`；失败经过见
  [`FAILED_EXPERIMENTS.md`](FAILED_EXPERIMENTS.md)。
- 设备门继续假定用户已在 WMDC/Device Emulator GUI 手动连接恰好一个目标；RAPI 只复用
  当前会话，不连接、选择、cradle、重置或强杀设备。

## 近期已完成能力摘要

- next678–682 将导航候选、资源终态、失败分类、清理快照和 history viewport snapshot
  迁入 Browser opaque handle；宿主仍拥有线程、WM、网络 I/O、页面替换和窗口策略。
- next698–706 完成有界滚动、inline/box 几何、focus/autofocus 和 Core→Browser 几何桥；
  next707–724 完成 selector 组合器、属性/结构/表单状态伪类及 disabled/validation/
  contenteditable/placeholder relation。细节与边界分别见 `docs/TESTING.md` 和
  `KNOWN_LIMITATIONS.md`，不是本文件的逐 next 清单。
- Browser script session 已覆盖 page scroll、resize/`matchMedia`、`visualViewport`、
  window/page lifecycle、focus/activeElement、任务检查点和 beforeunload；宿主只提供
  WM/时钟/调度/策略，Browser 不创建线程或自行推进队列。
- Browser/Core 的表单 owner、validation、submission、dialog、reset、`requestSubmit`、direct
  `submit()`、detached `FormData`、图片元数据/decode/image-map、selector 子集和
  source-selection 生命周期已形成有界合同；TEST1170–1208 的逐项夹具、边界和错误回退
  统一见 [`docs/TESTING.md`](../docs/TESTING.md)。最近的 TEST1199–1208 还覆盖 source
  mutation callback、direct-element removal、元素文本内容、Text/Comment/CDATA
  CharacterData mutation、`substringData()` 范围和 `Text.splitText()` 结构合同，
  均保持产品语义在 Core/Browser 而非宿主。
- 设备门的部署前双空间预检、空间不足应急回收、旧目录日志完整性检查和完成后清理已集中在
  `scripts\device_gate.ps1`；这只是测试基础设施护栏，不改变任何公共 DLL ABI 或产品语义。
- `tmp/` 仅保存本地设备日志与截图；更早的基线和逐批实现由 Git 历史保存。

## 当前中期里程碑

在保持 VS2008/WM6 约束的前提下，把已经形成的 Core、Browser 和平台宿主能力整合为可由真实页面驱动的有界网页运行时。重点是完成用户可感知的纵向能力、把通用语义留在公共 DLL，并以小型真实页面/交互语料库防止只增加孤立 API。

## 当前短期目标

- 当前基线已包含表单 owner/validation/submission/reset/FormData、selector、滚动/几何、
  生命周期、焦点、图片元数据/decode/image-map，以及 next754–757 的有界 `srcset`、
  `<picture><source>` 选择和 source identity 生命周期；next758 又补齐脚本图片来源
  mutation 的可选 typed host callback；next759 再补齐 direct-element DOM removal 与
  retained-layout invalidation；next760 再补齐 `textContent`/非编辑 `innerText` 的
  snapshot/lifecycle 失效与 Core retained-layout invalidation；next761 再补齐
  `Text.nodeValue`/`data`/`textContent` 的有界自身 mutation 与 detached wrapper 失效；
  next762 再补齐 Text 的四个 CharacterData mutator，并保持同一 callback/布局失效合同；
  next763 再补齐 Text/Comment/CDATA 的通用 child-data primitive 与 Ex2 callback，保持旧
  Text-only ABI 和同一 callback/布局失效合同；next764 再补齐 `substringData()` 的严格
  UTF-16 offset/count 校验、超长 count 截断和 detached Text/Comment/CDATA 快照读取；
  next765 再补齐 direct Text child 的 `splitText()`、Ex3 callback、UTF-16/UTF-8 边界
  映射与 sibling/snapshot reconciliation；
  next766 再补齐关系 50 的 `Text.wholeText` 读取，覆盖逻辑相邻 Text 拼接、非 Text
  边界、UTF-8 probe/truncation、只读 getter 和 detached 快照；
  稳定合同和逐测试说明以
  [`docs/TESTING.md`](../docs/TESTING.md) 与 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)
  为准。
- `test_host` 只保留 callback 接线、平台调度、fixture 和断言；可复用的 URL、DOM、Event、
  表单、图像和生命周期语义必须继续位于对应公共 DLL。
- 当前 fixed-buffer 数值转换、原子部署和 RAPI 日志恢复已由 `formal10` 正式门验证；`next766`
  的 wholeText 纵切也已由 `20260908-153431-next766-whole-text` 正式门验证。下一步应从
  compatibility corpus、源码、设备日志或截图固定一个新的真实产品缺口，选择 next767 的下一条
  公共 DLL 纵向能力。不得把测试宿主扩展当作产品语义实现。

## 已验证产品事实

### 公共边界

- 顶层公共 DLL 为 TLS、JSON、HTTP、image、script、core 和 browser。
- NetSurf/libcss/libdom/hubbub、Expat、libsvgtiny、libjpeg 等移植工程是内部实现依赖。
- 独立脚本和浏览器脚本共用 Duktape；浏览器 JavaScript tracked 默认仍为关闭。
- 通用 URL、history、DOM、Event、表单、图像和脚本 session 语义位于对应公共 DLL；宿主保留 WM 窗口、消息、控件、SIP/IME、picker、导航调度和资源 I/O。

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
  调用 Core；成功后 Core 丢弃 retained layout，Browser 刷新目标的
  `children`/`childNodes`/query snapshot，并让旧的无 id 文本 wrapper 保留数据但变为
  detached。contenteditable 的 `innerText` 复用同一失效规则；Text 的
  `Text`/`Comment`/`CDATA` 的 `nodeValue`/`data`/`textContent` 与四个 CharacterData
  mutator 保持 child list 和连接中 wrapper 身份，非法/失效目标 fail closed。`Text.splitText()`
  通过 Ex3/`PCore_NodeSplitTextChildById` 对 direct Text child 插入紧邻 sibling，保留
  原 wrapper、旧 NodeList snapshot，并对 UTF-16→UTF-8 不可表示边界 fail closed；宿主
  负责输入/事件策略和重新 style/layout/paint，通用节点插入、reparent、文本节点删除、
  Text 合并、MutationObserver 与完整 live collection仍未实现。关系 50 的
  `Text.wholeText` 读取已在 next766 加入：连续 Text sibling 由 Core/libdom 拼接，
  非 Text 边界和无效 child fail closed，detached wrapper 保留最近一次数据快照。
- Browser 层提供有界 history、same-document state、script session、DOM/Event/input/navigation callbacks，以及 timer/microtask/lifecycle、native 控件事务、导航资源事务、候选生命周期/结果协调和 FormData snapshot bridge（含 Ex submitter 与 formdata 事件路径）。`select.options`、`selectedOptions`、`length`、`option.index`、`option.form`、fieldset 的 type/form/elements、img/object 的 form 以及 output 的 form/labels、`form.elements` 中的 fieldset/object/output enumeration 也在 Browser 中以有界、可寻址元素 bridge 提供。
- Browser script session 的 `PBrowser_ScriptSessionRunTaskCheckpoint` 统一驱动 timer、animation frame、message、idle 和 microtask：调用方选择阶段后，Browser 按固定顺序在每个阶段后运行一次有界 microtask；宿主提供时钟、各阶段限额和 UI 消息循环。参考宿主已在真实窗口消息循环安装 16 ms `WM_TIMER`，未调用 pump 的 session 不会自行推进异步队列。
- 页面替换前，Browser session 可由宿主显式调用 `PBrowser_ScriptSessionDispatchBeforeUnload`，同步派发 cancelable 的 `beforeunload` 并返回取消决定；参考宿主在取消或脚本调用失败时保留旧页，允许后才调用 page teardown。Browser 不显示 prompt，也不拥有宿主的关闭/导航策略。
- Browser 层还提供由宿主显式驱动的 viewport resize 合同：`PBrowser_ScriptSessionNotifyResize` 更新 CSS viewport/DPR 和动态 `screen` 方向，值变化时同步派发一次 window `resize`；同一 session 的 `screen.orientation` 对象保持身份稳定，方向翻转时在媒体列表刷新后先派发一次可信 `change`，再进入 visual/window `resize`；调用不负责 Core relayout 或 frame scheduling。
- 同一 Browser session 还提供布局视口对应的 `visualViewport`：`width`/`height` 与 CSS viewport 同步，`pageLeft`/`pageTop` 与 page scroll 同步，`scale` 为 1、offset 为 0；有效 resize/scroll 先派发 visual viewport 事件，再派发 window 事件，并对重复快照去重。TEST1133 覆盖该合同。
- Browser history entry 同时拥有非负的 `(scroll_x, scroll_y)` viewport snapshot；新 document entry 和同 URL 新 document 从零开始，`replaceState`/traversal 保留目标值，`pushState` 新 entry 从零开始，history 裁剪会同步搬移 snapshot。Browser 不访问窗口、不知道 Core 的页面 extent；宿主读取 `PCore_DocumentWidth/Height` 后保存/读取并对两个轴 clamp/apply。
- Browser script session 的 `PBrowser_ScriptSessionGetScrollRestoration` 暴露脚本的 `auto`/`manual` 策略。宿主在非 fragment history traversal 前只对 `AUTO` 自动读取并应用 entry snapshot；`MANUAL` 保留当前 viewport，查询失败按默认 `AUTO` 处理。fragment reveal 与显式脚本滚动不受该自动恢复门影响。
- Browser script session 的 `window.scrollTo`/`scrollBy` 经过 `PBrowserScriptScrollCallbacks` 交给活动宿主；宿主返回实际 page 坐标后，Browser 只派发一次 `scroll`。宿主的物理滚动路径用 `PBrowser_ScriptSessionNotifyScroll` 反向同步，重复坐标不派发事件，回调内不会重入 runtime。
- Browser script session 的 scroll callback 和 `PBrowser_ScriptSessionNotifyScroll` 均使用 CSS page 坐标；宿主在调用 Core 的物理滚动、绘制、命中测试和滚动条路径时负责当前 DPI 的双向换算。重复坐标不派发事件，回调内不会重入 runtime。
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
- 深层 DOM 资源准备使用单个事务级 heap scratch，避免大批固定栈缓冲耗尽 WM6 线程栈。
- 导航 request 在 worker join 后由宿主先收敛失败/过时资源，再调用 Browser 的 `PBrowser_NavigationCleanupGetInfo` 复制 cleanup decision、candidate/resource 终态、pending、`can_release`、hash-only failure summary 和 fallback 计数；复制值在 candidate/resource handle 销毁后仍可用于日志。TEST1127 同时覆盖 pending/terminal decision、required failure、optional fallback、取消、stale、清理前复制、释放后快照存活，以及成功/失败 `pcore_navigation_finish` 的真实回收路径。
- `<details>/<summary>` 支持 click 与 Enter/Space 激活、取消和 DOM 状态同步。
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

- `TEST_MAX_NUMBER`：1208。
- tracked `test_host/test_host.ini`：`auto=1`、`javascript=0`，选择 `13,20,27,56,58,62,64-67,73,75,999`。
- tracked INI 是窄 smoke，不是全量目录；nightly 打包脚本从源码 dispatch 动态生成全量自动清单。
- 设备连接必须先由用户在 WMDC/Device Emulator GUI 手动完成；RAPI gate 只使用当前唯一会话。

## 最新有效设备证据

最新正式证据是 `20260908-153431-next766-whole-text`：正式 Debug ARMV4I、
当前 GUI 连接的唯一 WMDC 目标、全新外部目录，定向运行 `TEST1205,1206,1207,1208,999`。
5/5 测试通过，只有一个 `TESTBENCH PASS`，零 `ERROR`/`FAIL`，完整日志已回收，
`current_cleanup=removed_after_complete_log`。它确认 wholeText、CharacterData 和
splitText 的相邻纵切，以及当前 32 KiB RAPI 写块、空间预检和完成后清理路径。

前一项 `20260908-151743-next765-native-resize-formal10` 以同样的正式配置通过
`TEST1113,1114,1115,1116,1199,999`（6/6、零 `ERROR`/`FAIL`），确认嵌套 `WM_SIZE`
重入保护、native child 重建顺序和超时后日志恢复。更早的 formal3–8 只在传输阶段失败，
不能当作产品测试结果；formal9 的等待超时随后重开 RAPI 会话并取得完整 PASS 日志。
逐批实现细节和本地日志仍保留在 Git 历史与 `tmp/device-runs/`，不在此重复维护。

本批 `test_c89ize`、`audit_repo`、`git diff --check` 和 Release ARMV4I 构建均通过；仅有既存
libcss 数值转换警告。

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
- TEST1189–1199 的 form-owner、output/object/img metadata、image-map、srcset/picture
  选择和 source lifecycle 夹具均已有自动门证据；详细合同、边界和逐项结果统一见
  [`docs/TESTING.md`](../docs/TESTING.md)，这里不重复维护历史清单。
- TEST1201 是离线的 Browser/Core direct-element DOM removal 夹具，暂无新增立即人工风险；
  自动门证明 `Element.removeChild()`/`remove()` 的直接子节点和错误关系边界、旧/新
  collection snapshot 隔离、Core retained layout invalidation 以及 detached no-op。插入、
  reparent、文本节点删除、MutationObserver、完整 live collection 和 native/视觉行为仍
  需要未来能力或人工/真实页面观察。
- TEST1202 是离线的 Browser/Core 文本内容 mutation 夹具，暂无新增立即人工风险；自动门
  证明 `textContent`/非编辑 `innerText` 的新旧文本 wrapper、detached 数据、父级
  `children`/query snapshot 和 Core retained layout 失效/恢复。插入、reparent、文本节点
  删除、MutationObserver、完整 live collection 以及 native/OEM 文本视觉仍进入未来或
  累计人工范围。
- TEST1203 是离线的 Browser/Core Text 自身 mutation 夹具，暂无新增立即人工风险；自动门
  证明 `nodeValue`/`data`/`textContent` setter 通过 Text-only DOM write callback 修改
  Core Text、保持连接中 wrapper/NodeList 身份、更新父级文本和 length、使 retained layout
  失效，以及元素 child、缺失/越界索引和 detached wrapper 的 fail-closed 行为。Comment/
  CDATA 已由 TEST1205 覆盖；插入、reparent、文本节点删除、MutationObserver、完整 live
  collection 和 native/OEM 文本视觉仍未实现或需人工观察。
- TEST1204 是离线的 Browser/Core CharacterData mutation 夹具，暂无新增立即人工风险；自动门
  证明 Text/Comment 的四个 mutator 复用同一 Core bridge，按 UTF-16 范围完成连续修改和
  count 截断，保持 wrapper/NodeList identity，并对非法范围、detached wrapper fail
  closed。节点结构的其他 mutation、MutationObserver、完整 live collection 和 native/OEM
  文本视觉仍未实现或需人工观察。
- TEST1205 是离线的 Browser/Core Comment CharacterData 夹具，暂无新增立即人工风险；自动门
  证明 Ex2 callback、通用 child-data primitive、Comment setter/四个 mutator、Text/element/
  缺失/越界返回码和 retained-layout invalidation；CDATA 共用同一公共接口合同但没有独立
  HTML fixture。节点结构、MutationObserver、完整 live collection 和 native/OEM 文本视觉
  仍未实现或需人工观察。
- TEST1206 是离线的 Browser/Core `substringData()` 夹具，暂无新增立即人工风险；自动门
  证明 Text/Comment 的 UTF-16 offset/count 校验、超长 count 截断、只读性和父级替换后的
  detached 快照读取。节点结构的其他 mutation、observer、完整 live collection 和
  native/OEM 文本视觉仍未实现或需人工观察。
- TEST1207 是离线的 Browser/Core `Text.splitText()` 夹具，暂无新增立即人工风险；自动门
  证明 Ex3 callback、UTF-16→UTF-8 边界映射、原 wrapper/旧 NodeList snapshot、紧邻
  sibling、末尾空 Text、Core layout invalidation 以及 invalid/Comment/detached fail
  closed。通用节点插入、reparent、Text 合并、MutationObserver、完整 live collection
  和 native/OEM 文本视觉仍未实现或需人工观察。
- TEST1208 是离线的 Browser/Core `Text.wholeText` 夹具，暂无新增立即人工风险；自动门
  证明关系 50 的完整/截断/探测 UTF-8 读取、逻辑相邻 Text 拼接、element/Comment 边界、
  Browser 只读 getter、splitText/CharacterData 后的实时值和 detached 快照。读取不改
  child list、不触发 layout 或资源 I/O；通用节点插入、reparent、Text 合并、
  MutationObserver、完整 live collection 和 native/OEM 文本视觉仍未实现或需人工观察。
允许累计的人工风险包括低风险视觉、触摸、SIP/IME、旋转、picker 和失败网络观察。崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即人工复核。

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
- Browser selector 仍是有界子集：支持列表/关系/属性/结构伪类、表单状态、focus/link/visited/target/lang、`:not()`/`:is()`/`:where()`/`:has()`、可选 interaction 的 `:active`/`:hover`、Core validation 的 `:in-range`/`:out-of-range`、依据 readonly/effective-disabled 和可选 contenteditable callback 判定的 `:read-only`/`:read-write`、text-like input/textarea 的 `:placeholder-shown`、依据默认 checked/default-selected 与首个 submit control 的 `:default`，以及直接、无参数的 `:scope` context。TEST1152–1169、TEST1179–1183 已覆盖这些路径的查询、mutation、预算和非法输入回退。范围伪类只接受非空且受约束的 input number/range/date/month/week/time/datetime-local，underflow/overflow 才构成 out-of-range；空值、bad/type mismatch、disabled/readonly、无范围限制、非 input 和单独 stepMismatch 安全不匹配。显式 contenteditable 在 callback 缺失或查询失败时两种编辑伪类都不匹配；placeholder 伪类不匹配空 placeholder、其他 input 类型、普通元素或带参数形式。`:visited` 只由宿主 Ex callback 明确批准，Browser 不保存或推断 history；`:scope` 的 receiver/document owner 规则不扩展为嵌套参数或完整 Selectors；`:default` 不提供完整默认按钮算法，relation 45 缺失时保守不匹配。完整 CSS Selectors、visited 的持久化/隐私隔离/真实颜色、伪元素/namespace/shadow DOM、`:has()` 链式关系、`:target` reveal 以及复杂页面的 896 KiB heap 预算边界仍未承诺；详细合同见 [`docs/TESTING.md`](../docs/TESTING.md)。
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
  生命周期。
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
最多返回 64 项且文件只返回 filename/type metadata；Browser 构造成功后同步派发非冒泡、
不可取消的 `formdata` 事件，监听器可修改返回对象。完整 live collection、文件读取、
复杂 parser 重构和 native 表单视觉仍未实现。

完整列表见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 后续计划：next767

next766 的 `Text.wholeText` 关系 50 已由 TEST1208 及 TEST1205–1207、TEST999 的相邻
回归验证：Core 对直接 Text child 调用 libdom 逻辑相邻遍历并按 UTF-8 probe/truncation
合同返回，Browser 提供只读 getter，splitText/CharacterData 后保持实时值，detached
wrapper 回退到数据快照；非 Text、边界、缺失和越界 child 均 fail closed。`formal10` 仍
确认嵌套 `WM_SIZE` 重入保护、32 KiB 原子部署、fixed-buffer 数值上下文和超时后日志恢复。
下一步仍必须先从 compatibility corpus、源码、设备日志或截图固定一个新的真实产品缺口，
再决定进入哪个公共 DLL；不要预先把尚未验证的 Web API 或视觉行为写成承诺。完整滚动容器
树、Range/Selection、pinch zoom、transforms、scroll-margin、平滑/惯性滚动、完整媒体查询
语法、bfcache、绝对 URL、CORS、完整图像 loading 和 image-map 的未覆盖扩展仍是限制。

优先场景应同时满足：

1. 可在仓库内离线固定主要 HTML/CSS/交互，避免网络内容漂移；
2. 对应一个真实页面或用户操作，而不是孤立 getter/setter；
3. 由源码、日志或截图先证明边界，且暴露当前限制中的一个核心流程缺口；
4. 通用语义进入公共 DLL，宿主只保留平台接线；
5. 可以自动断言主要结果，人工部分只保留无法机器判断的视觉/输入风险。

## next767 完成标准

- 先用 compatibility corpus、源码、日志或截图固定一个真实页面/交互组合缺口，并把最小可重复 fixture 或哨兵写入测试入口；
- 可复用的 URL/history/DOM/Event/资源/布局/生命周期语义位于对应公共 DLL，`test_host` 只负责 WM 接线、调度和 fixture，不新增业务所有权；
- 自动断言覆盖该纵向能力的成功、失败/取消、资源清理和直接相邻旧路径，且不会削弱现有布局、几何、滚动、history、生命周期、selector、focus、form-owner、reset、requestSubmit、direct-submit 或 FormData 旧/Ex 路径；
- C89 回归、VS2008 ARMV4I 正式构建、同批 staging、仓库审计和风险相称的设备门均通过，无旧 EXE/DLL 混包；
- 定向门及直接相邻回归唯一 `TESTBENCH PASS`、零 `ERROR`/`FAIL`，视觉、触摸、SIP/IME、picker 或旋转风险进入人工累计清单；
- 完成后 handoff 应覆盖为 next767 快照，ROADMAP 只保留当前尚未完成的纵向能力；新测试、公共边界和设备证据应可由本文件与 `docs/TESTING.md` 复核。

# Positron 架构与公共边界

本文定义 Positron 的稳定工程边界：哪些能力属于公共 DLL，哪些属于宿主，数据和资源由谁拥有，以及 WM6/VS2008 对实现施加的约束。精确函数签名以各项目公开头文件为准；当前开发状态见 [`.agents/HANDOFF.md`](../.agents/HANDOFF.md)。

## 设计目标

Positron 为 Windows Mobile 6 / Windows CE 5.2 提供可以单独消费、也可以组合使用的基础 DLL。公共接口刻意保持窄而明确：

- C ABI，不向调用方暴露 C++、NetSurf、Duktape、cJSON 或 mbed TLS 内部类型；
- 所有跨边界文本均为 UTF-8；
- 状态通过 opaque handle 表示；
- 每个分配结果都有明确的释放者；
- 平台窗口、消息循环和网络调度留给宿主；
- 资源预算和功能上限可预测，失败时 fail closed。

项目不以完整现代浏览器、完整 Web 标准或桌面级并发运行时为目标。

## 总体分层

```text
WM6 应用 / test_host.exe
        │
        ├── 平台窗口、消息循环、WM 控件、SIP/IME、文件选择器
        ├── 应用导航策略、后台网络调度、资源 I/O、WM 接线与页面提交策略
        │
        ├── positron_browser.dll ── history/viewport snapshots、script session、DOM/Event、资源事务
        ├── positron_core.dll    ── HTML/CSS/DOM、style、layout、paint、表单
        ├── positron_script.dll  ── 有预算的通用 JavaScript runtime
        ├── positron_image.dll   ── bitmap/SVG decode、draw、encode
        ├── positron_http.dll    ── HTTP/1.1 与 URL reference 解析
        ├── positron_json.dll    ── JSON parse/query/serialize
        └── positron_tls.dll     ── TLS client、peer、listener
```

`test_host.exe` 只是上述组合的一种实现。可复用的业务语义必须位于适当的公共 DLL；宿主只保留 Windows Mobile 平台适配、应用策略和测试夹具。

## 公共 DLL

### `positron_tls.dll`

TLS 层拥有 mbed TLS context、socket 会话、证书链、peer identity 和 listener。它提供：

- verified 与显式 insecure 客户端连接；
- 运行时追加根证书；
- 读、写、关闭和线程安全错误复制；
- identity 创建/载入、DER SHA-256 指纹；
- 可选或强制客户端证书的 listener；
- pin 校验的 peer 连接。

调用方拥有 host/path 等输入字符串；连接、identity 和 listener handle 必须分别由对应 close API 释放。`PTls_LastError` 返回借用存储，跨线程或跨后续调用保留错误时应使用复制接口。

TLS 版本与信任数据受 WM6 工具链和 vendored 版本限制。verified 是默认产品方向，insecure 入口只用于明确的诊断和受控环境。

### `positron_json.dll`

JSON 层把 cJSON 隐藏在 opaque handle 后。顶层 parse handle 由 `PJson_Free` 释放；对象成员和数组元素是借用子节点，随父树失效，不能单独释放。序列化字符串使用 `PJson_FreeString`，不能假设它与调用方 CRT heap 相同。

### `positron_http.dll`

HTTP 层建立在 TLS 层之上，负责 HTTP/1.1 GET/POST、响应解析、有限 redirect、body 上限、进度回调和 reference URL 解析。它不拥有浏览窗口、历史记录或页面提交。

`PHttp_Get*`/`PHttp_Post*` 返回的 response 必须用 `PHttp_FreeResponse` 释放。网络失败通过 `status_code == 0` 与错误文本表达，调用方不得把非空 response 指针误判为请求成功。

### `positron_image.dll`

图像层是位图/SVG 的公共边界。它复制调用方输入字节或像素，内部持有解码对象，并提供：

- BMP/PNG/JPEG/GIF 等位图解码与信息查询；
- 从 BGR24/BGRA32 像素创建 retained bitmap；
- 绘制到调用方 HDC；
- PNG/JPEG/BMP/GIF 编码和配对 buffer 释放；
- SVG 解析、尺寸查询和绘制。

bitmap/SVG handle 由创建方通过对应 free API 释放。编码 buffer 必须用 `PImage_FreeBuffer`。HDC 仍属于调用方，图像层不创建或管理宿主窗口。

### `positron_script.dll`

脚本层封装一个受预算约束的 Duktape context。它提供 UTF-8 source 求值、JSON 结果桥、受限 native function、CommonJS 风格模块 provider 和内存/执行统计。

每个 script handle 独立拥有 heap、模块缓存、native function 注册和错误/result 缓冲。回调同步运行在调用线程，不得重入、销毁当前 context 或保存借用参数指针。宿主应使用有界预算和内存上限，不把该运行时当作完整浏览器沙箱。

`PSCRIPT_MAX_NATIVE_FUNCTIONS` 当前为 27。Browser 组合在同时启用 DOM、validation、contenteditable、导航、`document.activeElement`、`HTMLElement.focus()`/`blur()` 和可选 pointer-interaction selector 桥时会占满这组槽位；额外的宿主全局 native function 必须先检查注册计数，达到上限时保守失败。

### `positron_core.dll`

Core 是渲染和文档模型的产品边界，内部静态链接移植后的 NetSurf 支持库。主要职责包括：

- UTF-8 HTML 解析为 libdom 文档；
- CSS 解析、cascade、媒体条件和整树 computed style；
- 外链 CSS、`@import`、图片和 script 资源发现与有界缓存；
- NetSurf box construction、layout、hit testing 和 GDI paint；
- 最近一次 layout 的 page-level width/height extent，供宿主决定滚动条、clamp viewport，并把同一坐标应用到 paint/命中测试；同时为 ID-addressable 元素提供有界的 border-box union 和 inline 行片段快照，供 Browser 组合 `getBoundingClientRect()` 与 `getClientRects()`；
- 为已布局的常见 block、replaced、table/flex box 提供最近一次 layout 的 offset、client 和 scroll 尺寸快照；这些是整数 CSS 像素的只读 relation，不触发 relayout；同时为支持的、有 DOM id 的 overflow box 保留滚动条位置，提供关系 38/39、按 id setter 和 pointer snapshot；
- 在宿主提供活动 modal id 时，把普通文档、实体色 backdrop 和指定 `<dialog open>` 按固定顺序组合绘制；
- 表单值、约束验证、提交、reset 和 successful controls；Core 统一持有
  effective-disabled 状态（含 disabled fieldset 的 first-legend exemption 与
  optgroup→option 继承），并通过 relation 44 向 Browser 提供只读 UTF-8 `"0"`/`"1"`
  快照；选项选择和 successful submission 也消费同一状态；
- 表单关联关系：支持的 input、select、textarea、button 默认归最近祖先 form；存在
  `form="id"` 时解析文档中对应的 form，把 form 外控件纳入按文档顺序的有界
  `form.elements` snapshot，空值或无效目标不回退到祖先。validation、successful-control
  与 multipart/dialog submission、reset 以及 submit/reset 默认动作也复用同一 owner，
  不会只在 Browser 关系层识别跨树控件。`PCore_FormResetById` 是 Core 提供的无坐标
  state-only reset 入口；Browser/宿主必须先处理可取消 reset 事件，并在成功后重新
  layout/paint，Core 不创建事件或 native 控件；
  `PCore_FormSubmissionNoValidationById`、`PCore_FormDialogSubmissionNoValidationById`
  和 `PCore_MultipartSubmissionNoValidationById` 是 `HTMLFormElement.submit()` 使用的
  无 validation/submitter state-only 结果入口；它们不派发事件、不导航、不关闭 dialog，
  调用方负责容量与 multipart handle 释放；
- 单元素 `contenteditable` 的祖先继承、有效模式、有界 UTF-8 纯文本 mutation，以及供宿主创建编辑表面的已布局 editing-host 快照；剪贴板数据不进入 Core 文档状态；
- 交互状态、DOM 事件、焦点候选和支持控件的默认动作；
- 当前交互节点的有界 id 查询；`PCore_InteractionFocusElementId` 与
  `PCore_InteractionStateElementId` 只复制非空 UTF-8 id 和完整字节数，不改变
  focus/active/hover 状态、style 或 layout，无法解析、没有 id、状态组合非法或
  缓冲不足时 fail closed；
- 按 DOM id 解析已布局且符合有界焦点资格的目标，并在宿主焦点事务中更新 Core
  focus node；`PCore_FocusTargetInfoById` 只返回 geometry/kind，
  `PCore_InteractionFocusById` 只更新 Core 状态，page-level focus reveal、native
  HWND、focus 事件、焦点矩形和重绘调度仍由宿主负责；
- 按 DOM 顺序解析带 `autofocus` 属性且符合相同布局/可见性/焦点资格预算的第一个目标；
  `PCore_AutofocusTargetInfo` 只复制 geometry/kind 与可选 id，
  `PCore_InteractionFocusAutofocus` 只更新 Core focus node，
  `PCore_EventDispatchFocus` 为无 id 的当前焦点节点提供目标保持的同步 dispatch。
  Core 不在页面解析或后台线程中自动执行这条路径，宿主仍负责何时调用、page-level/nested
  reveal、native HWND、focus/focusin 和重绘；Browser 的 activeElement projection 对无
  id 节点继续回退到 `document.body`；
- 给脚本/浏览器层使用的有界 DOM、属性、关系、表单与导航查询。

Core 不执行网络请求。资源获取通过调用方提供的 resolve/fetch/free 回调完成；Core 在回调返回前复制需要保留的字节，再按契约调用 free。Core 也不执行 JavaScript，只发现、缓存和枚举脚本。

资源发现仍属于 Core，资源事务属于 Browser。Core/宿主通过 Browser 的 `PBrowser_NavigationResource*` API 注册 URL、role 和 required/optional policy，再把每次尝试、成功字节、失败分类或取消结果提交给 Browser。Browser 负责 `pending`、`ready`、`failed`、`cancelled` 的单向终态、按 URL 去重、资源字节预算、transport 重试计数、required/optional gate、hash-only 摘要和 fallback family 计数；宿主负责 DNS/TCP/TLS/HTTP、worker、取消时机和页面提交策略。Browser 不创建线程、不访问网络、不持有 Core document，也不替宿主决定何时重试。

主文档必须先成功，样式表和 `@import` 在 style pass 中通常注册为 required，脚本与图片注册为 optional。宿主在 layout/swap 前调用 `PBrowser_NavigationCommitGetInfo`，一次读取独立 candidate result 与 resource gate；只有 `decision=PBROWSER_NAVIGATION_COMMIT_READY` 且 `can_commit` 非零时才可继续页面提交。required 失败、仍有 pending、资源取消或候选取消/过时时保留旧 document/session/history，不触发 teardown 或 swap。optional 失败可以继续提交，Core 按既定的 alt/src/default-style fallback 绘制。失败摘要最多 4 项且不含原始 URL，transport 失败每项最多 2 次重试（总计 3 次尝试），HTTP、resolve、budget、memory 和取消不重试；这些状态和计数由 Browser 提供，宿主只负责调度与日志。组合快照不合并两个 handle，也不替代 `PBrowser_NavigationCandidateMarkCommitted` 的最终重检。

文档 handle 拥有 DOM、computed styles、box tree、资源缓存、image carriers、表单、交互状态和支持 overflow box 的 retained scrollbar offset。释放文档会使从它借用的节点、字符串、资源字节和几何信息全部失效。`PCore_DocumentWidth`/`PCore_DocumentHeight` 只报告最近一次 layout 的 page-level extent；Core 不访问窗口、不创建窗口，但对支持的 nested overflow 元素拥有滚动位置与 clamp，宿主通过公开 API 接线。style/layout/paint 通常属于同一 UI 线程；不得在后台 worker 并发操作同一个文档。

`PCore_ContentEditableTargetInfo` 是宿主的 native editing-host 快照：它只返回已布局、可见且带非空 id 的有效 editing host，按 DOM 顺序限制为每页 16 个，文本限制为 8192 个 UTF-8 字节。嵌套且仅继承编辑状态的后代归最近 editing host 所有，不会产生第二个窗口。快照的几何和字符串只在当前文档/layout 仍有效时使用；文档 mutation 或重排后宿主应重新枚举并重建窗口。

Core 不保存编辑选区，也不重新派发 `selectionchange`。Browser 以 JavaScript UTF-16 code-unit 偏移提供 `selectionStart`、`selectionEnd` 和 `selectionDirection`，并在范围实际改变时分发一次非冒泡、不可取消的 `selectionchange`；宿主可通过 `PBrowserScriptContentEditableSelectionCallbacks` 把这些范围映射到原生 EDIT，再用 `PBrowser_ScriptSessionNotifyContentEditableSelection` 报告原生范围变化。WM EDIT 宿主对无修饰鼠标拖选和 Shift/方向键扩展保留短暂 anchor，在默认消息完成后报告有序范围和方向；捕获丢失、取消模式或焦点切换会先结束未完成手势，再由 Browser 去重通知。无法物化原生窗口时 Browser 保留脚本侧回退。宿主不得在 Core 中复制第二份文本或选区模型。

受限剪贴板事务同样由宿主负责平台接线：宿主从 `WM_PASTE` 读取 `CF_UNICODETEXT`，从原生选区取得 `WM_COPY` data，或从 `WM_CUT` 取得 data，将 CRLF 规范化为 UTF-8 后交给 Browser 的 `beforeinput`；`WM_COPY` 的折叠选区不改写剪贴板，粘贴/复制数据超长、格式缺失或读取失败时在 native mutation 前 fail closed。对 `WM_CUT`，事件未取消时才允许 native default，再调用 Core 文本 mutation、Browser `input` 和选区通知；WinCE 原生 EDIT 在该默认动作中可能内部重入 `WM_COPY`，宿主只对同一外层剪切临时放行该重入。Core 不读取系统剪贴板、不保存 clipboard handle。

### `positron_browser.dll`

Browser 层拥有无窗口的浏览器会话语义，而不是渲染器：

- 有界 history entries、每项 viewport snapshot、same-document state、traversal 和
  `scrollRestoration` 策略；
- 浏览器 script session 与 bootstrap；
- 浏览器脚本 `window.scrollTo`/`scrollBy` 的 typed viewport callback，以及宿主物理滚动后的去重同步入口；
- 浏览器脚本 `Element.scrollLeft`/`scrollTop`/`scrollTo()`/`scrollBy()` 的有界元素滚动桥：callback 的 `element_id` 把请求交给 Core，`PBrowser_ScriptSessionNotifyElementScroll` 接收宿主 pointer/其他物理路径的实际位置并去重派发目标元素 `scroll` 事件；
- 浏览器脚本 viewport metadata（`innerWidth`/`outerWidth`、`devicePixelRatio`、`screen`）、稳定的 `screen.orientation` 对象及方向变化事件、布局视口对应的 `visualViewport` 快照、宿主 resize 通知、去重的 visual/window `resize` 事件和有界 `matchMedia()` 列表刷新；
- DOM/属性/表单/validation adapter 的 JSON 与 typed dispatch；Browser selector 的
  `:valid`/`:invalid` 和有界 `:in-range`/`:out-of-range` 都读取同一 Core validation
  callback。范围伪类仅覆盖非空且受约束的 input `number`/`range`/`date`/`month`/`week`/
  `time`/`datetime-local`；`range` 的默认范围也算受限范围，underflow/overflow 才是
  out-of-range，空值、bad/type mismatch、disabled/readonly、无范围限制、非 input 和
  单独 stepMismatch 安全不匹配；
- 同一 selector bridge 还提供有界 `:read-only`/`:read-write`：文本输入类型与
  `textarea` 依据 readonly 和 Core effective-disabled 判定，存在
  `isContentEditable` callback 时补充显式或继承 editing host；不支持编辑的 input 类型、
  普通元素以及 callback 缺失时的显式 contenteditable 按既定 fail-closed 规则处理；
- 同一 selector bridge 还提供有界 `:placeholder-shown`：省略 `type` 或使用 `text`、
  `search`、`url`、`tel`、`email`、`password` 的 input，以及 textarea，在 live `value`
  为空且 `placeholder` 值非空时匹配；type/value/placeholder mutation 会被后续查询读取，
  不支持的 input 类型、普通元素和带参数形式安全不匹配，且不扩大 native placeholder
  绘制或 SIP/IME 的宿主职责；
- `isContentEditable`/`innerText` 的有界单元素纯文本桥、脚本侧 `selectionStart`/`selectionEnd`/`selectionDirection` 和去重后的 `selectionchange`；
- Event、input、keyboard、element focus、window focus/blur、composition、click 和导航协调；
- 可选的脚本 form 默认动作事务：`HTMLFormElement.reset()` 先由 Browser 按 form id 通过
  `PBrowserScriptFormEventCallbacksEx` 派发可冒泡、可取消的 `reset`，仅在未取消时调用
  `PBrowserScriptFormResetCallbacks.reset_form`；`requestSubmit([submitter])` 则先调用
  `PBrowserScriptFormSubmitCallbacks.validate_submit`，验证通过后派发可冒泡、可取消的
  `submit`，再调用 `submit_form`。宿主把 reset 接到 `PCore_FormResetById`，把 submit
  接到 Core 的 by-id validation/submission/dialog/multipart primitives，并负责成功后的
  style/layout/paint、网络或 dialog 策略。另一个 `PBrowserScriptFormSubmitDirectCallbacks`
  表为 `HTMLFormElement.submit()` 提供直接默认动作：Browser 不做 validation、submit
  event 或 submitter 选择，宿主把 callback 接到 Core 的三个 NoValidationById primitive
  后执行同一应用策略。三种脚本方法均不由 Browser 创建 native 控件，Core 的 state-only
  入口也不自行派发事件；旧的 form-submit 表保持兼容；
- 可选的 `document.activeElement` 投影：宿主注册
  `PBrowserScriptActiveElementCallbacks` 后，Browser 读取宿主提供的当前焦点
  UTF-8 id，并通过既有 DOM read adapter 解析；空、过长、失效或不可用 id 一律
  回退到 `document.body`；未注册 callback 的 session 不安装该可选属性；
- 可选的 pointer-interaction selector 投影：宿主注册
  `PBrowserScriptInteractionCallbacks` 后，Browser 在每次 selector 查询时按
  `"active"`/`"hover"` 读取当前 id，并以精确节点匹配 `:active`/`:hover`；空、过长、
  失效或未注册 callback 安全地不匹配。Browser 不派发 pointer 事件、不更新 Core
  状态、不自动 style/layout/paint；hit-test、按下/释放/移动时机、失效和视觉由宿主
  负责；
- 可选的 `HTMLElement.focus()`/`blur()` 请求桥：宿主注册
  `PBrowserScriptFocusRequestCallbacks` 或其 Ex 版本后，Browser 在 bootstrap 后
  安装方法，验证 id 与操作值并同步调用宿主 typed callback；宿主用 Core 的按 id
  焦点边界解析目标、更新 native/Core 焦点并派发对应的 focus/blur 与
  focusin/focusout 事件。Ex 版本还把 `focus({preventScroll:true})` 和实际
  page-level scroll 坐标纳入同步结果。若 Browser 能沿 DOM 关系找到保留滚动状态的
  嵌套祖先，它会把同一个 `prevent_scroll` 提示传给宿主，先禁止宿主移动页面，再
  在 callback 返回后用有限的 `container:"all"` reveal 处理最近到最外的祖先；显式
  `preventScroll` 则保持页面和元素滚动位置不变。未注册时不增加方法；注销后已安装
  方法保持安全 no-op；完整滚动树、scroll chaining、scroll-margin 和
  smooth/inertial scrolling 不属于这条边界；
- 页面级与有限嵌套的 `Element.scrollIntoView()`：Browser 复用 Core relation geometry、
  retained-scrollbar 关系 40–43 和现有 page-level/element scroll callback，计算有限的
  block/inline 对齐。父链最多遍历 64 层；默认只选择能被 DOM relation 寻址且拥有 retained
  scrollbar 的最近祖先，`container:"all"` 才从最近祖先向外依次处理适用祖先，并在每次
  滚动后重新读取目标矩形；链完成后目标仍在页面视口外才使用 page-level fallback。宿主
  仍负责 clamp、物理滚动、绘制和实际位置同步；完整 scroll tree、scroll chaining、scroll
  anchoring、scroll-margin、smooth/inertial scrolling 和匿名目标不进入 Browser ABI；
- `Element.getBoundingClientRect()` 与 `Element.getClientRects()` 共用 Core 的有界
  layout fragment relation。块级元素通常返回一个片段，inline flow 按视觉行返回最多
  16 个片段；前者计算这些片段的 viewport-relative union，后者每次新建 array-like
  集合并按索引和 `.item()` 暴露正尺寸矩形。未布局、隐藏、无可用 box 或非正尺寸时
  分别返回全零/空集合。两者都不暴露 Core box 指针，不提供 transforms、Range/Selection、
  完整 nested overflow 坐标、pinch zoom、平滑滚动或视觉像素精度。
- 对支持的已布局元素，Browser 还通过同一 relation callback 暴露只读的
  `offsetWidth`/`offsetHeight`、`clientWidth`/`clientHeight` 和
  `scrollWidth`/`scrollHeight`。这些 getter 只消费 Core 的整数 CSS 像素快照；
  callback 未注册、元素未布局、隐藏、inline/text 或没有可用 box 时返回 `0`，不会
  复制 box model 或触发 relayout。对有 id 的支持 box，Browser 另通过 scroll callback
  接入 Core 的 retained `scrollTop`/`scrollLeft`；关系 40/41 表示 retained scrollbar
  的轴可用性，42/43 表示 padding/client edge 坐标，供有限的 nested
  `scrollIntoView()` 使用。完整 scroll chaining、transforms、pinch zoom 和平滑/惯性
  滚动仍由产品边界排除。
- timer、animation frame、microtask、idle、message 和页面生命周期队列，以及初次完成加载后的 pageshow、可见性切换的 visibilitychange/pagehide/pageshow、宿主驱动的 document.hasFocus/window focus/blur、显式的 document teardown 与队列清理入口；
- `PBrowser_ScriptSessionRunTaskCheckpoint` 提供统一的有界脚本任务检查点：按调用方选择的阶段以 timer → animation frame → message → idle 的固定顺序运行，并在每个阶段后排空一次 microtask；Browser 拥有顺序和队列预算，宿主提供单调时钟、idle deadline、message limit 和消息循环接线；
- native EDIT/SELECT/button/file/disclosure 等平台控件事务状态。
- 导航资源事务：按 URL 去重并合并 role/policy，拥有资源字节、终态、失败分类、transport 重试预算、required/optional commit gate、hash-only failure summary 和 fallback observation。
- 导航候选生命周期：以 opaque handle 拥有不可变 generation、取消请求、退休状态和 committed/failed 终态，并提供当前 generation 的 `CanApply` 提交资格与 pending/committed/failed/cancelled/stale 结果摘要；`PBrowser_NavigationCommitGetInfo` 只读组合该结果与独立资源 gate，`PBrowser_NavigationCleanupGetInfo` 在释放前复制 candidate result 与完整有界 resource observation；两个入口都不接管任一 handle 的所有权。

它通过 callback table 与 Core 和宿主交换信息，不直接依赖窗口、网络或设备控件。callback 必须同步、有界、不可重入，并遵守头文件中的借用缓冲规则。history 与 script-session handle 相互独立，销毁顺序由宿主明确管理。每个 history entry 还由 Browser 保存非负的 `(scroll_x, scroll_y)` viewport snapshot；该快照随 entry 的新建、裁剪、replace 和 traversal 规则维护，但 Browser 不知道 Core 的文档 extent、不访问 HWND，也不替宿主做 clamp。

脚本滚动也遵循同一边界：`PBrowserScriptScrollCallbacks` 接收 Browser 规范化的
CSS page 坐标，由宿主换算为 Core 的物理设备坐标，按当前 extent/client area 应用，
再把实际位置换回 CSS 坐标返回。`Element.scrollIntoView()` 在 Browser 内先用
`getBoundingClientRect()` 的单元素矩形计算 page-level 的 block/inline 对齐；若可寻址
父链中找到 retained overflow ancestor，则默认使用 Core 的 client edge/轴关系只移动最近
元素滚动容器；传入 `container:"all"` 时按最近到最外的顺序处理最多 64 层，并在每次
滚动后重新取得矩形，只有目标仍在页面视口外才复用 page-level callback。默认是 block
start、inline nearest，也支持有限的 center、end 和 `false` 末端对齐。对
`Element.scrollTo()`/`scrollBy()`，
同一个 callback 的末尾 `element_id` 指向 Core 的有界 retained overflow box，返回值是
两个轴的实际 clamp 位置。候选 session 尚未提交时宿主只回显坐标，不能改变旧页面。宿主
完成 page scrollbar、嵌套 overflow pointer、触摸、键盘、resize 或 fragment reveal 后，
分别使用 `PBrowser_ScriptSessionNotifyScroll` 或
`PBrowser_ScriptSessionNotifyElementScroll` 同步 Browser；通知只更新脚本侧状态并在
实际变化时派发一次对应的非冒泡 `scroll`，不会重新调用 callback。这让脚本 origin 与
平台 origin 共用一份最终位置，同时避免同步 callback 递归进入同一 runtime。无
layout/矩形或不支持的 `smooth`、scroll-margin、scroll chaining/anchoring 请求由脚本
安全 no-op，Browser 不替宿主做 clamp、绘制或窗口管理。

窗口 resize 的边界也由 Browser 提供：宿主完成新的 Core style/layout、page-level
clamp 和 native child reposition 后，调用
`PBrowser_ScriptSessionNotifyResize` 传入 CSS viewport 宽高与 DPR。Browser 更新
viewport getters 和 `screen` 的宽高/方向，刷新最多 64 个已创建的
`matchMedia()` 列表，并在匹配结果实际变化时先同步派发一次带 `media`/`matches`
的 `change`。`screen.orientation` 在同一 session 内保持对象身份稳定，方向真正
翻转时再派发一次以该对象为 target 的可信 `change`；随后按顺序派发 visual viewport
与 window 的不冒泡、不可取消 `resize`；同方向的尺寸变化不会伪造 orientation 事件。
相同快照或未发生匹配变化不会重复派发。有效快照变化时，`visualViewport` 的
`width`/`height`/`pageLeft`/`pageTop` 与同一布局视口同步，`scale` 固定为 `1`、
`offsetLeft`/`offsetTop` 固定为 `0`。该入口不访问窗口、不触发 Core layout，也不自动
运行 timer 或 animation-frame queue；页面若在 handler 中排队工作，仍由宿主消息循环
调用独立 pump 或统一的 `PBrowser_ScriptSessionRunTaskCheckpoint` 驱动。统一检查点
不创建线程，也不接管宿主时钟；它只对 `phase_mask` 选中的阶段执行固定顺序，并在
每个阶段后运行有界 microtask。orientation 监听器最多保留 16 个；超过追踪上限的列表只保留
初始匹配快照，完整媒体查询语法、
nested overflow、pinch zoom 和视觉像素差异不因这个通知而获得额外保证。

Browser 还拥有脚本可见的 history scroll-restoration 策略：
`PBrowser_ScriptSessionGetScrollRestoration` 只读返回 `AUTO` 或 `MANUAL`。宿主
在 traversal 后读取 entry viewport snapshot 时，只有明确得到 `AUTO` 才执行自动
clamp/apply；`MANUAL` 保留当前 page viewport。该查询不接管窗口或滚动副作用，
fragment reveal 和显式 `scrollTo` 仍走各自的宿主路径；查询失败必须按默认
`AUTO` 处理，不能错误地跳过恢复。

页面生命周期同样由 Browser 保持顺序：宿主在 classic script 完成后推进
`PBrowser_ScriptSessionDispatchPageLifecycle("complete")`，Browser 先完成已有的
`readystatechange`/`DOMContentLoaded`/`load` 序列，再派发一次 window `pageshow`；
重复的 `complete` 不会复制初始事件。宿主驱动可见性时，进入 hidden 先派发
document `visibilitychange` 和 window `pagehide`，恢复 visible 再派发
`visibilitychange` 和一次 `pageshow`，相同状态保持静默。该有限实现不提供 bfcache，
因此这些 page event 的 `persisted` 固定为 `false`。

窗口激活由宿主在每次 `WM_ACTIVATE` 时调用
`PBrowser_ScriptSessionDispatchWindowFocus(session, focused)` 推进。Browser
归一化 focused 值并维护脚本可见的 `document.hasFocus()`；状态变化时只派发一次
可信、不可取消、不冒泡的 window `focus` 或 `blur`，同时调用对应的属性 handler
和 listener。新 session 默认 focused，非激活窗口创建后由宿主补发零值。该入口
不创建窗口、不操作 native 控件，也不替宿主决定初始焦点、焦点矩形或跨窗口策略。

`document.activeElement` 是另一条显式的宿主桥。宿主保留 native 焦点和平台
控件状态，把 Core 的 `PCore_InteractionFocusElementId`（或等价的应用焦点模型）
适配为 `PBrowserScriptActiveElementCallbacks`，并在页面脚本开始前注册。Browser
只负责 getter、id-addressable DOM lookup 和 `body` 回退，不复制焦点状态，也不
尝试侦测 `WM_ACTIVATE`、创建控件或改变 Core 文档。

初始 `autofocus` 同样不是 Browser session 的自主生命周期步骤。页面提交后，宿主在
Core style/layout 和 native 子控件创建完成的边界显式调用
`PCore_AutofocusTargetInfo`/`PCore_InteractionFocusAutofocus`。有 id 的目标可以接入
既有 Ex focus request callback；无 id 的目标由宿主使用 Core 的 target-preserving
`PCore_EventDispatchFocus` 派发 focus/focusin。Browser 只拥有脚本事件对象、滚动链和
activeElement 的 id 投影，不复制第二份 autofocus 或焦点节点模型；无 id 目标的
`document.activeElement` 仍按可空 id 合同回退到 `body`。

脚本调用 `HTMLElement.focus()` 或 `blur()` 时，Browser 只发出一个同步的、带
`element_id` 与 `focused` 的 typed 请求；Ex 请求还带有 `prevent_scroll`，并接收
宿主实际应用的 CSS page scroll 结果。Browser 不自行选择下一个目标，也不直接访问
窗口。宿主应先用 `PCore_FocusTargetInfoById` 检查当前 layout 中的目标，再按需调用
`PCore_InteractionFocusById`、切换 native HWND，并通过已有事件命中/dispatch 接线
发出 focus family；默认 focus 可在同一宿主 callback 中把目标矩形 reveal 到
page-level viewport；`prevent_scroll` 为真时跳过这一步。若 Browser 在目标到根之间
发现可寻址的 retained overflow ancestor，则它会在 callback 返回后调用已有的
`scrollIntoView({block:"nearest", inline:"nearest", container:"all"})` 路径，
最多处理 64 层并在每次滚动后重读目标矩形；这段嵌套 reveal 由 Browser 拥有，宿主只
负责其 page-level 适配和实际位置同步。Browser 随后再同步脚本 scroll 状态，避免
callback 内重入 runtime。无 id、disabled、hidden、
stale、未布局或其他不符合 Core 资格的目标必须 no-op；重复 focus 不重复派发 focus
family，但 Ex callback 仍可按默认规则 reveal 已聚焦且不可见的目标；对非当前目标的
`blur()` 必须 no-op，且 blur 不执行滚动。该桥只覆盖 id-addressable 的有界目标，不
提供完整 focus navigation、自主自动初始焦点、焦点矩形、滚动树、scroll chaining、
scroll-margin、平滑/惯性滚动、跨窗口策略或原生控件的 OEM 视觉保证。

候选 handle 只表达产品层的 admission 状态，不拥有 response、资源事务、worker、窗口或 Core document。宿主在启动 worker 时创建 handle，在候选被新导航取代时请求取消并退休；worker 完成消息回到 UI 线程后，宿主以当前 generation 调用 `PBrowser_NavigationCandidateCanApply`，并在 layout/swap 前调用 `PBrowser_NavigationCommitGetInfo` 组合 candidate result 与资源 gate；只有组合快照 READY 且最终 candidate 重检通过才能运行页面提交，随后标记 committed 或 failed。worker 收尾后、销毁 candidate/resource handle 前，宿主先让失败或过时 request 的 pending 资源进入终态，再调用 `PBrowser_NavigationCleanupGetInfo`，把 `decision`、终态、gate、pending、`can_release` 和有界 failure/fallback 观测复制到自己的诊断存储；复制后的快照不借用 handle 内存。宿主写日志时调用 Browser 的结果快照，不自行根据 worker 标志重建分类。Browser 不强杀阻塞网络，也不执行 teardown 或 history commit。

浏览器 JavaScript 与 `positron_script.dll` 共用 Duktape 实现，但角色不同：Script DLL 是通用嵌入服务；Browser DLL 负责把有限 Web 对象、事件语义和任务检查点组合到一个页面 session。浏览器脚本仍需要宿主提供真实 DOM、平台默认动作、导航、窗口生命周期和消息循环；如果宿主不调用 pump，页面的异步脚本队列不会自行推进。

## 内部静态库

以下工程是实现细节，不是供第三方应用直接链接的顶层 ABI：

- `positron_netsurf`、`positron_hubbub`、`positron_libcss`、`positron_libdom`；
- `positron_expat`、`positron_libsvgtiny`、`positron_libjpeg`；
- 其他只为公共 DLL 提供目标文件的移植工程。

它们按“一库一工程”隔离上游 include 命名冲突和对象名冲突。外部应用若直接链接这些静态库，将绕过 Positron 的 ABI、所有权和兼容性保证。

## 宿主职责

宿主拥有所有与具体应用或 Windows Mobile UI 绑定的行为：

- 顶层窗口、消息循环和 DPI/旋转通知；layout 后读取 Core 的 page-level width/height，维护两个轴的 viewport offset、clamp 和 native child reposition，并把新的物理 client area 按 DPI 换算后通知 Browser viewport。嵌套 overflow 的 WM 指针则换算为 document 坐标后交给 `PCore_OverflowPointer`，按 `PCore_OverflowDirtyRect` 失效，再把 `PCore_OverflowScrollSnapshot` 转发给 `PBrowser_ScriptSessionNotifyElementScroll`；元素滚动位置和范围不在宿主复制。宿主还负责把每次 `WM_ACTIVATE` 映射为 Browser 的 window focus 通知，但不在宿主复制 `document.hasFocus()` 或 focus/blur 事件语义；
- native EDIT、COMBOBOX、按钮、文件选择器和 SIP/IME；contenteditable 的 WM EDIT 代理也由宿主创建、定位、销毁并跟踪其平台鼠标/键盘选区；受限 `WM_PASTE`/`WM_COPY`/`WM_CUT` 的 `CF_UNICODETEXT` 读取、写入和所有权也只属于宿主；
- 后台线程、loading 状态与页面 swap；较新的导航可以取代仍在准备的候选，宿主为每个请求保存 Browser 的 candidate handle，以宿主 generation 计数器构造并门控 worker 完成、进度和提交消息，再让退休候选持有自己的线程、response、资源队列和脚本对象直到 worker 收尾；Browser handle 拥有该候选的 generation、取消请求、退休状态、提交资格和结果分类，宿主只拥有退休队列与平台回收，并通过 `PBrowser_NavigationCandidateGetResult` 把分类复制到应用日志；退休队列有固定上限，达到上限时新导航 fail closed 而不改变当前页；候选成功时先在旧 document/session 仍有效的窗口内调用 `PBrowser_ScriptSessionDispatchBeforeUnload`，按应用策略处理取消，再调用 Browser teardown、停止 native 回调、释放旧对象并提交新页；
- DNS/TCP/TLS/HTTP 组合策略、worker、取消时机和资源调度；宿主通过 Browser 资源事务注册 URL 并提交 attempt/data/failure/cancel 结果，决定何时重试、何时运行 style/layout、何时提交页面。资源终态、成功字节、预算、required/optional gate、失败摘要和 fallback 计数由 Browser 拥有，宿主读取统计用于 loading、日志和应用策略，不复制第二份资源状态或数据；页面提交时由 Browser 的 `PBrowser_NavigationCommitGetInfo` 给出 candidate/resource 组合快照，宿主不复制其中的分类规则；
- request 结束时的清理顺序仍由宿主编排：先 join worker，失败/过时 request 先取消 Browser 资源事务中的 pending 项，再读取 `PBrowser_NavigationCleanupGetInfo` 并复制有界结果，最后销毁 candidate/resource handle。该快照只提供 Browser-owned 状态，不改变 candidate state、不拥有宿主线程/response/窗口，也不把“可释放”误当成页面提交成功；
- 新窗口、外部协议、下载和文件系统权限策略；
- 把 Core 文档回调注册到 Browser session；布局 relation 还包括元素滚动的当前 offset，
  relation 44 还把 effective-disabled 状态交给 Browser selector；宿主不复制 box tree、
  滚动模型或 fieldset/optgroup 继承规则；
- 把 Core 的焦点 id 查询注册为 Browser 的可选 `document.activeElement` callback，
  把 `PCore_InteractionStateElementId` 注册为可选 interaction callback，并在需要
  脚本主动聚焦时注册 `PBrowserScriptFocusRequestCallbacks` 或 Ex 版本；
  宿主负责按 id 验证 Core 几何/资格、遵守 Ex 的 `prevent_scroll` 并执行
  page-level scroll reveal、native HWND 切换、focus family 事件和重绘调度；页面提交后
  若启用 `autofocus`，宿主还负责在 layout/native 子控件创建完成后显式调用 Core 的
  autofocus 查询/设置入口；Browser 负责已验证目标的有界嵌套 overflow reveal，宿主不得
  在其中复制第二份滚动树；
- 把 Core 的 contenteditable 状态/文本 callback 和 selection callback 注册到 Browser session，并把 WM/native 输入接到 `beforeinput`→Core mutation→`input` 顺序；宿主只保留窗口、焦点、坐标、键盘/拖选 anchor、Shift 状态和原生选区，在范围变化或捕获/焦点中断后调用 Browser 的通知入口，不经 Core 重复派发 `selectionchange`；对 `WM_PASTE`/`WM_COPY`/`WM_CUT`，宿主读取并规范化有界 `CF_UNICODETEXT`，让 Browser 决定取消后再执行 native default，折叠复制保持原剪贴板不变，并对 WinCE `WM_CUT` 的同一 HWND 内部 `WM_COPY` 重入做局部放行；
- 从 Browser 读取活动 modal id，并在 WM_PAINT 中调用 Core 的 modal paint 组合入口；
- 决定何时启用浏览器 JavaScript；
- 应用级崩溃恢复、持久化和日志。

宿主可以实现这些策略，但不得复制已经属于公共 DLL 的 URL、history、DOM、事件、表单或图像业务语义。发现可复用语义仍滞留在 `test_host` 时，应把它视为架构债务并迁移到相应 DLL。

### 产品代码边界门

这是工程纪律，不是对参考宿主的建议：可被另一个 WM6 应用复用的语义必须由拥有它的顶层 DLL 实现，并通过稳定的 C ABI 暴露；`test_host` 只能调用该 ABI。宿主可以包含窗口/WM 接线、网络与线程调度、应用级取消和页面提交策略、测试 fixture 及断言，但不能把产品 `.c` 文件编译进来，也不能定义任何 `PBrowser_*`、`PCore_*`、`PHttp_*`、`PTls_*`、`PJson_*`、`PImage_*` 或 `PScript_*` 公共入口。静态审计负责拦截源文件包含和公共入口定义；代码审查仍必须判断没有名字看似私有的宿主 helper 在复制产品语义。

## 页面加载与提交

推荐的主文档事务如下：

1. UI 线程记录导航意图和当前页面；在离开当前 entry 前，把宿主 viewport 通过 `PBrowser_HistorySetEntryScroll` 写回 Browser，但不立即销毁旧文档。
2. worker 获取主文档及可并行准备的网络资源；网络层不触碰 DOM/NetSurf 状态。宿主为候选创建 Browser handle，并在较新的导航到来时请求取消、退休旧 handle；旧 worker 的完成和进度消息必须同时通过宿主消息身份与 `PBrowser_NavigationCandidateCanApply` 的 generation 门控，仍在收尾的退休候选数量受宿主固定上限约束。
3. UI 线程解析 HTML，创建候选文档。
4. 通过 Core 的 resolver/fetch 回调发现 CSS、`@import`、图片和 script；宿主为每项调用 Browser 资源事务的 register/begin-attempt，并把网络结果、成功字节、失败分类或取消提交回 Browser。取消项不会被后续样式阶段当作待获取资源。
5. style/image pass 若发现新的 pending 资源则回到 worker；样式表与 `@import` 按 required policy，图片与 script 按 optional policy。重复 URL 由 Browser 事务去重并合并 stylesheet/script/image role bitmask；宿主只保留 URL 到 Browser resource index 的短引用。
6. UI 线程在 layout/swap 前读取 Browser 的 `PBrowser_NavigationCommitGetInfo` 与 resource stats；组合快照为 PENDING 时继续等待资源，其他非 READY 结果禁止 swap，并在失败/取消/过时时释放候选、保留旧页、旧 session 和旧队列；optional 失败在 Core fallback 可用时允许继续，并在 layout 成功后通知 Browser 记录 fallback family 观测。宿主日志可读取最多 4 项 hash-only failure summary，但不拥有或重建摘要。
7. 组合快照 READY 且最终 candidate 重检通过后，在旧 document/session 仍有效时先调用 `PBrowser_ScriptSessionDispatchBeforeUnload`。该同步门只返回脚本是否取消，不显示提示或执行导航；宿主按应用策略决定拒绝或继续。只有允许继续时才调用 Browser teardown，停止旧页回调并原子提交页面与 history；新 entry 的 viewport 初始为 `(0, 0)`。
8. history traversal 或 same-document fragment 完成后，宿主用 `PBrowser_HistoryEntryScroll` 读取目标 entry 的快照；对于非 fragment traversal，只有当前 script session 的 `PBrowser_ScriptSessionGetScrollRestoration` 明确返回 `AUTO` 时才按 client area 对 `(scroll_x, scroll_y)` 两个轴 clamp 并应用到 scrollbar/HWND、paint 和命中测试。`MANUAL` 保留当前 viewport，fragment reveal 与显式滚动仍照常执行。Browser 只提供值，不执行滚动副作用。
9. worker 已收尾后、销毁 request 的 candidate/resource handle 前，失败或过时 request 先取消剩余 pending 资源；宿主调用 `PBrowser_NavigationCleanupGetInfo` 复制终态、gate、pending、`can_release` 和有界 failure/fallback 观测。正常路径要求 `can_release` 为非零；若观测仍为 0，宿主必须 fail closed、记录异常并完成成对的最终释放，不能把它当作成功提交。复制值在 handle 销毁后仍可用于日志。
10. 交互、旋转或动态 DOM 修改按需重新 style/layout/paint。

任何后台线程都不能持有 DOM 节点、computed style、box tree 或 HDC。失败日志应区分 DNS、TCP、TLS、证书、HTTP、资源、解析、style、layout 和提交阶段。

## 脚本与事件组合

浏览器脚本默认关闭。显式启用时，宿主按以下原则组合：

1. Core 发现并缓存 classic script；
2. Browser session 注册有界 DOM/Event/platform callbacks；
3. 按文档顺序执行允许的 inline/external script；
4. native Windows 消息先形成 typed event，再由 Browser 决定取消或允许默认动作；WM6 `EDIT` 可能在 `WM_CHAR` 默认处理完成前发送 `EN_CHANGE`，宿主必须在默认处理返回后读取最终值，避免把旧值提交为一次 mutation；`WM_PASTE`/`WM_CUT`/`WM_COPY` 只在可取得有界 `CF_UNICODETEXT` data 时进入 contenteditable 事务，折叠复制直接 no-op；
5. Core 执行 DOM/form/default mutation；对 contenteditable，只有 `beforeinput` 未取消时才执行有界纯文本 mutation，Browser 的脚本 selection API 在可用时同步宿主原生选区；原生 WM EDIT 的无修饰 down/move/up 或 Shift/方向键默认处理返回后，宿主用短暂 anchor 计算方向，并在捕获/取消/焦点中断时收尾，再通知 Browser；paste/copy/cut 还必须在 native default 前完成 clipboard data 的格式和容量检查，且不得让宿主复制一份 Browser 的 Range/Selection 模型；
6. Browser 派发 mutation 后的 `input`、`change`、focus、`selectionchange` 或 lifecycle 事件；原生选区通知必须先由 Browser 去重，不能由宿主和 Core 各发一次；
7. 宿主按需重新 layout/paint；活动 modal 时先让 Core 画普通文档，再组合实体色 backdrop 和 dialog；宿主的 page scrollbar、触摸、键盘或 fragment reveal 更新 page offset 后调用 Browser 的 scroll notification，让脚本 `scrollX`/`scrollY` 与物理位置保持一致；嵌套 overflow 指针更新后通过 Core dirty rect 重绘，并用 element-scroll notification 同步目标元素和事件；WM_SIZE 完成新的 style/layout、clamp 和 native child reposition 后再调用 Browser 的 resize notification，让 `innerWidth`、DPR、稳定的 `screen.orientation` 和 window `resize` listener 看到同一快照；方向翻转时 orientation `change` 位于 visual/window `resize` 之前。
8. 页面替换时先调用 Browser 的 cancelable `beforeunload` 门，再调用 page-teardown（visible 页面依次为 `visibilitychange`、`pagehide`、`unload`，并清理页面队列），最后销毁 session 与文档；被取消或失败的候选不得触发 teardown。Browser 不提供 prompt UI，宿主拥有继续/拒绝策略，脚本调用失败时必须 fail closed。

事件顺序、取消和状态提交必须由产品层确定，不能依赖 test fixture 的偶然消息顺序。真实 SIP、OEM IME、系统 picker 和窗口创建仍需要设备人工验收。

## ABI 与所有权规则

### 字符串

- 跨公共边界的字符串一律 UTF-8，除非参数明确是 Win32 `WCHAR`/HDC 等平台类型。
- 输入字符串在调用返回后仍由调用方拥有，DLL 不保存指针，除非 API 明确说明会复制。
- 借用字符串只在头文件规定的 mutation 或 handle 生命周期内有效。
- probe/capacity API 必须 NUL 终止可写缓冲，并报告完整所需字节数。

### Handle 与内存

- opaque handle 不是 Win32 kernel handle，不使用 `CloseHandle`。
- 创建与销毁 API 必须配对；子节点和查询结果若为借用值，不得单独 free。
- DLL 分配的公开 buffer 必须由同一 DLL 的配对函数释放，不能跨 CRT heap 使用 `free`。
- 销毁父对象后，所有借用节点、字符串、buffer 和 callback token 立即失效。

### ABI 演进

- 已发布结构体通过 `cbSize` 或显式 ABI 版本演进；新字段追加，不改变旧字段布局。
- 新能力优先新增函数或 `Ex` 入口，不静默改变旧入口含义。
- 无效参数、容量不足、越界、错 origin 和错状态必须返回稳定错误，不能部分提交。
- 公共头文件是精确契约；README 只解释调用模式，不复制整套声明。

## 线程与重入

- TLS/HTTP 可以在宿主 worker 使用，但每个连接/response 的并发所有权必须唯一。
- Core document、layout、paint 和 Browser script session 默认由单一 UI 线程串行驱动。
- 同步 callback 不得重入触发它的 session，也不得在回调中销毁父 handle。
- 页面替换、取消和关闭必须先阻止新回调，再释放平台控件、script session、Core document 和 history/app state；具体顺序以拥有关系为准。
- 导航取消是宿主与网络层之间的协作式边界：generation 立即阻止过时结果提交，worker 在网络调用边界检查取消并在完成消息后释放退休请求；公共 HTTP 调用正在阻塞时不保证立即中断 socket，宿主不得把“已请求取消”误报为“网络已即时停止”。

## 平台与移植约束

所有产品 C 代码必须兼容 VS2008 的 C89 方言和 WM6 ARMV4I：

- 不使用块中声明、`for (int ...)`、designated initializer 或 C99-only CRT；
- 缺失 CRT/Win32 API 通过 `compat/` 中可审计 shim 解决；
- 第三方 C99 降级应由可重复、幂等的转换脚本完成；
- 使用正式 `.sln`/`.vcproj` 构建，不用现代桌面编译结果代替目标构建；
- vendored 源码保持版本、许可证、生成步骤和本地补丁记录。

移植代码的正确性需要三层证据：转换器回归、VS2008 ARMV4I 构建、真实设备行为。只满足其中一层不足以成为产品基线。

## 明确非目标

- TLS 1.3、HTTP/2、HTTP/3 或现代浏览器级网络栈；
- 完整 WHATWG URL、DOM、HTML、CSSOM、Web API 或 ECMAScript host environment；
- 完整 CSS Grid、任意 float/position/table 边界和桌面级字体排版；
- 完整 nested overflow scroll tree、scroll chaining/anchoring、scroll-margin、平滑/惯性
  滚动和无界 `scrollIntoView()` 容器遍历；当前只提供带 id 的常见 box 的 retained
  offset bridge，以及最多 64 层可寻址父链中最近容器的一次有限 reveal，或显式
  `container:"all"` 时从最近到最外的有界滚动链；
- 通用 ClipboardEvent、async clipboard、CF_TEXT/富文本转换或跨应用剪贴板格式互操作；当前宿主只提供有界 `CF_UNICODETEXT` contenteditable paste/copy/cut 接线；
- 多窗口浏览器、完整现代 modal dialog/backdrop（仅支持有界实体色组合）或持久化浏览历史；
- 在 DLL 内接管应用消息循环、系统 picker、OEM IME 或设备连接；
- 把 `test_host.exe` 变成产品依赖。

当前具体支持范围与剩余缺口见[已知限制](../.agents/KNOWN_LIMITATIONS.md)。

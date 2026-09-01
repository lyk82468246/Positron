# 当前已知限制

本文件只列仍然存在、会影响设计或验收的限制。已解决问题不保留在这里；失败路线见 [`FAILED_EXPERIMENTS.md`](FAILED_EXPERIMENTS.md)，旧事故见 [`docs/history/`](../docs/history/README.md)。

## 平台与工具链

- 目标是 Windows Mobile 6 / Windows CE 5.2 ARMV4I，不支持现代桌面 Windows API 假设。
- 正式构建依赖 Visual Studio 2008 SP1 与 Windows Mobile 6 Professional SDK。
- 产品 C 代码受 C89 约束；部分第三方源码依赖仓库转换器和 WinCE CRT shim。
- VS/WMDC/Device Emulator 属于外部专有工具链，仓库不能提供或重现完整安装环境。
- WM6 的 Smart Minimize 可能保留进程和系统级 DLL 映射；跨 stage 运行存在混用旧 DLL 的风险。

## TLS 与 HTTP

- mbed TLS 固定在 2.16.12，已结束上游支持；没有 TLS 1.3，发布前必须审查当前漏洞与信任数据。
- verified 客户端仍受旧平台证书、时钟、密码套件和根证书快照限制。
- insecure 连接入口仍为兼容/诊断而保留，调用方若误用会失去证书与 hostname 认证。
- peer TLS pin 的空值只适合受控 discovery/TOFU，不代表已认证对端。
- identity 文件由消费者负责持久化、访问控制、备份和轮换，DLL 不提供系统密钥库。
- HTTP 只覆盖有界 HTTP/1.1，不提供 HTTP/2、HTTP/3、连接池、完整缓存、cookie jar 或浏览器级代理策略。
- URL reference resolver 是保守的 HTTP(S) 子集，不是完整 WHATWG URL 实现；userinfo、IPv6、非 HTTP(S) scheme 和异常 authority 会 fail closed。
- 真实网络测试仍受设备时钟、DNS、TLS、代理和外部站点变化影响，离线契约不能替代网络哨兵。

## HTML、CSS 与布局

- HTML/CSS/DOM 由固定版本 NetSurf 支持库移植而来，不等于现代浏览器当前实现。
- CSS Grid、完整 float、完整 positioned layout、复杂 table/caption/column/baseline、完整 generated content 与自定义 counter style 未覆盖。
- 仅支持一部分媒体条件、selector、字体与单位；custom properties、`var()` 和大量现代函数缺失。
- `details`/`summary`、`hidden` 等只有受限静态或交互子集；`dialog` 已有 Browser 脚本的 show/showModal/close/requestClose、returnValue、cancel/close 事件、活动 modal id、宿主驱动的 Escape 请求桥接和参考宿主的有界 backdrop 点击策略。Core/Browser 组合支持有 id 祖先 dialog 的显式、脚本和隐式 `method="dialog"` 提交，包括 validation、可取消 `submit`、submitter value 与直接 close；无 id、无祖先 dialog 或跨文档目标会 fail closed。宿主可以组合 Core 的 scoped focus snapshot 实现顺序 Tab/Shift+Tab 子树范围，并调用 `PCore_PaintDocumentWithModal` 得到实体色遮罩和指定 dialog 重绘；这不是 CSS `::backdrop`、透明合成或跨文档 top layer。Browser 不自动接管平台消息；宿主必须显式调用这些边界。
- Core 已支持有界的自定义 `tabindex` 顺序：正值升序（同值保持 DOM 顺序），随后是零/缺省组；`PCore_FocusTargetInfoWithin` 可按已知 DOM id 限定到一个祖先子树；负值、disabled/hidden/stale 目标和 file picker 仍会被排除。完整浏览器焦点策略、自动初始焦点、动态焦点区域和跨窗口焦点仍未实现。
- Core/Browser 对带 DOM `id` 的常见 block/replaced/flex overflow box 提供 retained scrollbar offset、`scrollLeft`/`scrollTop`、`scrollTo()`/`scrollBy()` 和宿主 pointer 同步；这只是有界的两个轴桥接，不能代表完整 CSS overflow 语义。client 尺寸是 retained scrollport 的 padding 区域，滚动条覆盖在边缘。
- Browser script session 已能由宿主显式维护顶层窗口 focus/blur 状态，并让 `document.hasFocus()` 与去重后的 window 事件保持一致；这不等于完整浏览器焦点策略，native 控件焦点、焦点矩形、焦点陷阱和 OEM/跨窗口激活仍由宿主负责。
- Core 的 `PCore_InteractionFocusElementId` 只报告当前交互状态中、带非空 UTF-8
  id 的焦点节点；没有焦点、没有 id、节点过时或缓冲不足时调用方必须按失败/回退
  处理。Browser 的 `document.activeElement` 是显式 callback 注册后才安装的可选
  projection，通过现有 ID lookup 返回元素，否则返回 `document.body`；它不提供
  自动初始焦点、完整焦点算法、native 焦点矩形或跨窗口焦点。
- `contenteditable` 目前覆盖单元素的祖先继承、`isContentEditable`、有界纯文本 mutation、宿主编排的 `beforeinput`/`input`，Browser 的 `selectionStart`/`selectionEnd`/`selectionDirection`，以及去重后的非冒泡、不可取消 `selectionchange`。带 id 且已布局的有效 editing host 可由宿主映射为最多 16 个 WM multiline EDIT 代理；无修饰鼠标拖选和 Shift/方向键扩展会把 CRLF 位置转换为逻辑 UTF-16 并报告 forward/backward 方向，捕获丢失、取消模式和焦点切换会结束未完成手势而不重复通知。文本上限为 8192 UTF-8 字节，嵌套继承后代不重复代理。宿主另有受限 `CF_UNICODETEXT` paste/cut 事务，但 Range/Selection 对象、ClipboardEvent/async clipboard、CF_TEXT/富文本转换、OEM 特有的自动重复与复杂行导航、design mode 和完整 IME 组合仍未实现。
- 字体 fallback 使用 bundled 子集与系统 GDI，不能保证桌面浏览器字形、kerning、emoji 彩色渲染或抗锯齿一致。
- WM6 高 DPI、字体度量和设备色深会产生量化差异；自动像素断言不能取代整体视觉判断。

## 图像与 SVG

- 位图能力受 WM Imaging 与固定 libjpeg-turbo 版本限制，不宣称支持所有损坏或渐进编码边界。
- SVG 是 libsvgtiny 与 NanoSVG 的有限组合，不支持完整 SVG DOM、filter、mask、animation、script、external resource、完整 paint server 和任意文本排版。
- 径向渐变焦点、spread method、复杂继承和部分 alpha/compositing 边界仍不完整。
- Core 的页面 image cache 有固定数量和字节预算，超限或解码失败时降级为 alt/src 文本。

## DOM、表单与事件

- DOM bridge 以有界 ID/结构 token 和 snapshot collection 为主，不是完整 live DOM/CSSOM。
- 大量 IDL reflection、namespace、mutation observer、range/selection 和 shadow DOM 不存在。
- 表单实现覆盖常用控件、validation、submission、reset 和 successful controls，但没有完整本地化 validation UI、所有 input type 的系统 picker 或桌面浏览器级 editing 行为。
- `labels`、form collections 和若干 NodeList 是静态 snapshot；文档 mutation 后调用方应重新查询。
- 事件系统覆盖常用 capture/target/bubble、取消和默认动作，但不支持所有 DOM Event 子类、pointer/touch/drag/drop/clipboard 或浏览器手势。宿主对单元素 `contenteditable` 另有受限 `CF_UNICODETEXT` paste/cut/copy 接线：非空选区才复制，折叠选区保持剪贴板不变，超长或非 Unicode 格式在 native mutation 前拒绝；它不是通用 DOM ClipboardEvent 或 async clipboard API。
- native 控件状态由 Core、Browser 和宿主共同提交；回调错误、stale token 或几何变化会 fail closed，可能表现为本次默认动作不执行。

## JavaScript

- 浏览器 JavaScript 默认关闭，启用后仍是实验性的有界 classic-script 组合。
- 独立 script 和浏览器 script 共用 Duktape 2.7.0，不存在第二套引擎；两者提供的 host objects 与生命周期不同。
- 不支持 ES module、dynamic import、WebAssembly、worker、service worker 或完整现代 ECMAScript host environment。
- Browser bootstrap 只暴露当前已接线的 DOM/Event/form/navigation/timer 子集；缺失 API 通常 fail closed 或为 `undefined`。
- `window.scrollTo`/`scrollBy` 的 page-level 请求，以及 `Element.scrollIntoView()` 的
  有限 block/inline 对齐，只有在宿主注册 `PBrowserScriptScrollCallbacks` 时才会应用到
  真实 viewport；callback 和 `PBrowser_ScriptSessionNotifyScroll` 使用 CSS page 坐标，
  宿主必须在 Core 的物理设备坐标与 CSS 坐标之间换算，返回 clamp 后的坐标，并在滚动条、
  触摸、键盘、resize 或 fragment reveal 后通知 Browser。`scrollIntoView()` 复用单元素
  `getBoundingClientRect()`，默认 start/nearest，支持 center、end 和 `false`，只接受
  `behavior` 的 `auto`/`instant`，以及 `container` 的 `nearest`/`all`；无 layout/矩形或
  不支持的 smooth、scroll-margin 请求安全 no-op。若父链能由 DOM relation 寻址，Browser
  最多遍历 64 层：默认把目标交给最近 retained overflow ancestor，`container:"all"`
  才从最近到最外依次处理适用祖先，链完成后目标仍在页面视口外才回退到 page-level
  scroll。带 id 的常见 overflow
  box 可使用 `scrollLeft`/`scrollTop`/`scrollTo()`/`scrollBy()`，由 Core callback clamp，
  并由宿主的 pointer notification 同步；该边界不覆盖完整滚动链/锚定或平滑/惯性滚动。
- 宿主完成 WM_SIZE 的 Core style/layout、page-level clamp 和 native child reposition 后可调用 `PBrowser_ScriptSessionNotifyResize`；该入口更新 `innerWidth`/`outerWidth`/`devicePixelRatio`、`screen` 宽高/方向和布局视口对应的 `visualViewport`，刷新每个 session 最多 64 个 `matchMedia()` 列表，并在匹配结果翻转时同步派发 `change`。同一 session 的 `screen.orientation` 对象保持身份稳定，方向翻转时再派发一次可信 `change`，随后按 visual viewport、window 顺序派发 `resize`；同方向尺寸变化不派发 orientation 事件。`visualViewport` 的 scale 固定为 1，offset 固定为 0，pageLeft/pageTop 与 page scroll 同步；它不替宿主运行 timer/animation frame，不支持完整媒体查询语法、pinch zoom 或嵌套 overflow，也不为视觉像素或真实旋转提供保证；超过 64 个媒体列表和 16 个 orientation 监听器只保留有界的已注册状态。
- `Element.getBoundingClientRect()` 只在 Core 已完成 layout 且存在对应 box 时返回由
  片段组成的整数 CSS 像素 border-box union；未布局或不可用时为全零矩形。
  `getClientRects()` 每次新建最多 16 个按视觉行排列的正尺寸矩形；块级元素通常一个，
  inline flow 可有多个。两者都要求宿主在 layout 后同步 page scroll，不提供 transforms、
  Range/Selection、完整 nested overflow 坐标、pinch zoom、平滑滚动或视觉像素精度。
- Browser 还可把 Core 的最近一次 layout 快照映射为只读的
  `offsetWidth`/`offsetHeight`、`clientWidth`/`clientHeight` 和
  `scrollWidth`/`scrollHeight`。目前只承诺已布局的常见 block、replaced、table/flex
  box；inline/text、隐藏、未布局或无 box 时返回 `0`，查询不触发 relayout。offset、
  client、scroll 的整数 CSS 像素定义不等于完整 CSSOM box model。带 id 的常见
  overflow box 另有 retained `scrollTop`/`scrollLeft`、`scrollTo()`/`scrollBy()` 桥，
  但不提供 scroll chaining、scroll-margin、smooth/inertia、transforms 或 pinch zoom；
  没有 id、layout 或 retained scrollbar 时安全 no-op。`scrollIntoView()` 的祖先链仍是
  有界的，不提供完整滚动树或标准 scroll chaining。
- 脚本任务队列不会自行创建线程或从 Browser session 后台推进。宿主必须在自己的 UI 消息循环中调用独立 pump，或用 `PBrowser_ScriptSessionRunTaskCheckpoint` 选择阶段；统一入口按 timer → animation frame → message → idle 的顺序运行，并在每个阶段后执行一次有界 microtask。宿主仍负责单调时钟、frame timestamp、idle deadline、message limit 和调度/功耗策略；未调用 pump 的页面不会推进这些异步队列。
- script heap、native function、module/source、timer、queue 和执行时间都有固定预算；复杂页面可能因资源上限失败。`PSCRIPT_MAX_NATIVE_FUNCTIONS` 当前为 26；Browser 同时启用 DOM、validation、contenteditable、导航、`document.activeElement` 和 `HTMLElement.focus()`/`blur()` 桥时会占满槽位，额外宿主 native function 必须先检查计数并在达到上限时保守失败。
- 页面首次完成加载时，宿主需显式推进 `PBrowser_ScriptSessionDispatchPageLifecycle("complete")`；Browser 在既有的 `readystatechange`、`DOMContentLoaded`、`load` 序列后派发一次 `pageshow`，重复 complete 不会复制。宿主驱动可见性时，进入 hidden 派发 `visibilitychange`→`pagehide`，恢复 visible 派发 `visibilitychange`→`pageshow`，相同状态保持静默；`persisted` 固定为 `false`，不提供 bfcache。页面替换仍要求先显式调用 `PBrowser_ScriptSessionDispatchBeforeUnload`：在旧 session 仍有效时同步派发有界、可取消的 `beforeunload`，由宿主决定是否提供自己的确认 UI；参考宿主没有 prompt，取消或脚本调用失败就保留当前页面。允许继续后再调用 `PBrowser_ScriptSessionDispatchPageTeardown`，派发 `visibilitychange`、`pagehide`、`unload` 并清理页面队列；不提供异步卸载保证。
- 窗口 focus/blur 也必须由宿主在每次 `WM_ACTIVATE` 时调用 `PBrowser_ScriptSessionDispatchWindowFocus`；新 session 默认 focused，非激活窗口创建后要补发零值。该 API 只同步脚本状态和事件，不侦测 OEM 激活，也不保证 native HWND 焦点或视觉结果。
- `document.activeElement` 只有在宿主注册 `PBrowserScriptActiveElementCallbacks`
  后才存在；getter 每次同步读取一个有界焦点 id，并通过 DOM read adapter 解析。
  空、超长、过时或不可用的 id 都回退到 `document.body`，注销来源后也保持该回退。
  未注册 callback 的 session 不承诺这个可选属性，以控制 WM6 bootstrap 成本。
- `HTMLElement.focus()`/`blur()` 只有在宿主注册
  `PBrowserScriptFocusRequestCallbacks` 或 Ex 版本后才安装；Browser 只验证并同步
  转发 `element_id`/`focused` 请求。Ex 版本还传递 `prevent_scroll`，宿主以 Core 的
  `PCore_FocusTargetInfoById`/`PCore_InteractionFocusById` 接线 native HWND、focus
  family 和重绘，并可把默认 focus 的 page-level reveal 结果回传给 Browser。若 Browser
  能从目标向上找到最多 64 层内、可由 Core relation 寻址的 retained overflow ancestor，
  它会把 `prevent_scroll` 设为有效提示，让宿主先保持 page viewport 不动，再复用
  `scrollIntoView({container:"all"})` 完成最近到最外的有限嵌套 reveal；显式
  `focus({preventScroll:true})` 则保持页面和元素滚动位置不变。Browser 在 callback
  返回后同步脚本滚动位置；无 id、disabled、hidden、stale、未布局、对非当前目标的
  blur，以及注销后的方法都 fail closed/no-op；重复 focus 不重复派发 focus family，
  blur 不执行滚动。该桥不提供完整 focus navigation、自动初始焦点、focus ring、
  完整滚动树、scroll chaining、scroll-margin、平滑/惯性滚动、跨窗口策略或 OEM 控件
  视觉保证。
- Browser session callback 同步且不可重入；宿主若在 callback 中销毁或重入 session，行为不受支持。
- 该运行时不是完整浏览器安全沙箱，不能直接执行不可信互联网脚本并假定与现代浏览器等价隔离。

## History、导航与窗口

- history 是进程内、有界条目集合，不持久化到磁盘，也不恢复跨进程页面状态。
- same-document 与跨文档 scroll restore 覆盖 Browser entry 保存的有界 page-level `(scroll_x, scroll_y)` viewport snapshot；参考宿主读取 Core 的 page-level width/height，对两个轴按当前 client extent clamp，并把物理坐标用于 scrollbar、paint、命中测试和 native child。浏览器脚本的 page-level `scrollTo`/`scrollBy` 也可经 typed callback 应用到该视口，宿主在 CSS page 坐标与物理坐标之间换算，并在物理滚动后用 notification 同步脚本偏移；有效变化会先派发 `visualViewport.scroll`，再派发 window `scroll`。脚本把 `history.scrollRestoration` 设为 `manual` 时，宿主会跳过自动 entry restore，但 fragment reveal 和显式滚动仍可执行。元素 overflow 的 retained offset 不属于 history snapshot；完整滚动容器树、scroll chaining、视觉 viewport 偏移、滚动锚定、平滑/惯性滚动和跨窗口恢复仍未实现。
- 当前是单窗口/单 browsing context 组合；`_blank`、未知 named target、第二个 global、opener、跨窗口 history 和真实窗口复用未实现或保守拒绝。
- `window.open()` 仅在允许复用当前 context 的受限 target 上工作，不创建新的 WM 顶层窗口。
- download、外部协议、权限、文件系统和应用跳转策略仍由宿主决定。
- Browser candidate 以不可变 generation、取消请求、退休状态和 committed/failed 终态保护 UI 文档提交；`CanApply` 同时检查 generation 与 active 状态。宿主仍拥有 worker、response、资源事务、WM 消息、退休队列和页面 swap；退休队列有界，达到上限时新导航 fail closed 并保留当前页。取消是协作式的：worker 若已进入阻塞的 PHttp 调用，不能保证 socket 立即中断；DOM parse/style/layout/paint 仍在单一 UI 线程，复杂页面可能造成短时卡顿。
- Browser 资源事务按 URL 拥有 `pending`、`ready`、`failed`、`cancelled` 终态、失败分类和成功字节；transport 失败每项最多重试 2 次（最多 3 次尝试），HTTP、resolve、budget、memory 和 cancelled 不重试，预算耗尽保持 transport failure。样式表/`@import` 是 required，脚本/图片是 optional；`PBrowser_NavigationCommitGetInfo` 在 layout/swap 前提供 candidate/resource 组合 gate，required 失败、未收敛 pending、资源取消、候选过时或 cancellation 保留旧 document/history，optional 失败交给 Core fallback。统计最多保留 4 项 `role/failure#hash`，fallback family 计数是粗粒度观测，不等于逐元素归因或可见 UI；重复 URL 和深层 `@import` 的去重与分类已由 TEST1123 覆盖，但不能保证任意真实站点的 fallback 视觉。

- 清理边界由宿主在 worker join 后编排：失败或过时 request 必须先让 Browser 资源事务中的 pending 项进入 `cancelled` 等终态，再读取 `PBrowser_NavigationCleanupGetInfo`。该 API 只复制 candidate result、resource gate、pending、hash-only failure summary 和 fallback 计数；`can_release` 对未收敛工作保持为 0，committed candidate 还要求 READY gate。复制值在 candidate/resource handle 销毁后仍然有效，但它不保证任意网络调用已即时中断，也不提供逐资源 UI 或页面视觉归因。

## Native 控件、SIP 与设备 UI

- Windows Mobile EDIT/COMBOBOX/button/file picker 的真实行为因 ROM、OEM 和输入法而异。
- synthetic `WM_CHAR`/key/composition/mouse 测试可以证明 WM EDIT 代理的事务边界、有界脚本选区同步、selectionchange 去重、无修饰拖选方向、Shift/捕获/焦点中断的收尾，以及 TEST1115 的受限 `CF_UNICODETEXT` paste/cut data、取消和 fail-closed 路径、TEST1116 的 `WM_COPY` 非空/折叠选区和超长/非 Unicode 拒绝；由于 WinCE 的直接 `SendMessage` 不会更新键盘状态表，TEST1114 对 Shift 扩展在 key-up 前注入有界原生范围，不能替代 OEM 默认键盘行为。CF_TEXT/富文本转换、不同应用的剪贴板互操作、候选词窗口、完整 IME、真实硬键盘、自动重复或 SIP 视觉仍需人工验收。
- 文件选择器的权限、取消、窗口返回和路径显示需要真实设备人工验收。
- select popup、焦点矩形和滚动可见性仍可能受控件窗口层级与 DPI 影响；next670 已修复并在 192-DPI 设备验证 block 文本 label forwarding，复杂嵌套 label 或其他窗口层级组合仍需人工观察。
- 旋转、不同 screen/DPI、软键盘占用区域和系统非客户区只能通过设备观察确认。

## WMDC 与自动化

- 设备连接必须由用户在 WMDC/Device Emulator GUI 中完成；设备门不能自动选择、cradle 或重置。
- RAPI 1 只暴露当前连接，不支持安全枚举多个设备；自动门假定恰好一个当前目标。
- 主机 WMDC 重装可能恢复旧 RAPI COM 注册值并触发 `0x8007007E`，应使用严格、幂等的修复脚本取证，不要手改未知注册项。
- RAPI 没有可靠的通用远端强杀语义；超时后可能需要用户在设备上关闭遗留进程。
- 新增公共 DLL 导出后若直接使用增量链接产物，设备门曾出现已有 Browser bootstrap 的 `PSCRIPT_ERROR_TIMEOUT (-4)`；完整执行 `scripts\build.bat Debug rebuild` 并重新 staging 后恢复通过。看到这类 bootstrap 超时应先排除旧产物/混包，不能把一次增量构建失败当作产品回归。
- 自动可视门只保证首帧和断言，不保证边距、字体、触摸、SIP、picker、旋转或失败网络体验。

## 测试覆盖

- next670 已建立一次 1080 项动态全量自动设备 checkpoint；后续定向门仍不能替代下一次按风险触发的全量范围基线。
- TEST1081/TEST1082 覆盖 Browser-owned history viewport snapshot 的 fragment/traversal 保持、新 entry 清零、横向值存取、宿主 clamp 和非法参数 fail closed；
- TEST1128 覆盖 Core page-level width、宿主横向/纵向 viewport clamp、Browser 横向 snapshot restore、fragment document-space 命中和直接相邻的离线宽页面路径；不证明嵌套 overflow 容器或真实页面视觉兼容性；
- 已有离线 compatibility-corpus 流程，仍不代表任意真实网站或完整 Web 标准：
  - TEST1117–TEST1119 覆盖 contenteditable/dialog/history 组合、重复资源准备、失败旧页保留、页面 teardown、generation 取消、过时消息隔离、退休请求回收和最新候选提交；
  - TEST1120–TEST1123 覆盖 Browser 资源事务终态、失败分类、transport 重试预算、required/optional gate、重复 script/image、三层 `@import`、最多 4 项 hash-only 摘要和 layout 后 fallback family 观测；
  - TEST1124–TEST1126 覆盖 candidate generation/取消/退休/终态结果、candidate/resource commit snapshot、`can_commit` 和非法参数；
  - TEST1127 覆盖 cleanup snapshot 的 pending/terminal decision、required failure、optional fallback、取消、stale、清理前复制和 handle 销毁后的快照存活性。
- TEST1130 覆盖 Core layout relation 与 Browser `getBoundingClientRect()` 的边界、矩形边界算术和 page-level scroll 换算；不证明复杂 CSS 几何、nested overflow 或真实页面视觉。
- TEST1131 覆盖 WM_SIZE 到 Browser 的 CSS viewport/DPR resize 通知、window 事件字段、重复快照去重和 screen 方向更新；不证明真实旋转、字体/边距、滚动条或 resize handler 中 animation-frame 的视觉结果。
- TEST1133 覆盖 `visualViewport` 的布局视口/page scroll 快照、visual/window 事件顺序、事件字段、监听器移除和重复 resize/scroll 去重；不证明 pinch zoom、nested overflow 或视觉像素。
- TEST1134 覆盖 `history.scrollRestoration` 的 Browser→宿主策略门：`manual` 保留当前 viewport，`auto` 恢复并 clamp Browser entry snapshot，且非法查询参数 fail closed；不证明复杂窗口或真实页面的视觉滚动体验。
- TEST1135 覆盖稳定 `screen.orientation` 对象、方向翻转 `change` 事件、同方向/重复 resize 去重、监听器移除和非法参数保护；不证明设备真实旋转动画、非客户区或视觉像素。
- TEST1139 覆盖 `document.hasFocus()`、window focus/blur 的状态变化、事件字段、属性 handler、listener、非零归一化和重复通知去重；不证明 OEM 激活通知、native 控件焦点矩形或真实窗口切换视觉。
- TEST1140 覆盖 Core 焦点 id 到 Browser `document.activeElement` 的可选桥、body 回退、
  注册/注销、Core size-probe、过小缓冲和失效 id 的 fail-closed 行为；不证明完整
  浏览器焦点算法、自动初始焦点、native HWND 焦点矩形或真实窗口切换视觉。
- TEST1141 覆盖按 id 的 `HTMLElement.focus()`/`blur()` 请求、Core 目标资格与
  focus node 更新、旧目标 blur/focusout→新目标 focus/focusin 顺序、非当前目标
  blur、不可用目标和注销后的 no-op；不证明完整焦点算法、自动初始焦点、焦点矩形、
  page-level focus reveal、真实 native HWND/OEM 控件或跨窗口视觉。
- TEST1142 覆盖 Ex focus request 的默认 page-level focus reveal、callback 后脚本
  scroll 同步、目标矩形可见性，以及 `focus({preventScroll:true})` 的 viewport 保持
  和 scroll 事件抑制；它只覆盖页面级目标，不证明 nested overflow、scroll-margin、
  平滑/惯性滚动、真实触摸滚动、不同 DPI 下的焦点视觉或 OEM 控件行为。
- TEST1143 覆盖页面级 `Element.scrollIntoView()` 的默认 start/nearest、`false` 末端
  对齐、center 对齐、已可见目标的 nearest 静默，以及不支持 smooth 行为的安全拒绝；
  不证明 scroll-margin、nested overflow、平滑/惯性滚动、复杂布局对齐或真实滚动条、
  触摸和不同 DPI 下的视觉结果。
- TEST1144 覆盖 `Element.getClientRects()` 的单矩形回归：新集合/新矩形身份、
  `length`/索引/`.item()` 合同、与 `getBoundingClientRect()` 的几何一致性、page-level
  scroll 跟随和隐藏元素空集合；不证明多片段文本。
- TEST1145 覆盖窄容器 inline 文本的多片段组合：Core count/index relations、Browser
  `getClientRects()` 的按行顺序和新对象身份，以及 `getBoundingClientRect()` union。
  它不证明 transforms、Range/Selection、nested overflow、pinch zoom、复杂字体度量或
  视觉像素精度。
- TEST1146 覆盖支持 box 的六个布局尺寸 relation、Browser 只读 getter、边框/内边距/
  retained-scrollport 算术、后代 extent 和隐藏元素零值回退；它不证明元素滚动操作或
  真实滚动条视觉。
- TEST1147 覆盖带 id 的嵌套 overflow box：Core relation 38/39、按 id setter、两个轴
  clamp、Browser `scrollLeft`/`scrollTop`/`scrollTo()`/`scrollBy()`、目标元素 scroll
  事件去重，以及宿主 pointer snapshot→Browser notification 的同步。它不证明完整
  滚动容器树、scroll chaining/anchoring、scroll-margin、smooth/inertia、匿名目标、
  触摸手势或滚动条视觉。
- TEST1148 覆盖 `Element.scrollIntoView()` 对最近可寻址 retained overflow 祖先的一次
  有限嵌套 reveal：Core relation 40/41 报告轴可用性、42/43 报告 client edge，Browser
  最多遍历 64 层并只移动最近祖先；断言包括两个轴的 start 对齐、页面 viewport 保持、
  一次非冒泡 scroll、重复 nearest 静默和 smooth 拒绝。它不证明完整 scroll tree、
  scroll chaining/anchoring、scroll-margin、平滑/惯性滚动、匿名目标、复杂布局或真实
  滚动条视觉。
- TEST1149 覆盖 `Element.scrollIntoView({container:"all"})` 的两个 retained overflow
  ancestor 链：Browser 从最近到最外依次重读目标矩形并滚动，断言 inner→outer 事件顺序、
  页面 viewport 稳定、重复 nearest 静默、未知 container 与 smooth 拒绝。它仍不证明
  无界滚动树、scroll chaining/anchoring、scroll-margin、平滑/惯性滚动、匿名目标、
  复杂布局或真实滚动条视觉。
- TEST1150 覆盖 `HTMLElement.focus()` 对同一嵌套 overflow 链的协调：Browser 发现
  retained ancestor 后向 Ex host callback 传递有效 `prevent_scroll`，再按 inner→outer
  顺序完成有限 reveal；断言两个轴、focus/focusin 与 scroll 顺序、重复 focus 静默、
  `focus({preventScroll:true})` 保持页面和元素位置，以及远端目标 blur 不移动页面。
  它仍不证明完整焦点算法、滚动树、scroll chaining/anchoring、scroll-margin、平滑/惯性
  滚动、复杂布局或真实滚动条视觉。
- tracked INI 是快速 smoke，不是测试全集；全量自动清单由打包/门脚本从源码 dispatch 生成。
- manual-only fixture 必须在 `auto=0` 下运行，不能放入自动全量并把主动跳过视为通过。
- TEST13 是一个真实网页哨兵，不代表任意互联网网站兼容性。
- 人工风险可以按规则累计，但崩溃、数据损坏、严重布局破坏和核心交互阻塞必须立即复核。

## 明确不保证

- 现代浏览器标准符合性或任意网站可用性；
- 在未审查旧依赖安全状态时用于高风险生产环境；
- 由 `test_host.exe` 提供可复用产品 API；
- 通过单次截图、提示音、部分日志或桌面构建证明设备基线；
- 绕过公开头文件、直接链接内部 NetSurf 静态库后的 ABI 稳定性。

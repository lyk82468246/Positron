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
 - 仅支持一部分媒体条件、selector、字体与单位；custom properties、`var()` 和大量现代函数缺失。Browser 脚本 selector 目前只覆盖简单 compound selector、顶层逗号列表、后代/子代/相邻兄弟/一般兄弟组合器，六类有界属性匹配，`:checked` 的 input/option 状态、`:valid`/`:invalid` 的 form 与可验证控件状态、有界 `:in-range`/`:out-of-range` 的范围验证状态，以及只接受单一简单 compound 参数的 `:not()`、`:is()`/`:where()` 的最多 16 个简单 compound 分支、`:has()` 的最多 16 个相对简单 compound 分支、有界 `:target` 和单一语言标签的 `:lang()`；`:placeholder-shown` 只覆盖省略 `type` 或 text-like `input`（text/search/url/tel/email/password）与 textarea 的空 live value、非空 placeholder 状态；空 placeholder、其他 input 类型、普通元素和带参数形式均不匹配；范围伪类只支持非空且受约束的 input number/range/date/month/week/time/datetime-local，空值、bad/type mismatch、disabled/readonly、无范围限制、非 input 和单独 stepMismatch 均不匹配；注册 interaction callback 后还可读取 Core 精确 active/hover 节点的 `:active`/`:hover`，不等于 CSS selector 引擎的完整语法。
- `:scope` 是同一 selector 子集中的有界 context 扩展：element query 的 receiver 作为 scope，带直接、无参数 `:scope` 的 selector 可以把 owner 放在结果首位，并支持 `:scope > ...`/`:scope ...` 的子代与后代关系；无 scope 的 element query 仍排除 owner，document query 以 `document.documentElement` 为 scope，`matches()`/`closest()` 以 receiver 为 scope。嵌套参数、伪元素和完整 Selectors 语法仍 fail closed。
- `:default` 是同一 selector 子集中的有界 form-state 扩展：checkbox/radio 只读取
  content `checked` 属性，option 读取 Core relation 45 的 default-selected 快照，
  submit-capable button/input/image 只匹配所属 form 中按文档顺序的第一个 submit control。
  live `.checked`/`selectedIndex` mutation 不会改写默认状态；relation 缺失、非支持元素、
  带参数、伪元素或尾随逗号仍 fail closed。TEST1181 通过多个短脚本 session 适配固定的
  730 KiB heap；这不代表完整 `:default` 选择器、native 默认按钮行为或视觉保证。
- `<option>` 的脚本 `selected`/`defaultSelected` 属性只在宿主注册
  `PBrowserScriptOptionCallbacks` 后可用。`selected` 通过 Core 按 id API 修改 live
  选择并遵守单选互斥/多选规则；`defaultSelected` 只修改 Core 默认基线，不改写 content
  attribute 或当前 live 选择。该可选表复用既有 Browser form-property native slot，
  缺失、非 option、无效 id 或 callback 错误均 fail closed；它不提供 native SELECT
  popup、键盘/触摸、SIP/IME、layout、paint 或完整 HTML option 算法。
- 同一 Browser DOM bridge 还提供可选的 `<option>` `value`、`label`、`text` 属性：
  `value`/`label` 在对应 attribute 存在时返回其 UTF-8 值，否则回退到 option 文本；
  `text` 读写 option 的纯文本，属性或文本 mutation 会被后续读取和所属 select 的
  live value 观察到，空 attribute 不等于缺失。该扩展复用通用 attribute/text callback，
  不增加 Core ABI 或 native slot；非 option、无效 id、缺失 callback 或失败 mutation
  均安全拒绝，也不实现 native SELECT popup、layout/paint、键盘/触摸或完整 HTML option
  算法。
- 同一 Browser DOM bridge 还提供可选的 `select.options`、`selectedOptions`、`length`
  和 `option.index`：集合按可寻址 id 的 DOM 顺序生成独立 HTMLCollection snapshot，
  `selectedOptions` 在读取时筛选 live selected 状态，`option.index` 包括 optgroup 内的
  option。每次 getter 都生成新集合，snapshot 数组的本地修改不会写回 DOM；遍历最多 256
  个节点并返回 64 个 option，缺少稳定 id 的元素不能被当前 wrapper 寻址。该扩展不实现
  完整 live collection、`length` setter、append/remove、option.form 的完整 owner 算法（当前
  只沿有界可寻址父链投影到所属 select）、
  native SELECT popup、键盘/触摸、SIP/IME、layout/paint 或不同 DPI 视觉。
- `select.type` 与 `optgroup.label` 现在有一个有界的 Browser metadata projection：`type`
  只由 live `multiple` attribute 决定并返回 `select-one`/`select-multiple`，不能通过 setter
  写入；`optgroup.label` 反映自身 attribute，缺失时为空字符串，`option.label` 的文本
  fallback 保持不变。该桥复用通用 attribute callback，不增加 ABI 或 native slot；它不
  实现 native SELECT popup、完整 option/group 算法、layout/paint 或平台输入行为。
- `:read-only`/`:read-write` 是同一 selector 子集中的有界编辑状态：文本输入类型与
  `textarea` 读取 readonly/effective-disabled，存在 Core `isContentEditable` callback
  时读取显式或祖先继承的 editing host；不支持编辑的 input 类型和普通元素按
  `:read-only` 处理。显式 contenteditable 在 callback 缺失或查询失败时两者都不匹配，
  不代表完整 CSS Selectors、富文本或 native 编辑行为。
- `details`/`summary`、`hidden` 等只有受限静态或交互子集；`dialog` 已有 Browser 脚本的 show/showModal/close/requestClose、returnValue、cancel/close 事件、活动 modal id、宿主驱动的 Escape 请求桥接和参考宿主的有界 backdrop 点击策略。Core/Browser 组合支持有 id 祖先 dialog 的显式、脚本和隐式 `method="dialog"` 提交，包括 validation、可取消 `submit`、submitter value 与直接 close；无 id、无祖先 dialog 或跨文档目标会 fail closed。宿主可以组合 Core 的 scoped focus snapshot 实现顺序 Tab/Shift+Tab 子树范围，并调用 `PCore_PaintDocumentWithModal` 得到实体色遮罩和指定 dialog 重绘；这不是 CSS `::backdrop`、透明合成或跨文档 top layer。Browser 不自动接管平台消息；宿主必须显式调用这些边界。
- Core 已支持有界的自定义 `tabindex` 顺序：正值升序（同值保持 DOM 顺序），随后是零/缺省组；`PCore_FocusTargetInfoWithin` 可按已知 DOM id 限定到一个祖先子树；负值、disabled/hidden/stale 目标和 file picker 仍会被排除。`PCore_AutofocusTargetInfo`/`PCore_InteractionFocusAutofocus` 现在允许宿主在 style/layout 与 native 子控件创建完成后，按 DOM 顺序选择第一个符合相同资格的 `autofocus` 目标；这只是一次显式、有界的事务，不是 Browser 自主生命周期，也不提供完整焦点导航、动态焦点区域、focus ring 或跨窗口焦点。目标必须有可用 layout，深度超限、无 id 或 id 超出 Browser 桥接容量时宿主应安全回退；无 id 目标的事件可通过 `PCore_EventDispatchFocus` 派发，但 Browser 的 `document.activeElement` 仍按 id projection 合同回退到 `document.body`。
- Core/Browser 对带 DOM `id` 的常见 block/replaced/flex overflow box 提供 retained scrollbar offset、`scrollLeft`/`scrollTop`、`scrollTo()`/`scrollBy()` 和宿主 pointer 同步；这只是有界的两个轴桥接，不能代表完整 CSS overflow 语义。client 尺寸是 retained scrollport 的 padding 区域，滚动条覆盖在边缘。
- Browser script session 已能由宿主显式维护顶层窗口 focus/blur 状态，并让 `document.hasFocus()` 与去重后的 window 事件保持一致；这不等于完整浏览器焦点策略，native 控件焦点、焦点矩形、焦点陷阱和 OEM/跨窗口激活仍由宿主负责。
- Core 的 `PCore_InteractionFocusElementId` 与 `PCore_InteractionStateElementId` 只报告当前交互状态中、带非空 UTF-8 id 的节点；没有对应状态、没有 id、节点过时、状态组合非法或缓冲不足时调用方必须按失败/回退处理。Browser 的 `document.activeElement` 是显式 callback 注册后才安装的可选 projection，通过现有 ID lookup 返回元素，否则返回 `document.body`；`:active`/`:hover` 另由显式 interaction callback 投影当前精确节点，注销或无效来源时安全不匹配。Browser 不自主执行初始 `autofocus`，宿主可在 layout/native 子控件创建后显式调用 Core 的有界入口；这仍不提供完整焦点算法、pointer capture、native 焦点矩形或跨窗口焦点。
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
- `labels`、form collections 和若干 NodeList 是静态 snapshot；支持的 form owner/form.elements
  关系现在识别带 `form="id"` 的 input、select、textarea、button，并按文档顺序纳入跨树
  控件；validation、submission/multipart、dialog/default-submit、reset 和按坐标的
  submit/reset 激活也复用这条 owner 规则；`PCore_FormResetById` 还提供按 form ID 的
  state-only 初值恢复；Core 入口本身不派发事件、不操作 native 控件或 layout。启用
  Browser 的 `PBrowserScriptFormResetCallbacks` 与 Ex form-event adapter 后，脚本
  `HTMLFormElement.reset()` 会先派发可取消 reset，再由宿主 callback 调 Core 并重新
  layout/paint；启用 `PBrowserScriptFormSubmitCallbacks` 后，脚本
  `HTMLFormElement.requestSubmit([submitter])` 按 validation→可取消 submit→默认动作
  顺序运行，Core by-id primitives 只准备 urlencoded/multipart/dialog 结果，宿主决定
  导航或 close；另行启用 `PBrowserScriptFormSubmitDirectCallbacks` 后，
  `HTMLFormElement.submit()` 跳过 validation、submit 事件和 submitter 选择，调用 Core
  的 NoValidationById primitive，再由宿主决定导航或 close。两条脚本提交方法都要求有
  可寻址的 form id，并在 callback 缺失、目标非法或容量不足时 fail closed。文档 mutation
  后调用方仍应重新查询。
  Browser 的 `new FormData(form[, submitter])` 另有独立的 detached snapshot：无显式
  submitter 时使用旧 callback，带第二参数时使用 Ex callback，并复用 successful-control
  与 form-owner 规则；最多返回 64 项，名称最多 64 字节，字符串值最多 128 字节，文件名
  和 MIME 类型各最多 64 字节。非空 submitter 必须是启用且归属于目标 form 的 submit-type
  input/button；普通、禁用、跨 form、伪造对象、无 id、超限或缺少 callback 均安全失败。
  构造成功后 Browser 在 form 上同步派发非冒泡、不可取消的 `formdata` 事件，事件的
  `formData` 指向正在返回的对象；监听器可在构造返回前修改字段，`form.onformdata`
  也可用。它不触发 validation、submit/reset 事件、默认动作或导航。文件只返回 filename/type
  和空内容，不暴露 picker 路径；完整 live
  HTMLFormControlsCollection、文件读取和所有其他 form-associated 元素、fieldset/object/
  image 归属及浏览器完整表单树规则仍未实现。
- 事件系统覆盖常用 capture/target/bubble、取消和默认动作，但不支持所有 DOM Event 子类、pointer/touch/drag/drop/clipboard 或浏览器手势。宿主对单元素 `contenteditable` 另有受限 `CF_UNICODETEXT` paste/cut/copy 接线：非空选区才复制，折叠选区保持剪贴板不变，超长或非 Unicode 格式在 native mutation 前拒绝；它不是通用 DOM ClipboardEvent 或 async clipboard API。
- native 控件状态由 Core、Browser 和宿主共同提交；回调错误、stale token 或几何变化会 fail closed，可能表现为本次默认动作不执行。

## JavaScript

- 浏览器 JavaScript 默认关闭，启用后仍是实验性的有界 classic-script 组合。
- 独立 script 和浏览器 script 共用 Duktape 2.7.0，不存在第二套引擎；两者提供的 host objects 与生命周期不同。
- 不支持 ES module、dynamic import、WebAssembly、worker、service worker 或完整现代 ECMAScript host environment。
- Browser bootstrap 只暴露当前已接线的 DOM/Event/form/navigation/timer 子集；缺失 API 通常 fail closed 或为 `undefined`。
- Browser 的 `matches()`、`closest()`、`querySelector()` 和 `querySelectorAll()` 支持有界 selector 列表与关系组合器：标签、`#id`、`.class`、存在属性和属性值的 `=`, `^=`, `$=`, `*=`, `~=`, `|=` 匹配可通过空格、`>`、`+`、`~` 连接；组合链及祖先/兄弟遍历各自最多 64 步。属性值中的引号、空格、逗号和引号内的 `]` 会被保留，空操作数、未闭合引号、非法或过深 selector fail closed。结构伪类只限 `:root`、`:empty`、child/of-type 与四种 `nth-*` 变体；表单状态伪类只限 input/option 的实时 `:checked`、通过 Core effective-disabled relation 得到的 input/button/select/textarea/option/optgroup `:disabled`/`:enabled`（fieldset 自身回退到直接属性）、直接 `required` 属性对应的 `:required`/`:optional`，以及 `form`、input、select、textarea 通过 validation callback 得到的 `:valid`/`:invalid`；焦点状态只限通过 activeElement callback 获取当前焦点的 `:focus`/`:focus-within`；链接状态包括带 `href` 属性的 `<a>`/`<area>` 的静态 `:link`/`:any-link`（空值也算带属性），以及在宿主注册 `PBrowserScriptInteractionCallbacksEx` 后由宿主明确批准的 `:visited`；`:visited` 只收到元素 id 与原始 href，Browser 不保存或推断 history，宿主负责 URL 解析、历史来源和隐私策略，callback 缺失、失败、无效输入或超长值均安全不匹配；`:target` 只在当前 URL fragment 解码后等于元素当前非空 `id` 时匹配，无 fragment、malformed percent-encoding、仅有 `name` 的 named anchor 或 stale wrapper 都安全不匹配；`:not()` 只接受单一简单 compound 参数，`:is()`/`:where()` 只接受最多 16 个逗号分隔的简单 compound 分支，`:has()` 只接受最多 16 个相对简单 compound 分支，且每个后代/兄弟遍历最多 64 步；`:active`/`:hover` 仅在宿主注册 interaction callback 并返回当前 Core 状态的精确 id 时匹配。伪元素、namespace、shadow DOM、属性大小写修饰符和完整 CSS Selectors 语法仍未实现。`:has()` 的链式相对 selector、完整分支语法和更深遍历仍未实现；`:target` 不拥有 fragment reveal 或页面滚动，真实页面视觉仍需宿主验收。
- `:lang()` 是同一 selector 子集中的有界扩展：只接受单一 ASCII 语言标签，沿最多 64 层 `parentElement` 读取继承语言，`lang` 优先于 `xml:lang`，按大小写不敏感的精确值或 `-` 子标签前缀匹配；空值、非法参数、语言标签列表和引号形式 fail closed。该实现不代表完整 BCP 47 解析或 namespace 语言规则。
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
 - script heap、native function、module/source、timer、queue 和执行时间都有固定预算；复杂页面可能因资源上限失败。独立 `positron_script.dll` context 默认 512 KiB，Browser bootstrap 使用 730 KiB 的独立有界堆上限；`PSCRIPT_MAX_NATIVE_FUNCTIONS` 当前为 28。Browser 同时启用 DOM、validation、contenteditable、导航、`document.activeElement`、`HTMLElement.focus()`/`blur()`、pointer-interaction selector 和 FormData 桥时会占满槽位，额外宿主 native function 必须先检查计数并在达到上限时保守失败；参考宿主为大型完整页面 bootstrap 使用默认脚本页预算的 4 倍，较小离线夹具仍可使用更低预算，但所有 page budget 都有上限且不改变 Browser 的固定 heap/native-function/source 预算；不能通过跳过必要桥或扩大为无界表来规避预算。
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
  blur 不执行滚动。该桥不提供完整 focus navigation、自主自动初始焦点、focus ring、
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
- 设备门在部署前分开查询 `-RemoteBase` 目标卷和内部 object store：目标卷要求 staging
  总大小加 1 MiB 余量；外部目标的内部 object-store 只以 64 KiB 作为告警线，不把粗粒度
  内部数字误报成目标卷不足，已知内部路径才把 object store 当作硬性容量门。旧 RAPI
  没有路径级导出时，外部 `\Storage Card` 会 fail closed；OEM/WMDC 实际缓存需求仍可能
  超出告警线。正常清理要求 `test_host.log` 完整复制两次且终态标记稳定；若目标卷或已知
  内部 object store 确认不足，设备门会对门自己生成且非当前运行的旧目录做一次有界应急
  回收，先尽力保存日志，必要时允许删除终态不完整的旧目录以释放空间。未知目录、当前
  目录、删除失败的目录仍保留；应急回收后的复检仍不足才阻断部署。
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
- TEST1151 覆盖页面提交后由宿主显式触发的有界 `autofocus`：Core 按 DOM 顺序跳过
  hidden、disabled 和未布局目标，提供 UTF-8 id size-probe 与 geometry 快照，并只更新
  Core focus node；有 id 目标复用 Browser focus bridge，无 id 目标通过
  `PCore_EventDispatchFocus` 保留焦点节点并派发 focus/focusin。它同时检查重复应用不
  重复派发、过小 id buffer 不部分写入，以及无 id 目标的 `document.activeElement` 按
  id projection 合同回退到 `body`；不证明完整焦点算法、native HWND/焦点矩形或 OEM 视觉。
- TEST1152 覆盖 Browser selector 的有界列表和关系组合器：`matches()`、`closest()`、
  `querySelector()` 与 `querySelectorAll()` 对顶层逗号、后代/子代/相邻兄弟/一般兄弟
  保持一致，属性值中的逗号不会误拆分，非法或过深输入 fail closed。
- TEST1153 覆盖 Browser selector 的属性匹配操作符：`=`, `^=`, `$=`, `*=`, `~=`, `|=`
  在简单 compound、通配标签、组合器和顶层列表中按有界规则匹配；引号内空格、逗号和
  `]` 会被保留，空操作数、未闭合引号、未支持的大小写修饰符和其他非法输入安全拒绝。
  真实页面的完整 CSS selector、动态伪类/伪元素、属性大小写修饰符、namespace、shadow
  DOM、布局视觉和不同 DPI 仍属于宿主集成观察。
- TEST1154 覆盖 Browser selector 的有限结构伪类：`:root`、`:empty`、child/of-type
  变体和四种 `nth-*` 变体；支持整数、`odd`/`even` 和受限 `an+b` 公式，并确认空公式、
  `of` 过滤、伪元素和超大数值 fail closed。判断使用只读 childNodes/关系快照，
  仍受 64 步、公式系数和 730 KiB Browser heap 上限约束；完整动态状态、伪元素、namespace、
  shadow DOM 和 CSS Selectors 语法不在保证范围内。
- TEST1155 覆盖 Browser selector 的有限表单状态：`input:checked` 读取现有 checked
  callback 的当前值，`:disabled`/`:enabled` 按 input、button、select、textarea、option
  的直接 `disabled` 属性匹配，`:required`/`:optional` 按 input、select、textarea 的
  直接 `required` 属性匹配。夹具验证 `matches()`、`closest()`、两种 query、状态 mutation
  后的实时结果、列表顺序和不支持输入的 fail-closed 行为；fieldset/optgroup 的 effective
  继承由 TEST1166 覆盖，option 的动态 selected→`:checked` 映射由 TEST1157 覆盖。
- TEST1157 覆盖 Browser selector 对 option live selected 状态的 `:checked` 映射：单选初始
  选择、`selectedIndex` mutation、多选初始选择、matches/closest、列表顺序和非法输入
  fail closed；真实 native SELECT 的 popup、键盘、SIP/IME、触摸和视觉仍属于宿主观察。
- TEST1158 覆盖 Browser selector 对 Core validation 状态的 `:valid`/`:invalid` 映射：
  required 空值、value/custom validity mutation、form 聚合、willValidate 非候选排除和
  非法输入 fail closed；真实 validation UI、提示本地化、键盘/SIP/IME 和视觉仍属于宿主观察。
- TEST1159 覆盖 Browser selector 对焦点状态的 `:focus`/`:focus-within` 映射：
  activeElement 初始空值、焦点切换、blur 清理、祖先范围、两种 query 以及带参数、伪元素
  和注销 callback 的 fail-closed 行为；真实 native focus、焦点矩形、键盘/SIP/IME、触摸
  和视觉仍属于宿主观察。
- TEST1160 覆盖 Browser selector 对静态链接状态的 `:link`/`:any-link` 映射：带 `href`
  属性的 `<a>`/`<area>`（包括空值）匹配、移除/新增属性后的实时查询和列表顺序；
  真实链接绘制、鼠标 hover/active 和导航仍属于宿主观察。
- TEST1161 覆盖 Browser selector 对有界 `:target` 的映射：当前 URL fragment 解码后
  与元素当前非空 `id` 相等时，`matches()`、`closest()`、两种 query 和列表顺序保持
  一致；fragment 导航、百分号编码、id mutation、无 fragment、malformed encoding、
  仅有 `name` 的 named anchor、带参数、伪元素和尾随逗号的输入分别验证匹配或
  fail-closed。它不证明 fragment reveal、视觉滚动、完整 selector 语法或 stale
  wrapper 的可变身份。
- TEST1162 覆盖 Browser selector 对有界 `:lang()` 的映射：当前元素及最多 64 层
  `parentElement` 父链的 `lang`/`xml:lang` 继承、大小写不敏感的语言前缀、`lang` 优先
  与 XML fallback、空值停止继承和属性 mutation 由 `matches()`、`closest()`、两种
  query 共同断言；空参数、列表、引号形式、伪元素和尾随逗号必须 fail closed。它不
  代表完整 BCP 47 解析、namespace 语言继承或真实页面视觉。
- TEST1163 覆盖 Browser selector 对有界 `:is()`/`:where()` 正向分组的映射：最多 16 个
  逗号分隔的简单 compound 分支在 `matches()`、`closest()`、两种 query 中保持一致，
  属性/类 mutation 会实时更新；空分支、嵌套伪类、组合器、伪元素、未闭合和尾随逗号
  等输入安全 fail closed。它不代表完整 Selectors、specificity 计算或真实页面视觉。
- TEST1164 覆盖 Browser selector 对有界 `:has()` 相对分支的映射：最多 16 个简单
  compound 分支支持后代、直接子代、相邻兄弟和后续兄弟关系，后代/兄弟遍历最多 64 步；
  `matches()`、`closest()`、两种 query、属性/类 mutation、表单状态和查询顺序保持一致。
  空分支、链式关系、伪元素、未闭合或尾随逗号等输入安全 fail closed。它不代表完整
  Selectors `:has()`、任意相对 selector、shadow DOM 或真实页面视觉。
- TEST1165 覆盖 Core pointer interaction 到 Browser selector 的可选映射：
  `PCore_InteractionStateElementId` 通过 interaction callback 提供当前 active/hover
  id，`matches()`、`closest()`、两种 query 只匹配精确节点；命中后的状态切换、size-
  probe、过小缓冲、非法伪类参数和 callback 注销都必须 fail closed。该能力不派发
  pointer 事件、不自动重做 style/layout/paint，不提供 pointer capture 或真实触摸/视觉
  保证。
- TEST1166 覆盖 Core effective-disabled relation 到 Browser selector 与表单提交的统一：
  disabled fieldset 的 first-legend exemption、disabled optgroup 对 option 的继承、
  fieldset/optgroup mutation、matches/query、关系 size-probe、disabled option 的选择
  拒绝和 successful form submission 排除均由同一离线 fixture 断言。旧宿主缺少 relation 44
  时 Browser 回退到直接属性；native SELECT popup、触摸和视觉仍需宿主验收。
- TEST1167 是离线的 Core/Browser range selector 夹具，无新增立即人工风险；原生范围控件
  视觉、本地化 validation UI、触摸和不同 DPI 仍需宿主观察，自动门证明范围状态映射、
  约束 mutation、查询顺序和非法输入回退。
- TEST1168 是离线的 Core/Browser editable selector 夹具，无新增立即人工风险；自动门证明
  `:read-only`/`:read-write` 对文本控件、readonly/effective-disabled、contenteditable
  祖先继承、属性 mutation、query/closest 和 callback 注销的有界映射。真实 native 编辑、
  SIP/IME、富文本、视觉和不同 DPI 仍进入累计人工清单。
- TEST1169 是离线的 Core/Browser placeholder selector 夹具，无新增立即人工风险；自动门证明
  `:placeholder-shown` 对 text-like input/textarea 的空 value、非空 placeholder、value/type/
  placeholder mutation、matches/closest/query 顺序和非法输入的有界映射。真实 native
  placeholder 绘制、SIP/IME、触摸、视觉和不同 DPI 仍进入累计人工清单。
- TEST1170 是离线的 Core/Browser form-owner 夹具，无新增立即人工风险；自动门证明最近
  祖先与显式 `form="id"` 归属、空值/无效目标不回退、跨树 `form.elements` 文档顺序、
  namedItem、label association、mutation 后重查和旧 snapshot 保持。真实 native 表单
  控件、SIP/IME、picker、触摸、视觉和不同 DPI 仍进入累计人工清单。
- TEST1171 是离线的 Core form-owner 生命周期夹具，无新增立即人工风险；自动门证明
  form 外的显式控件参与 validation、`reportValidity()` invalid-event 扫描、urlencoded
  successful-control submission 和外部 submit/reset activation，reset 后初始值恢复且
  required invalid 再次出现。multipart 以同一 owner 实现为基础，但本夹具不承诺 native
  表单视觉、SIP/IME、picker、触摸或不同 DPI 结果。
- TEST1172 是离线的 Core 按 form ID state-only reset 夹具，无新增立即人工风险；自动门
  证明 `PCore_FormResetById` 恢复 form 子树与显式外部 input/checkbox/select/textarea，
  无效 owner 保持原值，缺失/非 form/空值/NULL 参数安全拒绝。该 API 不派发 reset 事件、
  不创建 native 控件或触发 layout；这些仍由 Browser/宿主按事务顺序完成。
- TEST1173 是离线的 Browser/Core 脚本 form-reset 夹具，无新增立即人工风险；自动门证明
  `HTMLFormElement.reset()` 先派发按 id 的可冒泡、可取消 reset，取消会阻止 Core 默认
  动作，允许后由宿主 callback 调 `PCore_FormResetById` 并完成 re-layout，且
  `target`/`currentTarget` 身份、调用次数和 `undefined` 返回值保持合同。缺少任一
  reset/form-event adapter、无效 form id 或 callback 失败时仍 fail closed；native 表单
  视觉、SIP/IME、picker、触摸和不同 DPI 仍需人工验收。
- TEST1174 是离线的 Browser/Core 脚本 request-submit 夹具，无新增立即人工风险；自动门证明
  `HTMLFormElement.requestSubmit([submitter])` 的 validation→可取消 submit→默认动作顺序，
  `novalidate`/`formnovalidate`、无 submitter successful-control 序列、POST action/body、
  取消和非法目标的 fail-closed 边界。Core 的 by-id primitives 不派发事件或执行导航，
  `test_host` 只接线和断言；native 表单视觉、SIP/IME、picker、触摸和不同 DPI 仍需人工
  验收。
- TEST1175 是离线的 Browser/Core 脚本 direct-submit 夹具，无新增立即人工风险；自动门证明
  `HTMLFormElement.submit()` 跳过 validation、submit 事件和 submitter，仍经 Core
  NoValidationById primitive 生成 urlencoded、dialog、multipart 结果，并在无效目标或
  callback 缺失时 fail closed。它要求有 id 的 form；初始 inline 阶段的 multipart 网络
  动作以及 native 表单视觉、SIP/IME、picker、触摸和不同 DPI 仍未实现或需人工验收。
- TEST1176 是离线的 Browser/Core FormData snapshot 夹具，无新增立即人工风险；自动门证明
  `new FormData(form)` 对成功控件、显式 form owner、重复 select、disabled/unnamed/submit
  排除、detached mutation 和无 submit 事件的有界映射。TEST1177 继续覆盖 Ex bridge 的
  enabled submitter、外部 owner、跨 form/禁用/非元素拒绝和无 submit 事件。两项只支持
  有 id 的 form、64 项和受限字段容量，文件只保留 metadata；完整 live collection、文件
  读取、native 表单视觉、SIP/IME、picker、触摸和不同 DPI 仍未实现或需人工验收。
- TEST1178 是离线的 Browser FormData `formdata` 事件夹具，无新增立即人工风险；自动门
  证明事件同步派发、`FormDataEvent.formData` 身份、监听器及 `onformdata` mutation、
  非冒泡/不可取消和 submit 无副作用。它不扩展文件内容、完整 live collection 或其他
  form-associated 元素，真实 native 表单视觉、SIP/IME、picker、触摸和不同 DPI 仍需人工
  验收。
- TEST1179 是离线的 Browser selector `:visited` 夹具，无新增立即人工风险；自动门证明
  `PBrowserScriptInteractionCallbacksEx` 的宿主批准结果、`<a>`/`<area>` 的绝对/相对
  href、fragment/空 href、matches/closest/query、href mutation、列表顺序以及注销或
  非法输入时的 fail-closed。Browser 不存储或修改 history，宿主只负责 URL 解析、历史
  来源和隐私策略；真实链接样式、跨窗口 history、触摸和视觉仍需人工验收。
- TEST1180 是离线的 Browser selector `:scope` 夹具，无新增立即人工风险；自动门证明
  element/document query 的 scope owner 规则、子代/后代关系、文档顺序、matches/closest
  receiver scope、无 scope 时的 owner 排除、大小写形式以及参数/伪元素/尾随逗号的
  fail-closed。Browser 不保存 scope 状态，宿主只提供既有 DOM relation callback；真实
  页面完整 Selectors、视觉和不同 DPI 仍需人工验收。
- TEST1181 是离线的 Browser selector `:default` 夹具，无新增立即人工风险；自动门证明
  默认 checked 控件、Core relation 45 的 option default-selected、form 首个 submit
  control、查询顺序、live state mutation、matches/closest 和非法输入的 fail-closed。
  多个短脚本 session 只为适配固定 730 KiB heap；真实 native 默认按钮行为、表单视觉、
  触摸、SIP/IME 和不同 DPI 仍需人工验收。
- TEST1182 是离线的 Browser/Core option property 夹具，无新增立即人工风险；自动门证明
  `selected`/`defaultSelected` getter/setter、单选互斥、多选独立选择、`selectedIndex`
  一致性、默认基线与 live 状态分离，以及非 option/无效 id/缺失 callback 的 fail-closed。
  真实 native SELECT popup、键盘/触摸、SIP/IME、视觉和不同 DPI 仍需人工验收。
- TEST1183 是离线的 Browser option 基础属性夹具，无新增立即人工风险；自动门证明
  `value`/`label` 的属性优先与文本 fallback、`text` mutation、select live value/
  selectedIndex 联动、空属性区分以及非 option setter 的 fail-closed。该桥复用既有
  Core DOM attribute/text callback，不提供 native SELECT popup、键盘/触摸、SIP/IME、
  layout、paint 或不同 DPI 保证。
- TEST1184 是离线的 Browser select/option collection 夹具，无新增立即人工风险；自动门
  证明 `options`/`selectedOptions` 的文档顺序、`item()`/`namedItem()`、selected mutation、
  `select.length`、`option.index` 和 snapshot 隔离，并确认 optgroup 不进入 collection、
  非 select/option 目标安全返回。该门只覆盖有稳定 id 的可寻址元素和 64 个 option/256
  个遍历节点预算，不代表完整 live HTMLCollection、length setter、append/remove、
  option.form 的完整 owner 算法、native SELECT popup、键盘/触摸、SIP/IME 或视觉保证。
- TEST1185 是离线的 Browser option.form owner 夹具，无新增立即人工风险；自动门证明
  optgroup 父链、显式 `select form="id"`、form attribute mutation、无 owner/无效 owner
  的 `null` 回退和既有 input owner 不变。实现只复用现有 DOM relation、form owner 与
  attribute mutation，不增加 ABI；64 层父链预算、无 id 元素不可寻址、完整 HTML
  option/form-owner 算法、native SELECT popup、键盘/触摸、SIP/IME 或视觉保证仍未实现。
- TEST1186 是离线的 Browser select/optgroup metadata 夹具，无新增立即人工风险；自动门
  证明 `select.type` 对 `multiple` attribute 的 live `select-one`/`select-multiple` 映射、
  只读 setter、`optgroup.label` 的显式值/缺失回退/attribute mutation，以及 option label
  fallback 和非目标 fail-closed。该扩展不增加 ABI，不提供 native SELECT popup、键盘/触摸、
  SIP/IME、layout/paint 或不同 DPI 视觉保证。
- TEST1156 覆盖 Browser selector 的有限 `:not()`：只接受一个不含伪类、伪元素、列表或
  组合器的简单 compound（标签、`#id`、`.class`、属性存在或精确 `=` 值）。`matches()`、
  `closest()`、两种 query、mutation、组合/列表顺序和 `details:not([open])` 等实际场景由
  夹具验证；空参数、嵌套伪类、组合器、列表和非精确属性操作符安全 fail closed。
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

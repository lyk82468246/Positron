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
- HTTP 只覆盖有界 HTTP/1.1，不提供 HTTP/2/3、连接池、完整缓存、cookie jar 或浏览器代理；Browser `document.cookie` 仅为 session 的有界内存状态。
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
  1.5 MiB Browser heap；这不代表完整 `:default` 选择器、native 默认按钮行为或视觉保证。
- `<option>` 的脚本 `selected`/`defaultSelected` 属性只在宿主注册
  `PBrowserScriptOptionCallbacks` 后可用。`selected` 通过 Core 按 id API 修改 live
  选择并遵守单选互斥/多选规则；`defaultSelected` 只修改 Core 默认基线，不改写 content
  attribute 或当前 live 选择。该可选表复用既有 Browser form-property native slot，
  缺失、非 option、无效 id 或 callback 错误均 fail closed；这个 property bridge 本身不
  实现 native SELECT popup 或平台输入编排；有限的关闭态单选键盘桥另见下文。
- Native SELECT 平台输入仅有一条有界路径：关闭态单选 COMBOBOX 可通过
  `PCore_EventDispatchKeyExToSelectIndex` 把 `keydown`/`keyup` metadata 送到 live DOM，
  允许默认动作后同步 Core/native selection；listener 使 retained layout 失效时仍可用。
  展开 popup、触摸、SIP/IME、OEM 自动重复、跨设备焦点及视觉/DPI 未覆盖；不等于完整
  select 键盘行为。
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
- `fieldset.type`、`fieldset.form` 与 `fieldset.elements` 现在也有一个有界 Browser/Core
  projection：`type` 固定为只读 `fieldset`，`form` 使用 Core FORM_OWNER 的祖先/显式
  `form="id"` 规则，`elements` 从 fieldset 子树按 DOM 顺序生成独立 HTMLCollection
  snapshot，包含带稳定 id 的 listed input/select/textarea/button/object/output（含嵌套 fieldset）。
  每次最多遍历 256 个节点、返回 64 项；`form.elements` 同样包含有 id 的
  fieldset/object/output。`img.form` 复用同一 owner 关系，但 img 不进入任一集合。该桥不
  覆盖其他 listed elements、无 id 节点、完整 live collection 或 append/remove，也不改变
  fieldset/object/output 在 successful-control 中的排除。
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
- `HTMLImageElement` 目前只有有界的属性/资源状态 bridge：`alt`、raw `src`/`srcset`/
  `sizes`/`useMap`、`crossOrigin`、`isMap`、`controls`、`width`/`height`、
  `referrerPolicy`、`decoding`、`loading`、`fetchPriority`、`currentSrc` 以及
  `naturalWidth`/`naturalHeight`/`complete`。Core relation 查询不 fetch、decode 或 layout；
  无 source 的图片 complete 且自然尺寸为 0，无法选出候选且没有 `src` 的非空 `srcset`
  保持 incomplete，成功资源要等 retained decode attempt，终态 fetch failure 则 complete
  且尺寸为 0。当前 source 选择最多接受 16 个、每个最多 2047 字节的同类候选：正密度
  （`x`）按当前 Core viewport DPI 选择，正宽度（`w`）按 `sizes` 源尺寸与 DPI 选择；
  `sizes` 仅接受 px/vw/vh 以及单一 `(min-width|max-width: <length>)` 条件，缺失或不支持
  时按 100vw。`<picture>` 另在最多 16 层祖先、8 个 preceding `<source>` 和 64 个
  direct-child 节点内按 document order 过滤有界 `media`/`type`，然后复用每个 source
  的 `srcset`/`sizes` 选择；不合格 source 才回到 img 的候选。WM6 libdom 对省略 source
  结束标签的兼容路径只保留这些上限。混合/畸形候选、未知 MIME、超长 URL 和不支持的
  URL 语法无法安全选择时回退 raw `src` 或保持空值。完整媒体查询、loading/fetch-priority
  策略、绝对 URL、CORS/referrer enforcement 或 native 图像视觉仍未实现。Core 只提供有界的
  image-map 命中：已布局图片最多解析 64
  个 `<area>`、64 个坐标，并支持 `default`、`rect`、`circle` 和 `poly`/`polygon`；自然
  坐标按渲染尺寸缩放，坏坐标、未知形状、`nohref`、空区域和超限输入安全忽略。该路径
  不实现 transforms、复杂图像生命周期、pointer/touch 手势或完整 HTML image-map 算法。
  Browser 的 `decode()` 只提供有界的
  Promise 生命周期：无 source/已知正尺寸在 microtask 中完成，终态失败和 source
  mutation 以 `EncodingError` 拒绝，page teardown 对剩余请求以 `AbortError` 拒绝；每个
  session 最多 64 个 pending 请求。宿主在 Core relation 更新后通过
  `PBrowser_ScriptSessionNotifyImageEvent` 显式派发 `load`/`error`；事件 trusted、非冒泡、
  不可取消，重复同终态幂等，过时/相反/未就绪通知 fail closed。该桥不自动抓取、选择、
  解码或绘制图像；每个 session 的 image/source 终态映射最多 64 项，source 改变会释放
  旧项；支持的 `Element.id` setter 改名也会释放旧 key，避免改名后耗尽终态预算；超限
  的新终态通知保持 fail closed。generation-aware 的 Ex 入口接受宿主递增的非零替换
  generation；一旦某个 image/source key 建立 generation，缺失 generation 的旧入口、
  较低或不匹配的 Ex 通知均 fail closed。source Ex 入口只在有界关系能解析最近
  `<picture>` 与带 id 的 `<img>` 时建立 generation；匿名或不可寻址关系直接拒绝，宿主
  应改为通知该 img。generation map 与终态 map 都受每 session 64 项预算约束。脚本写入
  img 的 `src`/`srcset`/`sizes` 或
  picture source 的 `media`/`type`/`srcset`/`sizes` 时，宿主可在 DOM attribute callbacks
  之后注册可选的 `PBrowser_ScriptSessionRegisterImageSourceCallbacks`，同步取得借用的
  id/kind/attribute/removed 元数据；该桥复用既有 native slot，不执行自动 I/O、选择、
  layout 或 paint。未注册时 mutation 不回滚；通用动态 DOM 插入/删除（仅另有有界
  direct-element 与 direct-Text removal）、完整 image loading
  和宿主 replacement pipeline 仍不在 Browser 契约内，callback 也不得重入或销毁 session。

## DOM、表单与事件

- 通用 namespace 创建/树语义、observer、range、shadow DOM 及大部分 IDL reflection 不支持；
  仅列出的 HTML/XML 查询可用。
- Browser/Core 只支持有界 DOM mutation：`textContent`/非编辑 `innerText`、CharacterData
  setter/mutator、`Text.splitText()`/`wholeText`/`replaceWholeText()`、`Node.normalize()`，
  Ex2/Ex3 的 Text/Comment/CDATA direct-child removal，以及 Ex4–Ex13 的 existing-element/
  CharacterData insertion、replacement 和 relative text。Ex10 按未过滤 `childNodes` 索引移动
  现有 Text/Comment/CDATA，Ex11 用另一个现有 CharacterData 替换 direct child，Ex12 用已
  连接的 CharacterData 替换 direct element child；next794 让 CharacterData 的 relative
  `before()`/`after()` 和单节点 `replaceWith()` 复用这两条既有路径。上述路径支持同父、跨父
  和 wrapper owner 更新。成功 mutation 使 layout 失效，UTF-16/UTF-8、detached、结构 token、
  错误 parent/reference、对象、节点和超限输入均 fail closed。Ex6 的 `append()`/`prepend()`
  （零至四值）创建 primitive Text 或移动 element；Ex7–Ex9 的 primitive `replaceWith()`
  遵循各自 callback 合同。Browser 提供两种 `DocumentFragment` staging：最多四个
  primitive Text 的 text-only 路径，以及最多四个 detached Element/Text/Comment/CDATA 根的
  结构路径。Element 须有唯一非空 id、至多一个 direct Text；Comment/CDATA 复用 Core callback。
  Fragment 自身支持单一 Fragment 参数的 `append()`、`prepend()`、
  `insertBefore()`、`appendChild()`、`replaceChildren()`，成功保留 identity 并清空源。
  `cloneNode(false/true)`
  仅复制 detached bounded 根，源/副本隔离，连接前修复 id；Fragment-owned
  CharacterData data 只更新 detached 快照。`getElementById()` 只在最多四个 staged 根中查找
  Element，忽略非 Element。`querySelector()`/`querySelectorAll()` 复用有界 parser，按序扫描最多四个
  Element 根，返回首个匹配或静态 NodeList；根不嵌套，空、超长或无匹配返回
  `null`/空列表。
  `children` 是缓存 `[SameObject]` HTMLCollection，随根/id/name mutation 更新；名项只读非枚举，忽略 Text。
  `Element.getElementsByTagName()` 是当前唯一的 bounded live collection：每次调用返回新
  `HTMLCollection`，已连接 owner 的同一对象在子树或 id/name mutation 后按需刷新，最多访问
  256 个节点并返回 64 项；索引、`item()`、`namedItem()`、`forEach()`、keys/values/entries
  和默认 iterator 都读取这份有界结果，Browser-created wrapper 也保持 identity。刷新超限
  时保留上一次成功结果，detached owner 返回空集合。`getElementsByClassName()` 与
  `getElementsByTagNameNS()` 仍是 bounded snapshot，按既有 tag/class 或 namespace/localName
  规则查找；NS 承诺 HTML namespace、通配符、大小写 localName 与 null/未知 fail closed。
  detached `normalize()` 限 64 个 direct Text/CDATA 或四个 Fragment 根，删除空并合并相邻
  Text/CDATA；live Element 的同一有界 Core 路径也合并 Text/CDATA，并在保留 Browser-created
  wrapper 时同步其数据；detached
  Element 的 `replaceChildren()`（0–4 项）、`replaceChild()`（单项）和 `textContent` 均为
  有界原子 staging，保留 childNodes/owner；它们接受直接 Text、Comment、CDATA（以及按
  既有规则转换的 primitive），`textContent` 仍只生成 Text 并排除 Comment。attached
  `textContent` 同步重建 Text wrapper；detached Element HTML 只做属性/direct Text
  escaping，纯文本 `innerHTML` 复用 textContent staging；markup、超长和 detached
  `outerHTML` setter 在 mutation 前拒绝。
  Fragment `textContent` getter 排除 Comment；setter 预检 65,535 字符，失败保留旧树/集合。
  Browser 预检 relations/namespace/replace/组合；Fragment Element 与 CharacterData
  的 sibling/element-sibling getter 按 staging/live 提供，未归属为 `null`。
  `replaceChild()` 展开四根并清空源；不支持输入拒绝。
- `document.createTextNode(value)` 提供 detached Text 快照；插入 live Element 后
  保留 wrapper identity，并支持 `insertBefore()`、`appendChild()`、有界 `append()`/`prepend()`、
  CharacterData mutator、`wholeText`、`splitText()`、`replaceWholeText()`、`remove()`、
  `cloneNode()` 和 1–4 个 primitive 的 `before()`/`after()`/`replaceWith()`。UTF-16 code-unit
  offset/count 校验；detached 更新快照，live regular Element 复用 Core callback；detached
  relative inert，Fragment/未物化 staged Element relative inert；已物化 Browser-created Element
  仅接受 1–4 个 primitive 文本参数的 `before()`/`after()`/`replaceWith()`，复用既有 Core
  callback，替换成功后 wrapper 回到 detached。Element、Fragment、CharacterData 或其他对象参数
  fail closed。64 个 direct child、65,535 个
  脚本字符；generic Node、复杂 fragment 和其他动态树语义仍 fail closed。
- `document.createElement(tag)` 是 Browser-owned 的有界 detached staging：标签只接受小写化
  ASCII `[a-z][a-z0-9-]*`（最多 32 个 UTF-8 字节），物化前每个 Element 须有唯一非空 id。
  每个 wrapper 最多 64 个 attribute（值最多 65,535 个脚本字符）和 64 个直接 child；直接
  child 可以是 Text、Comment、CDATA 或另一个 Browser-created Element。嵌套图的深度最多 4
  层、总 Element 数最多 64、每个 Element 的 child 数最多 64；重复/缺失 id、循环、连接中的
  source、混入不支持的节点或超出预算都在 Core 触碰前 fail closed。Ex11/新 nested callback
  通过 `__pcoreSetText` 递归创建 Core 节点；attach、remove/reinsert、`cloneNode(true)`、
  递归 `textContent` 与 owner/alias identity 都在 Browser 侧保持一致，不新增 Core fragment ABI。
  `cloneNode(false/true)` 的属性、字符数据和 nested Element 子树与源隔离，连接前仍须修复 id。
  nested Element 可作为有界 Fragment 之外的 detached graph 使用；Fragment 内的 Element 仍只
  接受既有的 direct Text 形状，nested DocumentFragment、任意 detached parser、事件/资源/
  observer 和其他动态树语义不创建。关系、物化、同父排序和移除后的 `childNodes`、首尾 child、
  `hasChildNodes()` 跟随 staging；detached staging 的 `children`/`childElementCount` 只反映
  直接 Element child。直接 CharacterData 的 `normalize()` 以 Comment/Element 为边界合并
  Text/CDATA；`attributes` 是稳定的 bounded `NamedNodeMap`；`get/set/removeAttributeNode*`、
  value/namespace/iteration 和跨 owner value-copy 共用 facade。`style.cssText` 与反射 setter
  在 detached/removed 状态暂存，物化后走 Core；宿主负责 layout/paint，视觉/触摸/SIP 不由
  该门保证。已物化 wrapper 的 `children` 现在从同步后的 direct-child snapshot 生成有界
  HTMLCollection，并提供 `item()`/`namedItem()`、`childElementCount`、
  `firstElementChild` 和 `lastElementChild`；collection 也不是完整 live collection。已物化
  wrapper 的 `innerHTML` setter 复用 Core parser 后，会原地保留 `childNodes` collection
  identity、刷新子节点快照并清理旧 child owner；`outerHTML` setter 只能通过 public wrapper
  alias 进入既有 replacement，成功后 wrapper 变为 detached，id alias 被清除但 staged
  属性/CharacterData 可再次设置并重新物化。任意 detached parser、事件/资源执行，以及除
  `Element.getElementsByTagName()` 外的完整 live collection 仍不支持。已物化 wrapper 的
  `insertAdjacentText()` 与 `insertAdjacentHTML()` 现在都覆盖四个位置，分别复用既有 Core
  Text-child callback 与 Ex9 parser 路径，并在成功后原地同步 `childNodes`、parser-created
  children 与 wrapper identity；脱离后 direct 普通 Text/CDATA 也保留最近一次快照数据。
  `insertAdjacentElement()` 同样覆盖四个位置，复用既有 Core element-child mutation，并在
  regular/已物化 Browser-created source 的移动后同步目标与可寻址 source snapshot；未物化
  source、非法位置、对象/arity、自引用和 detached target 仍在 mutation 前 fail closed。
  已物化 wrapper 的 `appendChild()`、`insertBefore()` 和 `removeChild()` 也支持 direct Element
  child 的有界插入、重排、跨父移动和移除；regular、detached created 与已物化 created source
  都在目标/source snapshot 和 alias identity 同步后返回。detached target、错误 parent/reference、
  无效对象和超限结构仍 fail closed；已物化 nested graph 只沿上述有界 attach/remove 路径
  更新，不因此开放任意 detached Node tree。
  已物化 wrapper 的 `replaceChildren()` 对零至四个 primitive 值在 Core text-only 替换完成后
  原地重建 Browser-owned Text wrapper，保留 `childNodes` collection identity，并让被替换的
  旧 child 脱离；对象、Element、CharacterData 或 Fragment 参数继续走既有的有界路径，超出
  参数/文本预算和无效目标 fail closed。detached Element 仍受上述 nested graph 预算约束，
  不提供除 `Element.getElementsByTagName()` 外的通用 live collection 或任意混合节点树。
  这仍是有界 parser-wrapper mutation，不提供完整 detached HTML parser、脚本/资源执行、
  MutationObserver 或除上述 bounded tag collection 外的完整 live collection。
- `HTMLBodyElement.text` 现在提供一个 live、遗留的 `text` attribute 投影：缺失 getter 返回
  空字符串，setter 对 `null` 使用 `[TreatNullAs=EmptyString]`，其他输入按 JavaScript
  `String` 转换；`setAttribute()`/`removeAttribute()` 的变化会被后续 getter 读取。它不
  扩展 deprecated presentation-color、完整 HTMLBodyElement 接口或 detached body staging。
- `document.createComment(data)` 和 `document.createCDATASection(data)` 都是 bounded detached
  CharacterData wrapper：`String` 转换，最多 65,535 字符，提供 data/
  offset、clone、relative、remove/reinsert；也接受 live CharacterData source
  move/replace。CDATA 提供 `wholeText`、`splitText`、`replaceWholeText`，逻辑相邻
  Text/CDATA 组成同一段，split 返回 Text suffix。Comment/CDATA 经 Ex12/Ex14 物化，
  更新复用 callback；事件、资源、observer、嵌套/混合/超限树语义和 detached target relative
  均 fail closed。
- HTML getter 与 Core mutation 入口共用有界 UTF-8 parser；它们保持
  身份、拒绝非法/超限/id 冲突并使 layout 失效。OuterHTML 只接受单一 Element 根或空字符串；
  顶层文本/Comment、多根和结构冲突拒绝。Core 无 DocumentFragment ABI；Browser 在 Core
  parser 之外维护两类 staging，不执行脚本、资源或事件。
  预算见 [`docs/TESTING.md`](../docs/TESTING.md)。
- `document.write()`/`document.writeln()` 不是通用 parser：只有注册
  `PBrowserScriptDocumentWriteCallbacks` 并设置当前 classic-script 索引才安装；片段有界、
  同步插入 script 后。Core 在 fragment parser 之前对 ASCII 大小写不敏感且带 tag-name
  boundary 的 `<script...` 起始 token 做 fail-closed 检查；因此 `<script>`、非法/超限/冲突
  id 均拒绝，不执行脚本、抓取资源或派发事件。该检查也可能保守拒绝包含同样原始 token
  的普通文本/属性值；无 callback、无效索引或 Core/宿主失败均 fail closed。
- 表单实现覆盖常用控件、validation、submission、reset 和 successful controls，但没有完整本地化 validation UI、所有 input type 的系统 picker 或桌面浏览器级 editing 行为。
- `labels`、form collections 和若干 NodeList 是静态 snapshot；支持的 form owner/form.elements
  关系现在识别带 `form="id"` 的 input、select、textarea、button、fieldset、img、object、output；按文档顺序
  纳入跨树 listed form-associated 元素；在表单关系中 img 只提供 FORM_OWNER，不进入 `form.elements` 或
  `fieldset.elements`；fieldset/object/output 只进入 `form.elements`，不会进入 successful-control
  或 FormData visitor。output 也可读取 `form` 与 `labels`，并通过 Core 提供 descendant-text
  `value`、带独立 override 的 `defaultValue` 以及 reset 恢复；这不是完整 listed-content
  算法。validation、submission/multipart、dialog/default-submit、reset 和按坐标的
  submit/reset 激活仍只对可提交控件复用同一 owner 规则；`PCore_FormResetById` 还提供按 form ID 的
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
  Core 的 `PCore_MultipartSubmissionEncode()` 现在负责 multipart boundary、CRLF、字段/文件
  顺序、quoted metadata 和 binary file bytes；宿主必须提供同步的 file read/free callback，
  body 总量固定不超过 1 MiB，容量不足、读取失败、缺少 callback 或超限均 fail closed 且
  不产生部分输出。该 API 不执行文件 I/O、网络发送、取消或重试。
  `PCore_FormDataById[Ex]()` 创建的独立 successful-control snapshot 也可通过
  `PCore_FormDataEncode()` 生成同一 bounded multipart body；它不受源 form 的 method、action
  或 enctype 约束。FormData entry 查询仍只提供文件名/type 元数据，实际文件读取只在同步
  encode callback 中发生，完整 File/Blob 对象、异步读取和浏览器安全策略仍未实现。
  Browser 的 `new FormData(form[, submitter])` 另有独立的 detached snapshot：无显式
  submitter 时使用旧 callback，带第二参数时使用 Ex callback，并复用 successful-control
  与 form-owner 规则；最多返回 64 项，名称最多 64 字节，字符串值最多 128 字节，文件名
  和 MIME 类型各最多 64 字节。非空 submitter 必须是启用且归属于目标 form 的 submit-type
  input/button；普通、禁用、跨 form、伪造对象、无 id、超限或缺少 callback 均安全失败。
  构造成功后 Browser 在 form 上同步派发非冒泡、不可取消的 `formdata` 事件，事件的
  `formData` 指向正在返回的对象；监听器可在构造返回前修改字段，`form.onformdata`
  也可用。脚本对象的 `append()`、新键 `set()` 和数组构造与原生快照共享 64 项预算；
  超限抛出 `QuotaExceededError`，失败不改变旧 pairs，替换已有键和删除后的追加仍可用。
  独立脚本 `URLSearchParams` 的 `append()`、新键 `set()` 和 pair-sequence 构造也共享
  `PBROWSER_SCRIPT_URL_SEARCH_PARAMS_MAX_PAIRS`（当前 64）项预算；超限同样抛出
  `QuotaExceededError`，失败不改变旧 pairs，替换已有键和删除后的追加仍可用。
  它不触发 validation、submit/reset 事件、默认动作或导航。Browser 对象仍只
  返回 filename/type 和空内容；应用若需发出 multipart 请求，必须把 Core snapshot 交给
  `PCore_FormDataEncode()` 并自行提供同步 file callback。完整 live
  HTMLFormControlsCollection、File/Blob 读取和其他 form-associated 扩展及浏览器完整表单树
  规则仍未实现。
- 事件系统覆盖常用 capture/target/bubble、取消和默认动作，但不支持所有 DOM Event 子类、pointer/touch/drag/drop/clipboard 或浏览器手势。宿主对单元素 `contenteditable` 另有受限 `CF_UNICODETEXT` paste/cut/copy 接线：非空选区才复制，折叠选区保持剪贴板不变，超长或非 Unicode 格式在 native mutation 前拒绝；Core mutation 暂时释放 retained layout，宿主在下一次 relayout 前必须用 native EDIT 的 DOM id 维持连续 beforeinput/input/change 的目标身份；它不是通用 DOM ClipboardEvent 或 async clipboard API。
- native 控件状态由 Core、Browser 和宿主共同提交；回调错误、stale token 或几何变化会 fail closed，可能表现为本次默认动作不执行。

## JavaScript

- 浏览器 JavaScript 默认关闭，启用后仍是实验性的有界 classic-script 组合。
- 独立 script 和浏览器 script 共用 Duktape 2.7.0，不存在第二套引擎；两者提供的 host objects 与生命周期不同。
- 不支持 ES module、dynamic import、WebAssembly、worker、service worker 或完整现代 ECMAScript host environment。
- Storage maps are session-local and independent; quota is 64 entries with 256/4096 UTF-16 key/value characters. Over-limit writes atomically throw `QuotaExceededError`; persistence is not provided.
- Browser bootstrap 只暴露当前已接线的 DOM/Event/form/navigation/timer 子集；缺失 API 通常 fail closed 或为 `undefined`。
- `document.write()`/`writeln()` 仅在 callback 存在时安装；受 16,384 字节和 parser 预算约束，
  不提供动态脚本、资源、`open()`/`close()` 或流式重写。源文本的 `<script...` 保护扫描只
  属于 document-write 边界，不改变其他 HTML mutation parser 的合同。
  `PScript_CollectGarbage()` 只回收
  引导临时对象，不改变 heap/globals。
- Selector remains a bounded subset with finite relation/list/branch budgets; unsupported pseudo-elements, namespaces, shadow DOM, full grammar, chained `:has()` and `:target` reveal fail closed. Exact supported states and limits are maintained in [`docs/TESTING.md`](../docs/TESTING.md).
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
 - script heap、native function、module/source、timer、queue 和执行时间都有固定预算；复杂页面可能因资源上限失败。独立 `positron_script.dll` context 默认 512 KiB，Browser bootstrap 使用 1.5 MiB 的独立有界堆上限；`PSCRIPT_MAX_NATIVE_FUNCTIONS` 当前为 29。Browser 同时启用 DOM、validation、contenteditable、导航、`document.activeElement`、`HTMLElement.focus()`/`blur()`、pointer-interaction selector、FormData 和有界 DOM removal 桥时会占满槽位，额外宿主 native function 必须先检查计数并在达到上限时保守失败；参考宿主为大型完整页面 bootstrap 使用默认脚本页预算的 4 倍，较小离线夹具仍可使用更低预算，但所有 page budget 都有上限且不改变 Browser 的固定 heap/native-function/source 预算；不能通过跳过必要桥或扩大为无界表来规避预算。
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
- 设备门在部署前分开查询目标卷和内部 object store。未传 `-RemoteBase` 时，默认隔离根是
  外置卡的 `\Storage Card\Temp\Positron-device-gate`；目录创建失败、路径级 API 不可用
  或硬性容量不足时，门会记录原因并回退到内置 `\Temp\Positron-device-gate`，在那里重新
  创建、清理和预检。显式 `-RemoteBase` 不启用回退，对外部路径没有路径级导出时仍
  fail closed。目标卷要求 staging 总大小加 1 MiB 余量；外部目标的内部 object-store
  只以 64 KiB 作为告警线，不把粗粒度内部数字误报成目标卷不足，已知内部路径才把
  object store 当作硬性容量门。OEM/WMDC 实际缓存需求仍可能超出告警线。正常清理要求
  `test_host.log` 完整复制两次且终态标记稳定；若最终目标卷或已知内部 object store
  确认不足，设备门会对门自己生成且非当前运行的旧目录做一次有界应急回收，先尽力保存
  日志，只有日志完整且稳定时才允许删除来释放空间；日志不完整的目录、未知目录、当前
  目录和删除失败的目录仍保留。应急回收后的复检仍不足才阻断部署。
- 新增公共 DLL 导出后若直接使用增量链接产物，设备门曾出现已有 Browser bootstrap 的 `PSCRIPT_ERROR_TIMEOUT (-4)`；完整执行 `scripts\build.bat Debug rebuild` 并重新 staging 后恢复通过。看到这类 bootstrap 超时应先排除旧产物/混包，不能把一次增量构建失败当作产品回归。
- 自动可视门只保证首帧和断言，不保证边距、字体、触摸、SIP、picker、旋转或失败网络体验。

## 测试覆盖

- 离线 compatibility corpus 已覆盖 history/viewport、资源 candidate/cleanup、生命周期、
  focus、geometry、overflow、scroll、selector 和 autofocus 的有界 callback 合同；这些夹具
  证明 snapshot、事件顺序、预算、clamp 和非法输入 fail closed，但不证明复杂 CSS、无限
  scroll tree、真实旋转、触摸、OEM 控件或视觉像素。
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
  仍受 64 步、公式系数和 1.5 MiB Browser heap 上限约束；完整动态状态、伪元素、namespace、
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
- TEST1179–1181 覆盖有限 selector 状态（`:visited`、`:scope`、`:default`）及 live
  mutation/query 顺序；宿主批准、history/URL 解析和 native 控件视觉仍由宿主负责。
- TEST1182–1188 覆盖 option/select/optgroup/fieldset 的属性、选择状态、owner 和有限
  collection snapshot（64 项、256 节点预算）；完整 live collection、popup、键盘、
  SIP/IME、触摸和视觉仍未实现。
- TEST1189–1192 覆盖 output/object/img 的有限 form-owner、labels/metadata 与
  successful-control 排除；不创建 plugin/替代内容窗口，也不覆盖完整资源行为或 native
  表单视觉。
- TEST1193 是离线的 Core/Browser `HTMLImageElement` 元数据与资源状态夹具，无新增立即
  人工风险；自动门证明 `document.images` snapshot、attribute/boolean/尺寸属性边界、
  非 `img` fail-closed，以及成功 SVG、终态 fetch failure、无 source 和仅 `srcset` 的
  `naturalWidth`/`naturalHeight`/`complete` 投影，并确认 retained decode 后才暴露自然
  尺寸。该门不覆盖后续 TEST1196 的密度选择、绝对 URL、CORS/referrer、完整 loading
  策略或 native 图像视觉；这些仍是未来能力或人工观察范围。
- TEST1194 是离线的 Browser `HTMLImageElement.decode()` 与宿主终态事件夹具，无新增立即
  人工风险；自动门证明无 source/成功/失败/source mutation/teardown 的 Promise 结果、
  `EncodingError`/`AbortError` 分类、Core relation 就绪门、trusted 非冒泡不可取消的
  `load`/`error`、重复通知幂等以及过时/相反/缺失目标的 fail-closed。该桥不实现
  绝对 URL、CORS/referrer、完整 loading 策略或 native 图像视觉；宿主仍负责
  fetch/decode/layout、终态通知和 microtask 调度，候选选择由 Core 统一执行。
- TEST1195 是离线的 Core image-map 命中夹具，无新增立即人工风险；自动门证明已布局
  `<img usemap>` 最多解析 64 个 linked `<area>` 和 64 个坐标，支持
  `default`/`rect`/`circle`/`poly`，并验证自然坐标缩放、malformed/nohref fail-closed、
  area 链接 metadata/几何、active/hover 状态、area→map 事件冒泡以及 Browser
  `isTrusted`/`trusted` 字段一致。transforms、完整 HTML image-map 算法、pointer/touch
  手势和 native 图像视觉仍未实现。
- TEST1196 是离线的 Core/Browser `srcset` 密度选择夹具，无新增立即人工风险；自动门证明
  两个 DPI 下的最小足够密度/最高候选、`currentSrc` 与 Core fetch/layout/cache/natural
  size/`complete` 一致、缓存重扫不重复抓取，以及无 `src`、混合 `w`/`x` 和畸形密度
  的回退或 fail-closed。不同密度资源的真实视觉、绝对 URL、CORS/referrer、完整 loading
  策略、自动图像事件和 native 图像视觉仍未实现或需人工观察。
- TEST1197 是离线的 Core/Browser `srcset` 宽度选择夹具，无新增立即人工风险；自动门证明
  `w` 候选按 `sizes` 的 px/vw/vh 长度和单一 min/max-width 条件选择，在 240/480 CSS
视口下与 Core fetch/cache/layout/currentSrc/natural-size 一致；无 `sizes`、畸形 `sizes`
和混合 descriptor 安全回退。复杂媒体条件、绝对 URL、CORS/referrer、完整 loading、
动态网络切换、旋转和不同密度资源的真实视觉仍未实现或需人工观察。
- TEST1198 覆盖 Core/Browser 的有界 `<picture><source>` 选择：最多 16 层祖先、8 个
  preceding source 和 64 个 direct-child 节点按文档顺序过滤 media/type，再复用 source
  的 `srcset`/`sizes` 选择；unsupported/malformed source 回退到后续 source 或 img
  候选，240/480 CSS 视口下的 `currentSrc`、fetch/cache/layout/natural-size 保持一致。
  完整媒体查询、绝对 URL、CORS/referrer、loading 策略和 native 图像视觉仍未实现或需
  人工观察；source mutation 的生命周期通知由 TEST1199 覆盖。
- TEST1200 覆盖脚本图片来源 mutation 的可选 typed callback：`img.sizes`、source 的
  `media`/`type`/`srcset`/`sizes` 以及 set/remove attribute 都产生正确的 id、kind、
attribute 和 removed 元数据；重复注册、native-function 数量不变、不一致 metadata
fail closed 和注销后的静默均已自动断言。该门不执行自动资源替换，也不覆盖通用动态 DOM
  插入/删除（TEST1201、TEST1214–1216 仅覆盖有界 removal 路径）、完整 loading、
  视觉或触摸/SIP 风险。
- TEST1300 覆盖 generation-aware image source replacement：source A→B→A 时旧
  `decode()` 以 `EncodingError` 退休，Core 选出的 `currentSrc` 仍是唯一真值；旧
  generation、缺失 generation 的 legacy event 和相反终态均被拒绝，失败候选只接受匹配
  generation 的 `error`。夹具使用带 id 的 `<picture>`/`<img>` 关系；匿名或不可寻址的
  source 关系仍按上述 fail-closed 限制处理。该门不增加自动 fetch/decode/layout/paint，
  也不覆盖绝对 URL、CORS/referrer、完整 loading 策略或 native 图像视觉。
- TEST1301 覆盖 Core 的 `PCore_MultipartSubmissionEncode()`：成功控件顺序、boundary/
  CRLF、quoted 字段与文件名、binary file bytes、size probe、容量不足时无部分输出以及
  缺少 file callback 的 fail-closed。body 总量固定为 1 MiB 上限；宿主只提供同步文件
  callback，网络发送、文件权限和请求取消/重试仍不在本测试覆盖范围。
- TEST1302 覆盖 `PCore_FormDataEncode()`：默认 GET/urlencoded form 的独立 FormData
  snapshot 仍可生成 multipart body，并断言成功控件顺序、文件 bytes、容量/size-probe
  原子性和 callback 缺失失败。Browser `FormData` 对象本身仍是 metadata-only；事件修改、
  File/Blob API、网络发送和 native 表单视觉不由该夹具承诺。
- TEST1303 覆盖 Browser 脚本 `FormData` 的 64 项 mutation budget：满容量时 append、
  新键 set 和数组构造的 `QuotaExceededError`、失败不变性、已有键替换以及删除后的追加。
  这只约束 pairs 数量，不提供完整 File/Blob 内容、网络发送或 native 表单视觉。
- TEST1304 覆盖 Browser 脚本 `URLSearchParams` 的 64 项 mutation budget：满容量时
  append、新键 set 和 pair-sequence 构造的 `QuotaExceededError`、失败不变性、已有键替换
  以及删除后的追加。该门不证明 URL 解析、导航、网络发送或 native 表单视觉。
- TEST1305 covers Storage quota, atomic errors, capacity reuse and session/local independence; persistence is out of scope.
- tracked INI 是快速 smoke，不是测试全集；全量自动清单由打包/门脚本从源码 dispatch 生成。
- manual-only fixture 必须在 `auto=0` 下运行，不能放入自动全量并把主动跳过视为通过。
- TEST13 是一个真实网页哨兵，不代表任意互联网网站兼容性。
- 人工风险可以按规则累计，但崩溃、数据损坏、严重布局破坏和核心交互阻塞必须立即复核。

## 不保证

- 现代浏览器标准符合性或任意网站可用性；
- 在未审查旧依赖安全状态时用于高风险生产环境；
- 由 `test_host.exe` 提供可复用产品 API；
- 通过单次截图、提示音、部分日志或桌面构建证明设备基线；
- 绕过公开头文件、直接链接内部 NetSurf 静态库后的 ABI 稳定性。

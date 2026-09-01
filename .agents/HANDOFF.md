# 当前交接

本文件只描述当前产品基线、最近有效证据、未决风险和唯一下一步。逐批实现过程由 Git 历史保存，历史事故见 `docs/history/`，未来方向见 `ROADMAP.md`。

## 项目使命

Positron 为 Windows Mobile 6 / Windows CE 5.2 ARMV4I 提供模块化 TLS、JSON、HTTP、图像、脚本、渲染与浏览器会话 DLL。公共边界保持 C ABI、UTF-8、opaque handle 和显式所有权；`test_host.exe` 只是回归宿主与示例消费者。

## 当前 Git 与工作区

- 分支：`main`。next709 的 Browser selector 结构伪类语义、TEST1154 夹具和职责文档已完成；
  tracked 范围只涉及 `positron_browser`、`test_host` 和对应文档，未把 `tmp/`
  中的本地证据纳入版本控制。
- `TEST_MAX_NUMBER` 已为 1154。tracked `test_host/test_host.ini` 仍是窄 smoke：
  `auto=1`、`javascript=0`、选择 `13,20,27,56,58,62,64-67,73,75,999`；nightly/device
  tooling 从源码 dispatch 动态生成全量清单。
- 设备门继续假定用户已在 WMDC/Device Emulator GUI 手动连接恰好一个目标；RAPI 只复用
  当前会话，不连接、选择、cradle、重置或强杀设备。

## 近期已完成能力摘要

- next698–706 已完成 Browser/Core 的有限滚动、inline 几何、布局尺寸、nested reveal、
  focus 和 autofocus 组合；对应语义由公共 DLL 拥有，宿主只做 Core relation、WM/native
  接线、clamp、绘制和生命周期调度。TEST1143–1151 已覆盖这些路径的离线合同与定向门；
  完整滚动树、Range/Selection、transform、pinch zoom、平滑/惯性滚动、完整焦点算法和
  OEM 视觉仍属于已知限制，具体边界见 `docs/TESTING.md`。

- next707 在 `positron_browser.dll` 的脚本 bootstrap 中补齐了有界 selector 列表和
  关系组合器：`matches()`、`closest()`、`querySelector()` 与 `querySelectorAll()`
  现在一致支持顶层逗号列表、后代空格、子代 `>`、相邻兄弟 `+` 和一般兄弟 `~`；
  简单 compound selector、属性值引号/逗号保护、非法 selector fail-closed 和每条
  组合/祖先/兄弟遍历 64 步上限均由 Browser 拥有。TEST1152 只提供离线 fixture 和
  断言，没有把 selector 解析放回宿主。为保持脚本 bootstrap 可在 WM6 设备上运行，
  Browser 会话堆上限从 662 KiB 明确调整为 678 KiB；首次试跑暴露的内存上限错误已由
  同一批复测消除。

- next708 在同一 Browser selector 边界内补齐了六类属性操作符：`=`, `^=`, `$=`, `*=`,
  `~=`, `|=`，并允许它们与通配标签、顶层列表及四种关系组合器共同使用。解析器在
  引号内寻找属性结束符，因此空格、逗号和 `]` 不会误截断值；空操作数、未闭合引号、
  不支持的大小写修饰符和未知语法均 fail closed。TEST1153 覆盖正向/负向匹配、列表
  顺序、组合器和非法输入；实现全部位于 `positron_browser.dll`，宿主只保留 fixture
  与断言，未新增公共 ABI 或 heap ceiling。

- next709 在同一 Browser selector 解析器中加入有界结构伪类：`:root`、`:empty`、
  `:first-child`、`:last-child`、`:only-child`、`*-of-type` 变体和四种 `nth-*` 变体。
  `odd`/`even`、整数和受限 `an+b` 公式只读取 childNodes/关系快照，组合与祖先/兄弟
  遍历仍有 64 步上限；非法公式、`of` 过滤、`:not()`、伪元素和超大数值安全拒绝。
  TEST1154 覆盖 root/empty/child/type/list/matches/closest 与 fail-closed 输入；实现
  全部位于 `positron_browser.dll`，宿主只保留离线 fixture 和断言。为容纳 bootstrap，
  Browser 会话 heap ceiling 从 678 KiB 调整为 710 KiB；空元素的 `:empty` 不依赖会
  返回 NULL 的 textContent callback，而使用只读 childNodes 快照。

- next678 已把候选 generation、取消请求、退休状态、提交资格和 committed/failed 终态迁入 `positron_browser.dll` 的 opaque handle。next679 进一步把 pending/committed/failed/cancelled/stale 结果分类作为只读 Browser 摘要；next680 再提供独立 candidate/resource 的只读提交组合快照；next681 增加提交后 cleanup snapshot 和宿主回收观测；next682 将 history entry 的 viewport snapshot 迁入 Browser，并移除宿主的按 entry 滚动数组。宿主仍拥有 worker、response、资源事务、WM 消息、退休队列、窗口滚动应用和页面 swap，不把线程、窗口、网络或 Core document 带入 Browser ABI。
- Browser 现在同时拥有 URL 去重、role/policy、资源字节、终态、失败分类、重试预算、commit gate、hash-only 摘要、fallback 计数、候选 admission 状态、候选结果分类、candidate/resource 组合 decision 和 cleanup snapshot；宿主只保留 URL→resource-index 短引用、candidate handle 和平台调度状态，并消费结果快照写日志。清理快照复制完整有界 resource observation，要求 pending 工作先收敛，committed candidate 还必须配 READY gate。
- Browser script session 现在可注册 page-level scroll callback：脚本 `scrollTo`/`scrollBy` 请求由宿主 clamp/apply 并返回实际坐标，宿主的 scrollbar、触摸、键盘、resize 或 fragment reveal 路径可用 `PBrowser_ScriptSessionNotifyScroll` 同步脚本偏移；同步通知去重 `scroll` 事件，且脚本 callback 内不会递归进入 runtime。候选 session 在提交前只回显坐标，不改变旧页。
- Browser script session 还提供 `PBrowser_ScriptSessionNotifyResize`：宿主在 WM_SIZE 完成 Core style/layout、page-level clamp 和 native child reposition 后传入 CSS viewport 宽高/DPR；Browser 更新 `innerWidth`/`outerWidth`/`devicePixelRatio`、`screen` 宽高/方向，刷新每个 session 最多 64 个 `matchMedia()` 列表，并在匹配翻转时先同步派发 `change`、再派发去重的 window `resize`。它不触发 Core layout，也不自动运行 timer/animation frame；超过 64 个列表只保留创建时快照。
- Browser script session 还提供布局视口对应的 `visualViewport`：`width`/`height` 与 viewport 同步，`pageLeft`/`pageTop` 与 page scroll 同步，`scale` 固定为 1，`offsetLeft`/`offsetTop` 固定为 0；有效 resize/scroll 按 visual viewport、window 顺序同步派发并去重。它不模拟 pinch zoom、视觉 viewport 偏移或 nested overflow。
- Browser script session 还提供由宿主驱动的顶层窗口焦点合同：`PBrowser_ScriptSessionDispatchWindowFocus` 归一化激活值，更新 `document.hasFocus()`，并在实际变化时按属性 handler、listener 顺序派发可信的非冒泡 focus/blur；重复值保持静默。参考宿主已把 `WM_ACTIVATE` 接到该入口，但 native 控件焦点、焦点矩形和 OEM/跨窗口策略仍由宿主负责。next695 增加的 `PBrowserScriptActiveElementCallbacks` 是显式可选桥：宿主把 Core 的当前焦点 id 提供给 Browser，getter 解析有效 id，否则回退 `document.body`；未注册 callback 的 session 不承诺安装该属性。next696 增加的 `PBrowserScriptFocusRequestCallbacks` 只把带 id 的 `focus()`/`blur()` 请求同步交给宿主；next697 通过新增 Ex callback 传递 `prevent_scroll` 和实际 CSS page scroll 结果，默认 focus 可 reveal 到 page-level viewport；next705 又让 Browser 在发现 retained overflow ancestor 时复用 Ex 的有效 `prevent_scroll`，并在 callback 后完成有界嵌套 reveal；宿主仍用 Core 的资格/焦点 API、native HWND 和已有事件接线完成事务，disabled/hidden/stale/重复请求安全 no-op。
- Browser script session 还提供统一的 `PBrowser_ScriptSessionRunTaskCheckpoint`：宿主用 `phase_mask` 选择 timer、animation frame、message 和 idle callback，Browser 按固定顺序执行并在每个阶段后运行一次有界 microtask；宿主提供单调时钟、frame timestamp、idle deadline、message limit 和 UI 消息循环。参考宿主已通过 16 ms `WM_TIMER` 接入，Browser 不创建线程或接管宿主调度。
- Browser script session 的页面生命周期还覆盖初始 `complete` 后只派发一次的 `pageshow`，以及 hidden→visible 的 `visibilitychange`→`pagehide`/`pageshow` 顺序；重复 complete 或重复 hidden 值保持静默，有限 page event 的 `persisted` 固定为 `false`，因为没有 bfcache。宿主在每次 `WM_ACTIVATE` 时调用 `PBrowser_ScriptSessionDispatchWindowFocus`，Browser 维护 `document.hasFocus()` 并在状态变化时派发可信的 window focus/blur；重复状态保持静默。
- Core 通过既有 DOM relation callback 暴露当前 layout border-box 的 `x`、`y`、`width`、`height` 四个整数 CSS 像素分量；Browser 的 `Element.getBoundingClientRect()` 和有界 `Element.getClientRects()` 组合 viewport-relative 矩形并扣除 CSS page scroll。宿主把 Core 的物理滚动坐标与 Browser 的 CSS page 坐标在当前 DPI 边界换算，避免高 DPI 下重复放大或缩小。
- Core 的 `PCore_InteractionFocusElementId` 以 size-probe/固定容量合同复制当前焦点节点的非空 UTF-8 id；无焦点、无 id、过时节点或过小缓冲会 fail closed，不改变 DOM、style、layout 或焦点状态。`PCore_FocusTargetInfoById` 与 `PCore_InteractionFocusById` 为宿主提供按 id 的已布局资格检查和 Core focus node 更新；next706 又增加 `PCore_AutofocusTargetInfo`、`PCore_InteractionFocusAutofocus` 与目标保持的 `PCore_EventDispatchFocus`，让宿主在 layout/native 子控件完成后显式选择第一个合格 `autofocus` 目标并覆盖无 id 事件，而不切换职责边界。Browser 通过可选 `PBrowserScriptActiveElementCallbacks` 把可寻址 id 投影为 `document.activeElement`，通过旧/Ex focus request callback 接收脚本请求；Ex 结果可在 callback 返回后同步 page-level scroll，无效来源回退到 `document.body`，不可用目标 no-op。
- 上一产品基线为 `88d68ebd`（next669，首个离线 compatibility corpus 完整流程）；更早基线为 `c0c4ba0e`（next668，单元素 `contenteditable` 的受限 CF_UNICODETEXT 粘贴/剪切与 WM_COPY 边界）。
- Core 现在报告稳定的有效表单方法常量，并为显式 submitter 或单行输入隐式提交解析最近祖先 dialog id 与 submitter value。Browser 提供按 id 直接执行 `dialog.close(value)` 的会话边界；参考宿主只在 validation 和可取消 `submit` 均允许后调用它，不生成网络导航，也不错误派发 `cancel`。Core 还提供 `PCore_PaintDocumentWithModal`：普通文档绘制后覆盖有界实体色 backdrop，并按 Browser 的活动 id 重绘已打开的 dialog；next658 的 backdrop 指针策略和此前的 modal 焦点/Escape 边界保持不变。
- `tmp/` 保存本地设备日志和截图，不跟踪。

## 当前中期里程碑

在保持 VS2008/WM6 约束的前提下，把已经形成的 Core、Browser 和平台宿主能力整合为可由真实页面驱动的有界网页运行时。重点是完成用户可感知的纵向能力、把通用语义留在公共 DLL，并以小型真实页面/交互语料库防止只增加孤立 API。

## 当前短期目标

- next706 已在 Core 中提供有界 `autofocus` 目标发现、Core focus node 设置和目标保持
  的事件 dispatch；宿主在 style/layout 与 native 子控件创建完成后显式调用它，有 id
  目标复用 Browser focus bridge，无 id 目标仍可派发 focus/focusin。TEST1151、
  `1151,999` 定向门和 `1142,1148-1151,999` 相邻回归门均已通过；`test_host.exe`
  只保留生命周期接线、native/page 适配、fixture 和断言。
- next707–709 已完成 Browser selector 的列表/组合器/属性操作符/结构伪类纵向能力；
  TEST1152–1154、`1152-1154,999` 定向门已通过。当前唯一下一步是 next710：重新检查
  compatibility corpus、源码、设备日志和截图，固定一个新的用户可见缺口，再选择一个
  边界清楚的公共 DLL 纵向能力。不要仅为增加编号拆分提交，也不要把页面语义放回
  `test_host`。

## 已验证产品事实

### 公共边界

- 顶层公共 DLL 为 TLS、JSON、HTTP、image、script、core 和 browser。
- NetSurf/libcss/libdom/hubbub、Expat、libsvgtiny、libjpeg 等移植工程是内部实现依赖。
- 独立脚本和浏览器脚本共用 Duktape；浏览器 JavaScript tracked 默认仍为关闭。
- 通用 URL、history、DOM、Event、表单、图像和脚本 session 语义位于对应公共 DLL；宿主保留 WM 窗口、消息、控件、SIP/IME、picker、导航调度和资源 I/O。

### 当前网页能力

- HTML/CSS/DOM、整树 style、NetSurf layout/redraw、GDI 绘制与资源缓存已形成正式 Core 路径。
- 常用 block/inline/flex/table、图片/SVG、背景、列表、有限定位、表单控件、验证、提交与 reset 已有设备回归；这不代表完整 CSS/HTML。
- Browser 层提供有界 history、same-document state、script session、DOM/Event/input/navigation callbacks，以及 timer/microtask/lifecycle、native 控件事务、导航资源事务和候选生命周期/结果协调。
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
- 支持的链接、summary、native EDIT/SELECT/button/file 等目标，以及带有效非负 `tabindex` 的普通布局元素按有界顺序响应 Tab/Shift+Tab：正值升序、同值 DOM 稳定排序，随后零/缺省组；负值、disabled/hidden/stale 目标和 file picker 仍被排除。Browser 报告活动 modal id 后，宿主可用 Core 的 scoped snapshot 将顺序焦点限制在 dialog 子树；宿主仍同步焦点事件、原生焦点和滚动可见性。
- `<dialog>` 的 show/showModal/close/requestClose、returnValue、cancel/close 事件、活动 modal id 查询、宿主驱动的 Escape 请求桥接、有界 backdrop 指针策略、`method="dialog"` 默认动作和 Core modal paint 已形成契约。显式点击、脚本 `click()` 和单行输入隐式 Enter 都遵循 validation→可取消 submit→直接 close/returnValue；CSS `::backdrop`、透明合成、多个 modal 和跨文档 modal 仍未实现。
- 单元素 `contenteditable` 已形成 Core/Browser/宿主边界：Core 解析祖先继承并限制合法 UTF-8 纯文本 mutation，Browser 暴露 `isContentEditable`/`innerText`、`selectionStart`/`selectionEnd`/`selectionDirection` 与 typed input 事务，宿主为带 id 且已布局的有效 editing host 创建有界 WM multiline EDIT 代理。允许的 `WM_CHAR` 在默认处理完成后回读最终文本并派发 `input`；取消的 `beforeinput` 不修改 Core，重复/提前 `EN_CHANGE` 不会制造空事件。Browser 选区偏移使用 UTF-16 code unit；宿主将 WM EDIT 的 CRLF 位置转换为逻辑 LF，并在可用时同步原生 HWND。无修饰鼠标拖选以及 Shift/方向键扩展由宿主短暂保存 anchor；捕获丢失、取消模式和焦点切换会先结束手势，再通过 Browser 的去重通知入口刷新范围。宿主对 `WM_PASTE`/`WM_CUT` 只接受有界 `CF_UNICODETEXT`，把规范化后的 UTF-8 data 交给 `beforeinput`，允许后执行 native default，再提交 Core/input 和折叠选区；`WM_COPY` 只写入非空的有界 Unicode 选区，折叠选区保持现有剪贴板不变；格式缺失、超长或读取失败时 fail closed。为兼容 WinCE 原生剪切的内部重入，宿主只在外层 `WM_CUT` 默认处理期间放行同一 HWND 的嵌套 `WM_COPY`。每页最多 16 个宿主、文本最多 8192 UTF-8 字节；嵌套继承后代不重复创建 host。
离线 compatibility corpus 已覆盖导航资源事务、候选提交与回滚、history/viewport、页面生命周期、脚本调度、焦点、滚动和 Core/Browser 几何。每项测试的 fixture 与断言说明统一见 [`docs/TESTING.md`](../docs/TESTING.md)；handoff 只保留当前门和仍未完成的边界。

### 当前测试入口

- `TEST_MAX_NUMBER`：1154。
- tracked `test_host/test_host.ini`：`auto=1`、`javascript=0`，选择 `13,20,27,56,58,62,64-67,73,75,999`。
- tracked INI 是窄 smoke，不是全量目录；nightly 打包脚本从源码 dispatch 动态生成全量自动清单。
- 设备连接必须先由用户在 WMDC/Device Emulator GUI 手动完成；RAPI gate 只使用当前唯一会话。

## 最新有效设备证据

当前最新产品门为 next709 的 Browser selector 结构伪类与 next708/707 相邻回归：

- next709 最新定向目录：`tmp/device-runs/20260901-222938-next709-selector-structural-final2/`；
  动态选择 `1152-1154,999`，4 项；4/4 通过，零 `ERROR`/`FAIL`，唯一
  `TESTBENCH PASS`。TEST1154 验证 `:root`、`:empty`、child/of-type 与四种 `nth-*`
  结构伪类、列表顺序、`matches()`/`closest()`、受限 `an+b` 公式和非法输入回退；
  TEST1152/1153 验证既有组合器和六类属性操作符，TEST999 请求一次提示音。
- 设备：640x480，dpi=192；使用当前 WMDC GUI 会话、正式 Debug ARMV4I 构建和同批
  staging。RAPI gate 只复用已有 GUI 会话，不连接、选择、重置或杀死设备。
- 静态验证：`python scripts/test_c89ize.py`、正式 Debug ARMV4I 构建和
  `python scripts/audit_repo.py` 均通过；实现和测试分别位于 `positron_browser` 与
  `test_host`，未新增公共 ABI，Browser heap ceiling 为 710 KiB。678 KiB 预算在新
  bootstrap 下无法通过，已由定向实验确认并记录为产品边界。

next707 的相邻证据保留如下，作为本批的直接回归依据：

- next707 最终相邻目录：`tmp/device-runs/20260901-205857-next707-selector-final/`；
  动态选择 `1151-1152,999`，3 项；3/3 通过，零 `ERROR`/`FAIL`，唯一
  `TESTBENCH PASS`。TEST1152 验证 selector 列表、后代/子代/相邻兄弟/一般兄弟组合器、
  属性值逗号保护、`matches()`/`closest()`/query 结果顺序和非法/尾随逗号 selector
  fail-closed；TEST1151 验证既有 autofocus，TEST999 请求一次提示音。

## 历史设备证据（仅供追溯）

上一条产品门为 next701 的 Core/Browser 布局尺寸快照组合：

- next701 定向目录：`tmp/device-runs/20260901-102420-next701/`；
- 动态选择：`1146,999`，2 项；2/2 通过，零 `ERROR`/`FAIL`，唯一
  `TESTBENCH PASS`。TEST1146 验证六个布局尺寸 relation、Browser 只读 getter、
  offset/client/scroll 算术、隐藏元素零值回退和 getter descriptor，TEST999 请求一次
  提示音。
- 设备：640x480，dpi=192；使用当前 WMDC GUI 会话、正式 Debug ARMV4I 构建和同批
  staging。RAPI gate 只复用已有 GUI 会话，不连接、选择、重置或杀死设备。
- 静态验证：`python scripts/test_c89ize.py`、正式 Debug ARMV4I 全量 rebuild、同批
  staging 和定向设备门均通过；设备日志中的选择、完成数、错误数和唯一
  `TESTBENCH PASS` 已复核。

next700 及更早设备证据已由 Git 历史保留；当前 handoff 只保留最近的 next701、
next705、next706 和稳定全量基线，稳定语义见 `docs/TESTING.md`。

- 全量目录：`tmp/device-runs/20260830-163642-next670-full-final/`；
- 动态选择：`1-22,24-77,80-231,233-262,264-448,482-998,1000-1117,7b,999`，共 1080 项；仅排除 manual-only 的 TEST232/TEST263；
- 结果：1080/1080，通过；唯一 `TESTBENCH PASS`，零 `ERROR`/`FAIL`，TEST999 提示音请求一次；
- 设备：640x480，dpi=192；使用当前 WMDC GUI 会话、正式 VS2008 ARMV4I Debug 构建和同批 staging，完整门使用 3600 秒等待上限；
- 直接相邻尾段 `tmp/device-runs/20260830-163347-next670-window-1076-1117/` 也已 43/43 通过，确认 TEST1077 的高 DPI label 命中修复没有影响后续 native 控件、键盘和导航测试。

next670 的全量门覆盖了表格边框、DPI 几何、网络哨兵、独立 bootstrap、classList、selector、Promise/namespace 堆预算、native 控件、label forwarding、contenteditable、dialog、history 和 TEST1117 离线 corpus。全量选择由源码 dispatch 动态生成，不等于 tracked smoke INI；`tmp/` 中的日志仅是本地证据。

## 当前人工验收状态

以下路径已有过真实设备确认，但后续触及相邻基础设施时仍需重新评估：

- example.com → IANA 的容器边距、深层导航和旧页保留；
- SIP 候选词整词提交；
- bitmap/SVG、表格、列表和常见布局的可见结果；
- native EDIT/SELECT、真实 file picker、旋转和 DPI 路径。
- 带 `tabindex` 的普通元素的设备焦点矩形、触摸命中和不同 DPI 视觉仍需人工观察；语义顺序已有自动断言。
- `<dialog>` backdrop 的整体色彩、边界、滚动/旋转下的视觉仍属于可累计的人工观察；Core 的绘制顺序和设备门像素契约已有自动断言。
- contenteditable 的 OEM 硬键盘/自动重复、SIP/IME 候选词、跨应用剪贴板互操作、滚动/旋转和不同 DPI 下的文本视觉仍属于可累计人工风险；1113 已在真实 WM EDIT 上验证无修饰鼠标拖选的连续范围/方向通知，1114 验证了 Shift/方向键、捕获丢失和焦点切换的有界通知收尾，1112 覆盖脚本 `selectionchange` 去重，1115 覆盖宿主自备的 `CF_UNICODETEXT` paste/cut，1116 覆盖宿主 `WM_COPY` 与格式/容量拒绝。完整 ClipboardEvent/async clipboard、CF_TEXT/富文本转换仍不在契约内。
- TEST1117–TEST1154 都是离线自动夹具，没有新增必须立即人工复核的崩溃或数据风险；它们的真实视觉、触摸、旋转、SIP/IME、picker 和不同 DPI 呈现继续进入人工累计清单。自动结果不替代真实网络恢复、OEM 控件或逐资源视觉验收。
- TEST1136 是同步、可取消的脚本生命周期门，没有新增必须立即人工复核的视觉风险；参考宿主取消时只保留当前页面，不显示 prompt。自定义宿主若提供确认 UI，仍需把真实关闭、导航和提示交互加入人工清单。
- TEST1137 是离线的脚本队列顺序和宿主调度夹具，没有新增必须立即人工复核的视觉风险；真实页面的 timer/frame/message/idle 频率、功耗和 OEM 消息行为仍属于宿主集成观察，自动门只证明 Browser 顺序与限额合同。
- TEST1138 是离线的页面生命周期语义夹具，没有新增必须立即人工复核的视觉风险；真实页面的后台挂起、OEM 可见性通知、bfcache 和页面视觉仍属于宿主集成观察，自动门只证明 Browser 的有限事件顺序、去重和 `persisted == false` 合同。
- TEST1139 是离线的窗口焦点语义夹具，没有新增必须立即人工复核的视觉风险；真实 WM_ACTIVATE/OEM 前后台切换、native 控件焦点、焦点矩形和跨窗口策略仍属于宿主集成观察，自动门只证明 Browser 的 `document.hasFocus()`、focus/blur 事件字段与去重合同。
- TEST1140 是离线的 Core/Browser activeElement 语义夹具，没有新增必须立即人工复核的视觉风险；真实 native 控件焦点、焦点矩形、自动初始焦点、焦点陷阱、SIP/IME 和跨窗口切换仍属于宿主集成观察，自动门只证明有界 id 投影和 body 回退合同。
- TEST1141 是离线的 Browser/Core focus request 语义夹具，没有新增必须立即人工复核的视觉风险；真实 native HWND 切换、焦点矩形、OEM 控件、SIP/IME 和跨窗口切换仍属于宿主集成观察，自动门只证明按 id 资格、Core focus node、focus family 顺序、重复/失效目标 no-op 和注销后的 fail-closed 合同。
- TEST1142 是离线的 Browser/Core page-level focus reveal 语义夹具，没有新增必须立即人工复核的视觉风险；真实滚动条、触摸/键盘滚动、nested overflow、scroll-margin、平滑/惯性滚动、不同 DPI 下的焦点视觉和 OEM 控件仍属于宿主集成观察，自动门只证明默认 reveal、`preventScroll` 保持 viewport、矩形可见性和 callback 后脚本同步合同；TEST1150 另覆盖嵌套 focus reveal。
- TEST1143 是离线的 Browser page-level `scrollIntoView()` 语义夹具，没有新增必须立即人工复核的视觉风险；真实滚动条、触摸/键盘滚动、nested overflow、scroll-margin、平滑/惯性滚动、复杂布局和不同 DPI 下的对齐视觉仍属于宿主集成观察，自动门只证明有限对齐、事件去重和 smooth 拒绝合同。
- TEST1144 是离线的 Browser/Core `getClientRects()` 语义夹具，没有新增必须立即人工复核的视觉风险；真实页面的 inline 多片段、Range、transforms、nested overflow、字体、滚动和不同 DPI 视觉仍属于宿主集成观察，自动门只证明单矩形集合的身份、边界、滚动跟随和隐藏回退合同。
- TEST1145 是离线的 Browser/Core inline 多片段语义夹具，没有新增必须立即人工复核的视觉风险；真实页面的复杂 inline 嵌套、Range/Selection、字体度量、transforms、nested overflow、滚动和不同 DPI 视觉仍属于宿主集成观察，自动门只证明最多 16 个按行片段的 identity、顺序、union 和 Core/Browser 一致性。
- TEST1146 是离线的 Browser/Core 布局尺寸语义夹具，没有新增必须立即人工复核的视觉风险；真实页面的复杂 box model、滚动条绘制、nested overflow、transforms、字体度量和不同 DPI 视觉仍属于宿主集成观察，自动门只证明六个整数尺寸 relation、只读 getter、边框/内边距/retained-scrollport 算术和后代 extent 的有界一致性。
- TEST1147 是离线的 Browser/Core 元素 overflow 滚动语义夹具，没有新增必须立即人工复核的崩溃或数据风险；真实嵌套滚动条的绘制、裁剪、指针/触摸、不同 DPI 和复杂滚动容器仍属于宿主集成观察，自动门只证明带 id 常见 box 的 offset/clamp、脚本方法、事件去重和 pointer snapshot 同步。
- TEST1148/1149 是离线的 Browser/Core 有限 nested `scrollIntoView()` 语义夹具，没有新增必须立即人工复核的崩溃或数据风险；真实多层滚动树的裁剪、scroll chaining、scroll-margin、指针/触摸、不同 DPI 和平滑/惯性滚动仍属于宿主集成观察，自动门只证明最近 ancestor 与 `container:"all"` 的有界链、关系 40–43、对齐、page 稳定、事件去重和 smooth 拒绝合同。
- TEST1150 是离线的 Browser/Core nested focus 语义夹具，没有新增必须立即人工复核的崩溃或数据风险；真实滚动条裁剪、焦点矩形、触摸/SIP、不同 DPI 和完整滚动树仍属于宿主集成观察，自动门只证明 Ex `prevent_scroll` 协调、inner→outer 双轴 reveal、focus/scroll 顺序、重复 focus、显式 `preventScroll` 和远端 blur 的合同。
- TEST1151 是离线的 Core/Browser autofocus 语义夹具，没有新增必须立即人工复核的崩溃或数据风险；真实初始焦点矩形、native HWND、滚动条裁剪、触摸/SIP、不同 DPI 和多窗口策略仍属于宿主集成观察，自动门只证明 DOM 顺序资格、size-probe、Core focus node、无 id 目标事件保持和 Browser body 回退合同。
- TEST1152 是离线的 Browser selector 组合器语义夹具，没有新增必须立即人工复核的崩溃或数据风险；真实页面的复杂 CSS selector、动态伪类/伪元素、属性大小写修饰符、namespace、shadow DOM、布局视觉和不同 DPI 仍属于宿主集成观察，自动门只证明有界列表/组合器、属性值分隔、查询顺序和非法输入 fail-closed 合同。
- TEST1153 是离线的 Browser selector 属性操作符语义夹具，没有新增必须立即人工复核的崩溃或数据风险；真实页面的完整 CSS selector、动态伪类/伪元素、属性大小写修饰符、namespace、shadow DOM、布局视觉和不同 DPI 仍属于宿主集成观察，自动门只证明六类操作符、通配标签、组合器/列表顺序、引号内 `]` 保护和非法输入 fail-closed 合同。
- TEST1154 是离线的 Browser selector 结构伪类语义夹具，没有新增必须立即人工复核的崩溃或数据风险；真实页面的动态状态、伪元素、完整公式、namespace、shadow DOM、布局视觉和不同 DPI 仍属于宿主集成观察，自动门只证明有限 root/empty/child/of-type/nth 关系、列表顺序、matches/closest 和非法输入 fail-closed 合同。
- next682 的 TEST1081/1082 没有新增必须立即人工复核的崩溃或数据风险；不同页面高度、横向滚动、旋转、DPI 和真实后退按钮的整体视觉/触摸结果继续与既有滚动和 history 风险一起累计观察。自动门只证明 Browser snapshot 与宿主 clamp/apply 的语义。
- next683 的 TEST1128 同样是离线自动夹具，没有新增必须立即人工复核的崩溃或数据风险；宽页面的横向滚动条、左右边距、触摸/键盘操作、resize/旋转/DPI 视觉和真实页面 overflow 结果进入既有人工累计清单。自动门只证明 page-level extent、坐标一致性、clamp 和 snapshot 语义。
- next684 的 TEST1129 是离线脚本/宿主同步夹具，没有新增必须立即人工复核的崩溃或数据风险；真实页面脚本滚动、滚动条视觉、触摸/键盘、resize/旋转/DPI 和嵌套 overflow 仍进入既有人工累计清单。自动门只证明 page-level 坐标、clamp、反向同步、事件去重和 callback 不可重入。
- next685 的 TEST1130 是离线 Core/Browser 几何夹具，没有新增必须立即人工复核的崩溃或数据风险；真实页面的容器边距、复杂定位、transform、嵌套 overflow、滚动和不同 DPI 视觉仍进入既有人工累计清单。自动门只证明有限整数 border-box、viewport scroll 偏移和 DPI 坐标边界。

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
- Browser selector 目前只覆盖简单 compound selector 的顶层列表、后代/子代/兄弟关系、六类属性操作符和有限结构伪类；TEST1152–1154 已验证 64 步上限、属性/伪类参数保护、受限 `an+b` 公式和非法输入回退，但动态状态、`:not()`、伪元素、属性大小写修饰符、namespace、shadow DOM 和完整 CSS Selectors 语法仍未实现。新增 bootstrap 使浏览器脚本有界 heap ceiling 为 710 KiB，复杂页面仍可能触及资源预算。
- 多窗口、持久 history、完整下载/外部协议策略仍属于宿主或未实现范围。
- mbed TLS 2.16.12 等依赖为旧平台兼容 pin，发布前必须审查当前安全风险。
- OEM SIP/IME、系统 picker、视觉和旋转不能仅凭 synthetic 自动测试保证。

完整列表见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 唯一下一步：next710

next709 已在 Browser bootstrap 中补齐有限结构伪类：TEST1154、`1152-1154,999`
定向设备门验证了 root/empty/child/of-type/nth 关系、受限 `an+b` 公式、列表顺序、
`matches()`/`closest()` 和非法输入 fail-closed。下一步先从 compatibility corpus、
源码、日志或截图固定另一个真实缺口，再选择一个边界清楚的离线 fixture 或稳定哨兵。
实现必须把可复用语义放在正确的公共 DLL，宿主只做平台接线、调度、应用策略和断言；
不要把互不相关的能力拆成只增加编号的提交，也不要在没有证据时扩大 ABI。完整滚动
容器树、Range/Selection、pinch zoom、transforms、scroll-margin、平滑/惯性滚动、
完整媒体查询语法、bfcache 和视觉差异仍是限制，不应在下一步中被误写成已支持。

优先场景应同时满足：

1. 可在仓库内离线固定主要 HTML/CSS/交互，避免网络内容漂移；
2. 对应一个真实页面或用户操作，而不是孤立 getter/setter；
3. 由源码、日志或截图先证明边界，且暴露当前限制中的一个核心流程缺口；
4. 通用语义进入公共 DLL，宿主只保留平台接线；
5. 可以自动断言主要结果，人工部分只保留无法机器判断的视觉/输入风险。

## 下一步完成标准（next710）

- 先用 compatibility corpus、源码、日志或截图固定一个真实页面/交互组合缺口，并把最小可重复 fixture 或哨兵写入测试入口；
- 可复用的 URL/history/DOM/Event/资源/布局/生命周期语义位于对应公共 DLL，`test_host` 只负责 WM 接线、调度和 fixture，不新增业务所有权；
- 自动断言覆盖该纵向能力的成功、失败/取消、资源清理和直接相邻旧路径，且不会削弱 next685–707 的布局 relation、布局尺寸、元素滚动、`getBoundingClientRect()`/`getClientRects()`、DPI 换算、history snapshot、宿主 clamp/apply、scroll restoration、beforeunload、脚本任务检查点、窗口焦点、activeElement、focus/blur 请求、autofocus、page-level/nested scrollIntoView 或旧页保留契约；
- C89 回归、VS2008 ARMV4I 正式构建、同批 staging、仓库审计和风险相称的设备门均通过，无旧 EXE/DLL 混包；
- 定向门及直接相邻回归唯一 `TESTBENCH PASS`、零 `ERROR`/`FAIL`，视觉、触摸、SIP/IME、picker 或旋转风险进入人工累计清单；
- handoff 覆盖为 next709 快照，ROADMAP 只保留当前尚未完成的纵向能力。

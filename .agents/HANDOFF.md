# 当前交接

本文件只描述当前产品基线、最近有效证据、未决风险和唯一下一步。逐批实现过程由 Git 历史保存，历史事故见 `docs/history/`，未来方向见 `ROADMAP.md`。

## 项目使命

Positron 为 Windows Mobile 6 / Windows CE 5.2 ARMV4I 提供模块化 TLS、JSON、HTTP、图像、脚本、渲染与浏览器会话 DLL。公共边界保持 C ABI、UTF-8、opaque handle 和显式所有权；`test_host.exe` 只是回归宿主与示例消费者。

## 当前 Git 与工作区

- 分支：`main`。next726 的 Core 显式 form-owner 生命周期、TEST1171 夹具和职责文档已
  完成；本批把共享 owner/traversal 放入 `positron_core`，`test_host` 只新增 fixture/
  断言；`tmp/` 中的本地证据未纳入版本控制。
- `TEST_MAX_NUMBER` 已为 1171。tracked `test_host/test_host.ini` 仍是窄 smoke：
  `auto=1`、`javascript=0`、选择 `13,20,27,56,58,62,64-67,73,75,999`；nightly/device
  tooling 从源码 dispatch 动态生成全量清单。
- 2026-09-02 nightly 已使用 `laptop-li\joe` 的 Windows keyring 成功覆盖固定
  `nightly` pre-release（源提交 `989c3276`、Debug、19 个不压缩条目）。受限 Codex 进程
  可能以 `laptop-li\codexsandboxoffline` 身份运行，即使用户目录仍显示为 Joe，也看不到
  该 keyring；这种进程中的 `gh auth status` 为 invalid 不是用户登录状态的可靠证据。发布
  前应在 keyring 可见的用户上下文核对身份和 `gh auth status`；失败经过见
  [`FAILED_EXPERIMENTS.md`](FAILED_EXPERIMENTS.md)。
- 设备门继续假定用户已在 WMDC/Device Emulator GUI 手动连接恰好一个目标；RAPI 只复用
  当前会话，不连接、选择、cradle、重置或强杀设备。

## 近期已完成能力摘要

- next698–706 已完成 Browser/Core 的有限滚动、inline 几何、布局尺寸、nested reveal、
  focus 和 autofocus 组合；对应语义由公共 DLL 拥有，宿主只做 Core relation、WM/native
  接线、clamp、绘制和生命周期调度。TEST1143–1151 已覆盖这些路径的离线合同与定向门；
  完整滚动树、Range/Selection、transform、pinch zoom、平滑/惯性滚动、完整焦点算法和
  OEM 视觉仍属于已知限制，具体边界见 `docs/TESTING.md`。

- next707–721 已在 `positron_browser.dll` 的同一 selector 解析器中形成有界子集：
  `matches()`、`closest()`、`querySelector()` 与 `querySelectorAll()` 支持顶层列表、
  后代/子代/兄弟组合器、六类属性操作符、`:root`/`:empty`、child/of-type、受限
  `nth-*`，以及 `input:checked`、Core effective-disabled relation 驱动的
  `:disabled`/`:enabled` 和直接
  `required` 的 `:required`/`:optional`，option live selected 的 `:checked`，通过 validation
  callback 的 `:valid`/`:invalid`、有界范围验证的 `:in-range`/`:out-of-range`，通过
  activeElement callback 的 `:focus`/`:focus-within`，
  以及只按 `<a>`/`<area>` `href` 属性判断的静态 `:link`/`:any-link`，当前 URL fragment
  解码后与元素非空 `id` 对齐的 `:target`，沿最多 64 层父链继承语言的 `:lang()`，以及
  最多 16 个后代/子代/兄弟相对分支的 `:has()`；注册 interaction callback 后还可读取
  Core 当前精确节点的 `:active`/`:hover`；文本输入类型与 `textarea` 还可按
  readonly/effective-disabled 与可选 contenteditable callback 匹配 `:read-only`/
  `:read-write`，并可按空 live value 与非空 placeholder 匹配有界
  `:placeholder-shown`。Browser 拥有引号内分隔符保护、64 步遍历上限、参数校验和
  fail-closed 规则；TEST1152–1169 只提供 fixture
  与断言，未把语义放回宿主。表单状态通过 Core effective-disabled relation
  统一 fieldset first-legend exemption 与 optgroup→option 继承；`:not()`
  只接受一个不含伪类、伪元素、列表或组合器的简单 compound 参数，`:has()` 只接受
  单一相对关系加简单 compound；
  为容纳 bootstrap，会话 heap ceiling 固定为 714 KiB；`:target` 不负责 fragment reveal
  或视觉滚动，stale wrapper 读取安全不匹配。

- next678 已把候选 generation、取消请求、退休状态、提交资格和 committed/failed 终态迁入 `positron_browser.dll` 的 opaque handle。next679 进一步把 pending/committed/failed/cancelled/stale 结果分类作为只读 Browser 摘要；next680 再提供独立 candidate/resource 的只读提交组合快照；next681 增加提交后 cleanup snapshot 和宿主回收观测；next682 将 history entry 的 viewport snapshot 迁入 Browser，并移除宿主的按 entry 滚动数组。宿主仍拥有 worker、response、资源事务、WM 消息、退休队列、窗口滚动应用和页面 swap，不把线程、窗口、网络或 Core document 带入 Browser ABI。
- Browser 现在同时拥有 URL 去重、role/policy、资源字节、终态、失败分类、重试预算、commit gate、hash-only 摘要、fallback 计数、候选 admission 状态、候选结果分类、candidate/resource 组合 decision 和 cleanup snapshot；宿主只保留 URL→resource-index 短引用、candidate handle 和平台调度状态，并消费结果快照写日志。清理快照复制完整有界 resource observation，要求 pending 工作先收敛，committed candidate 还必须配 READY gate。
- Browser script session 现在可注册 page-level scroll callback：脚本 `scrollTo`/`scrollBy` 请求由宿主 clamp/apply 并返回实际坐标，宿主的 scrollbar、触摸、键盘、resize 或 fragment reveal 路径可用 `PBrowser_ScriptSessionNotifyScroll` 同步脚本偏移；同步通知去重 `scroll` 事件，且脚本 callback 内不会递归进入 runtime。候选 session 在提交前只回显坐标，不改变旧页。
- Browser script session 还提供 `PBrowser_ScriptSessionNotifyResize`：宿主在 WM_SIZE 完成 Core style/layout、page-level clamp 和 native child reposition 后传入 CSS viewport 宽高/DPR；Browser 更新 `innerWidth`/`outerWidth`/`devicePixelRatio`、`screen` 宽高/方向，刷新每个 session 最多 64 个 `matchMedia()` 列表，并在匹配翻转时先同步派发 `change`、再派发去重的 window `resize`。它不触发 Core layout，也不自动运行 timer/animation frame；超过 64 个列表只保留创建时快照。
- Browser script session 还提供布局视口对应的 `visualViewport`：`width`/`height` 与 viewport 同步，`pageLeft`/`pageTop` 与 page scroll 同步，`scale` 固定为 1，`offsetLeft`/`offsetTop` 固定为 0；有效 resize/scroll 按 visual viewport、window 顺序同步派发并去重。它不模拟 pinch zoom、视觉 viewport 偏移或 nested overflow。
- Browser script session 还提供由宿主驱动的顶层窗口焦点合同：`PBrowser_ScriptSessionDispatchWindowFocus` 归一化激活值，更新 `document.hasFocus()`，并在实际变化时按属性 handler、listener 顺序派发可信的非冒泡 focus/blur；重复值保持静默。参考宿主已把 `WM_ACTIVATE` 接到该入口，但 native 控件焦点、焦点矩形和 OEM/跨窗口策略仍由宿主负责。next695 增加的 `PBrowserScriptActiveElementCallbacks` 是显式可选桥：宿主把 Core 的当前焦点 id 提供给 Browser，getter 解析有效 id，否则回退 `document.body`；未注册 callback 的 session 不承诺安装该属性。next696 增加的 `PBrowserScriptFocusRequestCallbacks` 只把带 id 的 `focus()`/`blur()` 请求同步交给宿主；next697 通过新增 Ex callback 传递 `prevent_scroll` 和实际 CSS page scroll 结果，默认 focus 可 reveal 到 page-level viewport；next705 又让 Browser 在发现 retained overflow ancestor 时复用 Ex 的有效 `prevent_scroll`，并在 callback 后完成有界嵌套 reveal；宿主仍用 Core 的资格/焦点 API、native HWND 和已有事件接线完成事务，disabled/hidden/stale/重复请求安全 no-op。
- Browser script session 还提供统一的 `PBrowser_ScriptSessionRunTaskCheckpoint`：宿主用 `phase_mask` 选择 timer、animation frame、message 和 idle callback，Browser 按固定顺序执行并在每个阶段后运行一次有界 microtask；宿主提供单调时钟、frame timestamp、idle deadline、message limit 和 UI 消息循环。参考宿主已通过 16 ms `WM_TIMER` 接入，Browser 不创建线程或接管宿主调度。
- Browser script session 的页面生命周期还覆盖初始 `complete` 后只派发一次的 `pageshow`，以及 hidden→visible 的 `visibilitychange`→`pagehide`/`pageshow` 顺序；重复 complete 或重复 hidden 值保持静默，有限 page event 的 `persisted` 固定为 `false`，因为没有 bfcache。宿主在每次 `WM_ACTIVATE` 时调用 `PBrowser_ScriptSessionDispatchWindowFocus`，Browser 维护 `document.hasFocus()` 并在状态变化时派发可信的 window focus/blur；重复状态保持静默。
- Core 通过既有 DOM relation callback 暴露当前 layout border-box 的 `x`、`y`、`width`、`height` 四个整数 CSS 像素分量；Browser 的 `Element.getBoundingClientRect()` 和有界 `Element.getClientRects()` 组合 viewport-relative 矩形并扣除 CSS page scroll。宿主把 Core 的物理滚动坐标与 Browser 的 CSS page 坐标在当前 DPI 边界换算，避免高 DPI 下重复放大或缩小。
- Core 的 `PCore_InteractionFocusElementId` 与 `PCore_InteractionStateElementId` 以
  size-probe/固定容量合同复制当前 focus/active/hover 节点的非空 UTF-8 id；无状态、无 id、
  过时节点、非法组合或过小缓冲会 fail closed，不改变 DOM、style、layout 或交互状态。
  `PCore_FocusTargetInfoById` 与 `PCore_InteractionFocusById` 为宿主提供按 id 的已布局资格
  检查和 Core focus node 更新；next706 又增加 `PCore_AutofocusTargetInfo`、
  `PCore_InteractionFocusAutofocus` 与目标保持的 `PCore_EventDispatchFocus`，让宿主在
  layout/native 子控件完成后显式选择第一个合格 `autofocus` 目标并覆盖无 id 事件，而不切换
  职责边界。Browser 通过可选 `PBrowserScriptActiveElementCallbacks` 把可寻址 id 投影为
  `document.activeElement`，通过 `PBrowserScriptInteractionCallbacks` 投影 Core 当前
  active/hover id 为 `:active`/`:hover`，通过旧/Ex focus request callback 接收脚本请求；
  Ex 结果可在 callback 返回后同步 page-level scroll，无效来源回退到 `document.body`，
  不可用目标 no-op。
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
- next707–721 已完成 Browser selector 的列表/组合器/属性操作符/结构伪类/表单状态/
  有界 `:not()`、validation `:valid`/`:invalid`、activeElement 驱动的
  `:focus`/`:focus-within`、静态 `:link`/`:any-link`、当前 fragment 对齐元素 id 的
  `:target`、沿父链继承语言的 `:lang()`、正向分组 `:is()`/`:where()` 以及相对分支
  `:has()` 纵向能力；next721 还以 Core effective-disabled relation 统一 fieldset
  first-legend exemption、optgroup→option 继承、selector 状态、选项选择和提交过滤。
- next722 在同一 Browser selector 解析器中增加了 Core validation 驱动的有界
  `:in-range`/`:out-of-range`。仅支持带非空值和约束的 input
  number/range/date/month/week/time/datetime-local（range 默认范围也算）；
  underflow/overflow 决定 out-of-range，stepMismatch 单独仍为 in-range，空值、bad/type
  mismatch、disabled/readonly、无范围限制和非 input fail closed。TEST1167 覆盖
  Core flags、matches/closest/query、live mutation 和非法输入；`1063,1166-1167,999`
  定向门已通过。不要仅为增加编号拆分提交，也不要把页面语义放回 `test_host`。
- next723 在同一 Browser selector 解析器中增加了有界 `:read-only`/`:read-write`：文本
  输入类型与 `textarea` 排除 readonly/effective-disabled，显式或继承的 contenteditable
  在 Core callback 存在时可匹配；不支持编辑的控件和普通元素按 `:read-only` 处理。
  TEST1168 覆盖属性 mutation、contenteditable 祖先继承、matches/closest/query、查询
  顺序、callback 注销和非法 selector；`1063,1167-1168,999` 定向门已通过。
- next724 在同一 Browser selector 解析器中增加了有界 `:placeholder-shown`：省略 `type`
  或使用 text-like input 类型的 input，以及 textarea，在 live value 为空且 placeholder
  值非空时匹配；value/type/placeholder mutation、matches/closest/query 顺序和非法输入
  由 TEST1169 覆盖。Browser bootstrap heap ceiling 为 714 KiB 以容纳该 bootstrap。
- next725 修复了 Core 的显式 form-owner 解析：支持的 input、select、textarea、button
  在存在 `form="id"` 时归属于文档中对应的 form，空值或无效目标不回退到祖先；
  `form.elements` 现在从整棵文档按顺序收集显式跨树控件，仍由 Browser 生成有界 snapshot。
  TEST1170 覆盖默认/显式/无效归属、集合顺序、namedItem、label association、mutation
  后重查和旧 snapshot 保持；`1169,1170,999` 定向设备门已通过。
- next726 让同一 owner 解析贯穿 Core 的 validation、reportValidity invalid-event 扫描、
  urlencoded/multipart successful-control 构建、dialog/default-submit、reset 和按坐标的
  submit/reset 激活；新增共享文档遍历和 TEST1171，覆盖 form 外 input/textarea/button、
  required 阻止与恢复、提交顺序及 reset 初值恢复。`1170,1171,999` 定向设备门已通过。
- 当前唯一下一步是 next727：重新检查 compatibility corpus、源码、设备日志和截图，
  固定一个新的用户可见缺口，再选择一个边界清楚的公共 DLL 纵向能力。

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
- Core 的 form owner relation 对支持的 input、select、textarea、button 解析最近祖先或
  显式 `form="id"` 目标；Browser 的 `Element.form` 与 `form.elements` 复用这条规则，
  后者从整棵文档按顺序返回跨树控件的有界 snapshot，空值/无效目标不回退祖先。Core
  validation、reportValidity、successful-control/multipart submission、dialog/default-
  submit、reset 和按坐标的 submit/reset 激活也复用同一 owner。
- 支持的链接、summary、native EDIT/SELECT/button/file 等目标，以及带有效非负 `tabindex` 的普通布局元素按有界顺序响应 Tab/Shift+Tab：正值升序、同值 DOM 稳定排序，随后零/缺省组；负值、disabled/hidden/stale 目标和 file picker 仍被排除。Browser 报告活动 modal id 后，宿主可用 Core 的 scoped snapshot 将顺序焦点限制在 dialog 子树；宿主仍同步焦点事件、原生焦点和滚动可见性。
- `<dialog>` 的 show/showModal/close/requestClose、returnValue、cancel/close 事件、活动 modal id 查询、宿主驱动的 Escape 请求桥接、有界 backdrop 指针策略、`method="dialog"` 默认动作和 Core modal paint 已形成契约。显式点击、脚本 `click()` 和单行输入隐式 Enter 都遵循 validation→可取消 submit→直接 close/returnValue；CSS `::backdrop`、透明合成、多个 modal 和跨文档 modal 仍未实现。
- 单元素 `contenteditable` 已形成 Core/Browser/宿主边界：Core 解析祖先继承并限制合法 UTF-8 纯文本 mutation，Browser 暴露 `isContentEditable`/`innerText`、`selectionStart`/`selectionEnd`/`selectionDirection` 与 typed input 事务，宿主为带 id 且已布局的有效 editing host 创建有界 WM multiline EDIT 代理。允许的 `WM_CHAR` 在默认处理完成后回读最终文本并派发 `input`；取消的 `beforeinput` 不修改 Core，重复/提前 `EN_CHANGE` 不会制造空事件。Browser 选区偏移使用 UTF-16 code unit；宿主将 WM EDIT 的 CRLF 位置转换为逻辑 LF，并在可用时同步原生 HWND。无修饰鼠标拖选以及 Shift/方向键扩展由宿主短暂保存 anchor；捕获丢失、取消模式和焦点切换会先结束手势，再通过 Browser 的去重通知入口刷新范围。宿主对 `WM_PASTE`/`WM_CUT` 只接受有界 `CF_UNICODETEXT`，把规范化后的 UTF-8 data 交给 `beforeinput`，允许后执行 native default，再提交 Core/input 和折叠选区；`WM_COPY` 只写入非空的有界 Unicode 选区，折叠选区保持现有剪贴板不变；格式缺失、超长或读取失败时 fail closed。为兼容 WinCE 原生剪切的内部重入，宿主只在外层 `WM_CUT` 默认处理期间放行同一 HWND 的嵌套 `WM_COPY`。每页最多 16 个宿主、文本最多 8192 UTF-8 字节；嵌套继承后代不重复创建 host。
离线 compatibility corpus 已覆盖导航资源事务、候选提交与回滚、history/viewport、页面生命周期、脚本调度、焦点、滚动、Core/Browser 几何和显式 form-owner 组合。每项测试的 fixture 与断言说明统一见 [`docs/TESTING.md`](../docs/TESTING.md)；handoff 只保留当前门和仍未完成的边界。

### 当前测试入口

- `TEST_MAX_NUMBER`：1171。
- tracked `test_host/test_host.ini`：`auto=1`、`javascript=0`，选择 `13,20,27,56,58,62,64-67,73,75,999`。
- tracked INI 是窄 smoke，不是全量目录；nightly 打包脚本从源码 dispatch 动态生成全量自动清单。
- 设备连接必须先由用户在 WMDC/Device Emulator GUI 手动完成；RAPI gate 只使用当前唯一会话。

## 最新有效设备证据

当前最新产品门为 next726 的 Core 显式 form-owner 生命周期与 Browser/相邻关系回归：

- `tmp/device-runs/20260902-232736-next726/`；动态选择 `1170,1171,999`，3 项；3/3
  通过，零 `ERROR`/`FAIL`，唯一 `TESTBENCH PASS`。TEST1170 验证 Browser 的默认/显式/
  无效 form owner、跨树 controls 文档顺序、namedItem、label association、mutation 后重查
  和旧 snapshot；TEST1171 验证 Core validation、reportValidity、提交顺序、外部
  submit/reset 激活与初值恢复；TEST999 请求一次提示音。
- 设备：240x320，dpi=96；使用当前 WMDC GUI 会话、正式 Debug ARMV4I 构建和同批
  staging；RAPI 只复用 GUI 会话，不连接、选择、重置或杀死设备。
- 静态验证：`python scripts/test_c89ize.py`、正式 Debug ARMV4I 构建、同批 staging、
  `python scripts/audit_repo.py` 和 `git diff --check` 均已通过；Browser heap ceiling 为
  714 KiB，`PSCRIPT_MAX_NATIVE_FUNCTIONS` 为 27。

## 当前人工验收状态

以下路径已有过真实设备确认，但后续触及相邻基础设施时仍需重新评估：

- example.com → IANA 的容器边距、深层导航和旧页保留；
- SIP 候选词整词提交；
- bitmap/SVG、表格、列表和常见布局的可见结果；
- native EDIT/SELECT、真实 file picker、旋转和 DPI 路径。
- 带 `tabindex` 的普通元素的设备焦点矩形、触摸命中和不同 DPI 视觉仍需人工观察；语义顺序已有自动断言。
- `<dialog>` backdrop 的整体色彩、边界、滚动/旋转下的视觉仍属于可累计的人工观察；Core 的绘制顺序和设备门像素契约已有自动断言。
- contenteditable 的 OEM 硬键盘/自动重复、SIP/IME 候选词、跨应用剪贴板互操作、滚动/旋转和不同 DPI 下的文本视觉仍属于可累计人工风险；1113 已在真实 WM EDIT 上验证无修饰鼠标拖选的连续范围/方向通知，1114 验证了 Shift/方向键、捕获丢失和焦点切换的有界通知收尾，1112 覆盖脚本 `selectionchange` 去重，1115 覆盖宿主自备的 `CF_UNICODETEXT` paste/cut，1116 覆盖宿主 `WM_COPY` 与格式/容量拒绝。完整 ClipboardEvent/async clipboard、CF_TEXT/富文本转换仍不在契约内。
- TEST1117–TEST1171 都是离线自动夹具，无新增立即人工风险；真实视觉、触摸、旋转、SIP/IME、picker 和不同 DPI 继续进入累计清单。自动结果不替代真实网络恢复、OEM 控件或逐资源视觉验收。
- TEST1151 是离线的 Core/Browser autofocus 语义夹具，没有新增必须立即人工复核的崩溃或数据风险；真实初始焦点矩形、native HWND、滚动条裁剪、触摸/SIP、不同 DPI 和多窗口策略仍属于宿主集成观察，自动门只证明 DOM 顺序资格、size-probe、Core focus node、无 id 目标事件保持和 Browser body 回退合同。
- TEST1152–1165 是离线的 Browser selector 组合器、属性/结构伪类、表单验证、焦点、链接、
  fragment、语言、分组、`:has()` 和 pointer-interaction 夹具，无新增立即人工风险；自动门
  证明各自的有界查询、mutation、顺序、callback 边界和非法输入 fail-closed，真实页面的
  完整 Selectors、native 输入、触摸、布局视觉和不同 DPI 仍属宿主观察。
- TEST1166 是离线的 Core/Browser effective-disabled 夹具，无新增立即人工风险；native SELECT popup、触摸、视觉和不同 DPI 仍需宿主观察，自动门证明 relation、first-legend exemption、optgroup→option 继承、mutation、过小缓冲、disabled option 拒绝和提交排除。
- TEST1167 是离线的 Core/Browser range selector 夹具，无新增立即人工风险；原生范围控件视觉、本地化 validation UI、触摸和不同 DPI 仍需宿主观察，自动门证明范围状态映射、约束 mutation、查询顺序和非法输入回退。
- TEST1168 是离线的 Core/Browser editable selector 夹具，无新增立即人工风险；自动门证明
  `:read-only`/`:read-write` 对文本控件、readonly/effective-disabled、contenteditable
  祖先继承、属性 mutation、query/closest 和 callback 注销的有界映射。真实 native 编辑、
  SIP/IME、富文本、视觉和不同 DPI 仍进入累计人工清单。
- TEST1169 是离线的 Core/Browser placeholder selector 夹具，无新增立即人工风险；自动门证明
  `:placeholder-shown` 对 text-like input/textarea 的空 value、非空 placeholder、value/type/
  placeholder mutation、matches/closest/query 顺序和非法输入的有界映射。真实 native
  placeholder 绘制、SIP/IME、触摸、视觉和不同 DPI 仍进入累计人工清单。
- TEST1170 是离线的 Core/Browser form-owner 夹具，无新增立即人工风险；自动门证明
  最近祖先与显式 `form="id"` 归属、空值/无效目标不回退、跨树 `form.elements` 文档顺序、
  namedItem、label association、mutation 后重查和旧 snapshot 保持。真实 native 表单
  控件、SIP/IME、picker、触摸、视觉和不同 DPI 仍进入累计人工清单。
- TEST1171 是离线的 Core form-owner 生命周期夹具，无新增立即人工风险；自动门证明
  form 外显式控件参与 validation、`reportValidity()` invalid-event 扫描、urlencoded
  successful-control submission 和外部 submit/reset activation，reset 后初始值恢复且
  required invalid 再次出现。真实 native 表单控件、SIP/IME、picker、触摸、视觉和不同
  DPI 仍进入累计人工清单。
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
- Browser selector 仍是有界子集：支持列表/关系/属性/结构伪类、表单状态、focus/link/target/lang、`:not()`/`:is()`/`:where()`/`:has()`、可选 interaction 的 `:active`/`:hover`、Core validation 的 `:in-range`/`:out-of-range`、依据 readonly/effective-disabled 和可选 contenteditable callback 判定的 `:read-only`/`:read-write`，以及 text-like input/textarea 的 `:placeholder-shown`。TEST1152–1169 已覆盖这些路径的查询、mutation、预算和非法输入回退。范围伪类只接受非空且受约束的 input number/range/date/month/week/time/datetime-local，underflow/overflow 才构成 out-of-range；空值、bad/type mismatch、disabled/readonly、无范围限制、非 input 和单独 stepMismatch 安全不匹配。显式 contenteditable 在 callback 缺失或查询失败时两种编辑伪类都不匹配；placeholder 伪类不匹配空 placeholder、其他 input 类型、普通元素或带参数形式。完整 CSS Selectors、visited/伪元素/namespace/shadow DOM、`:has()` 链式关系、`:target` reveal 以及复杂页面的 714 KiB heap 预算边界仍未承诺；详细合同见 [`docs/TESTING.md`](../docs/TESTING.md)。
- 多窗口、持久 history、完整下载/外部协议策略仍属于宿主或未实现范围。
- mbed TLS 2.16.12 等依赖为旧平台兼容 pin，发布前必须审查当前安全风险。
- OEM SIP/IME、系统 picker、视觉和旋转不能仅凭 synthetic 自动测试保证。

Core/Browser form owner 目前只覆盖 input、select、textarea、button，并把显式 `form="id"`
控件按文档顺序加入有界 `form.elements` snapshot；validation、submission/multipart、
dialog/default-submit、reset 和按坐标的 submit/reset 激活也复用这条 owner 规则。完整
live collection、fieldset/object/image 等其他 form-associated 元素、复杂 parser 重构和
native 表单视觉仍未实现。

完整列表见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 唯一下一步：next727

next726 在 Core 中增加了共享的显式 form-owner 解析和整棵文档控制遍历：Browser 的
`Element.form`/`HTMLFormElement.elements` 与 Core 的 validation、reportValidity、
successful-control/multipart submission、dialog/default-submit、reset、submit/reset
激活现在使用同一条规则；TEST1171 固定 form 外 input/textarea/button 的 required 阻止、
提交顺序、外部按钮激活和 reset 初值恢复。`1170,1171,999` 定向设备门已通过；完整 live
collection、其他 form-associated 元素、native 表单视觉和设备差异仍未承诺。

下一批先从 compatibility corpus、源码、日志或截图固定另一个真实缺口，再选择一个边界
清楚的离线 fixture 或稳定哨兵。实现必须把可复用语义放在正确的公共 DLL，宿主只做平台
接线、调度、应用策略和断言；不要仅为增加编号拆分提交，也不要在没有证据时扩大 ABI。
完整滚动容器树、Range/Selection、pinch zoom、transforms、scroll-margin、平滑/惯性滚动、
完整媒体查询语法、bfcache 和视觉差异仍是限制，不应在下一步中被误写成已支持。

优先场景应同时满足：

1. 可在仓库内离线固定主要 HTML/CSS/交互，避免网络内容漂移；
2. 对应一个真实页面或用户操作，而不是孤立 getter/setter；
3. 由源码、日志或截图先证明边界，且暴露当前限制中的一个核心流程缺口；
4. 通用语义进入公共 DLL，宿主只保留平台接线；
5. 可以自动断言主要结果，人工部分只保留无法机器判断的视觉/输入风险。

## 下一步完成标准（next727）

- 先用 compatibility corpus、源码、日志或截图固定一个真实页面/交互组合缺口，并把最小可重复 fixture 或哨兵写入测试入口；
- 可复用的 URL/history/DOM/Event/资源/布局/生命周期语义位于对应公共 DLL，`test_host` 只负责 WM 接线、调度和 fixture，不新增业务所有权；
- 自动断言覆盖该纵向能力的成功、失败/取消、资源清理和直接相邻旧路径，且不会削弱 next685–724 的布局 relation、布局尺寸、元素滚动、`getBoundingClientRect()`/`getClientRects()`、DPI 换算、history snapshot、宿主 clamp/apply、scroll restoration、beforeunload、脚本任务检查点、窗口焦点、activeElement、focus/blur 请求、autofocus、page-level/nested scrollIntoView、selector 组合器/属性/结构伪类/表单状态/`:not()`/`:is()`/`:where()`/`:has()`/`:valid`/`:invalid`/`:in-range`/`:out-of-range`/`:focus`/`:focus-within`/`:link`/`:any-link`/`:target`/`:lang`/`:active`/`:hover`/`:read-only`/`:read-write`/`:placeholder-shown`、effective-disabled relation 或旧页保留契约；
- C89 回归、VS2008 ARMV4I 正式构建、同批 staging、仓库审计和风险相称的设备门均通过，无旧 EXE/DLL 混包；
- 定向门及直接相邻回归唯一 `TESTBENCH PASS`、零 `ERROR`/`FAIL`，视觉、触摸、SIP/IME、picker 或旋转风险进入人工累计清单；
- next727 完成后 handoff 应覆盖为 next727 快照，ROADMAP 只保留当前尚未完成的纵向能力。

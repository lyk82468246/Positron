# 当前交接

本文件只描述当前产品基线、最近有效证据、未决风险和唯一下一步。逐批实现过程由 Git 历史保存，历史事故见 `docs/history/`，未来方向见 `ROADMAP.md`。

## 项目使命

Positron 为 Windows Mobile 6 / Windows CE 5.2 ARMV4I 提供模块化 TLS、JSON、HTTP、图像、脚本、渲染与浏览器会话 DLL。公共边界保持 C ABI、UTF-8、opaque handle 和显式所有权；`test_host.exe` 只是回归宿主与示例消费者。

## 当前 Git 与工作区

- 分支：`main`。next672–687 的过时导航、资源终态/重试、提交门、摘要观测、资源事务所有权、候选生命周期所有权、candidate/resource 提交组合、提交后清理观测、Browser-owned history viewport snapshot、page-level 横向 viewport、脚本滚动视口桥接、布局几何桥接、viewport resize 通知和动态 `matchMedia()` 更新均已提交。
- next678 已把候选 generation、取消请求、退休状态、提交资格和 committed/failed 终态迁入 `positron_browser.dll` 的 opaque handle。next679 进一步把 pending/committed/failed/cancelled/stale 结果分类作为只读 Browser 摘要；next680 再提供独立 candidate/resource 的只读提交组合快照；next681 增加提交后 cleanup snapshot 和宿主回收观测；next682 将 history entry 的 viewport snapshot 迁入 Browser，并移除宿主的按 entry 滚动数组。宿主仍拥有 worker、response、资源事务、WM 消息、退休队列、窗口滚动应用和页面 swap，不把线程、窗口、网络或 Core document 带入 Browser ABI。
- Browser 现在同时拥有 URL 去重、role/policy、资源字节、终态、失败分类、重试预算、commit gate、hash-only 摘要、fallback 计数、候选 admission 状态、候选结果分类、candidate/resource 组合 decision 和 cleanup snapshot；宿主只保留 URL→resource-index 短引用、candidate handle 和平台调度状态，并消费结果快照写日志。清理快照复制完整有界 resource observation，要求 pending 工作先收敛，committed candidate 还必须配 READY gate。
- Browser script session 现在可注册 page-level scroll callback：脚本 `scrollTo`/`scrollBy` 请求由宿主 clamp/apply 并返回实际坐标，宿主的 scrollbar、触摸、键盘、resize 或 fragment reveal 路径可用 `PBrowser_ScriptSessionNotifyScroll` 同步脚本偏移；同步通知去重 `scroll` 事件，且脚本 callback 内不会递归进入 runtime。候选 session 在提交前只回显坐标，不改变旧页。
- Browser script session 还提供 `PBrowser_ScriptSessionNotifyResize`：宿主在 WM_SIZE 完成 Core style/layout、page-level clamp 和 native child reposition 后传入 CSS viewport 宽高/DPR；Browser 更新 `innerWidth`/`outerWidth`/`devicePixelRatio`、`screen` 宽高/方向，刷新每个 session 最多 64 个 `matchMedia()` 列表，并在匹配翻转时先同步派发 `change`、再派发去重的 window `resize`。它不触发 Core layout，也不自动运行 timer/animation frame；超过 64 个列表只保留创建时快照。
- Core 通过既有 DOM relation callback 暴露当前 layout border-box 的 `x`、`y`、`width`、`height` 四个整数 CSS 像素分量；Browser 的 `Element.getBoundingClientRect()` 组合 viewport-relative 矩形并扣除 CSS page scroll。宿主把 Core 的物理滚动坐标与 Browser 的 CSS page 坐标在当前 DPI 边界换算，避免高 DPI 下重复放大或缩小。
- 上一产品基线为 `88d68ebd`（next669，首个离线 compatibility corpus 完整流程）；更早基线为 `c0c4ba0e`（next668，单元素 `contenteditable` 的受限 CF_UNICODETEXT 粘贴/剪切与 WM_COPY 边界）。
- Core 现在报告稳定的有效表单方法常量，并为显式 submitter 或单行输入隐式提交解析最近祖先 dialog id 与 submitter value。Browser 提供按 id 直接执行 `dialog.close(value)` 的会话边界；参考宿主只在 validation 和可取消 `submit` 均允许后调用它，不生成网络导航，也不错误派发 `cancel`。Core 还提供 `PCore_PaintDocumentWithModal`：普通文档绘制后覆盖有界实体色 backdrop，并按 Browser 的活动 id 重绘已打开的 dialog；next658 的 backdrop 指针策略和此前的 modal 焦点/Escape 边界保持不变。
- `tmp/` 保存本地设备日志和截图，不跟踪。

## 当前中期里程碑

在保持 VS2008/WM6 约束的前提下，把已经形成的 Core、Browser 和平台宿主能力整合为可由真实页面驱动的有界网页运行时。重点是完成用户可感知的纵向能力、把通用语义留在公共 DLL，并以小型真实页面/交互语料库防止只增加孤立 API。

## 当前短期目标

next685 已完成一个由真实页面脚本证据固定的布局几何纵向能力：Core 提供有限 border-box relation，Browser 提供 `getBoundingClientRect()`，宿主负责当前 DPI 下的 CSS/物理滚动换算。next686 已补齐同一页面组合所需的 viewport resize：Browser 动态 viewport metadata 与 window `resize`，宿主负责 WM_SIZE 后的 DPI 换算和通知；next687 在此基础上补齐已有 `MediaQueryList` 的动态 viewport/DPR 刷新与 change 事件，TEST1129、TEST1130、TEST1131、TEST1132 覆盖直接相邻路径。当前唯一下一条纵向能力是 next688：从源码、日志或截图选择另一个尚未覆盖的用户可见组合缺口。

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
- Browser 层还提供由宿主显式驱动的 viewport resize 合同：`PBrowser_ScriptSessionNotifyResize` 更新 CSS viewport/DPR 和动态 `screen` 方向，值变化时同步派发一次 window `resize`；调用不负责 Core relayout 或 frame scheduling。
- Browser history entry 同时拥有非负的 `(scroll_x, scroll_y)` viewport snapshot；新 document entry 和同 URL 新 document 从零开始，`replaceState`/traversal 保留目标值，`pushState` 新 entry 从零开始，history 裁剪会同步搬移 snapshot。Browser 不访问窗口、不知道 Core 的页面 extent；宿主读取 `PCore_DocumentWidth/Height` 后保存/读取并对两个轴 clamp/apply。
- Browser script session 的 `window.scrollTo`/`scrollBy` 经过 `PBrowserScriptScrollCallbacks` 交给活动宿主；宿主返回实际 page 坐标后，Browser 只派发一次 `scroll`。宿主的物理滚动路径用 `PBrowser_ScriptSessionNotifyScroll` 反向同步，重复坐标不派发事件，回调内不会重入 runtime。
- Browser script session 的 scroll callback 和 `PBrowser_ScriptSessionNotifyScroll` 均使用 CSS page 坐标；宿主在调用 Core 的物理滚动、绘制、命中测试和滚动条路径时负责当前 DPI 的双向换算。重复坐标不派发事件，回调内不会重入 runtime。
- Core 的布局 relation 在成功 layout 后提供单元素 border-box 的整数 CSS 像素快照；Browser 用它生成 viewport-relative `getBoundingClientRect()`，再扣除当前 CSS page scroll。未布局、无对应 box 或不满足边界时返回全零/不可用结果；不承诺 transforms、nested overflow 或多片段 union。
- Core 的 `PCore_DocumentWidth` 与 `PCore_DocumentHeight` 在最近一次 layout 后报告 page-level extent；宽度包含页面内容的水平溢出且不小于 layout viewport。宿主把同一 `(scroll_x, scroll_y)` 用于 paint、命中测试、fragment reveal、滚动条和 native child reposition；嵌套 overflow 容器的独立滚动仍未实现。
- 页面导航保留旧页到候选文档成功提交；主文档和资源网络阶段与 UI 文档操作分离。Browser candidate handle 拥有 generation、取消/退休、提交资格和结果分类，宿主用它门控 worker 完成/进度消息并在 worker 收尾后回收旧候选；旧候选不能越过 generation 门。layout/swap 前，宿主通过 `PBrowser_NavigationCommitGetInfo` 读取独立 candidate/resource 的组合 decision 与 `can_commit`，不在宿主复制失败/过时提交规则。
- Browser 资源事务按 URL 去重并拥有 `pending`、`ready`、`failed`、`cancelled` 四种终态、成功字节、失败分类、required/optional gate、transport 重试预算、最多 4 项 hash-only 摘要和 fallback family 计数。宿主负责网络 I/O、worker、取消/重试时机和页面提交，只保留 URL→resource-index 短引用；HTTP、resolve、budget、memory 和取消不重试，取消也不会重新暴露为可用缓存。
- 深层 DOM 资源准备使用单个事务级 heap scratch，避免大批固定栈缓冲耗尽 WM6 线程栈。
- 导航 request 在 worker join 后由宿主先收敛失败/过时资源，再调用 Browser 的 `PBrowser_NavigationCleanupGetInfo` 复制 cleanup decision、candidate/resource 终态、pending、`can_release`、hash-only failure summary 和 fallback 计数；复制值在 candidate/resource handle 销毁后仍可用于日志。TEST1127 同时覆盖 pending/terminal decision、required failure、optional fallback、取消、stale、清理前复制、释放后快照存活，以及成功/失败 `pcore_navigation_finish` 的真实回收路径。
- `<details>/<summary>` 支持 click 与 Enter/Space 激活、取消和 DOM 状态同步。
- 支持的链接、summary、native EDIT/SELECT/button/file 等目标，以及带有效非负 `tabindex` 的普通布局元素按有界顺序响应 Tab/Shift+Tab：正值升序、同值 DOM 稳定排序，随后零/缺省组；负值、disabled/hidden/stale 目标和 file picker 仍被排除。Browser 报告活动 modal id 后，宿主可用 Core 的 scoped snapshot 将顺序焦点限制在 dialog 子树；宿主仍同步焦点事件、原生焦点和滚动可见性。
- `<dialog>` 的 show/showModal/close/requestClose、returnValue、cancel/close 事件、活动 modal id 查询、宿主驱动的 Escape 请求桥接、有界 backdrop 指针策略、`method="dialog"` 默认动作和 Core modal paint 已形成契约。显式点击、脚本 `click()` 和单行输入隐式 Enter 都遵循 validation→可取消 submit→直接 close/returnValue；CSS `::backdrop`、透明合成、多个 modal 和跨文档 modal 仍未实现。
- 单元素 `contenteditable` 已形成 Core/Browser/宿主边界：Core 解析祖先继承并限制合法 UTF-8 纯文本 mutation，Browser 暴露 `isContentEditable`/`innerText`、`selectionStart`/`selectionEnd`/`selectionDirection` 与 typed input 事务，宿主为带 id 且已布局的有效 editing host 创建有界 WM multiline EDIT 代理。允许的 `WM_CHAR` 在默认处理完成后回读最终文本并派发 `input`；取消的 `beforeinput` 不修改 Core，重复/提前 `EN_CHANGE` 不会制造空事件。Browser 选区偏移使用 UTF-16 code unit；宿主将 WM EDIT 的 CRLF 位置转换为逻辑 LF，并在可用时同步原生 HWND。无修饰鼠标拖选以及 Shift/方向键扩展由宿主短暂保存 anchor；捕获丢失、取消模式和焦点切换会先结束手势，再通过 Browser 的去重通知入口刷新范围。宿主对 `WM_PASTE`/`WM_CUT` 只接受有界 `CF_UNICODETEXT`，把规范化后的 UTF-8 data 交给 `beforeinput`，允许后执行 native default，再提交 Core/input 和折叠选区；`WM_COPY` 只写入非空的有界 Unicode 选区，折叠选区保持现有剪贴板不变；格式缺失、超长或读取失败时 fail closed。为兼容 WinCE 原生剪切的内部重入，宿主只在外层 `WM_CUT` 默认处理期间放行同一 HWND 的嵌套 `WM_COPY`。每页最多 16 个宿主、文本最多 8192 UTF-8 字节；嵌套继承后代不重复创建 host。
- 离线 compatibility corpus 的第一条完整流程已加入 TEST1117：固定 HTML/CSS 在无网络条件下串联 contenteditable 取消与提交、selectionchange、required dialog validation、`method="dialog"` close/returnValue、same-document fragment 导航、跨 origin 候选回滚和 history back。几何、状态、事件、导航和失败不变性都由自动断言覆盖；它是回归夹具，不改变公共 ABI，也不把页面语义移入宿主。
- TEST1118 将第二条离线候选流程接入公共 Browser/Core 路径：重复外链 script 与 SVG 资源只准备一次；503 候选保持旧 document/session、旧 timer 和队列；200 候选提交前派发一次 teardown，提交后 resource cache、load 结果与 history 均由自动断言核对。夹具完全离线，不把网络端点或测试语义写进公共 ABI。
- TEST1119 覆盖资源准备期间的新旧候选交错、generation 取消、过时进度/完成消息隔离、退休请求回收和最新候选提交。
- TEST1120 固定 ready、HTTP、transport、budget 和 cancelled 五种资源结果，自动断言 Browser 资源事务的终态计数、错误分类、缓存回调可见性和有界资源字节。
- TEST1121 固定一次可恢复 transport 失败、一次重试预算耗尽，以及 HTTP、resolve、budget、cancelled 的不可重试结果；自动断言 Browser 事务的 attempts/retries、最终分类、缓存可见性和有界计数。
- TEST1122 固定必需 stylesheet 失败与可选 image 失败：前者必须保留旧 document/history 且不触发提交，后者允许候选提交并通过 Browser gate 记录 optional failure 与 Core fallback 结果。
- TEST1123 固定重复 script/image、三层 `@import` 和混合 optional 失败：自动断言 Browser 按 URL 去重、role 升级、最多 4 项 hash-only 摘要、旧 URL/正文不泄露，以及 layout 后 Core image fallback 与 skipped script 的粗粒度观测。宿主只提供网络/worker 夹具和日志适配。
- TEST1124 固定三个 Browser candidate handle：自动断言 active generation admission、取消后不可提交、退休幂等、stale generation 隔离以及 committed/failed 终态不可复用。
- TEST1125 固定 Browser candidate result snapshot：自动断言 pending、committed、failed、cancelled 和 stale 分类，取消请求不会提前结束，终态不会因后续 generation 改写。
- TEST1126 固定 Browser candidate/resource commit snapshot：自动断言 resource pending/ready/required-failed/cancelled、optional failure 可提交，以及 candidate cancel-requested/cancelled/stale/failed/committed 的 decision 优先级、`can_commit` 和非法参数。
- TEST1081/TEST1082 继续覆盖同文档 fragment 与跨文档 traversal 的 Browser-owned viewport snapshot；TEST1082 还覆盖新 entry 清零、横向值存取、宿主 clamp 和非法参数 fail closed。
- TEST1132 覆盖已有 `MediaQueryList` 在 viewport/DPR 改变后的匹配刷新、legacy `addListener`/`removeListener`、`onchange`、可信 change 事件字段、状态翻转去重和监听器移除。

### 当前测试入口

- `TEST_MAX_NUMBER`：1132。
- tracked `test_host/test_host.ini`：`auto=1`、`javascript=0`，选择 `13,20,27,56,58,62,64-67,73,75,999`。
- tracked INI 是窄 smoke，不是全量目录；nightly 打包脚本从源码 dispatch 动态生成全量自动清单。
- 设备连接必须先由用户在 WMDC/Device Emulator GUI 手动完成；RAPI gate 只使用当前唯一会话。

## 最新有效设备证据

当前最新产品门为 next687 动态 `MediaQueryList` 与 viewport resize、layout geometry、DPI-safe script scroll 回归；最近一次稳定全量 checkpoint 仍为 next670：

- next687 定向目录：`tmp/device-runs/20260831-122837-next687-match-media-r5/`；
- 动态选择：`1132,1131,1130,1129,999`，5 项；5/5 通过，零 `ERROR`/`FAIL`，唯一 `TESTBENCH PASS`。TEST1132 验证宽度/分辨率媒体条件刷新、legacy `addListener`/`removeListener`、`onchange`、change 事件字段、状态翻转去重和监听器移除；TEST1131 验证 WM_SIZE 的 CSS viewport/DPR 与 window 事件；TEST1130 验证布局 border-box 与 `getBoundingClientRect()`；TEST1129 验证 CSS/物理 scroll 双向换算；TEST999 提示音请求一次。
- 设备：640x480，dpi=192；使用当前 WMDC GUI 会话、正式 VS2008 ARMV4I Debug 构建和同批 staging。
- 静态验证：`python scripts/test_c89ize.py`、正式 Debug 构建、设备门和 `python scripts/audit_repo.py` 均通过；设备日志中的选择、完成数、错误数和唯一 `TESTBENCH PASS` 已复核。

- next686 定向目录：`tmp/device-runs/20260831-114833-next686-r2/`；
- 动态选择：`1129,1130,1131,999`，4 项；4/4 通过，零 `ERROR`/`FAIL`，唯一 `TESTBENCH PASS`。TEST1131 验证 WM_SIZE 进入 Browser 的 CSS viewport/DPR resize 通知、window 事件字段、重复三元组去重和 screen 方向更新；TEST1130 验证布局前 relation 不可用、布局后 border-box CSS 几何、`getBoundingClientRect()` 的 page scroll 偏移；TEST1129 验证 CSS/物理 scroll callback 与通知的双向换算、clamp、事件去重和注销；TEST999 提示音请求一次。
- 设备：640x480，dpi=192；使用当前 WMDC GUI 会话、正式 VS2008 ARMV4I Debug 构建和同批 staging。
- 静态验证：`python scripts/test_c89ize.py`、正式 Debug 增量构建、设备门和 `python scripts/audit_repo.py` 均通过；设备门日志中的选择、完成数、错误数和唯一 `TESTBENCH PASS` 已复核。

- next684 定向目录：`tmp/device-runs/20260831-093711-next684-script-scroll-r2/`；
- 动态选择：`1129,1128,1080-1084,999`，8 项；8/8 通过，零 `ERROR`/`FAIL`，唯一 `TESTBENCH PASS`。TEST1129 验证脚本 scrollTo/scrollBy 的 host clamp、宿主反向同步、scroll 事件去重、注销/非法参数和 callback 不可重入；TEST1128 与 1080–1084 为直接相邻回归。
- 设备：640x480，dpi=192；使用当前 WMDC GUI 会话、正式 VS2008 ARMV4I Debug 构建和同批 staging。
- 静态验证：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、正式 Debug 增量构建均通过；设备日志中的选择、完成数、错误数和唯一 `TESTBENCH PASS` 已复核。

- next683 定向目录：`tmp/device-runs/20260831-023828-next683-page-scroll-r3/`；
- 动态选择：`1128,1080-1084,999`，7 项；7/7 通过，零 `ERROR`/`FAIL`，唯一 `TESTBENCH PASS`。TEST1128 验证 Core page width、宿主 x/y clamp、Browser 横向 snapshot、fragment document-space 命中和新 entry 清零；TEST1080–1084 验证直接相邻 history、fragment、label/native 路径。
- 设备：640x480，dpi=192；使用当前 WMDC GUI 会话、正式 VS2008 ARMV4I Debug 构建和同批 staging。
- 静态验证：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py` 与正式 Debug 增量构建均通过；设备日志中的选择、完成数、错误数和唯一 `TESTBENCH PASS` 已复核。

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
- TEST1117–TEST1132 都是离线自动夹具，没有新增必须立即人工复核的崩溃或数据风险；它们的 dialog、候选提交、资源 gate、cleanup、history、page viewport、单元素 geometry、resize metadata 和动态媒体查询的视觉、触摸、旋转与不同 DPI 呈现，继续与既有人工清单一起累计观察。自动结果不替代真实网络恢复、OEM 控件或逐资源视觉验收。
- next682 的 TEST1081/1082 没有新增必须立即人工复核的崩溃或数据风险；不同页面高度、横向滚动、旋转、DPI 和真实后退按钮的整体视觉/触摸结果继续与既有滚动和 history 风险一起累计观察。自动门只证明 Browser snapshot 与宿主 clamp/apply 的语义。
- next683 的 TEST1128 同样是离线自动夹具，没有新增必须立即人工复核的崩溃或数据风险；宽页面的横向滚动条、左右边距、触摸/键盘操作、resize/旋转/DPI 视觉和真实页面 overflow 结果进入既有人工累计清单。自动门只证明 page-level extent、坐标一致性、clamp 和 snapshot 语义。
- next684 的 TEST1129 是离线脚本/宿主同步夹具，没有新增必须立即人工复核的崩溃或数据风险；真实页面脚本滚动、滚动条视觉、触摸/键盘、resize/旋转/DPI 和嵌套 overflow 仍进入既有人工累计清单。自动门只证明 page-level 坐标、clamp、反向同步、事件去重和 callback 不可重入。
- next685 的 TEST1130 是离线 Core/Browser 几何夹具，没有新增必须立即人工复核的崩溃或数据风险；真实页面的容器边距、复杂定位、transform、嵌套 overflow、滚动和不同 DPI 视觉仍进入既有人工累计清单。自动门只证明有限整数 border-box、viewport scroll 偏移和 DPI 坐标边界。

允许累计的人工风险包括低风险视觉、触摸、SIP/IME、旋转、picker 和失败网络观察。崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即人工复核。

## 当前未决风险

- 已建立固定、小型、可重复的离线 corpus 流程，但它们仍不能代表任意真实网站；TEST13 仍只是单一网络哨兵。TEST1119–TEST1132 已覆盖候选取消、Browser candidate admission/结果/终态、资源事务终态、有界 transport 重试、required/optional 提交门、candidate/resource 组合决策、清理快照、page-level viewport、脚本/宿主滚动同步、有限 `getBoundingClientRect()` 几何、viewport resize metadata 和有界动态 `matchMedia()`，但取消仍是协作式的，不能保证正在阻塞的 PHttp 调用立即返回；宿主尚未提供面向用户的逐资源 UI，也不能保证任意真实站点的 fallback 视觉、复杂布局几何或精确逐元素归因。
- `<dialog>` 已有已验证的有界脚本生命周期、`method="dialog"` 默认动作、活动 modal id、Escape→`requestClose()` 桥接、宿主顺序 Tab/Shift+Tab 子树范围、有界 backdrop 指针策略和 Core 实体色 modal paint；当前表单桥要求最近祖先 dialog 有非空 id。CSS `::backdrop`、透明合成、多个 modal 和跨文档 modal 生命周期尚未实现，初始焦点、native 窗口视觉和非顺序平台焦点仍由宿主决定。
- `contenteditable` 具有单元素纯文本状态/mutation、Browser 的 bounded selectionStart/End/Direction、去重后的 `selectionchange` 和带 id、已布局 editing host 的有界 WM EDIT 代理；宿主在无修饰 `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` 以及键盘扩展后报告范围与 forward/backward 方向，捕获/取消/焦点中断会收尾而不重复派发，每页最多 16 个 host、文本最多 8192 UTF-8 字节，嵌套继承后代不重复代理。当前另有宿主级受限 `CF_UNICODETEXT` 粘贴/剪切/复制事务：`WM_COPY` 的非空选区才写入剪贴板，折叠选区是 no-op；不支持的格式和超长数据在 native mutation 前 fail closed。Range/Selection 对象、完整 ClipboardEvent/async clipboard、CF_TEXT/富文本转换、OEM 特有键盘自动重复与复杂行导航、designMode、完整 IME 组合尚未实现。
- float、复杂 table/position、现代 CSS 与任意畸形页面仍有明显边界。
- 浏览器 JavaScript 是有限组合，不具备完整 DOM/Web API 或现代浏览器安全沙箱。
- 多窗口、持久 history、完整下载/外部协议策略仍属于宿主或未实现范围。
- mbed TLS 2.16.12 等依赖为旧平台兼容 pin，发布前必须审查当前安全风险。
- OEM SIP/IME、系统 picker、视觉和旋转不能仅凭 synthetic 自动测试保证。

完整列表见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 唯一下一步：next688

先从源码、日志或截图固定一个真实缺口，再选择离线 fixture 或稳定哨兵。实现必须把可复用语义放在正确的公共 DLL，宿主只做平台接线、调度、应用策略和断言；不要把互不相关的能力拆成只增加编号的提交，也不要在没有证据时扩大 ABI。next687 的动态 `MediaQueryList` 与 next686 的 viewport resize、next685 的有限布局 geometry 和 DPI-safe scroll 已有自动与设备证据；复杂布局、nested overflow、transforms、平滑/惯性滚动、完整媒体查询语法、脚本队列自动调度和视觉差异仍是限制，不应在下一步中被误写成已支持。

优先场景应同时满足：

1. 可在仓库内离线固定主要 HTML/CSS/交互，避免网络内容漂移；
2. 对应一个真实页面或用户操作，而不是孤立 getter/setter；
3. 由源码、日志或截图先证明边界，且暴露当前限制中的一个核心流程缺口；
4. 通用语义进入公共 DLL，宿主只保留平台接线；
5. 可以自动断言主要结果，人工部分只保留无法机器判断的视觉/输入风险。

## 下一步完成标准（next688）

- 先用源码、日志或截图固定一个真实页面/交互组合缺口，并把最小可重复 fixture 或哨兵写入测试入口；
- 可复用的 URL/history/DOM/Event/资源/布局/生命周期语义位于对应公共 DLL，`test_host` 只负责 WM 接线、调度和 fixture，不新增业务所有权；
- 自动断言覆盖该纵向能力的成功、失败/取消、资源清理和直接相邻旧路径，且不会削弱 next685 的布局 relation、`getBoundingClientRect()`、DPI 换算、history snapshot、宿主 clamp/apply 或旧页保留契约；
- C89 回归、VS2008 ARMV4I 正式构建、同批 staging、仓库审计和风险相称的设备门均通过，无旧 EXE/DLL 混包；
- 定向门及直接相邻回归唯一 `TESTBENCH PASS`、零 `ERROR`/`FAIL`，视觉、触摸、SIP/IME、picker 或旋转风险进入人工累计清单；
- handoff 覆盖为 next687 快照，ROADMAP 只保留当前尚未完成的纵向能力。

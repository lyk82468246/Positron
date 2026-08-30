# 当前交接

本文件只描述当前产品基线、最近有效证据、未决风险和唯一下一步。逐批实现过程由 Git 历史保存，历史事故见 `docs/history/`，未来方向见 `ROADMAP.md`。

## 项目使命

Positron 为 Windows Mobile 6 / Windows CE 5.2 ARMV4I 提供模块化 TLS、JSON、HTTP、图像、脚本、渲染与浏览器会话 DLL。公共边界保持 C ABI、UTF-8、opaque handle 和显式所有权；`test_host.exe` 只是回归宿主与示例消费者。

## 当前 Git 与工作区

- 分支：`main`；next674 已完成产品实现、宿主接线、自动断言和文档快照，提交后工作区应保持 clean。
- next672 在 next671 的离线候选流程上补齐了宿主级过时导航事务：每个候选独占 worker handle 和取消标志；新导航会按 generation 退休旧候选，旧 worker 的进度/完成消息不能触碰当前候选；退休请求保留自己的 response、资源队列和线程，直到完成消息或关闭清理后释放。退休队列固定为 4 个请求，满载时新导航 fail closed；取消检查位于 worker 的阶段边界和 PHttp 返回边界，未改变公共 DLL ABI。
- next673 在上述导航事务上补齐了资源终态分类：每个资源沿 `pending` 单向进入 `ready`、`failed` 或 `cancelled`；失败按 resolve、transport、HTTP、budget、memory 分类，取消不计入网络失败。宿主日志输出有界的终态/错误计数，response 与资源缓冲仍由拥有它们的候选回收；没有改变公共 DLL ABI，也没有把强制中断阻塞 socket 伪装成已完成。
- next674 在资源终态分类之上加入了固定的 transport 重试预算：每个资源最多重试 2 次（最多 3 次尝试），只有 `status_code == 0` 的 transport 失败可重试；resolve、HTTP、budget、memory 和 cancelled 结果保持首个终态，预算耗尽后记录 `retry_exhausted` 并保持 transport failure。日志增加 attempts/retries/exhausted 计数，仍不改变公共 DLL ABI。
- next671 在 next670 的离线 corpus 基础上补齐了跨文档候选的 page teardown：Browser 提供一次性的 `visibilitychange`/`pagehide`/`unload` 与页面队列清理入口，宿主只在候选完成提交前调用；失败候选继续保留旧页。脚本 native function 预算按设备验证从 23 增加到有界的 24，为一个产品消费者回调保留槽位。
- next670 在 next669 的离线 corpus 基础上修复了 collapsed-border 的重复 DPI 换算、浏览器 `classList.toggle` 的参数个数语义、无 DOM bootstrap 回退，以及高 DPI 重排后 block 文本盒的 label 命中关联；同时将浏览器 session 堆上限固定为经设备二分验证的 646 KiB，并恢复稳定的 TEST3/TEST5 网络哨兵组合。
- 上一产品基线为 `88d68ebd`（next669，首个离线 compatibility corpus 完整流程）；更早基线为 `c0c4ba0e`（next668，单元素 `contenteditable` 的受限 CF_UNICODETEXT 粘贴/剪切与 WM_COPY 边界、折叠选区 no-op、超长/非 Unicode fail-closed 和 WinCE 原生 WM_CUT 重入保护）。
- Core 现在报告稳定的有效表单方法常量，并为显式 submitter 或单行输入隐式提交解析最近祖先 dialog id 与 submitter value。Browser 提供按 id 直接执行 `dialog.close(value)` 的会话边界；参考宿主只在 validation 和可取消 `submit` 均允许后调用它，不生成网络导航，也不错误派发 `cancel`。Core 还提供 `PCore_PaintDocumentWithModal`：普通文档绘制后覆盖有界实体色 backdrop，并按 Browser 的活动 id 重绘已打开的 dialog；next658 的 backdrop 指针策略和此前的 modal 焦点/Escape 边界保持不变。
- `tmp/` 保存本地设备日志和截图，不跟踪。

## 当前中期里程碑

在保持 VS2008/WM6 约束的前提下，把已经形成的 Core、Browser 和平台宿主能力整合为可由真实页面驱动的有界网页运行时。重点是完成用户可感知的纵向能力、把通用语义留在公共 DLL，并以小型真实页面/交互语料库防止只增加孤立 API。

## 当前短期目标

推进 next675：在资源失败分类和 transport 重试预算之上，定义候选页面的资源提交门；必需资源失败时保持旧页，可降级资源按明确策略继续，取消候选不得提交。

## 已验证产品事实

### 公共边界

- 顶层公共 DLL 为 TLS、JSON、HTTP、image、script、core 和 browser。
- NetSurf/libcss/libdom/hubbub、Expat、libsvgtiny、libjpeg 等移植工程是内部实现依赖。
- 独立脚本和浏览器脚本共用 Duktape；浏览器 JavaScript tracked 默认仍为关闭。
- 通用 URL、history、DOM、Event、表单、图像和脚本 session 语义位于对应公共 DLL；宿主保留 WM 窗口、消息、控件、SIP/IME、picker、导航调度和资源 I/O。

### 当前网页能力

- HTML/CSS/DOM、整树 style、NetSurf layout/redraw、GDI 绘制与资源缓存已形成正式 Core 路径。
- 常用 block/inline/flex/table、图片/SVG、背景、列表、有限定位、表单控件、验证、提交与 reset 已有设备回归；这不代表完整 CSS/HTML。
- Browser 层提供有界 history、same-document state、script session、DOM/Event/input/navigation callbacks，以及 timer/microtask/lifecycle 和 native 控件事务协调。
- 页面导航保留旧页到候选文档成功提交；主文档和资源网络阶段与 UI 文档操作分离。
- 资源准备在宿主内记录 `pending`、`ready`、`failed`、`cancelled` 四种终态；失败摘要区分 resolve、transport、HTTP、budget、memory，transport 失败最多重试 2 次（每项最多 3 次尝试），HTTP、resolve、budget、memory 和取消不重试，取消单独计数且不重新暴露为可用缓存。该摘要是导航调度 telemetry，不是公共 Core/Browser ABI。
- 深层 DOM 资源准备使用单个事务级 heap scratch，避免大批固定栈缓冲耗尽 WM6 线程栈。
- `<details>/<summary>` 支持 click 与 Enter/Space 激活、取消和 DOM 状态同步。
- 支持的链接、summary、native EDIT/SELECT/button/file 等目标，以及带有效非负 `tabindex` 的普通布局元素按有界顺序响应 Tab/Shift+Tab：正值升序、同值 DOM 稳定排序，随后零/缺省组；负值、disabled/hidden/stale 目标和 file picker 仍被排除。Browser 报告活动 modal id 后，宿主可用 Core 的 scoped snapshot 将顺序焦点限制在 dialog 子树；宿主仍同步焦点事件、原生焦点和滚动可见性。
- `<dialog>` 的 show/showModal/close/requestClose、returnValue、cancel/close 事件、活动 modal id 查询、宿主驱动的 Escape 请求桥接、有界 backdrop 指针策略、`method="dialog"` 默认动作和 Core modal paint 已形成契约。显式点击、脚本 `click()` 和单行输入隐式 Enter 都遵循 validation→可取消 submit→直接 close/returnValue；CSS `::backdrop`、透明合成、多个 modal 和跨文档 modal 仍未实现。
- 单元素 `contenteditable` 已形成 Core/Browser/宿主边界：Core 解析祖先继承并限制合法 UTF-8 纯文本 mutation，Browser 暴露 `isContentEditable`/`innerText`、`selectionStart`/`selectionEnd`/`selectionDirection` 与 typed input 事务，宿主为带 id 且已布局的有效 editing host 创建有界 WM multiline EDIT 代理。允许的 `WM_CHAR` 在默认处理完成后回读最终文本并派发 `input`；取消的 `beforeinput` 不修改 Core，重复/提前 `EN_CHANGE` 不会制造空事件。Browser 选区偏移使用 UTF-16 code unit；宿主将 WM EDIT 的 CRLF 位置转换为逻辑 LF，并在可用时同步原生 HWND。无修饰鼠标拖选以及 Shift/方向键扩展由宿主短暂保存 anchor；捕获丢失、取消模式和焦点切换会先结束手势，再通过 Browser 的去重通知入口刷新范围。宿主对 `WM_PASTE`/`WM_CUT` 只接受有界 `CF_UNICODETEXT`，把规范化后的 UTF-8 data 交给 `beforeinput`，允许后执行 native default，再提交 Core/input 和折叠选区；`WM_COPY` 只写入非空的有界 Unicode 选区，折叠选区保持现有剪贴板不变；格式缺失、超长或读取失败时 fail closed。为兼容 WinCE 原生剪切的内部重入，宿主只在外层 `WM_CUT` 默认处理期间放行同一 HWND 的嵌套 `WM_COPY`。每页最多 16 个宿主、文本最多 8192 UTF-8 字节；嵌套继承后代不重复创建 host。
- 离线 compatibility corpus 的第一条完整流程已加入 TEST1117：固定 HTML/CSS 在无网络条件下串联 contenteditable 取消与提交、selectionchange、required dialog validation、`method="dialog"` close/returnValue、same-document fragment 导航、跨 origin 候选回滚和 history back。几何、状态、事件、导航和失败不变性都由自动断言覆盖；它是回归夹具，不改变公共 ABI，也不把页面语义移入宿主。
- TEST1118 将第二条离线候选流程接入公共 Browser/Core 路径：重复外链 script 与 SVG 资源只准备一次；503 候选保持旧 document/session、旧 timer 和队列；200 候选提交前派发一次 teardown，提交后 resource cache、load 结果与 history 均由自动断言核对。夹具完全离线，不把网络端点或测试语义写进公共 ABI。
- TEST1119 覆盖资源准备期间的新旧候选交错、generation 取消、过时进度/完成消息隔离、退休请求回收和最新候选提交。
- TEST1120 固定 ready、HTTP、transport、budget 和 cancelled 五种资源结果，自动断言终态计数、错误分类、缓存回调可见性和有界资源字节；它验证宿主 telemetry，不宣称完整网络重试策略。
- TEST1121 固定一次可恢复 transport 失败、一次重试预算耗尽，以及 HTTP、resolve、budget、cancelled 的不可重试结果；自动断言每项 attempts/retries、最终分类、缓存可见性和有界计数。

### 当前测试入口

- `TEST_MAX_NUMBER`：1121。
- tracked `test_host/test_host.ini`：`auto=1`、`javascript=0`，选择 `13,20,27,56,58,62,64-67,73,75,999`。
- tracked INI 是窄 smoke，不是全量目录；nightly 打包脚本从源码 dispatch 动态生成全量自动清单。
- 设备连接必须先由用户在 WMDC/Device Emulator GUI 手动完成；RAPI gate 只使用当前唯一会话。

## 最新有效设备证据

当前最新产品门为 next674 定向门；最近一次稳定全量 checkpoint 仍为 next670：

- next674 定向目录：`tmp/device-runs/20260830-194038-next674-final2/`；
- 动态选择：`1117,1118,1119,1120,1121,999`，6 项；6/6 通过，零 `ERROR`/`FAIL`，唯一 `TESTBENCH PASS`；
- 设备：640x480，dpi=192；使用当前 WMDC GUI 会话、正式 VS2008 ARMV4I Debug 构建和同批 staging；TEST1119 覆盖资源准备期间的新旧候选交错、generation 取消、过时消息隔离、退休请求回收和最新候选提交，TEST1120 覆盖资源终态与错误分类摘要，TEST1121 覆盖 transport 重试预算和不可重试终态。

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
- TEST1117、TEST1118、TEST1119、TEST1120 与 TEST1121 都是无网络自动夹具，没有新增必须立即人工复核的风险；其 dialog、候选页面视觉、链接触摸、滚动/旋转和不同 DPI 的整体呈现仍按既有规则累计人工验收。TEST1121 的重试计数是宿主调度契约，不能替代真实网络恢复体验。

允许累计的人工风险包括低风险视觉、触摸、SIP/IME、旋转、picker 和失败网络观察。崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即人工复核。

## 当前未决风险

- 已建立五条固定、小型、可重复的离线 corpus 流程，但它们仍不能代表任意真实网站；TEST13 仍只是单一网络哨兵。TEST1119/1120/1121 已覆盖候选取消、资源终态 telemetry 和有界 transport 重试，但取消仍是协作式的，不能保证正在阻塞的 PHttp 调用立即返回；宿主尚未提供必需/可选资源的部分提交 UI 策略。
- `<dialog>` 已有已验证的有界脚本生命周期、`method="dialog"` 默认动作、活动 modal id、Escape→`requestClose()` 桥接、宿主顺序 Tab/Shift+Tab 子树范围、有界 backdrop 指针策略和 Core 实体色 modal paint；当前表单桥要求最近祖先 dialog 有非空 id。CSS `::backdrop`、透明合成、多个 modal 和跨文档 modal 生命周期尚未实现，初始焦点、native 窗口视觉和非顺序平台焦点仍由宿主决定。
- `contenteditable` 具有单元素纯文本状态/mutation、Browser 的 bounded selectionStart/End/Direction、去重后的 `selectionchange` 和带 id、已布局 editing host 的有界 WM EDIT 代理；宿主在无修饰 `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` 以及键盘扩展后报告范围与 forward/backward 方向，捕获/取消/焦点中断会收尾而不重复派发，每页最多 16 个 host、文本最多 8192 UTF-8 字节，嵌套继承后代不重复代理。当前另有宿主级受限 `CF_UNICODETEXT` 粘贴/剪切/复制事务：`WM_COPY` 的非空选区才写入剪贴板，折叠选区是 no-op；不支持的格式和超长数据在 native mutation 前 fail closed。Range/Selection 对象、完整 ClipboardEvent/async clipboard、CF_TEXT/富文本转换、OEM 特有键盘自动重复与复杂行导航、designMode、完整 IME 组合尚未实现。
- float、复杂 table/position、现代 CSS 与任意畸形页面仍有明显边界。
- 浏览器 JavaScript 是有限组合，不具备完整 DOM/Web API 或现代浏览器安全沙箱。
- 多窗口、持久 history、完整下载/外部协议策略仍属于宿主或未实现范围。
- mbed TLS 2.16.12 等依赖为旧平台兼容 pin，发布前必须审查当前安全风险。
- OEM SIP/IME、系统 picker、视觉和旋转不能仅凭 synthetic 自动测试保证。

完整列表见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 唯一下一步

next675 建立资源失败后的候选提交门：在 next674 的终态分类和 transport 重试预算之上，为主文档、样式等必需资源与图片/脚本等可降级资源定义明确策略；必需资源失败或候选取消时保持当前页面，只有满足提交门的候选才触发 teardown、swap 和 history 更新。不得把“部分资源可用”隐式当成成功，也不把阻塞 socket 的强制中断写成契约。

优先场景应同时满足：

1. 可在仓库内离线固定主要 HTML/CSS/交互，避免网络内容漂移；
2. 对应一个真实页面或用户操作，而不是孤立 getter/setter；
3. 暴露当前限制中的一个核心流程缺口；
4. 通用语义进入公共 DLL，宿主只保留平台接线；
5. 可以自动断言主要结果，人工部分只保留无法机器判断的视觉/输入风险。

## 下一步完成标准

- next675 的必需/可降级资源提交门完全离线固定，自动断言覆盖资源分类、候选取消、旧页保留、teardown、swap、history 和缓存回收；
- 可复用的 URL/history/DOM/Event/资源生命周期语义位于 Core 或 Browser，`test_host` 只负责 WM 接线、调度和夹具；
- VS2008 ARMV4I 正式构建、同批 staging、C89 回归和仓库审计通过，无旧 EXE/DLL 混包；
- next675 定向门及直接相邻回归唯一 `TESTBENCH PASS`、零 `ERROR`/`FAIL`，并保留可追溯的 `tmp/device-runs/` 证据；
- 视觉、触摸、SIP/IME、picker 或旋转等无法自动判断的风险进入既有人工累计清单，不以人工缺席伪造自动通过；
- handoff 覆盖为 next674 快照，ROADMAP 只保留下一条未完成的纵向能力。

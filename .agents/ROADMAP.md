# Positron 路线图

本文件只描述未来工作和选择优先级。已经完成的能力不保留在这里；当前事实见 [`HANDOFF.md`](HANDOFF.md)，限制见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)，逐批历史见 Git。

## 长期目标

让 Windows Mobile 6 / Windows CE 应用能够按需组合一组稳定、资源有界、可部署的公共 DLL：

- verified TLS、JSON、HTTP、图像和脚本基础设施；
- 可嵌入的 HTML/CSS/DOM/layout/paint Core；
- 无窗口依赖的 Browser history、script session 与平台事务协调；
- 由应用宿主提供窗口、消息循环、网络调度、native 控件和产品策略；
- 在旧工具链约束下仍有清楚 ABI、所有权、安全边界与真实设备证据。

长期成功不以测试编号数量衡量，而以真实应用能否通过公共 DLL 完成完整流程、宿主是否保持轻薄、以及回归是否能稳定发现用户可感知问题衡量。

## 中期里程碑

形成一个可由固定真实页面/交互语料驱动的轻量网页运行时：

1. 为常见页面建立小型、离线、可重复的 compatibility corpus；
2. 用 corpus 选择纵向能力，而不是零散增加 API；
3. 把通用 HTML/CSS/DOM/Event/form/history/script 语义放入 Core 或 Browser；
4. 宿主只保留 WM 平台接线、网络 I/O 和应用策略；
5. 为资源、生命周期和失败回滚建立可重复的全量 checkpoint；
6. 对无法自动判断的视觉、触摸、SIP、picker 和旋转做成批人工验收。

## 近期目标

### 真实页面组合缺口

现有 Browser viewport 组合已经覆盖 page-level scroll、有限布局几何、WM_SIZE
resize、动态 `matchMedia()`、布局视口对应的 `visualViewport` 快照、稳定的
`screen.orientation` 方向事件和 `history.scrollRestoration` 的宿主策略门；这些
能力都必须继续由 Browser/Core 提供，不能退回到 `test_host` 的业务 helper。
next691 已完成页面替换前的 cancelable `beforeunload` 门和参考宿主的 fail-closed
关闭/提交策略。next692 已把脚本 timer、animation frame、message、idle 和
microtask 的阶段顺序收拢为 Browser-owned 的统一任务检查点，并接入参考宿主的
UI 消息循环；宿主仍提供时钟、限额和平台调度。next693 已补齐完成加载后的初始
`pageshow`，以及 hidden→visible 恢复时的去重 `visibilitychange`/
`pagehide`/`pageshow` 组合，语义位于 Browser 而不是 `test_host`。下一条
next694 已补齐宿主驱动的顶层窗口 focus/blur 状态、`document.hasFocus()` 和
去重事件分发，语义位于 Browser，参考宿主只负责 `WM_ACTIVATE` 接线；
TEST1139 与定向设备门已验证该合同。next695 又补齐 Core 焦点 id 查询、Browser
可选的 `document.activeElement` 投影和参考宿主桥接；TEST1140 与定向设备门
验证了有 id、无 id、清除焦点、过小缓冲和注销后的 body 回退。next696 又补齐了
按 id 的 `HTMLElement.focus()`/`blur()` 请求桥：Browser 负责脚本方法和请求验证，
Core 负责已布局目标资格与 focus node，宿主负责 native HWND 和 focus family；
TEST1141 与定向设备门验证了切换顺序、no-op 和注销后的 fail-closed。next697
增加了 Ex focus request、默认 page-level focus reveal 和
`focus({preventScroll:true})`，由 TEST1142 与定向设备门验证 callback 后脚本滚动
同步和 viewport 可见性。next698 又补齐了页面级 `Element.scrollIntoView()`：Browser
复用 `getBoundingClientRect()` 和现有 scroll callback，提供有限的 block/inline 对齐，
并以 TEST1143 与定向设备门验证默认、center、末端、nearest 和 smooth 拒绝路径。
next699 补齐了基于 Core 单矩形快照的 `Element.getClientRects()` 回归。next700
进一步把普通 inline flow 的实际行片段接入 Core relation：Browser 每次新建最多 16
个 viewport-relative 矩形，并以同一集合计算 `getBoundingClientRect()` union；TEST1144
覆盖单矩形回归，TEST1145 覆盖窄容器多片段、索引顺序、identity 和 union。next701
在同一 bridge 上补齐了已布局常见 box 的六个只读布局尺寸快照，TEST1146 覆盖 Core
relation、Browser getter、边框/内边距/retained-scrollport 算术、后代 extent 和隐藏回退。
next702 已在 Core/Browser 中完成带 DOM id 的常见 overflow box retained 两轴滚动：
关系 38/39、按 id setter、两轴 clamp、脚本 `scrollLeft`/`scrollTop`/`scrollTo()`/
`scrollBy()`、目标元素事件去重和宿主 pointer snapshot 同步均有 TEST1147 覆盖。下一条
纵向能力是 next703，仍须从源码、日志或截图固定一个新的可复现用户可见组合缺口；不能
凭空扩张 ABI，也不能把页面业务规则塞回 `test_host`。完整滚动容器树、scroll chaining/
anchoring、nested `scrollIntoView()`、Range/Selection、pinch zoom、平滑/惯性滚动和
视觉猜测不得误写成已支持能力。

优先检查导航提交后的实际页面行为、资源/布局组合或已有人工反馈中仍未被自动覆盖的回归。实现前先固定最小离线 fixture 或稳定哨兵，明确旧页保留、失败回滚、资源所有权和页面生命周期的预期；实现后由拥有语义的 Core/Browser 或相应公共 DLL 提供能力，宿主只保留 WM、线程、网络和应用策略。任何新增结构必须保持 C ABI、UTF-8、opaque ownership、固定容量和 VS2008/WM6/C89 兼容。

next694、next695、next696、next697、next698、next699、next700、next701 和 next702 的完成证据
都包括定向自动断言、直接相邻回归和风险相称的设备门；下一批 next703 必须沿用同一
完成标准。
视觉、触摸、SIP/IME、picker 或旋转只能进入人工累计清单，崩溃、数据损坏、
严重布局破坏和核心交互阻塞必须立即人工复核。不要为增加测试编号拆分能力，
也不要在没有实际缺口证据时提前选择下一能力方向。

### 继续清理产品所有权

每批都审查 `test_host` 是否仍拥有可复用语义。若某段逻辑决定 URL、history、DOM、Event、表单默认动作、validation、图像或脚本 session 行为，应迁入对应公共 DLL；窗口、WM 消息、HDC、picker、SIP/IME 和应用导航策略仍留在宿主。

## 中期工作流

### JavaScript 与 Web 组合

- 依据 corpus 补齐高价值 DOM/Event/form/navigation 对象，不追求一次性完整 Web API。
- 明确 script session 与 document/window 生命周期，继续验证取消、过时导航和 queue 清理的组合顺序。
- 为 timer、microtask、animation frame、message 和 lifecycle 的组合顺序增加真实页面断言。
- 保持浏览器 JavaScript 显式 opt-in，并持续验证关闭时不抓取或执行页面脚本。
- 评估可信/不可信脚本边界，避免把有限 Duktape host 误称为现代浏览器安全沙箱。

### Layout、绘制与字体

- 用真实页面缺口驱动 float、position、table、media、字体和复杂 inline 行为。
- 优先修复严重错位、内容不可达、错误滚动和交互几何，不做脱离语料的全面 CSS 扩张。
- 在继续扩展前，保持 next702 的边界：完整滚动容器树、scroll chaining/anchoring、
  nested `scrollIntoView()`、smooth/inertia 和匿名目标不因一个带 id 的 retained offset
  bridge 而被误称为已实现；真实滚动条裁剪和触摸视觉进入人工验收矩阵。
- 继续降低深 DOM、资源树和重排路径的栈/heap 峰值，并为失败清理添加资源断言。
- 建立多 viewport/DPI 截图基线，但把设备量化和字体差异与语义断言分开。

### 表单、输入与可访问交互

- 依据实际流程在已有有界 `dialog` 脚本生命周期、`method="dialog"` 默认动作、活动 modal id、Escape 请求桥接、宿主顺序 Tab 子树范围、实体色 modal paint、backdrop 指针策略和单元素 contenteditable WM EDIT 接线、去重 `selectionchange` 通知、无修饰连续鼠标拖选以及 Shift/键盘、捕获和焦点中断收尾之上，继续用 compatibility corpus 选择相邻缺口。next667 已实现受限 paste/cut 事务与选区同步，next668 已完成 `WM_COPY` 非空选区、折叠选区 no-op、超长/非 Unicode fail-closed 及 WinCE `WM_CUT` 内部重入保护；next696 已把按 id 的 `HTMLElement.focus()`/`blur()` 请求桥接到 Core/native focus 事务，next697 又加入 Ex focus request 的 page-level reveal 与 `preventScroll`；保持已实现的有界 `tabindex` 排序与 Core/宿主事务边界。
- 保持 native 控件 mutation、Browser event policy 与 Core form state 的事务顺序。
- 扩充真实 SIP/IME、硬键盘、SELECT popup、file picker 和旋转的成批人工矩阵。
- 对 disabled/hidden/stale target 一律 fail closed，不为通过测试绕过生命周期检查。

### 网络、安全与资源

- 维护 Mozilla CA snapshot 和旧 mbed TLS 风险评估，记录可接受的部署威胁模型。
- 根据真实消费者需求评估 TLS peer identity 轮换、错误分类和 listener 资源上限。
- 为 redirect、失败旧页保留、资源取消和页面提交建立离线/loopback 测试，降低外网依赖。
- 只有测量显示收益时再考虑 keep-alive、缓存或性能优化；先保护所有权和失败回滚。

### 公共 DLL 生态

- 保持公开头文件、README 调用模式、sample 和 ABI 测试一致。
- 为每个顶层 DLL 提供最小独立消费示例，避免只能通过 `test_host` 理解调用方式。
- 逐步减少宿主私有桥和重复业务规则；内部静态库继续隐藏在公共 DLL 后。
- 发布前持续审计 third-party 版本、许可证、生成步骤和 GPL 组合义务。

## 批次选择优先级

每次只选一个边界清楚、可验证的纵向能力，按以下顺序取舍：

1. 崩溃、数据损坏、严重布局破坏或核心流程阻塞；
2. 产品语义仍错误地滞留在 `test_host`；
3. compatibility corpus 暴露的高频真实缺口；
4. 安全、ABI、所有权、资源或生命周期风险；
5. 有测量证据的性能/内存问题；
6. 其他孤立 API 或观感优化。

一个批次应包含产品实现、宿主接线、自动断言、风险相称的设备门和文档职责更新。不要把同一个子功能拆成多个只增加编号的提交，也不要为追求“大步”把互不相关的能力塞入同一批。

## 全量与人工门触发条件

满足任一条件时运行更宽自动门或全量 checkpoint：

- 多个低风险定向批次已经累计；
- 修改公共 ABI、所有权或 session/document 生命周期；
- 修改 layout/paint、输入、网络/TLS、资源缓存或设备自动化基础设施；
- 准备里程碑或 nightly 交付；
- 出现无法解释的超时、混包、崩溃或数据错误。

低风险视觉/触摸/SIP/picker/旋转风险可以累计后集中人工验收。崩溃、数据损坏、严重布局破坏和核心交互阻塞必须立即人工复核。

## 每批完成标准

- 能力对应一个完整用户/消费者结果，职责落在正确 DLL；
- 公共 ABI、UTF-8、opaque handle、所有权、VS2008/WM6/C89 兼容性不退化；
- C89 回归、正式 ARMV4I 构建和仓库审计通过；
- staging 来自同一批构建，无旧进程或 DLL 混包；
- 定向门及直接相邻回归唯一 PASS、零 ERROR/FAIL；
- 必要人工门完成，或明确进入允许累计清单；
- `HANDOFF.md` 覆盖为新快照，限制与本路线图删除已经完成的条目；
- 稳定 README/架构/测试文档只在长期读者事实改变时更新，不追加批次流水；
- 只提交本批 tracked 文件并推送当前分支。

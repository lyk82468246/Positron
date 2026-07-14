# 已验证基线与待消除限制

更新时间：2026-07-14

这份清单把“已经在设备上验证的最小链路”和“当前刻意保留的阶段性实现”分开记录。未被列为完成的项目不得在后续交接、README 或测试结论中表述为完整浏览器能力。

## 已验证基线（不是完整功能声明）

| 范围 | 已验证事实 | 不代表 |
|---|---|---|
| CSS 媒体查询与 token | TEST 21 已在设备确认运行时 viewport/DPI、旧式 min/max-width 及整数像素 MQ4 `width <=` / `width <`；TEST38-39 又确认同表顶层 `:root` token 语义与正式 redraw。 | 所有媒体特性、MQ4 范围、元素作用域或完整 custom properties 均已覆盖。 |
| 现代 CSS 值 | TEST40 已在设备确认：数值型 `oklch()` 转裁剪 sRGB，并求值无需布局上下文的同单位 `calc()` 四则运算。 | 完整 gamut mapping、`none`/复杂角度、`color-mix()`、混合单位 calc 或 CSS Color/Values 均已实现。 |
| 反向 flex 内边距 | TEST 22 已在设备上确认：224px viewport 下，`row-reverse`、左右 25px padding、隐藏侧栏时，主内容为 `x=25,width=174`。 | 完整 Flexbox 规范或任意真实站点的复杂 flex 均已兼容。 |
| IANA 窄屏页 | TEST13 的 `Example Domains` 已可读；TEST41 的竖横屏截图确认 `/numbers` grid 宽表格不再把主内容推到左边界外。 | 任意 IANA 子页版式通过，或页面已达到现代浏览器还原度。 |
| 嵌套 overflow | NetSurf 3.11 scrollbar 已接入；TEST42 的离屏步进断言及真机箭头/thumb 交互通过，host 拖动只重绘对应 overflow viewport。 | 不代表惯性触摸、overlay scrollbar 或任意嵌套组合均已覆盖。 |
| 图片 | 既有 TEST18-20 已确认 WM Imaging 四格式缓存链；TEST25-37/13 已确认 SVG 链。新的公共 retained 位图 ABI 与核心对象复用已编译通过，待 TEST19/20 重新真机确认。 | 复杂 SVG text、任意渐变、复杂 CSS 背景、跨线程图片句柄或任意网络图片均已通过。 |
| ENGINE 离线回归 | 2026-07-11 用户确认原整组至 TEST24 通过；2026-07-12 又单独确认 TEST25 SVG parse。TEST23 的浮动实现已因真实 Browse 回归撤回。 | 网络 Browse、GDI Render 组，或未被这些测试覆盖的真实页面兼容性均已通过。 |
| 旋转尺寸 | `WM_SIZE` 以新 client 宽高从 document CSS 缓存 restyle + layout；TEST24 已确认跨断点重选、无联网及滚动比例，真实 TEST13 横竖屏也保持同一阅读区域。 | 所有媒体语法和任意样式资源均已覆盖。 |

## 真实页面观察到的未完成项

### IANA 根变量布局已改善，完整真实页仍继续观察

TEST38-39 真机确认根变量语义及 25px inset 后，新的 TEST13 截图中 IANA logo、导航、正文与注册表两列均未再裁切或重叠，custom-properties 导致的窄屏间距根因可以关闭。普通文本空白折叠也已由 TEST13/15 确认。当前结论仍不是任意 IANA 子页或任意真实站点都已完整还原。

- **可能范围**：剩余 flex/table/inline/字体或未实现 CSS 特性的组合；尚未把单一原因当作结论。
- **已撤回的一项**：IANA 页脚是 table cell 内 `display:inline; float:left` 列表。TEST23 曾在最小样例中确认两个浮动块同行及 `clear:both`，但将该构盒规则直接接入真实页面后，2026-07-11 Browse 截图出现严重错位和替代方框；实现已撤回。该测试不再参加 ENGINE 组，不能作为 float 支持证据。
- **当前站点版本风险**：2026-07-13 重新读取到 IANA 的 `iana_website.80c103cc08b6.css`；除已确认的 `var(--space-*)` 外还有 22 处 `oklch()`、15 处 `calc()`、`color-mix()`、grid/gap 与 `:has()`。新兼容模块只处理数值型 OKLCH 和可完全求值的同单位 calc；混合单位及其他现代能力仍会降级。
- **最新子页结论**：`/numbers` 使用 `display:grid`，其中 `.dtable-wrap { overflow:auto }` 包住宽表格。TEST41 的竖横屏截图已确认 flex 主内容负 x 修复，标题/正文保持 inset；Grid 仍只保持单列文档顺序。
- **下一步**：TEST3/43/44 与真实 TEST13 已验收后台资源、真实单响应进度、协作式 UI 提交基线和主文档失败回滚。当前用 TEST13/24/45 验收正式 Browse、旧 ABI 旋转缓存和新 CSS import tree。Grid/gap 因 NetSurf HTML 层仍无现成轨道布局，须先继续审计可移植上游实现。float 仍须对照上游 box construction/normalisation 与 list marker，不能复用 TEST23 的简化断言。
- **完成条件**：在目标设备的竖屏和横屏下，主内容、页脚和导航均不裁切、不重叠，且没有明显错误图标/替代字符；结果需要新的真机截图确认。

### 旋转 responsive restyle 已完成当前验收

`WM_SIZE` 现调用 `PCore_SetViewport`、基准 URL 感知的 `PCore_StyleDocumentEx2` 和 `PCore_LayoutDocument`。外链 CSS 与成功导入首次导航时以原始字节缓存到 document，尺寸变化只从该缓存重选 `@media`，不重新联网。旧 `PCore_StyleDocumentEx` 保持兼容。TEST24 已于 2026-07-11 在设备确认 320px 到 299px 的旧接口外链 CSS 重选，fetch/free 都保持一次；TEST45 将覆盖导入树的 cache-only 重选。

- **当前取舍**：只缓存最多 32 份、单份不超过 256 KiB、每 document 合计不超过 512 KiB 的成功外链 CSS 原始字节；缓存未命中的样式在旋转时保持缺失，不能在 `WM_SIZE` 中重新联网。
- **设备结论**：TEST24 的 0/50/100% 比例断言通过；真实 TEST13 从竖屏 `Further Reading / Domain Names` 区域旋转到横屏后仍停留在同一区域。扩大 MQ4 语法或处理 custom properties 仍是独立兼容性工作。
- **完成条件**：旋转前后跨越 TEST 21 式断点时，computed style 与几何都切换正确，并恢复原滚动位置的合理比例。

### 导航 CSS/图片抓取已异步，最终提交仍在 UI

主文档之后的外链 CSS、CSS `@import`、`<img>` 和应用样式后发现的背景 URL 现也分轮交给同一 worker；HTTP 字节通过 `WM_APP` 消息交回窗口线程，DOM/libcss/NetSurf/GDI 从不跨线程。TEST3/43/44 与真实 TEST13 已确认真实正文进度、后台资源阶段、成功 swap 和主文档失败回滚。parse/style/image-discovery/layout 现用一次性 WM timer 在调用之间让出消息循环；单个不可重入调用仍可能卡顿。

- **当前取舍**：同一时刻只允许一个导航请求；旧页可绘制和滚动，但加载中再次点击链接会被忽略。HTML parse、style、cache copy 和 layout 仍在 UI 提交阶段同步执行，全部网络完成后仍可能短暂卡顿。
- **资源预算**：`test_host` 最多暂存 64 个去重 URL、合计 2 MiB 原始字节，成功提交时 core 会复制所需数据后立刻释放事务。该值用于限制 WM 峰值，是可替换的宿主策略，不是 `positron_core` ABI 或最终页面的硬上限。
- **后续实现**：单响应 `Content-Length`/progress 回调已实现并由 TEST3/13 确认；`@import` 已进入事务但 TEST45 尚待设备确认。整页多资源聚合进度、web fonts、脚本及更广资源类型仍未实现。
- **CSS import 边界**：最多追踪 16 层递归和本次样式 pass 的 64 个解析表；失败、循环和超深导入按 libcss 契约注册空表。成功导入复用每 document 最多 32 份/512 KiB 的 CSS 字节缓存；不含 HTTP 缓存失效、跨源安全策略或独立持久缓存。URL 合并由宿主回调负责，WM 宿主使用 `InternetCombineUrlA`，core 本身不绑定传输层。
- **并发约束**：在确认 libdom/libcss/NetSurf 移植层的线程安全前，不能让 worker 与 UI 同时操作同一 document 或共享全局 viewport context；过期请求只丢弃结果，不使用强制终止线程。
- **第一阶段完成条件**：慢网主文档 GET 期间旧页可滚动，loading 可见；成功后才 swap，错误保留当前页面，关闭窗口不会遗留线程。
- **当前完成条件**：TEST43 的 URL/去重/成功/失败断言通过；真实 TEST13 的 CSS/图片网络等待不阻塞 UI，generation 正确，成功后才 swap，失败资源保留 fallback。

### 图片格式已覆盖，新的公共 retained 位图 ABI 待验收

WM Imaging 的 BMP/PNG/JPEG/GIF 均已在设备通过尺寸探测和 Draw 返回，但首轮多格式 fixture 的可见性与旧截断 BMP 不足以完成视觉验收。当前 `<img>` 解码失败时仍刻意回退到 alt/src 文本。

- **当前结论**：BMP/PNG/JPEG/GIF 四格式与旧 TEST20 缓存 `<img>` 已由设备视觉确认。2026-07-15 新增的公共位图句柄由创建线程使用和释放；DLL 复制编码输入并保留 WM `IImage`，NetSurf 重绘复用该句柄，旧 `PCore_Image*` 只做兼容转发。它已通过 VS2008 ARM 编译和 PE 导入/导出检查，但仍须 TEST19/20 真机确认后才能关闭迁移。为保证 WM Imaging 的惰性解码数据源始终有效，句柄存活期间会同时保留一份编码字节；core 的 document cache 也保留原字节以支持重布局，因此当前以额外编码内存换取重绘不重复解码。TEST25-37/13 的 SVG 真机结论保持不变。CSS 背景仍不含 background-size 和多层背景；SVG 仍缺复杂 shaping、`textPath`、逐字 dx/dy、任意 shear、径向焦点 `fx/fy` 或 spread method。单次栅格源缓冲限制为 1,048,576 像素，超大输出会降低内部采样分辨率后再缩放。
- **完成条件**：每种宣称支持的格式均有内存单测和真实 Browse 页面实例，且资源失败仍保留可访问 fallback。

## 维护规则

1. 每次真机截图改变结论时，同时更新本文件、`HANDOFF.md`、`ROADMAP.md` 和根目录 `README.md`。
2. 测试名称后的“OK”只说明其明确断言成立；必须同时写出它没有覆盖的范围。
3. 新增临时 stub、降级、硬编码测试尺寸或线程取舍时，先在此登记后续任务和完成条件。

# 已验证基线与待消除限制

更新时间：2026-07-25

这份清单把“已经在设备上验证的最小链路”和“当前刻意保留的阶段性实现”分开记录。未被列为完成的项目不得在后续交接、README 或测试结论中表述为完整浏览器能力。

## 已验证基线（不是完整功能声明）

| 范围 | 已验证事实 | 不代表 |
|---|---|---|
| CSS 媒体查询与 token | TEST 21 已在设备确认运行时 viewport/DPI、旧式 min/max-width 及整数像素 MQ4 `width <=` / `width <`；TEST38-39 又确认同表顶层 `:root` token 语义与正式 redraw。 | 所有媒体特性、MQ4 范围、元素作用域或完整 custom properties 均已覆盖。 |
| 现代 CSS 值 | TEST40 已在设备确认：数值型 `oklch()` 转裁剪 sRGB，并求值无需布局上下文的同单位 `calc()` 四则运算。 | 完整 gamut mapping、`none`/复杂角度、`color-mix()`、混合单位 calc 或 CSS Color/Values 均已实现。 |
| 反向 flex 内边距 | TEST 22 已在设备上确认：224px viewport 下，`row-reverse`、左右 25px padding、隐藏侧栏时，主内容为 `x=25,width=174`。 | 完整 Flexbox 规范或任意真实站点的复杂 flex 均已兼容。 |
| IANA 窄屏页 | TEST13 起始页和 `Example Domains` 已可读；TEST41 的竖横屏截图确认 `/numbers` grid 宽表格不再把主内容推到左边界外。next80 已修复 libcss 父 bloom 节点数据过早销毁；TEST56/58/59/60 与真实 `/domains/reserved` 横竖屏均已通过，首个 `Domain` 表头恢复正常。 | 任意 IANA 子页版式通过，或页面已达到现代浏览器还原度。 |
| 嵌套 overflow | NetSurf 3.11 scrollbar 已接入；TEST42 的离屏步进断言及真机箭头/thumb 交互通过，host 拖动只重绘对应 overflow viewport。next54 的 fixed-height 回归已在 next55 收窄，用户确认 auto-height 空间、箭头、短页纵条与色块页正常。 | 不代表惯性触摸、overlay scrollbar 或任意嵌套组合均已覆盖。 |
| table span/归一化/折叠边框 | NetSurf 3.11 span occupancy 与匿名 row/cell 已由 TEST46/47 验收。next64/TEST53 至 next68/TEST56 已覆盖常见 collapsed-border、cell alignment/empty-cells 与显式 table height；next73/TEST57 又确认 25/50/auto 百分比 row 分配及超约束缩放。 | 尚不覆盖任意 inline/float/form 畸形组合、caption/column 归一化、`col`/`colgroup` border 来源、百分比 cell/后代内容、跨行 baseline 或所有复杂表格边界。 |
| author-level inline CSS | 外部 author stylesheet 正常参与 libcss 选择；TEST57 使用外部类规则通过。next75/TEST58 已确认 NetSurf 式声明列表解析、libcss inline cascade、继承、后代 class 选择与正式布局/重绘。next81 已把全零 `nsoption` shim 改成具名默认，未知读取会编译失败；TEST56/58-61 已由设备确认。 | 正式构盒不调用 NetSurf `box_construct.c`；旧缺口是 `pcore_style_subtree` 固定给 `css_select_style` 传 `NULL`。具名 option 不能被误写成 inline CSS 的直接开关。 |
| Forms/widgets | next85 已按 NetSurf gadget 数据形态接入只读 checkbox/radio，并通过 1em geometry、selected redraw、hidden-input 和动态 `0.2em` 间距设备验收。 | 没有点击切换、radio 分组、键盘焦点、文本框/textarea/select、表单编码或提交，也没有把 WM 原生子窗口嵌入文档。 |
| 列表 marker | next57/59 已确认基础 marker 与字体；next61/TEST50 已确认 libcss 上游 47 种 counter formatter、document-cache `list-style-image` 与失败类型回退；next62/TEST51、next63/TEST52 已确认 inline-first 及 block-first/空条目/嵌套/图片的 `list-style-position:inside`。 | 不代表 float 邻接 marker、自定义 `@counter-style` 或完整 CSS Lists。普通语言字体不属于当前 marker 工作范围。 |
| 字体 fallback | next59 随包部署约 901 KiB 的三份静态 Positron Symbols/Emoji（来自 Noto OFL），精确 cmap 选择统一用于 GDI 测量、换行命中与绘制 run；设备确认箭头/marker/五个 emoji 可见且比 next58 稍平滑。当前范围明确只支持符号与单色 emoji fallback。 | 不计划在本阶段加入普通语言/多语种字体；也没有复杂 ZWJ/variation shaping、彩色 emoji、网页 `@font-face` 或字体下载。`ANTIALIASED_QUALITY` 最终效果仍依赖 OEM GDI。 |
| 图片 | TEST19/20 已确认公共 retained 位图 ABI、WM Imaging 四格式和核心缓存复用；TEST25-37/13 已确认 SVG 链。 | 复杂 SVG text、任意渐变、复杂 CSS 背景、跨线程图片句柄或任意网络图片均已通过。 |
| ENGINE 离线回归 | 2026-07-11 用户确认原整组至 TEST24 通过；2026-07-12 又单独确认 TEST25 SVG parse。TEST23 的浮动实现已因真实 Browse 回归撤回。 | 网络 Browse、GDI Render 组，或未被这些测试覆盖的真实页面兼容性均已通过。 |
| 旋转尺寸 | `WM_SIZE` 以新 client 宽高从 document CSS 缓存 restyle + layout；TEST24 已确认跨断点重选、无联网及滚动比例，真实 TEST13 横竖屏也保持同一阅读区域。 | 所有媒体语法和任意样式资源均已覆盖。 |

## 真实页面观察到的未完成项

### IANA 根变量布局已改善，完整真实页仍继续观察

TEST38-39 真机确认根变量语义及 25px inset 后，新的 TEST13 截图中 IANA logo、导航、正文与注册表两列均未再裁切或重叠，custom-properties 导致的窄屏间距根因可以关闭。普通文本空白折叠也已由 TEST13/15 确认。当前结论仍不是任意 IANA 子页或任意真实站点都已完整还原。

- **可能范围**：剩余 flex/table/inline/字体或未实现 CSS 特性的组合；尚未把单一原因当作结论。
- **已撤回的一项**：IANA 页脚是 table cell 内 `display:inline; float:left` 列表。TEST23 曾在最小样例中确认两个浮动块同行及 `clear:both`，但将该构盒规则直接接入真实页面后，2026-07-11 Browse 截图出现严重错位和替代方框；实现已撤回。该测试不再参加 ENGINE 组，不能作为 float 支持证据。
- **当前站点版本风险**：2026-07-13 重新读取到 IANA 的 `iana_website.80c103cc08b6.css`；除已确认的 `var(--space-*)` 外还有 22 处 `oklch()`、15 处 `calc()`、`color-mix()`、grid/gap 与 `:has()`。新兼容模块只处理数值型 OKLCH 和可完全求值的同单位 calc；混合单位及其他现代能力仍会降级。
- **最新子页结论**：`/numbers` 使用 `display:grid`，其中 `.dtable-wrap { overflow:auto }` 包住宽表格，TEST41 已确认该路径。`/domains/reserved` 没有 Grid 包装层，但同样以 `.dtable-wrap { overflow:auto }` 包住宽表格。next77 已让 flex main 在竖屏保持正确 inset；旋转到横屏后，wrapper 本身位置正常，但第一格内容左移约 18px，恰好抵消作者的 `padding-left:18px`，导致 `Domain` 贴到 clip edge，视觉上连字体/样式也异常。非拉丁字符 tofu 与这个英文表头问题无关，并按项目范围保留。
- **失败实验**：next78 在 layout 末尾递归 `scrollbar_set(...,0)` 后，真实页横屏从首个 `Domain` 异常扩大为全部表格单元格异常，TEST56 随后失败并触发系统级异常。该实验及其诊断 API/扩展 TEST59 已撤回，旧包已改名为 `C:\WMShare\Positron-next78-FAILED-DO-NOT-USE`。
- **已验收边界**：next80 的 TEST56/58/59/60 与 TEST13 起始页、`Open example`、IANA `/domains/reserved` 横竖屏均通过。仍禁止恢复 next78 的全局 scrollbar 回调重置；这次闭环不等于所有 IANA 子页或完整现代 CSS 已实现。
- **完成条件**：在目标设备的竖屏和横屏下，主内容、页脚和导航均不裁切、不重叠，且没有明显错误图标/替代字符；结果需要新的真机截图确认。

### 旋转 responsive restyle 已完成当前验收

`WM_SIZE` 现调用 `PCore_SetViewport`、基准 URL 感知的 `PCore_StyleDocumentEx2` 和 `PCore_LayoutDocument`。外链 CSS 与成功导入首次导航时以原始字节缓存到 document，尺寸变化只从该缓存重选 `@media`，不重新联网。旧 `PCore_StyleDocumentEx` 保持兼容。TEST24 已于 2026-07-11 在设备确认 320px 到 299px 的旧接口外链 CSS 重选，fetch/free 都保持一次；TEST45 又确认导入树的 cache-only 重选。

- **当前取舍**：只缓存最多 32 份、单份不超过 256 KiB、每 document 合计不超过 512 KiB 的成功外链 CSS 原始字节；缓存未命中的样式在旋转时保持缺失，不能在 `WM_SIZE` 中重新联网。
- **设备结论**：TEST24 的 0/50/100% 比例断言通过；真实 TEST13 从竖屏 `Further Reading / Domain Names` 区域旋转到横屏后仍停留在同一区域。扩大 MQ4 语法或处理 custom properties 仍是独立兼容性工作。
- **完成条件**：旋转前后跨越 TEST 21 式断点时，computed style 与几何都切换正确，并恢复原滚动位置的合理比例。

### 导航 CSS/图片抓取已异步，最终提交仍在 UI

主文档之后的外链 CSS、CSS `@import`、`<img>` 和应用样式后发现的背景 URL 现也分轮交给同一 worker；HTTP 字节通过 `WM_APP` 消息交回窗口线程，DOM/libcss/NetSurf/GDI 从不跨线程。TEST3/43/44 与真实 TEST13 已确认真实正文进度、后台资源阶段、成功 swap 和主文档失败回滚。parse/style/image-discovery/layout 现用一次性 WM timer 在调用之间让出消息循环；单个不可重入调用仍可能卡顿。

- **当前取舍**：同一时刻只允许一个导航请求；旧页可绘制和滚动，但加载中再次点击链接会被忽略。HTML parse、style、cache copy 和 layout 仍在 UI 提交阶段同步执行，全部网络完成后仍可能短暂卡顿。
- **已验收观测**：next86 的 TEST13 报 total/network/max-UI=6435/5503/673ms，parse/style/images/layout/paint=11/182/6/673/36ms；2 个资源全部成功，document/cache=10499/121111 bytes，预算拒绝为 0。网络主导总时长，layout 主导单次 UI 停顿；该数据用于定位，不是已完成的调度优化，也不是产品遥测上传。
- **资源预算**：`test_host` 最多暂存 64 个去重 URL、合计 2 MiB 原始字节，成功提交时 core 会复制所需数据后立刻释放事务。该值用于限制 WM 峰值，是可替换的宿主策略，不是 `positron_core` ABI 或最终页面的硬上限。
- **后续实现**：单响应 `Content-Length`/progress 回调已实现并由 TEST3/13 确认；`@import` 事务已由 TEST45 确认。整页多资源聚合进度、web fonts、脚本及更广资源类型仍未实现。
- **CSS import 边界**：最多追踪 16 层递归和本次样式 pass 的 64 个解析表；失败、循环和超深导入按 libcss 契约注册空表。成功导入复用每 document 最多 32 份/512 KiB 的 CSS 字节缓存；不含 HTTP 缓存失效、跨源安全策略或独立持久缓存。URL 合并由宿主回调负责，WM 宿主使用 `InternetCombineUrlA`，core 本身不绑定传输层。
- **并发约束**：在确认 libdom/libcss/NetSurf 移植层的线程安全前，不能让 worker 与 UI 同时操作同一 document 或共享全局 viewport context；过期请求只丢弃结果，不使用强制终止线程。
- **第一阶段完成条件**：慢网主文档 GET 期间旧页可滚动，loading 可见；成功后才 swap，错误保留当前页面，关闭窗口不会遗留线程。
- **当前完成条件**：TEST43 的 URL/去重/成功/失败断言通过；真实 TEST13 的 CSS/图片网络等待不阻塞 UI，generation 正确，成功后才 swap，失败资源保留 fallback。

### 图片格式与公共 retained 位图 ABI 已完成当前验收

WM Imaging 的 BMP/PNG/JPEG/GIF 均已在设备通过尺寸探测和 Draw 返回，但首轮多格式 fixture 的可见性与旧截断 BMP 不足以完成视觉验收。当前 `<img>` 解码失败时仍刻意回退到 alt/src 文本。

- **当前结论**：BMP/PNG/JPEG/GIF 四格式与 TEST20 缓存 `<img>` 已由设备视觉确认。2026-07-15 next45 又确认公共位图句柄的四格式颜色、清空调用方输入后的重复绘制、损坏输入拒绝、旧 `PCore_Image*` 转发和 NetSurf retained redraw；TEST13/26/27 同批无回归。句柄仍只允许在创建线程使用和释放。为保证 WM Imaging 的惰性解码数据源始终有效，句柄存活期间会保留一份编码字节；core 的 document cache 也保留原字节以支持重布局，因此当前以额外编码内存换取重绘不重复解码。TEST25-37/13 的 SVG 真机结论保持不变。CSS 背景仍不含 background-size 和多层背景；SVG 仍缺复杂 shaping、`textPath`、逐字 dx/dy、任意 shear、径向焦点 `fx/fy` 或 spread method。单次栅格源缓冲限制为 1,048,576 像素，超大输出会降低内部采样分辨率后再缩放。
- **独立消费与编码**：next46 已确认只导入 `positron_image.dll`/`COREDLL.dll` 的 ABI 1.0 示例横竖屏工作；next47 确认 ABI 1.1 的 PNG/JPEG 内存编码与释放/回读闭环；next48 证明 WM quality=100 不能修复小图色度串扰。ABI 1.2 保留 quality=-1 的 WM 默认路径，显式 0..100 使用静态 libjpeg-turbo 1.5.3 4:4:4，next49 已确认行方向、颜色和 SOF 采样正确。ARMV4I 构建无 SIMD，显式编码会额外生成约 `width*height*3` 字节的 24bpp 中间位图；Debug DLL 增加约 238 KiB，但静态 `.lib`、源码和独立 JPEG DLL 均不部署。next50 截图确认 ABI 1.3 的复制式 padded BGR24/BGRA32、RGB/alpha PNG、JPEG 与 SVG 视觉正确；next51 又确认 ABI 1.4 的 BMP/GIF 系统 encoder、签名与回读成功。next51 的退出仍失败：WM 标题栏 X 是 Smart Minimize，不保证发送 `WM_CLOSE`。next52 以系统 `aygshell.dll` 的 `SHDoneButton` 换成标题栏 OK，并由 `IDOK` 真退出；用户已确认任务管理器进程消失且可再次启动。这不会增加底部软键或占用客户区。跨线程句柄仍未提供。
- **完成条件**：每种宣称支持的格式均有内存单测和真实 Browse 页面实例，且资源失败仍保留可访问 fallback。

## 维护规则

1. 每次真机截图改变结论时，同时更新本文件、`HANDOFF.md`、`ROADMAP.md` 和根目录 `README.md`。
2. 测试名称后的“OK”只说明其明确断言成立；必须同时写出它没有覆盖的范围。
3. 新增临时 stub、降级、硬编码测试尺寸或线程取舍时，先在此登记后续任务和完成条件。

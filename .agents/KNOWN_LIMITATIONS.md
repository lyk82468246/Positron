# 已验证基线与待消除限制

更新时间：2026-07-11

这份清单把“已经在设备上验证的最小链路”和“当前刻意保留的阶段性实现”分开记录。未被列为完成的项目不得在后续交接、README 或测试结论中表述为完整浏览器能力。

## 已验证基线（不是完整功能声明）

| 范围 | 已验证事实 | 不代表 |
|---|---|---|
| CSS 媒体查询 | TEST 21 已在设备上确认：运行时 client viewport 与 DPI 进入 libcss，320px 命中 `min-width:300px`，299px 命中 `max-width:299px`。 | 所有媒体特性和旋转场景均已覆盖。 |
| 反向 flex 内边距 | TEST 22 已在设备上确认：224px viewport 下，`row-reverse`、左右 25px padding、隐藏侧栏时，主内容为 `x=25,width=174`。 | 完整 Flexbox 规范或任意真实站点的复杂 flex 均已兼容。 |
| IANA 窄屏页 | 撤回 TEST23 对应实现后的最新 TEST 13 截图确认：灾难性的正文重叠已消失，此前约 25px 的左缘裁切也未复现，`Example Domains` 可读。 | TEST 13 版式通过，或页面已达到原浏览器/现代浏览器的还原度。 |
| 图片 | TEST 18、19、20 已分别确认资源去重、WM Imaging 内存 BMP 解码/绘制、缓存 `<img>` 进入 NetSurf `box->object -> content_redraw -> plot_bitmap` 链。 | PNG/JPEG/GIF、SVG、背景图或任意网络图片均可显示。 |
| ENGINE 离线回归 | 2026-07-11 用户确认 TEST 6-11、15、16、18、21、22、24 通过。TEST23 的浮动最小样例曾通过，但对应实现已因真实 Browse 回归撤回。 | 网络 Browse、GDI Render 组，或未被这些测试覆盖的真实页面兼容性均已通过。 |
| 旋转尺寸 | `WM_SIZE` 以新 client 宽高从 document CSS 缓存 restyle + layout；TEST24 已确认 320px/299px 跨断点重选且 fetch/free 都为 1。 | 真实 Browse 页面、所有媒体语法和任意样式资源均已通过旋转验收。 |

## 真实页面观察到的未完成项

### IANA 窄屏布局仍非验收通过

撤回 TEST23 对应实现后的最新 TEST 13 已确认灾难性回归消失、左缘裁切未复现，但截图仍可见：导航在窄屏右侧纵向堆叠，页脚两列严重拥挤并产生不理想断行；`Homepage` 后和正文中的方框也说明 SVG/图标、字形或替代资源尚未正确呈现。当前结论只能是“旧的可读基线恢复”，不是“TEST 13 符合最终预期”或“版式通过”。

- **可能范围**：剩余 flex/table/inline/字体或未实现 CSS 特性的组合；尚未把单一原因当作结论。
- **已撤回的一项**：IANA 页脚是 table cell 内 `display:inline; float:left` 列表。TEST23 曾在最小样例中确认两个浮动块同行及 `clear:both`，但将该构盒规则直接接入真实页面后，2026-07-11 Browse 截图出现严重错位和替代方框；实现已撤回。该测试不再参加 ENGINE 组，不能作为 float 支持证据。
- **当前站点版本风险**：2026-07-11 读取到 IANA 改用带哈希的 CSS 资源，且其中有 CSS custom properties 与 `@media (width <= 1000px)` 范围语法。TEST 21 只覆盖旧式 `min-width` / `max-width`；不得假定当前线上样式完全被 libcss 3.11 解析。
- **下一步**：基线恢复已经由最新截图确认。随后必须对比上游 `box_construct.c` 的 float 前后处理、匿名盒 normalisation 与 list marker，并分别审计当前 IANA 样式中的 custom properties、MQ4 范围语法和 SVG 资源，再设计新的端到端回归，而不是复用 TEST23 的简化断言。
- **完成条件**：在目标设备的竖屏和横屏下，主内容、页脚和导航均不裁切、不重叠，且没有明显错误图标/替代字符；结果需要新的真机截图确认。

### 旋转 responsive restyle 待真实页面验收

`WM_SIZE` 现调用 `PCore_SetViewport`、缓存专用 `PCore_StyleDocumentEx` 和 `PCore_LayoutDocument`。外链 CSS 首次导航时以原始字节缓存到 document，尺寸变化只从该缓存重选 `@media`，不重新联网。TEST24 已于 2026-07-11 在设备确认 320px 到 299px 的外链 CSS 重选，fetch/free 都保持一次。

- **当前取舍**：只缓存最多 32 份、单份不超过 256 KiB、每 document 合计不超过 512 KiB 的成功外链 CSS 原始字节；缓存未命中的样式在旋转时保持缺失，不能在 `WM_SIZE` 中重新联网。
- **后续实现**：验证真实 Browse 页面旋转前后跨断点的样式和滚动位置；处理 custom properties/新式媒体查询语法是独立兼容性工作。
- **完成条件**：旋转前后跨越 TEST 21 式断点时，computed style 与几何都切换正确，并恢复原滚动位置的合理比例。

### 导航仍同步阻塞 UI

点击链接后，`PHttp_Get`、HTML parse、外部 CSS fetch、图片 fetch、style 和 layout 都在窗口线程执行。请求未返回前，旧页面不能继续响应，也没有 loading 指示。

- **当前取舍**：同步路径较短且已验证 document 仅在新页面完整 layout 后才 swap，失败时保留旧文档。
- **后续实现**：导航 generation + worker fetch + `PostMessage` 回 UI 线程提交；旧 `g_render_doc` 在新文档成功前保持可绘制可滚动。第一阶段使用不定量 loading 状态；真实百分比需给 `positron_http` 增加 content-length/progress 回调。
- **并发约束**：在确认 libdom/libcss/NetSurf 移植层的线程安全前，不能让 worker 与 UI 同时操作同一 document 或共享全局 viewport context；过期请求只丢弃结果，不使用强制终止线程。
- **完成条件**：慢网加载时旧页可滚动/可点击，loading 可见；最新请求才允许 swap；错误和过期结果不会替换当前页面。

### 图片能力只以 BMP 最小链路为基线

WM Imaging 在本设备上对内存 BMP 已确认可用；先前内存 PNG 解码失败。当前 `<img>` 解码失败时刻意回退到 alt/src 文本，不把失败伪装成已显示图片。

- **后续实现**：以独立的设备测试分别验证小 PNG、JPEG、GIF，记录 WM Imaging 的 HRESULT；SVG 和 CSS background image 是独立工作项。
- **完成条件**：每种宣称支持的格式均有内存单测和真实 Browse 页面实例，且资源失败仍保留可访问 fallback。

## 维护规则

1. 每次真机截图改变结论时，同时更新本文件、`HANDOFF.md`、`ROADMAP.md` 和根目录 `README.md`。
2. 测试名称后的“OK”只说明其明确断言成立；必须同时写出它没有覆盖的范围。
3. 新增临时 stub、降级、硬编码测试尺寸或线程取舍时，先在此登记后续任务和完成条件。

# 已验证基线与待消除限制

更新时间：2026-07-11

这份清单把“已经在设备上验证的最小链路”和“当前刻意保留的阶段性实现”分开记录。未被列为完成的项目不得在后续交接、README 或测试结论中表述为完整浏览器能力。

## 已验证基线（不是完整功能声明）

| 范围 | 已验证事实 | 不代表 |
|---|---|---|
| CSS 媒体查询 | TEST 21 已在设备确认：运行时 viewport/DPI、旧式 min/max-width 及整数像素 MQ4 `width <=` / `width <` 均在 320/300/299px 正确命中边界。 | 所有媒体特性、MQ4 范围、custom properties 和旋转场景均已覆盖。 |
| 反向 flex 内边距 | TEST 22 已在设备上确认：224px viewport 下，`row-reverse`、左右 25px padding、隐藏侧栏时，主内容为 `x=25,width=174`。 | 完整 Flexbox 规范或任意真实站点的复杂 flex 均已兼容。 |
| IANA 窄屏页 | 撤回 TEST23 对应实现后的最新 TEST 13 截图确认：灾难性的正文重叠已消失，此前约 25px 的左缘裁切也未复现，`Example Domains` 可读。 | TEST 13 版式通过，或页面已达到原浏览器/现代浏览器的还原度。 |
| 图片 | TEST 18、19、20 已分别确认资源去重、WM Imaging 内存 BMP 解码/绘制、缓存 `<img>` 进入 NetSurf `box->object -> content_redraw -> plot_bitmap` 链。 | PNG/JPEG/GIF、SVG、背景图或任意网络图片均可显示。 |
| ENGINE 离线回归 | 2026-07-11 用户确认 TEST 6-11、15、16、18、21、22、24 通过。TEST23 的浮动最小样例曾通过，但对应实现已因真实 Browse 回归撤回。 | 网络 Browse、GDI Render 组，或未被这些测试覆盖的真实页面兼容性均已通过。 |
| 旋转尺寸 | `WM_SIZE` 以新 client 宽高从 document CSS 缓存 restyle + layout；TEST24 已确认跨断点重选、无联网及滚动比例，真实 TEST13 横竖屏也保持同一阅读区域。 | 所有媒体语法和任意样式资源均已覆盖。 |

## 真实页面观察到的未完成项

### IANA 窄屏布局仍非验收通过

撤回 TEST23 对应实现后的最新 TEST 13 已确认灾难性回归消失、左缘裁切未复现，但截图仍可见导航和页脚拥挤。普通文本空白折叠修复已由 TEST13 确认：源码 LF 方框消失且词间距正常；补齐 UA CSS 后 TEST15 也已确认 `normal_ws=ok pre_lf=kept`。当前结论仍不是“TEST 13 符合最终预期”或“版式通过”。

- **可能范围**：剩余 flex/table/inline/字体或未实现 CSS 特性的组合；尚未把单一原因当作结论。
- **已撤回的一项**：IANA 页脚是 table cell 内 `display:inline; float:left` 列表。TEST23 曾在最小样例中确认两个浮动块同行及 `clear:both`，但将该构盒规则直接接入真实页面后，2026-07-11 Browse 截图出现严重错位和替代方框；实现已撤回。该测试不再参加 ENGINE 组，不能作为 float 支持证据。
- **当前站点版本风险**：2026-07-11 读取到 IANA 改用带哈希的 CSS 资源，且其中有 CSS custom properties 与大量 `@media (width <= Npx)` / `(width < Npx)`。当前兼容层只改写整数 px 的这两种左侧 width 形式；其他单位、反向/双边范围、custom properties 均未支持。
- **下一步**：从当前 TEST13 页脚/导航问题提取最小结构，再对比上游 `box_construct.c` 的 float 前后处理、匿名盒 normalisation 与 list marker，并独立处理 SVG/custom properties，而不是复用 TEST23 的简化断言。
- **完成条件**：在目标设备的竖屏和横屏下，主内容、页脚和导航均不裁切、不重叠，且没有明显错误图标/替代字符；结果需要新的真机截图确认。

### 旋转 responsive restyle 已完成当前验收

`WM_SIZE` 现调用 `PCore_SetViewport`、缓存专用 `PCore_StyleDocumentEx` 和 `PCore_LayoutDocument`。外链 CSS 首次导航时以原始字节缓存到 document，尺寸变化只从该缓存重选 `@media`，不重新联网。TEST24 已于 2026-07-11 在设备确认 320px 到 299px 的外链 CSS 重选，fetch/free 都保持一次。

- **当前取舍**：只缓存最多 32 份、单份不超过 256 KiB、每 document 合计不超过 512 KiB 的成功外链 CSS 原始字节；缓存未命中的样式在旋转时保持缺失，不能在 `WM_SIZE` 中重新联网。
- **设备结论**：TEST24 的 0/50/100% 比例断言通过；真实 TEST13 从竖屏 `Further Reading / Domain Names` 区域旋转到横屏后仍停留在同一区域。扩大 MQ4 语法或处理 custom properties 仍是独立兼容性工作。
- **完成条件**：旋转前后跨越 TEST 21 式断点时，computed style 与几何都切换正确，并恢复原滚动位置的合理比例。

### 导航仅主文档 GET 已异步

第一阶段已把主文档 `PHttp_Get` 移到单一 worker；HTTP response 通过 `WM_APP` 消息交回窗口线程，DOM/libcss/NetSurf/GDI 从不跨线程。设备已确认旧页可滚动且成功后正常换页。父窗口条带出现复制残影，`STATIC` 子窗口又完全不可见；现已按 WM6 SDK 改为 `PROGRESS_CLASS` Common Control，待设备复测。若仍异常则挂起该视觉问题。失败分支待测。

- **当前取舍**：同一时刻只允许一个主文档请求；旧页可绘制和滚动，但加载中再次点击链接会被忽略。HTML parse、外部 CSS fetch、图片 fetch、style 和 layout 仍在 UI 提交阶段同步执行，主文档返回后仍可能短暂卡顿。
- **后续实现**：把资源发现与 CSS/图片 fetch 组织成后台事务，再由 generation 校验最新结果后提交。真实百分比需给 `positron_http` 增加 content-length/progress 回调。
- **并发约束**：在确认 libdom/libcss/NetSurf 移植层的线程安全前，不能让 worker 与 UI 同时操作同一 document 或共享全局 viewport context；过期请求只丢弃结果，不使用强制终止线程。
- **第一阶段完成条件**：慢网主文档 GET 期间旧页可滚动，loading 可见；成功后才 swap，错误保留当前页面，关闭窗口不会遗留线程。
- **完整完成条件**：资源 fetch 也不阻塞 UI；支持 generation 后只有最新请求允许 swap，过期结果不会替换当前页面。

### 图片能力只以 BMP 最小链路为基线

WM Imaging 在本设备上对内存 BMP 已确认可用；先前内存 PNG 解码失败。当前 `<img>` 解码失败时刻意回退到 alt/src 文本，不把失败伪装成已显示图片。

- **后续实现**：以独立的设备测试分别验证小 PNG、JPEG、GIF，记录 WM Imaging 的 HRESULT；SVG 和 CSS background image 是独立工作项。
- **完成条件**：每种宣称支持的格式均有内存单测和真实 Browse 页面实例，且资源失败仍保留可访问 fallback。

## 维护规则

1. 每次真机截图改变结论时，同时更新本文件、`HANDOFF.md`、`ROADMAP.md` 和根目录 `README.md`。
2. 测试名称后的“OK”只说明其明确断言成立；必须同时写出它没有覆盖的范围。
3. 新增临时 stub、降级、硬编码测试尺寸或线程取舍时，先在此登记后续任务和完成条件。

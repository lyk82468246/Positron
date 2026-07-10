# Positron Roadmap

更新时间：2026-07-10
基线：Phase 4 已完成 M7-flex + M7-table，正式 Browse 路径走 NetSurf `layout_document` + `html_redraw`。M5f border、selector、TEST 11 正反样例与图片资源发现/fetch 已于 2026-07-10 真机通过；文档级图片缓存与 TEST 19 WM Imaging 原生图片绘制待复编回归。

## 总原则

Positron 是给 WM6 打补丁，不是拆掉 WM6 重建。

- WM6 已经做得够用的部分，优先用系统能力：WinInet、GDI、WM Imaging API、coredll。
- WM6 做不到现代要求的部分，才自研/移植：现代 TLS、现代 HTML/CSS 渲染、后续 JS runtime。
- 页面还原度优先于“随便降级”：如果 GDI 没有 dashed pen，就手绘 dashed border；如果 WinInet 不能现代 TLS，就 mbedTLS 补上。

## 短期规划

目标：把当前“已经能浏览真实网页”的 NetSurf 路径打磨到更可信、更像网页。

### 1. M5f：真实 border 绘制验证

当前进展：

- NetSurf `content/handlers/html/redraw_border.c` 已加入 `positron_core.vcproj`。
- `pcore_layout_stubs.c` 里的 `html_redraw_borders` / `html_redraw_inline_borders` no-op 已移除。
- `redraw_border.c` 已用 `scripts/c89ize.py` 做 C89 化，脚本也补了 `plot_style_t` / `plot_font_style_t` 简单 designated initializer 规则。
- 2026-07-08 用户真实 VS2008 编译暴露 `redraw_border.c` include 前置依赖不足（`html/private.h` 中 `dom_document` / `dom_node` / `bool` 连锁错误）；已按 `layout.c`/`redraw.c` 补齐 dom/css/content 前置 include。
- 2026-07-10 已成功复编，TEST 17 真机可见 H1、flex、table/cell 边框并通过。

源码接入与真机验收已完成；后续用真实 Browse 页面继续观察复杂 border 风格。

验收：

- TEST 17 能看到 H1 下边框、flex 容器 dashed border、块级和 table/cell 相关边框。
- TEST 13 打开 example/iana 类页面，导航/表格/分隔线观感明显提升。

如果编译报 C89 语法错误，优先跑/改 `scripts/c89ize.py`，再做手工修补；不要只在 vendored 源里一次性手改。

### 2. CSS selector 补强验证

当前进展：

- attribute selectors：`[foo]`、`[foo=bar]`、`[foo*=bar]` 等已在 `pcore_select.c` 实现。
- adjacent/general sibling：`+` / `~` 已实现。
- `:link` / `:lang()` 已实现。
- TEST 9 已扩展为离线 computed-style 验收，覆盖 attribute + sibling + static pseudo selector 组合，并于 2026-07-10 真机通过。
- 动态状态伪类仍 false。

优先级建议：

1. 结合 TEST 13 看真实页面 CSS 套用是否更完整。
2. 后续再按页面痛点补其他静态伪类。

验收：

- TEST 9 中 `[title]` / `[data-role=]` / `[class~=]` / `[lang|=]` / `[data-code^=]` / `[data-code$=]` / `[data-code*=]` / `h1 + p` / `h1 ~ span` / `a:link:lang(zh)` 都能影响 computed style。
- 再跑 TEST 13 看真实页面 CSS 套用是否更完整。

### 3. ENGINE 回归可观测性

- TEST 11 同时覆盖折叠组 `body.y=p.y=16` 和 `padding-top:1px` 阻断组 `body.y=8,p.y=25`，已由 2026-07-10 用户真机截图确认通过。
- TEST 11/15/16/18 改为收集失败后继续执行，避免较早断言遮住后续结果。
- 待 WM6 复编确认 TEST 15、16、18 仍通过。

## 中期规划

目标：让 TEST 13 从“能打开简单页面”变成“能浏览一批轻量真实页面”。

### 1. 图片与 SVG

当前状态：

- `<img>` 已先在 `pcore_box.c` 接入 alt/src 文本占位，并由 TEST 17 于 2026-07-10 真机验证。
- 旧 TEST 18 的 `<img src>` 资源发现/fetch 已真机通过；当前源码将成功字节复制到 document user-data 缓存，并按 URL 去重。embedder 缓冲仍由 `freefn` 立即释放；核心副本随文档释放。
- 已新增 `pcore_wmimage.cpp` C++ 小适配层，调用 WM Imaging API 的 `CreateImageFromBuffer` / `IImage::Draw`，并在 TEST 19 中用内存 2x2 BMP 验证原生解码/绘制；待复编真机确认。内存 PNG 首次真机反馈为 decode fail，需等 BMP 基线通过后再做格式覆盖。
- `plot_bitmap` 是 stub。
- `box->object/background` 相关内容基本为空。
- SVG logo、PNG/JPEG 图片仍不会真实显示，只会在 `<img>` 有 alt/src 时显示文本占位。

建议顺序：

1. 先复编新版 TEST 18，验证二次扫描 first/second 均为 `2/2` 且 fetch calls 保持 2。
2. 复编 TEST 19，验证 WM Imaging 能从内存 BMP 取到 2x2 尺寸并用 `IImage::Draw` 画出 red/green + blue/yellow 方块。
3. 将文档缓存字节接回 NetSurf bitmap / plot_bitmap。
4. SVG 可后置，必要时先占位或引入 libsvgtiny。

验收：

- TEST 17 可见 `Image fallback: Logo`。
- TEST 18 显示 `image cache: first=2/2 second=2/2; fetch calls=2`。
- TEST 19 显示 WM Imaging decoded 2x2 BMP and drew it via `IImage::Draw`。
- 本地 HTML + 小 PNG/JPEG 能显示。
- 真实网页 logo/图片不再空白。

### 2. Resource loader

当前外部 CSS 已通过 `PCore_StyleDocumentEx` + fetch callback 拉取。

后续应统一处理：

- CSS
- 图片
- 相对 URL / 根相对 URL
- 简单缓存
- 失败占位
- redirect / http/https 切换

原则：

- `positron_core` 保持 transport-agnostic。
- 网络仍由 embedder/test_host 通过 `positron_http` 提供 fetch。

### 3. 布局补强

已完成：

- block flow
- real NetSurf inline/text layout
- flex
- table 常见路径

仍缺或简化：

- float
- table rowspan 精确跨行占用
- border-collapse 视觉完整度
- overflow scrollbars
- forms/widgets

建议按真实页面痛点推进，不一次性铺开。

### 4. 交互体验

当前点击导航会同步抓取、解析、重排，设备上可能卡住。

建议：

- 点击链接后显示 loading 状态。
- 后台 fetch，完成后 swap document。
- 尽量保持 UI 线程消息循环响应。
- 谨慎跨线程碰 DOM/GDI；必要时用队列把 UI 更新投回窗口线程。

## 长期规划

目标：从“能渲染网页”走向“能写 Positron 应用”。

### 1. JavaScript runtime

候选方向：

- Duktape 更现实：C、轻量、老平台友好。
- QuickJS 较现代，但工具链/体积/移植风险需评估。

第一阶段不要追完整浏览器 JS：

- 简单 DOM 查询/修改。
- 点击事件。
- JS 调 native API。
- native 回调 JS。

### 2. Positron App API

继续保持 DLL 生态：

- `positron_tls`
- `positron_json`
- `positron_http`
- `positron_core`

API 要能被外部 WM6 C app 消费，不只服务 test_host。LocalSend WM6 port 是这个原则的现实驱动。

候选 API：

- 窗口/导航
- 文件读写
- HTTP/fetch
- 简单本地存储
- Native bridge

### 3. 性能与内存

WM6/ARMV4I 资源紧，后续必须持续做：

- CSS/cache 管理。
- 图片缓存与释放。
- 字体缓存清理。
- 重排节流。
- 绘制剔除。
- 大页面 cap 和失败策略。

### 4. 固定回归网页集

建议维护一组轻量网页作为人工 smoke test：

- `example.com`
- iana help/example-domains
- 一个 table 页面
- 一个图片页面
- 一个 flex/nav 页面
- 一个包含 attribute selectors 的页面

每次大改渲染路径后都跑一轮，避免只在内置 TEST 里看起来正常。

## 建议执行顺序

1. TEST 15/16/18 ENGINE 回归 + TEST 19 原生图片绘制回归。
2. 图片基础路径。
3. resource loader 整理。
4. float/table 细化。
5. 后台导航体验。
6. JS runtime spike。

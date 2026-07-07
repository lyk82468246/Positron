# Positron Roadmap

更新时间：2026-07-07  
基线：Phase 4 已完成 M7-flex + M7-table，正式 Browse 路径走 NetSurf `layout_document` + `html_redraw`。Codex 接手后已刷新 README/PHASE4，并已开始 M5f border 接入。

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
- TEST 17 的内置页面与 MessageBox 已加入明确 border 预期。

下一步必须先做 VS2008/WM6 编译和真机确认；当前环境没有可调用的 VS2008/MSBuild。

验收：

- TEST 17 能看到 H1 下边框、flex 容器 dashed border、块级和 table/cell 相关边框。
- TEST 13 打开 example/iana 类页面，导航/表格/分隔线观感明显提升。

如果编译报 C89 语法错误，优先跑/改 `scripts/c89ize.py`，再做手工修补；不要只在 vendored 源里一次性手改。

### 2. CSS selector 缺口补强

当前 `pcore_select.c` 明确 stub：

- attribute selectors：`[foo]`、`[foo=bar]`、`[foo*=bar]` 等。
- adjacent/general sibling：`+` / `~`。
- 动态伪类多数仍 false。

优先级建议：

1. attribute selectors。
2. adjacent/general sibling selectors。
3. `:link` 等静态可判定伪类。

验收：

- 加离线 TEST，验证 `[class]` / `[href^=]` / `h1 + p` 等规则能影响 computed style。
- 再跑 TEST 13 看真实页面 CSS 套用是否更完整。

### 3. 文档刷新

根目录 `README.md` 和 `PHASE4.md` 已落后于 `main`：

- 它们仍像停在手写 block layout / 首张 GDI 页面阶段。
- 实际上 M6/M7 已经切到 NetSurf real layout/redraw/flex/table。

建议单独做 docs commit，不混入功能改动。

## 中期规划

目标：让 TEST 13 从“能打开简单页面”变成“能浏览一批轻量真实页面”。

### 1. 图片与 SVG

当前状态：

- `plot_bitmap` 是 stub。
- `box->object/background` 相关内容基本为空。
- SVG logo、PNG/JPEG 图片都不会显示。

建议顺序：

1. 基础 `<img>` 资源发现和 fetch。
2. PNG/JPEG/GIF 位图解码，优先看 WM Imaging API。
3. 接 NetSurf bitmap / plot_bitmap。
4. SVG 可后置，必要时先占位或引入 libsvgtiny。

验收：

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

1. M5f border。
2. attribute selectors。
3. sibling selectors。
4. docs refresh。
5. 图片基础路径。
6. resource loader 整理。
7. float/table 细化。
8. 后台导航体验。
9. JS runtime spike。

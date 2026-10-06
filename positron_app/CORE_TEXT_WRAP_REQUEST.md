# 交给主线与 DLL：长 URL 的有界 CSS 断行

## 消费者与责任

请在 Core 的 CSS、文字测量和布局路径补齐长文本断行能力，不修改 EXE 的 URL、DB、history 或导航语义。EXE 历史页只是 HTML 消费者；不应手工插入空格、拆分 UTF-8、固定像素宽度或隐藏横向滚动条来模拟排版。

当前历史页 `app_internal_pages.c` 的 `app_internal_visits()` 输出标题、完整 URL、UTC 时间及 Browser 导航栈。没有固定控件宽度，但 `https://www.iana.org/help/example-domains` 这样的无空格文本会溢出。设备日志中该页内容宽 581、实际视口宽 454；截图见仓库本地 `tmp/QQ20261006-232231.png`，tmp 不提交。

Core 的 `pcore_plot_gdi.c:gdi_font_split()` 目前只在空格处分割，无空格字符串返回 unsplittable。仓库 CSS/HTML 路径未找到 `overflow-wrap`、`word-wrap` 或 `word-break` 的实现。这是接管线索，请用最小 fixture 验证，不把源码搜索本身当作设备验收。

## 请求的最小合同

- 优先设计并实现标准的 `overflow-wrap:break-word`，可兼容 `word-wrap:break-word` 别名；默认 `normal` 的行为保持不变。若必须先只支持一个拼写，明确反馈 EXE 应使用哪个。
- 普通有空格文本仍优先使用正常断点；仅在一个不可断文本超过可用行宽时，按真实字体测量进行紧急断行。不以固定字符数、字节数或设备像素常量拆分。
- 分别审查 line layout 与 intrinsic/min-content 尺寸，采用选定 CSS 值的正确语义；不能把 `break-word` 与 `anywhere` 的固有尺寸规则混为一谈。普通 block 中应允许长 URL 在视口内显示，而不凭空撑宽页面。
- 必须保持 UTF-8 标量边界，避免 UTF-16 代理对被拆开；不可出现零长度断行导致死循环。明确组合字符、字形簇及复杂文字的支持边界，不宣称完整 Unicode line breaking。
- 尊重 `white-space:nowrap/pre/pre-wrap` 的相关规则；未声明紧急断行的页面不能被全局强制逐字折行。
- DOM 文本、链接 href、命中、fragment、选择和导航地址不应被改变。CSS、layout、paint 和几何由公共 Core 拥有，test_host 只提供 fixture 和断言。

## 验收与交付

最小 fixture 使用 auto-width 的普通 block，不指定像素控件宽度，覆盖 IANA 完整 URL、长查询参数、无空格长标题、英文短段落、中文与 non-BMP 文本；比较默认 normal 与选择的紧急断行属性。测试 96/128/192 DPI、多种窄视口、旋转后重新布局及文字片段的命中/绘制一致性。保留 nowrap/pre 及既有文本尺寸、图片、表格、TEST13 相邻回归。

执行 C89、仓库审计、串行正式 Debug/Release 构建和匹配完整包的 ARMV4I 自动设备门。不要抢占 EXE 当前构建/设备门，先协调串行窗口。无需为本项新增公共 ABI；若发现确实需要，先提方案。

交付时明确可用 CSS 拼写、支持值、限制和测试证据。EXE 随后只在历史页的 URL/长标题容器设置该样式，并验收实际历史页不超宽。滚动花屏是另一项问题，EXE 正在单独排查；本请求不授权顺便改其滚动或双缓冲。

# M7 Flex/Table 移植记录

本文从早期工具会话交接中提炼 M7 flex/table 的独有工程结论。它解释当时的移植选择，不是
当前能力或构建状态的权威来源。

## Flex

`layout_flex.c` 最初在 VS2008 中出现大量 `dom_document`、`html_content`、`dom_node` 相关
语法错误。这不是源文件损坏，而是原 NetSurf 构建隐式提供的前置头没有随单文件工程迁移。

修复方法是让 `layout_flex.c` 的 include 集合与已经可编译的 `layout.c` 对齐，包括 DOM、
limits、stdbool、CSS utils 和 protected content 类型。修复后 `positron_core` 可以编译，
设备上的三个色块横排证明正式 flex layout 生效。

当时测试页下部超出屏幕而没有滚动条，是直绘 self-test 没有接宿主滚动，不是 flex 算法本身
的证据。正式 Browse 路径与直绘测试必须分开判断。

对应历史提交：

```text
f724ef2 Phase 4 (M7-flex): port NetSurf layout_flex.c - flex renders on device
```

## Table

勘探发现 `layout_table` 主函数已经在移植的 `layout.c` 中。`table.c` 主要提供列类型和 cell
border 辅助函数；仅把它加入工程并不能让 HTML table 正确布局。

M7-table 分为两步：

1. 使用 `scripts/c89ize.py` 转换 `table.c` 的中间块声明，对齐 include，加入
   `positron_core.vcproj`，并移除对应桩。
2. 在 Positron box builder 中构造 NetSurf 期望的
   `BOX_TABLE > BOX_TABLE_ROW_GROUP > BOX_TABLE_ROW > BOX_TABLE_CELL` 层次，
   设置 cell 的 start column/span、table 的 row/column 数，并分配 column metadata。

当时评估过直接移植 `box_normalise.c`，但它会继续拉入 `box_create` 和
`box_manipulate.c`，且与 Positron 当时的 computed-style 体系不兼容，因此没有扩大迁移面。

另一个根因是早期 UA CSS 把 `table` 和 `tr` 当成 block。修正为正式的
`table`、`table-row-group`、`table-row`、`table-cell` display 后，2×2 table 才能进入
正确布局。

对应历史提交：

```text
db97b95 Phase 4 (M7-table): port table.c + build BOX_TABLE - tables render on device
```

后续 table normalisation、span、border、vertical-align 和 row height 已继续演进，不能从
本文推断当前支持范围。当前限制见
[`../../.agents/KNOWN_LIMITATIONS.md`](../../.agents/KNOWN_LIMITATIONS.md)。

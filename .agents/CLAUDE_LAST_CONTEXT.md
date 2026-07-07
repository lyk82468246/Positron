# Claude Last Context Before Network Failure

来源：用户 2026-07-07 粘贴的 Claude Code 最后几段对话，以及本机 Claude memory。这里保留关键信息，避免后续 agent 只看 git 但不知道为什么这么做。

## M7-flex 的真实过程

用户先遇到真实编译错误，错误形态是大量：

- `dom_document` / `html_content` / `dom_node` 标识符语法错误
- `在 * 前缺少 )`
- 从 `layout_flex.c` 引入的 `private.h` 开始连锁爆炸

Claude 判断这不是文件损坏，而是 `layout_flex.c` include 列表缺少 NetSurf 原构建中隐含的前置头。`layout.c` 能编，是因为它自己 include 了更完整的头：

- `<dom/dom.h>`
- `<limits.h>`
- `<stdbool.h>`
- `css/utils.h`
- `netsurf/content.h`
- `content/content_protected.h`
- 等

修复方式：把 `layout_flex.c` 的 include 区对齐到已经能编的 `layout.c`。之后 `positron_core` 编译通过。

随后为了验证 flex，Claude 修改 TEST 17：

- 内置 HTML 加 `.row { display:flex }`
- 三个色块 `One` / `Two` / `Three`
- 注意不能用 `style="display:flex"`，因为当前 select 流程没有处理 inline style，`css_select_style` 的 `inline_style` 传的是 `NULL`。

设备验证结果：

- 三色块横排，证明 `layout_flex.c` 生效。
- 色块下半部分超屏且无滚动条，不是 flex bug，而是 TEST 17 直绘 self-test 没接 scroll；正式 Browse TEST 13 有滚动。

收尾：

- 提交：`f724ef2 Phase 4 (M7-flex): port NetSurf layout_flex.c - flex renders on device`
- 曾经 push 第一次 TLS handshake 失败，重试成功。属于网络问题，不是代码问题。

## M7-table 的真实过程

用户选择继续 M7-table。

勘探结论：

- `layout_table` 主函数已经在已移植的 `layout.c` 中。
- `table.c` 不是主布局函数，而是辅助：
  - `table_calculate_column_types`
  - `table_used_border_for_cell`
- 当时 `pcore_layout_stubs.c` 里有这两个 table 桩。
- 让 table 真正成表的关键不是 `table.c` 本身，而是 `pcore_box.c` 必须构造 NetSurf 期望的 box 形状。

M7-table A 步：

- `table.c` 跑 `scripts/c89ize.py`，改 9 处中间块声明。
- 手拆 1 处多声明符：`int fixed_width = 0, percent_width = 0;`
- include 对齐 `layout.c`。
- `table.c` 加入 `positron_core.vcproj`。
- 删除 `pcore_layout_stubs.c` 中两个 table 桩。
- 用户编译通过。

M7-table B 步：

关键发现：

- `layout_table` 会 assert `table->columns > 0`，并 `memcpy` `table->col`。
- 因此 `pcore_construct_table` 必须预先：
  - 生成 `BOX_TABLE > BOX_TABLE_ROW_GROUP > BOX_TABLE_ROW > BOX_TABLE_CELL`
  - 设置 cell 的 `start_column`
  - 设置 cell 的 `columns` / `rows`
  - 设置 table 的 `columns` / `rows`
  - 分配 `table->col[]`

曾评估是否移植 `box_normalise.c`，但放弃：

- `box_normalise.c` 依赖 `box_create`
- `box_create` 在 `box_manipulate.c`
- 其签名和当前 computed-style 体系不合，连带移植太大

因此采用手写简化 `pcore_construct_table`，参考 NetSurf 的列计算思路。

另一个前置坑：

- `PCORE_UA_CSS` 原来把 `table, tr` 设成了 `display:block`。
- 这是 table 一直堆叠的直接原因之一。
- 修复为正确 UA display：
  - `table { display: table }`
  - `thead/tbody/tfoot { display: table-row-group }`
  - `tr { display: table-row }`
  - `td/th { display: table-cell }`

TEST 17 同步更新：

- 加 2x2 table：`A1 B1 / A2 B2`
- 把 flex + table 挪到 H1 下方，避免超屏看不到。

预期画面：

1. 深红 H1
2. 三色块横排：flex 生效
3. 2x2 表格：A1/B1 在第一行，A2/B2 在第二行
4. 浅蓝段落垫底，可能超屏，不影响验证

最终仓库状态显示 M7-table 已提交并在设备验证：

- `db97b95 Phase 4 (M7-table): port table.c + build BOX_TABLE - tables render on device`

## Claude 最后中断点

Claude Code 最后不是正常完成新任务后退出，而是连续遇到 API 429 / Service Unavailable / ConnectionRefused。用户因此放弃 Claude Code，转由 Codex 接手。

重要提醒：

- 不要把最后 transcript 里的“请编译/等结果”当成当前未完成状态；git 最新提交已经包含 M7-table 完成。
- 以 `git log` 和当前源码为准。


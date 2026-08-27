# IANA 深链崩溃调查（2026-08-27）

> **已解决事故记录**：本文件保留 TEST 13 从 `example.com` 进入 IANA 页面时的失败实验，
> 并在末尾记录 next650 的根因、正式修复和通过证据。临时诊断代码仍保存在独立实验分支，
> 不应直接合并；当前基线以 Git、源码和已经验收的设备日志为准。

## 现象与边界

TEST 13 的第一跳 `example.com/` 可以完成；点击页面中的 IANA 链接后，目标页面
`https://www.iana.org/help/example-domains` 在样式阶段失败，设备上的 `test_host.exe`
可能显示崩溃/错误报告、停止响应或留下需要人工杀死的进程。失败发生在进入第三跳
`/domains/reserved` 之前，因此不能把 Reserved 页的布局截图或网络结果当作本事故的根因。

本调查只针对 Positron 产品代码调用的 `PCore_StyleDocumentEx2()` 资源收集路径，
不改变 WMDC 连接策略。设备门的前提始终是用户已经在 WMDC GUI 中建立一个唯一的当前
RAPI 连接；脚本不得选择设备、绑定 VMID、自动 cradle/connect、重置或远程强杀进程。

## 已确认的事实

- HEAD 为 `16d8b9ff`（`make nightly layout checks DPI aware`），主线最后一个已知页面能力
  批次是 next649；本事故没有形成新的通过基线。
- 设备为 WM6/ARMV4I，测试使用正式 `scripts\device_gate.bat`，Debug staging，`TEST 13`
  定向选择。每次运行都重新编译并部署同一套 DLL/EXE/INI。
- 运行时抓取的 IANA HTML 临时副本为 6,639 字节；准确外链 CSS 临时副本为 88,658 字节。
  两者均在 `tmp/`，不跟踪、不属于产品资源。
- 正常第一跳会记录 `parse-done`、`style-call-return rc=0`、`layout-done` 和成功摘要。
  IANA 第二跳稳定记录到 `style-begin`，随后出现外链 CSS 请求；失败日志没有完成第二跳，
  因而自动门报告 `completed=1/3`、`test13_route_ok=False`。
- r36/r37 的离线 CSS 读入和解析阶段均能完成；r38 将同一设备文件换成只有两条简单规则的
  CSS，结果仍失败。因此不能把本次事故归因于网络 TLS，也不能只归因于 IANA CSS 的现代语法。
- r40 完全跳过 `pcore_collect_resources()` 后，TEST 13 三跳自动通过（唯一
  `TESTBENCH PASS`）。这只把可疑范围缩小到资源收集/其调用前后，不等于证明某一个 DOM
  API 或某一行代码就是根因。
- r39 跳过外部样式表挂载但仍执行资源读取和 DOM 遍历，仍失败；r44 让该 CSS 请求立即
  返回失败并继续走收集路径，仍失败。这两次不能证明 CSS parser、缓存或回调所有权均无缺陷，
  但说明“仅把样式表 append 到选择上下文”不是唯一必要条件。
- r47 将 `rel` 判断临时回退为旧的单值 `rel="stylesheet"` 比较，仍失败；不能把
  `relList` token 化单独定为根因。
- r45 的低频计数在资源收集约第 96 次递归访问后没有再写出完成标记，门禁最终超时；
  计数只用于定位，不能把“第 96 个节点”当作稳定的 DOM 序号或根因。r48 的 `disabled`
  探测回退在用户中断后没有完整 gate 结果，属于不可判定实验。

## 实验时间线

下表中的目录均在本机 `tmp/device-runs/`，可用同目录的 `test_host.log`、
`device-gate-result.txt` 和（若已拉回）诊断 trace 复核。`FAIL` 表示 gate 观察到失败；
`TIMEOUT` 表示有界门未看到进程完成；两者都不是产品通过证据。

| 运行 | 临时变量 | 结果与结论 |
|---|---|---|
| r31 `20260827-211837-next-crash-direct-probe-r31` | 在正常网络路径加入 style 返回探针 | IANA 停在 `style-begin`/资源阶段；门超时，用户手动终止。网络与样式尚未分离。 |
| r32 `20260827-213440-next-crash-phase-r32` | 增加 Core style 各阶段标记 | 第一页 style 完成；IANA 后续 style 调用在资源收集附近停止；用户终止。旧 trace 还可能含前一轮内容，不能单独作为完整调用栈。 |
| r33–r35 `...collect-r33/r34/r35` | 增加 `collect_*` 边界标记，仍走网络 | 网络请求失败/未完成，日志停在 `resource-miss-queued`；不能据此诊断 DOM。 |
| r36 `...offline-css-r36` | 精确 IANA CSS 从 `\\Storage Card\\Positron-crash-trace` 离线读入 | `resource-local-hit`、CSS 获取/解析标记均出现；随后在完整页面资源收集附近失败。排除了 TLS 作为唯一变量。 |
| r37 `...dom-walk-r37` | 增加逐节点名称和 child/sibling 标记 | 日志接近正文最后一个链接的文本子节点时停止；逐节点 DOM 诊断可能扰动 WM6，不能直接采信。 |
| r38 `...mincss-r38` | 外链 CSS 替换为 `body { margin: 8px; } a { color: #0645ad; }` | 结果与 r37 相同；CSS 复杂度不是充分解释。 |
| r39 `...noattach-r39` | 保留 fetch/cache，临时不调用 `pcore_add_author_css()` | 仍失败；外链表挂载不是唯一触发条件。 |
| r40 `...nocollect-r40` | 完全跳过 `pcore_collect_resources()` | TEST 13 三跳 PASS；这是本批最强定位证据，但页面没有验证真实外链样式。 |
| r41 `...element-only-r41` | 递归入口只处理元素节点 | 仍失败；当时仍有逐节点诊断，结论需谨慎。 |
| r42 `...coarse-trace-r42` | 改为粗粒度阶段/节点标记，恢复收集路径 | 仍失败；没有形成可用调用栈。 |
| r43 `...no-node-trace-r43` | 禁用逐节点 DOM 名称访问 | 仍失败；说明单纯 `get_node_name()` 探针不是充分解释。 |
| r44 `...resource-skip-r44` | 精确 CSS 请求立即返回失败，不入队、不交付正文 | 仍失败并产生 TEST 13 FAIL；回调失败路径也不能安全通过。 |
| r45 `...collect-count-r45` | 每 8 次递归写一个计数，CSS 请求仍跳过 | 约第 96 次访问后不再返回，门超时；门禁没有远程杀进程，用户需人工清理。 |
| r46 `...elements-no-probe-r46` | 元素过滤 + 无节点名称探针 | 仍失败；元素过滤没有独立解决问题。 |
| r47 `...old-rel-r47` | `rel` 回退旧的大小写不敏感单值比较 | 仍失败；暂不能归因于 rel token 化。 |
| r48 `...no-disabled-r48` | 去掉 `disabled` 属性探测 | 用户中断运行，目录只有部分日志且无完整 gate 结果；不可判定，不能合并。 |

## 诊断代码与临时文件

当前实验工作区（稍后会在新分支原样保存）包含以下**未验证**改动：

- `positron_core/pcore_select.c`：设备文件阶段 trace、逐节点/计数探针、元素过滤、
  rel/disabled A/B 和若干临时隔离分支；trace 文件名当前为
  `\\Storage Card\\Positron-crash-trace\\pcore-select-trace-r48.log`。
- `test_host/main.c`：精确 IANA CSS 的离线读取/短路回调，以及 style 返回探针。
  这些都是测试宿主/诊断代码，不是公共 API 修复。
- `tmp/iana_website.css` 当前曾被替换为最小 CSS；`tmp/iana_example_domains.html`、
  `tmp/rapi_push.ps1`、`tmp/rapi_pull.ps1` 和所有 `tmp/device-runs/` 内容都只属于本地
  取证，不应加入 Git 或 nightly 包。

提交前必须删除所有上述探针和离线 hook，除非新 agent 用独立、可复现的自动测试证明某个
真实修复；不得把 `r40` 的“跳过资源收集”作为默认路径。

## 给后续调查的安全顺序

1. 先在干净主线源码上复核 `PCore_StyleDocumentEx2()` 的资源收集前后生命周期，确认
   `pcore_stylesheet_cache_get()`、fetch/free 所有权、`css_select_ctx` sheet 生命周期和
   document user-data 析构；不能只凭当前带探针的分支判断。
2. 把 IANA HTML/CSS 固定成离线、最小可重复 fixture，分别增加 `link`、嵌套元素、表格/footer
   和脚本文本，自动断言每个 fixture 的 style 返回；每次只改变一个变量。
3. 若需要设备 trace，使用一次打开的有界日志句柄或阶段计数，避免每个节点调用 DOM 名称
   API、反复打开文件或在资源回调中做不可控 I/O。trace 必须有唯一 run id，且不能改变
   资源所有权和 DOM 引用计数。
4. 每个候选必须通过 `python scripts\\test_c89ize.py`、正式 ARMV4I Debug 构建和
   `scripts\\device_gate.bat -TestSelection 13`；崩溃、卡死、内容不可达立即人工复核，
   不能累计到下一批。
5. 修复通过后再移除诊断代码，跑相关自动回归（至少 TEST 13、外链 CSS/cache、布局/旋转
   相关门），最后才更新 `.agents/HANDOFF.md`、`.agents/KNOWN_LIMITATIONS.md` 和路线图。

## 解决附录（next650）

后续工作从 `origin/main` 的干净代码建立独立修复分支，未合入诊断快照。基线门
`tmp/device-runs/20260827-225240-iana-clean-r1/` 再次只完成 TEST13 的 1/3 跳，证明去掉探针后
故障仍存在。源码审查随后确认：递归的 `pcore_collect_resources()` 在每个 DFS 帧中声明
1024 字节 stylesheet reference 和 2048 字节 resolved URL 自动数组；IANA 的深层 DOM 因而在
WM6 较小 UI 线程栈上累计约 3 KiB/层，符合 CSS 内容、挂载与请求路径 A/B 均不能解除故障，
而完全跳过遍历可以通过的既有证据。

正式修复在 `PCore_StyleDocumentEx2()` 的单次样式事务中 heap 分配一套同样有界的 scratch，
递归帧只持有其指针；URL 解析、宿主 fetch、document-owned cache、CSS 解析/挂载、`@import`、
media 和释放路径均保持启用。DOM 名称/属性 intern 与 scratch 分配失败统一 fail closed。修复未
加入 IANA URL、离线 CSS、resource-skip、nocollect 或 minimal-CSS 特例，也未改变公共 C ABI。

验证证据：

- `tmp/device-runs/20260827-230326-iana-stack-scratch-r1/`：TEST1098 通过 1/1；20 层嵌套 DOM
  完成真实外链 CSS fetch/parse/attach/free，第二次样式事务命中 document cache。
- `tmp/device-runs/20260827-230355-iana-stack-scratch-test13-r2/`：TEST13 三跳通过，IANA
  `help/example-domains` 与 `domains/reserved` 均完成，资源 2/2/0，零 ERROR/FAIL。
- `tmp/device-runs/20260827-230531-iana-stack-scratch-regression-r3/`：资源缓存、CSS、导航、
  布局/旋转相关回归通过 21/21，唯一 `TESTBENCH PASS`，`test13_route_ok=True`。
- 用户在 320x320 WM6 设备上按 TEST13 路径人工复核该候选，未见显著崩溃、卡死或布局异常。
- `python scripts/test_c89ize.py`、正式 Debug/Release ARMV4I 构建和仓库/文档审计通过。

因此本事故已由真实资源处理路径中的递归栈占用修复。r40 仍只是一项隔离证据，不能恢复或
合并；r45 的约第 96 次访问也仍不是稳定 DOM 序号。资源遍历仍是递归实现，极端 DOM 深度尚无
独立硬上限，继续作为一般有界资源限制保留。

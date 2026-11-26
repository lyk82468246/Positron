# Positron 性能改进路线图

本文件规划 Positron 在 Windows Mobile 6 / Windows CE 5.2 ARMV4I 上的分阶段性能优化路线、所有权分工、预算护栏与验收门槛。本计划记录尚未实施的阶段设计，不代表对应优化已完成；当前设备基线与最新证据见 [HANDOFF](../.agents/HANDOFF.md)，动态候选队列见 [ROADMAP](../.agents/ROADMAP.md)，全仓源码热点取证见 [全仓性能堵点只读审计](history/PERFORMANCE_BOTTLENECK_AUDIT.md)，既有脚本阻塞实测见 [脚本同步阻塞审查](history/SCRIPT_BLOCKING_REVIEW.md)。

## 目标与硬约束

在保持稳定 C ABI、公共 DLL 所有权、固定资源预算与 fail-closed 语义的前提下，按投入产出比逐步消除 WM6 ARMV4I 上用户最敏感的四类瓶颈：**网页加载耗时、表单与脚本交互卡顿、渲染与滚动掉帧、纯资源应用启动与读写延迟**，同时降低空闲唤醒功耗与 32 MiB 进程地址槽内存碎片风险。

所有阶段必须遵守以下硬约束：

1. **先测后改、Release 口径优先**：每个优化纵切开工前必须先取得对应场景的 Release 基线（或剥离热路径 `OutputDebugStringW`/`WriteFile` 干扰的定量汇总），禁止用 `_DEBUG` 逐帧日志耗时替代真实性能归因。
2. **产品所有权不回退**：可复用的渲染、排版、样式、DOM/Selector、脚本、网络、数据库、归档、媒体和 Runtime 优化必须落在对应公共 DLL；`positron.exe` 只优化宿主调度、绘制缓冲、定时器、原生控件同步和平台适配；`test_host.exe` 仅承担夹具与断言。
3. **缓存必须有界可回收**：任何新增缓存（光栅 DIB、字体/画刷、CSS/DOM 索引、可信字节码、DNS/连接池、SQL 语句）都必须有固定条目或字节上限、明确淘汰规则和页面/session teardown 释放点，并通过重复开关与容量上限回归。
4. **安全与正确性零静默降级**：不以跳过 SHA-256/租约核验、破坏 SQLite 既定恢复边界、放宽视觉/功能断言、取消必要动画或扩大无界预算来换取耗时数字。
5. **串行小纵切交付**：每个子纵切独立提交，均须通过 `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、正式串行 Debug/Release ARMV4I 构建以及相称的定向设备门（保留唯一 `TESTBENCH PASS`、零 `ERROR/FAIL`、模块引用 `0/0` 与零新增 dump）。

## 分阶段路线总览

```text
Phase 0  离线量化基线与低干扰测量口径
   ↓
Phase 1  EXE 页面提交、表单输入、触摸滚动与定时器去冗余     （零 ABI 变更，交互/提交立竿见影）
   ↓
Phase 2  Image/Core 光栅缓存、GDI 句柄复用与文本测量优化     （零 ABI 变更，绘制/滚动/首排提速）
   ↓
Phase 3  Browser Selector/DOM 桥接瘦身与 Core 索引化        （优先零 ABI 变更，脚本页大幅提速）
   ↓
Phase 4  Script/Browser 启动编译热点测量与可信字节码复用     （需独立预算与内存峰值审查）
   ↓
Phase 5  Runtime/PXI/DB/Archive 冗余 I/O、跨线程与查询精简  （保留安全核验与恢复合同）
   ↓
Phase 6  HTTP/TLS 连接复用与压缩、并发子资源抓取、Media 音频 （协议/并发升级，需完整网络与故障门）
```

| 阶段 | 核心收益场景 | 主要 Owner | 是否改动公共 ABI | 核心进入/退出门槛 |
| --- | --- | --- | :---: | --- |
| **Phase 0** | 全链路可比基线 | `positron_app` / `test_host` | 否 | 5 类离线/本地场景可重复输出阶段耗时与计数，无 Debug 日志干扰 |
| **Phase 1** | 表单打字、提交与滚动 | `positron_app` | 否 | 消除加载 4–5 遍重排与按键同步全页重排；滚动/表单/多标签设备门通过 |
| **Phase 2** | 画面绘制、SVG 与排版 | `positron_image` / `positron_core` | 否 | SVG 有界光栅缓存、去 `/255` 软除法、`plot_text` 用字体缓存；像素门通过 |
| **Phase 3** | 脚本页面与 DOM 查询 | `positron_core` / `positron_browser` / `positron_script` | 尽量不变（必要时 `Ex`） | 消除 `liveTagCollection9` 万级跨桥与 Core `O(N)` 无索引遍历；脚本全套回归通过 |
| **Phase 4** | 新页脚本初始化耗时 | `positron_script` / `positron_browser` | 内部优先 | 先测字节码体积/峰值/收益；双配置通过并复核 Debug TEST203 默认预算 |
| **Phase 5** | 纯资源应用启动/安装/SQL | `positron_runtime` / `positron_app` / `positron_db` / `positron_archive` | 内部优先 | 消除 2–3 轮重复核验、15× `GetProcAddress` 与 2 MiB 临时分配；全套安装/Notes 门通过 |
| **Phase 6** | 真实网络多资源页与媒体 | `positron_http` / `positron_tls` / `positron_app` / `positron_media` | 按需 `Ex` | 有限 Keep-Alive/gzip/2–4 并发 worker 与 Media 定点音频/双缓冲 WaveOut；双配置门通过 |

---

## 各阶段详细规划

### Phase 0：可重复量化基线与低干扰测量口径

- **目标**：在动手改热路径前建立前后可对比的数字基线，避免凭体感或 Debug 日志噪音做优化决策。
- **范围与交付**：
  1. 固化 5 组可重复基准夹具：图文+平铺 SVG 静态页、多控件表单输入/切焦页、原版 jQuery/Bootstrap 脚本与 Selector 页、本地 loopback 多子资源 HTTPS 页、隔离 Notes 纯资源应用冷启动+翻页查询。
  2. 统一采集口径：阶段汇总耗时（首排、样式、绘制、单帧滚动、脚本 bootstrap/求值、应用启动/资源读取）、关键操作计数（`StyleDocument`/`LayoutDocument` 次数、JSON 跨桥次数、SVG 光栅次数、SQLite 提交次数）以及进程/脚本峰值堆。
  3. 测量默认在 Release 或关闭热路径 `AppDebug_Log` 的受控模式执行；保留既有 Debug 诊断开关用于功能排错，不混淆性能对比。
- **退出条件**：任意后续阶段开工前具备对应场景的改前基线，优化完成后输出同口径前后对照。

### Phase 1：应用宿主调度、提交与交互去冗余（`positron_app`）

不触碰任何公共 DLL ABI，优先清除宿主层的明显重复工作与高频卡顿。

1. **1A. 加载与提交去冗余重排**：
   - 修复 `APP_NAV_COMMIT_IMAGES` 发现 pending 图片后退回 `APP_NAV_COMMIT_STYLE` 导致的整页重复样式计算。
   - 消除 `APP_NAV_COMMIT_LAYOUT` 完成后页面切换时无条件再次 `app_relayout()`（以及内部页 `app_build_page` + `app_load_local_page` 的双遍布局）；仅在视口尺寸因滚动条实际变化时补排一次。
   - 优化脚本分步调度：在单次 UI 消息让步边界内为极短 bootstrap 步骤设置微小时间片预算（例如单 tick 连续推进至达到小阈值再让步），消除 40 次强制 16ms/50ms 定时器空等，同时保留取消与 stale 隔离。
2. **1B. 原生表单输入与焦点切换解耦**：
   - 解除 `EN_CHANGE`、`WM_SETFOCUS`、`WM_KILLFOCUS` 同步触发整页 `app_relayout()` + 两次 `AppControls_Reposition` + 全视口 `InvalidateRect` 的强耦合。
   - 区分“仅值/选区/焦点变化”与“可能改变布局的 DOM/样式变更”；必要更新复用现有 `APP_WM_CONTROLS_REFRESH` 合并路径，并删除 `app_controls_changed` 和 `WM_SIZE` 中的重复第二次 `AppControls_Reposition`。
3. **1C. 触摸滚动批处理与空闲功耗治理**：
   - `AppControls_RepositionForScroll` 复用最近一次布局缓存的控件文档坐标，使用 `BeginDeferWindowPos`/`DeferWindowPos`/`EndDeferWindowPos` 批量移动，并对已经隐藏且仍在屏外的控件跳过重复 `ShowWindow`/`MoveWindow`。
   - 触摸拖动期间合并连续 `WM_MOUSEMOVE` 绘帧并对 `AppScript_NotifyScroll` 做有界帧级合并（保留抬起/结束时的最终精确位置同步）。
   - 优化 `app_controls_select_fingerprint` 减少逐 `<option>` 堆分配，并只重建实际发生变化的 `<select>` 窗口。
   - 将永久常驻的 `APP_SETTINGS_TIMER_ID`（50ms）与 `APP_SCRIPT_TIMER_ID`（250ms）改为有挂起任务时按需启停；地址栏 160ms 动画在跑马灯停顿帧和加载重置停顿帧跳过无变化重绘。
- **验收门**：EXE 历史/滚动像素门、设置/下载/多标签自检门、双配置构建及真机/模拟器输入与拖动跟手性人工复核。

### Phase 2：GDI 绘制、SVG/图像缓存与文本排版热路径（`positron_image` + `positron_core`）

直接降低每帧 GWES 跨进程系统调用和 ARMV4I 软件除法/软件浮点开销，保持公共 ABI 不变。

1. **2A. SVG 有界预乘光栅缓存与去软除法（`positron_image` / `positron_core`）**：
   - 在 `pimage_raster.cpp` 中为 `(svg, raster_w, raster_h)` 增加固定容量上限（如最多 N 项且总像素字节有硬上限）的预乘 BGRA DIBSection 缓存，在 SVG 句柄销毁时确定性释放。
   - 将预乘 Alpha 循环中的每像素 3 次 `/ 255` 软件除法替换为整数移位近似 `((c * a + 128) + ((c * a + 128) >> 8)) >> 8`，并对 `alpha == 0` 和 `alpha == 255` 增加快路径。
   - `pcore_plot_gdi.c` 的平铺绘制 `plot_bitmap` 对重复 tile 复用单次缓存结果，不再每 tile 重跑 `nsvgRasterize`。
   - 评估为 `PIMAGE_BITMAP` 增加按有界尺寸缓存的解码 `HBITMAP`/DIB，避免每帧与每个平铺 tile 重复走 COM `IImage::Draw`。
2. **2B. Core GDI 句柄缓存与裁剪去抖（`positron_core`）**：
   - `plot_text` 改为复用既有 `g_fc` `HFONT` 缓存并缓存 `tmAscent`，不再逐文本片段 `CreateFontIndirectW`/`DeleteObject`；单段文本跳过无用的 `GetTextExtentPoint32W`。
   - `plot_clip` 在单次 `PaintDocument` 事务内追踪当前裁剪矩形与 viewport/window origin，裁剪区未变时直接短路，变化时复用单个 `HRGN` + `SetRectRgn`，替代每次 6+ 次 GWES 调用。
   - 为 `plot_rectangle`/`stroke_line`/`plot_disc`/`plot_polygon` 增加小型固定容量 `HBRUSH`/`HPEN` 缓存，清理虚线绘制内循环的重复除法。
3. **2C. 文本测量与断行热路径（`positron_core`）**：
   - `pcore_font_kind_for_codepoint` 增加 `codepoint < 0x200D` 快速返回，跳过 ASCII/Latin 字符的三次二分查找。
   - `gdi_font_width` 与 `pcore_measure` 对常规短字符串使用固定栈缓冲，仅超长时回退堆分配。
   - `gdi_font_split` 结合可用宽度限制 `GetTextExtentExPointW` 的 `nMaxExtent`，消除长段落折行的 `O(K·L)` 全后缀重测；`g_measure_dc` 追踪当前选中字体，避免连续同字体重复 `SelectObject`。
- **验收门**：Core/Image 像素与尺寸全套回归（TEST19/20/39/42/1311/1315–1319/1340/1345/1346）、高 DPI 与低 DPI 对照、GDI 对象计数不增长验证及双配置设备门。

### Phase 3：Browser DOM/Selector 桥接瘦身与 Core 索引化（`positron_core` + `positron_browser` + `positron_script`）

大幅削减脚本页面的 JSON 跨桥次数、堆碎片与 Core 全树线性遍历。

1. **3A. 消除 `liveTagCollection9` 与 Selector 的数量级往返（`positron_browser`）**：
   - 为 Browser session 维护单调递增的 DOM 结构/属性/ID 变更计数；`liveTagCollection9` 仅在计数变化时重新扫描，彻底消除 `for (i = 0; i < list.length; i++)` 导致的 `2M+2` 遍整树重扫，并让 `document.getElementsByTagName` 避免自触发循环刷新。
   - 恢复 `querySelector('#id')` 的直接查找快路径；每个 query 在遍历 DOM 前只解析一次 selector 结构体，逗号分支命中即短路。
   - 拆分内部子元素遍历与对外 `HTMLCollection` 命名属性装饰，避免内部遍历时 `decorate13` 对全树节点盲目读取 `id`/`name`。
2. **3B. 轻量化跨桥与节点闭包收敛（`positron_browser` + `positron_script`）**：
   - 缓存不可变 `tagName`/`localName`/`nodeName`；折叠 5 层 `document.getElementById` 包装链，缓存命中时不再冗余调 `__pcoreHasElement` 与构造丢弃的 `new PElement(id)`。
   - 修复 `p_browser_script_form_property` 对同一参数做两次 `PJson_Parse`，移除 `p_browser_script_args_object` 冗余 `malloc+memcpy`；`pscript_native_dispatch` 改用 `duk_get_current_magic()` 替代线性 `strcmp`；评估开启 `DUK_USE_JSON_STRINGIFY_FASTPATH` 对体积与速度的影响。
   - 合并空闲态 `PBrowser_ScriptSessionRunTaskCheckpoint`（队列空时 1 次检查返回而非 8 次跨桥）；将 C→JS 的 scroll/lifecycle/history 通知改为直接调用已注册全局函数，替代每次 `PScript_Evaluate` 编译两段源码。
   - 将 Text/Comment/CDATA（~45 闭包/节点）和 `createElement`（~35 闭包/节点）的访问器上移到共享 prototype；同步修正 `pscript_memory_reserve` 在 GC 重试成功后未清零 `memory_limited` 的状态问题。
3. **3C. Core 样式缓存补盲与文档索引（`positron_core`）**：
   - 缓存已解析的 UA CSS 样式表与未变 `<style>` 块；修复外部 `<link>` 缓存按栈地址 `parsed_pw` 误失配的问题，同指针跳过 `memcmp`。
   - 为三遍 CSS 兼容预处理器（media range、`:root` vars、`oklch`/`calc`）增加零分配前置特征扫描，未命中特征时直接跳过。
   - 在 Core 文档中维护有界 `id -> dom_element*` 索引并在 DOM 增删/改 `id` 时同步维护；`pcore_element_by_id` 先判定结构 token 再查索引；`pcore_box_for_node` 优先读取节点已挂载的 `corestring_dom___ns_key_box_node_data`；常驻高频 tag/attribute `dom_string` 避免选择器和构盒循环反复分配释放。
- **验收门**：Browser DOM/Selector/Event/Form/jQuery/Bootstrap 双配置门（TEST1152–1309、1325–1329、1336、1338）及 JSON 往返次数/堆峰值对比。

### Phase 4：脚本启动编译热点与字节码复用（`positron_script` + `positron_browser`）

承接 [ROADMAP](../.agents/ROADMAP.md) 与 [SCRIPT_BLOCKING_REVIEW](history/SCRIPT_BLOCKING_REVIEW.md) 的既定审查门槛：

1. **4A. 字节码产物体积与收益实测**：利用仓库 Duktape 已启用的 `DUK_USE_BYTECODE_DUMP_SUPPORT`，实测 36 段 bootstrap 编译后的总字节数、峰值内存、`duk_load_function` 相比源码编译的耗时降幅，以及与 1.5 MiB / 3 MiB 堆预算的兼容性。
2. **4B. 内部可信字节码缓存**：若实测净收益明确且内存预算可控，在 DLL 内部实现固定预算的编译产物复用（严格限定为同进程/同版本/同引擎配置自产产物，不暴露任意外部 bytecode 输入；每个 session 仍保有完全独立的 globals/closures/callbacks，分配失败安全回退源码编译）。同步评估 `PScript_CollectGarbage` 区分常规回收与显式紧缩，避免热路径无条件双遍 STW 紧急压缩。
3. **4C. 冷启动作者大脚本评估（待独立决策）**：若完成 4B 后首次访问大型作者脚本（如 jQuery）仍是核心体验阻塞，再另行设计“只编译不执行的独立 compiler context”线程与 ABI 合同，不提前与 4B 捆绑。
- **验收门**：TEST1336、TEST1338、TEST203（重点复核 Debug 默认预算）、TEST1327 及真实页面总启动耗时与最大单次调用耗时对照。

### Phase 5：纯资源应用 Runtime / PXI / DB / Archive I/O 减冗余

在严格保留不可变快照、SHA-256 核验、独占租约与原子提交合同的前提下，清除重复计算与无谓跨线程抖动：

1. **5A. 宿主低风险零语义变更去冗余（`positron_app`）**：
   - 在 `app_install_store.c` 进程/store 生命周期内缓存 15 个 DB DLL 函数指针，消除每次调用的 15× `GetProcAddress`。
   - `app_navigation_fetch_resource` 按 Runtime 资源真实长度分配读取缓冲，替代每次无条件 `malloc(2 MiB)`。
   - 在 Runtime worker 上复用单个 `PROV_RSA_AES` CryptoAPI provider 句柄，消除每文件 2 次 `CryptAcquireContext`（先 `PROV_RSA_FULL` 失败再回退）。
   - `revision_read_at` 与文件列表查表改用预缓存 UTF-8 键与有序查找，消除 `O(N²)` 宽字符转换。
2. **5B. 消除安装/启动重复整树扫描与细粒度跨线程 DB 查表（`positron_runtime` + `positron_app`）**：
   - 审查 `revision_capture` 的“双遍整树打开 + 峰值 256 句柄 + `O(N × depth)` 重复路径规范化”和提交后立刻关闭再重跑 2–3 遍全树核验的链路；在不破坏“物理包含性/变更检测/独立已安装核验”合同的前提下，评估单次规范化目录树验证与减少冗余整盘重读。
   - 将 `PRuntime_InstalledReadFile` 每 64 KiB chunk 触发一次跨线程 `rv_refresh()` 查 SQLite 优化为按文件读取边界或与既有租约版本协同，减少无谓线程阻塞和内存四次拷贝。
   - `AppRuntimeServices_Pump` 支持单次 UI tick 在有界预算内交付多条已完成队列结果（避免高吞吐应用被硬限为 1 条/tick）。
3. **5C. Archive 与 PXI 解包优化（`positron_archive` + `positron_app`）**：
   - `pa_admit` 将 `O(N²)` 路径碰撞检查改为排序后相邻比较 `O(N log N)`，并为中央目录与本地头解析引入有界读缓冲，消除 128 项 PXI 打开时的 ~640 次微小读与 ~1280 次 stat。
   - 评估 PXI 解包由逐文件 `FlushFileBuffers` 收敛为关键边界刷盘（保持所有权清单先刷后改名及中断后安全核验语义）。
4. **5D. DB 查询与持久化调优（`positron_db` + `positron_app`）**：
   - 在 `app_settings_store` / `app_install_store` 缓存高频 SQL 的 prepared statement；合并启动时 10 次 `sqlite_master` 探测；为 `PDb_OpenUtf8Ex` 设置显式有界 `cache_size`（避免默认 8 MiB 占用）；优化只读 `PDb_ExecuteRestricted` 事务包装。
   - 仅在单独验证 journal 恢复与异常退出行为后，再审慎评估高频访问历史/下载表的 `journal_mode`（如 `TRUNCATE`/`PERSIST` 替代 `DELETE`）与批量裁剪策略；**不重开已挂起的文件配额 FULL / 物理断电门**。
- **验收门**：Runtime/DB/Archive 双配置定向门（TEST1321、1363–1386）与 `app_pxi_gate`、`app_pxi_update_gate`、`app_runtime_notes_gate` 冷库核对全部通过。

### Phase 6：网络协议/并发升级与 Media 热路径

涉及网络传输行为或媒体缓冲结构变化，放在前面零风险收益落袋之后独立推进：

1. **6A. HTTP 缓冲与有界压缩（`positron_http`）**：
   - 修复 `read_until_headers` 的 `O(H²)` 扫描，已知 `Content-Length` 时预分配目标缓冲，增大正文读取块，消除 chunked 解码高频 `memmove`。
   - 复用仓库已固定的 miniz DEFLATE 在 `positron_http` 内支持有界 `Accept-Encoding: gzip`（固定压缩流与解压后 1 MiB/流上限，防止高压缩比内存炸弹），保持 `PHttpResponse` 布局兼容。
2. **6B. TLS 会话复用、DNS 缓存、Keep-Alive 连接池与并行子资源抓取（`positron_tls` + `positron_http` + `positron_app`）**：
   - 在 `positron_tls` 启用有界 TLS session resumption（ticket/cache）与短 TTL 有界 DNS 缓存；流式路径复用有界 resolver 线程池替代每请求新建线程 + `LoadLibrary`。
   - 在 `positron_http` 设计同源有界 HTTP/1.1 Keep-Alive 连接池（明确最大空闲连接数、空闲过期、半开连接重试安全边界、取消与关闭清理）。
   - 在 `positron_app` 将子资源抓取从单 worker 扩展为固定 2–4 个并发 worker，同时保留 Browser 的 generation、required/optional gate、取消与旧页保留语义。
3. **6C. Media 音频热循环与缓冲优化（`positron_media`）**：
   - 将 `pm_ff_convert_audio` 的 8 路格式分支移出样本内循环，优化 ARMV4I 无 FPU 环境下的浮点/定点转换，并用 `memcpy` 替代逐样本字节拼装。
   - 为 `pmedia_waveout` 实现双缓冲（2 个 `WAVEHDR` 轮转）消除块间播放停顿。
   - 评估可 seek 源改用流式有界 AVIO 缓冲，避免 16 MiB 整文件倍增 `realloc` 造成 32 MiB 槽位碎片化，并消除 `pm_probe` + `pm_open` 整文件双读。
- **验收门**：HTTP/TLS loopback 与 verified HTTPS 门（TEST3/4/1064/1065/1342/1343/1350/1351）、多标签取消/旧页保留门、Media 全套双配置设备门（TEST1312、1331–1362）。

## 明确非目标与风险护栏

- 不以性能优化为名将公共 DLL 语义移入 `positron.exe` 或 `test_host.exe`。
- 不将 live `PCoreDocument`、`PBrowserScriptSession` 或 `PDb` handle 跨线程共享。
- 不引入无界内存缓存、无限连接池、无限线程池或放宽现有 heap/资源硬上限。
- 不为了跑分删除或放宽已有回归断言、禁用安全核验或跳过设备门。
- 不把桌面/模拟器单次耗时外推为全部 WM6 真机保证；视觉、触摸、输入法与真机续航体验仍走累计人工矩阵。

# Positron 性能堵点只读审计（WM6 / CE 5.2 ARMV4I）

本文件保存基于提交 `6a14f814` 的全仓只读源码审计与既有设备实测汇总，作为后续性能取证与纵切选型的基线参考，**不是当前路线图或公共能力合同**。当前开发状态与稳定边界见 [ARCHITECTURE](../ARCHITECTURE.md)、[CAPABILITIES](../CAPABILITIES.md)、[HANDOFF](../../.agents/HANDOFF.md) 和 [ROADMAP](../../.agents/ROADMAP.md)。

审计针对 Windows Mobile 6 / Windows CE 5.2 ARMV4I 平台特征（低主频单核 ARMv4I、**无硬件 FPU / 无硬件整数除法**、32 MiB 进程虚拟地址槽、高延迟 GWES 跨进程 GDI 调用、慢速 Flash/SD I/O），严格区分**已有设备实测证实的堵点**与**源码静态审计发现的潜在热路径**。遵循项目纪律：任何优化在修改产品代码前，仍须补充相称的计时/峰值内存测量，且不得削弱公共 DLL 所有权边界、fail-closed 语义、安全核验合同或固定预算约束。

---

## 总览：核心堵点与优先级（P0–P2）

| 优先级 | 影响场景 | 核心堵点摘要 | 语义 Owner | 证据类型 |
| :---: | --- | --- | --- | --- |
| **P0** | 网页加载耗时 | 无 TCP/TLS 复用、无 Keep-Alive/Session 复用、无 DNS 缓存、强制 `Accept-Encoding: identity`，且单 worker 严格串行拉取所有子资源 | `positron_http` / `positron_tls` / `positron_app` | 源码实证 |
| **P0** | 脚本初始化与执行 | 每次新 session 重新编译 36 段共 ~477 KiB bootstrap JS（无字节码复用）；`liveTagCollection9` 与纯 JS Selector 导致数千至数万次 JSON 跨桥往返 + Core `O(N)` 无索引 ID 遍历 | `positron_browser` / `positron_script` / `positron_core` | **设备实测** + 源码实证 |
| **P0** | 渲染绘制与滚动帧率 | SVG 零光栅缓存（每帧、每平铺 tile 重新软光栅化 + 每像素 3 次软除法 `/255`）；`plot_text` 绕过 `HFONT` 缓存每段文字创建/销毁字体；`plot_clip`/刷子笔高频 GWES 句柄抖动 | `positron_image` / `positron_core` | 源码实证 |
| **P0** | 网页提交与表单输入 | 单次页面加载触发 4–5 次全量 `PCore_StyleDocument` 和 2–3 次全量 `PCore_LayoutDocument`；原生表单**每次按键/焦点变化**同步触发整页 `app_relayout()` + 2 次控件重排 | `positron_app` / `positron_core` | 源码实证 |
| **P0** | 纯资源应用启动/读写 | 已安装应用启动/安装存在 2–3 轮重复全树双枚举 + 4 MiB 全量拷贝 + 每文件 2 次 CryptoAPI 上下文创建；每次读取 64 KiB 资源块触发跨线程查 DB + 15 次 `GetProcAddress` + 2 MiB 临时分配 | `positron_runtime` / `positron_app` | 源码实证 |
| **P1** | 布局排版与内存压力 | 文本测量无 ASCII 快速路径（每 ASCII 字符 3 次二分查找）；`gdi_font_split` 对整段剩余文本 `O(K·L)` 重测；CSS UA/内联样式每次重解析且 3 遍预处理器无条件 `malloc`；Duktape 节点闭包膨胀与双遍紧急 GC | `positron_core` / `positron_browser` / `positron_script` | **设备实测** + 源码实证 |
| **P1** | 触摸拖动与空闲功耗 | 触摸拖动每次 `WM_MOUSEMOVE` 同步 `UpdateWindow` + 未批处理 `MoveWindow(..., TRUE)` + 同步 JS `scroll` 派发；50ms/160ms/250ms 定时器空闲常驻唤醒 | `positron_app` | 源码实证 |
| **P1** | 数据库与 PXI 写入 | SQLite `journal_mode=DELETE` + `synchronous=FULL`（每笔访问记录/下载状态均整套删建 journal 刷盘）；零语句缓存；PXI 解包每文件 `FlushFileBuffers` + Archive `O(N²)` 路径碰撞检查 | `positron_db` / `positron_archive` / `positron_app` | 源码实证 |
| **P2** | 媒体解码与播放 | 16 MiB 整文件倍增 `realloc` 缓冲且 `pm_probe` + `pm_open` 读两遍；音频内循环逐样本软浮点转换 + 8 路分支；单缓冲 WaveOut 必然导致音频断续 | `positron_media` | 源码实证 |

---

## 一、已有设备实测证实的堵点

根据 [SCRIPT_BLOCKING_REVIEW.md](SCRIPT_BLOCKING_REVIEW.md) 与 [WINWORLD_SCRIPT_INITIALIZATION.md](WINWORLD_SCRIPT_INITIALIZATION.md) 的 ARMV4I DeviceEmulator 实测结果：

1. **脚本编译/解析主导初始化与库加载耗时**：
   - 完整 Browser bootstrap：Debug **6820 ms**（其中编译占 **6497 ms / 95%**），Release **2392 ms**（编译占 **2216 ms / 93%**）。
   - 分步协议（`BootstrapBegin/Step`）将单次最大阻塞从 Release 2.6s 降到 **339 ms**（Debug 仍为 **1262 ms**），但总编译工作量未减少。
   - 原版 jQuery 3.5.1（89 KiB）单次求值：Debug **2468–3130 ms**（编译 1714 ms），Release **904–1003 ms**（编译 601 ms）；Bootstrap 4.6.2（62 KiB）：Debug **962–1370 ms**，Release **387–408 ms**。
2. **高堆占比触发频繁 GC 抖动**：
   - jQuery + Popper + Bootstrap 在 3 MiB 应用堆预算下峰值达 **2,642,875 / 3,145,728 B（84%）**，紧贴上限运行。

---

## 二、网络与传输（`positron_http` / `positron_tls` / `positron_app`）

### 2.1 无连接复用、无会话恢复、无 DNS 缓存 + 严格串行子资源加载（P0）
- **强制短连接**：[positron_http.c](../../positron_http/positron_http.c#L486) 与 [p_http_stream.c](../../positron_http/p_http_stream.c#L440) 均硬编码 `Connection: close`。每次请求及每次 3xx 重定向都新建 socket 并执行完整 TLS 1.2 握手（[positron_http.c](../../positron_http/positron_http.c#L1782-L1785)）。
- **无 TLS 会话复用**：`positron_tls/mbedtls_config.h` 未启用 `MBEDTLS_SSL_SESSION_TICKETS` 或 `MBEDTLS_SSL_CACHE_C`；[positron_tls.c](../../positron_tls/positron_tls.c#L1343-L1466) 每次连接都重新 `mbedtls_ctr_drbg_seed()`（`CryptGenRandom`）并完成完整非对称 ECDHE/RSA 握手与 X.509 链验证。（注：121 个根证书在 `PTls_Init` 中一次性解析缓存于 `g_cacert`，并非每连接重解析，但链验证仍需线性遍历。）
- **无 DNS 缓存，流式路径每请求起线程**：[ptls_tcp_connect](../../positron_tls/positron_tls.c#L481) 每次阻塞调用 `gethostbyname`；流式/预检路径的 [ptls_control_resolve](../../positron_tls/positron_tls.c#L2204-L2263) 对每个非 IPv4 域名都会新建 OS 线程并执行 `LoadLibrary` + `WSAStartup` + `gethostbyname` + `WSACleanup` + `FreeLibraryAndExitThread`。明文 `wininet_fetch` 同样每次新建并销毁 `InternetOpenW/ConnectW/HttpOpenRequestW`（[positron_http.c](../../positron_http/positron_http.c#L1470-L1502)）。
- **单 worker 瀑布式串行**：[app_navigation_worker](../../positron_app/main.c#L5631-L5667) 仅起 1 个线程逐个拉取子资源，且 [app_navigation_advance](../../positron_app/main.c#L5833-L6241) 把“主文档 → JS 串行下载+执行 → CSS 串行下载 → `@import` 串行下载 → 图片串行下载”完全串联。一个含 15 个 HTTPS 资源的页面需串行支付 15 次 DNS + TCP + 完整 TLS 1.2 握手（WM6 ARM 上单次握手常达 300–1000ms+，仅握手即可达 5–15 秒）。

### 2.2 禁用 HTTP 压缩与小缓冲重复扫描（P1）
- **无压缩**：硬编码 `Accept-Encoding: identity`（[positron_http.c](../../positron_http/positron_http.c#L488)），HTML/CSS/JS 传输体积放大 3–5 倍（仓库已固定静态编入 miniz DEFLATE，具备未来在 HTTP 层接入 gzip 解压的基础条件）。
- **缓冲与扫描效率**：[read_until_headers](../../positron_http/positron_http.c#L643-L669) 每次读 1024 字节后从下标 0 全量重扫 `\r\n\r\n`（`O(H²)`）；`bytebuf` 从 8 KiB 倍增 `HeapReAlloc` 至 1 MiB，即使已知 `Content-Length`（L1879）也未预分配；正文读写使用 2048 字节栈缓冲；`decode_chunked` 每次追加前 `memmove` 压缩且同时持有 `raw` 与 `out_body` 双份 1 MiB 内存（[positron_http.c](../../positron_http/positron_http.c#L351-L381)）。

---

## 三、Browser 与 Script（`positron_browser` / `positron_script`）

### 3.1 Bootstrap 无字节码复用（P0，已有设备实测）
- [P_BROWSER_BOOTSTRAP_STAGES](../../positron_browser/positron_browser.c#L8568-L8606) 共 37 个阶段（36 段嵌入式 JS 源码合计 **488,454 B ≈ 477 KiB**，最大 4 段均逼近 VS2008 64 KiB 字面量上限 + 1 次收尾 GC）。每次新建 `ScriptSession` 都从 UTF-8 源码重新词法/语法分析与编译，并执行 576 次 `Object.defineProperty`。
- 仓库中的 Duktape 实际已开启 `DUK_USE_BYTECODE_DUMP_SUPPORT`（[duk_config.h](../../netsurf-all-3.11/netsurf/content/handlers/javascript/duktape/duk_config.h#L2913)），但 `positron_script` 尚未提供内部可信字节码缓存复用接口（符合 [ROADMAP.md](../../.agents/ROADMAP.md#L259-L267) 既定方向）。

### 3.2 纯 JS Selector 与 `liveTagCollection9` 引发爆炸式跨桥 JSON 往返（P0）
- **`liveTagCollection9` 灾难性重扫**：[positron_browser.c](../../positron_browser/positron_browser.c#L6963-L7021) 为 `getElementsByTagName()` 创建 64 个下标 getter 闭包，且**每次读取 `.length`、`[i]`、`.item(i)` 或 `.namedItem()` 都重新触发整树 DFS `__refresh9()`**（最多遍历 256 节点并逐节点跨桥查 `localName`，再对每个命中项跨桥查 `id` 与 `name` 并做 `O(M²)` 去重和重新 `defineProperty`）。更严重的是，`document.getElementsByTagName`（[L5406-L5410](../../positron_browser/positron_browser.c#L5406-L5410)）内部自己就写了 `for (i = 0; i < list.length; i++)` 循环——在 64 项、256 节点规模下，**单次调用可产生约 50,000 次 JSON 跨桥往返**。`doc.forms/images/scripts/links` 全部经由该路径。
- **Selector 丢弃 `#id` 快路径 + 每节点重解析**：`PART9`（[L6890-L7043](../../positron_browser/positron_browser.c#L6890-L7043)）覆盖了 `PART2` 原有的 `#id` 快路径，导致 `querySelector('#btn')` 也要遍历整棵 DOM 树；对树中**每个节点**都会重跑字符级 `selectorParse9`（字符串逐字符拼接与数组分配），逗号分支匹配成功后不短路（L6946）。
- **隐藏的 `4N` 遍历税**：`children9()` 调用 `decorate13`（[L8439-L8443](../../positron_browser/positron_browser.c#L8439-L8443)），即使只是内部遍历子元素，也会迫不及待为每个子节点跨桥读取 `id` 与 `name`（每个父节点消耗 `1 + 3K` 次往返）。

### 3.3 每次 DOM 属性访问的高额 JSON 封送成本与重复包装（P0–P1）
- **单次原生桥调用链**：[pscript_native_dispatch](../../positron_script/positron_script.c#L202-L331) 线性 `strcmp` 查找函数名 → 新建 Duktape Array → `duk_json_encode`（且 `DUK_USE_JSON_STRINGIFY_FASTPATH` 被 `#undef`）→ C 侧 [p_browser_script_args_object](../../positron_browser/positron_browser.c#L9443-L9476) 冗余 `malloc+memcpy` → `cJSON_Parse` 建树 → 线性键查找 → 宿主回调**两次**（size-probe + copy）→ JSON 转义 → `duk_json_decode`。每次属性读写触发约 6–12 次 WinCE 堆分配。
- **冗余叠加**：
  - `tagName`/`localName`/`nodeName`（不可变字段）每次读取都跨桥查 `relation 7`（[L6356-L6359](../../positron_browser/positron_browser.c#L6356-L6359)）。
  - `getBoundingClientRect()` 每次触发 `5–65` 次 JSON 往返（[L2146-L2168](../../positron_browser/positron_browser.c#L2146-L2168)）。
  - `el.attributes.length` 拉取全部属性名，遍历属性退化为 `O(A²)` 往返（[L7109-L7111](../../positron_browser/positron_browser.c#L7109-L7111)）。
  - `document.getElementById(id)` 叠了 **5 层包装闭包**，即使命中缓存仍每次跨桥调 `__pcoreHasElement` 并丢弃一个新 `new PElement(id)`（[L5385](../../positron_browser/positron_browser.c#L5385)）。
  - [p_browser_script_form_property](../../positron_browser/positron_browser.c#L11688-L11811) 对同一 `args_json` 做**两次**完整 `PJson_Parse`。
  - C→JS 通知（scroll / lifecycle / history）通过 `PScript_Evaluate` 每次临时编译 2 段 JS（调用 + `delete`）（[L18804-L18856](../../positron_browser/positron_browser.c#L18804-L18856)）。
  - 空闲态调用 [PBrowser_ScriptSessionRunTaskCheckpoint](../../positron_browser/positron_browser.c#L12995-L13060) 仍无条件执行 8 次 `PScript_CallGlobalJson`（24 次 `duk_pcall` + 16 次 JSON 编解码）。

### 3.4 节点闭包膨胀与紧凑堆 GC 抖动（P1）
- `child(o, i)` 为每个 Text/Comment/CDATA 包装器实例单独定义约 **45 个闭包**（[L7319-L7442](../../positron_browser/positron_browser.c#L7319-L7442)）；`createElement()` 为每个新元素实例定义约 **35 个闭包**（[L3356-L3389](../../positron_browser/positron_browser.c#L3356-L3389)），未共享 prototype。
- [PScript_CollectGarbage](../../positron_script/positron_script.c#L877-L908) 连续执行两遍 STW 标记清除（常规 + `DUK_GC_COMPACT` 紧急压缩）；接近 3 MiB 上限时 Duktape 每次分配失败可触发多达 **10 轮**全堆紧急压缩重试。
- *(顺带发现的潜在状态缺陷)*：[pscript_memory_reserve](../../positron_script/positron_script.c#L510-L525) 置位 `ctx->memory_limited = 1` 后，若 Duktape 经 GC 重试分配成功也未清零，后续同类求值若抛出普通 JS 错误会被误判为 `PSCRIPT_ERROR_MEMORY_LIMIT`。

---

## 四、Core 与 Image 渲染引擎（`positron_core` / `positron_image`）

### 4.1 SVG 零光栅缓存 + 每像素 3 次软件除法 + 平铺重复光栅化（P0）
- **位置**：[pimage_raster.cpp](../../positron_image/pimage_raster.cpp#L512-L591)（`pimage_draw_path_batch`）、[pcore_plot_gdi.c](../../positron_core/pcore_plot_gdi.c#L731-L849)（`plot_bitmap`）。
- **机制**：虽然 SVG XML 与 NanoSVG 路径解析已缓存，但**光栅化像素/DIB 完全不缓存**。每次 `PImage_DrawSvg` 都会：`malloc(W*H*4)` → 软浮点 `nsvgRasterize` → `CreateDIBSection` → 遍历全像素做预乘 Alpha，**每个像素执行 3 次整数除法 `/ 255`**（ARMv4I 无硬件除法指令，每像素调 3 次 `__rt_sdiv`，甚至对 `alpha==0` 和 `alpha==255` 也不跳过）→ `CreateCompatibleDC` + `AlphaBlend` + 全套销毁。
- **平铺背景放大**：`plot_bitmap` 循环每个可见平铺 tile 调用 `pcore_plot_bitmap_once`，若背景是平铺 SVG，**每帧每个 tile 都会完整重跑一次软光栅化与 `/255` 预乘**。普通位图（[pimage_wm.cpp](../../positron_image/pimage_wm.cpp#L376-L414)）同样未缓存解码后的 `HBITMAP`/DIB，每帧每 tile 直接调用 COM `IImage::Draw`。

### 4.2 GDI 绘制路径高频句柄抖动（P0）
- **`plot_text` 绕过字体缓存**：布局测量已有 72 项 `g_fc` `HFONT` 缓存，但绘制路径 [plot_text](../../positron_core/pcore_plot_gdi.c#L589-L651) 每次直接调用 `pcore_plot_font` → `CreateFontIndirectW` → `SelectObject` → `GetTextMetricsW` → `ExtTextOutW` → **无条件调用 `GetTextExtentPoint32W`**（即便单段文本根本不用返回值）→ `DeleteObject`。屏幕上每个文本片段每帧触发 6+ 次 GWES 跨进程系统调用。
- **`plot_clip` 与图形原语句柄抖动**：[plot_clip](../../positron_core/pcore_plot_gdi.c#L500-L524) 无“裁剪区未变化”短路，每次执行 `RestoreDC → SaveDC → GetViewportOrgEx → GetWindowOrgEx → CreateRectRgn → SelectClipRgn → DeleteObject`（6+ 次 GWES 调用）；[plot_rectangle](../../positron_core/pcore_plot_gdi.c#L526-L573) / `stroke_line` / `plot_disc` / `plot_polygon` 每次现建现删 `HBRUSH`/`HPEN`，虚线框每段还执行 4 次软件除法。

### 4.3 文本测量与断行热路径（P1）
- **无 ASCII 快速路径**：[pcore_font_kind_for_codepoint](../../positron_core/pcore_plot_gdi.c#L167-L208) 在测量与绘制的内循环中对**每个 ASCII 字符**执行 3 次二分查找（共约 19 次比较），尽管三张符号/Emoji 表的最小码点均为 `U+200D`。
- **`gdi_font_split` `O(K·L)` 重复测量与堆分配**：[pcore_measure](../../positron_core/pcore_plot_gdi.c#L1166-L1228) 每次做 3 次 `malloc`/`free`；[gdi_font_split](../../positron_core/pcore_plot_gdi.c#L1362-L1439) 折行时传 `nMaxExtent = 0x7fffffff` 测量整段剩余文本，导致 `K` 行段落产生 `O(K·L)` 次 UTF-8 解码、二分查找和 GDI 测量；`g_measure_dc` 每次测量均 `SelectObject` 选入再恢复旧字体。

### 4.4 CSS 解析缓存盲区与全量样式/布局重建（P1）
- **缓存遗漏**：[PCore_StyleDocumentEx2](../../positron_core/pcore_select.c#L2628-L2770) 仅缓存无 `@import` 的外部 `<link>` 样式表；**UA 样式表、所有 `<style>` 块、所有行内 `style="..."` 属性、以及含 `@import` 的整棵样式表树在每次 `StyleDocument` 均从零重解析**。即便是命中的 `<link>` 缓存，也每次全量 `memcmp`（最多 256 KiB）且按键 `entry->parsed_pw == cc->pw` 校验栈指针地址（不同调用栈地址将 100% 缓存未命中）。
- **无条件 3 遍 CSS 兼容预处理器**：[pcore_parse_css_mode](../../positron_core/positron_core.c#L979-L1057)（连同 UA CSS 和每条行内 `style=""`）每次都无条件 `malloc(len*2+1)` 和 `malloc(len+64)`，[pcore_css_compat_values](../../positron_core/pcore_css_values.c#L578-L697) 对每个未转换字节逐字节 `memcpy(1)` 后若 `conversions==0` 再整块释放。
- **全 DOM 清理/重算 + 无 ID 索引**：`libdom` 与 Core 均无 `id -> element` 哈希表，45+ 个 `PCore_Node*ById` 每次均为 `O(N)` 整树遍历（连 `__body__`/`__documentElement__` 都要先全树遍历未命中再回退，[pcore_select.c](../../positron_core/pcore_select.c#L5016-L5039)）；`pcore_box_for_node` 忽略节点已挂载的 box 指针，每次全树 `O(B)` 搜索（[pcore_box.c](../../positron_core/pcore_box.c#L14238-L14279)）；任何微小 DOM 改动调用 `pcore_render_invalidate` 释放整棵盒树。

---

## 五、应用宿主（`positron_app`）UI 线程与调度

### 5.1 页面加载与提交触发 4–5 次冗余整页样式/布局（P0）
- **图片下载完退回 STYLE**：[app_navigation_advance](../../positron_app/main.c#L6060-L6067) 发现 pending 图片后把恢复阶段设回 `APP_NAV_COMMIT_STYLE`，图片拉完后白跑一遍全量 `PCore_StyleDocumentEx2`。
- **提交后无条件二次/三次重排**：`APP_NAV_COMMIT_LAYOUT` 在 [main.c](../../positron_app/main.c#L6082) 刚做完 `PCore_LayoutDocument`，切换页面后 L6149 立即无条件调用 `app_relayout()`（再做 1 次完整 `Style + Layout`；若滚动条出现改变视口宽度，循环内再做第 3 遍）。内部页 `app_load_local_page`（[L3514](../../positron_app/main.c#L3514)）同样存在双遍布局。
- **脚本分步固定定时器空等**：每个 bootstrap stage（最多 40 步）和每个 `<script>` 之间各强制等待 1 个 16ms timer tick（设置页甚至用 50ms tick，[main.c](../../positron_app/main.c#L4989)），即使轻步骤耗时 <1ms 也会平白累加数百毫秒空等。

### 5.2 原生表单输入/焦点无合并同步整页重排（P0）
- JS DOM 改动已通过 `APP_WM_CONTROLS_REFRESH` 合并，但原生控件事件 [app_controls_notify](../../positron_app/app_controls.c#L828-L833) → [app_controls_changed](../../positron_app/main.c#L1313-L1334) **完全同步且无合并**，每次调用都执行：`app_relayout()`（完整样式+布局+控件重定位）→ **冗余第 2 次 `AppControls_Reposition`** → 全视口 `InvalidateRect(NULL, TRUE)`。
- 该路径在**每次键盘/SIP 输入 (`EN_CHANGE`)**、**每次焦点进出 (`WM_SETFOCUS`/`WM_KILLFOCUS`)** 与单选/复选切换时都会触发。两个输入框间切焦点一次即导致 **2 遍整页重排 + 4 遍控件重定位**。

### 5.3 触摸拖动与原生控件重定位（P1）
- [app_page_pointer_move](../../positron_app/main.c#L7993-L8015) → [app_scroll_to_position](../../positron_app/main.c#L953-L1032) 在每次 `WM_MOUSEMOVE` 上同步执行：双轴分次 `ScrollWindowEx` → 同步 `UpdateWindow` → 无节流同步 `AppScript_NotifyScroll` → [AppControls_RepositionForScroll](../../positron_app/app_controls.c#L3702-L3783)（重新查询 Core 布局几何、未用 `DeferWindowPos` 而逐个调用 `MoveWindow(..., TRUE)`，连已隐藏且仍在屏外的控件也重复调用 `ShowWindow`）。
- `<select>` 变更检测 [app_controls_select_fingerprint](../../positron_app/app_controls.c#L644-L696) 每次 `Reconcile` 对全部 `<option>` 做 2 次 Core 查询 + `malloc`/`free`，任意一个下拉框变化就销毁并重建**整页所有 `<select>` 窗口**。

### 5.4 空闲常驻定时器与 Debug 日志干扰（P1）
- `APP_SETTINGS_TIMER_ID`（**50 ms / 20 Hz**，[main.c](../../positron_app/main.c#L3819)）与应用同生命周期永不停止；`APP_SCRIPT_TIMER_ID`（**250 ms**）常驻轮询；`APP_ADDRESS_ANIMATION_TIMER`（**160 ms**，[app_address_bar.c](../../positron_app/app_address_bar.c#L114-L120)）在标题超出 240px 时空闲期永续重绘，连跑马灯 1000ms 停顿帧也照常重画 `ExtTextOutW` + `BitBlt`。
- **Debug 构建测量失真提示**：[app_debug.c](../../positron_app/app_debug.c#L22-L67) 在 `_DEBUG` 下对每次 `WM_PAINT`、每次 `app_relayout`、每次 `setAttribute`、每次事件派发都同步调用 `OutputDebugStringW` + `WriteFile`（文件达 128 KiB 上限后 `OutputDebugStringW` 依然每次触发内核调试陷阱）。Release 已编译排除，但在做性能归因时必须使用 Release 或关闭热路径日志，避免被调试输出误导。

---

## 六、纯资源应用 Runtime、DB、Archive 与 Media

### 6.1 Runtime 安装/启动与资源读取的冗余 I/O 与跨线程抖动（P0）
> 说明：不可变快照核验与 SHA-256 校验属于安全/正确性合同，优化目标是消除**纯粹的重复计算与无谓跨线程/符号查找开销**，而非弱化校验。

- **启动双次全树枚举 + 每文件 2 次 CryptoAPI + 安装后重跑 2–3 轮**：
  - [revision_capture](../../positron_app/app_runtime_platform.c#L615-L666) 先扫描并钉住最多 128 个句柄（每文件/目录调用 `app_runtime_canonical` 对每层路径做 `FindFirstFileW`，`O(N × depth)`），把全部文件（最多 4 MiB）同步读入内存，**在保持第一批 128 句柄打开的同时再次整树扫描（峰值 256 句柄）并做 `O(N²)` `wcscmp` 交叉比对**。
  - [rv_step](../../positron_runtime/positron_runtime.c#L1657-L1769) 对每个文件重新调用 [revision_hash_begin](../../positron_app/app_runtime_platform.c#L553-L576)（先试 `PROV_RSA_FULL` 失败再回退 `PROV_RSA_AES`——**每文件 2 次 `CryptAcquireContext`**）；每 64 KiB 块调用 `revision_read_at` 做 `O(N)` 线性查找 + `WideCharToMultiByte`（`O(N²)` 宽字符转换）；阶段 1 还有 `O(N²)` `CompareStringW` 冲突检查。
  - 首次安装/更新/恢复提交后（[app_runtime_install.c](../../positron_app/app_runtime_install.c#L356-L358)），立即关闭刚核验完的 revision 并调用 `install_open_ready()`，把整套双枚举 + 4 MiB 读取 + 全文件 SHA-256 完整重跑第 2 遍（PXI 更新达 **3 遍**）。
- **每读取 64 KiB 资源块就跨线程查 DB + 15 次 `GetProcAddress` + 2 MiB 临时堆分配**：
  - 每次读取 `app://` 子资源，[main.c](../../positron_app/main.c#L5463) 都直接 `malloc(2 MiB)`（无论文件多小）；`AppRuntimeApp_Read` 阻塞等待 Runtime worker（跨线程第 1 跳）；`PRuntime_InstalledReadFile` **每 64 KiB 块**调用 `rv_refresh()` → 阻塞等待 `AppSettingsStore` worker（跨线程第 2 跳）→ [install_api](../../positron_app/app_install_store.c#L89-L109) 执行 **15 次 `GetProcAddress`** → 现场 `PDb_Prepare/Step/Finalize` 查 SQLite → 经历 4 次整块内存拷贝。
  - 每次作者 JS 调用 `storage.sql`（[positron_runtime.c](../../positron_runtime/positron_runtime.c#L1115)）同样触发上述跨线程跳 + 15 次 `GetProcAddress` + 主库 SQLite 查询；UI 端 `AppRuntimeServices_Pump` 每个 timer tick 最多交付 1 条结果。

### 6.2 SQLite 持久化与查询开销（P1）
- **最重 Flash 刷盘模式**：[PDb_OpenUtf8Ex](../../positron_db/positron_db.c#L2189-L2195) 设 `journal_mode=DELETE` + `synchronous=FULL`（每次写入均需创建、fsync、删除 journal 文件）。每次网页导航写历史（[app_visits_write](../../positron_app/app_settings_store.c#L637-L672)）都提交一次 `synchronous=FULL` 事务并执行 `OFFSET 500` 裁剪子查询；每次下载经历 4 次 `synchronous=FULL` 提交；`PDb_OpenUtf8Ex` 未设 `cache_size`（默认 2000 页 ≈ 8 MiB 潜在占用）。
- **重复打开与无语句缓存**：[PDb_OpenExistingLocalUtf8](../../positron_db/positron_db.c#L4722-L4741) 连续打开同一数据库两次并执行 16 条元数据/PRAGMA 查询；应用启动 `app_settings_initialize` 执行 10 条 `sqlite_master` 计数查询 + 写事务；所有查询均无 prepared statement 缓存；`PDb_ExecuteRestricted` 即使纯只读查询也显式包裹 `BEGIN`/`COMMIT` 且每 100 条 SQLite VM 指令回调一次 progress handler。

### 6.3 Archive 与 PXI 解包 I/O（P1）
- [pa_admit](../../positron_archive/positron_archive.c#L175-L242) 对路径冲突采用 `O(N²)` `MultiByteToWideChar + CompareStringW`（128 项 = 8,128 次，1024 项 = 52 万次）；解析目录头每项触发 4–5 次微小无缓冲读回调，每次经 [pxi_read](../../positron_app/app_pxi.c#L48-L57) 附带 2 次 `GetFileInformationByHandle`（128 项仅打开即 ~640 次读 + ~1280 次 stat）。
- [pxi_extract_entry](../../positron_app/app_pxi.c#L156-L200) 对**每个解出的独立文件调用 `FlushFileBuffers`**，关闭后立刻重新 `CreateFileW` 钉住句柄。

### 6.4 Media 内存峰值与软浮点热循环（P2）
- [pm_load_input](../../positron_media/positron_media.c#L162-L240) 同步把整份媒体文件（最高 16 MiB）从 32 KiB 倍增 `realloc` 读入单一连续堆块（易在 CE 5.2 的 32 MiB 槽位中 OOM/碎片化），且 `pm_probe` 与 `pm_open` 会读并解析整份文件两遍；`seek(0)` 重建整个 FFmpeg 上下文。
- [pm_ff_convert_audio](../../positron_media/positron_media_ffmpeg.c#L1196-L1262) 将 8 路格式分支写在 `(samples × channels)` 最内层循环中，对 AAC/MP3 的 `AV_SAMPLE_FMT_FLTP` 每样本执行软浮点比较、乘法与强转（44.1 kHz 立体声 ≈ 88,200 次软浮点运算/秒）；[pmedia_waveout_pump](../../positron_media/pmedia_waveout.c#L191-L271) 仅 1 个 `WAVEHDR` 单缓冲且每块 `Unprepare/Prepare`，必然产生播放间隙。

---

## 七、建议的推进顺序（低风险高回报优先）

若后续进入优化阶段，建议按**“先量化基线 → 先消明显冗余（零 ABI 变更）→ 再做跨层结构优化”**的顺序分纵切推进：

1. **第一梯队（纯内部优化、零公共 ABI 变更、立竿见影）**：
   - **渲染与排版（Core + Image）**：缓存 SVG 预乘 `HBITMAP`/DIBSection 并消除 `/255` 软除法与平铺重光栅化；`plot_text` 接入 `g_fc` 字体缓存并去冗余 `GetTextExtentPoint32W`；`plot_clip` 复用单个 `HRGN` 并加未变更短路；`pcore_font_kind_for_codepoint` 加 `< 0x200D` ASCII 快路径；`gdi_font_split` 限制 `nMaxExtent`。
   - **UI 调度与页面提交（`positron_app`）**：去掉加载期图片回退 `STYLE` 与提交后二次 `app_relayout()`；将原生控件按键/焦点变化与全页 `app_relayout()` 解耦（或合并为按需重排/重绘）；`AppControls_RepositionForScroll` 缓存几何+跳过隐藏项；空闲时停用 50ms/160ms 无变化定时器。
   - **Browser 局部热点与 Runtime 冗余**：修复 `liveTagCollection9` 每次下标访问重扫整树、`p_browser_script_form_property` 二次解析与 `install_api()` 每次 15 次 `GetProcAddress`；缓存不可变 `localName`/复用 CryptoAPI provider 句柄/按真实文件大小分配 Runtime 读取缓冲（替代每次 `malloc(2 MiB)`）。
2. **第二梯队（需设计固定预算与回归夹具）**：
   - **Core 样式与 DOM 索引**：缓存 UA CSS 与 `<style>`、短路线性预处理器、引入文档级 `id -> element` 索引与复用已挂载的 box 指针。
   - **Browser/Script 字节码复用与轻量化桥接**：按 `ROADMAP` 先测 Duktape 可信字节码缓存的体积/峰值内存/冷热耗时，再决定内部复用；对高频单字段 DOM 属性减少 JSON 往返与节点闭包数量。
3. **第三梯队（协议/并发/存储架构升级，需完整设备门与故障边界评审）**：
   - **HTTP/TLS 连接复用 + `Accept-Encoding: gzip` + 有界并行子资源抓取（2–4 worker）**。
   - **SQLite `journal_mode=TRUNCATE/PERSIST` 与语句缓存评估**（须核对既有崩溃恢复边界）及 **Media 音频内循环定点数/双缓冲 WaveOut**。

# Positron

**当前设备基线（next156，2026-08-08）**：修正 next155 中事件回调 JSON 过滤器
误删高位 UTF-8 字节的问题；显式脚本 context 中，EDIT/SELECT 可接收单个 BMP
`WM_CHAR` 的 Unicode `keypress`，EDIT 的 `beforeinput.data` 同步携带 UTF-8 字符，
取消 SELECT 仍会阻止原生默认动作。TEST121 使用 `→`/`★` 检查 key/keyCode/charCode、
UTF-8 key、beforeinput 数据和 target/bubble。C89、仓库审计、VS2008 ARMV4I Debug
增量构建、staging 和 `screen=640x480 dpi=192` 设备验收均通过，日志为
`TESTBENCH PASS`，位于 `C:\WMShare\Positron-next156\test_host.log`。默认
`javascript=0`、TEST13 网络路径不变。

**next157 设备失败（2026-08-08，不能作为基线）**：在 next156 的 BMP 桥之上，原生 EDIT/SELECT
现在把成对 UTF-16 代理项合并为一个 Unicode 标量，再分别派发一次 `keypress`；EDIT
还派发一次包含完整 UTF-16 数据的 `beforeinput(insertText)`。TEST122 检查
`U+1F600/U+1F603` 的标量 keyCode/charCode、JavaScript UTF-16 code unit、data、
target/bubble 和取消 SELECT 默认动作。不完整代理项回退到原生窗口过程；IME、
composition、剪贴板完整 Unicode payload、字体覆盖和完整 Keyboard/Event API 仍未实现。
`C:\WMShare\Positron-next157\test_host.log` 中 TEST13 及 TEST20/27/43/44/56/58-121
均通过，但 TEST122 失败；因此不能把该包或该实现写成设备基线。

**next158 诊断完成（2026-08-08）**：TEST13 与 TEST20-121 继续通过；TEST122 的实际
事件显示标量 `keyCode/charCode` 正确，但四字节 UTF-8 的 `key/data` 在 Duktape 中成为
长度 1 的非标准 ECMAScript 字符，而不是两个 UTF-16 code unit。问题不在 WM 代理对合并。

**next159 待设备验收（2026-08-08）**：事件 JSON 现在把合法 non-BMP UTF-8 标量写成
两个 `\uXXXX` 代理项，由 Duktape JSON decoder 生成 CESU-8/ECMAScript UTF-16 字符串；
ASCII/BMP、标量代码、默认 `javascript=0` 和 TEST13 均不改变。ARMV4I staging 包位于
`C:\WMShare\Positron-next159`，须以同包完整日志晋级。

**next155 设备失败记录（已替代，2026-08-08）**：TEST13 以及 TEST120 之前的回归均
通过，但 TEST121 失败。宿主侧 `pcore_browser_script_key_safe()` 把合法 UTF-8 的高位
字节判为不安全，导致 `key`/`beforeinput.data` 被清空；不是 TEST13 网络或 BMP 编码
断言应放宽。next156 已改为 JSON 转义并保留该断言，失败包不得作为基线。

**next154 设备门禁（2026-08-08）**：在保持默认 `javascript=0` 和 TEST13
网络路径不变的前提下，显式脚本 context 中的原生 EDIT/SELECT 新增
`WM_SYSKEYDOWN/UP` 与 ASCII `WM_SYSCHAR` 事件桥；桥显式记录 system-key 的
`altKey`，不依赖桌面线程的 `GetKeyState`，并沿用现有 target/bubble/cancel ABI。
TEST120 覆盖 EDIT/SELECT 的 `keydown/keyup/keypress`、ArrowLeft/ArrowRight、Alt
元数据和取消 SELECT 默认动作。C89、仓库审计、VS2008 ARMV4I Debug 增量构建、staging
和 `screen=640x480 dpi=192` 设备验收均通过，日志为 `TESTBENCH PASS`，位于
`C:\WMShare\Positron-next154\test_host.log`。这不是 IME/composition 或完整
Keyboard/Event API 的实现。

**next153 设备门禁（2026-08-08）**：next153 在 `screen=640x480 dpi=192`
设备上完成默认配置，TEST13、20、27、43、44、56、58-77、80-119 全部通过并记录
`TESTBENCH PASS`。TEST13 的 example.com、IANA Example Domains、IANA Reserved
Domains 三段导航均完成；TEST112 确认页面级 script context 的后续求值，TEST113 确认
click 事件派发/取消默认动作，TEST114 确认原生表单事件元数据、冒泡和 DOM 更新，
TEST115 确认原生 EDIT 键盘事件元数据，TEST116 确认可冒泡 focusin/focusout，
TEST117 确认受限 beforeinput 的数据、冒泡与取消默认动作，TEST118 确认原生 SELECT
键盘事件的 target/bubble 元数据和 WM 消息入口，TEST119 确认 EDIT/SELECT 的
`WM_CHAR -> keypress` 元数据、冒泡与取消 SELECT 默认动作。ARMV4I Debug 增量构建、
staging 和设备验收均已通过；后续仍需轮换分辨率/DPI，并人工复查新增可见能力。设备
日志位于 `C:\\WMShare\\Positron-next153\\test_host.log`。

**next153 实现说明（设备已通过，2026-08-08）**：在显式 `javascript=1` 的页面 context
中把原生 EDIT/SELECT 的可识别 `WM_CHAR` 映射为可取消的 `keypress`，复用
`PCoreKeyEventData` 把 `key/keyCode/charCode/repeat` 送入 target/bubble listener；
TEST119 同时检查 synthetic SELECT 派发、真实 EDIT/SELECT WM 消息入口，以及取消
SELECT 默认动作。C89、仓库审计、VS2008 ARMV4I 增量构建、staging 与
`screen=640x480 dpi=192` 设备验收均已通过；默认 `javascript=0`、TEST13 网络路径和
next152 已验收行为不变。

**next152 实现说明（设备已通过，2026-08-08）**：显式 `javascript=1` 的页面新增原生
`COMBOBOX/LISTBOX` 的 `WM_KEYDOWN/WM_KEYUP` 子类桥，复用 `PCoreKeyEventData` 和
`PCore_EventDispatchKeyAt`；TEST118 同时验证公开 SELECT 键盘事件传播与真实 WM
`COMBOBOX` 消息。该批已通过 C89、仓库审计、VS2008 ARMV4I 增量构建、staging 和
`screen=480x640 dpi=192` 设备 testbench；默认 `javascript=0`、TEST13 网络路径不变。

**next146 实现说明（2026-08-08）**：在保持默认 `javascript=0` 和 TEST13
网络路径不变的前提下，浏览器导航请求会把显式 `javascript=1` 的初始 classic-script
runtime 与当前 document 一起保留；成功导航时整体换入新页面，失败导航、旧页面释放和
窗体关闭时一起清理。TEST112 离线确认初始脚本状态可被后续求值复用，并能通过最小 DOM
bridge 更新文字后重新进入 style/layout。该候选已通过 C89、ARMV4I 增量构建和仓库审计，
并已在 `240x320 dpi=96` 设备上通过 TEST112；这不代表已实现事件、异步任务或完整
DOM/window binding。

**next147 实现说明（2026-08-08）**：在 next146 的页面级 context 之上，显式
`javascript=1` 页面获得最小 `addEventListener/removeEventListener` bridge；WM 点击继续
走已验收的 Core DOM event dispatch，JavaScript handler 可以读取可信 click 的基本信息、
更新 DOM，并以 `preventDefault()` 阻止既有默认动作。TEST113 离线覆盖 listener、取消、
重新布局和移除监听器后的第二次派发；默认 `javascript=0` 与 TEST13 路径不变。C89 和
ARMV4I 构建已通过，并已在 `480x640 dpi=192` 设备上通过 TEST113；这不代表完整
Mouse/Keyboard/Event API。

**next148 实现说明（2026-08-08）**：在 next147 的事件 bridge 之上，显式
`javascript=1` 页面现在接收 WM 原生 EDIT/SELECT 的 `focus`、`blur`、`input` 和
`change` 事件；`input/change` 允许冒泡，焦点事件保持非冒泡，事件仍是可信且不可取消。
TEST114 离线覆盖事件元数据、父级冒泡和 DOM 更新；宿主接线已通过 C89、ARMV4I
增量构建，并在 `screen=320x320 dpi=128` 设备上通过。默认 `javascript=0` 与 TEST13
路径不变，仍不是完整 Keyboard/Focus/Input/Event API。

**next149 实现说明（设备已通过，2026-08-08）**：在 next148 的表单事件桥上新增
`PCoreKeyEventData` 与键盘事件派发 ABI；显式 `javascript=1` 时，WM 原生 EDIT 的
`keydown/keyup` 会把 `key/keyCode/charCode/repeat/shiftKey/ctrlKey/altKey` 传入页面
事件对象。TEST115 离线与 `screen=320x320 dpi=128` 设备日志均覆盖 Enter 的可信事件
元数据和默认动作结果；C89、ARMV4I 增量构建、仓库审计和 staging 均已通过。默认
`javascript=0`、TEST13 网络路径和已验收的表单行为不变；WM SELECT、`keypress`、
`beforeinput`、`focusin/focusout` 和完整 Keyboard/Event API 仍未实现。

**next150 实现说明（设备已通过，2026-08-08）**：在 next148 的原生焦点桥上追加
可冒泡的 `focusin/focusout`；现有非冒泡 `focus/blur` 顺序保持不变，显式
`javascript=1` 时在对应生命周期点派发新事件。TEST116 离线覆盖目标/冒泡阶段、
`bubbles/cancelable/trusted` 元数据及事件后 style/layout；C89、ARMV4I 增量构建、
staging 和 `screen=320x320 dpi=128` 设备验收均已通过。默认 `javascript=0`、
TEST13 网络路径和 next149 键盘/表单行为保持不变；`beforeinput`、WM SELECT 键盘
变化、字符输入/IME 和完整 Keyboard/Event API 仍未实现。

**next151 实现说明（设备已通过，2026-08-08）**：公开
`PCoreInputEventData` 并把 `inputType/data` 传入最小 JavaScript 事件对象；显式
`javascript=1` 时，原生 EDIT 对可识别的字符、换行、退格、删除、粘贴、剪切和清除
操作派发可冒泡、可取消的 `beforeinput`。取消发生在原生 EDIT 默认动作之前。
TEST117 离线覆盖目标/冒泡监听器、可信元数据、`preventDefault()` 阻止插入而允许删除、
以及事件后的 style/layout。C89、仓库审计、VS2008 ARMV4I 增量构建、设备包与设备
验收均已通过；WM SELECT 键盘、IME/composition、完整 Unicode/剪贴板数据、`keypress`
和完整 Input/Keyboard/Event API 仍未实现。默认 `javascript=0`、TEST13 网络路径和
next150 行为不变。

**next152 实现说明（设备已通过，2026-08-08）**：在同一键盘事件 ABI 上给原生
`COMBOBOX/LISTBOX` 保存原始窗口过程并做 WM 子类化；显式 `javascript=1` 时先派发
`keydown/keyup`，仅在未被取消时继续执行系统控件默认处理。TEST118 覆盖 SELECT
目标/冒泡、ArrowDown 元数据、可信标志、取消策略和真实 `WM_KEYDOWN/WM_KEYUP`
入口。该批设备日志已通过；IME、`WM_SYSKEY*`、`keypress` 与完整 Keyboard/Event API
不在本批范围内。

**浏览器脚本门禁（next144，2026-08-08）**：新增默认关闭的 `javascript=0/1` 浏览器
开关、按文档顺序枚举经典 inline script 的 core ABI，以及基于独立
`positron_script.dll` 的最小 `document.getElementById(...).textContent=` bridge。
TEST110 同时断言关闭时不执行，以及开启后两个 inline script 共享初次加载 context、
跳过非 JavaScript type 与 external `src`、修改 DOM 后进入正式 style/layout。VS2008
ARMV4I 增量构建与 `screen=320x320 dpi=128` 设备验收均已通过；默认 TEST13 不扫描、
抓取或执行 JavaScript。
该批没有外部脚本执行、事件回调、持久 context 或完整 DOM binding。

**浏览器脚本顺序门禁（next145，2026-08-08）**：新增统一的
`PCore_GetScriptCount/PCore_GetScript` 序列 ABI，按 DOM 顺序映射 inline 与 external
script；开启浏览器脚本时，external body 通过已有 document cache 异步抓取，再与 inline
body 共用一次初始 Duktape context。TEST111 覆盖成功/失败 external、JSON 跳过、执行顺序
和 DOM 结果；ARMV4I 增量构建、C89、仓库审计与 `screen=320x320 dpi=128` 设备验收均已
通过。默认 `javascript=0`，因此 TEST13 默认路径不变。

> **动态 DPI 基线（2026-08-08）**：next143 保留 next137 的非整数 DPI 设备像素换算，
> 隔离 TEST60/63 的显式 CSS 几何上下文，并让 TEST62/75 的几何断言按实际 DPI 等比
> 换算；没有固定 96 DPI、放宽断言或固定分辨率。`screen=480x640 dpi=192` 默认配置已
> 全部通过，next145 又在 `screen=320x320 dpi=128` 完成至 TEST111；下一批应继续轮换
> 分辨率/DPI，并人工复查 TEST13。next144 只在显式
> `javascript=1` 时执行初次加载的 classic inline scripts，默认关闭，float 候选保持撤回，
> next37/next114 Browse 路径仍是回归基线。

**Float 方向暂挂（2026-08-04）**：next115 的普通 float 和 next116 的显式 block-level float 都未通过真实设备门禁。next116 的自动 TEST13 数值记录为 OK，但人工截图显示导航被扁平化、正文边界异常，且 TEST79 最终失败；因此 TEST79 已从默认配置和 ENGINE 组移除。不要把 TEST23/79 当作已支持的 CSS Floats，也不要在没有完整 box construction/normalisation 方案前继续扩大该方向。

失败分支、环境误报、已替代实验和暂挂方向的总索引见 [`.agents/FAILED_EXPERIMENTS.md`](.agents/FAILED_EXPERIMENTS.md)；其中包括 next37 回退、next78 scrollbar 实验、next115/116 float 回退，以及当前“局部容器偏小、文本偏多”的开放视觉限制。

**状态更正（next145，2026-08-08）**：TEST111 已在 `screen=320x320 dpi=128` 设备通过，
因此 external classic script 的 DOM 顺序执行与异步取回已从“待验收”进入设备基线。它仍只在
显式 `javascript=1` 时生效；默认 `javascript=0` 的 TEST13 不扫描、抓取或执行脚本。

面向 **Windows Mobile 6 Professional**（Windows CE 5.2, ARMv4i）的现代基础设施与应用运行时。

Positron 一方面提供可被任意 WM 程序独立调用的现代 DLL 集合，包括 TLS、HTTP、JSON、图片、脚本运行时与渲染核心等能力；另一方面在这些基础设施上建设自带浏览器内核和 Electron-like 应用运行时。当前主线已经进入 HTML/CSS 真实渲染：NetSurf 3.11 的解析、样式、layout/redraw、GDI 绘制、基础定位、动态 `:hover`、点击导航和脚本资源缓存接口都在 `positron_core.dll` 后面推进；`positron_script.dll` 已作为独立 Duktape 执行服务接入解决方案，next145 在 next144 的默认关闭 inline-script 纵切之上增加了已通过设备门禁的 classic external-script 取回与 DOM 顺序执行，next147-149 又逐步加入页面级事件桥，但仍不是完整 DOM/window 或浏览器事件 API。

公共 DLL 是正式产品，不只是 `test_host.exe` 或浏览器的内部依赖。架构与 ABI 原则见 [.agents/ARCHITECTURE.md](.agents/ARCHITECTURE.md)。

> **Browse 冻结基线（2026-07-15）**：导航产品路径曾完整恢复到 `9c5c7c7`/next37，并由 next44 确认 TEST13 从 start page 到 IANA 深层导航全流程正常。此后 `main` 已继续加入图片、字体、列表和表格能力，但没有重新合入 next38 之后失败的 stylesheet metadata、base URL、redirect origin 与 timeout 实验；这些历史保存在 `codex/post-next37-experiments`。详见 [.agents/ROLLBACK_NEXT37.md](.agents/ROLLBACK_NEXT37.md)。

---

## 当前状态

| Phase | 内容 | 状态 |
|---|---|---|
| **1** | `positron_tls.dll` — TLS 1.2 客户端（mbedTLS 2.16 LTS） | ✅ 完成，WM6 Emulator 验证 |
| **2** | `positron_json.dll` (cJSON 1.7.18) + `positron_http.dll` (HTTP/1.1：HTTPS via mbedTLS，明文 HTTP via WinInet) | ✅ 完成，WM6 Emulator 验证 |
| **3** | 嵌入式 CA bundle + verified TLS (`PTls_ConnectVerified`) + CryptGenRandom 熵源 | ✅ 完成，WM6 Emulator 验证 |
| **4** | `positron_core.dll` — NetSurf 内核移植（HTML/CSS 渲染层） | 🚧 正式 Browse 路径已走 NetSurf `layout.c/redraw.c`；flex、table、border、selector、缓存图片链、CSS 背景图与 NetSurf overflow scrollbar 已真机验证，窄屏复杂布局仍待补 |
| **5** | `positron_image.dll` — 可复用图片基础设施 | 🚧 retained 解码、SVG、PNG/JPEG/BMP/GIF 与原始像素入口均已真机闭环；当前 ABI 1.5 增加只读 SVG 创建阶段遥测，next52 原生标题栏 OK 真退出已真机确认 |
| **6** | `positron_script.dll` — 独立 JavaScript 执行基础设施 | ✅ Duktape 2.7.0 稳定 C ABI、模块/provider、global/JSON、native callback 与 structured JSON setter 已完成构建；next134 日志中的 TEST80-99 已通过。next145 又通过默认关闭、显式开启才生效的浏览器 classic inline/external script 顺序门禁；事件、异步任务、网络/完整 DOM binding 仍未开放 |

Phase 3 验证：`test_host.exe` 的通信组——HTTPS GET（`checkip.amazonaws.com`，大陆直连纯文本 IP）、POST（postman-echo）、badssl.com 正样本 + expired + self-signed 三连测，全部真机通过。详见 [PHASE3.md](PHASE3.md)。

Phase 4 进展：vendoring NetSurf 3.11，五个底层库（libwapcaplet / libparserutils / libhubbub / libdom / libcss）全部在 VS2008 / WinCE / ARM 下编译通过（C99→C89 脚本化转换，见 `scripts/c89ize.py` 等）。`positron_core.dll` 已作为产品级引擎边界立起，公开 `PCore_ParseHTML/ParseCSS/StyleDocumentEx/StyleDocumentEx2/LayoutDocument/PaintDocument/LinkAt/FormActivateAt/EventDispatchAt` 等小巧 opaque-HANDLE API。HTML→DOM、CSS 解析、CSS select/computed style、整树样式、外部 `<link rel="stylesheet">` 抓取、GDI 窗口绘制、垂直滚动、viewport/DPI 自适应、点击命中与导航、checkbox/radio 基础交互、HTTPS verified fetch、明文 `http://` via WinInet、跨协议重定向、完整 Mozilla CA bundle 均已真机验证。next123 又把设备窗口的物理像素与 CSS 视口分开，Browse/旋转路径使用 `PCore_SetDeviceViewport`，以符合 NetSurf 的高 DPI 换算约定；next134 已在 `screen=240x320 dpi=96` 设备日志中确认 TEST13/20/27/43/44/56/58-77/80-99，通过其他分辨率/DPI 继续轮换验收。next94 新增 transport/UI 无关的 `PCore_TextInputInfo/SetValue` 与 WM 原生单行 `EDIT` 宿主桥；next97 又在保持结构 ABI 不变的前提下通过 `PCore_TextInputIsMultiline` 接通 textarea 与 WM 多行 `EDIT`，TEST65/66 均已通过设备验收。`StyleDocumentEx2` 新增文档基准 URL 与宿主解析回调；CSS `@import` 使用 libcss 原生 pending/register API，WM 宿主用 `InternetCombineUrlA` 规范化相对 URL，TEST45 已真机通过。

当前 Browse 正式路径已经从早期手写块流布局切到 **NetSurf 真实布局/重绘引擎**：`PCore_LayoutDocument` / `PCore_PaintDocument` / `PCore_LinkAt` 走 `pcore_box_construct` → NetSurf `layout_document` → `html_redraw` → GDI plotter。M7-flex/table、M5f border、CSS attribute/sibling selector 与 `:link` / `:lang()` 已由 TEST 9/17 真机验证。TEST 11 的 margin collapse 与 `padding-top:1px` 阻断折叠成对断言已于 2026-07-10 真机通过。`<img>` alt fallback 已由 TEST 17 验证；TEST 18 的文档级资源缓存与 URL 去重、TEST 20 的 BMP/PNG/JPEG/GIF 缓存 replaced box/`content_redraw`/`plot_bitmap` 绘制均已真机通过。TEST 21 已验证运行时 viewport/DPI 及整数像素 MQ4 `(width <= Npx)` / `(width < Npx)` 的 320/300/299px 边界。TEST13 已确认 `white-space:normal/nowrap` 的源码换行被正确折叠、词间距正常；TEST15 又确认 `<pre>` 换行仍保留。TEST 22 已验证反向 flex 的 25px leading padding；TEST38-39 进一步关闭了 IANA 顶层根变量造成的窄屏间距问题，当前截图中的导航、正文和注册表列均已可读，但其他真实子页仍需持续观察。Browse host 在布局前使用同一 HTTP 获取器填充 `<img>` 缓存，失败仍保留 alt/src 回退。SVG parse/draw/cache/fallback 已由 TEST25-28 真机通过，TEST13 的 HTTPS HTML + 相对 SVG 网络 fixture 也已显示正确。详见 [PHASE4.md](PHASE4.md)、[.agents/ROADMAP.md](.agents/ROADMAP.md) 和 [.agents/KNOWN_LIMITATIONS.md](.agents/KNOWN_LIMITATIONS.md)。

当前可用能力：TLS/HTTP/JSON 通信栈；HTML/CSS/DOM 解析；CSS select + computed style；整树样式；外链 CSS；NetSurf real layout/redraw；GDI plotter；滚动、viewport/DPI 自适应、点击链接导航；flex、常见 table、border、CSS attribute/sibling/static-pseudo selector、`<img>` alt fallback 与 `<img src>` 资源发现/fetch。WM Imaging 的 BMP/PNG/JPEG/GIF 与缓存 `<img>` 链已真机验证。`positron_image.dll` 公共 C ABI 已接通 WM Imaging、Expat、libdom XML、libsvgtiny 与 NanoSVG rasterizer；`positron_script.dll` 提供独立 UTF-8 JavaScript 求值、持久上下文、错误恢复和资源遥测，本身不创建 DOM/window、也不抓取网络。next144 由 Browse 宿主在该 DLL 之上注入已通过设备门禁的最小 window/document text bridge。`PImage_CreateBitmapFromMemory/BitmapGetInfo/DrawBitmap/FreeBitmap` retained 位图对象会复制输入字节，NetSurf 图片载体也复用同一解码对象。2026-07-15 的 TEST19/20 已确认四格式颜色、清空调用方缓冲后重复绘制、损坏输入拒绝、旧核心 ABI 转发与正式缓存链；TEST26/27 和 TEST13 同批无回归。

最新设备反馈（2026-07-30）：next44 的 TEST13 全流程确认 next37 恢复点可作为 Browse 冻结基线；next45 又确认公共位图 ABI 的 TEST19 四格式、TEST20 缓存图片、TEST26/27 SVG 与 TEST13 全流程均正常。next46 的 ABI 1.0 独立 WM 示例也已在横竖屏确认，SVG 曲线缩放后的平滑观感略逊于先前大图但可接受。next47 已确认 ABI 1.1 的 PNG/JPEG 内存编码、DLL 配套释放及重新解码闭环；next48 证明 WM `EncoderQuality=100` 仍无法修复小图色度串扰。next49 已确认静态 libjpeg-turbo 1.5.3 的显式 quality JPEG 为正确 4:4:4，行方向、红绿蓝黄颜色与 PNG 一致，大面积色带消失。Debug DLL 相比 next48 增加 243,712 字节（约 238 KiB），设备不需额外 JPEG DLL；额外 CPU 与约 `width*height*3` 的主要中间像素内存只发生在显式 JPEG 编码。next50 的六项截图又确认 ABI 1.3 的 padded BGR24、straight-alpha BGRA32、RGB/alpha PNG、JPEG 与 SVG 一致。next51 进一步证明 ABI 1.4 的 BMP/GIF 隐藏编码、签名与回读检查通过，但也证明标题栏 X 是 Shell Smart Minimize，不保证发送 `WM_CLOSE`。next52 改用 WM/Pocket PC 原生 `SHDoneButton(SHDB_SHOW)` 标题栏 OK，并在 `WM_COMMAND/IDOK` 销毁窗口；用户已确认点击 OK 后进程消失且可以正常再次启动。next53 又确认 TEST46 四行三列表格 span 颜色/位置正确，TEST13/17/41/42 其余功能正常。next54 虽让 TEST41 的 auto-height 横条获得独立空间并去掉短页无效纵条，却因第二次整树 layout 同时改变 fixed-height overflow 几何而令 TEST42 自动断言失败；next55 已限制二次 layout 只影响 auto-height 容器并修正右箭头坐标，用户现已确认 TEST41/42、短页纵条与色块页全部正常。next56 的 TEST47 红/白、绿/蓝两行及同批其他测试现已确认正常。next57 的 TEST48 已确认列表层级和有序计数语义；next58/59 的随包字体最终让基础箭头、marker 与五个单色 emoji 可见。next61 的 TEST50 已确认 IV/z/aa/09 计数、绿色缓存 SVG marker 与圆形失败回退全部通过。next62/TEST51 与 next63/TEST52 又依次确认 inline-first 和 block-first/空条目/嵌套/图片的 inside marker 流在横竖屏符合预期。next64/TEST53 的纵横屏截图现已确认 collapsed border 的宽度、样式、hidden、来源 tie 与 separate 对照均符合预期；next65/TEST54 又确认 finite/auto rowspan、colspan 与 row-group 四组终止边正确。next106/TEST72 进一步确认首批 `required/valueMissing` 约束验证、提交/Enter 阻止、首个无效控件定位、`novalidate/formnovalidate`、multipart 与 reset；同包 TEST13 深链及 TEST20/27/43/44/56/58-71 全部 PASS。

2026-07-31 的 next109/TEST73 已继续推进该基线：live `:checked/:enabled/:disabled`、宿主焦点/按压状态驱动的 `:focus/:active`、cache-only 重样式、纵横屏保持与 reset 均通过；同包 TEST13 三段深链及 TEST20/27/43/44/56/58-72 全部 PASS。它不代表 DOM Focus/Mouse 事件传播、取消或默认动作已经实现。空且无 CSS 尺寸的 text input 仍缺浏览器默认 intrinsic size，TEST73 使用与既有原生 EDIT 路径一致的显式 CSS 尺寸。

2026-07-31 的 next110/TEST74 已补上最小 DOM 事件纵切：Core 公开文档所有的 listener handle、按 id/布局坐标派发的可信通用事件、捕获/目标/冒泡、`preventDefault`、两种停止传播和显式移除；WM Browse 宿主在链接、表单、文件、label 等现有 click 默认动作之前检查取消结果。同步修正 vendored libdom 将 target 重复放入祖先路径、无视 `bubbles/cancelable` 及不清理 dispatch-only 状态的问题。设备日志确认 TEST74 和 TEST13 三段深链及 TEST20/27/43/44/56/58-73 全部 PASS。该批尚不是完整 MouseEvent/KeyboardEvent/FocusEvent/InputEvent，也未完成所有 HTML 激活细节或 JavaScript 绑定。

2026-08-03 的 next111/TEST75 按 NetSurf 上游的绝对定位特例补齐 slim box builder：普通 `position:relative` 保持正常流并应用偏移，`position:absolute/fixed` 的 block 继续进入正式定位路径，`display:inline` 的脱流元素改为 `BOX_INLINE_BLOCK`。设备自动日志确认 TEST75 与 TEST13/20/27/43/44/56/58-74 全部 PASS。该批不等于 float、Grid 轨道或完整 positioned containing-block 组合已经实现。

2026-08-03 的 next113/TEST76 接通动态 `:hover`：WM6 宿主用 `WM_MOUSEMOVE` 加 250ms 定时器轮询离开窗口，core 命中最近 DOM 元素并在下一次样式选择时应用 hover 状态。设备自动日志确认 TEST76 与 TEST13/20/27/43/44/56/58-75 全部 PASS。该批不等于 `:visited`、`:target`、`:indeterminate`、专用 MouseEvent 或 JavaScript 已实现。

2026-08-03 的 next114/TEST77 建立了独立的脚本资源 ABI：core 扫描外部 `<script src>`，可通过 `PCoreResolveUrlFn` 使用宿主 URL 策略，调用 transport-agnostic fetch/free 回调，把成功字节按 document 生命周期缓存，并提供只读计数/枚举接口。该批不执行 inline 或 external JavaScript，也尚未接入 TEST13 的网络事务；ARMV4I 增量构建通过，TEST77 与整批设备自动 testbench 已确认 PASS。2026-08-04 的 next118 又把仓库已有 Duktape 2.7.0 封装成独立 `positron_script.dll`，TEST80 覆盖 ABI、持久求值、throw 后恢复和 DLL 内存遥测；next119 新增 TEST81，覆盖执行超时、源码长度上限和上下文恢复；next120 新增 TEST82 硬内存配额断言，设备确认峰值 496184/524288、恢复值 42。2026-08-05 的 next121 再增加 CommonJS 风格模块一次执行缓存、`require()`、失败回滚和清空 API；next124-126 继续增加 provider、global/JSON、native callback 与 structured setter。next134 在 `screen=240x320 dpi=96` 日志中确认 TEST80-99 全部通过。next144 把该独立 DLL 接到默认关闭的浏览器 classic inline-script 纵切，并已在 `screen=320x320 dpi=128` 完成 TEST110 与整批回归门禁。

当前明确缺口：位图四格式与 SVG 网络/缓存/fallback/fill-rule/基础渐变缓存链已经闭环，但径向焦点 `fx/fy` 与 spread method 仍是 NanoSVG 光栅器的显式 TODO。CSS Variables 兼容层只替换同一 stylesheet 顶层精确 `:root` token，不支持元素作用域、跨 stylesheet cascade 或 `@property`。现代值兼容只处理数值型 `oklch()` 到裁剪 sRGB，以及无需布局上下文即可完全求值的同单位 `calc()`；混合单位、`color-mix()` 和完整 CSS Color/Values 仍未支持。CSS Grid 目前只是保持文档顺序的单列 block 降级，TEST41 只防止 grid 内宽表格推走整个 flex 页面，不代表网格轨道或 gap 已实现。标准 NetSurf overflow scrollbar 已由 TEST42/next55 验收，但不包含触摸惯性或 overlay scrollbar。CSS `@import` 的嵌套解析、失败空表回退和文档缓存已由 TEST45 验收；它尚不代表跨源策略、完整缓存失效或整页资源进度已完成。有效表格的 span 占位、匿名 row/cell、collapsed-border 冲突、cell vertical alignment、`empty-cells`、显式 table height 与百分比 row 第二遍已由 TEST46/47、TEST53-57 真机确认；`col`/`colgroup` border 来源、百分比 cell/后代内容和跨行 baseline 仍未覆盖。正式 Positron 构盒走 `pcore_select.c` + `pcore_box.c`，并不调用 NetSurf `box_construct.c`；此前 HTML `style=` 缺失的直接原因是 `pcore_style_subtree` 向 `css_select_style` 固定传入空 inline sheet，而不是 `nsoption_bool(author_level_css)`。next75/TEST58 已确认 NetSurf 式 inline stylesheet能通过 cascade、继承、`!important`、错误恢复、后代 class 选择及正式布局/重绘。next81 已将全零 `nsoption` shim 改为具名专家默认：当前实际读取的 `font_min_size=85`、`core_select_menu=false`、`remove_backgrounds=false` 对齐 NetSurf 3.11，JavaScript 默认继续显式关闭，未审计名称会直接编译失败；TEST56/58-61 已由设备确认无异常。列表 marker 的 47 种上游 counter formatter、缓存 `list-style-image`、失败回退和 inside 首行流已由 next61-63/TEST50-52 验收；float 邻接 marker 与自定义 `@counter-style` 仍未完成。表单与最小事件纵切已推进到 next151 设备基线，原生 EDIT `beforeinput` 已通过 TEST117 设备验收；当前仍缺高级 validity、完整 MouseEvent/KeyboardEvent/FocusEvent/InputEvent 字段、完整 HTML activation/default-action 细节和完整 JS 绑定。自动桥也不等于真实触屏、多选控件/文件选择器视觉或公网上传验收。字体 fallback 的当前范围只包括符号和单色 emoji，不计划扩展普通语言/多语种字体。external classic script 的 DOM 顺序执行与异步取回已由 next145 设备验收；next146-150 的页面 context、click、表单、键盘和 focusin/focusout 也已分别门禁，仍未实现 `async/defer/module`、IME/composition、完整事件处理器、`background-size` 与多层背景。UI 提交已在 parse/script/style/image-discovery/layout 调用之间让出 WM 消息循环，单个不可重入调用仍可能短暂卡顿。复杂 SVG text、float 和其余 forms/widgets 仍不完整；浏览器脚本仍仅在显式 `javascript=1` 时运行并提供最小 DOM/event bridge。路线采用“存在性优先”：继续评估 WM SELECT 键盘、基础 Grid 或背景尺寸。首屏 SVG 性能、抗锯齿和高级视觉边角后置。

独立脚本边界补充：`positron_script.dll` 已提供可供其他 WM 程序调用的 Duktape 2.7.0 UTF-8 求值服务，TEST80-99 已在 next134 设备日志通过；模块/provider、global/JSON、native callback 与 structured setter 均保持独立 ABI，JSON 结果超过 DLL 的 255 字节有效载荷会显式失败而不截断。next145 保持浏览器 JavaScript 默认关闭的策略，只在显式开启时由宿主把已缓存的 external body 与 inline body按 DOM 顺序送入同一初始 context；它不提供 external async/defer/module、fetch/network、事件回调或完整 DOM binding。

状态更正：动态状态伪类截至 next109 已有 `:focus/:active/:checked/:enabled/:disabled`，next110 又补上通用 Event 的传播/取消与宿主 click default-action 门，next111 补齐了 basic relative/absolute positioning，next113/TEST76 又接通了由宿主命中状态驱动的 `:hover`。仍未完成的是 `:visited/:target/:indeterminate`、专用事件数据与完整 HTML/JS 事件语义，以及无 CSS 尺寸空 text input 的默认 intrinsic size。

布局状态更正：basic relative/absolute 已由 next111/TEST75 通过；float、sticky、复杂 containing-block 组合、Grid 轨道和 `background-size` 仍是后续缺口。

next59 的 TEST49 已确认四个箭头不再 tofu、marker 和五个单色 emoji 均可见，视觉比 next58 稍有改善。next60 首次 TEST50 显示 `found=4`，核查确认不是当前源码逻辑失败，而是 staging 在最后一次 Debug 增量编译后又修改了 `pcore_select.c`，最终错误地组合了新 `test_host.exe` 与旧 `positron_core.dll`。next61 重新增量编译后 TEST50 已通过；`stage.bat` 现会先执行同配置增量 Build，失败时不再复制任何产物。next62 的 TEST51 与 next63 的 TEST52 均已由横竖屏截图确认，inside 文本/图片 marker、悬挂换行、block-first、空条目和嵌套布局符合预期。

next58/59 的随包单色 symbol/emoji fallback 是宿主基础字体，不等于网页字体：`@font-face` 下载、复杂 emoji ZWJ/variation shaping、彩色字体仍未实现。`ANTIALIASED_QUALITY` 是向 WM GDI 提出的灰度抗锯齿请求，最终效果仍由设备/OEM 字体光栅器决定。

next66 的 TEST55 首次真机运行读到 `FFFFFF/00C300/C6C300`：隐藏格为白、强制 show 格为绿、filled 格为青，功能分类正确；失败是 WM compatible bitmap 的 3-6 色阶量化与过严精确 RGB 断言共同造成的假阴性。next67 只将 TEST55 改为逐通道紧容差，core layout/redraw 不动。TEST13 Further Reading 新出现的圆点来自 IANA 页面真实列表项与已验收的 marker 支持，不是表格对齐回归。

next67 的 TEST55 已通过自动断言，设备截图也确认 top/middle/bottom、大小字体共基线、rowspan 底对齐及白/绿/青 empty-cells 正确。截图另外暴露测试页的四组固定高度刚好超出 WM 客户区十几像素，产生几乎填满轨道的纵向滚动条。next68 已压缩 TEST55 的行高/间距并显式设定标题行高，没有用滚动条掩盖可见内容。

next68 已由设备验收：TEST55 在竖屏客户区内完整显示且不再产生多余纵向滚动条；TEST56 的 105px 三行表与 70px 两行表按预期等比分配行高，top/middle/bottom 和跨行单元格底对齐正确。同期 TEST13 长页面滚动与 IANA 页脚布局保持正常。

next69 首次百分比 row 第二遍得到错误的 `20/30/30`。随后在多个共享目录包之间切换时出现的 TEST56 异常，最终由失败文本版本不符及 next72 同包 TEST56 通过证明为 WM/CE 全局 DLL 复用导致的 EXE/DLL 混搭。next72 的 TEST57 `styles=0:0` 暴露 inline `style=` 未参与正式选择；next73 将夹具改为外部 author stylesheet 后，用户确认 TEST55/56/57 通过，第一张表约为 20/40/20，超约束表为 25/25。后续架构复核确认正式路径没有调用受 `author_level_css` 控制的 NetSurf `box_construct.c`，真正缺口是 `pcore_style_subtree` 固定传 `NULL` inline sheet。

next74 首次接入 inline sheet 后，设备在 TEST56 报告 `va=0/2/3/3`：三行与两行高度仍正确，但 `.distributed .top` 的通配祖先 class 复合选择丢失。TEST56 原夹具和断言未改；next75 修复选择器回调对通配 qname `*` 的祖先/父节点匹配，并在 TEST58 增加独立 `.scope .probe` 断言。2026-07-24 设备确认 TEST56 与 TEST58 均通过；TEST58 可见页的 cascade 文本和 25/50/auto 三行布局符合预期。随后 TEST13 起始页和其余回归正常，但 IANA `/domains/reserved` 等宽表格子页仍会把主内容推到负 x。next77 只允许“横向、可收缩 flex item 且后代含 Grid fallback 或 `overflow-x:auto/scroll`”越过隐式 min-content 钳制，设备确认 TEST59 与同批回归通过、竖屏子页边距恢复；然而同页旋转为横屏后，首个英文表头 `Domain` 内容左移约 18px。next78 尝试递归 `scrollbar_set(...,0)` 后把异常扩大到全部表格单元格，并令 TEST56 失败、触发系统级 `test_host.exe` 异常，因此已经完整撤回。next79 已恢复 next77 机器码，设备确认 TEST56/59 均通过，真实页也准确回到“仅横屏首个 `Domain` 异常”的原始状态。next80 修复了 libcss 节点数据被过早销毁而留下父 bloom 悬空指针的问题；新增 TEST60 在同一 DOM 纵横屏重选时自动检查首表头的 18px/10px inset 与粗体宽度。2026-07-25 设备确认 TEST56/58/59/60 全部通过，真实 TEST13 `/domains/reserved` 的首个 `Domain` 在横竖屏均恢复正常 padding、字重和基线。该问题与非拉丁字体覆盖无关，普通语言/多语种字体明确不在当前开发范围。

next85 已建立只读 checkbox/radio 的静态 forms 基线。next86（提交 `210611d`）又在不改变导航控制流的前提下加入宿主侧阶段遥测；设备 TEST13 实测 total/network/max-UI=6435/5503/673ms，parse/style/images/layout/paint=11/182/6/673/36ms，说明总时长主要消耗在网络，而最长连续 UI 停顿集中在 `PCore_LayoutDocument`。next87（提交 `3b2446c`）进一步把该调用拆成 box construction、首轮 layout、overflow settling/可选二次 layout 与 finalize 计时；IANA 起始页得到 `580=515+65+0ms`（无二次布局），进入 Reserved 子页后最后一次导航得到 `662=495+124+43ms`（发生二次布局）。next88 的设备数据将两页构盒进一步定位到单张图片创建：tree/image 分别为 `523/518ms` 与 `481/474ms`。next89（提交 `ef0cc06`）用既有 SVG API 优先处理 XML-like 字节，并在同一 document 内复用 retained handle。next90 的 image ABI 1.5 与独立 core 统计在 next91 自动设备日志中确认创建耗时几乎全在 `svgtiny_parse`。next92 按 NetSurf 的“缓存条目与使用者分离”模型，让同时存活且 URL、长度、双哈希一致的文档共享 SVG；引用归零立即释放，不形成常驻缓存。设备日志确认 Reserved 页 `image reuse=1`、`svg creates=0`，图片阶段由前页的 523ms 降至 2ms，新增 TEST63 也通过释放首文档后的像素绘制检查。

next93 将静态 forms 基线推进到第一段真实交互：宿主先于链接命中消费 checkbox/radio，core 按 NetSurf 上游语义同步 `selected` 与 libdom checked 状态；同一 form owner、同名 radio 互斥，不同组和不同表单隔离，disabled 控件不改变。TEST64 还要求局部 dirty rect 有效，并在 240×320 到 320×240 重排后逐项复核状态。设备无人值守日志确认 TEST13/20/27/43/44/56/58-64 全部 PASS。

next94 把单行 text/password 输入接到同一表单状态层：core 生成 NetSurf `GADGET_TEXTBOX/PASSWORD` 盒并提供 UTF-8 枚举/写值 ABI，`test_host` 在其 border-box 上放置 WM 原生 `EDIT` 子窗口，滚动时移动、旋转时重建并保留焦点，换页前先销毁旧控件。TEST65 自动验证 maxlength、read-only、disabled、非法 UTF-8、DOM 重排保持，并用真实 `EN_CHANGE` 探针检查宿主消息桥。设备无人值守日志确认 TEST13/20/27/43/44/56/58-65 全部 PASS，next94 已提升为设备基线。

next97 沿用同一状态桥与原生控件生命周期，把 `<textarea>` 接为 `GADGET_TEXTAREA` 和 WM 多行 `EDIT`。core 将 WM 的 CRLF/CR 统一为 DOM LF，宿主启用多行换行、纵向滚动与回车输入；TEST66 覆盖初值、readonly/disabled、非法 UTF-8、CRLF/LF 归一化、真实 `EN_CHANGE` 和纵横屏重排保持。next95/96 证明 WM 多行 EDIT 的程序化 `SetWindowTextW` 不能作为可靠通知探针；next97 改由 `EM_REPLACESEL` 模拟编辑，并在真实通知回调中验收。设备无人值守日志确认 TEST13/20/27/43/44/56/58-66 全部 PASS，next97 已提升为设备基线。

next98 按 NetSurf `box_select`/`form_option` 模型构造 `<select>`，option 文本折叠空白、value/selected/disabled 与 libdom 同步，最宽 option 参与正式 layout。新增 `PCore_SelectInfo/SelectOptionInfo/SelectSetOptionSelected` 独立 ABI，不改变 next94/97 的文本结构；WM 宿主以原生 `COMBOBOX` 覆盖单选 border-box，并复用滚动、换页销毁和旋转重建。2026-07-30 设备自动日志确认 TEST13/20/27/43/44/56/58-67 全部 PASS；TEST67 检查错误的多 selected 归一化、disabled policy、multiple core 状态、DOM 重排持久化与原生下拉桥。该 next98 基线当时尚不包含多选 WM 列表和提交；下述 next99 在此基础上继续推进。

next99 把按钮和基础提交接入正式路径：`input[type=submit/reset/button]` 与 `<button>` 使用 NetSurf gadget、CSS layout/redraw 和文档坐标命中；`PCore_FormSubmissionAt` 按上游 `form.c` successful-controls 规则生成 UTF-8 `application/x-www-form-urlencoded` 数据。WM 宿主把 GET 数据替换到 action query，把 URL-encoded POST 交给既有 `PHttp_PostEx`，旧页在 worker 完成前保持可见。TEST68 成批覆盖 disabled/无名/未选控件过滤、multiple select 重复字段、仅提交被点击按钮、GET/POST 请求所有权、超长目标拒绝及 multipart 明确拒绝。2026-07-30 设备自动日志确认 TEST13/20/27/43/44/56/58-68 全部 PASS，next99 已提升为设备基线。multipart/file upload、Enter 隐式提交、reset 恢复、label 激活和完整事件系统尚未包含。

next100 首次设备日志确认 TEST13 深链及 TEST20/27/43/44/56/58-68 全部通过，但 TEST69 暴露 vendored libdom 0.4.2 的 textarea 默认值缓存笔误：首次 `get_value()` 错把 `default_value_set` 而非 `value_set` 置真，使后续 reset 得到空默认值。next101 对该上游源做一行语义修复，并补齐普通表单默认动作：reset 按 `defaultValue/defaultChecked/defaultSelected` 恢复整个所属 form，随后由正式 layout 重建 NetSurf gadget 与 WM 控件；单行 text/password 的 WM `EDIT` 收到 Enter 时按 NetSurf 规则选择第一个可用 submit，或在没有 submit 按钮时提交其余 successful controls；显式 `label[for]` 与包裹式 label 可激活 checkbox/radio/button 或把焦点交给原生 text/select 控件。2026-07-30 设备自动日志确认 TEST69 与 TEST13/20/27/43/44/56/58-68 全部 PASS，next101 已提升为设备基线。multipart/file upload、WM 多选列表、约束验证和完整 DOM 事件取消/传播仍未实现。

next102 首次设备门禁中 TEST13/20/27/43/44/56/58-69 全部通过，TEST70 唯一失败于 file reset：libdom 会把无初始 `value` 属性的 file 控件第一次运行时 `set_value()` 误记为 `defaultValue`。next103 在真实 reset 路径强制清空 file 显示值和原始路径，不改断言；设备日志随后确认 TEST13/20/27/43/44/56/58-70 全部 PASS，next103 已提升为基线。其余 multipart/file 设计保持不变：Core 沿用 NetSurf `GADGET_FILE` 及“显示文件名/原始路径分离”模型，以不泄露 `fetch_multipart_data` 的快照 ABI 暴露普通项与文件项；WM 宿主使用 Pocket PC `GetOpenFileNameEx`，读取文件后向既有 `PHttp_PostEx` 传递显式长度的二进制 body。当前请求体会整体缓存在宿主内存，文件 Content-Type 固定为 `application/octet-stream`，尚无流式上传、上传方向进度、MIME 推断、`multiple` 文件选择或公网 POST 验收。

next104 在既有 `PCore_Select*` multiple 状态 ABI 上接入 WM 原生 `LISTBOX`：使用适合触屏逐项切换的 `LBS_MULTIPLESEL`，高度严格跟随 NetSurf border-box，并复用单选已有的滚动定位、导航销毁和旋转重建生命周期。TEST71 自动切换多个 option，拒绝并回滚 disabled option，确认 disabled select、单选 `COMBOBOX` 回归、重建后的原生选中状态、GET 重复字段和 reset 默认恢复。设备日志确认 TEST13 三段深链及 TEST20/27/43/44/56/58-71 全部 PASS，next104 已提升为基线。真实手指操作和控件观感留到后续累计人工检查。

next105 首次把表单提交前约束验证接入 Core，但 TEST72 在 reset 后仅恢复 6 个 invalid，暴露 libdom 0.4.2 会把无初始 `value` 属性的 text/password 第一次运行时写值误记为 `defaultValue`。next106 在 `PCore_TextInputSetValue` 写入前冻结解析时默认值，不改断言；设备日志随后确认 TEST72 的 required text/password/textarea/file、checkbox、同名 radio、single/multiple select、首个无效控件几何、提交/Enter 阻止、`novalidate/formnovalidate`、multipart 和 reset 全部通过，并且 TEST13/20/27/43/44/56/58-71 无回归。WM 宿主会滚动并聚焦首个无效原生控件，目前只以系统提示音反馈，不绘制验证气泡。

---

### next125 独立脚本宿主回调桥

next125 将 `PScript_RegisterGlobalJsonFunction`、`PScript_UnregisterGlobalJsonFunction` 和 `PScript_GetNativeFunctionCount` 加入独立 DLL ABI。每个回调同步接收 compact JSON 参数数组并返回一个 JSON 值；固定最多 16 个全局名字，回调结果最多 255 字节有效载荷，回调不得重入或销毁上下文，也不能被异步持有。TEST90-94 分别覆盖参数/返回值、结构化 JSON、失败恢复、替换/注销和槽位上限；它们不初始化 `positron_core`，不接入 TEST13。

next126 将 `PScript_SetGlobalJson` 加入 ABI 1.6。宿主可以把对象、数组、字符串、数字、布尔值或 `null` 原子注入 persistent global；输入沿用 64 KiB 源码上限，解析失败或超限不会替换旧值。TEST95-99 覆盖结构化读取、跨调用 mutation、错误恢复、输入上限和类型替换；它们仍不初始化 `positron_core`，不接入 TEST13。

next127 的设备日志在 `screen=240x320 dpi=96` 下确认 TEST95-96 通过；TEST97 的
失败来自断言把 Duktape 的 `SyntaxError: invalid json ...` 当作不含 JSON 诊断。
next128 只改测试断言，要求错误码为 `PSCRIPT_ERROR_JSON` 且诊断非空，不依赖引擎
错误文本的大小写。

next129 将 TEST20 的图片盒从临时 96 DPI CSS 视口改为实际设备视口：读取 WM
设备 DPI，经 `PCore_SetDeviceViewport` 换算 CSS media/vw/vh，并按
`MulDiv(48, dpi, 96)` 验证 48 CSS px 的物理尺寸。

next130 将 TEST27 的 SVG 测试同样切到设备视口：`120x60` CSS SVG 盒按实际 DPI
换算，style/layout 前安装设备 DPI，离屏红绿采样点也按物理坐标换算。

next131 将 TEST56 的离线表格几何段显式设为 96 DPI CSS 视口，避免把 105/70 CSS px
误当作设备像素；同测试的可见窗口仍走 `PCore_SetDeviceViewport`。

## 工具链

- **编译器**：MSVC 9.0（VS2008 SP1，C89-only，无 C99/C++11）
- **SDK**：Windows Mobile 6 Professional SDK (ARMV4I)
- **目标 Subsystem**：`windowsce,5.02`
- **链接库**：`ws2.lib`（WinCE 版 Winsock2，非桌面 `ws2_32.lib`）
- **加密库**：mbedTLS **2.16.12**（历史 2.16 LTS 系列的 WM6/MSVC9 兼容固定版本，不表示当前仍受上游维护；尝试过的 2.28.10 含 MSVC9 无法直接编译的 C99 声明）。当前 verified 路径显式使用 `MBEDTLS_SSL_VERIFY_REQUIRED` 并调用 `mbedtls_ssl_set_hostname()`；迁移到仍受维护版本仍是安全性中期目标。
- **JSON 库**：cJSON **1.7.18**（C89 兼容）

---

## 仓库结构

```
Positron/
  Positron.sln                  VS2008 solution（含 NetSurf 静态库与产品 DLL）
  README.md                     本文件
  PHASE1.md                     Phase 1 经验/坑记录
  PHASE2.md                     Phase 2 设计 + 已知风险点

  positron_tls/                 TLS 1.2 DLL
    positron_tls.h              公开 API
    positron_tls.c              实现（DllMain + Winsock2 BIO + 熵源 + API）
    mbedtls_config.h            裁剪后的 mbedTLS 配置
    ca_bundle.h                 嵌入的完整 Mozilla 根集（~140 根，脚本生成）
    gen_ca_bundle.py            从 curl cacert.pem 提取根证书的生成脚本
    positron_tls.vcproj
    mbedtls/                    完整 vendored mbedTLS 2.16.12 源与许可证

  positron_json/                cJSON 包装 DLL
    positron_json.h / .c / .vcproj
    cjson/                      cJSON 1.7.18 源（已入 git）

  positron_http/                HTTP/1.1 客户端 DLL
    positron_http.h / .c / .vcproj

  positron_core/                NetSurf 引擎共享 DLL 边界
    positron_core.h / .c          PCore_* API（Parse/Style/Layout/Paint/LinkAt）
    pcore_box.c                   DOM+computed style → NetSurf box tree；正式 layout/paint/link path
    pcore_plot_gdi.c              NetSurf plotter + GDI 字体量度表
    pcore_talloc.c                精简 talloc 垫片
    nsshim/                       NetSurf layout/redraw 依赖的精简 shim 头
    positron_core.vcproj          DLL，静态链入 NetSurf 库与移植的 layout/redraw 源

  assets/fonts/                 WM GDI 随包静态 symbols/mono emoji fallback 字体

  positron_expat/               Expat 2.8.2 静态库及 WM/VS2008 适配
  positron_libsvgtiny/          NetSurf libsvgtiny 静态库
  positron_image/               可供任意 WM 程序调用的图片 DLL（WM 位图 + SVG retained C ABI）
  positron_script/              可供任意 WM 程序调用的 JavaScript DLL（Duktape 2.7.0 C ABI）

  samples/positron_image_demo/  仅依赖 positron_image.dll 的独立 WM C 示例

  test_host/                    端到端测试 EXE（分通信/引擎/GDI渲染/Browse 组）
    main.c
    test_host.vcproj

  compat/                       VS2008 + WinCE 缺的 C99 shims
    stdint.h
    inttypes.h

  scripts/
    stage.bat                   增量构建并把 7 个二进制拷到 C:\WMShare\
    stage_image_demo.bat        只打包图片 DLL 与独立示例

  .agents/                      Codex 接手交接、调试纪律、路线图
```

---

## 编译

### 一次性准备

1. 安装 **VS2008 SP1** + **Windows Mobile 6 Professional SDK** + **WM6 Pro Emulator**。
2. Clone 本仓库。mbedTLS、cJSON、NetSurf、Expat、libjpeg-turbo、NanoSVG 和字体源均已固定版本并随仓库提供，不需要额外下载源码。
3. 可先运行 `python scripts\audit_repo.py`，确认 15 个 VS2008 工程引用的源码和关键许可证都存在且已被 Git 跟踪。

VS2008、WM6 SDK、模拟器和设备镜像是外部专有工具链，不能随本仓库再分发。第三方版本和许可证清单见 [THIRD_PARTY.md](THIRD_PARTY.md)。

### 构建

命令行构建（agent 和日常开发的首选入口）：

```cmd
scripts\build.bat                 :: 默认 Debug 增量 Build
scripts\build.bat Debug rebuild   :: Debug 全量 Rebuild
scripts\build.bat Debug build     :: Debug 增量 Build
scripts\build.bat Release rebuild :: Release 全量 Rebuild
scripts\build.bat Debug clean     :: 清理 Debug
```

脚本调用 VS2008 的 `Common7\IDE\devenv.com`，按解决方案中的工程依赖构建
`Debug|Windows Mobile 6 Professional SDK (ARMV4I)`，并将完整输出写入
`vs2008-build.log`。ARM 编译器本体位于 `VC\ce\bin\x86_arm\cl.exe`，但不应绕过
`.sln` 直接逐文件调用它，否则必须手工复制 SDK include/lib、宏、链接参数和工程顺序。

也可以打开 `Positron.sln`，确认顶部工具栏：
- Solution Configuration = `Debug`
- Solution Platform = `Windows Mobile 6 Professional SDK (ARMV4I)`

**生成 → 生成解决方案** (F7)。产物：

```
positron_tls/bin/Debug/positron_tls.dll
positron_json/bin/Debug/positron_json.dll
positron_http/bin/Debug/positron_http.dll
positron_script/bin/Debug/positron_script.dll
positron_core/bin/Debug/positron_core.dll
test_host/bin/Debug/test_host.exe
```

---

## 部署到模拟器

> ⚠ **VS2008 内置 Smart Device 部署对当前工程不可用**——无论 RemoteDirectory 怎么配都被部署引擎覆盖。详见 [PHASE1.md](PHASE1.md)。改用模拟器共享文件夹。

### 一次性配置

1. 启动 WM6 Pro Emulator（VS → 工具 → Device Emulator Manager）
2. Cradle 它（连 WMDC）
3. 模拟器 → File → Configure → **Shared folder** 选 `C:\WMShare\`
4. 主机 Windows Mobile Device Center → Mobile Device Settings → Connection Settings：
   - 勾 "Allow data connections on device when connected to PC"
   - "This computer is connected to" 选 **The Internet**
   - Uncradle / Cradle 重挂一次

### 每次构建后

```cmd
scripts\stage.bat         :: 默认先增量构建 Debug，再打包
scripts\stage.bat Release :: 或 Release
scripts\stage.bat Debug C:\WMShare\Positron-next :: 旧进程锁文件时隔离 staging
```

把 7 个二进制、测试配置及 `fonts` 子目录拷到 `C:\WMShare\`。

模拟器内 Start → File Explorer → **Storage Card** → 双击 `test_host.exe`。

### 测试入口

`test_host.exe` 启动后先选择测试组。部署时必须保留 EXE/DLL 同级的 `fonts` 子目录：

快速复测可在 `test_host.exe` 同目录放置 `test_host.ini`：

```ini
# 支持逗号、空格、范围，以及特殊编号 7b
auto=1
tests=13,20,27,43,44,56,58-77,80-122
```

`auto=1` 启用无人值守 testbench：不显示 Yes/No/OK，按编号升序运行，所有原始 INFO/ERROR 与 TEST13 每次导航遥测写入 EXE 同目录的 `test_host.log`（每次启动覆盖）。可视测试窗口至少完成一次 `WM_PAINT` 后正常关闭；TEST13 自动经过 example.com、IANA Example Domains 和 IANA Reserved Domains。自动模式验证已有断言、资源计数和首帧可绘制性，**不等价于人工检查字体、抗锯齿和版式观感**；最近一次 next116 已证明“自动 OK”不能取代 Browse 人工门禁。设为 `auto=0` 时仍先提示是否只运行配置项；选 No 完整保留原 All/四组流程。文件缺失时直接走旧流程，文件存在但无效时提示并忽略。TEST23 与 TEST78/79 不可选。`scripts\stage.bat` 会先调用同配置的 VS2008 增量 Build，再复制配置及三份静态 symbol/emoji fallback 字体；构建失败不会留下混合版本包。

测试交付默认按能力批次进行：先积累多项相关实现、自动像素/资源/安全断言和直绘/正式链两层回归，再请求一次设备验收。只有真实编译错误、高风险回归定位或设备特有故障才临时拆成单项包，避免每个微小改动都要求人工截图。

- **Communication**：TEST 1-5，TLS / HTTP / JSON，需要网络。
- **Standalone script**：TEST 80-99，独立 positron_script.dll 的 ABI、持久上下文、错误恢复、DLL 内存遥测、执行超时/源码长度边界、硬内存配额、CommonJS 风格模块生命周期、宿主源码 provider、global primitive 注入、结构化 JSON setter、JSON 函数调用和同步 native JSON 回调；不初始化 positron_core，不连接 DOM/window/network。TEST80-99 已由 next134 的 `screen=240x320 dpi=96` 设备日志验收。
- **Engine**：TEST 6-11、15、16、18、21、22、24、25、38、40-45、59-61、74-76，HTML/CSS/DOM/select/style/layout/box tree/image resource cache、responsive media viewport、row-reverse flex padding、cached CSS restyle、SVG parse、受约束的 `:root` token、现代 CSS 值、grid/overflow min-content 隔离、overflow scrollbar、分阶段导航资源事务、主文档失败回滚、CSS import tree、libcss 节点缓存纵横屏重选、具名 NetSurf option 默认、DOM Event 传播/取消、基础定位与动态 `:hover`，离线。TEST40-45、59、60、74-76 已真机通过；next78 已撤回。TEST23、TEST79 的 float 候选均因真实 Browse/设备门禁回归撤回。
- **GDI Render**：TEST 12、14、17、19、20、26-37、39、46-58、62-73，覆盖 WM Imaging、SVG path/cache/fallback/fill-rule、CSS background-image、原生 GDI text、线性/径向渐变、继承/透明 stop、同文档及重叠文档缓存复用、IANA token 间距、table span/匿名归一化/collapsed border/cell alignment/height distribution、列表 marker/counter/image/inside flow、HTML inline author CSS、普通表单、multipart/file、WM multiple select、required 验证与动态表单伪类；TEST73 已由 next109 设备门禁确认。动态 `:hover` 的自动断言属于 Engine TEST76。
- **Browse**：TEST 13，真实页面抓取 + 渲染，需要网络；HTTPS 走 mbedTLS verified，明文 HTTP 走 WinInet。

当前关键 smoke test：

- TEST 17：内置 NetSurf real layout + redraw 页面。预期看到深红 H1 及红色下边框、带边框的三色 flex 横排、带可见单元格边框的 2×2 table。
- TEST 13：交互模式从 start page 点击链接；自动模式依次直达 example.com、IANA Example Domains 和 Reserved Domains。两者走相同的 fetch → parse → style/resources → layout → paint 导航事务。
- TEST 64：自动点击 checkbox/disabled/radio，验证同组互斥与跨表单隔离，并在纵横屏重排后复核 checked 状态。
- TEST 65：枚举 text/password border-box，验证原生 WM `EDIT` 的 `EN_CHANGE`、maxlength/read-only/disabled、UTF-8 DOM 同步和重排持久性。
- TEST 66：枚举 textarea border-box，验证原生 WM 多行 `EDIT`、CRLF/LF DOM 归一化、readonly/disabled 和重排持久性。
- TEST 71：以 WM 原生 multiple `LISTBOX` 验证逐项增删、disabled 回滚、旋转/重建状态保持、GET 重复字段、reset 与单选回归。
- TEST 72：验证 required text/password/textarea/file、checkbox、radio group、single/multiple select，提交/Enter 阻止、no-validate 旁路、首个无效控件定位、multipart 和 reset。
- TEST 73：验证 live checked/enabled/disabled、宿主 focus/active、cache-only 重样式、纵横屏保持与 reset；事件传播由 TEST74 单独验证。
- TEST 74：验证通用 DOM Event 的 capture/target/bubble、非冒泡、cancelable/default-action、stop propagation、listener remove 与坐标命中派发；不代表专用事件数据或 JavaScript 已启用。
- TEST 76：验证命中最近 DOM 元素后 `:hover` 样式重选为红色，清除 hover 后恢复蓝色；WM6 宿主离开窗口使用定时器轮询，不代表完整 MouseEvent。
- TEST 119：在显式 `javascript=1` 页面中验证 EDIT/SELECT 的 `keypress` 元数据、target/bubble
  传播、可取消状态和真实 `WM_CHAR` 入口；设备自动 OK 仍需配合人工视觉检查。
- TEST 120/121：在显式 `javascript=1` 页面中验证 `WM_SYSKEY/WM_SYSCHAR` 的 system-key
  元数据，以及单个 BMP `WM_CHAR` 的 UTF-8 key/data 传递；next156 已设备通过。
- TEST 122：在显式 `javascript=1` 页面中验证成对 UTF-16 代理项合并为一次 Unicode 标量
  `keypress` 和完整 `beforeinput.data`；next158 已定位 Duktape 字符串表示，next159 待验收。

> ⚠ **跑 TEST 5 之前先把模拟器系统时钟设到当前**（开始 → 设置 → 系统 → Clock & Alarms）。WM6 Emulator 默认是 2005-2007 年某个时间，会让所有现役证书都看着像"尚未生效"。

---

## 已知限制 / 注意事项

- **熵源**：默认 `CryptGenRandom`（Phase 3 起）；CSP 不可用时自动退回 QPC+GetTickCount+tid/pid jitter，CTR-DRBG 兜底。
- **HTTP 限制**：单连接 `Connection: close`、无 keep-alive、无 gzip 解码、响应体 cap 1 MB；GET 已有有限 3xx follow，明文 `http://` 经 WinInet。
- **导航卡顿**：主文档、外链 CSS、CSS `@import`、`<img>` 与已计算 CSS 背景资源的 GET 已组成分阶段 worker 事务，旧页在网络等待时可滚动。`PHttp_GetEx/PostEx` 在请求线程报告已解码正文大小和可选 `Content-Length`；父窗口进度条对已知总长显示当前响应的真实百分比，对 chunked/无长度响应保持活动动画，TEST3/13 已真机确认。每个资源响应会开始自己的进度序列，所以这不是整页资源总字节百分比。HTML parse、style、图片 cache copy、layout 仍严格留在 UI 线程，但现通过一次性 WM timer 分成四个提交阶段，让触摸、旋转、绘制和进度控件可在阶段之间运行；单个 NetSurf 调用仍可能短暂卡顿。next86-91 的逐层遥测把主要 UI 热点定位到 `svgtiny_parse`。next92 在旧页与新页同时存活的事务窗口内复用内容一致的 SVG，Reserved 页确认 `image reuse=1`、`svg creates=0`，图片阶段为 2ms；首个页面的冷解析仍可能超过 500ms。TEST13 显示导航完成快照，后续旋转布局不会回写该框。TEST44 已确认主文档失败保留旧页与事务收尾，TEST45 已确认嵌套导入与失败回退。网页字体/脚本资源仍待后续。`test_host` 暂存最多 64 个 URL、合计 2 MiB 原始字节；这是可替换的宿主预算，不是 `positron_core` ABI 限制。
- **渲染限制**：TEST25-37 与 TEST13 fixture 已确认 SVG parse/draw/cache/fallback/fill-rule/网络链、CSS 单背景图、基础 SVG text、线性/径向渐变、继承/透明 stop 及缓存复用；复杂 SVG text、径向焦点、spread method、background-size 和多层背景仍未完成。TEST38-39 已确认受约束的 `:root` token，TEST40 已确认数值型 OKLCH/可求值 calc；两者都不代表完整 CSS Variables/Color/Values。TEST41 只验证 grid 单列降级不会把反向 flex 主内容推至负坐标；TEST42 验证的是 overflow scrollbar，不是完整 Grid。TEST46/47 与 TEST53-57 覆盖一批已验收的表格构盒、边框和行高子例；百分比 cell/后代或 column 模型仍未完成。TEST48-52 覆盖 47 种上游 counter formatter、图片 marker 及 inside 流，但仍不代表完整 CSS Lists/Counter Styles。TEST23 浮动实现已因 Browse 回归撤回。完整范围见 [.agents/KNOWN_LIMITATIONS.md](.agents/KNOWN_LIMITATIONS.md)。
- **WM6 X 按钮 = 最小化不是关闭**。每次启动 test_host 前确认任务管理器没有遗留实例，否则 stage.bat 替换 exe 时会产生 image 不一致。
- **WMDC 桥会静默断**：host 待机 / 模拟器长跑后偶尔失联，表现是 `PTls_Connect` 拿到 `-0x004C [BIO: recv WSA=...]`。修法：重启 WMDC（任务栏 → 退出 → 重启）。**联网测试前先在 IE Mobile 打开 baidu 验证一遍**。
- **模拟器时钟**：跑 verified TLS 前必须校准（见上）。证书 notBefore/notAfter 都按 UTC 比对当前时间。

---

## License

Positron 自有代码使用 [MIT License](LICENSE)。Vendored 源码保留各自许可证，不能被根许可证覆盖；尤其 `netsurf-all-3.11/netsurf/` 的浏览器源码是 GPLv2。完整组件、版本、路径和通知要求见 [THIRD_PARTY.md](THIRD_PARTY.md)。

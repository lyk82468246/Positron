# Positron Roadmap

更新时间：2026-08-07
基线：正式 Browse 路径走 NetSurf `layout_document` + `html_redraw`；TEST13 深层导航保持 next37 冻结语义。图片/SVG、字体 fallback、列表 marker/counter/inside flow、table 常见路径、表单、最小 DOM Event 纵切、基础 relative/absolute positioning、动态 `:hover` 与脚本资源发现/缓存 ABI 已推进到设备自动化基线。next118-126 已把独立 `positron_script.dll` 的 ABI、预算、模块、provider、global/JSON、native callback 与 structured setter 分批完成；next142 在 `screen=240x320 dpi=96` 日志中确认 TEST13/20/27/43/44/56/58-77/80-104 通过并记录 `TESTBENCH PASS`，next141 已在 `screen=320x320 dpi=128` 下通过 TEST13/20/27/43/44/56/58-74。next135 的表单长度约束、next136 的 TEST59 CSS 参考上下文修复、next137 的非整数 DPI 设备像素换算、next138 的 TEST60、next139 的 TEST63、next141 的 TEST62 和 next142 的 TEST75 均已有构建或设备结果。浏览器 JS 默认关闭，96 DPI 不是产品固定值。next115 与 next116 的 float 候选均已因 TEST79/TEST13 真实回归否决，next114 的 Browse 路径保持为浏览器回归基线。失败/暂挂方向总索引见 `FAILED_EXPERIMENTS.md`；正文按时间保留已完成工作的来龙去脉，末尾“建议执行顺序”才是当前优先级；详细边界见 `KNOWN_LIMITATIONS.md`。

**当前开发门禁（next142）**：next141 在 `screen=320x320 dpi=128` 下通过
TEST13/20/27/43/44/56/58-74，next142 修正 TEST75 后在 `screen=240x320 dpi=96`
默认配置下通过 TEST13/20/27/43/44/56/58-77/80-104，并记录 `TESTBENCH PASS`。
下一批应轮换另一组分辨率/DPI；不能把 96 DPI 当作产品固定值，并保留日志头部。

## 总原则

Positron 是给 WM6 打补丁，不是拆掉 WM6 重建。

- WM6 已经做得够用的部分，优先用系统能力：WinInet、GDI、WM Imaging API、coredll。
- WM6 做不到现代要求的部分，才自研/移植：现代 TLS、现代 HTML/CSS 渲染、后续 JS runtime。
- 页面还原度优先于“随便降级”：如果 GDI 没有 dashed pen，就手绘 dashed border；如果 WinInet 不能现代 TLS，就 mbedTLS 补上。
- 2026-07-26 起采用“存在性优先”：尚不存在的主能力、崩溃和数据错误优先于已存在能力的速度、抗锯齿、视觉微调和高级边角语义。性能工作只在阻塞可用性或有明确设备热点证据时插队。

## 短期规划

目标：先补齐“能完成基本网页任务”的缺失纵向能力，再改善已经可用部分的观感和性能。

当前新增功能优先级：

1. **表单交互纵切**：next93 至 next109 已依次完成 checkbox/radio、text/password、textarea、single/multiple select、button、提交/reset/Enter/label、multipart/file、首批 `required/valueMissing` 与动态表单伪类；next135 又加入 `minlength`/`maxlength`，但需随 next139 设备复测。`pattern`、类型/范围约束、custom validity 与 `invalid` 事件仍在后续扩展，不阻塞更大的“有无”缺口。
2. **事件基础**：next110/TEST74 已建立通用事件对象的目标链、捕获/目标/冒泡、取消、停止传播、listener 生命周期与宿主 click default-action 边界。下一步不是继续雕刻事件边角，而是在专用 Mouse/Keyboard/Focus/Input 数据与 JS 绑定之前先推进重大布局或脚本资源接口。
3. **重大布局“有无”**：next111/TEST75 已接入基础 relative/absolute positioning，next113/TEST76 又补齐 CSS `:hover` 的宿主状态桥；next115 与 next116 的 float 候选均因 TEST79/TEST13 真实回归撤回。Float 方向暂挂，下一次重大布局实验改评估基础 Grid 或背景尺寸/重复，并继续保留 TEST13 深链门禁。
4. **资源类型补齐**：脚本资源发现/下载/缓存接口已完成；next118 先把独立 JavaScript runtime DLL 做成其他 WM 程序可调用的最小产品面，再由后续批次评估浏览器消费；网页字体不扩展为普通语言字体工程。
5. **独立 JavaScript 能力**：`positron_script.dll` 的 ABI、持久求值、错误恢复、预算和资源计数已由 TEST80 设备验收；下一步再设计显式的浏览器 JS 开关与 DOM/native bridge，避免把未验证绑定直接接入 TEST13。

### 6f. next123：高 DPI 设备视口换算（待设备验收）

- NetSurf 的标准约定是：CSS media/vw/vh 使用 CSS 像素视口，`layout_document` 和 GDI 重绘使用设备像素。next122 的新模拟器日志首次暴露两者被宿主混用：TEST20 的 48 CSS px 图像盒成为 96 device px，自动化因此停在 TEST20；这不是 provider 回归。
- `PCore_SetDeviceViewport` 现在接收物理客户区和 DPI，换算 CSS 视口后交给样式选择，并让下一次 `PCore_LayoutDocument` 保留该 CSS 视口、使用原始物理布局尺寸。旧 `PCore_SetViewport` 的显式 CSS 像素语义保留，离线 ENGINE 几何测试不被隐式重解释。
- Browse 导航、WM_SIZE 旋转重排和 test_host 启动路径已统一使用设备视口入口；TEST20 断言改为检查 48 CSS px 按实际 DPI 得到的设备尺寸，并把 screen/DPI 写入 `test_host.log`。
- ARMV4I Debug/Release 构建、审计和 `C:\WMShare\Positron-next123` staging 已通过；必须在新分辨率模拟器运行默认自动配置，并人工复查 IANA 与 Example Domain 的初始页、滚动、链接和旋转。若该批次仍有视觉回归，优先回到 next121/next114 的 Browse 代码，而不是放宽断言。
- next127/128 的设备日志已分别在 `240x320 dpi=96` 与 `240x240 dpi=96` 完成 TEST13/20/27/43-99 的自动回归；next129 把 TEST20 的离线图片断言改回 `PCore_SetDeviceViewport` 与实际 DPI 物理尺寸换算，等待非 96 DPI 设备验收。
- next129 在 `480x640 dpi=192` 下已验证 TEST13/20 的动态换算；TEST27 发现 SVG 离线测试仍有固定 `120x60` 设备像素断言，next130 已改为实际 DPI 尺寸与采样坐标，等待设备复测。
- next130 在 `480x480 dpi=192` 下验证 TEST27/43/44 后，TEST56 暴露离线 CSS 几何段继承设备 DPI；next131 已隔离该段的 96 DPI CSS 契约，同时保留可见窗口的真实设备视口。

### 6h. next137：非整数 DPI 的通用设备像素换算（待设备验收）

- next136 在 `screen=320x320 dpi=128` 下的日志证明 TEST13 网络/导航路径完成，但 TEST20 的 48 CSS px 图片盒仍为 48 device px，动态期望为 64。该值不是断言错误，也不是把测试改回 96 DPI 能解决的测试隔离问题。
- 根因是 vendored `libcss/src/select/unit.c` 的 `css_unit_len2device_px` 先把每 CSS 单位的设备比例取整；在 128 DPI，`1 CSS px * 128 / 96 = 1.333` 被截成 1。next137 保留固定点分数比例，完成整段长度换算后再做最终取整，因而同时覆盖图片、边框、字体、表格等所有设备像素长度。
- 这批没有修改 TEST20/27 的动态断言，没有固定屏幕分辨率或 DPI；`scripts/test_c89ize.py` 四项回归通过，目标源 `c89ize.py` 报告 0 change，VS2008 ARMV4I 增量构建成功，仅保留既有 libcss `fpmath.h` 三条警告，staging 为 `C:\WMShare\Positron-next137`。
- 设备复测要先确认运行目录使用同一包的 `positron_core.dll`/依赖 DLL，再检查 TEST13 三段页面、TEST20/27 图像尺寸和 TEST59-77/100-104；随后轮换另一种分辨率/DPI 做人工 Browse 视觉检查。

### 6i. next138：隔离 TEST60 的 CSS 几何上下文（待设备验收）

- next137 的 `screen=320x320 dpi=128` 日志在 TEST60 记录 `x=24/19/19/24`、`y=13/13/7/7`，正好是显式 CSS padding 在 `128/96` 设备比例下的换算结果；两列首段文字宽度仍相等，因此不是 selector、restyle 或 node-data 生命周期回归。
- next138 在 TEST60 每个离线 style/layout pass 前显式使用 `PCore_SetViewport(width, height, 96)`，保留该测试的 CSS 像素契约；清理阶段调用真实设备视口恢复路径。产品 Browse、分辨率、设备 DPI 和 TEST60 断言均未被固定或放宽。
- `scripts/test_c89ize.py` 四项回归、`scripts/audit_repo.py`、VS2008 `Debug|Windows Mobile 6 Professional SDK (ARMV4I)` 增量构建与 staging 均通过，包为 `C:\WMShare\Positron-next138`；设备复测后再推进下一批“存在性优先”能力。

### 6j. next139：隔离 TEST63 的 CSS 几何上下文（待设备验收）

- next138 在 `screen=320x320 dpi=128` 下通过 TEST60，TEST63 随后报 `shared SVG did not survive first document release`；该测试的固定 `240x120` viewport 与 `120x60` image box 继承了设备-backed DPI，失败信息无法区分布局尺寸和 SVG 生命周期。
- next139 在 TEST63 前显式使用 `PCore_SetViewport(240,120,96)`，清理阶段恢复真实设备视口，并报告 post-release `layout/node/box` 值；共享 SVG 的 create/reuse/fetch/free 断言保持严格。
- C89 回归、仓库审计、VS2008 `Debug|Windows Mobile 6 Professional SDK (ARMV4I)` 增量构建与 staging 均通过，包为 `C:\WMShare\Positron-next139`；设备复测后再判断是否需要真正修改跨文档 SVG 生命周期。

### 6l. next140：固定 DPI 的 TEST62 探针尝试（已替代）

- next139 在 `screen=480x640 dpi=192` 下通过 TEST13/20/27/43/44/56/58/59/60/61；TEST62 的离线 checkbox/radio probe 返回 `36x36`，正好是 `192/96=2` 的设备换算，不是控件状态或绘制回归。
- next140 在四个静态 toggle probe 和 hidden-input 检查前显式使用 `PCore_SetViewport(64,48,96)`；可见 TEST62 页面在 probe 完成后调用真实 `test_host_set_device_viewport`。该方案让 192 DPI 的实际 `36x36` 伪装成 96 DPI 上下文，违反动态 DPI 原则，已废弃。

### 6m. next141：TEST62 控件几何按设备 DPI 等比换算（待设备验收）

- 保留 TEST62 探针的 `64x48 CSS px` 表面，调用 `PCore_SetViewport(64,48,实际设备 DPI)`；原本 96-DPI 的 `14..24px` 控件范围改为 `MulDiv(14/24,dpi,96)`，因此 `192 DPI` 下允许 `28..48` 物理像素，`36x36` 正常通过。
- 控件状态、绘制路径、隐藏 input 抑制和可见 TEST62 设备视口均未改变；这次只修正测试断言的单位换算。
- C89 回归、仓库审计、VS2008 `Debug|Windows Mobile 6 Professional SDK (ARMV4I)` 增量构建与 staging 已通过，设备复测后再继续 TEST63-77/100-104。

### 6n. next142：TEST75 定位几何按设备 DPI 等比换算（已设备验收）

- next141 在 `screen=320x320 dpi=128` 下通过 TEST13/20/27/43/44/56/58-74，TEST75 返回 `main=0,0 240x160`、`relative=13,36 40x27`、`absolute=80,40 33x20`、`inline=133,67 27x16`；这些正是 CSS 几何按 `128/96` 换算后的正确设备像素，失败只来自 96-DPI 硬编码断言。
- next142 只把 TEST75 的固定 `180/120/20/30/25/15/100/50/20/12px` 断言改为 `MulDiv(css_px, dpi, 96)`，保留定位构盒、绘制和可见页面路径。
- C89 回归、仓库审计、VS2008 `Debug|Windows Mobile 6 Professional SDK (ARMV4I)` 增量构建、staging 和 `screen=240x320 dpi=96` 设备验收均通过；日志为 `TESTBENCH PASS`。下一批轮换分辨率/DPI 后再继续存在性优先的能力。

### 6g. next124：独立脚本 global/JSON 调用桥（已设备验收，纳入 next134）

- `positron_script.h` ABI minor 升至 1.4，新增 `PScript_SetGlobalString`、`PScript_SetGlobalNumber`、`PScript_SetGlobalBoolean`、`PScript_GetGlobalJson` 和 `PScript_CallGlobalJson`。宿主只需要 `windows.h` 与公开头文件，不需要包含 Duktape；全局值在同一 opaque context 内跨求值和函数调用持久存在。
- `PScript_CallGlobalJson` 只接受 JSON array 参数，函数返回值通过 JSON 编码进入既有 DLL-owned result buffer；未定义/不可 JSON 化的值、缺失/非函数 global、非法参数和超过 255 字节的结果都返回专用可诊断错误，不静默截断。调用仍受既有 Duktape timeout、heap limit 和 fatal recovery 边界约束。
- TEST85-89 分别覆盖 primitive 注入/读取、JSON object 函数调用、跨调用可变状态、 malformed/missing/non-callable 错误恢复、全局名与结果长度限制。它们不初始化 `positron_core`，不接入 TEST13，也不启用浏览器 JavaScript。
- ARMV4I Debug 已构建通过；next134 的 `screen=240x320 dpi=96` 设备日志确认 TEST80-89 通过，并与 Browse 门禁同批完成。独立脚本自动 OK 仍不替代 Browse 视觉验收。

### 6h. next125：独立脚本 JSON 宿主回调桥（已设备验收，纳入 next134）

- `positron_script.h` ABI minor 升至 1.5，新增 `PScript_RegisterGlobalJsonFunction`、`PScript_UnregisterGlobalJsonFunction` 和 `PScript_GetNativeFunctionCount`。宿主回调只跨越 compact JSON 数组/JSON 值，不需要 Duktape 头文件，也不获得 DOM/window/network 能力。
- DLL 内部使用固定 16 槽表；同名注册会替换回调并恢复该 global，注销会写回 `undefined` 并释放槽位。回调是同步调用，不能重入或销毁上下文；返回缓冲最多 255 字节有效载荷，失败或非法 JSON 由外层调用以 recoverable call error 返回。
- TEST90-94 分别覆盖基本调用、结构化 JSON、失败恢复、替换/注销和槽位上限；它们不初始化 `positron_core`，不改变 TEST13，也不打开浏览器 JavaScript。
- ARMV4I Debug 已构建通过；next134 的 `screen=240x320 dpi=96` 设备日志确认 TEST80-94 通过，并与 Browse 门禁同批完成。

### 6i. next126：独立脚本结构化 JSON global 注入（已设备验收，纳入 next134）

- `positron_script.h` ABI minor 升至 1.6，新增 `PScript_SetGlobalJson`。宿主可在不包含 Duktape 头文件的情况下注入 object、array、string、number、boolean 或 `null`；输入使用既有 64 KiB 源码上限，解析失败不会修改原 global。
- setter 通过受保护 JSON decode 把值复制进 context；调用返回后不保留宿主输入指针，后续 `PScript_GetGlobalJson`、`PScript_Evaluate` 和 `PScript_CallGlobalJson` 都能观察或修改该值。它不改变 255 字节 DLL result buffer 的输出限制。
- TEST95-99 分别覆盖结构化读取、跨公共调用的可变对象、 malformed/null 恢复、超限输入的原值保留和 array/object/string 类型替换；它们不初始化 `positron_core`，不改变 TEST13，也不打开浏览器 JavaScript。
- ARMV4I Debug 已构建通过；next134 的 `screen=240x320 dpi=96` 设备日志确认 TEST80-99 通过。

### 6j. next135：表单文本长度约束（待设备验收）

- `PCoreFormValidationInfo` 新增 `PCORE_VALIDITY_TOO_SHORT` 与 `PCORE_VALIDITY_TOO_LONG`，只对 text/password/textarea 读取有效的 HTML 非负整数 `minlength`/`maxlength`。约束按 UTF-8 字符数计算；坏属性、disabled 和 readonly 保守忽略，required、提交阻断和首个无效控件几何保持既有路径。
- TEST100-104 覆盖静态 too-short/too-long、动态 `PCore_TextInputSetValue` 更新、textarea、边界值、禁用/只读/坏属性豁免、提交恢复和首个错误几何。它们不改 TEST13、布局引擎、浏览器 JS 或独立脚本执行路径。
- ARMV4I Debug 增量构建和 `C:\WMShare\Positron-next135` staging 已通过；设备验收时运行默认配置，并与 TEST13/20/27/43/44/56/58-99 同批查看日志。

### 6k. next136：隔离 TEST59 的 CSS 几何上下文（待设备复测）

- next135 在 `screen=480x640 dpi=192` 下于 TEST59 停止：显式 `224/320` CSS 像素夹具继承前一个设备-backed render 的 192 DPI，固定 `25px` padding 被错误换算为 `50px`。
- next136 在 TEST59 每个离线 pass 前显式调用 `PCore_SetViewport(width,240,96)`，保持 `x=25` 与 `w=width-50` 的 CSS 几何契约；测试结束后恢复运行时设备视口。该批不改 core flex/layout、不放宽断言，也不触碰 TEST13。
- ARMV4I Debug 增量构建和 `C:\WMShare\Positron-next136` staging 已通过；设备验收时运行默认配置，重点确认 TEST59-77 与 TEST100-104 继续通过。

### 6d. next121：独立 JavaScript 模块生命周期（已设备验收）

- `positron_script.h` ABI minor 升至 1.2，新增 `PScript_EvaluateModule`、`PScript_ClearModules`、`PScript_GetModuleCount`、模块名称/数量上限和专用错误码；旧 `PScript_Create`、`PScript_Evaluate` 和 80-82 行为保持兼容。
- 模块采用小型 CommonJS 风格包装：源代码接收 `module`、`exports`、`require`，同一上下文同名模块只执行一次并缓存 `exports`；已加载模块可被后续模块 `require()`，失败会删除不完整条目；清空只替换模块注册表，不销毁普通脚本上下文。
- TEST83 覆盖 `base=40`、`entry=require('base')+2`、重复加载不重新执行、失败回滚、清空和重新加载。它不读取 URL/文件、不提供网络、DOM、window 或浏览器脚本开关。
- next121 已完成 VS2008 ARMV4I Debug/Release 增量构建、`C:\WMShare\Positron-next121` staging 和设备验收；TEST83 日志为 `base=40 entry=require(base)+2=42 cache=ok rollback=ok clear/reload=ok modules=1`，并与 TEST13/20/27/43/44/56/58-77/80-82 一起以 `TESTBENCH PASS` 收尾。TEST13 不因该批增加脚本请求。

### 6e. next122：宿主模块源码 provider（已设备验收，纳入 next134）

- `positron_script.h` ABI minor 升至 1.3，新增 `PScript_SetModuleSourceProvider`、`PScript_LoadModule` 和 `PSCRIPT_ERROR_MODULE_SOURCE`；回调沿用 WM 宿主常见的 `fetch/free` 所有权约定，源码由宿主提供，DLL 在当前调用返回后释放。
- 模块内部加载器改为保留外层 Duktape 栈，因而 `require('name')` 在执行模块时可以同步向宿主索取未缓存依赖；成功模块仍只执行一次，provider 失败、源码执行失败都不留下半成品。
- TEST84 覆盖根模块与依赖按需回调、缓存不重复取源、缺源、执行失败回滚、清空后重新取源和 free 回调计数。next122 ARMV4I Debug/Release 构建与 `C:\WMShare\Positron-next122` staging 已通过，next134 的 `screen=240x320 dpi=96` 设备日志已确认通过；这批仍不接入浏览器 JS。

短期暂不继续追逐首个 SVG 的冷解析毫秒数、渐变高级参数、抗锯齿微调或复杂表格边角；next92 已把重复解析造成的导航热点降到可接受范围。

### 0. next37 稳定基线与开发转向

- `main` 产品源码固定在用户再次确认 TEST13 正常的 `9c5c7c7`/next37。
- next38 之后的 stylesheet metadata、base URL、redirect origin 和 timeout 方向暂时挂起，归档分支为 `codex/post-next37-experiments`。
- next44 已由用户确认 TEST13 全流程完全正常，Browse 冻结基线成立。
- `positron_image.dll` 公共 retained 位图 ABI、核心兼容转发及 NetSurf 解码对象复用已由 next45 的 TEST19/20 真机确认；TEST26/27 与 TEST13 同批无回归。
- 公共 DLL 的独立消费闭环已由 next46 横竖屏确认；曲线在示例小尺寸非等比缩放下略显粗，但不构成原有 SVG 正式链回归。
- next47 已确认 ABI 1.1 原生内存编码、配套释放和重新解码闭环；PNG 正常，设备默认 JPEG 在 16x16 高饱和边界上出现明显串色。next48 的 WM `EncoderQuality=100` 仅有限改善，仍有明显横纵色带。当前短期批次保留旧 WM 默认入口，把 ABI 1.2 的显式 quality JPEG 改由 libjpeg-turbo 1.5.3 以 4:4:4 编码，next49 集中复测。
- 任何冻结项重新接入都必须一次只改一个变量，并完整跑通 TEST13 深层导航；仅离线 TEST 不能作为合并依据。

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
- 当前 IANA CSS 使用 MQ4 范围语法。2026-07-11 已在 `PCore_ParseCSS` 前加入保守兼容：仅把整数像素 `(width <= Npx)` / `(width < Npx)` 转为 libcss 3.11 可解析的 `max-width`；字符串、注释、其他单位和复杂范围不改写。扩展 TEST21 的 320/300/299px 边界已由用户真机确认通过。
- 动态状态伪类中 `:focus/:active/:checked/:enabled/:disabled` 与 next113 的 `:hover` 已有状态来源；`:visited/:target/:indeterminate` 仍保持 false。

优先级建议：

1. 结合 TEST 13 看真实页面 CSS 套用是否更完整。
2. 后续再按页面痛点补其他静态伪类。

验收：

- TEST 9 中 `[title]` / `[data-role=]` / `[class~=]` / `[lang|=]` / `[data-code^=]` / `[data-code$=]` / `[data-code*=]` / `h1 + p` / `h1 ~ span` / `a:link:lang(zh)` 都能影响 computed style。
- 再跑 TEST 13 看真实页面 CSS 套用是否更完整。

### 3. ENGINE 回归可观测性

- TEST 11 同时覆盖折叠组 `body.y=p.y=16` 和 `padding-top:1px` 阻断组 `body.y=8,p.y=25`，已由 2026-07-10 用户真机截图确认通过。
- TEST 11/15/16/18 改为收集失败后继续执行，避免较早断言遮住后续结果。
- 2026-07-11 用户真机截图确认完整 ENGINE 组 TEST 6-11、15、16、18、21、22 全部通过；该离线回归门槛已关闭。后续改动这些路径时，仍须重跑整组。

### 4. next111：基础 positioned layout

- slim `pcore_box.c` 按 NetSurf `box_construct.c` 的上游规则补齐 `position:absolute/fixed` 且 `display:inline` 的 blockification，输出 `BOX_INLINE_BLOCK`，交给既有 `layout_position_absolute()`；普通 block absolute 与 relative 继续复用 NetSurf `layout.c`。
- TEST75 同时断言 static flow、relative top/left 偏移、positioned parent 下的 absolute block，以及 absolute inline 的几何和尺寸，并打开正式 layout/redraw 窗口做首帧冒烟。
- 2026-08-03 设备日志确认 TEST75 与 TEST13/20/27/43/44/56/58-74 全部 PASS。float、sticky、复杂 containing-block 组合、Grid 轨道和背景尺寸不因本批自动获得支持。

### 5. next113：动态 `:hover`

- `positron_core` 增加 document-owned hover node，`PCore_InteractionSetAt` 可按命中盒设置/替换/清除 `PCORE_INTERACTION_HOVER`；libcss selector callback 在下一次 `PCore_StyleDocument`/`Ex` pass 中匹配 `:hover`。
- WM6 host 在 `WM_MOUSEMOVE` 中桥接 document CSS 坐标，并以 250ms `WM_TIMER` 轮询光标离开客户区；不依赖桌面 Win32 的 `TrackMouseEvent` 或 `WM_MOUSELEAVE`。
- TEST76 断言蓝色 → hover 红色 → 清除恢复蓝色，并与 TEST13/20/27/43/44/56/58-75 同批自动运行。2026-08-03 `C:\WMShare\Positron-next113\test_host.log` 以 `TESTBENCH PASS` 结束。
- 该批只实现 CSS hover 状态桥，不宣称 `:visited/:target/:indeterminate`、专用 MouseEvent、触屏 hover、异步事件队列或 JavaScript。

### 6. next114：脚本资源发现与 document 缓存接口

- `positron_core.dll` 新增 `PCore_FetchScriptResources` 与基准 URL 感知的 `PCore_FetchScriptResourcesEx`；core 只扫描非空外部 `<script src>`，URL 解析由宿主 `PCoreResolveUrlFn` 负责，字节由 `PCoreFetchFn/PCoreFreeFn` 负责。
- 成功字节按 document 生命周期去重缓存，`PCore_GetScriptResourceCount/PCore_GetScriptResource` 为未来脚本运行时提供只读枚举和借用数据指针；缓存最多 32 条、单条 512 KiB、合计 2 MiB，失败 body 不进入缓存。
- TEST77 离线断言相对/root-relative/absolute URL、重复引用、cache-only 第二遍、枚举内容和 inline script 不执行。该批不解释 `type`、不执行 JavaScript，也不把脚本请求加入冻结的 TEST13 网络事务；ARMV4I 构建及设备 `TESTBENCH PASS` 已确认。

### 6a. next118：独立 JavaScript 运行时 DLL

- `positron_script/positron_script.vcproj` 以 VS2008 ARMV4I DLL 形式编译仓库内 NetSurf Duktape 2.7.0 单文件源，不让其他程序在构建时下载或自行拼装解释器。
- `positron_script.h` 只暴露稳定 C ABI：ABI 查询、创建/销毁 opaque `HANDLE`、UTF-8 源码求值、结果/错误借用字符串、预算、DLL 内存使用量和求值计数。上下文、堆、返回字符串和错误字符串均由 DLL 管理。
- TEST80 不初始化 `positron_core`，只验证 `40+2`、持久上下文、异常后恢复和遥测计数；这使“其他 WM 程序可以调用”先有可运行纵切，同时不把浏览器 TEST13 暴露给未验证的脚本网络/DOM 风险。
- 当前本地 VS2008 ARMV4I Debug 增量构建为 0 错误/0 警告；TEST80 设备门禁已通过，长循环 timeout/source-size/recovery 边界和未来 DOM/window/fetch/native bridge 仍待后续批次。浏览器 JS 默认继续关闭。

### 6b. next119：独立 JavaScript 安全边界

- TEST81 使用独立 `positron_script.dll` 上下文和 50 ms 预算执行 `while (true) {}`，要求返回 `PSCRIPT_ERROR_TIMEOUT`，随后以超过 `PSCRIPT_MAX_SOURCE_BYTES` 的显式长度验证拒绝，再求值 `6 * 7` 并得到 `42`。
- 该断言同时检查上下文仍可用、执行计数只统计真正进入求值的两次调用、DLL 堆遥测非零；源码长度拒绝不伪造 64 KiB 缓冲区，也不代表已经有独立的总内存配额。
- next119 已完成 VS2008 ARMV4I Debug 增量构建、staging 到 `C:\WMShare\Positron-next119` 和设备验收；TEST81 日志为 timeout=-4、limit=-2、recovery=42、eval=2/2、memory=64993。超时后的 UI 调度、脚本模块图、DOM/window/fetch/native bridge 仍不在此批次。

### 6c. next120：独立 JavaScript 总内存配额

- `positron_script.h` ABI minor 升至 1.1；旧 `PScript_Create` 保持入口，新增 `PScript_CreateEx`、`PSCRIPT_ERROR_MEMORY_LIMIT`、`PScript_GetMemoryLimit` 和 `PScript_GetPeakMemoryUsed`。配额覆盖 Duktape 分配及 wrapper allocation header，不包含宿主进程其他内存。
- TEST82 以 512 KiB 上限执行短生命周期数组压力，要求超限返回专用错误、峰值不超过上限，并在失败后继续求值 `42`。这验证 runtime heap 边界，不等于模块图、DOM/window/fetch 或浏览器 JS 接入。
- next120 已完成 VS2008 ARMV4I Debug 增量构建、staging 到 `C:\WMShare\Positron-next120` 和设备验收；TEST82 日志为 `rc=-6 used=70057 peak=496184 limit=524288 recovery=42 eval=2/2`。这仍只约束 DLL 的 Duktape heap，不约束宿主进程其他内存。

### 7. next115：普通浮动构盒候选（已否决）

- `pcore_box.c` 只对普通非替换元素的 `float:left/right` 建立 style-less `BOX_FLOAT_LEFT/RIGHT` 包装，浮动子项先按 CSS blockification 构造成 block/flex/table，再交给 NetSurf 正式 float/clear 布局；`img` 与 form gadget 明确排除，现有替换元素和原生控件路径不改。
- TEST79 是离线夹具，断言左右浮动同线、浮动旁文本使用中间空间、`clear:both` 位于两个浮动之后，以及 flex 容器直接子项不会误变为 float。TEST78 保留为撤回历史实验编号，不可运行。
- ARMV4I 增量构建和 `C:\WMShare\Positron-next115` staging 已完成；设备 TEST79 返回失败，且用户截图确认 TEST13 导航视觉回归。next114 保持基线，不把历史 TEST23 原样恢复。

### 8. next116：收窄的 block-level 浮动候选（已否决，方向暂挂）

- next115 的设备日志显示 `flowtext` 被 `PCore_NodeBox` 找到的是零宽 inline 起始盒，且 TEST13 截图出现导航扁平化；该候选不再作为基线。
- next116 只让显式 `display:block` 的非替换 float 进入 `BOX_FLOAT_LEFT/RIGHT`，但真实页面仍出现导航扁平化和正文排版回归；自动 TEST13 的数值 OK 不能覆盖人工 Browse 截图。
- 设备日志最终为 `TEST79 FAIL`，几何为 `float=(0,0 70x36)/(154,0 70x36) probe=(70,0 50x20) flowblock=(y0 h48) clear=(0,48 224x12) flex=(0,60 120x24) items=(0,60)/(32,60)`。代码、TEST79 默认配置和 ENGINE 接入已撤回，next114 恢复为设备基线；后续必须先完成完整 box construction/normalisation 方案，再重新考虑 float。

## 中期规划

目标：让 TEST 13 从“能打开简单页面”变成“能浏览一批轻量真实页面”。

### 1. 图片与 SVG

当前状态：

- `<img>` 已先在 `pcore_box.c` 接入 alt/src 文本占位，并由 TEST 17 于 2026-07-10 真机验证。
- 旧 TEST 18 的 `<img src>` 资源发现/fetch 已真机通过；当前源码将成功字节复制到 document user-data 缓存，并按 URL 去重。embedder 缓冲仍由 `freefn` 立即释放；核心副本随文档释放。
- 已新增 `pcore_wmimage.cpp` C++ 小适配层，调用 WM Imaging API 的 `CreateImageFromBuffer` / `IImage::Draw`，并在 TEST 19 中用内存 2x2 BMP 验证原生解码/绘制；2026-07-10 已真机通过。内存 PNG 首次真机反馈为 decode fail，需在 BMP 基线后单独做格式覆盖。
- 缓存命中且可解码的 `<img>` 现在生成 `box->object`，并走 `content_redraw -> plot_bitmap -> IImage::Draw`；TEST 20 已真机验证。
- BMP/PNG/JPEG/GIF 的 WM Imaging 可见绘制已确认；2026-07-12 用户确认 TEST20 四格式缓存 `<img>` 正式链。SVG retained draw、缓存 `<img>`、损坏 fallback、网络相对资源与 fill-rule 已由 TEST26-29/13 真机确认。
- SVG 使用仓库内 NetSurf `libsvgtiny`，不另写解析器。`positron_libsvgtiny.vcproj` 已建立；缺失的 `autogenerated_colors.c` 由 `scripts/gen_svgtiny_colors.py` 从上游 `colors.gperf` 可重复生成。
- 已 vendor 官方 Expat 2.8.2，并建立内部 `positron_expat.lib`、libdom Expat XML binding 和版本锁定的 `scripts/port_expat_vs2008.py`。`positron_image.dll` 的 `PImage_SvgInfoFromMemory` 已把内存 SVG parse/link 接通；2026-07-12 TEST25 在 WM6 ARM 真机确认 `64x32`、`2 shapes`。此刻可以称“SVG 已解析”，仍不能称“SVG 已显示”。

建议顺序：

1. 最新 TEST13 已确认普通文本方框消失、词间距正确；补齐上游 `<pre>` UA 默认后，TEST15 也已由用户确认 `normal_ws=ok/pre_lf=kept`，文本空白闭环完成。页面整体仍未通过。
2. 旋转验收已完成：扩展 TEST24 的缓存重选、无联网和 0%/50%/100% 滚动比例已由设备确认；真实 TEST13 横竖屏切换也保持在 `Further Reading / Domain Names` 同一阅读区域。
3. next115/next116 的 float 构盒实验均已暂停。当前仍按“float 未支持”对外表述；若重新启动，必须先按上游 box construction/normalisation 设计完整整树结构，并同时通过 TEST79、TEST13 深链和旋转门禁，历史 TEST23 不恢复。
4. 导航异步化第二阶段已实现并通过 VS2008 ARM 增量构建：主文档 GET 后，UI 线程只负责 parse 与结构化资源发现，外链 CSS、`<img>` 和外链 CSS 应用后出现的背景 URL 分轮交给同一 worker；全部成功或失败后才在 UI 做最终 style/layout/swap。旧页与不定量进度条贯穿所有网络阶段，窗口关闭会等待并统一回收 request/document/resource。TEST43 已在设备确认显式 origin URL、去重、cache hit copy 与一次失败；冻结 TEST13 全流程也已在后续 next37 恢复基线及多批回归中通过。
5. **已完成并真机验收**：Expat -> libdom XML -> libsvgtiny 内存 SVG 解析与 TEST25。
6. **已完成并真机验收**：首版固定折线 cubic 因明显阶梯边缘判定视觉失败；替代实现保留 libsvgtiny 解析，使用固定提交的 NanoSVG 5 倍子像素栅格器及预乘 BGRA DIB + WM `AlphaBlend`。2026-07-13 增强 TEST26 的内部填充、部分覆盖边缘断言及设备截图均通过。
7. **已完成并真机验收**：统一图片载体和缓存 SVG `<img>` 正式链由 TEST27 的自动断言及设备截图确认。
8. **已完成并真机验收**：TEST28 将已 fetch 的损坏 SVG 缓存后布局，拒绝错误的 replaced object 并保留非空 alt 文本盒；TEST13 的 HTTPS HTML + 同目录相对 SVG fixture 已验证真实页面 origin、相对 URL、网络 fetch/cache 和正式绘制。
9. **已完成并真机验收**：libsvgtiny 最小 fill-rule 扩展与 TEST29 已确认默认 nonzero、attribute/style 继承 evenodd；首次 `#00a000 -> #00a200` 是 RGB565 量化假失败，改用纯绿后严格像素断言通过。
10. **已完成并真机验收**：CSS 单一背景 URL 纳入 document image cache，构盒后挂到 `box->background`，由 NetSurf redraw 处理 repeat/position，GDI plotter 参照上游 frontend 落实 tile flags。TEST30 已确认同 URL 去重、no-repeat、position 与双向 repeat；不包含 background-size、多层背景和异步资源 fetch。
11. **已完成并真机验收**：基础 SVG text 继续使用 libsvgtiny 解析，并扩展其输出以保留继承的 fill、font-size、通用 font-family、weight/style、text-anchor 与常见 transform 元数据；`positron_image.dll` 按原 SVG 顺序交错执行 NanoSVG path 批次和 WM 原生 GDI 字体命令。TEST31 已确认蓝色粗体文本、居中 anchor 与后来红色 path 的覆盖顺序。联网审计过 ThorVG、resvg 和 PlutoSVG：它们分别是现代 C++、Rust 或偏 OpenType SVG 的方案，当前都不是 VS2008/C++03 的直接替换件，因此此阶段没有复制一个不适配的完整渲染器。复杂 shaping、`textPath`、逐字 dx/dy、任意 shear 和完整透明度仍为显式缺口。
12. **已完成并真机验收**：TEST32 已证明红到蓝渐变和白色文本能经过 document cache、160x80 replaced box 与 NetSurf redraw。首轮截图暴露 libsvgtiny 纯色三角展开的密集接缝；改为 libsvgtiny 结构化 stops/归一化轴 -> NanoSVG 单路径连续填充后，2026-07-13 真机截图确认色带平滑，文字与 seam/jump guard 均通过。
13. **已完成并真机验收**：TEST33 用三块 70x70 图和九个颜色点验证 objectBoundingBox 斜向渐变、userSpaceOnUse 水平渐变以及 `gradientTransform="rotate(90 ...)"` 后的竖向渐变，并对水平/竖直轴各做一条接缝扫描；2026-07-13 真机截图确认三块方向、颜色和连续性均正确。
14. **已完成并真机验收**：TEST34 在 libsvgtiny DOM 桥中加入 `radialGradient` 的 `cx/cy/r`、objectBoundingBox/userSpaceOnUse 和 `gradientTransform`，继续复用 NanoSVG 已有径向光栅器。2026-07-13 真机截图确认三块图依次为平滑椭圆、圆和向右平移的圆，九点颜色及三条连续性断言同时通过。
15. **已完成并真机验收**：TEST35 将 160x80 中心径向 SVG 作为内存资源送入文档缓存，生成 replaced box 并经 NetSurf redraw 绘制。2026-07-13 真机截图确认连续横向椭圆、无 fallback，且此前 fetch/free、盒尺寸、横纵采样与连续性断言全部通过。
16. **已完成并真机验收**：TEST36 一次验证 objectBoundingBox 无单位 `0..1` 坐标、`xlink:href` 渐变继承、属性/inline style `stop-opacity`、线性/径向 alpha 混合及循环引用深度保护；TEST37 再用 SVG2 `href` 把同一半透明偏心径向 SVG 同时用于 `<img>` 和 CSS 背景，资源只 fetch/free 一次且两处像素一致。2026-07-13 用户截图确认四面板与两处缓存结果符合预期。焦点 `fx/fy` 与 spread method 因 NanoSVG rasterizer 仍有明确 TODO，不在已完成范围。
17. **公共位图 ABI 已完成当前真机验收**：上游解析库保持静态 `.lib`；`positron_image.dll` 统一封装 WM Imaging 与 libsvgtiny，公开 opaque bitmap/SVG create/info/draw/free。位图创建时复制编码字节并保留 `IImage`，调用方可立即释放输入；对象必须在创建线程由同一 DLL 释放。`positron_core` 的旧 `PCore_Image*` 保留为兼容转发，NetSurf 图片载体直接复用 retained handle，避免每次重绘重新解码。2026-07-15 next45 的 TEST19/20 已确认四格式颜色、重复绘制、输入所有权、错误拒绝、兼容转发和正式缓存链，TEST13/26/27 同批无回归。
18. **IANA custom-properties 根因批次已完成并真机验收**：联网读取当前 `iana_website.80c103cc08b6.css` 后确认窄屏 padding/margin 大量依赖 `var(--space-*)`；审计 NetSurf 最新 libcss `104d87f` 仍无 custom-properties 实现。`PCore_ParseCSS` 因此新增保守兼容层，只收集同一 stylesheet 顶层精确 `:root` token，支持嵌套引用、fallback、循环拒绝、字符串/注释保护，并设置 128 token/16 层递归/8 倍输出上限。TEST38 的语义断言和 TEST39 的 240/320px 25px inset + 正式 redraw 均由设备确认；新的 TEST13 截图中导航、正文和注册表列已恢复可读。元素作用域、跨表级联和 `@property` 仍不在范围；float 保持撤回。
19. **现代 CSS 值兼容批次已完成并真机验收**：当前 IANA CSS 另有 22 处 `oklch()` 和 15 处 `calc()`。新增独立 `pcore_css_values.c`，其中 Oklab 到线性 sRGB 的矩阵移植自 Bjorn Ottosson 的公开域/MIT 参考实现，再做 sRGB transfer 与边界裁剪；`calc()` 只求值同单位加减、一个有单位因子的乘法及无单位除数，混合 `%/px` 等依赖布局上下文的表达式原样保留。TEST40 已确认红色、alpha、IANA link 色、变量展开后的 `+ - * /` 几何和混合单位保留；同期 TEST13 的配色、标题与间距改善。数值型 OKLCH、裁剪 gamut 与可完全求值 calc 不得表述为完整 CSS Color 4/Values 实现。
20. **IANA `/numbers` grid-overflow 修复已真机验收**：该页的 `main` 含 `display:grid`，其 `.dtable-wrap { overflow:auto }` 内宽表格曾在单列 block 降级中把 flex item 的 min-content 撑宽，`row-reverse` 因而把正文排到负 x。`layout_flex.c` 现只对树中实际含 grid/inline-grid 降级盒的 flex item 跳过错误的 block min-content 钳制；普通 flex 保持原规则，`inline-grid` 按 inline-block 降级。TEST41 的竖横屏截图均确认主内容保持左右 inset，宽表格没有再移动页面。此项仍不代表 Grid 轨道或 gap 已实现。
21. **NetSurf overflow scrollbar 已完成当前验收**：移植并 C89 化上游 `desktop/scrollbar.c`，恢复 `descendant_x1/y1` 溢出判定、`box_handle_scrollbars` 创建/更新/成对销毁、祖先 scroll offset 坐标和 GDI redraw。公开 `PCore_OverflowPointer` 把 WM 的 DOWN/MOVE/UP 转发给箭头、page well 与 thumb drag；TEST42 的离屏 16px 步进断言及真机箭头/thumb 交互均已通过。随后新增 `PCore_OverflowDirtyRect`，host 只失效 overflow viewport，不再为每个拖动消息重绘整窗；VS2008 ARM 增量构建 0 错误。仍不宣称触摸惯性、overlay scrollbar 或完整 Grid。
22. **CSS/图片后台资源事务与单响应进度已真机验收**：pending request 持有未交换 document 与最多 64 个去重 URL；worker 只读显式新页面 origin 并保存总计最多 2 MiB 原始字节，DOM/libcss/NetSurf/GDI 始终留在 UI。TEST3/43/13 已确认真实正文进度、资源去重/失败 fallback 和成功 swap。UI 提交现由一次性 WM timer 拆成 parse/style/image-discovery/layout 四段；单个 NetSurf 调用仍可能卡顿。TEST44 已确认主文档失败保留旧页与事务收尾。2 MiB 是 `test_host` 临时宿主预算，不是产品上限；整页聚合进度、字体和脚本仍未完成。
23. **CSS `@import` 首批实现已真机验收**：`positron_core` 新增兼容扩展 `PCore_StyleDocumentEx2`，保留旧 ABI，并使用 libcss 原生 `next_pending_import/register_import` 递归解析最多 16 层导入；缺失、循环或超深导入注册空表，使父表后续规则继续生效。URL 策略仍在宿主，WM `test_host` 使用 `InternetCombineUrlA` 处理文档、父表和子表相对引用。成功字节进入现有 document CSS cache，旋转仅 cache-only 重选。TEST45 已确认三层 URL 规范化、一个失败导入、父/子 computed color、首次 fetch/free 计数和二次缓存重选。当前不宣称跨源策略、缓存失效、整页聚合进度或 web fonts 已完成。
24. **图片 DLL JPEG 4:4:4 开源后端已真机验收**：ABI 1.1 的 WM Imaging PNG/JPEG 内存编码、`PImage_FreeBuffer` 所有权和重新解码闭环已由 next47 确认；next48 证明 WM quality=100 仍无法避免小尺寸高饱和图的明显色度串扰。按“成熟能力优先移植开源实现”的原则，新增 `positron_libjpeg` 静态工程，固定 libjpeg-turbo 1.5.3；显式 quality JPEG 先由 WM Imaging 转成锁定的 24bpp 行，再由 libjpeg 压缩器以 ISLOW DCT、三个分量 1x1 的 4:4:4 输出。旧 `PImage_EncodeBitmap` 默认 JPEG 与 PNG 仍保留 WM 路径。next49 已确认 SOF 断言、行方向及红绿蓝黄颜色正确，大面积横纵色带消失。Debug DLL 相比 next48 增加约 238 KiB；无额外部署 DLL。编码时主要临时开销是约 `width*height*3` 的 WM 24bpp 中间位图、压缩输出和 libjpeg 工作区，普通解码/绘制不走此路径。
25. **ABI 1.3 原始像素入口已完成视觉验收，示例退出缺陷转入 next51**：新增复制式 `PImage_CreateBitmapFromPixels`，首批支持顶到下的 BGR24 与 straight-alpha BGRA32，调用方同时提供缓冲长度、尺寸、stride 和格式。实现先验证所有行的可读边界，再复制到 DLL 自有的 WM 对齐存储，并通过 `CreateBitmapFromBuffer` retained；调用方可立即覆盖源缓冲。next50 截图确认 padded BGR24、半透明 BGRA32、RGB/alpha PNG、4:4:4 JPEG 与 retained SVG 六项颜色正确；启动前的短缓冲拒绝和输入清零也均未触发错误。示例原先未处理 `WM_CLOSE`，标题栏 X 采用 WM 智能最小化而使进程残留，故退出生命周期尚未通过。
26. **ABI 1.4 原生 BMP/GIF 输出与示例真退出均已验收**：next51 能进入六项可见界面，证明启动前的 `BM`/`GIF8` 签名、重新解码和 16x16 尺寸均通过；截图也确认原始像素、PNG alpha、JPEG 与 SVG 视觉保持正常。next51 的 `WM_CLOSE -> DestroyWindow` 没有解决退出，因为 WM/Pocket PC 标题栏 X 是 Shell Smart Minimize，不保证发送 `WM_CLOSE`。next52 按 WM6 SDK 的 `ShellApiDemo` 模式启用 `SHDoneButton(SHDB_SHOW)`，在 `WM_COMMAND/IDOK` 销毁窗口并进入 `WM_DESTROY -> PostQuitMessage`；用户已确认点击原生 OK 后任务管理器不再残留，且示例可正常再次启动。不创建左右软键，不占客户区；跨线程句柄仍未提供。
27. **NetSurf table span 占位批次已由 next53 真机验收**：`pcore_box.c` 不再让每行从第 0 列盲排，而是移植 NetSurf 3.11 `box_normalise.c` 的 span occupancy：有限 rowspan 逐行递减，`rowspan=0` 延伸到当前 row group 末尾，下一 `<tbody>` 不继承占位，`colspan=0` 归一为 1，并对 HTML span 值和 WM 临时列记录设置上限。TEST46 的四行三列颜色、位置、几何、12 点像素与正式 redraw 均已确认；同批 TEST13/17/41/42 其余功能正常。此项尚不包括畸形表格空单元格生成或完整 collapsed-border 冲突规则。
28. **next54 的滚动条占位修复只部分通过**：host 用 `WS_VSCROLL`、`SetWindowLong` 与 `SWP_FRAMECHANGED` 按实际文档高度切换原生非客户区，TEST41 的 auto-height 横条也已不再覆盖内容；但第二次整树 layout 同时让 fixed-height overflow 预留空间，改变了已验收几何，TEST42 因而在旧右箭头位置得到 `used=0/0/0`。设备截图还暴露右箭头沿用背景 `rect.y0` 后向下偏 2px。不能把 next54 记为完成。
29. **next55 收窄 reflow 并补箭头像素回归，已由设备验收**：第二次 layout 前只屏蔽 fixed-height `overflow:auto` 的首轮横向 extent，使其保持 next53 几何；auto-height 容器仍利用首轮 descendant bounds 预留 16px。NetSurf 右箭头改回与左箭头对称的 `area.y0` 坐标基准。用户确认 TEST41/42 的横条空间、箭头位置、短页纵条与色块页表现均正常；冻结的 TEST13 导航链未改。
30. **next56 匿名表格归一化批次已由设备验收**：按 NetSurf 3.11 `box_normalise.c` 补齐 table/row-group/row/cell 的匿名包装及短行空单元格生成。匿名盒使用 libcss 默认样式与父样式 compose，并由 box tree 独立释放，避免借用父背景/边框。用户确认 TEST47 红/白、绿/蓝两行以及同批其余测试正常；冻结的 TEST13 导航链未改。
31. **next57 NetSurf 列表 marker 语义已验收、视觉缺口转入 next58**：移植上游 `box_construct_marker` 的 disc/circle/square 构造，恢复 DOM LI 到 box 的 user-data 映射，使 `layout_lists` 正式计算有序列表；UA CSS 补常用 UL/OL padding、margin 与嵌套 marker。TEST48 已确认 8 个 marker 的嵌套层级、`start/value` 与 `reversed` 顺序正确，但设备 Tahoma 把 circle/square 显示为错误字形/豆腐。当前 `positron_list_style_stub.c` 仍只格式化十进制，完整 roman/alpha/CJK counter-style 和 `list-style-image` 后续再移植。
32. **next58 随包单色字体 fallback 部分通过**：从 Noto 官方 Noto Sans Symbols 2 与 Noto Emoji 生成改名后的静态 TrueType 子集，合计约 814 KiB；emoji 固化 400 字重并为 1,242 个补充平面字形建立 BMP PUA 别名，避免依赖 WM6 GDI 的 surrogate/cmap12 支持。`PCore_Init/Shutdown` 通过 CE 原生 `AddFontResourceW/RemoveFontResourceW` 管理 DLL 相邻 `fonts` 目录并广播 `WM_FONTCHANGE`；绘制、width、position、split 共用 fallback run 与同一 emoji 映射。设备已确认 disc/circle/square 及部分符号/emoji 可见；四个基础箭头仍为 tofu，圆形与 emoji 边缘较粗。
33. **next59 补齐基础符号并请求抗锯齿，已设备验收**：追加官方 hinted Noto Sans Symbols 子集，保留 Symbols 2 对已验收 marker 的优先级，只在其真实 cmap 缺口使用 Basic face；三份部署字体共约 901 KiB。生成器输出两套独立精确覆盖表，`PCore_BundledFontSupports` 与 TEST49 在开窗前逐个断言本页 16 个码点。设备确认四个箭头不再 tofu、marker 与五个 emoji 均可见，视觉比 next58 稍好；细线仍受 OEM GDI 限制。复杂 ZWJ shaping、彩色字体与网页 `@font-face` 不在本批次。
34. **next60/61 完整 counter style 与图片 marker 已验收**：用 `scripts/port_list_style_vs2008.py` 从仓库原版 libcss `format_list_style.c` 可重复生成 ASCII/C89 文件，保留上游 47 种 Roman/Latin/Greek/CJK 等 counter style，不再使用 decimal-only stub。生成器把 C99 指定初始化器按已知结构转成位置初始化器，并把 UTF-8 字面量转为固定三位八进制；`list-style-image` 仅从 computed `display:list-item` 发现资源，复用 document image cache，成功生成 NetSurf marker object，失败保留 `list-style-type`。next60 首测失败来自 staging 混用旧 Debug core DLL；`stage.bat` 增加自动增量构建门禁后，next61 的 TEST50 已在设备确认 IV/z/aa/09、绿色缓存 SVG marker 与圆形失败回退均正确。
35. **next62 `list-style-position:inside` 内联首行已验收**：上游 NetSurf 3.11 已计算 inside/outside computed value，却仍统一把 marker 放到负 x。当前补丁在 marker 尺寸准备后，让 inline-first list item 的首行宽度吸收文本或缓存图片 marker，后续换行回到内容起点；outside 路径保持原语义。TEST51 自动检查 `VIII.`、12x12 SVG 资源计数、inside/outside 几何及悬挂换行；用户提供的横竖屏截图均符合预期。
36. **next63 inside 匿名首行扩展已由设备验收**：W3C CSS Lists Level 3 把 inside marker 定义为列表内容开头的 inline element。构盒阶段为 inside 项加入零宽、不绘制的匿名 inline run，因此首个作者子项为 block 或不存在时，NetSurf 自然生成 marker 首行并更新后续块、兄弟和父高度；图片 marker 行高取 `max(line-height, intrinsic height)`。TEST52 的横竖屏截图已确认 III block-first、IV 空条目、V/VI 嵌套计数以及绿色图片 marker 均符合预期；float 邻接保持后续限制，且未修改 TEST13 导航冻结路径。
37. **next64 collapsed-border 冲突批次已由设备验收**：继续使用 NetSurf 3.11 `table.c` 的原生 used-border 算法，不另写浏览器规则。只读 `PCore_TableCellBorder` 诊断和 TEST53 一次断言 wider、equal-width style priority、hidden、left/top tie、cell/row/row-group/table origin 及 separate 对照；2026-07-16 用户提供的纵横屏截图确认蓝色 dotted、品红 double、hidden gap、橙色 tie、品红 top、青色横线和 separate 双边均符合预期。W3C CSS Tables 仍要求处理 `col`/`colgroup` 来源，但 NetSurf 3.11 及 2026-04-28 官方仓库最新提交的 `table.c` 都保留该模型 TODO，本项不扩大为完整表格边框支持。
38. **next65 跨行终止边与 row-group 边界已由设备验收**：上游 `table_used_bottom_border_for_cell` 在 rowspan 到达表格底部时错误取起始 row 的下边框；当前改为记录跨度终止 row，并让非末尾 `tbody` 的共享边由下一组 cell top 承接。TEST54 集中覆盖有限 rowspan、`rowspan=0`、colspan 相邻边及两个 row group；2026-07-16 设备截图确认 finite 红色终止边、auto 紫色终止边、colspan 青边和组间橙边均正确。
39. **next66-67 table-cell 对齐与空格绘制已由设备验收**：官方 NetSurf 到 2026-04-28 最新版本仍把 cell baseline 降级为 top，也未消费 libcss 已计算的 `empty-cells`。当前 baseline 复用 NetSurf inline layout 的 3/4 line-height 近似；`empty-cells:hide` 参考 Mozilla 的成熟实现，仅在 separated model 且无可见内容时抑制 cell 背景和边框。TEST55 已确认 top/middle/bottom、大小字体 baseline、rowspan bottom 及 hide/show/filled；next66 首次失败仅因 WM compatible bitmap 的 3-6 色阶量化，next67 改为仍能拒绝通道错位的紧容差后自动断言和可见语义均通过。IANA Further Reading 的圆点是真实 `<li>` 在 marker 支持完善后的正常呈现。
40. **next68 修正 TEST55 可见页并补显式 table height 分配，已由设备验收**：next67 四组固定高度只超出 WM 客户区十几像素，但仍生成了纵向滚动条；next68 将其压到约 240px 并设标题行高。NetSurf `layout.c` 原本只扩大 table 自身而留着 row/cell 分配 TODO；当前参考 Blink `LayoutTableSection` 的比例分配和小数余量算法，用 NetSurf 现有 rowspan 活跃列把每行增量累加到覆盖该行的 cell bottom padding，再由已验收的 vertical-align 消费。新增只读 `PCore_TableRowGeometry` 和 TEST56，一次检查 105px 三行等比分配、top/middle/bottom、70px 两行分配与 rowspan bottom。2026-07-16 设备截图确认 TEST55 无多余纵向滚动条、TEST56 两张表的行高和对齐正确，TEST13 长页面滚动回归正常。
41. **源码自包含与许可证文档整理完成**：官方 mbedTLS `mbedtls-2.16.12` 完整源树已纳入 Git，本机副本与标签提交 `cf466712...` 规范化内容一致；cJSON `v1.7.18` 与官方提交 `acc76239...` 一致并补独立许可证。根 `LICENSE` 只覆盖 Positron 自有代码，`THIRD_PARTY.md` 明确 NetSurf GPLv2、Apache/MIT/OFL/zlib/IJG 等边界。`scripts/audit_repo.py` 会检查 14 个 vcproj 的源码引用、Git 跟踪、版本和关键许可证；正常 clone 不再需要下载源码，仍需用户自行安装不可再分发的微软工具链。
42. **next73 百分比 table-row 第二遍已由设备验收**：next69 首次得到 20/30/30；包切换期间的 TEST56 异常后来确认是 WM/CE 全局 DLL 复用导致的混搭。next72 的 `styles=0:0` 暴露 inline author CSS 未进入选择；TEST57 改用外部类规则后，next73 同时通过 TEST55/56/57，设备显示 20/40/20 与 25/25。后续复核确认正式 Positron 路径不调用受 `author_level_css` 控制的 NetSurf `box_construct.c`，直接根因是 `pcore_style_subtree` 固定传空 inline sheet。
43. **next75/TEST58 HTML `style=` 正式路径与 TEST56 回归恢复已由设备验收**：复用 Positron 现有 CSS 兼容转换和 URL resolver，以 `inline_style=true` 创建 libcss 声明列表，并像 NetSurf 一样逐元素选择后销毁临时 sheet。next74 设备 TEST56 的行高仍正确，但 `.distributed .top` 丢失 top 对齐，暴露 class-only 祖先复合选择器的通配 qname `*` 未被 `named_ancestor_node`/`named_parent_node` 正确处理。next75 统一使用 universal-aware name matcher，并在 TEST58 增加独立后代 class 断言；2026-07-24 设备确认未改断言的 TEST56 和 TEST58 均通过，可见 cascade 文本与 25/50/auto 三行布局符合预期。
44. **next77/TEST59 已收窄无 Grid 的 flex-overflow 主内容负 x**：TEST13 起始页及其余回归正常，但 IANA `/domains/reserved` 曾整体左移。该页与 TEST41 的 `/numbers` 不同，没有 Grid 包装层，只有 `.dtable-wrap { overflow:auto }` 宽表格；next77 将既有 Grid 特例推广为严格受限的 min-content boundary，只对横向、`flex-shrink>0` 且后代含 grid/inline-grid fallback 或 `overflow-x:auto/scroll` 的 flex item跳过隐式 `box->min_width` 钳制，显式 `min-width` 仍优先。设备已确认 TEST59、同批回归和竖屏子页边距；但同页旋转到横屏后，首个 `Domain` 内容仍向左偏移约 18px，不能把真实页问题写成完成。
45. **next78 已失败并撤回**：在最终 layout 后递归 `scrollbar_set(...,0)` 令 TEST13 横屏全部表格单元格异常，TEST56 随后报垂直对齐回归并触发系统级 `test_host.exe` 异常。该行为、两个公共诊断 API 和同 DOM 扩展 TEST59 已删除；不得继续沿“全局重置 scrollbar 回调”方向开发。
46. **next79 已恢复 next77 core 行为并通过设备门禁**：保留已验收的受限 flex min-content boundary，恢复旧版 TEST59；ARM DLL 的 `.text` 与 next77 大小和 SHA-256 完全一致。设备确认 TEST56/59 正常，TEST13 也准确回到仅横屏首个 `Domain` 异常，没有 next78 的全表和系统异常。
47. **next80 已完成 libcss selector node-data 生命周期修复**：旧 `set_libcss_node_data` 立即执行 `CSS_NODE_DELETED`，而 libcss `css__get_parent_bloom` 在回调返回后仍借用父 bloom，形成明确悬空指针。现按 NetSurf 原生模式将 node-data 挂到 DOM user-data；每个新 selection context 开始前清掉上一轮缓存，避免跨 stylesheet/media 复用。TEST60 使用同一 DOM 的 224×320→400×240 二次重选，自动断言 IANA 同型首表头 18px/10px inset 和粗体文字宽度。2026-07-25 TEST56/58/59/60 与真实 TEST13 `/domains/reserved` 横竖屏均由设备确认正常。
48. **next81 已清理全零 `nsoption` shim 并通过设备门禁**：审计 ARM 工程实际编译的 NetSurf 文件后，当前只读取 `font_min_size`、`core_select_menu`、`remove_backgrounds`；旧宏令整数默认错误地变成 0。新 shim 对齐 NetSurf 3.11 的 85/false/false，并显式记录 author CSS、前景/背景图片开启和 JavaScript 关闭的产品策略。token-paste 查找让任何未列出的新 option 在编译期失败。TEST61 以 `1px`/`8.5pt` 同宽、`12pt` 更宽验证正式 font/layout 路径；2026-07-25 设备确认 TEST56/58-61 未发现问题。
49. **next82/83 暴露 TEST62 像素指标缺口，next84 修复，next85 完成间距验收**：参考 NetSurf `box_special.c::box_input` 的 gadget 绑定方式，在 Positron slim builder 中为 checkbox/radio 创建只读 `form_control`，让已移植的 NetSurf `layout.c` 和 `redraw.c` 原生处理 1em 几何、方框/圆框与 selected 状态。next84 以最终 gadget 状态和 RGB 暗度通过设备门禁，四种可视状态正确；next85 在上游默认 `0.1em` padding 之外追加动态 `0.2em` 右 margin。设备截图确认状态、间距和 hidden-input 行为基本符合预期，不使用固定设备像素。
50. **next86 已建立并验收复杂页面完成阶段遥测**：不改变 next37 冻结导航控制流，只在 `test_host` 请求对象记录总/网络耗时、parse/style/image-discovery/layout/首帧、最大 UI slice，以及资源 queued/fetched/failed、worker rounds、document/cache bytes 和预算拒绝。设备实测 6435ms 总时长中网络 5503ms；最大 UI slice 与 layout 均为 673ms，style 182ms，其余阶段不超过 36ms；2 个资源和约 128KiB 原始字节没有触及预算。下一步细分 core layout 内部，而不是先改网络或扩大 2MiB 宿主预算。
51. **next87 只读 core layout 阶段遥测已由设备验收**：`PCore_LayoutDocument` 记录 box construction、首轮 `layout_document`、overflow 检查/可选 settling 二次 layout、finalize 与 total；`PCore_GetLayoutStats` 只复制最近一次结果。IANA 起始页得到 `580=515+65+0ms, pass=0`，进入 Reserved 后最后一次导航得到 `662=495+124+43ms, pass=1`，同批其余门禁均 OK。该批未改变布局次数、几何、滚动条判定或导航控制流；下一步细分约 500ms 的构盒阶段。
52. **next88 构盒热点已定位**：独立 `PCoreBoxStats` 把 box construction 分成 tree/backgrounds，并在 tree 内记录 style/text/image/anonymous/table-normalise 的互斥累计时间和调用数。设备上 IANA 起始页 tree/image=`523/518ms`，Reserved 为 `481/474ms`，backgrounds=0；热点是单张图片 retained object 创建，不是 DOM 遍历或表格归一化。
53. **next89 图片分派与文档内 retained reuse 已验收**：不新增解码器；XML-like 字节先走已有 `PImage_CreateSvgFromMemory`，失败仍回退 WM Imaging。document image cache 除原始字节外接管 retained bitmap/SVG 或失败状态，重排 carrier 只借用句柄。设备确认 TEST20/27 二次 layout 的 4/4 与 1/1 reuse、TEST27 首次 SVG-first 及其余默认门禁全部通过；TEST13 首次/随后页面 image 为 `469/37ms`。首屏冷启动仍待细分，该优化不跨 document、导航或线程。
54. **next90 首次 SVG 创建分段已取得设备结论**：`positron_image` ABI 1.5 在 retained SVG 内记录 wrapper/diagram setup、`svgtiny_parse`、`pimage_raster_create` 与 total；按句柄查询避免全局诊断状态。core 将数据挂在现有 document image cache，并用独立 API 汇总。next91 设备数据中 TEST27、IANA Example、Reserved 的 parse 分别占创建总时长的 `58/59`、`37/37`、`593/595ms`，raster 最多 2ms；下一优化目标是避免跨 document 重复解析相同 SVG，或定位 libsvgtiny/libdom XML 的巨大抖动，不是改绘制器。
55. **next91 无人值守设备 testbench 已通过首轮验收**：`test_host.ini` 的 `auto=1` 直接执行配置编号、抑制所有选择/结果 MessageBox，并将原始 INFO/ERROR 覆盖写入 EXE 同目录 `test_host.log`。全部可视 TEST 复用公共窗口和原测试函数，首帧后走正常销毁；TEST13 通过既有导航事务依次加载 example.com、IANA Example Domains 和 Reserved Domains。配置的 TEST13/20/27/43/44/56/58-62 全部 PASS。自动首帧冒烟不冒充人工视觉验收；后续候选收紧导航日志作用域，避免把 TEST44 预期失败记成 TEST13 ERROR。
56. **next92 重叠文档 SVG 复用已通过设备门禁**：参考 NetSurf `hlcache` 的内容条目/使用者分离，不引入完整浏览器缓存。相同 URL、长度与双哈希的 SVG 在旧页和待提交新页同时存活时共享 retained handle；document 析构减引用，最后一个引用释放时立即销毁。TEST13 Reserved 页从重新创建 `593ms parse` 变为 `image reuse=1, creates=0, image=2ms`；新增 TEST63 证明释放首文档后第二文档仍能得到正确红绿像素。TEST44 日志作用域也已修正，next92 配置的 TEST13/20/27/43/44/56/58-63 全部 PASS。
57. **next93 checkbox/radio 基础交互已通过设备门禁**：`PCore_FormActivateAt` 沿正式盒树命中控件，checkbox 切换、disabled 消费但不改变；radio 按 libdom form owner 与 `name` 分组，取消同组旧选项并同步 checked 状态。宿主只失效变更控件的联合区域。TEST64 自动检查同组互斥、跨组/跨表单隔离、已选项幂等以及纵横屏重排保持；同批 TEST13/20/27/43/44/56/58-64 全部 PASS。该项不代表焦点、label 激活、键盘、文本编辑、select 或提交已经存在。

验收：

- TEST 17 可见 `Image fallback: Logo`。
- TEST 18 显示 `image cache: first=2/2 second=2/2; fetch calls=2`。
- TEST 19 显示 BMP/PNG/JPEG/GIF 四行，验证直接调用 `positron_image.dll`、清空调用方输入后仍可重复绘制、损坏输入拒绝及旧 `PCore_Image*` 转发。
- TEST 20 显示 BMP/PNG/JPEG/GIF 四个相同的红/绿/蓝/黄图块，横竖屏一致且没有 fallback text（2026-07-12 真机确认）。
- 本地 HTML + 小 PNG/JPEG 能显示。
- 真实网页 logo/图片不再空白。

测试交付按能力批次进行：默认先积累多项相关实现、自动断言和直绘/正式链两层回归，再生成一个配置包含多个 TEST 的设备包。只有真实编译错误、高风险回归定位或设备专有问题才拆成单项测试，避免把每个微小提交都转化为人工验收负担。

### 2. Resource loader

当前外部 CSS、嵌套 `@import`、`<img>` 和计算后的单背景 URL 已通过分阶段 worker 事务拉取；core 仍只接收 transport-agnostic fetch/resolve callback。

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
- 有效表格的 colspan、有限/自动 rowspan 与 row-group 占位（TEST46 已真机确认）
- table/row-group/row/cell 匿名包装与短行空单元格生成（TEST47 已真机确认）
- 常见列表 marker、上游 47 种 counter formatter 与缓存图片 marker（TEST48-50/next61 已确认）
- inline-first `list-style-position:inside` 首行占位与悬挂换行（next62/TEST51 已验收）
- block-first、空条目和嵌套 inside marker 匿名首行（next63/TEST52 已验收）
- 随包静态 symbols/monochrome emoji fallback（next59 TEST49 已真机确认）

仍缺或简化：

- 基础 relative/absolute 已验收（TEST75）；float、sticky、复杂 containing-block 组合以及 Grid/flex/table 中全部定位交互仍缺
- float
- `border-collapse` 的 col/colgroup 来源及更多复杂表边界；next64-65/TEST53-54 已覆盖基本冲突、span 终止边与 row-group 边界
- overflow scrollbar 的惯性触摸、overlay 模式与更多嵌套组合
- float 邻接 marker、自定义 `@counter-style` 与完整 CSS Lists
- forms/widgets 剩余部分：`pattern`、类型语法、min/max/step/minlength、custom validity、`invalid` 事件、验证气泡、完整事件取消/传播与焦点状态；multipart/file、WM 多选列表与首批 required 已由 TEST70-72 通过设备门禁

建议按真实页面痛点推进，不一次性铺开。

### 4. 交互体验

主文档、外部 CSS 和图片 GET 已移出 UI 线程并由 TEST3/43/44/13 确认。HTML parse、style、document cache copy 与 layout 仍在完成消息的 UI 提交阶段执行，因此复杂页面在全部网络完成后仍可能短暂卡顿。

建议：

- 第一阶段验收：点击链接后不定量 loading 条持续移动，旧页仍可绘制和滚动；失败保留旧页，关闭窗口不会遗留网络线程。
- 第二阶段验收：TEST43 通过；真实 TEST13 在 CSS/图片等待期间进度条与旧页持续响应，成功后一次 swap，失败资源保留 fallback。
- 谨慎跨线程碰 DOM/GDI；确认线程安全前不让 worker 与 UI 共享 document 或全局 viewport context。

## 长期规划

目标：从“能渲染网页”走向“能写 Positron 应用”。

### 1. 浏览器 JavaScript runtime（独立 DLL 已提前落地，浏览器绑定仍属中期）

候选方向：

- Duktape 更现实：C、轻量、老平台友好。
- QuickJS 较现代，但工具链/体积/移植风险需评估。

仓库已包含 NetSurf 3.11 的 Duktape backend、bindings 和 WebIDL 素材，因此不从零手写解释器。next118 已先把 Duktape 2.7.0 封装为可供其他 WM 程序调用的 `positron_script.dll`，并以 TEST80 验证无浏览器依赖的基础执行闭环。浏览器绑定仍需单独的显式开关、生命周期、线程和错误策略；长期再扩大兼容范围。第一阶段不要追完整浏览器 JS：

- 简单 DOM 查询/修改。
- 点击事件。
- JS 调 native API。
- native 回调 JS。

### 2. WM 现代基础设施与 Positron App API

继续扩展可被任意 WM 程序独立使用的 DLL 生态，而不只服务浏览器或 Positron App：

- `positron_tls`
- `positron_json`
- `positron_http`
- `positron_core`
- `positron_image`（公共 DLL；SVG retained object、抗锯齿 draw 与缓存 `<img>` 已由 TEST25-27 真机确认）

API 要能被外部 WM6 C app 消费，不只服务 test_host。LocalSend WM6 port 是这个原则的现实驱动。

浏览器/应用运行时是这些基础设施的组合消费者。新增协议、解析器、编解码器或 runtime 时，优先联网寻找并移植成熟开源库；公共 DLL 负责稳定 C ABI 与 WM 平台适配。

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

1. 以 next134 的 TEST13/20/27/43/44/56/58-77/80-99 设备日志作为已验证自动化基线；先运行 next135 的 TEST100-104，再将长度约束纳入基线。后续每批继续以 TEST13 深层导航和旋转作为浏览器门禁。
2. 在 next135 设备验收后，评估把独立 DLL 接入显式的浏览器 JavaScript 开关；在 JS 默认关闭期间不得让 TEST13 平白增加脚本网络请求。
3. 下一批按“一个上游能力一个批次”评估基础 Grid 或背景尺寸，优先选择能让更多真实页面从“没有”变成“可用”的上游纵切。撤回的 TEST23/79 实验不得原样恢复。
4. 高级约束验证、专用事件数据与完整 HTML activation 继续保留，但不先于重大布局/资源缺口。真实触屏 label/Enter/multiple select、原生文件选择器、首个无效控件反馈和控件视觉验收放入后续人工检查批次。
5. 独立 DLL 通过设备门禁后，中期再利用仓库已有 NetSurf Duktape backend 做浏览器 JavaScript 最小纵切：脚本执行、DOM 查询/修改、点击事件和 native bridge；浏览器 JavaScript 默认仍保持关闭直到绑定路径逐项设备门禁通过。
6. 再扩展 cookies/history/storage 等浏览器与公共 DLL 基础设施。首屏 SVG 冷启动、整页聚合进度、视觉微调、高级 SVG/CSS 边角和全面性能优化后置；崩溃、数据错误或阻塞交互仍随时提到最高优先级。

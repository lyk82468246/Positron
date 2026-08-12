# 已验证基线与待消除限制

更新时间：2026-08-12

这份清单把“已经在设备上验证的最小链路”和“当前刻意保留的阶段性实现”分开记录。未被列为完成的项目不得在后续交接、README 或测试结论中表述为完整浏览器能力。

**状态更正（next145，2026-08-08）**：TEST111 已在 `screen=320x320 dpi=128` 设备通过，
因此 external classic script 的 DOM 顺序执行与异步取回不再属于“待设备验收”。它仍然只在
显式 `javascript=1` 时生效，默认 `javascript=0` 的 TEST13 不扫描、抓取或执行脚本。

**当前状态更正（next132，2026-08-07）**：next131 的 `screen=320x320 dpi=128`
日志中 TEST13 三段导航完成，但 TEST20 返回 `48x48`，严格动态 DPI 期望为 `64x64`。
next132 已把设备视口决定和单位上下文快照提前到正式构盒之前，并在 TEST20/27 的
样式完成后重新绑定设备视口；ARMV4I 增量构建通过，设备复测仍待完成。不能用放宽
断言或固定 96 DPI 宣称修复。

**历史状态更正（next134，2026-08-07）**：next132 在 `480x640 dpi=192` 下于
TEST58 停止。失败值 `article=320 rows=40/80/40` 是该离线几何测试继承了前一个
设备-backed render 的待布局上下文；next133 的固定 `230x260 @ 96 DPI` 隔离方案已
撤销。next134 让 TEST58 直接使用运行时屏幕/DPI，并把固定 CSS 长度换算为物理断言；
ARMV4I 构建/staging 通过，随后在 `screen=240x320 dpi=96` 设备日志中通过；TEST58
的断言和布局实现均未放宽或改写。

**当前状态更正（next135，2026-08-07）**：next134 的 TEST13/20/27/43/44/56/58-77/80-99
已在 `screen=240x320 dpi=96` 设备日志中通过。next135 新增 text/password/textarea 的
`minlength`/`maxlength` validity 及 TEST100-104；ARMV4I 构建/staging 已通过，随后随
next142 默认批次在同一 `screen=240x320 dpi=96` 设备确认。next143 的受限 ASCII
`pattern` adapter 与 TEST105-109 已在 `screen=480x640 dpi=192` 默认批次确认；首轮
TEST108 暴露并修复了 `tiny-regex-c` 对字符类末尾字面量连字符的错误处理。该能力仍
不是完整 JavaScript RegExp；
email/url/number 类型约束、range/custom validity 和 `invalid` 事件仍未实现，不能把这批
表单检查表述为完整 HTML Constraint Validation。

**当前自动化设备基线（next197，2026-08-12）**：`screen=240x240 dpi=96` 自动日志完成
TEST13/20/27/43/44/56/58-77/80-165，配置所选 112 项全部 OK、零 ERROR、零 FAIL、最终
TESTBENCH PASS；TEST13 使用 `OK (overview)`，其余 111 项使用标准数字 OK 行。
next167 的高 DPI interaction restyle 修复和 Learn More/SIP 人工结果继续有效；next168
新增成功-GET URL 历史与左键后退，next169 新增最小脚本 location/history 后退桥。
人工视觉/交互门改为累计若干风险批次后集中执行。
默认 `javascript=0`；完整 DOM/window、任意 OEM IME 和全站视觉仍未实现。

**next145 设备验收记录（2026-08-08）**：`PCore_GetScriptCount/PCore_GetScript` 按 DOM
顺序统一枚举 inline/external script；开启浏览器脚本时，external body 通过已有 document
cache 的资源 worker round 取回，再与 inline body 共用一次初始 Duktape context。TEST111
覆盖成功/失败 external、JSON 跳过和顺序结果；ARMV4I 构建、C89、审计与
`screen=320x320 dpi=128` 设备验收均已通过。默认 `javascript=0` 时该路径完全不扫描、
不抓取、不执行。

**next146 设备验收（2026-08-08）**：显式 `javascript=1` 时，导航请求会把初始
classic-script runtime 和最小 DOM bridge 绑定到待提交 document；成功导航整体换入，
失败导航、旧文档释放和窗体关闭清理。TEST112 离线确认后续求值可复用脚本状态、修改
`textContent` 并重新进入 style/layout。这个候选没有开启事件、异步任务、getter、完整
window 生命周期或完整 DOM binding；默认 `javascript=0` 及 TEST13 网络路径保持不变。
C89、ARMV4I 增量构建和仓库审计已通过；`screen=240x320 dpi=96` 设备日志记录 TEST112
OK 与 `TESTBENCH PASS`。它仍不是完整浏览器 JavaScript 能力。

**next147 设备验收（2026-08-08）**：显式 `javascript=1` 页面新增最小
`addEventListener/removeEventListener` bridge，WM 点击复用 Core 的可信 DOM event
dispatch；handler 可以读取基础事件字段、更新 DOM，并调用 `preventDefault()` 阻止既有
默认动作。TEST113 覆盖 listener、取消、重新布局和移除后的第二次派发。默认
`javascript=0`、TEST13 网络路径和默认动作保持不变；键盘、焦点、输入、异步任务、完整
Event 对象和完整 HTML activation 仍未实现。C89/ARMV4I 构建已通过；`screen=480x640 dpi=192`
设备日志记录 TEST113 OK 与 `TESTBENCH PASS`。

**next148 设备验收（2026-08-08）**：显式 `javascript=1` 页面接入原生 EDIT/SELECT 的
`focus`、`blur`、`input`、`change` 事件；input/change 可冒泡，focus/blur 不冒泡，四类
事件不可取消。TEST114 已离线与设备验证事件元数据、父级冒泡、可信标志和 DOM 更新；C89、
ARMV4I 增量构建、staging 与 `screen=320x320 dpi=128` 设备验收均已通过。键盘事件、focusin/focusout、异步任务、
完整 Event 对象、完整 HTML activation 和完整 DOM binding 仍未实现。

**next149 设备验收（2026-08-08）**：公开 Core 键盘事件数据 ABI，并把
WM 原生 EDIT 的 `WM_KEYDOWN/WM_KEYUP` 接入显式 `javascript=1` 页面；TEST115 离线验证
`key/keyCode/charCode/repeat/shiftKey/ctrlKey/altKey` 和可信标志。该候选尚未纳入设备基线，
`screen=320x320 dpi=128` 设备日志已确认通过；仍不覆盖 WM SELECT、`WM_SYSKEY*`、
`keypress`、`beforeinput`、`focusin/focusout`、字符输入/IME 或完整 Keyboard/Event API。
默认 `javascript=0`、TEST13 网络路径和 next148 已验收的表单事件保持不变。

**next150 设备验收（2026-08-08）**：显式 `javascript=1` 页面在原生
EDIT/SELECT 的既有 `focus/blur` 生命周期点追加可冒泡的 `focusin/focusout`，事件保持
`trusted=true`、`cancelable=false`；TEST116 离线与 `screen=320x320 dpi=128` 设备日志
验证目标/冒泡阶段、元数据和事件后布局。C89、ARMV4I 增量构建、staging 与设备验收均已通过。
这不代表完整焦点转移顺序、异步任务、`beforeinput`、WM SELECT 键盘变化、字符输入/IME
或完整 Keyboard/Event API 已实现；默认 `javascript=0` 和 TEST13 网络路径不变。

**next151 设备验收（2026-08-08）**：显式 `javascript=1` 页面在原生
EDIT 的字符、换行、退格、删除、粘贴、剪切和清除动作前派发可冒泡、可取消的
`beforeinput`；`inputType/data` 通过 `PCoreInputEventData` 进入最小 JavaScript 事件对象，
`preventDefault()` 会阻止对应的原生默认动作。TEST117 已通过离线断言，覆盖 target/bubble、
可信元数据、取消插入而允许删除以及事件后布局；C89、仓库审计、ARMV4I 增量构建、设备
staging 与 `screen=320x320 dpi=128` 真实设备验收均已通过，日志记录 `TESTBENCH PASS`。
WM SELECT 键盘、IME/composition、完整 Unicode/剪贴板 payload、`keypress` 和完整
Input/Keyboard/Event API 仍未实现；默认 `javascript=0` 与 TEST13 网络路径不变。

**next153 设备验收（2026-08-08）**：原生 EDIT/SELECT 的可识别
`WM_CHAR` 已在显式 `javascript=1` context 中接入可取消 `keypress`，TEST119 覆盖
synthetic SELECT、真实 EDIT/SELECT WM 消息、target/bubble 元数据和取消 SELECT 默认
动作。C89、仓库审计、ARMV4I 增量构建、staging 和 `screen=640x480 dpi=192` 设备日志
均已通过；该桥只覆盖 ASCII，不覆盖 Unicode/IME、`WM_SYSCHAR` 或完整 Keyboard/Event
API。

**next154 设备验收（2026-08-08）**：新增原生 EDIT/SELECT
`WM_SYSKEYDOWN/UP` 与 ASCII `WM_SYSCHAR` 的 system-key 事件桥，TEST120 覆盖
`altKey`、target/bubble、取消 SELECT 默认动作和现有键盘元数据。该实现已通过 C89、
仓库审计、ARMV4I 增量构建、staging 和 `screen=640x480 dpi=192` 设备日志均已通过，
日志记录 `TESTBENCH PASS`；它不实现 IME/composition、Unicode 输入、Imm32 API 或完整
Keyboard/Event API。默认
`javascript=0` 与 TEST13 网络路径不变。

**next155 首次设备失败（2026-08-08，已替代）**：TEST121 首次设备包失败的根因是
事件回调的旧安全过滤器把合法 UTF-8 高位字节清空；不是断言放宽或 TEST13 网络回归。
该包不能作为基线。

**next156 设备验收（2026-08-08）**：事件回调对 `inputType`、`data`、`key` 使用
JSON 字符串转义，保留合法 UTF-8 并转义 JSON 特殊字符；单个 BMP `WM_CHAR` 的 UTF-8
`keypress` 与 EDIT `beforeinput.data` 桥、TEST121 的 `→`/`★` key/code、target/bubble
和取消 SELECT 默认动作保持。C89、仓库审计、ARMV4I 增量构建、staging 和
`screen=640x480 dpi=192` 设备日志均已通过并记录 `TESTBENCH PASS`；代理对、IME/composition、
完整 Unicode 输入和字体覆盖仍未实现。默认 `javascript=0` 与 TEST13 网络路径不变。

**next157 设备失败（2026-08-08，不能作为基线）**：显式脚本 context 中，原生 EDIT/SELECT 各自缓存
一个 high-surrogate；匹配 low-surrogate 后才向 Core 派发一次 Unicode 标量
`keypress`，EDIT 再派发一次 `beforeinput(insertText)`，其 `data` 为完整 UTF-8 payload。
TEST122 同时检查 JavaScript 的两个 UTF-16 code unit、标量 keyCode/charCode、target/bubble
和取消 SELECT 默认动作。next157 的设备日志中 TEST122 失败，而既有回归通过；未配对代理项
会回退原生窗口过程。不能宣称实现 IME/composition、组合输入、剪贴板完整 Unicode、字体
覆盖或完整 Keyboard/Event API。

**next158 诊断完成（2026-08-08）**：TEST122 的标量 keyCode/charCode 正确，但直接
注入的四字节 UTF-8 在 Duktape 中得到 length 1 和 non-BMP `charCodeAt(0)`，没有形成
ECMAScript UTF-16 代理对；TEST13 与 TEST20-121 通过。

**next159/160 状态（2026-08-08）**：next159 的事件 JSON 把合法 non-BMP UTF-8 写成
两个 `\uXXXX` 代理项，设备实际结果已得到正确 CESU-8/UTF-16、标量代码和事件传播。
TEST122 失败来自测试把 SELECT target 的取消状态提前写为 `true`；next160 只修正为
target `false`、bubble `true`，完整设备日志已通过并成为基线。默认 `javascript=0` 和
TEST13 不变；仍不能宣称剪贴板完整 Unicode 或字体覆盖已实现。

**next161 IME composition 候选（2026-08-08）**：原生 EDIT 已接入 WM6 composition
消息并用 `ImmGetCompositionStringW` 将 UTF-16 组合串转为 UTF-8，显式脚本 context 可收到
`compositionstart/update/end` 与不可取消的 `beforeinput(insertCompositionText)`。TEST123
只验证 WM start/end 消息入口和正式共享 update 发射路径；它不创建虚假的 IME context，
所以不能证明任意 OEM SIP、候选窗口、预编辑 UI、`isComposing`、selection replacement、
组合期间的全部 `input` 顺序或完整 CompositionEvent/InputEvent API 已兼容。

**next161/162 网络验收边界（2026-08-09）**：next161 首轮设备运行在 TEST13 第三段
Reserved Domains 的主文档 TLS 握手阶段收到 peer EOF，因而 TEST123 未执行。next162
只对此类幂等 GET 的 `status=0 + empty body + ssl_handshake EOF` 做最多一次重试；它不是
通用网络恢复、断点续传或无限重连，不覆盖 POST、DNS、HTTP 状态失败、子资源请求，也不
掩盖第二次失败。日志的 retry 计数用于暴露恢复事实，而不是把网络失败当作功能断言通过。

**next163 脚本表单属性设备边界（2026-08-09）**：TEST124/125 的 size-tagged Ex ABI
补充 InputEvent/KeyboardEvent.isComposing，TEST126-128 又补充最小 DOM text/
attribute、input/textarea/select.value 和 live checkbox/radio.checked。
这些桥只在显式 javascript=1 的 classic script context 中注册，仍是同步、按
getElementById 的窄 ABI；getter 结果仍受脚本 JSON 回调的 255 字节有效载荷限制，
大文本/大属性可能读取失败。value/checked 修改发生在 DOM 层，已有 styled box 不会
自动重排；浏览器宿主需要随后重新 style/layout。完整 HTMLFormElement、files、
selectedOptions、change/input 自动事件、activation、focus、异步脚本、
window/fetch/network binding、CSP 和完整 DOM 均未实现。defaultValue/defaultChecked
现在仅有 TEST133-134 覆盖的 input/textarea/input 最小桥；它们仍不是完整
HTMLFormElement reset、change/input 事件或 activation 实现。vendored
libdom 的 checked live-state 修正已完成设备自动回归；`C:\WMShare\Positron-next163\test_host.log`
以 `TESTBENCH PASS` 结束。该日志不等于真实 SIP/IME 候选窗口或视觉效果已人工验收。

**next164 脚本 Event/DOM token/style（设备验收通过）**：TEST129-132 增加
同步事件的 target/currentTarget ID、id/className、classList 以及受控 style 声明
方法。事件 ID 在 callback 返回前有效；style 只处理简单分号/冒号声明，不实现
priority、CSSOM、computed style 或布局自动重排。新桥仍只按 getElementById 工作，
只在显式 javascript=1 的 classic context 注册；next164 日志以 TESTBENCH PASS 结束。

**next165 脚本表单默认属性（设备失败，已由 next166 修复）**：TEST133-135 增加
defaultValue/defaultChecked 和 selectedIndex 的读写、-1 清空与越界拒绝；
但六个新增 JS 原生入口使 TEST110 的 DOM bootstrap 超过既有 16 槽位上限，
因此这些测试没有在该包中执行。next166 将它们合并到一个按操作分发的 bridge
入口；属性仍按 getElementById 工作，不要求 style/layout box，selectedIndex
写入只更新 DOM option 状态，已有 styled box 不会自动重排。next166 的
320x320/128 DPI 设备自动回归已通过。

**next167 高 DPI 交互重排边界（设备及定向人工验收通过）**：真实链接点击会经过
focus/active restyle，而 TEST13 的 direct navigation 不经过这段；旧宿主可能在后续
layout 把物理宽度退化成 CSS 视口，造成 example.com 离开页贴左/溢出。宿主现于交互
restyle 前重申设备宽高和 DPI，TEST76 覆盖 640x480/192 DPI 的两次连续重排；用户人工
确认 Learn More 点击后离开页仍保持居中边距，next167 480x640/192 DPI 自动门禁也通过。
同包真实 SIP 候选词点击也已由用户人工确认可完整键入候选词。这仍不代表任意站点、
旋转时刻、所有视觉细节或任意 OEM IME 行为均已验收。

**next168 Browse 后退边界（自动设备验收通过，人工检查已累计）**：宿主只保存最多 16 个成功 GET URL，
按左方向键会重新联网加载上一项；失败不移动位置，POST 不入栈，回退后新导航截断前向
分支。next168 自身没有页面/document 缓存、前进 UI、滚动/表单状态恢复、重载确认、
持久历史、重定向历史语义或 JavaScript API；next169 只补最小读取/后退桥。TEST136 已在设备自动通过；真实
公网后退交互与失败网络条件仍在累计人工检查清单中。

**next169 最小脚本导航桥（自动设备验收通过）**：显式 `javascript=1` 的同一页面 Duktape
context 现在可读取 `location.href`、`document.URL/documentURI/location`，并调用
`history.back()` 请求 next168 的后退加载。URL 是 bootstrap 时固定的当前 document URL；
back 经窗口消息延迟到 JS callback 返回和导航空闲之后，调用本身不立即修改 history index。
TEST137 已在 320x320/128 DPI 自动日志中通过，整批为 85 OK、零 ERROR 与最终 PASS；
它只证明 URL 身份和延迟请求边界。next170-174 已验收赋值、reload、replace、forward 和
有界 go 纵切；next175 已验收补充只读 length；next176 已验收补充初始 null state；next177
已验收受控 JSON-only replaceState。仍无 URL 分量、pushState、完整 structured clone、
非当前 URL 改写、popstate、重定向历史、页面缓存、滚动/表单状态
恢复或持久历史。默认 `javascript=0` 与 TEST13 网络路径不变。

**next170 location 赋值桥（自动设备验收通过）**：`location.assign()`、`location.href=`、
`window.location=` 和 `document.location=` 共用一个 native callback，把最后一个非空、少于
1024 字节的 URL 请求保存在页面 bridge 中。窗口消息只在导航空闲时取出请求并调用现有 GET
入口，避免从 Duktape callback 同步重入；连续赋值是 last-request-wins，当前 URL/history
在赋值时保持不变。TEST138 是离线 bridge 门，不证明 URL 解析、网络成功、重定向或页面状态
恢复；next171 已验收纵切补充 reload，next172 已验收纵切补充 replace；URL 分量 setter、
next173 已验收纵切补充 `history.forward()`；next174 已验收纵切补充有界 `history.go()`；
next175 已验收补充只读 `history.length`；next176 已验收补充初始只读 null state；next177
已验收受控 JSON-only `history.replaceState()`。
320x320/128 DPI 日志已得到 TEST138 OK、86 条 OK、零 ERROR、零 FAIL 与最终 PASS。

**next171 GET-only location reload（自动设备验收通过）**：`location.reload()` 把
bootstrap 闭包中的 canonical document URL 复制进既有异步导航 bridge；窗口消息在导航
空闲后复用普通 GET 入口。callback 返回前不修改 URL/history，同一当前 URL 成功提交时
既有 duplicate-current guard 保留 count/index 与 forward branch。TEST139 是离线 bridge/
history 门，不证明真实网络重载、缓存验证、POST 重提交、表单/滚动状态恢复或完整 Reload
语义；`forceGet` 参数也未实现。320x320/128 DPI 日志已得到 TEST139 OK、87 条 OK、
零 ERROR、零 FAIL 与最终 PASS。

**next172 GET-only location replace（自动设备验收通过）**：`location.replace(url)`
复用异步 navigation bridge；URL 仍受非空和小于 1024 字节约束。只有 GET document 成功
换入后才以具名 replace-current 模式改写当前 history URL；失败保持旧项，成功保留
count/index 和现有 back/forward 条目。TEST140 是离线 callback/history 门，不证明真实网络
replace、重定向历史、POST replace、页面缓存或滚动/表单状态恢复。320x320/128 DPI 日志
已得到 TEST140 OK、88 条 OK、零 ERROR、零 FAIL 与最终 PASS。

**next173 history.forward（自动设备验收通过）**：forward target 查询不移动 index，
脚本请求经既有异步消息入口启动目标 URL 的 GET，只有成功 document 提交才更新 index；
失败保持当前项。TEST141 是离线 bridge/history 门，不证明真实网络前进、右方向键 UI、
页面缓存、滚动/表单状态恢复、`history.go/length/state` 或 popstate 语义。320x320/128 DPI
日志已得到 TEST141 OK、89 条 OK、零 ERROR、零 FAIL 与最终 PASS。

**next174 bounded history.go（自动设备验收通过）**：只接受整数 `-15…15`；非有限、
小数和越界输入无操作，合法偏移经既有异步桥排队。delta 0 指向当前 GET 条目，其他偏移
只在目标存在时导航，成功 document 提交后才移动 index。TEST142 是离线 target/bridge 门，
不证明真实网络 go、POST 重提交、页面缓存、状态恢复、history length/state 或 popstate。
320x320/128 DPI 日志已得到 TEST142 OK、90 条 OK、零 ERROR、零 FAIL 与最终 PASS。

**next175 read-only history.length（自动设备验收通过）**：bootstrap 暴露当前 document
成功提交后的预期历史长度快照，首次 document 至少为 1、最多为现有宿主的 16 项。
普通成功 GET 预先反映新增条目和 forward 分支截断；back/forward/go、replace、POST、
失败提交或脚本同步赋值不增加长度。TEST143 是离线 projection/bootstrap 门，不证明
真实网络遍历、POST 历史、跨 document 状态、history state/push/replaceState、popstate、
页面缓存或滚动/表单恢复。
320x320/128 DPI 日志已得到 TEST143 OK、91 条 OK、零 ERROR、零 FAIL 与最终 PASS。

**next176 initial history.state（自动设备验收通过）**：在尚无 state mutation API 时，
bootstrap 为初始/网络 document 暴露只读 null；脚本赋值与同步 go(0) 排队不改变 state
或当前 history 条目。TEST144 是离线 bootstrap/bridge 门，不证明 pushState、replaceState、
structured clone、跨 document state、popstate、同文档 URL 历史或页面状态恢复。
320x320/128 DPI 日志已得到 TEST144 OK、92 条 OK、零 ERROR、零 FAIL 与最终 PASS。

**next177 JSON-only history.replaceState（自动设备验收通过）**：新增不改 URL 的受控
`history.replaceState(state, title)`。state 必须能序列化为小于 1024 字节的 JSON，title
忽略，第三个 URL 只允许省略、空串或当前绝对 URL；getter 每次返回 JSON clone，修改返回
对象不会回写。初始脚本只把 state 留在候选 bridge，document 最终成功提交后才写入对应
成功 GET 条目；活动页面同步替换当前条目，遍历/重载按条目恢复且不增加 length。TEST145
离线覆盖 clone 隔离、URL 拒绝、成功提交、活动替换、逐项恢复和 14/16 callback 槽位。
本批不是完整 structured clone，也不实现 pushState、非当前 URL 改写、popstate、POST state
或页面缓存。320x320/128 DPI 日志已得到 TEST145 OK、93 条 OK、零 ERROR、零 FAIL 与最终
PASS，next177 已成为自动设备基线。

**next178 same-URL JSON-only history.pushState（自动设备验收通过）**：新增不联网的受控
`history.pushState(state, title)`。state 仍须序列化为小于 1024 字节的 JSON，title 忽略，
第三个 URL 只允许省略、空串或当前绝对 URL；调用同步追加条目、更新 length/state、截断
forward 分支，最多保留 16 项。初始 GET 脚本的多次 push/replace 只记录在候选 bridge，
document 最终成功后才按顺序提交，活动页立即提交。TEST146 离线覆盖成功提交隔离、多次
操作顺序、clone、URL 拒绝、同步 length、活动追加、前向截断、逐项恢复和 14/16 callback
槽位。本批遍历仍走现有 GET 重载，不是完整 structured clone，也不实现非当前 URL、POST
state、同 document 生命周期、popstate 或页面缓存。320x320/128 DPI 日志已得到 TEST146 OK、
94 条 OK、零 ERROR、零 FAIL 与最终 PASS，next178 已成为自动设备基线。

**next179 same-document traversal（自动设备验收通过）**：成功网络 document 获得内部
identity，pushState 条目继承当前 identity。back/forward 和非零 history.go 命中同一
identity 的 pushed sibling 时，在现有 DOM/runtime 中切换 index 和逐项 JSON state，不启动
GET，length 不变。go(0)、reload 和跨 identity 条目仍走网络路径，成功网络 document 获得
新 identity。TEST147 离线覆盖 back/forward/go 无 GET 切换、DOM/runtime 身份、state/length、
go(0) 排除以及 reload/跨 document 隔离。本批不派发 popstate，也不实现逐项滚动/表单恢复、
非当前 URL、POST state 或跨 document 页面缓存。320x320/128 DPI 日志已得到 TEST147 OK、
95 条 OK、零 ERROR、零 FAIL 与最终 PASS，next179 已成为自动设备基线。

**next180 minimal popstate 基线（2026-08-09 已设备验收）**：同 document traversal 在切换 index/state
后派发 popstate；支持 window.onpopstate 以及仅面向 popstate 的 window add/removeEventListener。
事件 state 是独立 JSON clone，target/currentTarget 为 window，bubbles/cancelable 为 false，
handler 异常被隔离；pushState/replaceState 本身不派发。TEST148 离线覆盖异步 back 边界、
state-before-event、属性/listener、重复去重、remove、clone/异常隔离、元数据和 push/replace
静默。本批不是完整 Window EventTarget 或 PopStateEvent 构造器，跨 document traversal 仍
不派发，也不实现逐项滚动/表单恢复、非当前 URL、POST state 或页面缓存。320x320/128 DPI
日志已得到 TEST148 OK、96 条 OK、零 ERROR、零 FAIL 与最终 PASS，next180 已成为自动设备基线。

**next181 history 片段 URL 基线（2026-08-09 已设备验收）**：replaceState/pushState 可接受当前 document
基础 URL 上的 #fragment、无片段基础 URL 或同基础 URL 绝对形式，并同步 location.href、
document.URL/documentURI 与 history entry；同 document traversal 在 popstate 前恢复 URL，
不会启动 GET。TEST149 覆盖初始/运行期状态、前向分支截断、片段清除/恢复和路径/查询/跨源
拒绝。本批仍不是通用 URL 解析器：普通相对 URL、路径/查询变化、hashchange、滚动/表单恢复、
POST state 及跨 document 页面缓存不在范围内。320x320/128 DPI 日志已得到 TEST149 OK、
97 条 OK、零 ERROR、零 FAIL 与最终 PASS，next181 已成为自动设备基线。

**next182 minimal hashchange 基线（2026-08-09 已设备验收）**：片段发生变化的同 document history
traversal 在 popstate 后派发 hashchange；支持 onhashchange 与 window hashchange listener，
oldURL/newURL 和基础事件元数据已填充，handler 异常被隔离。pushState/replaceState 与相同
片段 traversal 不派发。TEST150 覆盖顺序、重复/remove、不可取消语义与静默边界。本批不含
location.hash/片段赋值、跨 document hashchange、HashChangeEvent 构造器、完整 Window
EventTarget、逐项滚动/表单恢复或页面缓存。320x320/128 DPI 日志已得到 TEST150 OK、
98 条 OK、零 ERROR、零 FAIL 与最终 PASS，next182 已成为自动设备基线。

**next183 location URL 组件基线（2026-08-09 已设备验收）**：location 暴露动态只读 protocol、host、
hostname、port、pathname、search、hash 和 origin，并随 history 片段 entry 同步。TEST151
覆盖绝对 HTTPS URL 的显式端口、路径/查询/片段、origin、只读 descriptor 与遍历。本批解析器
不等价于完整 URL 标准：组件 setter、username/password、默认端口归一化、通用相对 URL、
location 片段导航及其他 scheme 不在范围内。配置读取上限已由 2048 提升到 4096 字节；
320x320/128 DPI 日志已得到 TEST151 OK、99 条 OK、零 ERROR、零 FAIL 与最终 PASS，next183
已成为自动设备基线。

**next184 location.hash 导航基线（2026-08-09）**：hash setter 排队执行同 document 片段
导航，新增 null-state history entry 并派发 hashchange；相同值不新增历史或事件，空字符串
清除片段，导航不发起 GET/popstate。TEST152 覆盖异步边界、history.length/state、事件与后退。
本批不含 location.href/assign/replace 相对片段、百分号编码/标准化、锚点滚动、跨 document
片段导航及其他 location 组件 setter。320x320/128 DPI 日志得到 TEST152 OK、配置所选 99 项
全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 的成功行是 `OK (overview)`，其余 98 项
使用标准数字 OK 行。

**next185 href/assign/replace 片段引用基线（2026-08-09）**：以 `#` 开头的 href setter 与
assign 调用排队新增 null-state 同 document entry，replace 调用排队替换当前 entry；它们延迟
更新 location/history，仅派发 hashchange，不发起 GET/popstate，相同目标静默。TEST153 覆盖
三入口、replace 后 length/document identity/state、后退事件顺序及无网络。其他相对/绝对 URL
仍走既有跨 document 导航；百分号编码/标准化、锚点滚动、跨 document 片段导航与其他组件
setter 不在本批。320x320/128 DPI 日志得到 TEST153 OK、配置所选 100 项全部 OK、零 ERROR、
零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 99 项使用标准数字 OK 行。

**next186 绝对同文档片段 URL 基线（2026-08-12）**：href/assign/replace 的绝对 URL 只有
在基址与当前 URL 完全相同且改变 fragment，或当前确有 fragment 时以绝对基址清除它，才走
同 document 队列。当前无 fragment 的同 URL 导航及 query/path/origin 不同目标保持普通导航。
TEST154 覆盖三入口、清除、history/state、hashchange、无网络与分类边界。相对 path+fragment、
百分号编码/标准化、锚点滚动和其他组件 setter 不在本批。320x320/128 DPI 日志得到 TEST154
OK、配置所选 101 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，
其余 100 项使用标准数字 OK 行。

**next187 根相对同文档片段 URL 基线（2026-08-12）**：根相对 href/assign/replace URL 只有
在解析后 path/query 与当前基址完全相同且改变 fragment，或当前确有 fragment 时以匹配根相对
基址清除它，才走同 document 队列。当前无 fragment 的同 URL 根相对导航及 query/path 不同
目标保持普通导航。TEST155 覆盖三入口、清除、same-value、history/state、hashchange、无网络
与分类边界。query-only、普通 path-relative、dot-segment、百分号标准化、锚点滚动和其他组件
setter 不在本批。320x320/128 DPI 日志得到 TEST155 OK、配置所选 102 项全部 OK、零 ERROR、
零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 101 项使用标准数字 OK 行。

**next188 query-relative 同文档片段 URL 基线（2026-08-12）**：query-relative href/assign/replace
URL 只有在解析后 pathname/query 与当前基址完全相同且改变 fragment，或当前确有 fragment 时
以匹配 query 清除它，才走同 document 队列。当前无 fragment 的同 query 导航、不同 query 和
普通 path-relative 目标保持普通导航。TEST156 覆盖三入口、清除、same-value、history/state、
hashchange、无网络与分类边界。普通 path-relative、dot-segment、百分号标准化、锚点滚动和其他
组件 setter 不在本批。C89、ARMV4I Debug 构建及 `C:\WMShare\Positron-next188` 七个二进制
SHA-256 核对已通过。240x240/96 DPI 日志得到 TEST156 OK、配置所选 103 项全部 OK、零 ERROR、
零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 102 项使用标准数字 OK 行。

**next189 同目录 path-relative 同文档片段 URL 基线（2026-08-12）**：不带 `./` 或 `../` 前缀的
同目录 path-relative href/assign/replace URL，只有解析后 path/query 与当前基址完全相同且改变
fragment，或当前确有 fragment 时用匹配相对文件名清除它，才走同 document 队列。当前无
fragment 的同 URL、不同 path/query 和点段前缀目标保持普通导航。TEST157 覆盖三入口、清除、
same-value、history/state、hashchange、无网络与分类边界。点段归一化、百分号标准化、锚点滚动
和其他组件 setter 不在本批。C89 与 ARMV4I Debug 构建已通过；`C:\WMShare\Positron-next189`
七个二进制 SHA-256 全部匹配。首次运行暴露并修正 TEST156 过时的同路径负向断言；修复后
240x240/96 DPI 日志得到 TEST156/157 OK、配置所选 104 项全部 OK、零 ERROR、零 FAIL 与最终
PASS；TEST13 使用 `OK (overview)`，其余 103 项使用标准数字 OK 行。

**next190 单个 `./` 同目录片段 URL 基线（2026-08-12）**：带单个 `./` 前缀的 href/assign/replace
URL，只有移除前缀并解析后 path/query 与当前基址完全相同且改变 fragment，或当前确有 fragment
时用匹配目标清除它，才走同 document 队列。当前无 fragment 的同 URL、不同 path/query 和
`../` 父目录目标保持普通导航。TEST158 覆盖三入口、清除、same-value、history/state、
hashchange、无网络与分类边界。`../`、重复/混合点段归一化、百分号标准化、锚点滚动和其他组件
setter 不在本批。C89、ARMV4I Debug 构建及 `C:\WMShare\Positron-next190` 七个二进制
SHA-256 核对已通过。240x240/96 DPI 日志得到 TEST158 OK、配置所选 105 项全部 OK、零 ERROR、
零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 104 项使用标准数字 OK 行。

**next191 单个 `../` 父目录片段 URL 基线（2026-08-12）**：带单个 `../` 前缀的 href/assign/replace
URL，只有上移一个目录并解析后 path/query 与当前基址完全相同且改变 fragment，或当前确有
fragment 时用匹配目标清除它，才走同 document 队列。当前无 fragment 的同 URL、不同 path/
query 和重复 `../../` 目标保持普通导航。TEST159 覆盖三入口、清除、same-value、history/state、
hashchange、无网络与分类边界。重复/混合点段归一化、百分号标准化、锚点滚动和其他组件 setter
不在本批。C89、ARMV4I Debug 构建及 `C:\WMShare\Positron-next191` 七个二进制 SHA-256 核对
已通过。240x240/96 DPI 日志得到 TEST159 OK、配置所选 106 项全部 OK、零 ERROR、零 FAIL 与
最终 PASS；TEST13 使用 `OK (overview)`，其余 105 项使用标准数字 OK 行。

**next192 连续前导父目录片段 URL 基线（2026-08-12）**：连续多个前导 `../` 的 href/assign/replace
URL，只有逐级上移目录并解析后 path/query 与当前基址完全相同且改变 fragment，或当前确有
fragment 时用匹配目标清除它，才走同 document 队列；越过 origin 根的额外父目录段钳制在根。
当前无 fragment 的同 URL、不同 path/query 和混合 `.././` 目标保持普通导航。TEST160 覆盖三
入口、清除、same-value、history/state、hashchange、无网络与分类边界。混合/内嵌点段归一化、
百分号标准化、锚点滚动和其他组件 setter 不在本批。C89、ARMV4I Debug 构建及
`C:\WMShare\Positron-next192` 七个二进制 SHA-256 核对已通过。240x240/96 DPI 日志得到 TEST160
OK、配置所选 107 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，
其余 106 项使用标准数字 OK 行。

**next193 父目录后单个 `./` 片段 URL 基线（2026-08-12）**：连续前导 `../` 之后允许一个 `./` 的
href/assign/replace URL，只有逐级上移目录、移除该单点段并解析后 path/query 与当前基址完全
相同且改变 fragment，或当前确有 fragment 时用匹配目标清除它，才走同 document 队列。当前无
fragment 的同 URL、不同 path/query 和重复 `../././` 目标保持普通导航。TEST161 覆盖三入口、
清除、same-value、history/state、hashchange、无网络与分类边界。重复/任意内嵌点段归一化、
百分号标准化、锚点滚动和其他组件 setter 不在本批。C89、ARMV4I Debug 构建及
`C:\WMShare\Positron-next193` 七个二进制 SHA-256 核对已通过。240x240/96 DPI 日志得到 TEST161
OK、配置所选 108 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，
其余 107 项使用标准数字 OK 行。

**next194 父目录后连续 `./` 片段 URL 基线（2026-08-12）**：连续前导 `../` 之后允许连续多个 `./`
的 href/assign/replace URL，只有逐级上移目录、移除这些单点段并解析后 path/query 与当前基址完全
相同且改变 fragment，或当前确有 fragment 时用匹配目标清除它，才走同 document 队列。当前无
fragment 的同 URL、不同 path/query 和路径中部 `segment/../` 目标保持普通导航。TEST162 覆盖
三入口、清除、same-value、history/state、hashchange、无网络与分类边界。任意内嵌点段归一化、
百分号标准化、锚点滚动和其他组件 setter 不在本批。C89、ARMV4I Debug 构建及
`C:\WMShare\Positron-next194` 七个二进制 SHA-256 核对已通过。240x240/96 DPI 日志得到 TEST162
OK、配置所选 109 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，
其余 108 项使用标准数字 OK 行。

**next195 连续前导 `./` 片段 URL 基线（2026-08-12）**：无父目录前缀时允许连续多个前导 `./` 的
href/assign/replace URL，只有移除这些单点段并解析后 path/query 与当前基址完全相同且改变
fragment，或当前确有 fragment 时用匹配目标清除它，才走同 document 队列。当前无 fragment
的同 URL、不同 path/query 和路径中部 `segment/../` 目标保持普通导航。TEST163 覆盖三入口、
清除、same-value、history/state、hashchange、无网络与分类边界。任意内嵌点段归一化、百分号
标准化、锚点滚动和其他组件 setter 不在本批。C89、ARMV4I Debug 构建及
`C:\WMShare\Positron-next195` 七个二进制 SHA-256 核对已通过。240x240/96 DPI 日志得到 TEST163
OK、配置所选 110 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，
其余 109 项使用标准数字 OK 行。

**next196 父目录后单个内嵌 `./` 片段 URL 基线（2026-08-12）**：连续前导 `../` 逐级上移后，允许
余下路径中出现一个内嵌 `./` 的 href/assign/replace URL；只有移除该单点段并解析后 path/query
与当前基址完全相同且改变 fragment，或当前确有 fragment 时用匹配目标清除它，才走同 document
队列。当前无 fragment 的同 URL、不同 path/query 和连续内嵌 `././` 目标保持普通导航。TEST164
覆盖三入口、清除、same-value、history/state、hashchange、无网络与分类边界。多个内嵌点段、
内嵌父目录、百分号标准化、锚点滚动和其他组件 setter 不在本批。C89、ARMV4I Debug 构建及
`C:\WMShare\Positron-next196` 七个二进制 SHA-256 核对已通过。240x240/96 DPI 日志得到 TEST164
OK、配置所选 111 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，
其余 110 项使用标准数字 OK 行。

**next197 父目录后连续内嵌 `./` 片段 URL 基线（2026-08-12）**：连续前导 `../` 逐级上移后，允许
余下路径同一位置出现连续多个内嵌 `./` 的 href/assign/replace URL；只有移除该连续单点段并解析
后 path/query 与当前基址完全相同且改变 fragment，或当前确有 fragment 时用匹配目标清除它，
才走同 document 队列。当前无 fragment 的同 URL、不同 path/query、分离位置的多个 `./` 和
内嵌 `../` 目标保持普通导航。TEST165 覆盖三入口、清除、same-value、history/state、hashchange、
无网络与分类边界。任意多位置点段归一化、内嵌父目录、百分号标准化、锚点滚动和其他组件 setter
不在本批。C89、ARMV4I Debug 构建及 `C:\WMShare\Positron-next197` 七个二进制 SHA-256 核对已
通过。240x240/96 DPI 日志得到 TEST165 OK、配置所选 112 项全部 OK、零 ERROR、零 FAIL 与最终
PASS；TEST13 使用 `OK (overview)`，其余 111 项使用标准数字 OK 行。

**next152 设备验收（2026-08-08）**：原生 `COMBOBOX/LISTBOX` 已加入
`WM_KEYDOWN/WM_KEYUP` 子类桥，复用公开 `PCoreKeyEventData` 和按命中点派发 ABI；
TEST118 覆盖 SELECT 的 target/bubble 与 ArrowDown 元数据，并在窗口创建时发送真实 WM
消息。C89、ARMV4I 构建、staging 和 `screen=480x640 dpi=192` 设备日志均已通过；因此 SELECT 键盘、
IME/composition、`WM_SYSKEY*`、Unicode/IME keypress 和完整 Keyboard/Event API 仍不能宣称已完成。

**当前状态更正（next136，2026-08-07）**：`screen=480x640 dpi=192` 日志中 TEST13/20/27/43/44/56/58
通过后，TEST59 暴露离线 flex 几何夹具继承设备 DPI：固定 `25px` padding 被按 192 DPI
换算为 `50px`。next136 只在 TEST59 的显式 CSS 几何 pass 前安装 96 DPI 参考上下文，
再恢复运行时设备视口；ARMV4I 构建/staging 已通过，设备复测待补。该修复不改变
NetSurf flex/layout，也不把 96 DPI 设为产品默认。

**当前状态更正（next137，2026-08-07）**：next136 在 `screen=320x320 dpi=128` 下
先通过 TEST13，随后 TEST20 报 `first box=48x48; expect 64x64 device px`。根因在
vendored `libcss` 的 `css_unit_len2device_px`：它先把每 CSS 单位的设备比例取整，
使 `128/96=1.333` 退化为 1。next137 改为保留固定点分数比例，乘完整 CSS 长度后再
做最终正负向取整；这是通用设备像素修复，不是放宽断言或固定 96 DPI。ARMV4I
构建/staging 已通过；设备复测已确认 TEST13/20/27/43/44/56/58/59 通过，随后
TEST60 停止。

**当前状态更正（next138，2026-08-07）**：TEST60 的离线表格几何夹具仍把运行时
`128 DPI` 传给 `PCore_SetViewport`，所以固定 CSS padding 被正确换算成设备像素后，
断言看到 `24/19/19/24` 与 `13/13/7/7`，而不是预期的 CSS `18/14/14/18` 与
`10/10/5/5`。文本宽度仍相等，未显示 selector/restyle 回归。next138 只为该显式
CSS 像素探针安装 96 DPI 参考上下文，并在结束时恢复真实设备视口；ARMV4I
构建/staging 已通过，`C:\WMShare\Positron-next138` 的设备复测待补。该修复不把
96 DPI 变成产品默认，也不放宽 TEST60 断言。

**当前状态更正（next139，2026-08-07）**：next138 在 `screen=320x320 dpi=128` 下
通过 TEST60，随后 TEST63 报告 `shared SVG did not survive first document release`。
TEST63 同样是固定 CSS `240x120`/`120x60` 的离线夹具，却继承了设备-backed DPI；next139
只为该探针安装 96 DPI 参考上下文，清理时恢复真实设备视口，并把失败诊断拆成
`layout/node/box` 值。共享 SVG 的跨文档 create/reuse/free 断言没有放宽，ARMV4I
构建/staging 已通过，`C:\WMShare\Positron-next139` 的设备复测待补。

**历史状态更正（next140，2026-08-07，已替代）**：next139 在 `screen=480x640 dpi=192` 下
通过 TEST13/20/27/43/44/56/58/59/60/61，随后 TEST62 的离线 checkbox/radio probe
返回 `36x36`，正好是 `192/96=2` 的 CSS 尺寸换算。next140 只为四个静态 toggle
probe 与 hidden-input 检查安装固定 96 DPI；这不是可接受的动态 DPI 修复，已由 next141
替代。

**当前状态更正（next141，2026-08-07）**：TEST62 保留 `64x48 CSS px` 探针表面，使用
设备实际 DPI，并将原本 96-DPI 的 `14..24px` 控件几何范围按 `dpi/96` 等比换算；控件
状态、绘制和隐藏 input 断言未放宽。ARMV4I 构建与
`C:\WMShare\Positron-next141` staging 已通过，设备日志确认 TEST62 及 TEST63-74
通过，TEST75 停止。

**当前状态更正（next142，2026-08-07）**：TEST75 的定位夹具保留 CSS 尺寸，断言将
宽高、绝对偏移和 relative 偏移统一按实际设备 DPI 的 `dpi/96` 等比换算；定位构盒、
绘制和可见页面路径未改变。ARMV4I 构建与
`C:\WMShare\Positron-next142` staging 已通过；`screen=240x320 dpi=96` 设备日志确认
TEST13、20、27、43、44、56、58-77、80-104 全部通过并记录 `TESTBENCH PASS`。

## 已验证基线（不是完整功能声明）

### 高 DPI / 大分辨率视口边界（next134，非 96 DPI 仍需继续轮换验收）

NetSurf 的标准坐标约定是：CSS media/vw/vh 使用 CSS 像素视口，
`layout_document` 与 GDI 重绘使用设备像素。next122 的新模拟器日志首次暴露
宿主把两者混用：TEST20 的 `48px` 图像盒实际为 `96x96` device px，自动化在
TEST20 停止；这不是图像缓存或脚本 provider 的失败。next123 增加
`PCore_SetDeviceViewport`，Browse 导航、WM_SIZE 旋转和宿主启动路径统一经过
设备像素到 CSS 像素的换算；`PCore_SetViewport` 的显式 CSS 像素语义保留给
离线引擎测试。ARMV4I 构建/staging 已通过，必须在新分辨率模拟器重新跑自动
配置并人工检查 IANA、Example Domain、滚动、链接和旋转；在此之前不能宣称
高 DPI Browse 已验收。

2026-08-07 的 next126 日志为 `screen=320x320 dpi=128`：TEST13 的三段导航完成，
但 TEST20 在离线缓存图片断言处停止。该失败不是 Browse 回归，而是 TEST20 沿用了
显式 CSS 视口路径，却把 48 CSS px 按设备 DPI 放大后比较；next127 已将 TEST20
隔离到 96 DPI CSS 视口，并修正了失败信息中的字段顺序。下一轮设备批次应轮换
分辨率、横竖方向或 DPI，并保留日志头部的屏幕尺寸与 DPI；即使自动 testbench 全部
通过，也必须人工检查 TEST13 的排版、滚动、链接和旋转。

next127 随后的设备日志为 `screen=240x320 dpi=96`：TEST13、TEST20、TEST27、
TEST43-96 均通过，TEST97 因测试把 Duktape 的小写 `invalid json` 错误文本误当成
失败而停止。next128 已将 TEST97 改为验证 `PSCRIPT_ERROR_JSON` 与非空诊断，保留
非法 JSON 后继续求值 `42` 及 JSON `null` 的后续恢复断言；这不改变 DLL 的运行时
行为，也不等于高 DPI Browse 已验收。

next128 的新设备日志为 `screen=240x240 dpi=96`，TEST13/20/27、TEST43-99 全部
通过。next129 进一步撤销 TEST20 中临时的 `PCore_SetViewport(..., 96)`：该离线
图片测试现在也通过 `PCore_SetDeviceViewport` 读取实际 DPI，并以
`MulDiv(48, dpi, 96)` 检查物理盒尺寸。96 是 CSS 像素定义中的参考 DPI，不是设备
锁定值；非 96 DPI 的设备仍需用 next129 实测验证。

next129 的新设备日志为 `screen=480x640 dpi=192`：TEST13 与 TEST20 通过，TEST27
发现 SVG 测试仍把 `120x60` CSS 尺寸当作物理尺寸比较，实际设备盒为 `240x120`。
next130 已修正 TEST27 的设备视口安装、动态尺寸断言和离屏采样坐标；该批次尚未
在设备上重跑。

next130 在 `screen=480x480 dpi=192` 下确认 TEST27/TEST43/44 通过后，TEST56 的
离线表格几何段因继承物理 DPI得到 `105 CSS px -> 210 device px` 而误报失败。
next131 已将该离线段隔离为 96 DPI CSS 契约，并保留可见渲染段的真实设备 DPI；
这不改变 table layout 实现。

| 范围 | 已验证事实 | 不代表 |
|---|---|---|
| CSS 媒体查询与 token | TEST 21 已在设备确认运行时 viewport/DPI、旧式 min/max-width 及整数像素 MQ4 `width <=` / `width <`；TEST38-39 又确认同表顶层 `:root` token 语义与正式 redraw。 | 所有媒体特性、MQ4 范围、元素作用域或完整 custom properties 均已覆盖。 |
| 现代 CSS 值 | TEST40 已在设备确认：数值型 `oklch()` 转裁剪 sRGB，并求值无需布局上下文的同单位 `calc()` 四则运算。 | 完整 gamut mapping、`none`/复杂角度、`color-mix()`、混合单位 calc 或 CSS Color/Values 均已实现。 |
| 反向 flex 内边距 | TEST 22 已在设备上确认：224px viewport 下，`row-reverse`、左右 25px padding、隐藏侧栏时，主内容为 `x=25,width=174`。 | 完整 Flexbox 规范或任意真实站点的复杂 flex 均已兼容。 |
| 基础定位 | next111/TEST75 已在设备确认：relative box 保持正常流并应用 top/left 偏移，absolute block 使用 positioned parent，`display:inline` 的 absolute box 被 blockify 后进入 NetSurf 正式定位路径。 | 不代表 float、sticky、Grid/flex/table 中所有定位交互、复杂 containing-block 或完整 CSS Positioned Layout 已实现。 |
| 普通浮动候选 | next115/next116 曾把 `float:left/right` 受控构盒路径和 TEST79 加入源码，但都已撤回。next116 的设备 TEST79 失败，且真实 TEST13 截图仍有导航/正文排版回归。 | Float 当前不支持；不能作为真实 IANA 页脚或完整 CSS Floats 证据。重新实现前必须先完成完整 box construction/normalisation，并通过 TEST79、TEST13 深链和旋转门禁。 |
| 动态 `:hover` 与交互重排 | next113/TEST76 已在设备确认 hover 切换；next167 又让 TEST76 覆盖 640x480/192 DPI 下 active/clear 两次重排仍保持 25 CSS px inset，真实 Learn More 点击的离开页边距也由用户确认。 | 不代表 `:visited`、`:target`、`:indeterminate`、专用 MouseEvent 数据、触屏 hover 语义、JavaScript 绑定或任意页面交互重排均已实现。 |
| IANA 窄屏页 | TEST13 起始页和 `Example Domains` 已可读；TEST41 的竖横屏截图确认 `/numbers` grid 宽表格不再把主内容推到左边界外。next80 已修复 libcss 父 bloom 节点数据过早销毁；TEST56/58/59/60 与真实 `/domains/reserved` 横竖屏均已通过。next167 自动三段导航通过，且人工点击 Learn More 后离开页仍居中。 | 任意 IANA 子页版式通过，或页面已达到现代浏览器还原度。 |
| 视觉容器与文本比例 | next117 人工复核确认主链路基本正常，但部分页面/测试存在容器或背景框偏小、文本量偏多导致的版式不协调。 | 尚未定位到单一 CSS 根因；不应通过放宽断言解决，也不代表核心解析/资源/导航失败。需要至少三个复现样例、computed style/box geometry 数据、针对性回归和竖横屏截图后才能关闭。 |
| 嵌套 overflow | NetSurf 3.11 scrollbar 已接入；TEST42 的离屏步进断言及真机箭头/thumb 交互通过，host 拖动只重绘对应 overflow viewport。next54 的 fixed-height 回归已在 next55 收窄，用户确认 auto-height 空间、箭头、短页纵条与色块页正常。 | 不代表惯性触摸、overlay scrollbar 或任意嵌套组合均已覆盖。 |
| table span/归一化/折叠边框 | NetSurf 3.11 span occupancy 与匿名 row/cell 已由 TEST46/47 验收。next64/TEST53 至 next68/TEST56 已覆盖常见 collapsed-border、cell alignment/empty-cells 与显式 table height；next73/TEST57 又确认 25/50/auto 百分比 row 分配及超约束缩放。 | 尚不覆盖任意 inline/float/form 畸形组合、caption/column 归一化、`col`/`colgroup` border 来源、百分比 cell/后代内容、跨行 baseline 或所有复杂表格边界。 |
| Forms/widgets | next85/93 至 next104/TEST71 已完成 checkbox/radio、文本、textarea、single/multiple select、button、GET/POST、reset/Enter/label 与 multipart/file；next106/TEST72 已确认首批 `required/valueMissing`；next109/TEST73 已确认动态表单伪类；next110/TEST74 已建立通用 DOM Event 传播/取消和宿主 click default-action 门；next135/TEST100-104 又加入 text/password/textarea 的 `minlength`/`maxlength`、UTF-8 字符计数、动态值更新和首个长度错误几何；next143/TEST105-109 加入受限 ASCII `pattern` mismatch、动态更新、坏属性豁免和 flags 组合，并已在 `screen=480x640 dpi=192` 设备通过；next147-150/TEST113-116 又加入最小 click、原生表单、EDIT 键盘和 focusin/focusout 桥，next151/TEST117 的 beforeinput 也已在 `screen=320x320 dpi=128` 设备通过。 | multipart 仍整体缓冲且 MIME 固定；尚无流式上传、上传进度、MIME 推断、multiple file、完整 JavaScript RegExp（groups/alternation/brace quantifier/Unicode/inverted class）、email/url/number 类型约束、range、custom validity、`invalid` 事件、验证气泡、完整 MouseEvent/KeyboardEvent/FocusEvent/InputEvent 字段或完整 HTML activation。空且无 CSS 尺寸的 text input 也缺浏览器默认 intrinsic size。自动断言不等于真实手指、原生选择器或公网 POST 已人工验收。 |
| author-level inline CSS | 外部 author stylesheet 正常参与 libcss 选择；TEST57 使用外部类规则通过。next75/TEST58 已确认 NetSurf 式声明列表解析、libcss inline cascade、继承、后代 class 选择与正式布局/重绘。next81 已把全零 `nsoption` shim 改成具名默认，未知读取会编译失败；TEST56/58-61 已由设备确认。 | 正式构盒不调用 NetSurf `box_construct.c`；旧缺口是 `pcore_style_subtree` 固定给 `css_select_style` 传 `NULL`。具名 option 不能被误写成 inline CSS 的直接开关。 |
| Forms/widgets 状态边界 | next109 把宿主维护的 focus/active 节点与 live checked/selected/disabled 状态交给 libcss callback；next110 的通用 Event 已有 capture/target/bubble、取消和停止传播，next113 又加入独立的 hover 状态，现有 click 默认动作尊重取消结果；next147-160/TEST113-122 又接入 click、原生表单、EDIT/SELECT 键盘、focus、beforeinput 及 Unicode/代理对，next161/TEST123 候选增加基础 WM IME composition 纵切。 | 尚无完整 MouseEvent/KeyboardEvent/FocusEvent/InputEvent/CompositionEvent 专用字段和完整 HTML activation；IME 自动探针不等于真实 SIP 候选窗口/预编辑 UI 已验收，也未覆盖 `isComposing`、`:visited/:target/:indeterminate` 或所有控件的浏览器默认 intrinsic size。 |
| 列表 marker | next57/59 已确认基础 marker 与字体；next61/TEST50 已确认 libcss 上游 47 种 counter formatter、document-cache `list-style-image` 与失败类型回退；next62/TEST51、next63/TEST52 已确认 inline-first 及 block-first/空条目/嵌套/图片的 `list-style-position:inside`。 | 不代表 float 邻接 marker、自定义 `@counter-style` 或完整 CSS Lists。普通语言字体不属于当前 marker 工作范围。 |
| 字体 fallback | next59 随包部署约 901 KiB 的三份静态 Positron Symbols/Emoji（来自 Noto OFL），精确 cmap 选择统一用于 GDI 测量、换行命中与绘制 run；设备确认箭头/marker/五个 emoji 可见且比 next58 稍平滑。当前范围明确只支持符号与单色 emoji fallback。 | 不计划在本阶段加入普通语言/多语种字体；也没有复杂 ZWJ/variation shaping、彩色 emoji、网页 `@font-face` 或字体下载。`ANTIALIASED_QUALITY` 最终效果仍依赖 OEM GDI。 |
| 图片 | TEST19/20 已确认公共 retained 位图 ABI 与 WM Imaging 四格式；TEST25-37/13 已确认当前 SVG 链。next89 已由 TEST20/27 确认同 document 二次布局复用；next92/TEST63 已确认两个同时存活且内容一致的 document 可共享 SVG，并在释放首文档后继续绘制。 | 复杂 SVG text、径向焦点/spread method、多层或可缩放 CSS 背景、空闲/持久缓存及跨线程图片句柄仍未完成。 |
| 外部脚本资源与独立 JS DLL | next114/TEST77 在 core 中建立了非空 script-src 扫描、宿主 URL resolver/fetch/free 回调、document 生命周期缓存、重复引用去重和只读枚举 ABI；next118-126/TEST80-99 又以仓库内 Duktape 2.7.0 完成独立 `positron_script.dll` 的求值、预算、heap 配额、模块、provider、global/JSON、native callback 与 structured setter，设备日志已确认。next144 再加入默认关闭的 browser inline-script 枚举/执行与最小 `getElementById`/`textContent` bridge，TEST110 已通过设备验收；next145 又把 external body 映射到 DOM 顺序执行，TEST111 在 `screen=320x320 dpi=128` 设备通过。 | next145 只执行 classic inline/external script，初次执行后销毁 context；`async/defer/module`、事件、异步任务、完整 DOM/window/fetch/network binding、CSP 或跨源策略仍未实现。脚本错误不撤销导航，失败 external 会跳过。DLL heap 配额不约束宿主进程其他内存；global/JSON 结果缓冲最多 255 字节有效载荷，模块 provider 仍是同步宿主回调。默认 `javascript=0` 时 TEST13 不扫描、抓取或执行脚本。 |
| ENGINE 离线回归 | 2026-07-11 用户确认原整组至 TEST24 通过；2026-07-12 又单独确认 TEST25 SVG parse。TEST23 的浮动实现已因真实 Browse 回归撤回。 | 网络 Browse、GDI Render 组，或未被这些测试覆盖的真实页面兼容性均已通过。 |
| 旋转尺寸 | `WM_SIZE` 以新 client 宽高从 document CSS 缓存 restyle + layout；TEST24 已确认跨断点重选、无联网及滚动比例，真实 TEST13 横竖屏也保持同一阅读区域。 | 所有媒体语法和任意样式资源均已覆盖。 |

### next125：独立脚本 JSON 宿主回调桥（待设备验收）

`PScript_RegisterGlobalJsonFunction` 通过固定 16 槽表把宿主同步回调暴露成 JavaScript global。参数是 compact JSON 数组，回调写回一个 JSON 值；回调不得重入/销毁上下文，也不能异步保存指针。DLL 只接受小于 256 字节的返回缓冲，失败、非法 JSON 和调用异常会回到既有 recoverable error 路径。TEST90-94 覆盖这条 ABI；next126 又提供 `PScript_SetGlobalJson`，用受保护 decode 注入结构化 JSON，输入最多 64 KiB，失败/超限保持原 global。TEST95-99 覆盖该 setter，但 Debug 构建通过不等于设备/第三方程序验收；它仍不提供 DOM、window、fetch、网络或浏览器 JS 开关。

## 真实页面观察到的未完成项

### IANA 根变量布局已改善，完整真实页仍继续观察

TEST38-39 真机确认根变量语义及 25px inset 后，新的 TEST13 截图中 IANA logo、导航、正文与注册表两列均未再裁切或重叠，custom-properties 导致的窄屏间距根因可以关闭。普通文本空白折叠也已由 TEST13/15 确认。当前结论仍不是任意 IANA 子页或任意真实站点都已完整还原。

- **可能范围**：剩余 flex/table/inline/字体或未实现 CSS 特性的组合；尚未把单一原因当作结论。
- **已撤回的一项**：IANA 页脚是 table cell 内 `display:inline; float:left` 列表。TEST23 曾在最小样例中确认两个浮动块同行及 `clear:both`，但将该构盒规则直接接入真实页面后，2026-07-11 Browse 截图出现严重错位和替代方框；实现已撤回。该测试不再参加 ENGINE 组，不能作为 float 支持证据。
- **next115 已否决**：设备日志得到 `float=(0,0 70x36)/(154,0 70x36) flow=(70,0 0x20)`，说明 TEST79 查询到的是零宽 inline 起始盒；同包 TEST13 截图还出现导航扁平化。next115 不得作为基线。
- **next116 已否决**：它只处理显式 block-level 的非替换 float，保留 inline/list-marker、flex、图片和表单的既有路径，但仍让真实 IANA 页面出现导航扁平化/正文排版回归；设备 `TEST79` 最终失败。实现和默认测试配置已撤回，next114 恢复为可靠基线。Float 方向暂挂，不能把离线几何候选当成真实页面支持。
- **当前站点版本风险**：2026-07-13 重新读取到 IANA 的 `iana_website.80c103cc08b6.css`；除已确认的 `var(--space-*)` 外还有 22 处 `oklch()`、15 处 `calc()`、`color-mix()`、grid/gap 与 `:has()`。新兼容模块只处理数值型 OKLCH 和可完全求值的同单位 calc；混合单位及其他现代能力仍会降级。
- **最新子页结论**：`/numbers` 使用 `display:grid`，其中 `.dtable-wrap { overflow:auto }` 包住宽表格，TEST41 已确认该路径。`/domains/reserved` 没有 Grid 包装层，但同样以 `.dtable-wrap { overflow:auto }` 包住宽表格。next77 已让 flex main 在竖屏保持正确 inset；旋转到横屏后，wrapper 本身位置正常，但第一格内容左移约 18px，恰好抵消作者的 `padding-left:18px`，导致 `Domain` 贴到 clip edge，视觉上连字体/样式也异常。非拉丁字符 tofu 与这个英文表头问题无关，并按项目范围保留。
- **失败实验**：next78 在 layout 末尾递归 `scrollbar_set(...,0)` 后，真实页横屏从首个 `Domain` 异常扩大为全部表格单元格异常，TEST56 随后失败并触发系统级异常。该实验及其诊断 API/扩展 TEST59 已撤回，旧包已改名为 `C:\WMShare\Positron-next78-FAILED-DO-NOT-USE`。
- **已验收边界**：next80 的 TEST56/58/59/60 与 TEST13 起始页、`Open example`、IANA `/domains/reserved` 横竖屏均通过。仍禁止恢复 next78 的全局 scrollbar 回调重置；这次闭环不等于所有 IANA 子页或完整现代 CSS 已实现。
- **完成条件**：在目标设备的竖屏和横屏下，主内容、页脚和导航均不裁切、不重叠，且没有明显错误图标/替代字符；结果需要新的真机截图确认。

### 旋转 responsive restyle 已完成当前验收

`WM_SIZE` 现调用 `PCore_SetViewport`、基准 URL 感知的 `PCore_StyleDocumentEx2` 和 `PCore_LayoutDocument`。外链 CSS 与成功导入首次导航时以原始字节缓存到 document，尺寸变化只从该缓存重选 `@media`，不重新联网。旧 `PCore_StyleDocumentEx` 保持兼容。TEST24 已于 2026-07-11 在设备确认 320px 到 299px 的旧接口外链 CSS 重选，fetch/free 都保持一次；TEST45 又确认导入树的 cache-only 重选。

- **当前取舍**：只缓存最多 32 份、单份不超过 256 KiB、每 document 合计不超过 512 KiB 的成功外链 CSS 原始字节；缓存未命中的样式在旋转时保持缺失，不能在 `WM_SIZE` 中重新联网。
- **设备结论**：TEST24 的 0/50/100% 比例断言通过；真实 TEST13 从竖屏 `Further Reading / Domain Names` 区域旋转到横屏后仍停留在同一区域。扩大 MQ4 语法或处理 custom properties 仍是独立兼容性工作。
- **完成条件**：旋转前后跨越 TEST 21 式断点时，computed style 与几何都切换正确，并恢复原滚动位置的合理比例。

### 导航 CSS/图片抓取已异步，最终提交仍在 UI

主文档之后的外链 CSS、CSS `@import`、`<img>` 和应用样式后发现的背景 URL 现也分轮交给同一 worker；HTTP 字节通过 `WM_APP` 消息交回窗口线程，DOM/libcss/NetSurf/GDI 从不跨线程。TEST3/43/44 与真实 TEST13 已确认真实正文进度、后台资源阶段、成功 swap 和主文档失败回滚。parse/style/image-discovery/layout 现用一次性 WM timer 在调用之间让出消息循环；单个不可重入调用仍可能卡顿。

- **当前取舍**：同一时刻只允许一个导航请求；旧页可绘制和滚动，但加载中再次点击链接会被忽略。HTML parse、style、cache copy 和 layout 仍在 UI 提交阶段同步执行，全部网络完成后仍可能短暂卡顿。
- **已验收观测**：next86 的 TEST13 报 total/network/max-UI=6435/5503/673ms，parse/style/images/layout/paint=11/182/6/673/36ms；2 个资源全部成功，document/cache=10499/121111 bytes，预算拒绝为 0。网络主导总时长，layout 主导单次 UI 停顿；该数据用于定位，不是已完成的调度优化，也不是产品遥测上传。
- **已验收诊断**：next87 的只读 layout breakdown 不改变布局行为；IANA 起始页报告 `580ms` 中 box/first=515/65，Reserved 子页的最后一次导航报告 `662ms` 中 box/first/settle=495/124/43。它证明构盒是当前首要细分对象，不证明任何性能改善，也不应据此跳过二次布局。
- **已取得诊断结论**：next88 的 IANA 起始页 tree/image=`523/518ms`，Reserved 为 `481/474ms`；backgrounds 为 0，tree-other 仅 `4/1ms`。它定位到 SVG retained object 创建，并不说明整个页面或 box tree 已缓存。
- **next89 已验收优化**：XML-like 图片字节先调用现有 SVG parser，避免 WM Imaging 的已知失败探测；成功/失败 retained 状态归 document image cache 所有，重排只新建轻量 carrier。TEST20/27 的 4/4 与 1/1 reuse 及全部默认门禁通过。TEST13 首次页面 image 仍为 `469ms`，随后 Reserved 降到 `37ms`；首屏冷启动未解决。对象仍在创建线程释放，不支持跨 document、跨导航或跨线程共享。
- **遥测边界**：TEST13 只显示导航完成快照，导航后的旋转布局不回写该统计，因此旋转后退出仍会显示初始 layout 的 reuse/markup-first。
- **next90 已取得设备诊断**：ABI 1.5 的 `PImage_SvgGetCreateStats` 与 core 的 `PCore_GetImageDecodeStats` 只记录成功 retained SVG 的 setup/parse/raster 创建时间。next91 日志中 TEST27 为 `59=0+58+1ms`，IANA Example 为 `37=0+37+0ms`，Reserved 为 `595=0+593+2ms`；当前冷创建成本几乎全在 `svgtiny_parse`，不是 wrapper setup 或 NanoSVG retained raster conversion。失败 SVG、后续 draw、字体 GDI 和布局不属于这四段；计时分辨率受 WM `GetTickCount` 限制。
- **next91 testbench 已通过首轮设备运行**：`auto=1` 将断言、首帧绘制和三步真实导航无人值守化，配置的 TEST13/20/27/43/44/56/58-62 最终全部 PASS，结果成功写入同目录 `test_host.log`。它不会做截图像素基线比较，也不能判断抗锯齿、字体观感或复杂真实页是否“好看”；网络 worker 仍依赖现有 HTTP 超时，未加入强制终止线程。首轮日志把 TEST44 的预期离线失败也标成通用 TEST13 NAV ERROR，后续候选已将逐页记录严格限定在 TEST13 自动路由。
- **next92 重叠文档 SVG 复用已验收**：实现沿用 NetSurf high-level cache 的“内容条目与使用者分离”原则，但范围刻意更小。只有 URL、长度和双内容哈希一致、且至少一个 document 仍持有引用的 SVG 才共享；引用归零立即释放，不设置任意 MiB 常驻预算。设备 TEST13 的 Reserved 页为 `image reuse=1`、`svg creates=0`、image=2ms，TEST63 也通过释放首 document 后的像素绘制。位图、空闲对象、跨线程和持久缓存均不在该机制内。
- **next93 checkbox/radio 交互已验收**：`PCore_FormActivateAt` 与链接命中使用相同 document-space 坐标，状态写回 libdom；宿主仅失效发生变化的控件联合区域。TEST64 验证 checkbox、disabled、radio 分组隔离和纵横屏重排保持，同批 TEST13/20/27/43/44/56/58-64 全部 PASS。这不是完整表单、焦点或事件系统。
- **next94/97 原生文本输入已验收**：core 以 UTF-8 ABI 暴露 text/password/textarea 的值与状态，WM 宿主使用原生单行/多行 `EDIT`，并通过真实 `EN_CHANGE` 把 UTF-16 编辑结果同步回 libdom。多行回写将 CRLF/CR 归一化为 LF。设备无人值守日志确认 TEST65/66 及同批 TEST13/20/27/43/44/56/58-64 全部 PASS。
- **next98 select 自动门禁已验收**：core 以独立 ABI 暴露单选/多选的 option 文本、value、selected/disabled 和几何，单选宿主使用 WM 原生 `COMBOBOX`。2026-07-30 设备日志确认 TEST13/20/27/43/44/56/58-67 全部 PASS。TEST67 的原生桥探针通过 `CB_SETCURSEL` 后调用与 `CBN_SELCHANGE` 相同的同步路径，只证明宿主适配和 DOM 状态连通，不等于真实手指展开下拉或多选 LISTBOX 已验收。button、label 激活、提交和完整事件系统仍未实现。
- **next99 button/提交已通过自动门禁**：按钮由 NetSurf CSS layout/redraw 呈现，不叠 WM 原生 BUTTON。core 只生成 UTF-8 URL-encoded successful controls，宿主 GET 替换 action query、POST 调用既有 `PHttp_PostEx`；资源子请求继续使用 GET。multipart 不会降级成残缺 POST，而是明确返回未实现。设备日志确认 TEST68 与同批 TEST13 深链全部 PASS；TEST68 本身不联网，只断言 core 数据与宿主请求对象，真实公网表单端点仍需后续批次覆盖。
- **next100 普通表单默认动作未通过门禁，next101 已修复**：next100 的 TEST13 深链和 TEST20/27/43/44/56/58-68 全部 PASS，但 TEST69 textarea reset 失败。libdom 0.4.2 首次读取 textarea value 时误置 `default_value_set`，使默认文本未保存；next101 将其改回 `value_set` 且不改原断言。设备日志随后确认 TEST69 与同批全部门禁 PASS。自动桥仍不代表真实手指、视觉位置、约束验证或 DOM 事件取消/传播已经验收。
- **next103 multipart/file 已通过设备门禁**：next102 的既有门禁全部 PASS，但 TEST70 在 reset 后残留显示文件名；libdom 会把无初始 `value` 属性的 file 控件第一次运行时 `set_value()` 记为 `defaultValue`。next103 在 reset 时按浏览器语义无条件清空 file 显示值与原始路径，设备日志随后确认 TEST13/20/27/43/44/56/58-70 全部 PASS。TEST70 不会自动操作系统文件选择器，也不访问公网回显端点；这些仍需后续人工/集成验收。
- **next104 WM multiple select 已通过设备门禁**：宿主为 multiple select 创建带 `LBS_MULTIPLESEL` 的原生 LISTBOX，按 NetSurf border-box 定位，并复用单选已有的滚动、换页销毁与旋转重建。`LBN_SELCHANGE` 与 `CBN_SELCHANGE` 共享同步入口；multiple 路径逐项读取 `LB_GETSEL`，写回 `PCore_SelectSetOptionSelected`，disabled option 被 Core 拒绝后立即恢复原生状态。TEST71 自动确认两项增删、disabled option/select、LISTBOX/COMBOBOX 类型、精确高度、重建保持、GET 重复值和 reset；同包 TEST13/20/27/43/44/56/58-71 全部 PASS。该断言不检查 owner-draw disabled 行、真实手指命中或视觉观感。
- **next106 首批 required 验证已通过设备门禁**：Core 对 text/password/textarea/file、checkbox、同名 radio、single/multiple select 计算 `valueMissing`，跳过 disabled/read-only，并在 URL-encoded 与 multipart 提交前阻止无效表单；宿主滚动并聚焦首个无效原生控件。next105 的 reset 失败来自 libdom 0.4.2 将第一次运行时 text/password 写值误记为默认值；next106 在写入前冻结解析时默认值，不改 TEST72 断言。设备日志确认 TEST72 和 TEST13/20/27/43/44/56/58-71 全部 PASS。尚无高级 validity 状态、`invalid` 事件或验证气泡。
- **next109 动态表单状态已通过设备门禁**：TEST73 验证 disabled/enabled、checkbox/option checked、focus、active、cache-only 重样式、纵横屏保持与 reset；同包 TEST13 三段导航及 TEST20/27/43/44/56/58-72 全部 PASS。next107/108 的失败不是颜色或伪类语义，而是测试夹具中无 CSS 尺寸的空 text input 几何为 0；next109 让夹具与既有 WM EDIT 前提一致地声明显式尺寸。默认 text input intrinsic size 保持为独立缺口，不通过放宽动态状态断言掩盖。
- **next110 最小 DOM Event 已通过设备门禁**：公开 ABI 只暴露 opaque listener handle 与普通 C callback，不泄露 libdom 类型；支持按 element id 或正式布局坐标派发 trusted generic Event。vendored libdom 的 target 重复传播、忽略 `bubbles/cancelable` 和 dispatch-only 状态残留已在本地按 DOM 语义修正。TEST74 覆盖 capture/target/bubble、非冒泡、取消、两种停止传播、移除监听器与坐标派发；TEST13 三段导航和 TEST20/27/43/44/56/58-73 同批全部 PASS。尚未实现专用事件数据、完整 HTML activation、异步事件队列或 JavaScript 绑定。
- **next110 最小 DOM Event 已通过设备门禁**：公开 ABI 只暴露 opaque listener handle 与普通 C callback，不泄露 libdom 类型；支持按 element id 或正式布局坐标派发 trusted generic Event。vendored libdom 的 target 重复传播、忽略 `bubbles/cancelable` 和 dispatch-only 状态残留已在本地按 DOM 语义修正。TEST74 覆盖 capture/target/bubble、非冒泡、取消、两种停止传播、移除监听器与坐标派发；TEST13 三段导航和 TEST20/27/43/44/56/58-73 同批全部 PASS。尚未实现专用事件数据、完整 HTML activation、异步事件队列或 JavaScript 绑定。
- **next111 基础定位已通过设备门禁**：TEST75 以四个不同标签的夹具断言正常流、relative 偏移、absolute block containing block 以及 absolute inline blockification，并打开正式 NetSurf layout/redraw 窗口做首帧冒烟；TEST13 三段导航和 TEST20/27/43/44/56/58-74 同批全部 PASS。该项只覆盖当前 slim builder 能安全接入的基础路径，float、sticky、复杂 containing-block 组合与 Grid/flex/table 定位交互仍保留。
- **next116 浮动候选已撤回**：设备日志为 `TEST79 FAIL`；即使自动 TEST13 记录了 OK，人工 Browse 截图仍显示导航和正文排版异常。源码、TEST79 默认配置和 ENGINE 接入已恢复到 next114，Float 方向暂挂。
- **next113 动态 `:hover` 已通过设备门禁**：TEST76 断言 `PCore_InteractionSetAt(..., PCORE_INTERACTION_HOVER)` 命中最近元素，重选样式得到红色，清除后恢复蓝色；宿主使用 WM6 可用的 `WM_MOUSEMOVE` 与定时器轮询，不调用桌面 `TrackMouseEvent`。同包 TEST13/20/27/43/44/56/58-75 全部 PASS。该项只覆盖 CSS 状态选择和离开窗口清理，不代表专用 MouseEvent、触屏 hover 或 JavaScript。
- **资源预算**：`test_host` 最多暂存 64 个去重 URL、合计 2 MiB 原始字节，成功提交时 core 会复制所需数据后立刻释放事务。该值用于限制 WM 峰值，是可替换的宿主策略，不是 `positron_core` ABI 或最终页面的硬上限。
- **后续实现**：单响应 `Content-Length`/progress 回调已实现并由 TEST3/13 确认；`@import` 事务已由 TEST45 确认；next114/TEST77 已建立脚本资源发现/缓存 ABI，next118-126 又提供不依赖浏览器的独立 `positron_script.dll` 能力。next134 的 `screen=240x320 dpi=96` 设备日志确认 TEST80-99 全部通过，但它们仍未接入 TEST13。高 DPI Browse 仍需在不同分辨率/DPI 下视觉复查；DOM/window/fetch/native bridge、整页多资源聚合进度、web fonts 和更广资源类型仍未实现。
- **next125 已设备验收（next134）**：TEST90-94 已实现同步 JSON 宿主回调注册/注销、替换、失败恢复和固定槽位上限；它是独立 DLL 的 native operation 边界，不是浏览器 JavaScript 或异步 native bridge。
- **next126 已设备验收（next134）**：TEST95-99 已实现 structured JSON global setter、跨调用 mutation、malformed/null 恢复、64 KiB 输入拒绝和 JSON 类型替换；setter 复制值进 context，不保留宿主输入指针，但仍受 Duktape heap 与 255 字节结果读取限制。
- **CSS import 边界**：最多追踪 16 层递归和本次样式 pass 的 64 个解析表；失败、循环和超深导入按 libcss 契约注册空表。成功导入复用每 document 最多 32 份/512 KiB 的 CSS 字节缓存；不含 HTTP 缓存失效、跨源安全策略或独立持久缓存。URL 合并由宿主回调负责，WM 宿主使用 `InternetCombineUrlA`，core 本身不绑定传输层。
- **并发约束**：在确认 libdom/libcss/NetSurf 移植层的线程安全前，不能让 worker 与 UI 同时操作同一 document 或共享全局 viewport context；过期请求只丢弃结果，不使用强制终止线程。
- **第一阶段完成条件**：慢网主文档 GET 期间旧页可滚动，loading 可见；成功后才 swap，错误保留当前页面，关闭窗口不会遗留线程。
- **当前完成条件**：TEST43 的 URL/去重/成功/失败断言通过；真实 TEST13 的 CSS/图片网络等待不阻塞 UI，generation 正确，成功后才 swap，失败资源保留 fallback。

### 图片格式与公共 retained 位图 ABI 已完成当前验收

WM Imaging 的 BMP/PNG/JPEG/GIF 均已在设备通过尺寸探测和 Draw 返回，但首轮多格式 fixture 的可见性与旧截断 BMP 不足以完成视觉验收。当前 `<img>` 解码失败时仍刻意回退到 alt/src 文本。

- **当前结论**：BMP/PNG/JPEG/GIF 四格式与 TEST20 缓存 `<img>` 已由设备视觉确认。2026-07-15 next45 又确认公共位图句柄的四格式颜色、清空调用方输入后的重复绘制、损坏输入拒绝、旧 `PCore_Image*` 转发和 NetSurf retained redraw；TEST13/26/27 同批无回归。句柄仍只允许在创建线程使用和释放。为保证 WM Imaging 的惰性解码数据源始终有效，句柄存活期间会保留一份编码字节；core 的 document cache 也保留原字节以支持重布局，因此当前以额外编码内存换取重绘不重复解码。TEST25-37/13 的 SVG 真机结论保持不变。CSS 背景仍不含 background-size 和多层背景；SVG 仍缺复杂 shaping、`textPath`、逐字 dx/dy、任意 shear、径向焦点 `fx/fy` 或 spread method。单次栅格源缓冲限制为 1,048,576 像素，超大输出会降低内部采样分辨率后再缩放。
- **独立消费与编码**：next46 已确认只导入 `positron_image.dll`/`COREDLL.dll` 的 ABI 1.0 示例横竖屏工作；next47 确认 ABI 1.1 的 PNG/JPEG 内存编码与释放/回读闭环；next48 证明 WM quality=100 不能修复小图色度串扰。ABI 1.2 保留 quality=-1 的 WM 默认路径，显式 0..100 使用静态 libjpeg-turbo 1.5.3 4:4:4，next49 已确认行方向、颜色和 SOF 采样正确。ARMV4I 构建无 SIMD，显式编码会额外生成约 `width*height*3` 字节的 24bpp 中间位图；Debug DLL 增加约 238 KiB，但静态 `.lib`、源码和独立 JPEG DLL 均不部署。next50 截图确认 ABI 1.3 的复制式 padded BGR24/BGRA32、RGB/alpha PNG、JPEG 与 SVG 视觉正确；next51 又确认 ABI 1.4 的 BMP/GIF 系统 encoder、签名与回读成功。next51 的退出仍失败：WM 标题栏 X 是 Smart Minimize，不保证发送 `WM_CLOSE`。next52 以系统 `aygshell.dll` 的 `SHDoneButton` 换成标题栏 OK，并由 `IDOK` 真退出；用户已确认任务管理器进程消失且可再次启动。这不会增加底部软键或占用客户区。跨线程句柄仍未提供。
- **完成条件**：每种宣称支持的格式均有内存单测和真实 Browse 页面实例，且资源失败仍保留可访问 fallback。

## 维护规则

1. 每次真机截图改变结论时，同时更新本文件、`HANDOFF.md`、`ROADMAP.md` 和根目录 `README.md`。
2. 测试名称后的“OK”只说明其明确断言成立；必须同时写出它没有覆盖的范围。
3. 新增临时 stub、降级、硬编码测试尺寸或线程取舍时，先在此登记后续任务和完成条件。
4. 新增失败分支或暂挂方向时，同时更新 [`FAILED_EXPERIMENTS.md`](./FAILED_EXPERIMENTS.md)；该索引必须写清状态、证据、是否可重试和重启门槛。

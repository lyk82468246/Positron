# Positron

**当前全量自动设备检查点（next206，2026-08-12）**：同一个 Duktape 页面 context 已具备最小
location/history bridge、受控 state、同 document traversal、popstate/hashchange、动态 URL
组件与 `location.hash` 导航。`screen=640x480 dpi=192` 日志中 TEST13 三段导航及
TEST20/27/43/44/56/58-77/80-173/999 全部通过：配置所选 121 项全部 OK、零条 `[ERROR]`、
零 FAIL、最终 `TESTBENCH PASS`，日志为 `C:\WMShare\Positron-next206\test_host.log`；TEST13
使用 `OK (overview)`，其余 120 项使用标准数字 OK 行。最近一次定向人工门
仍是 next167：用户确认
Learn More 离开页保持居中边距，真实 SIP 候选词点击可完整键入。后续人工视觉/交互检查
改为累计若干可能产生回归的批次后集中进行，不再逐个自动批次阻塞开发。默认
`javascript=0`；TEST123 仍只代表自动共享路径，不外推为任意 OEM IME。

**next204 自动设备与提示音门通过（2026-08-12）**：`test_host.ini` 可把专用 `TEST999` 放在所选
测试末尾；只有前序所选测试全部完成并运行到该项时，宿主才调用一次
`MessageBeep(MB_OK)`，随后记录 `TEST 999 OK`。这不是所有退出路径的全局钩子，前序失败或
未选择 999 都不会响。C89 回归和 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next204` 的七个二进制与构建产物 SHA-256 一致。设备日志得到配置所选
119 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；用户确认测试序列末尾实际听到一次提示音。

**next205 自动设备基线（2026-08-12）**：绝对 href/assign/replace URL 现在会移除查询或
fragment 前所有分离的字面 `/./` 段；规范化后仅 path/query 与当前文档相同且 fragment 改变
或清除时，才复用同文档队列。TEST172 固定三入口、清除、same-value、history/state、
hashchange、无网络以及不同 query/path、`%2E` 和 `..` 排除边界。默认 `javascript=0`、
TEST13、core ABI 与 14/16 callback 槽位不变。C89 与 ARMV4I Debug 构建已通过，
`C:\WMShare\Positron-next205` 七个二进制与构建产物 SHA-256 一致。首轮设备运行在 TEST167
停止，原因是 TEST167-169 仍把
本批新增支持的同一个绝对多位置输入断言为普通导航；修正版只移除这三条过时排除断言，
保留 query/path、`%2E` 与 `..` 边界。修正版设备日志得到 119 条标准数字 OK、1 条 TEST13
overview、零 ERROR/FAIL 与最终 PASS，next205 已成为基线。

**next206 自动设备基线（2026-08-12）**：按 WHATWG URL Standard 的 single-dot segment
定义，绝对 href/assign/replace URL 中内嵌的整个 segment 恰为 `%2E` 或 `%2e` 时，可移除该单个
编码点段并在 path/query 匹配时进入既有同文档片段队列。TEST173 覆盖大小写、三入口、清除、
same-value、history/state、hashchange、无 GET；next206 日志当时还排除多个编码点段，
`%2E%2E`、不同 query/path 与 `..`。next210 基线只用 TEST177 取代前一条旧排除断言。
默认 `javascript=0`、TEST13、core ABI 和 14/16 callback 槽位不变。
C89 与 ARMV4I Debug 构建、七个二进制 SHA-256 和设备门均已通过；日志得到 120 条标准数字
OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS。

**next207 定向自动设备基线（2026-08-12）**：绝对 href/assign/replace URL 的 path 末尾
segment 恰为 `%2E`/`%2e`（位于 query/fragment 或 URL 结尾前）时，也按 single-dot 移除。
TEST174 覆盖大小写、三入口、清除、same-value、history/state、hashchange、无 GET，以及
混合内嵌/末尾编码段、`%2E%2E`、不同 query/path 和 `..` 排除边界。正式 ARMV4I Debug 构建
与七个 staging 哈希已通过；本批默认设备门缩为 TEST13/151-174/999，共 26 项。
首轮设备日志在 TEST174 停止，原因是末尾 `/%2e` 的长度被误按内嵌 `/%2e/` 的 5 字符计算；
修正版只把边界判断和截取长度改为 4，不修改断言或支持范围；覆盖构建后的 26 项门得到
25 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS。

**next208 定向自动设备基线（2026-08-12）**：根相对 href/assign/replace URL 中单个、内嵌完整
segment 恰为 `%2E`/`%2e` 时，按 single-dot 移除后再与当前 origin/path/query 比较。TEST175
覆盖大小写、三入口、清除、same-value、history/state、hashchange、无 GET，并排除多个编码
点段、`%2E%2E`、不同 query/path 与 `..`。默认 `javascript=0`、TEST13、core ABI 和 callback
槽位不变；C89、ARMV4I Debug 构建与七个 staging 哈希已通过，定向门为
TEST13/151-175/999，共 27 项；日志得到 26 条标准数字 OK、1 条 TEST13 overview、零
ERROR/FAIL 与最终 PASS。

**next209 定向自动设备基线（2026-08-12）**：根相对 href/assign/replace URL 的 path 末尾
segment 恰为 `%2E`/`%2e` 时，也按 single-dot 移除。TEST176 覆盖大小写、三入口、清除、
same-value、history/state、hashchange、无 GET，并排除混合内嵌/末尾编码段、`%2E%2E`、
不同 query/path 与 `..`。C89、ARMV4I Debug 构建和七个 staging 哈希已通过；定向门为
TEST13/151-176/999，共 28 项；日志得到 27 条标准数字 OK、1 条 TEST13 overview、零
ERROR/FAIL 与最终 PASS。

**next210 定向自动设备基线（2026-08-12）**：绝对 href/assign/replace URL 中多个内嵌完整
`%2E`/`%2e` single-dot segment 可依次移除。TEST177 覆盖三入口、清除、same-value、
history/state、hashchange、无 GET、不同 query/path 与父目录排除；TEST173 中“重复编码点段
必须普通导航”的旧断言已由本测试取代，`%2E%2E` 和根相对重复编码边界不变。C89、ARMV4I
Debug 构建与七个 staging 哈希已通过；定向门为 TEST13/151-177/999，共 29 项；日志得到
28 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS。

自动设备门从本批起分层：低风险、局部变更运行“本批测试 + 直接共享路径 + TEST13 +
TEST999”；每累计约 5 个低风险批次，以及触及公共 DLL/ABI、布局/重绘、网络、输入基础设施、
里程碑交付或出现异常时，再运行全量门。next206 的 121 项日志是当前最近一次全量证据。

**next161 设备运行未完成（2026-08-09，不能作为基线）**：WM 原生 EDIT 子类接入
`WM_IME_STARTCOMPOSITION/WM_IME_COMPOSITION/WM_IME_ENDCOMPOSITION`，通过 WM6
`coredll` 的 `ImmGetContext/ImmGetCompositionStringW` 取得 UTF-16 组合串并转为 UTF-8；
显式脚本 context 可收到 `compositionstart/update/end` 及不可取消的
`beforeinput(insertCompositionText)`。TEST123 覆盖组合事件数据、顺序、冒泡、取消属性、
可取消的 `compositionstart` 和 WM 消息入口。自动探针复用真实消息入口与正式数据发射器，
但不伪造系统 IME context，因此真实 SIP 候选窗口输入仍需人工验收。默认 JavaScript 与
TEST13 均不改变。C89 专家脚本、仓库审计、VS2008 ARMV4I Debug 增量构建和
`C:\WMShare\Positron-next161` staging 已通过；但设备日志只完成 example.com 与 IANA
Example Domains，Reserved Domains 在收到 HTTP 状态前遇到 TLS 握手 EOF，TEST13 以
`2/3` 停止，TEST123 没有执行。因此该日志既不能验收、也不能否定 IME 纵切。实现依据
[Windows CE `ImmGetCompositionString`](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms906001%28v%3Dmsdn.10%29)、
[Windows CE `WM_IME_COMPOSITION`](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms921476%28v%3Dmsdn.10%29)、
[W3C UI Events](https://www.w3.org/TR/uievents/) 与
[Input Events Level 2](https://www.w3.org/TR/input-events-2/)。

**next162 待设备验收（2026-08-09）**：保留 next161 的 IME/TEST123，在 worker 线程中只对
主文档幂等 GET 的 `status=0 + empty body + ssl_handshake EOF` 做一次 250ms 后重试，并把
`document_retries` 写入 TEST13 日志和遥测。POST、DNS、HTTP 4xx/5xx、页面子资源及其他
错误均不自动重试；TEST43 离线固定该边界。C89、仓库审计和 VS2008 ARMV4I Debug
增量构建及 `C:\WMShare\Positron-next162` staging/哈希核对已通过，完整设备日志通过前
仍以 next160 为基线。

**next163 设备验收通过（2026-08-09）**：在 next162 的受限主文档握手 EOF 重试之上，
TEST124/125 为 InputEvent 与 KeyboardEvent 增加 size-tagged Ex ABI 的 isComposing，
TEST126 增加脚本 textContent/attribute bridge，TEST127 增加 input/textarea/select
的 value，TEST128 增加不改写 markup 的 live checkbox/radio checked。
为修正 libdom 0.4.2 的 checked 属性误当 live property 问题，vendored
html_input_element.c 现在把 checked 状态与 parsed checked attribute 分开。
默认 javascript=0、TEST13 网络路径和已验收 Browse 基线不变；ARMV4I 增量构建、
C89、审计、staging 与设备自动日志均已通过。`test_host.log` 以 `TESTBENCH PASS`
结束；自动断言不替代真实 SIP/IME 输入和视觉检查。

**next164 设备验收通过（2026-08-09）**：在 next163 的脚本表单属性桥之上，TEST129
增加 Event.target/currentTarget 的 DOM ID 映射，TEST130 增加 PElement.id/className
反射，TEST131 增加 classList 的 contains/add/remove/toggle，TEST132 增加受控的
style.cssText 与简单 get/set/removeProperty。新接口只在显式 javascript=1 的
classic script context 中可见；默认 javascript=0、TEST13 网络路径和 next163
Browse 基线不变。C:\WMShare\Positron-next164\test_host.log 已以
`TESTBENCH PASS` 结束；自动断言仍不替代人工视觉检查。

**next165 设备失败（2026-08-09，不能作为基线）**：TEST133-135 增加
input/textarea 的 defaultValue、input 的 defaultChecked，以及 select 的
selectedIndex 读写；但设备自动运行在 TEST110 的 DOM bootstrap 阶段停止，
因为新增六个独立 JS 原生入口超过了既有 16 槽位上限，后续 TEST133-135
没有执行。该失败是桥接集成错误，不是放宽断言或改变脚本槽位上限的理由。

**next166 设备验收通过（2026-08-09）**：保留上述六个属性和 TEST133-135
断言，只把 JS bridge 合并为一个按操作分发的原生入口，使 bootstrap 继续
容纳事件入口并保持 PSCRIPT_MAX_NATIVE_FUNCTIONS=16。默认 javascript=0、
TEST13 网络路径不变。`screen=320x320 dpi=128` 自动日志包含 83 条 OK、零 ERROR
并以 `TESTBENCH PASS` 结束；人工视觉和真实 SIP/IME 仍不由该日志证明。

**next167 设备验收通过（2026-08-09）**：针对真实 Learn More 点击触发的高 DPI
交互重排回归，`test_host` 在 form/interaction restyle 前重新调用
`PCore_SetDeviceViewport`，避免后续 layout 退回物理像素宽度的 CSS 视口。TEST76
增加 640x480/192 DPI 的两次交互重排几何断言；用户人工确认离开页边距恢复，随后
480x640/192 DPI 全自动回归通过。该修复不改变 core ABI、默认脚本开关或 TEST13
导航目标；同包真实 SIP 候选词点击已由用户人工确认通过。

**next168 自动设备验收通过（2026-08-09，人工后退纳入累计批次）**：新增仅限 Browse 宿主的 16 项成功 GET URL
历史；页面只有在成功换入后才提交历史位置，失败的后退重载不移动当前位置，POST 不入栈，
回退后再打开新链接会截断前向分支。渲染窗口无表单焦点时按左方向键重新加载上一项；
不加入页面缓存、前进按钮、持久历史或 JavaScript History API。TEST136 离线固定状态机，
默认自动配置扩展到 TEST136，TEST13 三段网络序列不变。C89、审计、ARMV4I Debug 构建和
`C:\WMShare\Positron-next168\test_host.log` 已在 640x480/192 DPI 得到 84 OK、零 ERROR
与最终 PASS。左键真实后退、失败网络和页面状态观感留到下一次累计人工检查。

**next169 自动设备验收通过（2026-08-09）**：继续复用同一个 `positron_script.dll`/Duktape
页面 context，增加只读 `location.href`、`document.URL/documentURI/location` 和最小
`history.back()`。URL 在 bootstrap 前由宿主复制进 runtime，随后藏入闭包；后退请求经
窗口消息排队，只在当前 JS 调用栈退出且导航空闲后复用 next168 的成功-GET 状态机，
不在 native callback 内重入网络或同步移动历史位置。TEST137 离线检查 URL 身份和延迟
后退请求；默认 `javascript=0`、TEST13 网络路径、core ABI 及 next168 设备基线不变。
C89 回归、仓库/文档审计和 VS2008 ARMV4I Debug 增量构建已通过，bootstrap 使用
14/16 个 native callback 槽位；`C:\WMShare\Positron-next169` 已隔离 staging，七个
ARMV4I 二进制与构建产物 SHA-256 一致。本批不代表完整
Location/History API、前进、replace/pushState、页面缓存或滚动/表单状态恢复。设备日志
在 320x320/128 DPI 下得到 TEST137 OK、85 OK、零 ERROR 与最终 PASS；真实脚本触发
公网后退的观感和失败网络行为并入累计人工检查。

**next170 自动设备验收通过（2026-08-09）**：在 next169 的同一异步导航桥上加入
`location.assign()`、`location.href=`、`window.location=` 与 `document.location=`。
脚本 callback 只保存最后一个请求并投递窗口消息，退出 JS 调用栈且导航空闲后才复用
现有 GET 导航事务；当前 URL 和已提交 history 不会在赋值时同步改变。TEST138 离线固定
上述四个入口、last-request-wins、延迟提交和 14/16 native callback 槽位；默认
`javascript=0`、TEST13、core ABI 与 next169 已验收行为不变。C89 回归、仓库/文档审计和
VS2008 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next170` 已隔离 staging，
七个 ARMV4I 二进制与构建产物 SHA-256 一致。320x320/128 DPI 设备日志得到 TEST138 OK、
86 条 OK、零 ERROR、零 FAIL 与最终 PASS，next170 已成为自动化设备基线。

**next171 自动设备验收通过（2026-08-09）**：在同一异步导航 bridge 上增加
`location.reload()`；bootstrap 把闭包中的 canonical document URL 随 reload 请求传给宿主，
native callback 只排队，退出 JS 调用栈且导航空闲后才复用现有 GET 事务。当前 URL/history
不会同步改变；成功重载同一当前 URL 时沿用既有 duplicate-current guard，不增加历史项，
也不截断已有 forward branch。TEST139 离线固定返回 `undefined`、延迟请求、URL 身份、
history 保持和 14/16 native callback 槽位；默认 `javascript=0`、TEST13 和 core ABI 不变。
C89、仓库/文档审计和 VS2008 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next171` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志得到 TEST139 OK、87 条 OK、零 ERROR、零 FAIL 与最终 PASS，
next171 已成为自动化设备基线。

**next172 自动设备验收通过（2026-08-09）**：新增 GET-only `location.replace()`，仍经同一
native callback 和窗口消息异步进入导航事务。history 新增具名 replace-current 提交模式：
失败不变，成功只替换当前 URL，保留 count/index 及相邻 back/forward 条目。TEST140 离线
固定返回 `undefined`、callback 前 URL/history 不变、成功提交结果和 14/16 native callback
槽位；默认 `javascript=0`、TEST13 与 core ABI 不变。C89、仓库/文档审计和 VS2008
ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next172` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志得到 TEST140 OK、88 条 OK、零 ERROR、零 FAIL 与最终 PASS，
next172 已成为自动化设备基线。

**next173 自动设备验收通过（2026-08-09）**：新增异步 `history.forward()`，只读取现有
forward target 并排队 GET；callback 返回和网络失败都不移动 index，只有成功 document
提交才切换到目标项。TEST141 离线固定返回 `undefined`、当前 URL/index 同步不变、目标
查询与成功提交；继续使用 14/16 native callback 槽位。默认 `javascript=0`、TEST13、
core ABI 与键盘 UI 不变；`history.go()`、右方向键、页面缓存和状态恢复不在本批。C89、
仓库/文档审计及 VS2008 ARMV4I Debug 增量构建已通过。
`C:\WMShare\Positron-next173` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志得到 TEST141 OK、89 条 OK、零 ERROR、零 FAIL 与最终 PASS，
next173 已成为自动化设备基线。

**next174 自动设备验收通过（2026-08-09）**：新增异步 `history.go(delta)`，只接受
`-15…15` 的整数偏移；越界、非有限或小数请求无操作且不覆盖最后一个有效排队请求。
`go(0)` 指向当前 GET 条目，非零偏移使用有界 history target；均只有成功 document 提交
才移动 index。TEST142 离线固定负/零/正/越界目标、JavaScript 数值转换、last-valid-wins、
同步 URL/index 不变及成功提交；仍使用 14/16 native callback 槽位。默认
`javascript=0`、TEST13 与 core ABI 不变，页面缓存、状态恢复、length/state/popstate 不在
本批。C89、仓库/文档审计及 ARMV4I Debug 增量构建已通过。
`C:\WMShare\Positron-next174` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志得到 TEST142 OK、90 条 OK、零 ERROR、零 FAIL 与最终 PASS，
next174 已成为自动化设备基线。

**next175 自动设备验收通过（2026-08-09）**：为最小 History API 增加只读
`history.length`。宿主在脚本 bootstrap 前计算当前 document 成功提交后的预期历史长度：
首次 document 至少为 1，普通成功 GET 会计入新条目并先反映 forward 分支截断，
back/forward/go、replace、POST 或失败提交不增加长度；结果受现有 16 项上限约束并作为
当前 document 快照注入。TEST143 离线固定首次/追加/分支/replace/target/POST 计数、只读
赋值、同步 traversal 不变和 14/16 native callback 槽位。默认 `javascript=0`、TEST13、
core ABI 与 next174 基线不变；`history.state`、push/replaceState、popstate 和页面缓存不在
本批。C89 回归及 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next175` 已隔离
staging，七个 ARMV4I 二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志得到
TEST143 OK、91 条 OK、零 ERROR、零 FAIL 与最终 PASS，next175 已成为自动设备基线。

**next176 自动设备验收通过（2026-08-09）**：为尚未使用 state mutation API 的初始/网络
document 暴露只读 `history.state === null`。属性使用无 setter 的 getter；脚本赋值不会
改变值，`history.go(0)` 的同步排队也不改变当前 state 或历史项。TEST144 离线固定 null
身份、只读描述符、赋值后不变、go(0) 延迟请求和 14/16 native callback 槽位。默认
`javascript=0`、TEST13、core ABI 与 next175 基线不变；pushState、replaceState、结构化
克隆、跨 document state、popstate 和同文档 URL 历史仍未实现。C89 回归及 ARMV4I Debug
增量构建已通过；`C:\WMShare\Positron-next176` 已隔离 staging，七个 ARMV4I 二进制与
构建产物 SHA-256 一致。320x320/128 DPI 日志得到 TEST144 OK、92 条 OK、零 ERROR、
零 FAIL 与最终 PASS，next176 已成为自动设备基线。

**next177 自动设备验收通过（2026-08-09）**：新增不改 URL 的受控
`history.replaceState(state, title)`。JSON-compatible state 序列化后必须小于 1024 字节；
title 忽略，第三个 URL 仅允许省略、空串或当前绝对 URL。getter 每次从 JSON 重新复制，
脚本修改返回对象不会回写。初始脚本的 state 先保存在候选 bridge，只有 document 最终
成功提交才写入对应成功 GET 条目；活动页面同步替换当前条目，back/forward/go/reload
按条目恢复且不增加 length。TEST145 离线固定 URL 拒绝、clone 隔离、成功提交、活动替换、
逐项恢复和 14/16 native callback 槽位。默认 `javascript=0`、TEST13、core ABI 与 next176
基线不变；完整 structured clone、pushState、非当前 URL 改写、popstate、POST state 和页面
缓存仍未实现。C89 回归及 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next177` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志得到 TEST145 OK、93 条 OK、零 ERROR、零 FAIL 与最终 PASS，
next177 已成为自动设备基线。

**next178 自动设备验收通过（2026-08-09）**：在 next177 的逐项 JSON state 上新增同 URL、
不联网的受控 `history.pushState(state, title)`。state 仍须序列化为小于 1024 字节的 JSON，
title 忽略，第三个 URL 只允许省略、空串或当前绝对 URL；调用同步追加条目并更新只读
length/state，截断 forward 分支，最多保留 16 项。初始 GET 脚本的多次 push/replace 先按
顺序留在候选 bridge，document 最终成功后才提交；活动页立即提交。TEST146 离线固定
成功提交隔离、多次操作顺序、clone、URL 拒绝、同步 length、活动追加、前向截断、逐项
恢复和 14/16 native callback 槽位。默认 `javascript=0`、TEST13、core ABI 与 next177
基线不变；遍历仍复用现有 GET 重载，不实现完整 structured clone、非当前 URL、POST state、
同 document 生命周期、`popstate` 或页面缓存。C89 回归及 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next178` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志得到 TEST146 OK、94 条 OK、零 ERROR、零 FAIL 与最终 PASS，
next178 已成为自动设备基线。

**next179 自动设备验收通过（2026-08-09）**：为成功网络 document 分配内部 identity，
pushState 条目继承当前 identity。back/forward 和非零 `history.go()` 命中同一 identity 的
pushed sibling 时，不再启动 GET 或替换页面，而是在现有 DOM/runtime 中切换 history index
和逐项 JSON state；length 保持不变。go(0)、reload、跨 identity 条目仍走现有网络路径，
成功网络 document 会获得新 identity。TEST147 离线固定 back/forward/go 的无 GET 切换、
DOM/runtime 身份、state/length、go(0) 排除以及 reload/跨 document 隔离。默认
`javascript=0`、TEST13、core ABI 与 next178 基线不变；本批尚无 `popstate`，也不实现逐项
滚动/表单恢复、非当前 URL、POST state 或跨 document 页面缓存。C89 回归及 ARMV4I Debug
增量构建已通过；`C:\WMShare\Positron-next179` 已隔离 staging，七个 ARMV4I 二进制与
构建产物 SHA-256 一致。320x320/128 DPI 日志得到 TEST147 OK、95 条 OK、零 ERROR、
零 FAIL 与最终 PASS，next179 已成为自动设备基线。

**next180 自动设备基线（2026-08-09）**：next179 的同 document traversal 在切换 index
和 history.state 后派发最小 `popstate`。支持 `window.onpopstate` 以及仅面向 popstate 的
window `addEventListener/removeEventListener`；事件 state 是独立 JSON clone，target/
currentTarget 为 window，bubbles/cancelable 为 false，handler 异常不会撤销遍历。
pushState/replaceState 本身不派发。TEST148 离线固定异步 back 边界、state-before-event、
属性/listener 回调、重复 listener 去重、remove、clone/异常隔离、事件元数据以及 push/replace
静默。默认 `javascript=0`、TEST13、core ABI 与 next179 基线不变；这不是完整 Window
EventTarget 或 PopStateEvent 构造器，跨 document traversal 仍不派发，逐项滚动/表单恢复、
非当前 URL、POST state 和页面缓存仍未实现。C89 回归及 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next180` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志得到 TEST148 OK、96 条 OK、零 ERROR、零 FAIL 与最终 PASS，
next180 已成为自动设备基线。

**next181 自动设备基线（2026-08-09）**：`history.replaceState/pushState` 现在接受当前
document 基础 URL 上的片段 URL（`#fragment`、无片段基础 URL 或同基础 URL 的绝对形式），
同步更新 `location.href`、`document.URL/documentURI` 与逐项 history URL；同 document
back/forward/go 在 popstate 前恢复目标 URL，全程不发起 GET。TEST149 离线固定初始与运行期
replace/push、前向分支截断、片段清除/恢复、URL-before-popstate 及路径/查询/跨源拒绝。
默认 `javascript=0`、TEST13、core ABI、14/16 callback 槽位与 next180 基线不变；普通相对
URL、路径/查询变化、hashchange、滚动/表单恢复和跨 document 页面缓存仍未实现。C89 回归及
ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next181` 已隔离 staging，七个 ARMV4I
二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志得到 TEST149 OK、97 条 OK、零
ERROR、零 FAIL 与最终 PASS，next181 已成为自动设备基线。

**next182 自动设备基线（2026-08-09）**：片段发生变化的同 document history traversal
现在在 `popstate` 之后派发最小 `hashchange`。支持 `window.onhashchange` 以及 window
`addEventListener/removeEventListener('hashchange', ...)`；事件提供 oldURL/newURL、window
target/currentTarget、不可取消元数据和异常隔离，重复 listener 去重且 remove 生效。
pushState/replaceState 自身以及相同片段 traversal 保持静默。TEST150 离线固定事件顺序、URL
元数据、属性/listener、异常/取消隔离、重复去重、remove 和静默边界。默认 `javascript=0`、
TEST13、core ABI 与 14/16 callback 槽位不变；location.hash/片段赋值、跨 document
hashchange、完整 Window EventTarget/HashChangeEvent、滚动/表单恢复和页面缓存仍未实现。
C89 回归及 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next182` 已隔离 staging，
七个 ARMV4I 二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志得到 TEST150 OK、
98 条 OK、零 ERROR、零 FAIL 与最终 PASS，next182 已成为自动设备基线。

**next183 自动设备基线（2026-08-09）**：`location` 新增动态只读 `protocol`、`host`、
`hostname`、`port`、`pathname`、`search`、`hash` 与 `origin`。getter 每次解析当前绝对
HTTP(S) `href`，因此 replaceState/pushState 与同 document traversal 后会与
`document.URL` 同步；TEST151 离线固定显式端口、路径/查询/片段/origin、只读 descriptor、
replace/push/back/forward 和 14/16 callback 槽位。默认 `javascript=0`、TEST13 与 core ABI
不变；组件 setter、username/password、完整 URL 标准化、通用相对导航和 location 片段导航
仍未实现。C89 回归及 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next183` 已
隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256 一致。首次设备启动暴露 2079 字节
ini 超过旧 2048 字节读取上限，测试未执行；候选现已把有界上限提升到 4096 字节并重新
构建/staging。修复后的 320x320/128 DPI 日志正确选择配置并得到 TEST151 OK、99 条 OK、
零 ERROR、零 FAIL 与最终 PASS，next183 已成为自动设备基线。

**next184 自动设备基线（2026-08-09）**：`location.hash` setter 现在通过既有 WM 消息队列
延迟执行同 document 片段导航；成功后新增 null-state history entry、同步 href/components 与
history.length，只派发 hashchange 而不发起 GET 或 popstate。相同值静默，空字符串清除片段；
后续 back 仍按 popstate→hashchange 顺序遍历。TEST152 离线固定异步边界、same-value、清除、
state/length、事件顺序、无网络与 14/16 callback 槽位。默认 `javascript=0`、TEST13、core ABI
不变；`location.href/assign/replace` 的相对片段、百分号标准化、锚点滚动及其他组件 setter 仍未
实现。C89 回归及 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next184` 的七个
ARMV4I 二进制与构建产物 SHA-256 一致。320x320/128 DPI 设备日志得到 TEST152 OK、配置
所选 99 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；其中 TEST13 使用 `OK (overview)` 行，
其余 98 项使用标准数字 OK 行。

**next185 自动设备基线（2026-08-09）**：`location.href='#...'`、`location.assign('#...')`
与 `location.replace('#...')` 现在复用同文档片段队列；前两者新增 null-state history entry，
replace 替换当前 entry 且不增加 history.length。三种入口都保持延迟提交、无 GET/popstate、
仅派发 hashchange，相同目标静默，后退仍为 popstate→hashchange。TEST153 固定三入口的
异步边界、state/length、replace 文档身份、事件与无网络行为；其他相对/绝对 URL 仍走既有
跨文档路径。百分号编码/URL 标准化、锚点滚动、跨 document 片段导航和其他组件 setter 不在
本批；默认 `javascript=0`、TEST13、core ABI 与 14/16 callback 槽位保持不变。
C89 回归、仓库审计和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next185` 的七个
ARMV4I 二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志得到 TEST153 OK、配置所选
100 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 99 项
使用标准数字 OK 行。

**next187 自动设备基线（2026-08-12）**：根相对 `location.href/assign/replace` URL 在解析后
与当前 path/query 完全相同且仅改变 fragment 时，复用同文档片段队列；当前确有 fragment 时
也可用匹配的根相对基址清除它。href/assign 新增 null-state entry，replace 替换当前 entry，
提交保持延迟、无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL 根相对导航，
以及 query/path 不同目标仍走普通导航。TEST155 固定三入口、清除、same-value、history/state、
事件、无网络和分类边界；query-only、普通 path-relative、dot-segment、百分号标准化、锚点滚动
及其他组件 setter 不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback 槽位不变。
C89 回归、仓库审计和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next187` 的七个
ARMV4I 二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志得到 TEST155 OK、配置所选
102 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 101 项
使用标准数字 OK 行。

**next188 自动设备基线（2026-08-12）**：query-relative `location.href/assign/replace` URL 在解析后
与当前 pathname/query 完全相同且仅改变 fragment 时复用同文档片段队列；当前确有 fragment 时
也可用匹配 query 清除它。href/assign push，replace 替换当前 entry，保持延迟提交、无 GET/
popstate、只派发 hashchange。当前无 fragment 的同 query 导航、不同 query 和普通 path-relative
目标仍走普通导航。TEST156 固定三入口、清除、same-value、history/state、事件、无网络和分类
边界；普通 path-relative、dot-segment、百分号标准化、锚点滚动及其他组件 setter 不在本批。
默认 `javascript=0`、TEST13、core ABI 与 14/16 callback 槽位不变。C89 回归和 ARMV4I Debug
增量构建已通过；`C:\WMShare\Positron-next188` 的七个 ARMV4I 二进制与构建产物 SHA-256
一致。240x240/96 DPI 日志得到 TEST156 OK、配置所选 103 项全部 OK、零 ERROR、零 FAIL 与
最终 PASS；TEST13 使用 `OK (overview)`，其余 102 项使用标准数字 OK 行。

**next189 自动设备基线（2026-08-12）**：不带 `./` 或 `../` 前缀的同目录 path-relative
`location.href/assign/replace` URL，在解析后与当前 path/query 完全相同且仅改变 fragment 时复用
同文档片段队列；当前确有 fragment 时也可用匹配的相对文件名清除它。href/assign push，replace
替换当前 entry，保持延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、
不同 path/query，以及带点段前缀的目标仍走普通导航。TEST157 固定三入口、清除、same-value、
history/state、事件、无网络和分类边界；点段归一化、百分号标准化、锚点滚动及其他组件 setter
不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback 槽位不变。C89 回归和
ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next189` 的七个 ARMV4I 二进制与构建
产物 SHA-256 一致。240x240/96 DPI 修复后日志得到 TEST156/157 OK、配置所选 104 项全部 OK、
零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 103 项使用标准数字 OK 行。

**next190 自动设备基线（2026-08-12）**：带单个 `./` 前缀的同目录
`location.href/assign/replace` URL，在移除该前缀并解析后与当前 path/query 完全相同且仅改变
fragment 时复用同文档片段队列；当前确有 fragment 时也可用匹配目标清除它。href/assign push，
replace 替换当前 entry，保持延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的
同 URL、不同 path/query 和 `../` 父目录目标仍走普通导航。TEST158 固定三入口、清除、
same-value、history/state、事件、无网络和分类边界；`../`、重复/混合点段归一化、百分号标准化、
锚点滚动及其他组件 setter 不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback
槽位不变。C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next190` 的七个
ARMV4I 二进制与构建产物 SHA-256 一致。240x240/96 DPI 日志得到 TEST158 OK、配置所选 105 项
全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 104 项使用标准
数字 OK 行。

**next191 自动设备基线（2026-08-12）**：带单个 `../` 前缀的
`location.href/assign/replace` URL，在上移一个目录并解析后与当前 path/query 完全相同且仅改变
fragment 时复用同文档片段队列；当前确有 fragment 时也可用匹配目标清除它。href/assign push，
replace 替换当前 entry，保持延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的
同 URL、不同 path/query 和重复 `../../` 目标仍走普通导航。TEST159 固定三入口、清除、
same-value、history/state、事件、无网络和分类边界；重复/混合点段归一化、百分号标准化、锚点
滚动及其他组件 setter 不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback 槽位
不变。C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next191` 的七个 ARMV4I
二进制与构建产物 SHA-256 一致。240x240/96 DPI 日志得到 TEST159 OK、配置所选 106 项全部 OK、
零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 105 项使用标准数字 OK 行。

**next192 自动设备基线（2026-08-12）**：连续多个前导 `../` 的
`location.href/assign/replace` URL，逐级上移目录并解析后与当前 path/query 完全相同且仅改变
fragment 时复用同文档片段队列；越过 origin 根的额外父目录段钳制在根。当前确有 fragment 时
也可用匹配目标清除它。href/assign push，replace 替换当前 entry，保持延迟提交、无 GET/
popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query 和混合 `.././` 目标
仍走普通导航。TEST160 固定三入口、清除、same-value、history/state、事件、无网络和分类边界；
混合/内嵌点段归一化、百分号标准化、锚点滚动及其他组件 setter 不在本批。默认
`javascript=0`、TEST13、core ABI 与 14/16 callback 槽位不变。C89 回归和 ARMV4I Debug 增量
构建已通过；`C:\WMShare\Positron-next192` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。
240x240/96 DPI 日志得到 TEST160 OK、配置所选 107 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；
TEST13 使用 `OK (overview)`，其余 106 项使用标准数字 OK 行。

**next193 自动设备基线（2026-08-12）**：连续前导 `../` 之后允许一个 `./` 的
`location.href/assign/replace` URL，先逐级上移目录、再移除该单点段，解析后与当前 path/query
完全相同且仅改变 fragment 时复用同文档片段队列；当前确有 fragment 时也可用匹配目标清除。
href/assign push，replace 替换当前 entry，保持延迟提交、无 GET/popstate、只派发 hashchange。
当前无 fragment 的同 URL、不同 path/query 和重复 `../././` 目标仍走普通导航。TEST161 固定
三入口、清除、same-value、history/state、事件、无网络和分类边界；重复/任意内嵌点段归一化、
百分号标准化、锚点滚动及其他组件 setter 不在本批。默认 `javascript=0`、TEST13、core ABI 与
14/16 callback 槽位不变。C89 回归和 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next193` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。240x240/96 DPI
日志得到 TEST161 OK、配置所选 108 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用
`OK (overview)`，其余 107 项使用标准数字 OK 行。

**next194 自动设备基线（2026-08-12）**：连续前导 `../` 之后允许连续多个 `./` 的
`location.href/assign/replace` URL，先逐级上移目录、再移除这些单点段，解析后与当前 path/query
完全相同且仅改变 fragment 时复用同文档片段队列；当前确有 fragment 时也可用匹配目标清除。
href/assign push，replace 替换当前 entry，保持延迟提交、无 GET/popstate、只派发 hashchange。
当前无 fragment 的同 URL、不同 path/query 和路径中部 `segment/../` 目标仍走普通导航。
TEST162 固定三入口、清除、same-value、history/state、事件、无网络和分类边界；任意内嵌点段
归一化、百分号标准化、锚点滚动及其他组件 setter 不在本批。默认 `javascript=0`、TEST13、
core ABI 与 14/16 callback 槽位不变。C89 回归和 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next194` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。240x240/96 DPI
日志得到 TEST162 OK、配置所选 109 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用
`OK (overview)`，其余 108 项使用标准数字 OK 行。

**next195 自动设备基线（2026-08-12）**：无父目录前缀时允许连续多个前导 `./` 的
`location.href/assign/replace` URL，移除这些单点段后与当前 path/query 完全相同且仅改变
fragment 时复用同文档片段队列；当前确有 fragment 时也可用匹配目标清除。href/assign push，
replace 替换当前 entry，保持延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment
的同 URL、不同 path/query 和路径中部 `segment/../` 目标仍走普通导航。TEST163 固定三入口、
清除、same-value、history/state、事件、无网络和分类边界；任意内嵌点段归一化、百分号标准化、
锚点滚动及其他组件 setter 不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback
槽位不变。C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next195` 的七个
ARMV4I 二进制与构建产物 SHA-256 一致。240x240/96 DPI 日志得到 TEST163 OK、配置所选 110 项
全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 109 项使用标准
数字 OK 行。

**next196 自动设备基线（2026-08-12）**：连续前导 `../` 逐级上移后，允许余下路径中出现一个内嵌
`./` 的 `location.href/assign/replace` URL；移除该单点段后与当前 path/query 完全相同且仅改变
fragment 时复用同文档片段队列，当前确有 fragment 时也可用匹配目标清除。href/assign push，
replace 替换当前 entry，保持延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment
的同 URL、不同 path/query 和连续内嵌 `././` 目标仍走普通导航。TEST164 固定三入口、清除、
same-value、history/state、事件、无网络和分类边界；多个内嵌点段、内嵌父目录、百分号标准化、
锚点滚动及其他组件 setter 不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback
槽位不变。C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next196` 的七个
ARMV4I 二进制与构建产物 SHA-256 一致。240x240/96 DPI 日志得到 TEST164 OK、配置所选 111 项
全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 110 项使用标准
数字 OK 行。

**next197 自动设备基线（2026-08-12）**：连续前导 `../` 逐级上移后，允许余下路径同一位置出现连续
多个内嵌 `./` 的 `location.href/assign/replace` URL；移除该连续单点段后与当前 path/query 完全
相同且仅改变 fragment 时复用同文档片段队列，当前确有 fragment 时也可用匹配目标清除。
href/assign push，replace 替换当前 entry，保持延迟提交、无 GET/popstate、只派发 hashchange。
当前无 fragment 的同 URL、不同 path/query、分离位置的多个 `./` 和内嵌 `../` 目标仍走普通导航。
TEST165 固定三入口、清除、same-value、history/state、事件、无网络和分类边界；任意多位置点段
归一化、内嵌父目录、百分号标准化、锚点滚动及其他组件 setter 不在本批。默认 `javascript=0`、
TEST13、core ABI 与 14/16 callback 槽位不变。C89 回归和 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next197` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。240x240/96 DPI
日志得到 TEST165 OK、配置所选 112 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用
`OK (overview)`，其余 111 项使用标准数字 OK 行。

**next198 自动设备基线（2026-08-12）**：连续前导 `../` 逐级上移后，允许余下路径多个位置出现内嵌
`./` 的 `location.href/assign/replace` URL；移除这些单点段后与当前 path/query 完全相同且仅改变
fragment 时复用同文档片段队列，当前确有 fragment 时也可用匹配目标清除。href/assign push，
replace 替换当前 entry，保持延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的
同 URL、不同 path/query 和内嵌 `../` 目标仍走普通导航。TEST166 固定三入口、清除、same-value、
history/state、事件、无网络和分类边界；无父目录前缀的内嵌点段、内嵌父目录、百分号标准化、
锚点滚动及其他组件 setter 不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback
槽位不变。C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next198` 的七个
ARMV4I 二进制与构建产物 SHA-256 一致。240x240/96 DPI 日志得到 TEST166 OK、配置所选 113 项
全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 112 项使用标准
数字 OK 行。

**next199 自动设备基线（2026-08-12）**：根相对 `location.href/assign/replace` URL 允许路径中出现
一个内嵌 `./`；移除该单点段后与当前 path/query 完全相同且仅改变 fragment 时复用同文档片段
队列，当前确有 fragment 时也可用匹配目标清除。href/assign push，replace 替换当前 entry，
保持延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query、
连续内嵌 `././` 和内嵌 `../` 目标仍走普通导航。TEST167 固定三入口、清除、same-value、history/
state、事件、无网络和分类边界；根相对多个点段、绝对 URL 点段、内嵌父目录、百分号标准化、
锚点滚动及其他组件 setter 不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback
槽位不变。原共享目录 fixture 连续两次在 TEST70 `WriteFile` 返回 0、写入 0/10 字节且错误码为 0；
修复包把临时上传文件移到设备本地 `\Temp`，同时保留精确失败诊断、内容和清理断言。C89 回归和
ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next199-fix` 的七个 ARMV4I 二进制与构建
产物 SHA-256 一致。240x240/96 DPI 日志得到 TEST70 与 TEST167 OK、配置所选 114 项全部 OK、
零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 113 项使用标准数字 OK 行。

**next200 自动设备基线（2026-08-12）**：根相对 `location.href/assign/replace` URL 允许同一路径位置
连续出现 `././`；移除连续点段后与当前 path/query 完全相同且仅改变 fragment 时复用同文档片段
队列，当前确有 fragment 时也可用匹配目标清除。href/assign push，replace 替换当前 entry，保持
延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query、多个
分离内嵌 `./` 位置和内嵌 `../` 目标仍走普通导航。TEST168 固定三入口、清除、same-value、history/
state、事件、无网络和分类边界；根相对多个分离点段、绝对 URL 点段、内嵌父目录、百分号标准化、
锚点滚动及其他组件 setter 不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback
槽位不变。C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next200` 的七个
ARMV4I 二进制与构建产物 SHA-256 一致。640x480/192 DPI 日志得到 TEST70、TEST167 与 TEST168
OK、配置所选 115 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，
其余 114 项使用标准数字 OK 行。

**next201 自动设备基线（2026-08-12）**：根相对 `location.href/assign/replace` URL 允许多个分离路径
位置出现内嵌 `./`；移除所有这些点段后与当前 path/query 完全相同且仅改变 fragment 时复用同文档
片段队列，当前确有 fragment 时也可用匹配目标清除。href/assign push，replace 替换当前 entry，
保持延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query、
绝对 URL 点段和内嵌 `../` 目标仍走普通导航。TEST169 固定三入口、清除、same-value、history/
state、事件、无网络和分类边界；绝对 URL 点段、内嵌父目录、百分号标准化、锚点滚动及其他组件
setter 不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback 槽位不变。首包因
TEST167 仍把本批新增能力当负边界而停止；修正版保持 TEST169 正向断言，把 TEST167 负边界换成
仍未支持的绝对 URL 点段。C89 回归和 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next201-fix` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。640x480/192
DPI 日志得到 TEST70、TEST167、TEST168 与 TEST169 OK、配置所选 116 项全部 OK、零 ERROR、
零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 115 项使用标准数字 OK 行。

**next202 自动设备基线（2026-08-12）**：绝对 `location.href/assign/replace` URL 允许路径中出现一个
内嵌 `./`；移除该点段后与当前 origin/path/query 完全相同且仅改变 fragment 时复用同文档片段
队列，当前确有 fragment 时也可用匹配目标清除。href/assign push，replace 替换当前 entry，保持
延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query、连续
内嵌 `././` 和内嵌 `../` 目标仍走普通导航。TEST170 固定三入口、清除、same-value、history/
state、事件、无网络和分类边界；绝对 URL 连续/多位置点段、内嵌父目录、百分号标准化、锚点滚动
及其他组件 setter 不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback 槽位不变。
C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next202` 的七个 ARMV4I 二进制与
构建产物 SHA-256 一致。640x480/192 DPI 日志得到 TEST70、TEST169 与 TEST170 OK、配置所选
117 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 116 项使用
标准数字 OK 行。

**next203 自动设备基线（2026-08-12）**：绝对 `location.href/assign/replace` URL 允许同一路径位置连续
出现 `././`；移除连续点段后与当前 origin/path/query 完全相同且仅改变 fragment 时复用同文档片段
队列，当前确有 fragment 时也可用匹配目标清除。href/assign push，replace 替换当前 entry，保持
延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query、多个
分离内嵌 `./` 位置和内嵌 `../` 目标仍走普通导航。TEST171 固定三入口、清除、same-value、history/
state、事件、无网络和分类边界；绝对 URL 多位置点段、内嵌父目录、百分号标准化、锚点滚动及其他
组件 setter 不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback 槽位不变。C89
回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next203` 的七个 ARMV4I 二进制与构建
产物 SHA-256 一致。640x480/192 DPI 日志得到 TEST70、TEST170 与 TEST171 OK、配置所选 118 项
全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 117 项使用标准数字
OK 行。

**next204 自动设备基线（2026-08-12）**：新增独立、可选的 `TEST999` 完成提示音门。
配置解析只把精确编号 999 视为特殊测试；该批尚未开放 172-998，next205/206 后续将
172/173 变为正式测试编号，当前 174-998 仍无效。TEST999 在所有普通所选测试之后
调用一次 `MessageBeep(MB_OK)` 并记录标准 `TEST 999 OK`。它不是全局退出钩子，因此前序失败
不会响。默认候选配置在 next203 的 118 项后追加 999；正式构建、C89 检查和 staging 哈希已
通过；设备日志得到 118 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS，
用户确认序列末尾实际响了一次，next204 已替代 next203 成为基线。

**next205 自动设备基线（2026-08-12）**：绝对 URL 的片段分类器从“只继续处理同一位置的
连续 `/./`”收敛为“遍历查询/fragment 前所有字面 `/./`”。TEST172 让多个分离位置通过
href/assign/replace 进入同文档队列并验证清除、same-value、history/state、hashchange 与无
GET；不同 query/path、百分号编码 `%2E` 和任何 `..` 仍走普通导航。默认 `javascript=0`、
TEST13、core ABI 和 14/16 callback 槽位不变。正式构建、C89 与 staging 哈希已通过，
`C:\WMShare\Positron-next205` 的 120 项设备日志门已通过，next205 已替代 next204 成为基线。
首轮日志在 TEST167 暴露 TEST167-169 的旧绝对多位置排除断言；修正版未放宽其他边界，已
覆盖重建同一 staging 并完整通过。

**next206 自动设备基线（2026-08-12）**：绝对 URL 片段分类器新增单个、内嵌完整 segment 的
ASCII 大小写不敏感 `%2e` 识别，对齐 [WHATWG URL Standard 的 single-dot 定义](https://url.spec.whatwg.org/#url-path-segment)。
TEST173 固定 href/assign/replace、清除、same-value、state/length、hashchange、无 GET 和
14/16 callback 槽位；next206 日志当时还把多个 `%2e` segment 与 `%2E%2E`、不同 query/path、
字面 `..` 一并留在普通导航。next210 基线只取代多个 single-dot 的旧排除断言。121 项设备日志
已全部通过。该批不实现 double-dot 的 `.%2e`/`%2e.`/`%2e%2e` 折叠、混合字面/编码点段、锚点
滚动或其他 location setter。正式构建、C89、staging 哈希和设备门均已通过。

**next207 定向自动设备基线（2026-08-12）**：同一绝对 URL 分类器继续识别 path 末尾、位于
query/fragment 或 URL 结尾前的单个 `%2e` segment；TEST174 保持 href/assign/replace、清除、
same-value、state/length、hashchange、无 GET 和 14/16 callback 槽位，并把混合编码点段、
`%2E%2E`、不同 query/path 与字面 `..` 留在普通导航。正式构建、C89 和 staging 哈希已通过。
默认候选只选择 TEST13/151-174/999（26 项）；next206 的 121 项日志作为最近全量检查点。
首轮 TEST174 的初始 href 因 `/%2e` 长度 off-by-one 未分类；修正版仅修正 5→4 的边界与截取，
随后 26 项定向设备门全部通过。

**next208 定向自动设备基线（2026-08-12）**：同文档片段分类器把单个根相对内嵌 `%2e`
segment 接到既有 root-relative 规范化路径。TEST175 保持 href/assign/replace、清除、same-value、
state/length、hashchange、无 GET 和 14/16 callback 槽位；多个编码点段、`%2E%2E`、不同
query/path 与字面 `..` 仍走普通导航。正式构建、C89 和 staging 哈希已通过；默认定向配置为
TEST13/151-175/999（27 项），设备日志全部通过；next206 仍是最近全量检查点。

**next209 定向自动设备基线（2026-08-12）**：同一根相对分类器继续识别 path 末尾、位于
query/fragment 或 URL 结尾前的单个 `%2e` segment。TEST176 固定 href/assign/replace、清除、
same-value、state/length、hashchange、无 GET 和 14/16 callback 槽位；混合编码点段、
`%2E%2E`、不同 query/path 与字面 `..` 留在普通导航。正式构建、C89 和 staging 哈希已通过；
默认定向配置 TEST13/151-176/999 的 28 项日志全部通过；next206 仍是最近全量检查点。

**next210 定向自动设备基线（2026-08-12）**：绝对 URL 分类器从单个扩展为多个内嵌 `%2e`
segment，并在移除全部点段后执行既有同文档比较。TEST177 保持 href/assign/replace、清除、
same-value、state/length、hashchange、无 GET、不同 query/path、父目录和 14/16 callback 槽位。
TEST173 的重复编码反向断言已撤掉；`%2E%2E`、内嵌+末尾混合与根相对重复编码仍不支持。
正式构建、C89 与 staging 哈希已通过；默认定向配置 TEST13/151-177/999 的 29 项日志全部
通过；next206 仍是最近全量检查点。

**next186 自动设备基线（2026-08-12）**：`location.href/assign/replace` 现在把与当前绝对
基址相同、仅改变 fragment 的 URL 识别为同文档导航，并允许在当前确有 fragment 时用绝对
基址清除它；href/assign 新增 null-state entry，replace 替换当前 entry。提交仍然延迟、无
GET/popstate、仅派发 hashchange。当前无 fragment 时再次导航到完全相同绝对 URL，以及查询、
路径或源不同的目标，仍走既有普通导航。TEST154 固定三入口、清除、history/state、事件、
无网络和普通导航边界；相对 path+fragment、百分号编码/标准化、锚点滚动和其他组件 setter
不在本批。默认 `javascript=0`、TEST13、core ABI 与 14/16 callback 槽位不变。
C89 回归、仓库审计和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next186` 的七个
ARMV4I 二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志得到 TEST154 OK、配置所选
101 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 100 项
使用标准数字 OK 行。

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

**next159 设备结果（2026-08-08，不能作为 PASS 基线）**：事件 JSON 现在把合法 non-BMP UTF-8 标量写成
两个 `\uXXXX` 代理项，由 Duktape JSON decoder 生成 CESU-8/ECMAScript UTF-16 字符串；
设备实际结果已得到两个 code unit、正确标量代码和正确事件顺序。TEST13 与 TEST20-121
通过，但 TEST122 的 target 监听器错误地预期后注册的取消监听器已经执行；实际 `false`
比错误期望 `true` 多 1 字节，导致 testbench 停止。

**next160 设备验收完成（2026-08-08）**：TEST122 的 SELECT 目标监听器先看到
`defaultPrevented=false`，父级冒泡监听器随后看到 `true`；这与监听器注册/执行顺序一致。
完整设备日志已通过，next157-159 的诊断包均由 next160 替代。

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
tests=13,20,27,43,44,56,58-77,80-171
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
  `keypress` 和完整 `beforeinput.data`；next160 已完成设备验收。
- TEST 124/125：验证旧 Input/Keyboard ABI 的默认值、size-tagged Ex ABI 的
  isComposing 和短结构拒绝；TEST126–128 验证脚本 DOM 文本/属性、表单 value
  以及 live checked 状态，均在首次 style/layout 前执行。
- TEST 123：验证 WM IME start/end 消息入口及共享 UTF-8 composition update 发射路径；
  自动 PASS 本身不等于真实 SIP；next167 已另行人工确认候选词点击可完整键入，仍不
  外推为任意 OEM 候选窗口、预编辑 UI 或完整 `isComposing` 语义均已验收。

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

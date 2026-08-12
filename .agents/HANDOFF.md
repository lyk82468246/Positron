# Positron Current Handoff

更新时间：2026-08-12
当前分支：`main`  
当前全量自动设备检查点：next206 已在 `screen=640x480 dpi=192` 下完成 TEST13 三段导航及
TEST20/27/43/44/56/58-77/80-173/999，日志 `C:\WMShare\Positron-next206\test_host.log`
包含配置所选 121 项全部 OK、零条 `[ERROR]`、零条 FAIL 并以 `TESTBENCH PASS` 结束；TEST13
使用 `OK (overview)`，其余 120 项使用标准数字 OK 行。next167 的高 DPI
交互重排修复与定向人工结果保持有效：Learn More 离开页居中，真实 SIP 候选词可完整
键入。next168 新增成功-GET URL 历史和左键后退，next169 新增最小脚本 location/history
后退桥；真实交互与后续高风险批次集中验收。
默认 `javascript=0` 与 TEST13 目标不变。

next204 已通过设备与人工提示音门：它只扩展 `test_host` 配置与回归宿主，允许在列表末尾选择精确的
`TEST999`。前序所选测试全部完成后，它调用一次 `MessageBeep(MB_OK)` 并写入
`TEST 999 OK`；前序失败、未选择 999 或其他退出路径不会响。C89 与 ARMV4I Debug 构建已
通过，`C:\WMShare\Positron-next204` 七个二进制哈希与构建产物一致；设备得到 119 项全部
OK、零 ERROR/FAIL 与最终 PASS，用户确认末尾实际听到一次提示音。

next205 已成为自动设备基线：绝对 href/assign/replace URL 会移除查询或 fragment 前所有分离的字面
`/./` 段；只有规范化后 path/query 与当前文档相同且 fragment 改变或清除，才复用同文档
队列。TEST172 覆盖三入口、清除、same-value、history/state、hashchange、无网络、不同
query/path、`%2E` 与 `..` 排除边界。默认 javascript=0、TEST13、core ABI 和 14/16 callback
槽位不变。C89、ARMV4I Debug 构建与 `C:\WMShare\Positron-next205` 七个二进制哈希已通过；
修正版设备门得到 120 项全部 OK、零 ERROR/FAIL 与最终 PASS。
首轮设备日志在 TEST167 停止：TEST167-169 仍把本批新增支持的同一个绝对多位置 `/./` 输入
断言为普通导航。修正版只删除这三条过时排除断言，保留 query/path、`%2E` 与 `..` 边界；
`C:\WMShare\Positron-next205` 已覆盖重建并重新核对七个哈希，随后完整复测通过。

next206 已成为自动设备基线：绝对 href/assign/replace URL 的单个内嵌完整 `%2E`/`%2e` segment 按
single-dot 处理；规范化后 path/query 与当前文档相同且 fragment 改变或清除时复用同文档
队列。TEST173 覆盖大小写、三入口、清除、same-value、state/length、hashchange、无 GET；
next206 日志当时还排除多个编码点段、`%2E%2E`、不同 query/path 与 `..`。next210 基线只用
TEST177 取代多个 single-dot 的旧排除断言。默认 javascript=0、TEST13、core
ABI 和 14/16 callback 槽位不变。C89、ARMV4I Debug 构建与
`C:\WMShare\Positron-next206` 七个二进制哈希已通过；设备门得到 121 项全部 OK、零
ERROR/FAIL 与最终 PASS。

next207 已成为定向自动设备基线：同一绝对 URL 分类器新增 path 末尾 `%2E`/`%2e` single-dot segment，覆盖
query/fragment 或 URL 结尾前的形式。TEST174 固定大小写、三入口、清除、same-value、
state/length、hashchange、无 GET，并排除混合内嵌/末尾编码点段、`%2E%2E`、不同 query/path
与 `..`。C89、ARMV4I Debug 构建及 `C:\WMShare\Positron-next207` 七个二进制哈希已通过；
定向设备门为 TEST13/151-174/999，共 26 项。
首轮日志在 TEST174 停止：实现把末尾 `/%2e` 误按内嵌 `/%2e/` 的 5 字符长度判断，初始 href
因此保持 `#old`。修正版只把终止位置和截取偏移从 5 改为 4；覆盖 staging 后的 26 项日志
得到 25 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS。

next208 已成为定向自动设备基线：根相对 href/assign/replace URL 中单个内嵌 `%2E`/`%2e` segment 按
single-dot 移除；规范化后 origin/path/query 匹配且 fragment 改变或清除时复用同文档队列。
TEST175 覆盖三入口、清除、same-value、state/length、hashchange、无 GET、重复编码点段、
`%2E%2E`、不同 query/path 与 `..` 排除边界。C89、ARMV4I Debug 构建及
`C:\WMShare\Positron-next208` 七个二进制哈希已通过；定向设备门为 TEST13/151-175/999，
共 27 项；日志得到 26 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS。

当前定向自动设备基线 next209：根相对 href/assign/replace URL 的 path 末尾 `%2E`/`%2e` segment 按
single-dot 移除；规范化后 origin/path/query 匹配且 fragment 改变或清除时复用同文档队列。
TEST176 覆盖三入口、清除、same-value、state/length、hashchange、无 GET、混合编码点段、
`%2E%2E`、不同 query/path 与 `..` 排除边界。C89、ARMV4I Debug 构建及
`C:\WMShare\Positron-next209` 七个二进制哈希已通过；定向设备门为 TEST13/151-176/999，
共 28 项；日志得到 27 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS。

当前定向自动设备基线 next210：绝对 href/assign/replace URL 中多个内嵌完整 `%2E`/`%2e` segment 依次按
single-dot 移除；规范化后 path/query 匹配且 fragment 改变或清除时复用同文档队列。TEST177
覆盖三入口、清除、same-value、state/length、hashchange、无 GET、不同 query/path 与父目录
排除边界。TEST173 的重复编码反向断言已撤掉，`%2E%2E`、内嵌+末尾混合和根相对重复编码
边界不变。C89、ARMV4I Debug 构建及 `C:\WMShare\Positron-next210` 七个二进制哈希已通过；
定向设备门为 TEST13/151-177/999，共 29 项；日志得到 28 条标准数字 OK、1 条 TEST13
overview、零 ERROR/FAIL 与最终 PASS。

当前全量自动设备基线 next211：根相对 href/assign/replace URL 中多个内嵌完整 `%2E`/`%2e` segment 依次按
single-dot 移除；规范化后 origin/path/query 匹配且 fragment 改变或清除时复用同文档队列。
TEST178 覆盖三入口、清除、same-value、state/length、hashchange、无 GET、不同 query/path
与父目录排除边界。TEST175 的重复编码反向断言已撤掉；`%2E%2E`、混合内嵌/末尾编码仍不
规范化。C89、ARMV4I Debug 构建及 `C:\WMShare\Positron-next211` 七个二进制哈希已通过；
这是 next206 后第 5 个低风险批次，设备门恢复为
TEST13/20/27/43/44/56/58-77/80-178/999，共 126 项；日志得到 125 条标准数字 OK、1 条
TEST13 overview、零 ERROR/FAIL 与最终 PASS，成为新的最近全量检查点。

当前定向自动设备基线 next212：绝对 href/assign/replace URL 中单个内嵌完整 `%2E%2E` double-dot segment
连同前一个非空目录折叠；规范化后 path/query 匹配且 fragment 改变或清除时复用同文档
队列。TEST179 覆盖三入口、清除、same-value、state/length、hashchange、无 GET、不同
query/path 与额外父目录边界；TEST173 中折叠后指向不同 path 的旧 double-dot 断言仍有效。
根相对、末尾、重复、混合点段和 `.%2e`/`%2e.` 拼写不变。C89、ARMV4I Debug 构建及
`C:\WMShare\Positron-next212` 七个二进制哈希已通过；定向门为 TEST13/151-179/999，
共 31 项；日志得到 30 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS。

当前定向自动设备基线 next213：根相对 href/assign/replace URL 中单个内嵌完整 `%2E%2E` double-dot segment
连同前一个非空目录折叠；补回当前 origin 后，path/query 匹配且 fragment 改变或清除时复用
同文档队列。TEST180 覆盖三入口、清除、same-value、state/length、hashchange、无 GET、不同
query/path 与额外父目录边界；TEST175 中折叠后指向不同 path 的旧 double-dot 断言仍有效。
末尾、重复、混合点段和 `.%2e`/`%2e.` 拼写不变。C89、ARMV4I Debug 构建及
`C:\WMShare\Positron-next213-fix` 七个二进制哈希已通过；定向门为 TEST13/151-180/999，
共 32 项；修正版日志得到 31 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终
PASS。首包的 ini 为 4120 字节，超过既有 4096 字节读取上限，因而在任何
测试执行前被忽略；修正版保持配置值不变，只精简注释至 1357 字节。旧目录因进程锁定保留为
失败证据，不得作为候选运行。

当前定向自动设备基线 next214：绝对 href/assign/replace URL 的 path 末尾 segment 若完整匹配 `%2E%2E`，
连同前一个非空目录折叠并保留结尾 `/`；规范化后 path/query 匹配且 fragment 改变或清除时
复用同文档队列。TEST181 覆盖三入口、清除、same-value、state/length、hashchange、无 GET、
不同 query/path、混合 single/double、重复 double-dot 与字面父目录边界。根相对末尾和
`.%2e`/`%2e.` 拼写不变。C89、ARMV4I Debug 构建及 `C:\WMShare\Positron-next214` 七个
二进制哈希已通过；ini 为 1357 字节，定向门为 TEST13/151-181/999，共 33 项；日志得到
32 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS。

当前定向自动设备基线 next215：根相对 href/assign/replace URL 的 path 末尾 segment 若完整匹配
`%2E%2E`，连同前一个非空目录折叠并保留结尾 `/`；补回当前 origin 后，path/query 匹配且
fragment 改变或清除时复用同文档队列。TEST182 覆盖三入口、清除、same-value、state/length、
hashchange、无 GET、不同 query/path、混合 single/double、重复 double-dot 与字面父目录边界。
`.%2e`/`%2e.` 拼写不变。C89、ARMV4I Debug 构建及 `C:\WMShare\Positron-next215` 七个
二进制哈希已通过；ini 为 1357 字节，定向门为 TEST13/151-182/999，共 34 项；日志得到
33 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS。

当前全量自动设备基线 next216：绝对 href/assign/replace URL 中单个内嵌完整 `.%2E` 或 `%2E.`
double-dot segment 连同前一个非空目录折叠；规范化后 origin/path/query 匹配且 fragment 改变
或清除时复用同文档队列。TEST183 覆盖两种拼写、三入口、清除、same-value、state/length、
hashchange、无 GET、不同 query/path、混合完整/半编码 double-dot、重复半编码 double-dot 与
字面父目录边界。根相对和末尾能力不变。C89、ARMV4I Debug 构建及
`C:\WMShare\Positron-next216` 七个二进制哈希已通过；ini 为 1294 字节，全量门为
TEST13/20/27/43/44/56/58-77/80-183/999，共 131 项；`screen=240x320 dpi=96` 日志得到
130 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS。

当前定向自动设备基线 next217：根相对 href/assign/replace URL 中单个内嵌完整 `.%2E` 或 `%2E.`
double-dot segment 连同前一个非空目录折叠；补回当前 origin 后，path/query 匹配且 fragment
改变或清除时复用同文档队列。TEST184 覆盖两种拼写、三入口、清除、same-value、state/length、
hashchange、无 GET、不同 query/path、混合完整/半编码 double-dot、重复半编码 double-dot 与
字面父目录边界。末尾能力不变。C89、ARMV4I Debug 构建及 `C:\WMShare\Positron-next217`
七个二进制哈希已通过；ini 为 1278 字节，定向门为 TEST13/151-184/999，共 36 项；设备日志
得到 35 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS。

当前定向自动设备基线 next218：绝对 href/assign/replace URL 的 path 末尾 segment 若完整匹配
`.%2E` 或 `%2E.`，连同前一个非空目录折叠并保留结尾 `/`；规范化后 path/query 匹配且
fragment 改变或清除时复用同文档队列。TEST185 覆盖两种拼写、三入口、清除、same-value、
state/length、hashchange、无 GET、不同 query/path、混合完整/半编码 double-dot、重复半编码
double-dot 与字面父目录边界。根相对末尾能力不变。C89、实际 JavaScript 探针、ARMV4I Debug
构建及 `C:\WMShare\Positron-next218` 七个二进制哈希已通过；ini 为 1278 字节，定向门为
TEST13/151-185/999，共 37 项；日志得到 36 条标准数字 OK、1 条 TEST13 overview、零
ERROR/FAIL 与最终 PASS。

当前定向自动设备基线 next219：根相对 href/assign/replace URL 的 path 末尾 segment 若完整匹配
`.%2E` 或 `%2E.`，连同前一个非空目录折叠并保留结尾 `/`；补回当前 origin 后，path/query
匹配且 fragment 改变或清除时复用同文档队列。TEST186 覆盖两种拼写、三入口、清除、
same-value、state/length、hashchange、无 GET、不同 query/path、混合完整/半编码 double-dot、
重复半编码 double-dot 与字面父目录边界。C89、实际 JavaScript 探针、ARMV4I Debug 构建及
`C:\WMShare\Positron-next219-fix` 七个二进制哈希已通过；ini 为 1278 字节，定向门为
TEST13/151-186/999，共 38 项；修正版设备日志得到 37 条标准数字 OK、1 条 TEST13 overview、
零 ERROR/FAIL 与最终 PASS。
首包 `C:\WMShare\Positron-next219` 的 20,991 字符 DOM bootstrap 在 TEST162 触发既有 1000ms
执行超时，TEST186/999 未运行。修正版不放宽预算，而是用共享 `ppartial` helper 把 bootstrap
降至 19,735 字符；14 组新旧正反行为探针通过。旧包不能作为候选结果。

当前验收/集成节奏：每个能力批次继续跑风险相关的自动设备门。低风险局部变更选择本批测试、
直接共享路径、TEST13 与 TEST999；累计约 5 个低风险批次，或触及公共 DLL/ABI、布局/重绘、
网络、输入基础设施、里程碑交付及任何异常时运行全量门。next216 的 131 项日志是最近全量
检查点。可能影响视觉、真实触摸、SIP、旋转
或网络失败交互的项目加入累计人工清单，若干批次后一次性检查。崩溃、数据损坏、严重布局
崩坏或核心交互阻塞仍立即人工复核。自动设备基线通过后应提交并推送其 scoped tracked
改动；`tmp/` 截图目录永远不加入 Git。

next163 已设备验收：它保留 next162 的主文档 GET 握手 EOF 单次重试，并加入
TEST124/125 的 size-tagged Input/Keyboard Ex isComposing ABI、TEST126 的
DOM text/attribute bridge、TEST127 的 input/textarea/select value 和 TEST128
的 live checkbox/radio checked。libdom 的 checked setter 已修正为不改写 parsed
checked attribute；默认 javascript=0，TEST13 不执行脚本。C89、仓库审计、VS2008
ARMV4I Debug 增量构建、关键文件哈希、staging 与设备日志均已完成；日志以
`TESTBENCH PASS` 结束。自动日志不覆盖真实 SIP/IME 候选窗口或视觉验收。

next164 已设备验收：它在显式 javascript=1 的 classic script context 中提供
Event.target/currentTarget ID、PElement.id/className、classList token 方法和受控
style declaration 方法；默认 javascript=0，TEST13 不执行脚本。C89、仓库审计、
VS2008 ARMV4I Debug 增量构建、staging 与设备日志均已完成并以 TESTBENCH PASS
结束，人工视觉检查仍是独立门槛。

next165 设备验收失败：它加入 TEST133-135 的 input/textarea defaultValue、
input defaultChecked 和 select.selectedIndex 读写、清空及越界拒绝，但六个
新增 JS 原生入口耗尽既有 16 槽位，TEST110 在 DOM bootstrap 阶段停止，TEST133-135
尚未执行；next165 不能作为基线。

next166 已设备验收：它保留上述属性和断言，只把六个新增 JS bridge 操作合并为
一个按操作分发的入口，继续保持 PSCRIPT_MAX_NATIVE_FUNCTIONS=16；320x320/128 DPI
自动日志的 TEST110、TEST133-135 和整批门禁均通过。

next167 已设备验收：真实点击 Learn More 会先设置 focus/active 并重选样式，旧宿主
随后 layout 时可能把物理像素宽度误当 CSS 视口，导致高 DPI 离开页贴左/溢出。
`pcore_restyle_form_state` 现在经统一 helper 重申物理客户区与 DPI，再执行 cache-only
style/layout；TEST76 覆盖两次交互重排，480x640/192 DPI 全自动日志通过。用户已确认
640x480/192 DPI 下离开页保持居中边距；真实 SIP 候选词点击也已另行人工通过。

next168 已通过自动设备门：Browse 宿主维护最多 16 个成功 GET URL；只有新 document
成功换入才提交，失败后退不移动 index，POST 不入栈，回退后新导航截断 forward branch。
无原生表单控件获得焦点时按左方向键重新加载上一 URL。TEST136 离线覆盖提交/失败/
截断边界；默认脚本、core ABI 和 TEST13 三段网络目标不变。ARMV4I Debug 构建与
`C:\WMShare\Positron-next168\test_host.log` 已以 TEST136 OK 和最终 PASS 结束；人工后退
不再单独阻塞该批，而是和后续可能影响交互/视觉的改动一起集中检查。

next169 已通过自动设备门：继续使用同一个 `positron_script.dll`/Duktape 页面 context，新增只读
`location.href`、`document.URL/documentURI/location` 与最小 `history.back()`。
宿主 URL 在 bootstrap 前复制进 runtime 后藏入闭包；back callback 只设置请求并投递
窗口消息，当前 JS 调用栈退出、页面成功提交且导航空闲后才复用 next168 状态机，因此不会
在 native callback 内重入网络或同步移动历史 index。TEST137 离线固定 URL 身份、对象
关系与延迟后退；默认 javascript=0、TEST13、core ABI 和 next168 设备基线不变。C89、
仓库/文档审计及 ARMV4I Debug 增量构建已通过，bootstrap 使用 14/16 个 native callback
槽位；`C:\WMShare\Positron-next169` 已隔离 staging，七个 ARMV4I 二进制与构建产物
SHA-256 一致。320x320/128 DPI 日志包含 TEST137 OK、85 条 OK、零 ERROR 和最终 PASS；
真实脚本触发公网后退及失败网络行为进入累计人工检查。

next170 已通过自动设备门：`location.assign()`、`location.href=`、`window.location=` 和
`document.location=` 现在通过 next169 的 bridge 保存最后一个导航请求，再投递窗口消息；
只有退出 JS callback 且导航空闲后才复用现有 GET 导航事务。赋值本身不立即修改闭包中的
当前 document URL 或已提交 history，连续赋值采用 last-request-wins。TEST138 离线固定四个
入口、延迟提交与 14/16 native callback 槽位；默认 javascript=0、TEST13、core ABI 和
next169 已验收行为不变。C89、仓库/文档审计及 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next170` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志包含 TEST138 OK、86 条 OK、零 ERROR、零 FAIL 与最终 PASS。

next171 已通过自动设备门：`location.reload()` 把 bootstrap 闭包中的 canonical document
URL 复制进 next170 的异步 bridge，退出 JS callback 且导航空闲后才复用普通 GET 事务。
当前 URL/history 不同步变化；同一当前 URL 成功提交时，现有 duplicate-current guard 保留
history count/index 和 forward branch。TEST139 离线固定返回值、URL 身份、延迟请求、
history 保持与 14/16 native callback 槽位；默认 javascript=0、TEST13、core ABI 和
next170 已验收行为不变。C89、仓库/文档审计及 ARMV4I Debug 增量构建已通过。
`C:\WMShare\Positron-next171` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志包含 TEST139 OK、87 条 OK、零 ERROR、零 FAIL 与最终 PASS。

next172 已通过自动设备门：GET-only `location.replace(url)` 经 next171 的同一异步
navigation bridge 排队；请求失败不修改 history，成功 document 提交通过具名
replace-current 模式只改当前 URL，并保留 count/index 及相邻 back/forward 条目。TEST140
离线固定返回值、同步 URL/history 不变、成功替换和 14/16 native callback 槽位；默认
javascript=0、TEST13、core ABI 和 next171 已验收行为不变。C89、仓库/文档审计及 ARMV4I
Debug 增量构建已通过。
`C:\WMShare\Positron-next172` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志包含 TEST140 OK、88 条 OK、零 ERROR、零 FAIL 与最终 PASS。

next173 已通过自动设备门：`history.forward()` 只读当前 forward target 并经既有窗口消息
排队 GET；callback 返回和网络失败不移动 index，只有成功 document 提交才切换到目标项。
TEST141 离线固定返回值、同步 URL/index 不变、目标查询、成功提交和 14/16 native callback
槽位；默认 javascript=0、TEST13、core ABI、键盘 UI 和 next172 已验收行为不变。C89、
仓库/文档审计及 ARMV4I Debug 增量构建已通过。
`C:\WMShare\Positron-next173` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志包含 TEST141 OK、89 条 OK、零 ERROR、零 FAIL 与最终 PASS。

next174 已通过自动设备门：`history.go(delta)` 只接受整数 `-15…15`；非法或越界值无操作，
合法值通过 next173 的同一异步消息入口排队。delta 0 指向当前 GET 条目，非零偏移只在目标
存在时导航，成功 document 提交后才移动 index。TEST142 离线固定负/零/正/越界目标、
数值转换、last-valid-wins、同步 URL/index 不变、成功提交和 14/16 native callback 槽位；
默认 javascript=0、TEST13、core ABI 和 next173 设备基线不变。C89、仓库/文档审计及
ARMV4I Debug 增量构建已通过。
`C:\WMShare\Positron-next174` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志包含 TEST142 OK、90 条 OK、零 ERROR、零 FAIL 与最终 PASS。

next175 已通过自动设备门：最小 History API 新增只读 `history.length`。宿主在 script
bootstrap 前计算当前 document 成功提交后的预期长度；首次 document 至少为 1，普通成功
GET 反映新增条目和 forward 分支截断，back/forward/go、replace、POST 或失败提交不增加
长度，现有 16 项上限保持。TEST143 离线固定首次/追加/分支/replace/target/POST 计数、
只读赋值、同步 traversal 不变和 14/16 native callback 槽位；默认 javascript=0、TEST13、
core ABI 与 next174 基线不变。C89 回归和 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next175` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志包含 TEST143 OK、91 条 OK、零 ERROR、零 FAIL 与最终 PASS。

next176 已通过自动设备门：在没有 state mutation API 的当前最小 History API 中，初始/
网络 document 暴露只读 `history.state === null`；无 setter，脚本赋值和同步排队的 go(0)
都不改变值或当前历史项。TEST144 离线固定 null 身份、只读描述符、赋值后不变、go(0)
延迟请求和 14/16 native callback 槽位；默认 javascript=0、TEST13、core ABI 与 next175
基线不变。push/replaceState、结构化克隆、跨 document state、popstate 和同文档 URL 历史
仍未实现。C89 回归及 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next176`
已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志包含
TEST144 OK、92 条 OK、零 ERROR、零 FAIL 与最终 PASS。

next177 已通过自动设备门：新增不改 URL 的受控 `history.replaceState(state, title)`；
JSON-compatible state 序列化后小于 1024 字节，title 忽略，URL 只允许省略、空串或当前
绝对 URL。getter 每次重新复制 JSON；初始脚本只更新候选 bridge，document 成功提交后
才写入对应成功 GET 条目，活动页面同步替换当前条目，遍历/重载按条目恢复且 length 不变。
TEST145 离线固定 clone 隔离、URL 拒绝、成功提交、活动替换、逐项恢复和 14/16 callback
槽位；默认 javascript=0、TEST13、core ABI 与 next176 基线不变。structured clone、
pushState、非当前 URL 改写、popstate、POST state 和页面缓存仍未实现。C89 回归及
ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next177` 已隔离 staging，七个
ARMV4I 二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志包含 TEST145 OK、93 条 OK、
零 ERROR、零 FAIL 与最终 PASS。

next178 已通过自动设备门：在 next177 的逐项 JSON state 上新增同 URL、不联网的受控
`history.pushState(state, title)`。state 仍须序列化为小于 1024 字节的 JSON，title 忽略，
URL 只允许省略、空串或当前绝对 URL；同步追加条目、更新 length/state、截断 forward 分支，
最多保留 16 项。初始 GET 脚本的多次 push/replace 先留在候选 bridge，document 最终成功
后按顺序提交，活动页立即提交。TEST146 离线固定成功提交隔离、多次操作顺序、clone、URL
拒绝、同步 length、活动追加、前向截断、逐项恢复和 14/16 callback 槽位。默认
javascript=0、TEST13、core ABI 与 next177 基线不变；遍历仍走现有 GET 重载，不实现完整
structured clone、非当前 URL、POST state、同 document 生命周期、popstate 或页面缓存。
C89 回归及 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next178` 已隔离 staging，
七个 ARMV4I 二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志包含 TEST146 OK、
94 条 OK、零 ERROR、零 FAIL 与最终 PASS。

next179 已通过自动设备门：成功网络 document 获得内部 identity，pushState 条目继承当前
identity。back/forward 和非零 history.go 命中同一 identity 的 pushed sibling 时，不启动
GET 或替换页面，而是在现有 DOM/runtime 内切换 index 和逐项 JSON state，length 不变。
go(0)、reload、跨 identity 条目仍走网络路径，成功网络 document 获得新 identity。TEST147
离线固定 back/forward/go 无 GET 切换、DOM/runtime 身份、state/length、go(0) 排除以及
reload/跨 document 隔离。默认 javascript=0、TEST13、core ABI 与 next178 基线不变；
popstate、逐项滚动/表单恢复、非当前 URL、POST state 和跨 document 页面缓存仍未实现。
C89 回归及 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next179` 已隔离 staging，
七个 ARMV4I 二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志包含 TEST147 OK、
95 条 OK、零 ERROR、零 FAIL 与最终 PASS。

next180 已通过设备自动门：next179 的同 document traversal 在切换 index/state 后派发最小
popstate。支持 window.onpopstate 以及仅面向 popstate 的 window add/removeEventListener；
事件 state 是独立 JSON clone，target/currentTarget 为 window，bubbles/cancelable 为 false，
handler 异常不会撤销遍历；pushState/replaceState 本身不派发。TEST148 离线固定异步 back
边界、state-before-event、属性/listener 回调、重复去重、remove、clone/异常隔离、元数据和
push/replace 静默。默认 javascript=0、TEST13、core ABI 与 next179 基线不变；这不是完整
Window EventTarget 或 PopStateEvent 构造器，跨 document traversal 仍不派发，逐项滚动/
表单恢复、非当前 URL、POST state 和页面缓存仍未实现。C89 回归及 ARMV4I Debug 增量构建
已通过；`C:\WMShare\Positron-next180` 已隔离 staging，七个 ARMV4I 二进制与构建产物
SHA-256 一致。320x320/128 DPI 日志包含 TEST148 OK、96 条 OK、零 ERROR、零 FAIL 与最终
PASS，next180 已成为自动设备基线。

next181 已通过设备自动门：history.replaceState/pushState 接受当前 document 基础 URL 上的
片段 URL（#fragment、无片段基础 URL 或同基础 URL 的绝对形式），同步更新 location.href、
document.URL/documentURI 与逐项 history URL；同 document traversal 在 popstate 前恢复目标
URL，不发起 GET。TEST149 离线固定初始/运行期 replace/push、前向分支截断、片段清除/恢复、
URL-before-popstate 及路径/查询/跨源拒绝。默认 javascript=0、TEST13、core ABI、14/16
callback 槽位与 next180 基线不变；普通相对 URL、路径/查询变化、hashchange、逐项滚动/表单
恢复及跨 document 页面缓存仍未实现。C89 回归及 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next181` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志包含 TEST149 OK、97 条 OK、零 ERROR、零 FAIL 与最终 PASS，
next181 已成为自动设备基线。

next182 已通过设备自动门：片段变化的同 document history traversal 在 popstate 之后派发
最小 hashchange。支持 window.onhashchange 与 window add/removeEventListener('hashchange')；
事件含 oldURL/newURL、window target/currentTarget、不可取消元数据和异常隔离，重复 listener
去重且 remove 生效。pushState/replaceState 自身及相同片段 traversal 静默。TEST150 离线
固定顺序、URL 元数据、属性/listener、异常/取消隔离、重复去重、remove 与静默边界。默认
javascript=0、TEST13、core ABI、14/16 callback 槽位与 next181 基线不变；location.hash/
片段赋值、跨 document hashchange、完整 Window EventTarget/HashChangeEvent、滚动/表单恢复
及页面缓存仍未实现。C89 回归及 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next182` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。320x320/128 DPI 日志包含 TEST150 OK、98 条 OK、零 ERROR、零 FAIL 与最终 PASS，
next182 已成为自动设备基线。

next183 已通过设备自动门：location 新增动态只读 protocol/host/hostname/port/pathname/search/
hash/origin；getter 每次解析当前绝对 HTTP(S) href，因此 replaceState/pushState 与同 document
traversal 后和 document.URL 同步。TEST151 离线固定显式端口、路径/查询/片段/origin、只读
descriptor、replace/push/back/forward 和 14/16 callback 槽位。默认 javascript=0、TEST13、
core ABI 与 next182 基线不变；组件 setter、username/password、完整 URL 标准化、通用相对
导航及 location 片段导航仍未实现。C89 回归及 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next183` 已隔离 staging，七个 ARMV4I 二进制与构建产物 SHA-256
一致。首次设备启动只显示 `test_host.ini ignored`：当前 ini 为 2079 字节，超过旧 2048 字节
读取上限，TEST151 未运行；候选现已把有界上限提升到 4096 字节并重新构建/staging，七个
二进制哈希一致。修复后的 320x320/128 DPI 日志正确选择配置并包含 TEST151 OK、99 条 OK、
零 ERROR、零 FAIL 与最终 PASS，next183 已成为自动设备基线。

next184 已成为自动设备基线：location.hash setter 通过既有 WM 消息队列延迟执行同 document
片段导航；成功后新增 null-state history entry、同步 href/components 与 history.length，仅
派发 hashchange，不发起 GET/popstate。相同值静默，空字符串清除片段，后续 back 仍按
popstate→hashchange 遍历。TEST152 离线固定异步边界、same-value、清除、state/length、事件
顺序、无网络和 14/16 callback 槽位。默认 javascript=0、TEST13、core ABI 与 next183 基线
不变；location.href/assign/replace 相对片段、百分号标准化、锚点滚动及其他组件 setter 未实现。
C89 回归及 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next184` 的七个 ARMV4I
二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志得到 TEST152 OK、配置所选 99 项
全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 98 项使用
标准数字 OK 行。

next186 已成为自动设备基线：与当前绝对基址相同、仅改变 fragment 的 href/assign/replace URL
复用同 document 队列；当前确有 fragment 时，绝对基址也可清除它。href/assign 新增 null-state
entry，replace 替换当前 entry；三者延迟提交、无 GET/popstate、只派发 hashchange。当前无
fragment 时的同 URL 导航及 query/path/origin 不同目标仍走普通导航。TEST154 固定三入口、
清除、state/length、事件、无网络、普通导航边界和 14/16 callback 槽位。相对 path+fragment、
百分号编码/标准化、锚点滚动及其他组件 setter 未实现；默认 javascript=0、TEST13 与 core ABI
不变。C89 回归、仓库审计和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next186`
的七个 ARMV4I 二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志得到 TEST154 OK、
配置所选 101 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，
其余 100 项使用标准数字 OK 行。

next187 已成为自动设备基线：根相对 href/assign/replace URL 解析后与当前 path/query 完全相同且
仅改变 fragment 时复用同 document 队列；当前确有 fragment 时也可用匹配根相对基址清除。
href/assign push null-state entry，replace 替换当前 entry；三者延迟提交、无 GET/popstate、
只派发 hashchange。当前无 fragment 的同 URL 根相对导航及 query/path 不同目标保持普通导航。
TEST155 固定三入口、清除、same-value、state/length、事件、无网络、分类边界和 14/16 callback
槽位。query-only、普通 path-relative、dot-segment、百分号标准化、锚点滚动和其他组件 setter
未实现；默认 javascript=0、TEST13 与 core ABI 不变。C89 回归、仓库审计和 ARMV4I Debug
构建已通过；`C:\WMShare\Positron-next187` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。
320x320/128 DPI 日志得到 TEST155 OK、配置所选 102 项全部 OK、零 ERROR、零 FAIL 与最终
PASS；TEST13 使用 `OK (overview)`，其余 101 项使用标准数字 OK 行。

next188 已成为自动设备基线：query-relative href/assign/replace URL 解析后与当前 pathname/query 完全
相同且仅改变 fragment 时复用同 document 队列；当前确有 fragment 时也可用匹配 query 清除。
href/assign push null-state entry，replace 替换当前 entry；三者延迟提交、无 GET/popstate、只
派发 hashchange。当前无 fragment 的同 query 导航、不同 query 和普通 path-relative 目标保持
普通导航。TEST156 固定三入口、清除、same-value、state/length、事件、无网络、分类边界和
14/16 callback 槽位。普通 path-relative、dot-segment、百分号标准化、锚点滚动和其他组件
setter 未实现；默认 javascript=0、TEST13 与 core ABI 不变。C89 回归和 ARMV4I Debug 增量
构建已通过；`C:\WMShare\Positron-next188` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。
240x240/96 DPI 日志得到 TEST156 OK、配置所选 103 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；
TEST13 使用 `OK (overview)`，其余 102 项使用标准数字 OK 行。

next189 已成为自动设备基线：不带 `./` 或 `../` 前缀的同目录 path-relative href/assign/replace URL，
解析后与当前 path/query 完全相同且仅改变 fragment 时复用同 document 队列；当前确有 fragment
时也可用匹配的相对文件名清除。href/assign push null-state entry，replace 替换当前 entry；三者
延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query 和
带点段前缀的目标保持普通导航。TEST157 固定三入口、清除、same-value、state/length、事件、
无网络、分类边界和 14/16 callback 槽位。点段归一化、百分号标准化、锚点滚动和其他组件 setter
未实现；默认 javascript=0、TEST13 与 core ABI 不变。C89 回归和 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next189` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。修复后
240x240/96 DPI 日志得到 TEST156/157 OK、配置所选 104 项全部 OK、零 ERROR、零 FAIL 与最终
PASS；TEST13 使用 `OK (overview)`，其余 103 项使用标准数字 OK 行。
首次设备运行在 TEST156 停止：该旧测试仍把现已进入 next189 能力范围的
`page?x=1#network` 当作普通导航。候选没有放宽片段行为断言，而是把 TEST156 的负向边界改为
真正不同 path 的 `other?x=1#network`；修复后的 EXE 已重新构建并覆盖原 next189 目录。复跑
日志包含修复版独有的 TEST156 文案、TEST157 OK 和最终 PASS，已关闭该门。

next190 已成为自动设备基线：带单个 `./` 前缀的同目录 href/assign/replace URL，在移除该前缀并解析后
与当前 path/query 完全相同且仅改变 fragment 时复用同 document 队列；当前确有 fragment 时也
可用匹配目标清除。href/assign push null-state entry，replace 替换当前 entry；三者延迟提交、
无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query 和 `../` 父目录
目标保持普通导航。TEST158 固定三入口、清除、same-value、state/length、事件、无网络、分类
边界和 14/16 callback 槽位。`../`、重复/混合点段归一化、百分号标准化、锚点滚动和其他组件
setter 未实现；默认 javascript=0、TEST13 与 core ABI 不变。C89 回归和 ARMV4I Debug 增量构建
已通过；`C:\WMShare\Positron-next190` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。
240x240/96 DPI 日志得到 TEST158 OK、配置所选 105 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；
TEST13 使用 `OK (overview)`，其余 104 项使用标准数字 OK 行。

next191 已成为自动设备基线：带单个 `../` 前缀的 href/assign/replace URL，在上移一个目录并解析后与
当前 path/query 完全相同且仅改变 fragment 时复用同 document 队列；当前确有 fragment 时也可
用匹配目标清除。href/assign push null-state entry，replace 替换当前 entry；三者延迟提交、无
GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query 和重复 `../../`
目标保持普通导航。TEST159 固定三入口、清除、same-value、state/length、事件、无网络、分类
边界和 14/16 callback 槽位。重复/混合点段归一化、百分号标准化、锚点滚动和其他组件 setter
未实现；默认 javascript=0、TEST13 与 core ABI 不变。C89 回归和 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next191` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。240x240/96 DPI
日志得到 TEST159 OK、配置所选 106 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用
`OK (overview)`，其余 105 项使用标准数字 OK 行。

next192 已成为自动设备基线：连续多个前导 `../` 的 href/assign/replace URL，逐级上移目录并解析后与
当前 path/query 完全相同且仅改变 fragment 时复用同 document 队列；越过 origin 根的额外父
目录段钳制在根。当前确有 fragment 时也可用匹配目标清除。href/assign push null-state entry，
replace 替换当前 entry；三者延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment
的同 URL、不同 path/query 和混合 `.././` 目标保持普通导航。TEST160 固定三入口、清除、
same-value、state/length、事件、无网络、分类边界和 14/16 callback 槽位。混合/内嵌点段
归一化、百分号标准化、锚点滚动和其他组件 setter 未实现；默认 javascript=0、TEST13 与 core
ABI 不变。C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next192` 的七个
ARMV4I 二进制与构建产物 SHA-256 一致。240x240/96 DPI 日志得到 TEST160 OK、配置所选 107 项
全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 106 项使用标准
数字 OK 行。

next193 已成为自动设备基线：连续前导 `../` 之后允许一个 `./` 的 href/assign/replace URL，先逐级上移
目录、再移除该单点段，解析后与当前 path/query 完全相同且仅改变 fragment 时复用同 document
队列；当前确有 fragment 时也可用匹配目标清除。href/assign push null-state entry，replace
替换当前 entry；三者延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、
不同 path/query 和重复 `../././` 目标保持普通导航。TEST161 固定三入口、清除、same-value、
state/length、事件、无网络、分类边界和 14/16 callback 槽位。重复/任意内嵌点段归一化、百分号
标准化、锚点滚动和其他组件 setter 未实现；默认 javascript=0、TEST13 与 core ABI 不变。C89
回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next193` 的七个 ARMV4I 二进制与
构建产物 SHA-256 一致。240x240/96 DPI 日志得到 TEST161 OK、配置所选 108 项全部 OK、零
ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 107 项使用标准数字 OK 行。

next194 已成为自动设备基线：连续前导 `../` 之后允许连续多个 `./` 的 href/assign/replace URL，先逐级
上移目录、再移除这些单点段，解析后与当前 path/query 完全相同且仅改变 fragment 时复用同
document 队列；当前确有 fragment 时也可用匹配目标清除。href/assign push null-state entry，
replace 替换当前 entry；三者延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment
的同 URL、不同 path/query 和路径中部 `segment/../` 目标保持普通导航。TEST162 固定三入口、
清除、same-value、state/length、事件、无网络、分类边界和 14/16 callback 槽位。任意内嵌
点段归一化、百分号标准化、锚点滚动和其他组件 setter 未实现；默认 javascript=0、TEST13 与
core ABI 不变。C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next194` 的
七个 ARMV4I 二进制与构建产物 SHA-256 一致。240x240/96 DPI 日志得到 TEST162 OK、配置所选
109 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 108 项使用
标准数字 OK 行。

next195 已成为自动设备基线：无父目录前缀时允许连续多个前导 `./` 的 href/assign/replace URL，移除这些
单点段后与当前 path/query 完全相同且仅改变 fragment 时复用同 document 队列；当前确有 fragment
时也可用匹配目标清除。href/assign push null-state entry，replace 替换当前 entry；三者延迟提交、
无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query 和路径中部
`segment/../` 目标保持普通导航。TEST163 固定三入口、清除、same-value、state/length、事件、
无网络、分类边界和 14/16 callback 槽位。任意内嵌点段归一化、百分号标准化、锚点滚动和其他
组件 setter 未实现；默认 javascript=0、TEST13 与 core ABI 不变。C89 回归和 ARMV4I Debug
增量构建已通过；`C:\WMShare\Positron-next195` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。
240x240/96 DPI 日志得到 TEST163 OK、配置所选 110 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；
TEST13 使用 `OK (overview)`，其余 109 项使用标准数字 OK 行。

next196 已成为自动设备基线：连续前导 `../` 逐级上移后，允许余下路径中出现一个内嵌 `./` 的
href/assign/replace URL；移除该单点段后与当前 path/query 完全相同且仅改变 fragment 时复用同
document 队列，当前确有 fragment 时也可用匹配目标清除。href/assign push null-state entry，
replace 替换当前 entry；三者延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的
同 URL、不同 path/query 和连续内嵌 `././` 目标保持普通导航。TEST164 固定三入口、清除、
same-value、state/length、事件、无网络、分类边界和 14/16 callback 槽位。多个内嵌点段、内嵌
父目录、百分号标准化、锚点滚动和其他组件 setter 未实现；默认 javascript=0、TEST13 与 core ABI
不变。C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next196` 的七个 ARMV4I
二进制与构建产物 SHA-256 一致。240x240/96 DPI 日志得到 TEST164 OK、配置所选 111 项全部 OK、
零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 110 项使用标准数字 OK 行。

next197 已成为自动设备基线：连续前导 `../` 逐级上移后，允许余下路径同一位置出现连续多个内嵌 `./`
的 href/assign/replace URL；移除该连续单点段后与当前 path/query 完全相同且仅改变 fragment 时
复用同 document 队列，当前确有 fragment 时也可用匹配目标清除。href/assign push null-state
entry，replace 替换当前 entry；三者延迟提交、无 GET/popstate、只派发 hashchange。当前无
fragment 的同 URL、不同 path/query、分离位置的多个 `./` 和内嵌 `../` 目标保持普通导航。
TEST165 固定三入口、清除、same-value、state/length、事件、无网络、分类边界和 14/16 callback
槽位。任意多位置点段归一化、内嵌父目录、百分号标准化、锚点滚动和其他组件 setter 未实现；
默认 javascript=0、TEST13 与 core ABI 不变。C89 回归和 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next197` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。240x240/96 DPI
日志得到 TEST165 OK、配置所选 112 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用
`OK (overview)`，其余 111 项使用标准数字 OK 行。

next198 已成为自动设备基线：连续前导 `../` 逐级上移后，允许余下路径多个位置出现内嵌 `./` 的
href/assign/replace URL；移除这些单点段后与当前 path/query 完全相同且仅改变 fragment 时复用
同 document 队列，当前确有 fragment 时也可用匹配目标清除。href/assign push null-state entry，
replace 替换当前 entry；三者延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的
同 URL、不同 path/query 和内嵌 `../` 目标保持普通导航。TEST166 固定三入口、清除、same-value、
state/length、事件、无网络、分类边界和 14/16 callback 槽位。无父目录前缀的内嵌点段、内嵌
父目录、百分号标准化、锚点滚动和其他组件 setter 未实现；默认 javascript=0、TEST13 与 core ABI
不变。C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next198` 的七个 ARMV4I
二进制与构建产物 SHA-256 一致。240x240/96 DPI 日志得到 TEST166 OK、配置所选 113 项全部 OK、
零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 112 项使用标准数字 OK 行。

next199 已成为自动设备基线：根相对 href/assign/replace URL 允许路径中出现一个内嵌 `./`；移除该
单点段后与当前 path/query 完全相同且仅改变 fragment 时复用同 document 队列，当前确有 fragment
时也可用匹配目标清除。href/assign push null-state entry，replace 替换当前 entry；三者延迟提交、
无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query、连续内嵌
`././` 和内嵌 `../` 目标保持普通导航。TEST167 固定三入口、清除、same-value、state/length、
事件、无网络、分类边界和 14/16 callback 槽位。根相对多个点段、绝对 URL 点段、内嵌父目录、
百分号标准化、锚点滚动和其他组件 setter 未实现；默认 javascript=0、TEST13 与 core ABI 不变。
C89 回归和 ARMV4I Debug 增量构建已通过；原共享目录 fixture 连续两次在 TEST70 `WriteFile`
返回 0/0 字节且错误码为 0，现改用设备本地 `\Temp`；`C:\WMShare\Positron-next199-fix` 的七个
ARMV4I 二进制与构建产物 SHA-256 一致。240x240/96 DPI 日志得到 TEST70 与 TEST167 OK、配置所选
114 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 113 项使用
标准数字 OK 行。

next200 已成为自动设备基线：根相对 href/assign/replace URL 允许同一路径位置连续出现 `././`；移除连续
点段后与当前 path/query 完全相同且仅改变 fragment 时复用同 document 队列，当前确有 fragment
时也可用匹配目标清除。href/assign push null-state entry，replace 替换当前 entry；三者延迟提交、
无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query、多个分离内嵌
`./` 位置和内嵌 `../` 目标保持普通导航。TEST168 固定三入口、清除、same-value、state/length、
事件、无网络、分类边界和 14/16 callback 槽位。根相对多个分离点段、绝对 URL 点段、内嵌父目录、
百分号标准化、锚点滚动和其他组件 setter 未实现；默认 javascript=0、TEST13 与 core ABI 不变。
C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next200` 的七个 ARMV4I 二进制与
构建产物 SHA-256 一致。640x480/192 DPI 日志得到 TEST70、TEST167 与 TEST168 OK、配置所选
115 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 114 项使用
标准数字 OK 行。

next201 已成为自动设备基线：根相对 href/assign/replace URL 允许多个分离路径位置出现内嵌 `./`；移除
所有这些点段后与当前 path/query 完全相同且仅改变 fragment 时复用同 document 队列，当前确有
fragment 时也可用匹配目标清除。href/assign push null-state entry，replace 替换当前 entry；三者
延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query、绝对
URL 点段和内嵌 `../` 目标保持普通导航。TEST169 固定三入口、清除、same-value、state/length、
事件、无网络、分类边界和 14/16 callback 槽位。绝对 URL 点段、内嵌父目录、百分号标准化、锚点
滚动和其他组件 setter 未实现；默认 javascript=0、TEST13 与 core ABI 不变。C89 回归和 ARMV4I
首包在 TEST167 停止，因为其旧负边界正是本批新增能力；修正版把该负边界换成仍未支持的绝对 URL
点段，TEST169 正向断言不变。C89 回归和 ARMV4I Debug 增量构建已通过；
`C:\WMShare\Positron-next201-fix` 的七个 ARMV4I 二进制与构建产物 SHA-256 一致。640x480/192
DPI 日志得到 TEST70、TEST167、TEST168 与 TEST169 OK、配置所选 116 项全部 OK、零 ERROR、
零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 115 项使用标准数字 OK 行。

next202 已成为自动设备基线：绝对 href/assign/replace URL 允许路径中出现一个内嵌 `./`；移除该点段后
与当前 origin/path/query 完全相同且仅改变 fragment 时复用同 document 队列，当前确有 fragment
时也可用匹配目标清除。href/assign push null-state entry，replace 替换当前 entry；三者延迟提交、
无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query、连续内嵌
`././` 和内嵌 `../` 目标保持普通导航。TEST170 固定三入口、清除、same-value、state/length、
事件、无网络、分类边界和 14/16 callback 槽位。绝对 URL 连续/多位置点段、内嵌父目录、百分号
标准化、锚点滚动和其他组件 setter 未实现；默认 javascript=0、TEST13 与 core ABI 不变。C89
回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next202` 的七个 ARMV4I 二进制与
构建产物 SHA-256 一致。640x480/192 DPI 日志得到 TEST70、TEST169 与 TEST170 OK、配置所选
117 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 116 项使用
标准数字 OK 行。

next203 已成为自动设备基线：绝对 href/assign/replace URL 允许同一路径位置连续出现 `././`；移除连续
点段后与当前 origin/path/query 完全相同且仅改变 fragment 时复用同 document 队列，当前确有
fragment 时也可用匹配目标清除。href/assign push null-state entry，replace 替换当前 entry；三者
延迟提交、无 GET/popstate、只派发 hashchange。当前无 fragment 的同 URL、不同 path/query、多个
分离内嵌 `./` 位置和内嵌 `../` 目标保持普通导航。TEST171 固定三入口、清除、same-value、
state/length、事件、无网络、分类边界和 14/16 callback 槽位。绝对 URL 多位置点段、内嵌父目录、
百分号标准化、锚点滚动和其他组件 setter 未实现；默认 javascript=0、TEST13 与 core ABI 不变。
C89 回归和 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next203` 的七个 ARMV4I 二进制与
构建产物 SHA-256 一致。640x480/192 DPI 日志得到 TEST70、TEST170 与 TEST171 OK、配置所选
118 项全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 117 项使用
标准数字 OK 行。

next204 已成为自动设备基线：`test_host.ini` 解析器额外接受精确的特殊编号 999；该批尚未
开放 172-998，next205/206 后续将 172/173 变为正式编号，当前 174-998 仍无效。TEST999 在普通所选测试与
TEST7b 之后运行，只调用一次 `MessageBeep(MB_OK)`，不经
`show_info` 或 MessageBox，并记录标准 `TEST 999 OK`。这刻意不是全局退出钩子：fail-fast
若发生在它之前就不会响。候选默认在 next203 的 118 项后追加 999；C89、ARMV4I Debug 构建
和 `C:\WMShare\Positron-next204` 七个二进制哈希核对已通过；设备日志得到 118 条标准数字
OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS，用户确认序列末尾实际响了一次。

next205 已成为自动设备基线：绝对 URL 分类器现在遍历查询/fragment 前所有字面 `/./`，让
多个分离内嵌位置规范化后进入既有同文档片段队列。TEST172 覆盖 href/assign/replace、清除、
same-value、state/length、hashchange、无 GET、不同 query/path、`%2E` 与 `..` 排除边界和
14/16 callback 槽位。默认 javascript=0、TEST13 与 core ABI 不变；百分号点段标准化、父目录
折叠、锚点滚动和其他 location setter 不在本批。C89、ARMV4I Debug 构建和
`C:\WMShare\Positron-next205` 七个二进制哈希核对已通过，120 项设备日志门也已完成。
首轮日志在 TEST167 停止，确认是 TEST167-169 的旧绝对多位置排除断言与新增能力冲突；修正
候选只撤掉这三条已失效断言，其他失败边界不变；重建后完整日志通过。

next206 已成为自动设备基线：绝对 URL 分类器新增单个、内嵌完整 segment 的 ASCII 大小写不敏感
`%2e` 识别，对齐 URL Standard 的 single-dot 定义。TEST173 覆盖 href/assign/replace、清除、
same-value、state/length、hashchange、无 GET；next206 日志当时还覆盖多个编码点段、
`%2E%2E`、不同 query/path、`..` 排除边界和 14/16 callback 槽位。next210 基线只撤掉多个
single-dot 的旧反向断言。double-dot 的其他编码形式、混合字面/编码点段、锚点
滚动和其他 location setter 不在本批。C89、ARMV4I Debug 构建和
`C:\WMShare\Positron-next206` 七个二进制哈希核对和 121 项设备日志门均已通过。

next207 已成为定向设备基线：绝对 URL 分类器在 next206 的内嵌编码单点段之外，继续识别 path
末尾、位于 query/fragment 或 URL 结尾前的单个 `%2e` segment。TEST174 覆盖三入口、清除、
same-value、state/length、hashchange、无 GET、混合编码点段、`%2E%2E`、不同 query/path、
`..` 排除边界和 14/16 callback 槽位。C89、ARMV4I Debug 构建与七个 staging 哈希已通过；
默认选择 TEST13/151-174/999（26 项），next206 保持最近全量检查点。
首轮 TEST174 失败已定位为末尾 token 长度 off-by-one；修正版不放宽任何断言，26 项日志已
完整通过。

next208 已成为定向设备基线：根相对 URL 分类器新增单个内嵌 `%2e` segment；TEST175 覆盖三入口、
清除、same-value、state/length、hashchange、无 GET、重复/双编码点段、不同 query/path、
`..` 排除边界和 14/16 callback 槽位。C89、ARMV4I Debug 构建与七个 staging 哈希已通过；
默认选择 TEST13/151-175/999（27 项），设备日志全部通过；next206 保持最近全量检查点。

next209 已成为定向设备基线：根相对 URL 分类器在 next208 的内嵌编码单点段之外，继续识别 path
末尾的单个 `%2e` segment。TEST176 覆盖三入口、清除、same-value、state/length、hashchange、
无 GET、混合编码点段、`%2E%2E`、不同 query/path、`..` 排除边界和 14/16 callback 槽位。
C89、ARMV4I Debug 构建与七个 staging 哈希已通过；默认选择 TEST13/151-176/999（28 项），
28 项设备日志全部通过；next206 保持最近全量检查点。

next210 已成为定向设备基线：绝对 URL 分类器遍历多个内嵌 `%2e` single-dot segment；TEST177
覆盖三入口、清除、same-value、state/length、hashchange、无 GET、不同 query/path、父目录
排除边界和 14/16 callback 槽位。TEST173 的重复编码旧断言已移除；`%2E%2E`、混合内嵌/末尾
编码与根相对重复编码仍不规范化。C89、ARMV4I Debug 构建和七个 staging 哈希已通过；默认
选择 TEST13/151-177/999（29 项），日志全部通过；next206 保持最近全量检查点。

next211 已成为全量设备基线：根相对 URL 分类器遍历多个内嵌 `%2e` single-dot segment；TEST178
覆盖三入口、清除、same-value、state/length、hashchange、无 GET、不同 query/path、父目录
排除边界和 14/16 callback 槽位。TEST175 的重复编码旧断言已移除；`%2E%2E` 与混合内嵌/末尾
编码仍不规范化。C89、ARMV4I Debug 构建和七个 staging 哈希已通过；默认选择
TEST13/20/27/43/44/56/58-77/80-178/999（126 项），日志全部通过；next211 已替代 next206
成为最近全量检查点。

next212 已成为定向设备基线：绝对 URL 分类器对单个内嵌完整 `%2e%2e` segment 折叠前一个非空
目录；TEST179 覆盖三入口、清除、same-value、state/length、hashchange、无 GET、不同
query/path、额外父目录边界和 14/16 callback 槽位。根相对、末尾、重复、混合点段及
`.%2e`/`%2e.` 拼写仍不规范化。C89、ARMV4I Debug 构建和七个 staging 哈希已通过；默认
选择 TEST13/151-179/999（31 项），设备日志全部通过；next211 保持最近全量检查点。

next213 已成为定向设备基线：根相对 URL 分类器对单个内嵌完整 `%2e%2e` segment 折叠前一个
非空目录；TEST180 覆盖三入口、清除、same-value、state/length、hashchange、无 GET、不同
query/path、额外父目录边界和 14/16 callback 槽位。末尾、重复、混合点段及 `.%2e`/`%2e.`
拼写仍不规范化。C89、ARMV4I Debug 构建和七个 staging 哈希已通过；默认选择
TEST13/151-180/999（32 项），修正版设备日志全部通过；next211 保持最近全量检查点。首个 next213
目录的 4120 字节 ini 被 4096 字节上限拒绝且没有执行测试；修正版仅精简注释至 1357 字节，
已正式暂存到 `C:\WMShare\Positron-next213-fix` 并重新核对全部哈希。

next214 已成为定向设备基线：绝对 URL 分类器对 path 末尾完整 `%2e%2e` segment 折叠前一个非空
目录并保留 `/`；TEST181 覆盖三入口、清除、same-value、state/length、hashchange、无 GET、
不同 query/path、混合 single/double、重复 double-dot、字面父目录和 14/16 callback 槽位。
根相对末尾及 `.%2e`/`%2e.` 拼写仍不规范化。C89、ARMV4I Debug 构建和七个 staging 哈希已
通过；默认选择 TEST13/151-181/999（33 项），ini 为 1357 字节，设备日志全部通过；next211
保持最近全量检查点。

next215 已成为定向设备基线：根相对 URL 分类器对 path 末尾完整 `%2e%2e` segment 折叠前一个
非空目录并保留 `/`；TEST182 覆盖三入口、清除、same-value、state/length、hashchange、无
GET、不同 query/path、混合 single/double、重复 double-dot、字面父目录和 14/16 callback
槽位。`.%2e`/`%2e.` 拼写仍不规范化。C89、ARMV4I Debug 构建和七个 staging 哈希已通过；
默认选择 TEST13/151-182/999（34 项），ini 为 1357 字节；设备日志全部通过，next211 保持最近
全量检查点。本批是该检查点后的第 4 个低风险定向批次。

next216 已成为全量设备基线：绝对 URL 分类器对单个内嵌完整 `.%2e` 或 `%2e.` segment 折叠
前一个非空目录；TEST183 覆盖两种拼写、三入口、清除、same-value、state/length、hashchange、
无 GET、不同 query/path、混合完整/半编码 double-dot、重复半编码 double-dot、字面父目录和
14/16 callback 槽位。根相对和末尾半编码 double-dot 仍不规范化。C89、ARMV4I Debug 构建和
七个 staging 哈希已通过；默认选择 TEST13/20/27/43/44/56/58-77/80-183/999（131 项），ini
为 1294 字节；`screen=240x320 dpi=96` 设备日志全部通过，next216 成为新的最近全量检查点。
本批是 next211 后第 5 个低风险批次，因此恢复全量门。

next217 已成为定向设备基线：根相对 URL 分类器对单个内嵌完整 `.%2e` 或 `%2e.` segment 折叠
前一个非空目录；TEST184 覆盖两种拼写、三入口、清除、same-value、state/length、hashchange、
无 GET、不同 query/path、混合完整/半编码 double-dot、重复半编码 double-dot、字面父目录和
14/16 callback 槽位。末尾半编码 double-dot 仍不规范化。C89、ARMV4I Debug 构建和七个
staging 哈希已通过；默认选择 TEST13/151-184/999（36 项），ini 为 1278 字节；设备日志全部
通过，next216 保持最近全量检查点。本批是该检查点后的第 1 个低风险定向批次。

next218 已成为定向设备基线：绝对 URL 分类器对 path 末尾完整 `.%2e` 或 `%2e.` segment 折叠
前一个非空目录并保留 `/`；TEST185 覆盖两种拼写、三入口、清除、same-value、state/length、
hashchange、无 GET、不同 query/path、混合完整/半编码 double-dot、重复半编码 double-dot、
字面父目录和 14/16 callback 槽位。根相对末尾半编码 double-dot 仍不规范化。C89、实际
JavaScript 探针、ARMV4I Debug 构建和七个 staging 哈希已通过；默认选择
TEST13/151-185/999（37 项），ini 为 1278 字节；设备日志全部通过，next216 保持最近全量检查点。
本批是该检查点后的第 2 个低风险定向批次。

next219 已成为定向设备基线：根相对 URL 分类器对 path 末尾完整 `.%2e` 或 `%2e.` segment 折叠
前一个非空目录并保留 `/`；TEST186 覆盖两种拼写、三入口、清除、same-value、state/length、
hashchange、无 GET、不同 query/path、混合完整/半编码 double-dot、重复半编码 double-dot、
字面父目录和 14/16 callback 槽位。首包在 TEST162 遇到 bootstrap 1000ms 超时；修正版合并
重复分类逻辑，把 bootstrap 从 20,991 降至 19,735 字符，不改变预算。C89、14 组实际
JavaScript 探针、ARMV4I Debug 构建和 `C:\WMShare\Positron-next219-fix` 七个 staging 哈希
已通过；默认选择 TEST13/151-186/999（38 项），ini 为 1278 字节；修正版设备日志得到
37 条标准数字 OK、1 条 TEST13 overview、零 ERROR/FAIL 与最终 PASS，next216 保持最近全量
检查点。本批是该检查点后的第 3 个低风险定向批次。

next185 已成为自动设备基线：`location.href='#...'`、`location.assign('#...')` 与
`location.replace('#...')` 复用同 document 片段队列；href/assign 新增 null-state entry，
replace 替换当前 entry 且不增加 length。三者都延迟提交、无 GET/popstate、只派发 hashchange，
相同目标静默，后退仍为 popstate→hashchange。TEST153 固定异步边界、state/length、replace
文档身份、事件、无网络及 14/16 callback 槽位；其他相对/绝对 URL 保持既有跨 document 路径。
百分号编码/标准化、锚点滚动、跨 document 片段导航和其他组件 setter 未实现。C89 回归、
仓库审计与 ARMV4I Debug 增量构建已通过；`C:\WMShare\Positron-next185` 的七个 ARMV4I
二进制与构建产物 SHA-256 一致。320x320/128 DPI 日志得到 TEST153 OK、配置所选 100 项
全部 OK、零 ERROR、零 FAIL 与最终 PASS；TEST13 使用 `OK (overview)`，其余 99 项使用
标准数字 OK 行。

next161 已接入 WM6 EDIT 的原生 IME composition 消息，使用 SDK
`<imm.h>` 和设备 `coredll` 中的 `ImmGetContext/ImmGetCompositionStringW/ImmReleaseContext`，
不链接桌面 `imm32.lib`。显式脚本 context 新增 `compositionstart/update/end` 与不可取消的
`beforeinput(insertCompositionText)`；TEST123 验证 WM start/end 入口、共享 UTF-8 update
发射路径、事件顺序/数据/冒泡/取消属性。`C:\WMShare\Positron-next161\test_host.log` 中
example.com 与 IANA Example Domains 已通过，Reserved Domains 在 HTTP 状态前遇到
`ssl_handshake -0x7280`/peer EOF，TEST13 仅完成 `2/3` 并终止整个 testbench，因此
TEST123 没有执行；不能把这次运行写成 IME PASS 或 FAIL。

next162 保留上述 IME 纵切，并仅为主文档幂等 GET 增加一次受限重试：
响应必须同时满足 `status=0`、空 body、错误属于 TLS 握手 peer EOF，worker 才等待 250ms
重发一次。POST、DNS、HTTP 状态失败、子资源及其他错误不重试；TEST43 离线固定分类，
TEST13 日志/遥测增加 retry 计数。自动探针没有配置真实 SIP/IME context，设备包通过后仍须
人工验证一次 SIP 组合输入；不得宣称候选窗口、预编辑 UI 或完整 InputEvent/CompositionEvent
API 已完成。C89、仓库审计和 ARMV4I Debug 增量构建已通过，next160 仍是正式设备基线。
`C:\WMShare\Positron-next162` staging 已完成，七个 ARMV4I EXE/DLL 与构建产物哈希一致。
上一设备基线：next153 已在 `screen=640x480 dpi=192` 下完成默认 testbench，
TEST13/20/27/43/44/56/58-77/80-119 全部通过并记录 `TESTBENCH PASS`。该批在默认关闭的
browser JavaScript 门之上新增 TEST111；TEST13 三段导航保持正常。TEST108 首轮
设备运行定位到 vendored `tiny-regex-c` 对 `[A-Z0-9-]+` 末尾字面量连字符的错误处理；
本地补丁不再在较早的范围连字符处提前返回，并由三个独立 pattern fixture 复验。
next137 已在 `screen=320x320 dpi=128` 下通过 TEST13/20/27/43/44/56/58/59，
next138 隔离 TEST60 后通过，next139 隔离 TEST63 后通过；next141 在
`screen=320x320 dpi=128` 下通过 TEST13/20/27/43/44/56/58-74，next142 修正 TEST75
后在 `screen=240x320 dpi=96` 默认配置下完成 TEST13/20/27/43/44/56/58-77/80-104，
日志为 `TESTBENCH PASS`。next140 的固定 96-DPI 尝试仍标记为已替代。ARMV4I 构建、
staging 和该设备验收均已通过；下一批应轮换分辨率/DPI。当前设备基线：next134 已在 `screen=240x320 dpi=96` 设备日志中通过 TEST13/20/27/43/44/56/58-77/80-99；next135 新增 TEST100-104 的 `minlength`/`maxlength` 表单约束，next136 隔离 TEST59 的 CSS 参考上下文，next137 在 `screen=320x320 dpi=128` 下通过 TEST13/20/27/43/44/56/58/59，next138/139 又分别修正 TEST60/63 的离线 CSS 上下文，next140 的固定 DPI 方案已替代，next141 让 TEST62 动态等比并通过至 TEST74，next142 让 TEST75 动态等比并通过默认设备批次，next143 新增 TEST105-109 的受限 ASCII `pattern` validity。next123/124/125/126/127/134/135 的 `positron_script.dll` 与表单扩展仍不接入冻结的浏览器 JS 路径。next123 以来 Browse 宿主使用物理像素/CSS 视口分离，并按设备报告的 DPI 换算；96 DPI 只是 CSS 规范基准，不是产品固定值。next114 建立外部 `<script src>` 的 transport-agnostic 发现/抓取/document 缓存 ABI，但没有执行 JavaScript。next115 与 next116 的 float 候选均已因 TEST79 失败和 TEST13 视觉回归否决，代码与默认配置已恢复 next114；Float 方向暂挂。完整 JavaScript RegExp、类型/范围 validity、`:visited/:target/:indeterminate`、专用事件数据、完整 HTML activation 和浏览器 JS binding 仍未实现；真实触屏与视觉仍待累计人工检查。**next78 仍是已撤回的失败实验，不得使用**。

失败/暂挂总索引见 [`FAILED_EXPERIMENTS.md`](./FAILED_EXPERIMENTS.md)。接手时先查该索引，再查本文件的当前基线；不要只依据某个旧包里的自动 `OK`。

当前交接基线：next154 已完成统一 script sequence ABI、external resource worker round、
按 DOM 顺序执行 external/inline classic scripts、页面级 context、click/表单/EDIT 键盘
事件桥、focusin/focusout 桥、beforeinput 桥、SELECT 键盘桥、WM_CHAR keypress 桥、
WM_SYSKEY/WM_SYSCHAR 桥和 TEST111-120；ARMV4I 增量构建、C89、仓库审计与
`screen=640x480 dpi=192` 设备验收均已通过。默认 `javascript=0`，因此不改变 TEST13
的网络路径。next155 首次设备包在 TEST121 因 UTF-8 被旧 JSON 过滤器清空而失败，已由
next156 修复；日志位于 `C:\WMShare\Positron-next156\test_host.log`。

**next153 设备验收（2026-08-08）**：新增原生 EDIT/SELECT `WM_CHAR -> keypress` 桥，
TEST119 覆盖 synthetic SELECT、真实 EDIT/SELECT WM 消息、target/bubble 元数据和
取消 SELECT 默认动作。C89、仓库审计、ARMV4I 增量构建、staging 和
`screen=640x480 dpi=192` 设备验收均已通过。默认 `javascript=0`、TEST13 路径及
next152 已验收的 SELECT keydown/keyup 行为不变。

**next154 设备验收（2026-08-08）**：新增原生 EDIT/SELECT
`WM_SYSKEYDOWN/UP`、ASCII `WM_SYSCHAR` 的显式 system-key 事件入口，沿用
`PCoreKeyEventData` 并明确 `altKey=true`；TEST120 覆盖 target/bubble、取消 SELECT
默认动作和事件元数据。C89、仓库审计、ARMV4I 增量构建、staging 与
`screen=640x480 dpi=192` 设备日志均已通过；日志为 `TESTBENCH PASS`。不要把它表述为
IME/composition 或完整 Keyboard/Event API。

**next155 首次设备失败（2026-08-08，已替代）**：next155 的 TEST13 与 TEST120 之前
的回归通过，但 TEST121 失败。`pcore_browser_script_key_safe()` 错把合法 UTF-8 高位
字节清空，不能放宽测试断言或使用该包作为基线。

**next156 设备验收（2026-08-08）**：事件回调对 `inputType`、`data`、`key` 使用
真正的 JSON 字符串转义，保留合法 UTF-8 并转义 JSON 特殊字符；TEST121 断言保持不变。
C89、仓库审计、ARMV4I 增量构建、`C:\WMShare\Positron-next156` staging 和
`screen=640x480 dpi=192` 设备验收均已通过，日志记录 `TESTBENCH PASS`；代理对、
IME/composition 和完整 Unicode 输入不在范围内。

**next152 设备验收（2026-08-08）**：原生 `COMBOBOX/LISTBOX` 的 WM 键盘消息已
加入宿主侧子类桥，TEST118 同时覆盖公开 SELECT 键盘事件 ABI 与真实 `WM_KEYDOWN/UP`
入口。C89、仓库审计、VS2008 ARMV4I 增量构建、staging 和 `screen=480x640 dpi=192`
设备验收均已通过，日志记录 `TESTBENCH PASS`。默认 `javascript=0` 和 TEST13 网络
路径不变。

**next146 设备验收（2026-08-08）**：显式 `javascript=1` 的导航请求现在把初始 classic-script
runtime 与当前 document 绑定保留；成功导航换入新 context，失败导航、旧文档释放和窗体
关闭清理待提交/当前 context。TEST112 离线确认后续求值能复用 `window` 状态并更新 DOM
后重新布局，设备日志在 `screen=240x320 dpi=96` 下记录 TEST112 OK 和 `TESTBENCH PASS`。
候选已通过 C89、ARMV4I 增量构建和仓库审计；默认 `javascript=0` 与 TEST13 路径不变，
不能将它表述为完整浏览器 JS。

**next147 设备验收（2026-08-08）**：显式 `javascript=1` 页面新增最小
`element.addEventListener/removeEventListener` bridge；Core 的 `PCore_EventDispatchAt`
仍负责 WM 点击派发，handler 可更新 DOM 或调用 `preventDefault()`。TEST113 离线覆盖
可信 click、取消、重新布局和 listener 清理；设备日志在 `screen=480x640 dpi=192` 下记录
TEST113 OK 和 `TESTBENCH PASS`。C89 与 ARMV4I 构建已通过，默认 `javascript=0` 与
TEST13 路径不变，键盘/焦点/输入事件仍未实现。

**next148 设备验收（2026-08-08）**：在 next147 的页面级 context 上接入 WM 原生
EDIT/SELECT 的 `focus`、`blur`、`input`、`change` 事件；`input/change` 冒泡，焦点事件
不冒泡，四类事件不可取消。EDIT 值先同步 Core DOM，失焦时对已变化值补发 `change`；
SELECT 选择变化同时派发 `input/change`。TEST114 已通过离线断言、C89、ARMV4I
增量构建、staging 和 `screen=320x320 dpi=128` 设备验收；默认 `javascript=0` 与
TEST13 路径不变。

**next149 设备验收（2026-08-08）**：为公开 Core 事件 ABI 增加
`PCoreKeyEventData` 和按元素/命中点键盘派发；显式 `javascript=1` 时，原生 EDIT 的
`WM_KEYDOWN/WM_KEYUP` 会向页面 listener 提供 `key/keyCode/charCode/repeat` 与修饰键。
TEST115 已通过离线断言和 `screen=320x320 dpi=128` 设备日志，C89、仓库审计、VS2008
ARMV4I 增量构建、staging 与设备验收均已通过。WM SELECT、`keypress`、`beforeinput`、
`focusin/focusout` 和完整 Event API 仍未实现；默认 `javascript=0` 与 TEST13 路径不变。

**next150 设备验收（2026-08-08）**：在原生 EDIT/SELECT 的既有 focus/blur
生命周期点追加可冒泡的 `focusin/focusout`，保留旧焦点事件的非冒泡语义和顺序。TEST116
已通过离线断言；C89、仓库审计、VS2008 ARMV4I 增量构建、`C:\WMShare\Positron-next150`
staging 与 `screen=320x320 dpi=128` 设备验收均已通过。默认 `javascript=0`、TEST13
路径、next148 表单事件和 next149 键盘事件不变。

**next151 设备验收（2026-08-08）**：新增 `PCoreInputEventData` 与最小
`beforeinput` 数据桥；显式 `javascript=1` 时，原生 EDIT 的字符、换行、退格、删除、
粘贴、剪切和清除动作可在原生默认处理前被监听并取消，事件带 `inputType/data` 和冒泡
元数据。TEST117 已通过离线断言；C89、仓库审计和 VS2008 ARMV4I 增量构建已通过，
设备 staging 与 `screen=320x320 dpi=128` 真实设备验收均已通过，日志记录 `TESTBENCH PASS`。
WM SELECT 键盘、IME/composition、完整 Unicode/剪贴板数据、`keypress` 和完整
Input/Keyboard/Event API 仍未实现。

**next152 实现记录（2026-08-08，设备已通过）**：`pcore_native_select` 保存原始窗口
过程并对原生 `COMBOBOX/LISTBOX` 子类化，先派发 `keydown/keyup`，未取消时调用原生
默认过程。TEST118 会在自动化窗口创建阶段发送 ArrowDown 的 WM 消息，并在窗口关闭
后核对 target/bubble 事件记录；设备日志已确认通过。这只验证 SELECT 键盘纵切，不扩展到 IME、`keypress`
或完整 Keyboard/Event API。

**状态更正（next132，2026-08-07）**：next131 在 `screen=320x320 dpi=128` 下的 TEST13
三段导航均完成，但 TEST20 动态 DPI 断言实际得到 `48x48`，期望 `64x64` device px，
因此不能把 next131 记为该设备的全通过。next132 已将设备视口决定和单位上下文快照
提前到正式构盒之前，并在 TEST20/27 的样式完成后重新绑定同一设备视口；VS2008
`Debug|Windows Mobile 6 Professional SDK (ARMV4I)` 增量构建已通过，待同设备复测。

**状态更正（next134，2026-08-07）**：next132 在 `screen=480x640 dpi=192` 下的
TEST13/20/27/43/44/56 均通过，但 TEST58 的离线几何段继承了前一个渲染测试留下的
设备视口待布局状态，得到 `article=320 rows=40/80/40`，而不是旧的 96 DPI 期望。
next133 曾用固定 230x260/96 DPI 隔离该段；因不符合项目的设备自适应原则，next134
已改为让 TEST58 直接读取运行时屏幕宽高和设备 DPI，并按 CSS 96 DPI 规范基准换算
物理断言；最终可见布局同样使用真实设备视口。ARMV4I 增量构建和 staging 已通过，
随后在 `screen=240x320 dpi=96` 设备日志中确认通过。该修复不放宽断言，也不修改布局引擎。

**next135（2026-08-07）**：表单校验新增 `PCORE_VALIDITY_TOO_SHORT` 与
`PCORE_VALIDITY_TOO_LONG`。text/password/textarea 控件读取有效的 HTML 非负整数
`minlength`/`maxlength`，按 UTF-8 字符数生成约束结果；required、disabled、readonly、
提交阻断和首个无效控件几何保持既有语义，坏属性保守忽略。TEST100-104 覆盖静态
边界、动态 native EDIT 更新、textarea、豁免项和首个长度错误；ARMV4I 增量构建与
`C:\WMShare\Positron-next135` staging 已通过，设备 testbench 尚未验收。

**next136（2026-08-07）**：设备日志在 `screen=480x640 dpi=192` 下于 TEST59 停止：
`width=224 main=(50,50) 124x77; expect x=25 w=174`。该测试是显式 CSS 像素几何
夹具，却继承了前一个设备-backed render 的 192 DPI 单位上下文；next136 在每个
离线 pass 前安装 `PCore_SetViewport(width,240,96)`，结束后恢复运行时设备视口。
这不是 core flex 回归，也没有放宽断言；ARMV4I 增量构建与
`C:\WMShare\Positron-next136` staging 已通过，设备复测待进行。

**next137（2026-08-07）**：next136 包在 `screen=320x320 dpi=128` 下记录为
`TEST13 PASS -> TEST20 FAIL: first box=48x48; expect 64x64 device px`。这次不是
DLL 混装或 TEST20 断言隔离：`libcss/src/select/unit.c` 的通用
`css_unit_len2device_px` 先把每 CSS 单位的 `1.333` 比例截成 `1`，再乘长度，导致
所有非整数 DPI 比例的尺寸丢失。next137 保留分数比例，完成整段长度换算后才按最终
设备像素取整；没有改成 48、没有固定 DPI，也没有修改 TEST20 断言。ARMV4I 增量
构建成功（libcss 仅保留既有 3 条 fpmath 警告），包为
`C:\WMShare\Positron-next137`。设备复测确认 TEST13/20/27/43/44/56/58/59 通过，
随后 TEST60 暴露独立离线夹具继承运行时 DPI 的问题。

**next138（2026-08-07）**：TEST60 的断言检查显式 CSS 像素几何：首表头的
`18px/10px`、普通表头的 `14px/10px`、正文 cell 的 `14px/5px`。next137 日志中
出现 `x=24/19/19/24 y=13/13/7/7`，正好是 `128/96` 缩放后的值，而两列文本宽度
仍相等，说明选择器、重选和 DOM node data 生命周期没有回归。next138 让该离线探针
在每次 style/layout 前显式使用 `96 DPI` CSS 参考上下文，并在清理阶段恢复真实设备
视口；没有放宽 TEST60 断言，也没有改变产品 DPI。ARMV4I 增量构建和
`C:\WMShare\Positron-next138` staging 已通过，设备复测待进行。

**next139（2026-08-07）**：next138 在 `screen=320x320 dpi=128` 下通过 TEST60，
随后 TEST63 报告 `shared SVG did not survive first document release`。该测试同时要求
固定 `240x120` viewport 和 `120x60` image box，却继承了设备-backed 的 `128 DPI` 上下文；
失败消息因此不能区分布局尺寸与 SVG 生命周期。next139 在 TEST63 前安装 96 DPI CSS
参考上下文，清理时恢复真实设备视口，并记录 post-release layout/node/box 诊断；没有放宽
共享 SVG 的 create/reuse/fetch/free 断言。ARMV4I 增量构建和
`C:\WMShare\Positron-next139` staging 已通过，设备复测待进行。

**next140（2026-08-07，已替代）**：next139 在 `screen=480x640 dpi=192` 下通过
TEST13/20/27/43/44/56/58/59/60/61，随后 TEST62 报告 `cb=36x36`、`radio=36x36`，
正好是设备 DPI 对离线 probe 尺寸的 `2x` 换算。next140 曾让四个静态 toggle probe 和
hidden-input 检查使用固定 96 DPI；这违反动态 DPI 原则，不能作为修复，已由 next141
替代。

**next141（2026-08-07）**：保留 TEST62 探针的 `64x48 CSS px` 表面，传入实际设备
DPI，并把原本 96-DPI 的 `14..24px` 控件基准用 `MulDiv(..., dpi, 96)` 等比换算为
物理像素。控件状态、绘制路径、隐藏 input 断言均未放宽；可见 TEST62 页面仍恢复真实
设备视口。ARMV4I 增量构建与 `C:\WMShare\Positron-next141` staging 已通过，设备日志
确认 TEST62 及 TEST63-74 通过，TEST75 停止。

**next142（2026-08-07）**：TEST75 的 `180/120/20/30/25/15/100/50/20/12px` 定位
夹具保留 CSS 尺寸，断言改为用实际设备 DPI 的 `MulDiv(css_px, dpi, 96)` 换算宽高、
偏移和相对定位结果；定位构盒与可见渲染路径未改变。ARMV4I 构建、
`C:\WMShare\Positron-next142` staging 和 `screen=240x320 dpi=96` 设备验收均已通过，
TESTBENCH 通过。

**next143（2026-08-08）**：`PCoreFormValidationInfo` 新增受限 ASCII
`patternMismatch`，TEST105-109 覆盖静态/动态 mismatch、豁免与坏属性、digit escape、
literal/range class、escaped punctuation 以及长度 flags 组合。首轮 TEST108 暴露
`tiny-regex-c` 在字符类中匹配 `-` 时会在第一个范围运算符处提前失败；本地补丁让扫描
继续寻找末尾字面量。VS2008 ARMV4I 构建和 `C:\WMShare\Positron-next143` staging
通过，最终 `screen=480x640 dpi=192` 默认设备批次全部 PASS。

**next144（2026-08-08）**：`positron_core` 新增 inline `<script>` 文档顺序枚举和按
UTF-8 `id` 查询/读写 `textContent` 的公共 C ABI；core 仍不依赖 Duktape。WM 宿主读取
`test_host.ini` 的 `javascript=0/1`，默认 `0` 时在 DOM 扫描前返回，因此 TEST13 不新增
脚本抓取或执行。开启时，同页 classic inline scripts 在初次 style/layout 前共享一个
`positron_script.dll` context，并通过最小 `document.getElementById(...).textContent=`
bridge 修改 DOM。TEST110 覆盖关闭不执行、文档顺序共享状态、JSON type/external src
跳过及 mutation 进入正式 layout。当前不含 external 执行、事件、持久 context 或完整
DOM binding；ARMV4I 增量构建已通过，`screen=320x320 dpi=128` 默认设备批次全部 PASS。

**next145（2026-08-08）**：`PCore_GetScriptCount/PCore_GetScript` 统一返回非空 inline 与
external script 的 DOM 顺序；external 项按相同 resolver 映射到 document cache。Browse 宿主
只在 `javascript=1` 时调用已有资源 worker round 抓取 external body，再与 inline body 共用
一个初始 Duktape context；失败 external、JSON/module 和 disabled 开关均跳过，不撤销导航。
TEST111 的离线 fixture 覆盖成功/失败 external、JSON 跳过、顺序结果 `1 → +10 → 11` 和
DOM `textContent`。ARMV4I 增量构建、C89、仓库审计与 `screen=320x320 dpi=128` 设备验收
均已通过；默认 `javascript=0`，因此不改变 TEST13 的网络路径。

2026-08-07 的 next126 设备日志记录为 `screen=320x320 dpi=128`：TEST13 的
`example.com`、IANA Example Domains、IANA Reserved Domains 三段导航均完成，随后
TEST20 停止。TEST20 的失败是断言隔离错误：它走显式 CSS 视口的离线缓存图片路径，
却按设备 DPI 计算 `48px` 的期望物理尺寸；实际盒为 `48x48`。next127 将该测试固定
到 96 DPI CSS 视口并修正失败诊断字段；这不等于高 DPI Browse 已验收，下一轮仍需
在不同分辨率/DPI 下记录日志并人工检查 TEST13。

设备验收记录要求：每次人工批次至少保留 `test_host.log` 开头的
`screen=宽x高 dpi=值`，并尽量轮换一个纵横方向或分辨率、一个 DPI 档位。自动断言
只证明对应代码路径和资源计数，不替代高 DPI 下的 Browse 版式、滚动、链接和旋转
截图检查。

2026-08-07 的 next127 设备日志记录为 `screen=240x320 dpi=96`：TEST13 的三段
导航、TEST20/27、ENGINE/表单回归以及 TEST80-96 均通过。TEST97 停止原因是测试
要求错误文本包含大写 `JSON`，而 Duktape 实际返回 `SyntaxError: invalid json ...`；
这不是 JSON 注入或上下文恢复失败。next128 只放宽该测试对引擎错误文本大小写的
耦合，仍要求 `PSCRIPT_ERROR_JSON` 和非空诊断，设备结果待补。

next128 的设备日志随后在 `screen=240x240 dpi=96` 下完成 `TESTBENCH PASS`，覆盖
TEST13、TEST20、TEST27、TEST43-99。next129 将 TEST20 从临时的 96 DPI 显式 CSS
视口改回真实 `PCore_SetDeviceViewport` 路径：48 CSS px 的期望值按当前设备 DPI
换算为物理像素；96 只保留为 CSS 规范的参考基准，不再作为设备 DPI 强制值。

next129 在 `screen=480x640 dpi=192` 下确认 TEST13 与 TEST20 的动态换算通过；
TEST27 暴露同一类旧断言，要求 `120x60` CSS SVG 盒却直接比较设备像素，实际为
`240x120`。next130 已让 TEST27 在 style/layout 前安装设备视口，按 DPI 检查盒尺寸，
并按物理坐标采样 SVG 色块；非 96 DPI 的设备回归待继续验证。

next130 随后的日志为 `screen=480x480 dpi=192`：TEST13/20/27/43/44 通过，TEST56
报告 `70/70/70` 与 `sum=210`。这是离线 TEST56 几何段继承 192 DPI 后把 105 CSS px
换算为 210 设备 px，并非 table 算法回归。next131 已把该段显式设为 96 DPI CSS
契约，同时让 TEST56 可见渲染段使用设备视口；58-99 的设备结果待补。

next137 继续保留 TEST20/27 的严格物理尺寸断言，不固定设备 DPI；设备复测时仍要先
记录日志头部的 screen/DPI，并确认运行目录中的 core/libcss 版本来自同一个 staging 包，
再判断是否是设备加载旧 DLL。

> **接手前先读**：导航路径以用户确认正常的 `9c5c7c7`/next37 为冻结起点，此后 `main` 已继续叠加图片、字体、列表和表格能力。next37 后那组失败的导航实验保存在远端 `codex/post-next37-experiments`，不得直接合回；这不表示当前整个仓库仍停在 next37。冻结项、失败时间线和后续门槛见 `ROLLBACK_NEXT37.md`。

## 项目目标

Positron 是面向 Windows Mobile 6 Professional / WinCE 5.02 / ARMV4I 的现代基础设施集合，并在其上建设浏览器内核和 Electron-like 应用运行时。公共 DLL 必须能被其他 WM 程序独立调用，不能只按 test_host 或浏览器内部模块设计。完整分层见 `ARCHITECTURE.md`。核心原则是“给 WM6 打补丁”，不是重造系统：

- 现代 TLS 是 WM6 缺口，所以用 `positron_tls.dll` + mbedTLS 2.16.12。
- 现代 HTML/CSS 渲染是 IE Mobile 缺口，所以移植 NetSurf 3.11。
- WM6 已有且够用的能力优先复用：明文 HTTP 用 WinInet，绘图用 GDI，后续图片应优先考虑 WM Imaging API。
- WM6 缺少的成熟能力优先联网检索并移植许可证兼容的开源实现；只有平台胶水和 ABI 包装才优先自写。

源码依赖现已自包含：NetSurf、mbedTLS、cJSON、Expat、libjpeg-turbo、NanoSVG、Duktape 与 Noto 字体都固定版本并随仓库提供。新环境只需另行安装不可再分发的 VS2008 SP1、WM6 Professional SDK 与模拟器；运行 `python scripts\audit_repo.py` 可检查 15 个工程引用、版本和许可证。第三方边界见根目录 `THIRD_PARTY.md`。

## 当前真实状态

Phase 1-3 已完成：

- `positron_tls.dll`：TLS 1.2，CA bundle，hostname/chain verify，`CryptGenRandom` 熵源。
- `positron_json.dll`：cJSON 包装。
- `positron_http.dll`：HTTP/HTTPS GET/POST，HTTPS 走 mbedTLS verified，明文 `http://` 走 WinInet，支持有限重定向。

Phase 4 已越过 M7-table 和 M5f border/selector，并推进到 TEST57 的百分比 table-row 高度验收：

- NetSurf 底层库已在 VS2008 / WinCE / ARMV4I / C89-only 下编译通过：
  `positron_netsurf`、`positron_hubbub`、`positron_libdom`、`positron_libcss`。
- `positron_core.dll` 是正式引擎边界，公开 `PCore_*` opaque-HANDLE API。
- Browse 正式路径已经切到 NetSurf 真实引擎：
  `PCore_LayoutDocument` / `PCore_PaintDocument` / `PCore_LinkAt` 走 `pcore_box_construct` -> NetSurf `layout_document` -> `html_redraw` -> GDI plotter。
- 旧的手写 block/inline layout + paint 已退休。
- `layout_flex.c` 已移植并真机验证，TEST 17 三色块横排。2026-07-11 的 TEST 22 进一步确认 `row-reverse` 配合 25px leading padding 时主内容不会被推到 viewport 左侧；这只是该回归子例，不代表完整 flex 兼容。
- `table.c` 已移植，`pcore_construct_table` 生成 `BOX_TABLE > ROW_GROUP > ROW > CELL`，TEST 17 2x2 table 网格真机验证。
- `redraw_border.c` 已接入源码和 `positron_core.vcproj`；`pcore_layout_stubs.c` 中 border no-op 已移除。2026-07-08 根据真实 VS2008 错误补齐 include 后，2026-07-10 已成功复编；TEST 17 真机可见 H1、flex、table/cell 边框并通过。
- `pcore_select.c` 已实现 CSS attribute selectors、adjacent/general sibling selectors、`:link` 与 `:lang()`；TEST 9 已于 2026-07-10 真机通过。
- TEST 11 原有 `body.y=8` / `p.y=24` 是旧手写布局器预期；NetSurf 折叠结果为 `body.y=p.y=16`。当前源码新增 `padding-top:1px` 阻断组，必须同时得到 `(16,16)` 与 `(8,25)` 才通过；2026-07-10 用户真机截图已确认 TEST 11 OK。
- TEST 18 的两个 `<img src>` 资源发现/fetch 已于 2026-07-10 真机通过；2026-07-11 已确认 document user-data 字节缓存与 URL 去重，二次扫描 fetch calls 保持 2。
- `pcore_wmimage.cpp` 是刻意新增的 C++ 小适配层，主体仍为 C89。TEST19 的 BMP/PNG/JPEG/GIF 可见绘制已确认；TEST20 小点回归也已通过 `g_render_sheet` 修复，四格式缓存绘制与资源计数 4/4 已由设备确认。
- TEST 21 已确认 `css_media.width/height` 采用实际 client viewport；2026-07-11 用户又确认整数像素 MQ4 `(width <= Npx)` / `(width < Npx)` 的 320/300/299px 边界通过。随后 TEST13 方框在空白折叠修复后消失且词间距正常；补齐 NetSurf 上游 `<pre>` UA 默认后，TEST15 已确认 `normal_ws=ok pre_lf=kept`。页脚/导航拥挤仍未解决。TEST24 的滚动比例断言及真实 TEST13 横竖屏同区域保持均已确认。

## 关键文件

- `positron_core/pcore_box.c`  
  DOM + computed style -> NetSurf `struct box`；含 flex/table 构建；正式 layout/paint/link hit-test。

- `positron_core/pcore_select.c`  
  libcss select handler、UA CSS、整树 computed style、外部 `<link rel=stylesheet>` 抓取入口；attribute/sibling/static-pseudo selector 已接入，next109 又让 live `:checked/:enabled/:disabled` 与宿主馈送的 `:focus/:active` 参与正式重样式。

- `positron_core/pcore_plot_gdi.c`  
  NetSurf plotter table + GDI 字体测量表。

- `positron_core/pcore_talloc.c`  
  精简 talloc 垫片。

- `positron_core/nsshim/`  
  拦截 NetSurf 头文件的 shim 层，支撑 `layout.c` / `redraw.c` / `layout_flex.c` / `table.c` 编译。

- `positron_core/pcore_layout_stubs.c`  
  仅保留未移植/未产生路径的链接桩。注意注释可能落后，看到 stub 前先确认真实源码和 vcproj。

- `test_host/main.c`  
  设备端唯一可靠测试 UI。没有 stdout，所有结果靠 MessageBox/window。TEST 19 是 WM Imaging 原生内存 BMP 解码/绘制基线；TEST 20 才验证缓存 `<img>` 已接入布局树和重绘链。

## 构建与运行

工具链：

- Visual Studio 2008 SP1
- Windows Mobile 6 Professional SDK
- ARMV4I
- C89-only，不能写 C99/C++11 风格代码

构建：

1. 首选运行 `scripts\build.bat`；默认执行 `Debug` 增量 `Build`。改工程依赖、生成规则或需要干净基线时显式运行 `scripts\build.bat Debug rebuild`。
2. 可用 `scripts\build.bat Debug build` 做增量构建，或用第二参数 `clean` 清理。`scripts\stage.bat` 也会在复制前自动调用同配置的增量 Build；构建失败时不复制，避免新 EXE 搭配旧 DLL。
3. 脚本调用 `Common7\IDE\devenv.com`，不是直接调用 `VC\ce\bin\x86_arm\cl.exe`；前者负责 `.sln` 工程依赖和完整 WM6 平台设置。
4. GUI 等价操作是打开 `Positron.sln`，选择 `Debug | Windows Mobile 6 Professional SDK (ARMV4I)` 后 Rebuild whole Solution。

2026-07-11 已由 Codex 在本机通过该脚本完整重建：9 个工程成功、0 个失败；随后增量构建也成功并报告 9 个工程均为最新。根目录 `vs2008-build.log` 保存最近一次调用的输出（已忽略，不入 git）。

部署：

```cmd
scripts\stage.bat
```

复制到 `C:\WMShare\`，然后在模拟器/设备里从 `\Storage Card\test_host.exe` 启动。不要依赖 VS Smart Device Deploy。
若旧 `test_host.exe` 锁住默认目录，可先真正关闭旧实例，再运行 `scripts\stage.bat Debug C:\WMShare\Positron-next` 隔离新二进制；不要把不同构建的 EXE/DLL 混在同一运行目录。

## test_host 分组

启动时可选择：

- 快速配置：next219 定向基线的 `test_host.ini` 使用 `tests=13,151-186,999`（38 项），ini 为 1278 字节；最近全量检查点 next216 已在 `screen=240x320 dpi=96` 通过 131 项；TEST174 是已通过的绝对 URL 末尾百分号编码单点段门；TEST175 是已通过的根相对编码单点段门，TEST176 是已通过的根相对末尾编码单点段门，TEST177 是已通过的绝对重复编码单点段门，TEST178 是已通过的根相对重复编码单点段门，TEST179 是已通过的绝对编码双点段门，TEST180 是已通过的根相对编码双点段门，TEST181 是已通过的绝对末尾编码双点段门，TEST182 是已通过的根相对末尾编码双点段门，TEST183 是已通过的绝对半编码双点段门，TEST184 是已通过的根相对半编码双点段门，TEST185 是已通过的绝对末尾半编码双点段门，TEST186 是已通过的根相对末尾半编码双点段门。TEST137 是只读 location/document URL 与延迟 `history.back()` 门，TEST138 是已通过的延迟 location 赋值门，TEST139 是已通过的 `location.reload()` 门，TEST140 是已通过的 `location.replace()` 门，TEST141 是已通过的 `history.forward()` 门，TEST142 是已通过的 `history.go()` 门，TEST143 是已通过的只读 `history.length` 门，TEST144 是已通过的初始 `history.state` 门，TEST145 是已通过的受控 `history.replaceState()` 门，TEST146 是已通过的同 URL `history.pushState()` 门，TEST147 是已通过的同 document traversal 门，TEST148 是已通过的最小 popstate 门，TEST149 是已通过的 history 片段 URL 门，TEST150 是已通过的最小 hashchange 门，TEST151 是已通过的 location URL 组件门，TEST152 是已通过的 location.hash 导航门，TEST153 是已通过的 href/assign/replace 片段引用门，TEST154 是已通过的绝对同文档片段 URL 门，TEST155 是已通过的根相对同文档片段 URL 门，TEST156 是已通过的 query-relative 同文档片段 URL 门，TEST157 是已通过的同目录 path-relative 同文档片段 URL 门，TEST158 是已通过的单个 `./` 同目录片段 URL 门，TEST159 是已通过的单个 `../` 父目录片段 URL 门，TEST160 是已通过的连续前导父目录片段 URL 门，TEST161 是已通过的父目录后单个 `./` 片段 URL 门，TEST162 是已通过的父目录后连续 `./` 片段 URL 门，TEST163 是已通过的连续前导 `./` 同目录片段 URL 门，TEST164 是已通过的父目录后单个内嵌 `./` 片段 URL 门，TEST165 是已通过的父目录后连续内嵌 `./` 片段 URL 门，TEST166 是已通过的父目录后多位置内嵌 `./` 片段 URL 门，TEST167 是已通过的根相对单个内嵌 `./` 片段 URL 门，TEST168 是已通过的根相对连续内嵌 `././` 片段 URL 门，TEST169 是已通过的根相对多位置内嵌 `./` 片段 URL 门，TEST170 是已通过的绝对 URL 单个内嵌 `./` 片段导航门，TEST171 是已通过的绝对 URL 连续内嵌 `././` 片段导航门，TEST172 是已通过的绝对 URL 多位置内嵌 `/./` 片段导航门，TEST173 是已通过的绝对 URL 内嵌百分号编码单点段片段导航门，TEST174 是已通过的绝对 URL 末尾编码单点段门，TEST175 是已通过的根相对编码单点段门，TEST176 是已通过的根相对末尾编码单点段门，TEST177 是已通过的绝对重复编码单点段门，TEST178 是已通过的根相对重复编码单点段门，TEST179 是已通过的绝对编码双点段门，TEST180 是已通过的根相对编码双点段门，TEST181 是已通过的绝对末尾编码双点段门，TEST182 是已通过的根相对末尾编码双点段门，TEST183 是已通过的绝对半编码双点段门，TEST184 是已通过的根相对半编码双点段门，TEST185 是已通过的绝对末尾半编码双点段门，TEST186 是已通过的根相对末尾半编码双点段门。`javascript=0` 是默认产品门，只有显式改为 `1` 才执行初次加载的 classic inline/external scripts，并保留页面 context、click listener、原生表单事件、EDIT/SELECT 键盘、focus、beforeinput、Unicode/代理对、composition、event target/currentTarget、classList、style、form default 和最小 location/history bridge；未成功抓取或不支持类型的 external 会跳过。TEST79/float 候选已撤回。自动日志会在开头写入 screen/DPI；若 TEST20 的 48 CSS px 被换算成异常物理尺寸，先记录设备指标，不要放宽断言。也支持 `tests=1-5 7b 999` 一类语法；999 只有被显式选中且前序通过时才响。`auto=1` 时不弹 Yes/No/OK，窗口首帧后自动关闭，TEST13 自动跑 example.com → IANA Example Domains → Reserved Domains，并把每个原始结果和逐页遥测覆盖写入同目录 `test_host.log`；`auto=0` 保留 Yes/No 与原四组路由。自动首帧冒烟不替代新视觉能力的人工截图；next167 已另行人工确认 Learn More 边距与真实 SIP 候选词完整输入。缺失/无效配置不会静默改变测试范围，TEST23/78/79 不可选。

- Communication：TEST 1-5，TLS/HTTP/JSON，需要网络。
- Engine：TEST 6-11、15、16、18、21、22、24、25、38、40-45、59-61、74-77，解析/选择/样式/layout/box tree/image resource cache、responsive media viewport、reverse flex、cached CSS restyle、SVG parse、受约束的 `:root` token、数值型 OKLCH/可求值 calc、grid/overflow min-content 隔离、overflow scrollbar、分阶段资源事务、失败回滚、CSS import tree、selector node-data restyle、具名 NetSurf option 默认、DOM Event 传播/取消、基础 relative/absolute positioning、动态 `:hover` 与脚本资源发现/缓存 ABI，离线。TEST40-45、59、60、74-77 已真机确认；next78 扩展测试及其 core 行为已经撤回。TEST23/79 浮动候选均因真实 Browse/设备回归撤回，不运行。
- GDI Render：TEST 12、14、17、19、20、26-37、39、46-58、62-73，离线窗口渲染、WM Imaging 位图、SVG path/cache/fallback/fill-rule、CSS background-image、原生 GDI text、线性/径向渐变、同文档及重叠文档缓存复用、table/list、HTML inline author CSS、普通表单、multipart/file、WM multiple select、首批 required 验证与动态表单伪类；TEST65-73 已验收。
- Browse：TEST 13，真实页面抓取 + 渲染，需要网络。

当前最关键验证：

- TEST 17：内置 NetSurf real layout + redraw 页面。预期：深红 H1 和红色下边框、带边框的三色 flex 横排、2x2 table 可见 cell 边框。
- TEST 56：显式 table height 分配。预期：105px 三行等高且文字依次 top/middle/bottom；70px 两行等高且橙色 rowspan 文字在底部；页面无多余纵向滚动条。
- TEST57：第一张 80px 表应约为 20/40/20px，第二张 50px 超约束表应为 25/25px；next73 已连同 TEST55/56 一起通过。
- TEST59：分别在 224px 和 320px viewport 建立无 Grid `overflow:auto` 宽表格夹具，必须保持 reversed-flex main 的 `x=25,width=viewport-50`。next78 的同 DOM 旋转/scrollbar 诊断版本已经撤回。
- TEST60：同一 DOM 先按 224×320、再按 400×240 重做 style/layout；IANA 同型 `.dtable` 的首个 `<th>` 必须保留 18px/10px inset，并与第二个同文字表头保持同一粗体宽度。它同时覆盖 `thead th`、`:first-child` 和后续 `tbody > tr:first-child > th` 选择器。
- TEST61：正式 NetSurf layout 中，同一串文本的 `1px` 与 `8.5pt` 必须测得相同宽度，证明 `font_min_size=85` 生效；`12pt` 控制组必须更宽。JavaScript 策略继续为 false。
- TEST80：不初始化 `positron_core`，直接加载独立 `positron_script.dll`，验证 Duktape 求值、持久上下文、错误恢复、内存计数和执行计数；不代表浏览器 DOM/window/network binding 已实现。设备日志已确认通过。
- TEST81：不初始化 `positron_core`，在独立脚本上下文中验证 50 ms 执行预算能打断无限循环、超过 `PSCRIPT_MAX_SOURCE_BYTES` 的源码被拒绝，以及拒绝/超时后仍能求值 `42`；这是 timeout/source-boundary/recovery 断言，不是完整内存配额或浏览器 JS 验收。next119 设备日志已确认通过。
- TEST82：不初始化 `positron_core`，以 `PScript_CreateEx` 建立 512 KiB Duktape heap 上限，执行短生命周期数组压力，要求返回 `PSCRIPT_ERROR_MEMORY_LIMIT`、峰值不超过上限，并在失败后求值 `42`；这是 runtime heap 边界，不是浏览器 JS 或模块生命周期验收。next120 设备日志已确认通过，峰值为 496184/524288。
- TEST83：不初始化 `positron_core`，以 `PScript_EvaluateModule` 验证 CommonJS 风格模块一次执行缓存、`require()`、失败条目回滚、`PScript_ClearModules` 和清空后的重新加载；没有 URL、文件、网络、DOM 或 window 解析。next121 ARMV4I Debug/Release 构建、staging 与设备日志均已确认通过。
- TEST84：不初始化 `positron_core`，以 `PScript_SetModuleSourceProvider`/`PScript_LoadModule` 验证宿主按名提供根模块和 `require()` 依赖、缓存命中不重复回调、provider 失败、执行失败回滚、buffer 释放和清空后的重新取源；没有 URL、文件、网络、DOM 或 window 解析。next134 的 `screen=240x320 dpi=96` 设备日志已确认通过。
- TEST85-89：不初始化 `positron_core`，以 ABI 1.4 的 primitive global setter、JSON getter、JSON-array function call、跨调用状态、错误恢复、非法全局名与 255 字节结果上限做五项断言；没有 URL、文件、网络、DOM 或 window 解析。next134 的 `screen=240x320 dpi=96` 设备日志已确认通过。
- TEST90-94：不初始化 `positron_core`，以 ABI 1.5 的同步 JSON 宿主回调注册、compact JSON 参数/返回值、失败恢复、同名替换/注销和固定 16 槽上限做五项断言；回调不能重入或异步持有上下文，结果最多 255 字节有效载荷；没有 URL、文件、网络、DOM 或 window 解析。next134 的 `screen=240x320 dpi=96` 设备日志已确认通过。
- TEST95-99：不初始化 `positron_core`，以 ABI 1.6 的 `PScript_SetGlobalJson` 注入 object/array/string/number/boolean/null，覆盖跨调用 mutation、malformed JSON 恢复、64 KiB 输入上限原值保留和类型替换；没有 URL、文件、网络、DOM 或 window 解析。next134 的 `screen=240x320 dpi=96` 设备日志已确认通过。
- TEST62：四个离屏探针确认 checkbox/radio 均采用 1em 几何，最终 gadget 的 checked 状态为 0/1，选中状态增加像素暗度，hidden input 不生成 box；它是静态 redraw 基线。
- TEST64：按盒树坐标执行 checkbox 切换、disabled 点击、同表单同名 radio 互斥、跨组/跨表单隔离和已选项幂等，再从 240×320 重排到 320×240 并复核 DOM 状态；next93 自动设备日志已通过。
- TEST13 next86 遥测：关闭 Browse 窗口后，既有 OK 框显示最后一次导航的 total/network/max-UI、parse/style/images/layout/paint、资源 queued/ok/fail、worker rounds、document/cache bytes 和 budget-rejected。style/images 是多轮累计，max-UI 才是单次消息循环最长阻塞。
- next87 在同一 OK 框追加 core layout 的 box/first/settle/final/other 与 settling pass。`PCore_GetLayoutStats` 只复制每个 document 最近一次布局统计；未改变构盒、两轮布局判定、几何或重绘。设备已确认两类真实页面的构盒均约 500ms；该结论只确定下一步细分方向，不代表卡顿已经优化。
- next88 新增独立 `PCoreBoxStats`/`PCore_GetBoxStats`，避免扩展 next87 已公开结构的大小。tree/backgrounds 互不重叠；tree 内 style/text/image/anonymous/table-normalise 互不重叠，`other` 为剩余 DOM 遍历、分支与分配时间。逐调用 `GetTickCount` 有轻微诊断开销，比较分布优先于比较 next87 的绝对毫秒。
- next88 设备数据已把两页热点缩到单张图片创建（518/474ms）。next89 用现有 `positron_image.dll` 做 XML-like 字节 SVG-first，避免先让 WM Imaging 失败；同一 document 的二次 layout 借用 image cache retained handle。TEST20/27 已通过 4/4、1/1 reuse，TEST27 也通过首次 markup-first。它不提供跨导航或跨线程句柄共享。
- TEST13 关闭后依次显示两个短框：`overview` 与 `box detail`。后者的 `image reuse/markup-first` 用于区分重排复用和首次 SVG 分派；两个概念不能相互代替。
- TEST13 显示的是导航完成快照；后续旋转布局不会回写 `g_nav_last_stats`。不要用旋转后弹窗的 reuse 值判断旋转路径，复用门禁在 TEST20/27。
- next90 将 `positron_image` ABI minor 提到 1.5，新增按 SVG handle 查询 total/setup/parse/raster；core 用独立 `PCoreImageDecodeStats` 汇总，不扩大 `PCoreBoxStats`。TEST27 与 TEST13 只读显示该数据，不改变创建、layout 或 redraw。
- next91 在 next90 只读候选上增加可选无人值守 testbench；不新增旁路测试实现，仍调用原 TEST 函数、公共 WndProc 和导航事务。失败保持 fail-fast，并以非零进程返回值及 `test_host.log` 的 `TESTBENCH FAIL` 收尾。
- next92 只共享同时存活文档的 SVG：键包含 URL、长度和两种 32 位内容哈希，document cache 各持一份引用，最后一个引用释放时立即销毁句柄。它不缓存空闲对象、不跨线程，也不改变位图所有权。TEST63 覆盖第二文档复用、首文档释放及后续像素绘制。
- next93 的 form 激活入口使用 document CSS px，与 `PCore_LinkAt` 相同；宿主先处理 overflow scrollbar，再处理 form control，最后才处理链接/空白关闭。控件状态同步回 libdom，因而 `WM_SIZE` 重建盒树后仍保留。next94 接通单行 text/password；next97 复用同一枚举、销毁、滚动、旋转和 `EN_CHANGE` 路径接入 textarea。next98 接入单选 `COMBOBOX`；next104 在同一生命周期中为 multiple 创建原生 `LISTBOX`，用 `LB_GETSEL/LB_SETSEL` 同步每项并回滚 disabled option。不要重写 radio 分组或既有输入同步逻辑。
- TEST 13：Start page -> Open example.com -> 点击页面链接，走正式 Browse 路径。

## 当前限制 / 下一步

优先候选：

> 2026-08-04：先解决“有没有”，再解决已有小范围“好不好”。next111/TEST75 已完成并验收 basic relative/absolute positioning，next113/TEST76 又完成并验收基础动态 `:hover`，next114/TEST77 的脚本资源接口已通过设备门禁；next115/116 的 TEST79 和真实 TEST13 均失败，Float 方向暂挂并恢复 next114。下一批评估显式 JavaScript 开关、基础 Grid 或背景尺寸等高价值缺口，继续每批保留 TEST13 深链/旋转门禁。高级约束验证与专用事件数据随后扩展。已有 NetSurf Duktape backend 的 JavaScript 最小纵切保持中期。首屏 SVG 冷解析、抗锯齿、渐变高级参数、视觉微调和全面性能优化后置，除非它们造成崩溃、数据错误或阻塞基本操作。

1. 2026-07-11 用户真机确认 ENGINE 原整组至 TEST24 通过；2026-07-12 单独确认 TEST25 SVG parse。后续修改引擎路径时必须重跑当前整组。
2. TEST23 的浮动构盒最小复现虽通过，但真实 Browse 严重回归，已撤回。next115/116 的 TEST79 和 TEST13 也均失败，float 代码、配置和 ENGINE 接入已恢复到 next114。TEST13 起始页正常不等于所有 IANA 子页正常；若未来重启 float，必须先完成上游 box construction/normalisation 的完整方案，并通过深层导航和旋转门禁。2026-07-24 `/domains/reserved` 已再次证明这一点；next80 已让 TEST60 与真实页横竖屏全部通过；next78 仍因扩大回归和系统异常保持撤回。
3. `WM_SIZE` 从 document-owned 外链 CSS 缓存 restyle + layout，且使用 cache-only callback。TEST24 与真实 Browse 旋转均已确认。
4. 主文档、外链 CSS、CSS `@import`、`<img>` 和 CSS 背景 GET 已组成分阶段 worker 事务；DOM/style/layout 仍只在 UI。2026-07-14 设备已确认 TEST3 的真实单响应正文进度、TEST43 资源事务、TEST44 主文档失败回滚和 TEST13 IANA Browse 基线。UI 提交现由一次性 WM timer 拆成 parse/style/image-discovery/layout 四段，单个 NetSurf 调用仍可能卡顿。宿主暂存预算为 64 URL/2 MiB，不是 core ABI 限制；整页聚合进度、网页字体和脚本仍待处理。
5. TEST30-37 已于 2026-07-13 真机通过：CSS 背景、基础 text、连续线性/径向渐变及坐标、继承/透明 stop、循环保护、径向 SVG 文档缓存以及 `<img>`/CSS 背景单次 fetch 复用均成立；复杂 shaping、`textPath`、逐字定位、任意 shear、径向焦点与 spread method 尚未实现。
6. 当前 IANA 线上 CSS `iana_website.80c103cc08b6.css` 使用 custom properties、媒体查询范围语法、`oklch()`、`calc()`、grid/gap 与 `:has()`。整数像素媒体范围、同表顶层 `:root` token、TEST40 的数值型 OKLCH/可求值 calc、TEST41 的 `/numbers` 宽度隔离及 TEST42 的 NetSurf overflow scrollbar 均已真机确认；不要扩大表述为完整 MQ4、custom-properties、CSS Color/Values、Grid、触摸惯性或 overlay scrollbar 支持。
7. 图片/SVG：TEST20 四格式缓存 `<img>`、TEST25-37 的 SVG 正式链，以及 TEST13 网络相对 SVG 链均已真机确认。公共 `positron_image.dll` 已覆盖 retained WM 位图/SVG、旧 core 转发、PNG/JPEG/BMP/GIF 编码与静态 libjpeg-turbo 4:4:4。next51 已确认 ABI 1.4 启动前 BMP/GIF 编码、签名、回读检查和六项视觉均正常，但标题栏 X 仍按 WM 约定 Smart Minimize，说明仅处理 `WM_CLOSE` 不足。next52 改用 `SHDoneButton(SHDB_SHOW)` 的原生标题栏 OK，并处理 `WM_COMMAND/IDOK`；用户已确认进程真退出且可再次启动。按用户要求不增加左右软键。复杂文本、径向焦点、spread method、background-size 和多层背景仍是显式缺口。
8. 测试节奏：默认按能力批次积累多个相关实现、自动断言和直绘/正式链回归，再用一个 `test_host.ini` 一次交付多个 TEST。除编译错误、高风险回归定位或设备专有故障外，不应为每个微小改动单独要求用户验收。
9. `PCore_StyleDocumentEx2` 是旧样式 ABI 的兼容扩展：接收绝对 document URL 和宿主 URL resolver；core 用 libcss 原生 pending/register 机制处理最多 16 层导入，失败导入注册空表。WM 宿主使用 `InternetCombineUrlA`，旋转只读 document CSS cache。TEST45 已真机确认嵌套、缺失回退、URL 规范化和缓存重选。
10. 表格构盒已移植 NetSurf 3.11 的 span occupancy，next53 已确认 TEST46；next56 又确认匿名包装与空 cell。next64/TEST53 的基本 collapsed-border 冲突已验收；next65/TEST54 又确认 rowspan 实际终止 row 和 row-group 边界承接。next67/TEST55 已确认 cell baseline 与 separated-table `empty-cells:hide`；next68/TEST56 已确认显式 table height 向 row/cell 的比例分配。NetSurf 3.11 与 2026-04-28 官方最新源码仍未建模 column/colgroup border 来源，该项继续保留。
11. next54 的宿主纵条和 auto-height 横条空间有效，但第二次整树 layout 误改 fixed-height overflow 几何，导致 TEST42 原右箭头点击位置失败；右箭头图形还相对 16px 控件向下偏 2px。该包不能作为新基线。
12. next55 在二次 layout 前屏蔽 fixed-height `overflow:auto` 的首轮横向 extent，只让 auto-height 容器获得额外空间；右箭头改用与左箭头对称的 `area.y0` 基准。用户已确认 TEST41/42、短页纵条与色块页均正常；冻结的 TEST13 导航链未改。
13. next56 按 NetSurf 3.11 规则补 table/row-group/row/cell 匿名盒和短行空 cell。用户已确认 TEST47 红/白、绿/蓝两行及同批其余测试正常。
14. next57 移植 NetSurf 列表 marker 构造并恢复 LI DOM user-data 映射。TEST48 自动校验 disc/circle/square、十进制 `start/value/reversed` 及 marker 几何；`PCore_ListMarker` 是只读诊断 API。
15. next58 引入 Noto OFL 来源的静态 Positron Symbols/Emoji 子集。next59 追加官方 hinted Noto Sans Symbols Basic 子集，用生成的精确 cmap 覆盖表互补两套 symbol face；设备确认箭头不再 tofu、marker 和五个 emoji 均可见且视觉稍有改善。当前字体范围明确只包含符号与单色 emoji；不要继续加入普通语言/多语种字体，也不要宣称网页 `@font-face`、复杂 emoji shaping 或彩色字体支持。`ANTIALIASED_QUALITY` 最终效果仍取决于 OEM GDI。
16. next60 用生成的 `positron_format_list_style.c` 替换 decimal-only stub，算法与 47 种样式来自仓库内原版 libcss。`scripts/port_list_style_vs2008.py` 负责指定初始化器和 UTF-8 字面量的可重复 C89/ASCII 转换；`list-style-image` 复用 document image cache，只有 computed list-item 才发现资源，解码失败保留类型 marker。next60 首次设备 TEST50 的 `found=4 fetched=2` 来自旧 Debug core DLL 打包事故；`stage.bat` 增加自动增量 Build 门禁后，next61 已确认 TEST50 的计数、缓存 SVG marker 与失败回退全部通过。
17. next62 把 NetSurf 3.11 已计算但未参与 layout 的 `list-style-position:inside` 接到 inline-first 首行：marker 尺寸在 minmax 前准备，首行吸收 marker+4px，换行恢复内容起点，图片 marker 可抬高行高。TEST51 通过新增只读 `PCore_ListItemGeometry` 自动检查 outside/inside、`VIII.`、缓存 12x12 SVG 与悬挂换行；用户提供的横竖屏截图均符合预期。`c89ize.py` 同批增加注释前导声明、函数头和多行初始化声明规则，`scripts/test_c89ize.py` 的 4 个回归必须先通过。
18. next63 按 W3C inside marker 的首个 inline element 语义，在构盒时加入零宽匿名 inline run；block-first、空条目、嵌套列表和 block-first 图片 marker 因而都由原 NetSurf block/inline layout 计算高度与兄弟位置，不在 layout 后手改坐标。TEST52 的 III/IV/V/VI、空行、嵌套缩进和绿色图片 marker 已由横竖屏截图确认。float 邻接仍明确不支持，本批未触碰冻结的 TEST13 导航链。
19. next64 新增 `PCore_TableCellBorder` 只读诊断，不改变布局/重绘。TEST53 一次检查 collapsed model 的 wider、style priority、hidden、left/top tie、origin precedence 与 separate 对照；用户已放大核对纵横屏截图并确认符合预期。
20. next65 对 vendored NetSurf `table.c` 做最小修复：bottom used border 使用 rowspan 实际终止 row；非末尾 row group 不冒充 table bottom，组间边由下一组 top 冲突算法承接。TEST54 自动断言 finite/zero rowspan、colspan 和 row-group 四组场景；仅需验收 TEST54，TEST13 路径未改。
21. next66 用 NetSurf 现有 inline baseline 约定补 table-cell baseline，并按 Mozilla `ShouldPaintBordersAndBackgrounds`/可见内容判定实现 separated model 的 `empty-cells:hide`。`PCore_TableCellGeometry` 只读返回 cell 与首段文字几何；TEST55 同时检查 top/middle/bottom、baseline、rowspan 与三类空格像素。因 baseline 是 table-cell 初始值，本批默认配置保留 TEST13 深层导航复测，不能只看 TEST55。
22. next66 的 TEST55 真机原始像素为 `FFFFFF/00C300/C6C300`，证明三类绘制正确，但设备 compatible bitmap 将 CSS `#00c000/#00c0c0` 量化了 3-6 色阶，使桌面式精确 RGB 断言假失败。next67 只改 TEST55 为紧格通道容差，未改 core layout/redraw。TEST13 Further Reading 的圆点来自 IANA 页面真实 `<li>` 与 next57-63 已验收的 marker 支持，不是 next66 回归。
23. next67/TEST55 自动断言和可见语义已验收；截图显示测试页因四组固定高度只超出 WM 客户区十几像素，仍生成了纵向滚动条。next68 将 TEST55 压缩到约 240px 内容高并设定标题行高；core 新增的显式表高分配参考 Blink 比例分配/小数余量规则，用 NetSurf rowspan 活跃列累加 cell bottom padding。`PCore_TableRowGeometry` 仅供 TEST56 读取最终 row 几何。2026-07-16 设备截图确认 TEST55 完整显示且无多余纵条，TEST56 的等高行、三种垂直对齐和 rowspan bottom 均正确；同批 TEST13 长页滚动正常。
24. 2026-07-20 完成仓库自包含审计：mbedTLS 2.16.12 完整官方源树和许可证已纳入 Git，cJSON 1.7.18 补齐独立许可证及来源。根 `LICENSE` 只覆盖 Positron 自有代码；NetSurf 浏览器源码的 GPLv2 等边界见 `THIRD_PARTY.md`。`python scripts\audit_repo.py` 当前检查 14 个工程、598 个工程输入、Git 跟踪、版本与关键许可证。
25. next69 首次百分比 table-row 第二遍得到错误的 `20/30/30`。随后切换包时 TEST56 的异常来自 WM/CE 全局 DLL 复用；next72 同包已证明 TEST56 正常。next72 的 TEST57 `styles=0:0` 暴露 inline `style=` 没有进入选择，next73 改外部 CSS 后 TEST55/56/57 均通过。后续确认直接根因是自有 `pcore_style_subtree` 固定传空 inline sheet，而不是未参与正式构盒路径的 NetSurf `author_level_css` 分支。next74 接入 inline sheet 后，TEST56 行高保持正确但 `.distributed .top` 失配；next75 修复祖先/父节点回调的通配 qname `*` 处理，设备已确认未改断言的 TEST56 和新增后代 class 断言的 TEST58 均通过。百分比 cell/后代内容和 `col`/`colgroup` 模型仍未覆盖。
26. 2026-07-24 TEST13 起始页正常，但进入 IANA `/domains/reserved` 后正文左移；该页没有 TEST41 的 Grid，只在宽表格外声明 `.dtable-wrap { overflow:auto }`。next77 把 TEST41 的 Grid fallback 特例收敛为通用但受限的 min-content boundary，仅对横向可收缩 flex item 的 grid/overflow 后代生效，显式 min-width 保持。设备确认 TEST59 和同批回归通过，竖屏子页边距恢复；继续旋转为横屏后，首个 `Domain` 表头内容却左移约 18px 至 wrapper 裁剪边界，字体/样式观感随之异常。
27. **next78 已撤回**：每次 layout 后递归调用 `scrollbar_set(...,0)` 并非无副作用的状态清零。设备上 TEST13 横屏从单个 `Domain` 异常扩大为全部表格单元格异常，随后 TEST56 报 `rows=35/35/35 35/35 sum=105/70 off=2/10/19 va=0/2/3/3`，并触发系统级 `test_host.exe` 异常。实现、`PCore_NodeScrollOffset`、`PCore_TableCellTextStyle` 与扩展 TEST59 已全部删除；旧包已改名为 `C:\WMShare\Positron-next78-FAILED-DO-NOT-USE`。
28. next79 恢复候选保留 next77 已验收的 flex min-content 修复，只恢复旧版 TEST59。`pcore_box.c` 与 `positron_core.h` 已回到 next77 内容；ARMV4I 重建后的 `positron_core.dll` `.text` 段大小 1,308,160 字节，SHA-256 `756629E25B063856B2DC334560B3EAB8C28A043D1758973FADE791BCC912CFFA`，与 next77 逐字节一致。包位于 `C:\WMShare\Positron-next79`，默认配置先隔离运行 TEST56/59，再单独跑 TEST13。
29. next79 已由设备确认 TEST56/59 正常，TEST13 也准确回到仅横屏首个 `Domain` 异常。继续审查 libcss 发现 Positron 的 `set_libcss_node_data` 曾立即执行 `CSS_NODE_DELETED`；但 `css__get_parent_bloom` 会在回调返回后继续使用该数据中的 bloom，形成悬空指针。next80 随后按 NetSurf `select.c` 的模式把数据挂到 libdom user-data，并在每个新 selection context 开始前递归失效旧缓存；TEST60 对两次 viewport restyle 的首表头几何和字重代理宽度做自动断言。
30. 2026-07-25 设备确认 next80 的 TEST56/58/59/60 全部通过；真实 TEST13 `/domains/reserved` 截图中首个 `Domain` 在横竖屏均恢复正常 inset、字重和基线，其余表格内容与滚动保持正常。该 selector node-data 生命周期批次完成。
31. 2026-07-30 next104 在 next103 表单基线上接入 WM 原生 multiple LISTBOX。不要改成自绘菜单：`LBS_MULTIPLESEL` 允许手指逐项切换，`LBS_NOINTEGRALHEIGHT` 保证窗口高度不偏离 NetSurf border-box；宿主逐项比较 `LB_GETSEL` 与 Core 状态，disabled option 被拒绝时以 `LB_SETSEL` 回滚。TEST71 同时检查 disabled select、单选 COMBOBOX、原生重建、GET 重复字段和 reset。设备日志连同 TEST13 深链及 TEST20/27/43/44/56/58-70 全部 PASS。
32. 2026-07-30 next105 首次接入提交前 required 验证，但 TEST72 reset 后仅恢复 6 个 invalid。根因不是断言，而是 libdom 0.4.2 会把无初始 `value` 属性的 text/password 第一次运行时写值记为 `defaultValue`。next106 在 `PCore_TextInputSetValue` 写入前冻结解析时默认值；设备日志确认 required text/password/textarea/file、checkbox、同名 radio、single/multiple select、首个无效控件几何、提交/Enter 阻止、两种 bypass、multipart 与 reset 全部通过，同批 TEST13/20/27/43/44/56/58-71 无回归。
33. 2026-07-31 next109/TEST73 将 live `:checked/:enabled/:disabled` 和宿主命中状态 `:focus/:active` 接到 libcss callback；交互变化采用 posted/coalesced cache-only restyle，避免在原生控件通知栈内同步销毁重建控件。设备日志确认焦点、按压、checkbox/option、纵横屏保持与 reset，并且 TEST13 三段导航和 TEST20/27/43/44/56/58-72 全部通过。此批不包含 DOM Focus/Mouse 事件传播、取消/default-action；无 CSS 尺寸的空 text input 仍没有浏览器默认 intrinsic size。
34. 2026-07-31 next110/TEST74 复用 libdom EventTarget 建立公共 C ABI，修正 target 重复传播、`bubbles/cancelable` 未生效和 dispatch-only 状态残留；Browse 宿主在既有 click 默认动作前派发可取消事件。设备日志确认 TEST74 及 TEST13/20/27/43/44/56/58-73 全部 PASS。专用事件类型数据、完整 HTML activation 与 JS binding 仍是后续边界。
35. 2026-08-03 next111/TEST75 按 NetSurf `box_construct.c` 的 absolute inline 特例补齐 slim builder：普通 relative box 保持正常流并应用偏移，absolute/fixed block 进入既有正式定位路径，`display:inline` 脱流元素改为 `BOX_INLINE_BLOCK` 后由 `layout_position_absolute()` 消费。设备日志确认 TEST75 及 TEST13/20/27/43/44/56/58-74 全部 PASS。该批不代表 float、Grid 轨道、sticky 或所有复杂 containing-block 组合已经实现。
36. 2026-08-03 next113/TEST76 接通动态 `:hover`：core 保存 hover DOM 元素并在下一次 libcss 选择时匹配，WM6 宿主用 `WM_MOUSEMOVE` 加 250ms 定时器轮询离开窗口；`TrackMouseEvent` 等桌面 API 不可依赖。设备日志确认 TEST76 及 TEST13/20/27/43/44/56/58-75 全部 PASS。该批不代表 `:visited/:target/:indeterminate`、专用 MouseEvent 或 JavaScript 已实现。
37. 2026-08-03 next114/TEST77 建立脚本资源 ABI：core 扫描非空外部 `<script src>`，可经 `PCoreResolveUrlFn` 使用宿主 URL 解析，调用 `PCoreFetchFn/PCoreFreeFn`，将成功 body 按 document 生命周期去重缓存，并以 `PCore_GetScriptResourceCount/GetScriptResource` 提供只读枚举。TEST77 覆盖相对/root-relative/absolute URL、重复引用、第二次 cache-only 扫描和 inline script 不执行；ARMV4I 增量构建和设备 `TESTBENCH PASS` 均已确认。该批不解释 `type`、不执行 JS，也不接入 TEST13 的网络事务。
38. 2026-08-04 next115 提交普通非替换 float 候选，但设备 TEST79 得到零宽 inline probe，TEST13 截图出现导航扁平化；该候选否决，不得作为设备基线。
39. 2026-08-04 next116 收窄为显式 block-level float，仍产生真实 TEST13 导航/正文排版回归，设备 TEST79 最终失败；代码、配置和 ENGINE 接入已撤回，next114 恢复为设备基线，Float 方向暂挂。
40. 2026-08-04 next118 接入独立 `positron_script.dll`：复用仓库内 NetSurf Duktape 2.7.0 单文件源，新增稳定 C ABI、DLL 自有堆/字符串所有权、持久上下文、预算、错误恢复和内存/执行计数；TEST80 不初始化 `positron_core`，只验证外部程序调用边界。VS2008 ARMV4I Debug 增量构建和设备 TEST80 已通过；timeout/source-size/recovery 边界与浏览器 inline/external JavaScript、DOM/window/fetch binding 仍关闭或待后续批次。
41. 2026-08-04 next119 为独立脚本 DLL 增加 TEST81：用短预算验证无限循环可中止、64 KiB 源码长度上限拒绝和上下文恢复。ARMV4I Debug 增量构建、staging 与设备日志均已通过；该批不添加完整内存配额，也不接入浏览器 JS。
42. 2026-08-05 next120 为独立脚本 DLL 增加 `PScript_CreateEx`、512 KiB runtime heap limit、peak memory telemetry 和 TEST82；ARMV4I Debug 增量构建、staging 与设备日志均已通过，浏览器 JS 仍关闭。
43. 2026-08-05 next121 为独立脚本 DLL 增加 CommonJS 风格模块 ABI：按名一次执行、缓存 exports、`require()` 读取已加载模块，失败删除半成品，`PScript_ClearModules` 显式清空。TEST83 已加入默认配置；ARMV4I Debug/Release 构建、staging 与设备日志均已通过，next121 提升为当前设备基线。

44. 2026-08-08 next157 在 next156 的 BMP WM_CHAR JSON/UTF-8 桥上增加代理对合并：每个
    EDIT/SELECT 记录自己的 high-surrogate，匹配 low-surrogate 后以一个 Unicode 标量
    派发 `keypress`；EDIT 的 `beforeinput.data` 保留完整 UTF-16 data，未配对输入回退
    原生窗口过程。C89、仓库审计、VS2008 ARMV4I Debug 增量构建和 staging 已通过，
    但 `C:\WMShare\Positron-next157\test_host.log` 的 TEST122 失败；next156 保持基线。
45. 2026-08-08 next158 只为 TEST122 增加 result 文本长度/值诊断；设备日志确认 WM
    代理对合并和标量代码正确，失败来自 Duktape 把四字节 UTF-8 暴露为长度 1 字符。
46. 2026-08-08 next159 将事件 JSON 的合法 non-BMP UTF-8 改写为两个 `\uXXXX`，保持
    BMP/ASCII 与标量代码不变。设备实际序列正确；TEST122 失败来自 target 监听器
    oracle 错误地提前期望取消状态，不是代理对桥失败。
47. 2026-08-08 next160 只修正 TEST122 的监听器顺序断言：先注册的 target 记录器看到
    `defaultPrevented=false`，后注册的取消器执行后，父级 bubble 记录器看到 `true`。
    C89、审计、ARMV4I 增量构建与 staging 已通过；`screen=640x480 dpi=192` 设备日志随后
    通过 TEST13/20/27/43/44/56/58-77/80-122 并记录 `TESTBENCH PASS`，next160 升为基线。
48. 2026-08-08 next161 候选接入 WM6 原生 EDIT composition 消息和 TEST123；默认
    `javascript=0` 与 TEST13 不变。自动探针只覆盖消息入口和共享数据发射器，设备 PASS 后
    仍须人工使用真实 SIP/IME 验证预编辑、提交和候选窗口路径。

## 开发纪律

- 面向用户回复使用简体中文。
- 默认先查环境，再改代码。WMDC、僵尸 `test_host`、共享目录、旧二进制非常容易造成假故障。
- 构建只允许通过 `scripts\build.bat` / `scripts\stage.bat` 的 WM6 ARMV4I 配置。禁止调用 `VC\bin\dumpbin.exe`、桌面 `link.exe` 或其他 x86 VC 工具；VS2008 `dumpbin.exe` 会内部启动桌面 `link.exe /dump` 并因缺少 `mspdb80.dll` 弹系统错误。PE section 检查使用 PowerShell/.NET 直接读文件。
- 改 TEST 时必须同步所有 MessageBox 文案、分组范围、最终 summary。
- 改 vendored NetSurf 源码时保持最小差异，C89 化要谨慎；先运行 `python scripts/test_c89ize.py`，再对目标源运行脚本并要求显式 `0 change(s)` 或审阅每项改写。`c89ize.py` 仍不能处理所有 designated initializers / static aggregate initializers。
- 如果新接入 NetSurf content-handler `.c` 后从 `html/private.h` 爆出 `dom_document` / `dom_node` / `bool` 之类连锁语法错，优先检查该 `.c` 的 include 区是否对齐 `layout.c` / `redraw.c`，不要先改 `private.h` 或让 `c89ize.py` 硬处理。
- 不要引入 IE Mobile ActiveX 作为渲染层；渲染层方向是 OSS browser kernel port。

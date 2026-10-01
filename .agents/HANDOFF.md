# 当前交接

本文件只保存当前事实、候选、风险和唯一下一步。稳定边界见 [架构](../docs/ARCHITECTURE.md)、
[能力矩阵](../docs/CAPABILITIES.md) 和 [测试合同](../docs/TESTING.md)；旧批次由 Git 与本地证据保存。

## 使命与当前目标

Positron 为 WM6 / CE 5.2 ARMV4I 提供九个资源有界、稳定 C ABI 的公共 DLL 及独立消费者
positron.exe。产品语义属于 DLL，test_host 只拥有 fixture、平台接线和断言。

用户已确认 WinWorld 菜单能正常展开/收起，SVG 与按钮作者外观正常。不要恢复旧的“只有 Button
文本”或“SVG 仍损坏”假设。当前授权目标是性能：用户报告展开约 10 秒、收起约 6 秒，期间 UI
无响应。新性能候选已有真实页面自动几何与分段计时；用户现在确认展开/收起均不超过约 2 秒，
期间地址栏和菜单可响应。实际点按性能门已通过；Release 应用的额外
性能对照尚未运行，不把 Debug 体验扩大为所有真机或配置的保证。

## 当前代码与所有权

当前分支 main，接管时的产品基线为 cb051cd8，现 HEAD 已由并发 CAB 工作前进至 c6b4819b。
关系 callback 可选 NULL 输出修复与按钮作者内容保留
继续有效。本会话的未提交变化尚未推送：

- libdom element.c：替换 class attribute 时同步更新 CSS class-token cache，分配失败保持旧值。
  这是菜单 class 已变而 CSS 仍将其隐藏的根因；不能仅凭 ARIA/class 判断菜单可见。
- Core pcore_select.c：外部 CSS bytes 继续受 32 项、每项 256 KiB、总计 512 KiB 预算约束。
  顶层、无 import 的完整解析结果可按 URL/bytes/resolver 身份复用，libcss 报告的累计大小最多
  1 MiB，随文档释放；callback 身份变化重新解析一次后可替换旧缓存，超预算与 import 路径
  保持逐次解析。必须覆盖候选 request→提交页 NULL pw 的真实切换，不能只验证固定上下文。
- 应用 Debug：DOM getter 明细默认关闭，可显式编译 APP_DEBUG_DOM_TRACE=1 恢复；有界日志句柄
  按 session 复用，退出关闭，仍限制 128 KiB。保留 mutation、脚本结果与低频 timing，不改变 Release。
- TEST24：检查缓存命中不重复解析 URL、媒体重选、class mutation 与 resolver 身份变化。
  TEST1327：增加 expand/hide/re-expand 的最终布局高度断言，避免旧 class-only 假阳性。
- TEST45：fixture 的 css_import_test_ctx 不再传给要求 navigation request 的 wm_combine_url；
  使用只调用公共 HTTP resolver 的 fixture 适配，产品导航接线没有改动。
- Debug 捕获：先保存旧日志，精确进程清理必须等 helper 的成功摘要，不再仅等 500 ms；
  session 长持有日志句柄使原固定等待暴露 device=32 共享冲突，失败未启动新包。

工作区同时出现不属于本轮的应用页面/菜单扩展，包括 app_internal_pages.c/.h、双语 about/
newtab/history/downloads/settings 页面及工程、资源、i18n、host 和 main.c 改动。不得覆盖或
整文件混入性能提交；main.c 已有重叠。本轮只选取自己的计时/退出日志 hunks，其他应用改动和
暂存内容原样保留；提交状态以 Git 为准。

## 证据与当前候选状态

### 功能与性能线索

人工应用包：tmp/device-runs/manual-button-20261001-224624/，PID 4144740058，包含 class cache 修复。
用户确认正常展开/收起，但 6–10 秒阻塞不可接受。该包已被后续设备门清理，不应让用户继续找旧窗口。

该包日志中 style 约 1232–1383 guest ms，layout 约 36–61 guest ms；高频 Debug DOM 日志会同步
打开/写入/关闭文件，日志已达到 128 KiB 上限，后续人工操作记录不完整。不能把这些数字直接换算为
用户墙钟或全部归咎于 CSS；Release 对照仍未完成。

最终 Debug 隔离包 tmp/device-runs/debug-capture-20261001-234236/ 的 automation-positron-debug.log
已完整回收。自动使用生产 jQuery/Bootstrap 展开→收起→再展开，三次终态 height=404/0/404、
ok=true；相应 style=8/7/9、layout=3/3/5、controls-refresh=13/15/16、paint=1/1/2 guest ms。
没有提高预算、关闭动画或站点特判；与旧包计时相比收益明确，但仍不是用户墙钟/输入响应证据。
早期低计时尚未覆盖 resolver 切换，另一个自动探针未完整结束；不得引用它们当三阶段通过。

同一已验证包已保存自动日志、确认精确清理成功后重新以仅 --url 启动普通模式，当前 PID
4134931822，远端为 \Storage Card\Temp\Positron-device-gate\debug-capture-20261001-234236。
不再自动点按或改写页面；用户已确认真实展开/收起不超过约 2 秒，地址栏/菜单可以响应。
同 PID 日志证明确有多次 pointer→native-button→class/ARIA→timer→重排，页面 extent 3270/4078
交替，style 约 79–103 guest ms；机器负载不同，不能把自动探针的 7–9 ms 当作普通点按固定耗时。
新日志在该本地目录
positron-debug.log，旧自动日志另存，后续应只 PullOnly，不无故重新部署或结束用户验收进程。

### 崩溃取证与修正后的自动门

用户新截图 tmp/QQ20261001-230409.png 显示 test_host 错误报告，模块偏移 0x1e244。
失败候选 tmp/device-runs/20261001-230333-css-parse-cache-perf/ 日志止于 TEST39 通过后，随后
TEST45 路径中断。源码确认 fixture context 与导航 callback 类型不匹配；未把截图偏移伪称为已解析
的 PDB 地址。失败门已保留，不是性能或设备通过证据。缓存还明确限制为顶层无 import 路径。

修正后的 tmp/device-runs/20261001-230944-css-cache-import-callback-fixed/ 为 24,45,999 全部通过。
相邻门 tmp/device-runs/20261001-231300-css-cache-adjacent-final/ 为
24,39,45,1315,1318,1325-1329,999 的 11/11：唯一 TESTBENCH PASS、零 ERROR/FAIL、匹配 Core
路径、完整日志回收、crash_check=PASS、无新增 dump；远端已在回收后清理。设备为 480x640、DPI192，
目标卷约 44 GB、内部约 23 MB，双空间预检通过。残留错误报告窗口需要用户点击 Done，不操作系统 GUI。

最后的 resolver 切换修正由 tmp/device-runs/20261001-234106-css-cache-resolver-handoff-final/
覆盖 24,45,1327,999 的 4/4：匹配 Core、唯一 PASS、零 ERROR/FAIL、crash_check=PASS、完整日志
回收后清理。不能用此前 11 项门声称后续源码全量已经重跑。

### 构建与审计

C89 与最新仓库审计通过。并发应用工程曾引用 12 个未跟踪文件使审计失败，随后另一批工作已将
这些文件暂存，审计恢复；本轮没有放宽门，也不把它们纳入性能提交。
最终缓存修正的正式 Debug/Release build 均通过，Core/test_host 保留已有 libcss 转换警告。
此前 VS2008 自身异常和 CabWiz 失败不是当前阻塞。失败 SafeMode 构建实验已撤回，不增加默认路径。
本轮未发布 nightly。隔离应用证据不用于声称并发页面改动通过；性能提交需按路径和 hunks 隔离。

## 有效边界与设备纪律

HTTP URL-aware/final URL、Core READY/PENDING/TERMINAL_FAIL、CSS data URI 与 Image rgba/linecap
继续有效。bootstrap-multiselect SyntaxError(line 912)、module/Shadow DOM、横向滚动条暂缓项，以及
SIP/IME、旋转、OEM 输入等门见 [限制](KNOWN_LIMITATIONS.md)，不因按钮通过而一并写成完成。
TEST232/263/1310 已有用户验收，不无故重跑；TEST262/264/293 保留独立边界。

WMDC 连接由用户在 GUI 手动完成，只使用当前唯一目标。外置卡 Temp 优先、内置 Temp 回退，
部署前检查目标卷与内部对象存储，日志完整回收后才清理旧目录。显式 ForceTerminatePositron 只结束
精确 Positron/test_host-run-* 目标，不杀 WMDC 或 VS GUI。新部署使用新目录，不覆盖诊断包。
仅在用户告知有新截图时查询 tmp。当前截图已按该规则读取，旧截图不自动代表新运行。

## 路线图复核与唯一下一步

ROADMAP 已复核：已确认的触摸展开/收起和作者图标退出当前修复目标，交互性能成为授权纵切。
稳定 Core 缓存预算写入组件 README；不能把自动正确性门替代耗时与 UI 响应证据。

唯一下一步：完成本轮独立提交并核对推送，保留并发应用改动；后续若真实低资源设备仍有卡顿，
按同一页面/配置的新计时选择下一项瓶颈，可补 Release 应用对照。当前普通点按证据已取得，
不要求用户无故重做。
若仍有长阻塞，继续按测量优化正确 owner；不关闭动画、提高脚本预算或加入站点特判来掩盖。

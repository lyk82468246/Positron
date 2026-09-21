# TEST232/TEST263 回归审查

## 结论

这次审查发现并修正了一个位于参考宿主事件接线中的高风险时序问题：`TEST232` 的文件
选择 `input` 监听器可能先改变页面文字，使 Core 的 retained layout 失效。旧实现把重排
版直接放在 Browser 的 `input` 回调内部；这会在文件事务尚未返回时重建 box tree，并可能
让后续 `change` 或下一次控件命中落到过期状态。当前实现只记录一个有界的待重排标记，
等 `input` 回调返回后在 `change` 回调入口执行一次重排，再继续派发 `change`。窗口销毁
时同时清除标记并移除该 HWND 上尚未消费的交互/picker 消息。

这不是公共 DLL API 变更。文件选择器、DOM 事件和 FormData metadata 仍由 Browser/Core
提供；`test_host` 只负责 HWND、消息循环、picker callback、布局调度和断言。

## 基线与证据

- `c32e2222` 是最近一份明确记录 TEST232 人工通过的基线：选择文件后显示 filename，
  事件顺序为 `input|file;change|file;`，再次打开并取消不改变旧状态。
- `73ebaf63` 之后加入了 TEST263 的延迟脚本 picker 路径，但历史记录没有证明
  `TEST232 → TEST263` 在同一个人工窗口生命周期中连续通过；因此不能把 TEST263 的旧结果
  当成这次修复的证据。
- `b5debcec` 引入了在 `input` 回调内部直接重排的路径。本次修复将它移出活动回调，并
  增加 session/`WM_DESTROY` 清理。
- `python scripts/test_c89ize.py` 通过；Debug 正式构建通过。
- `tmp/device-runs/20260920-183530-next222` 的 `231,999` 设备门 2/2 PASS，完整日志、
  外置部署、清理和 crash 检查均通过，崩溃转储增量为 0。TEST231 覆盖了真实窗口句柄下
  的取消、选择、错误和再次取消路径；它验证的是共享事件桥接，不等价于人工通过 TEST232
  或 TEST263。

同批定向检查还暴露了两个独立的旧问题：TEST262 的 disabled programmatic picker 路径
仍产生了不应有的 `click|disabled`，TEST264 的 disabled validation 结果仍异常。它们没有
改变本次 TEST232 修复，也没有被写成 TEST232 的失败证据；需要另行取证和分批处理。

## 追加审查证据与当前边界

对 `b5debcec` 的时序取舍又做了一次反向验证。把重排版推迟到 `input`/`change` 两个
回调完成之后，会使自动 TEST231 的结果只剩 `input|file;`，因为 Core 的坐标命中树已
在监听器 mutation 后失效；该实验已撤回。当前保留“`input` 返回后、`change` 派发前
恢复布局”的顺序，并清理每次选择前后的待重排标志；`231,1068,999` 的设备门记录在
`tmp/device-runs/20260920-194523-test232-lifecycle-final-audit`，三项通过且无新 dump。

连续自动渲染窗口的消息循环在新实现下通过了
`tmp/device-runs/20260920-194247-render-window-sequence-audit`。临时恢复旧
`PostQuitMessage` 实现的对照也通过了 `tmp/device-runs/20260920-194336-render-window-sequence-old-control`，
因此不能把 `WM_QUIT` 单独认定为 TEST232→TEST263 失效的已证实根因；当前改动仍保留
per-window closed flag，避免可复用的 `show_render_window()` 向宿主线程投递退出语义。
同时，session 销毁现在明确清除 host 的 active picker guard，防止窗口在 picker unwind
期间关闭后抑制下一个页面的 `file.click()`。这两项仍需真实 GUI 连续序列确认，未以自动
窗口门替代人工 TEST232/263 结论。

## TEST263 点击失效的后续定位

用户已确认 TEST232 通过，后续人工包移除 232。截图 `tmp/QQ20260920-212821.png` 显示固定 24px checkbox 变得过大，点击仍然退出；仅修改控件尺寸或保护已经排队的 picker 都不足以修复。固定尺寸已撤回。

TEST263 的 file click 监听器会更新 Events/value 文本，Core 随即丢弃 retained layout。宿主随后通过旧坐标调用 `PCore_FileInputAt`，无法找到目标，picker 根本没有排队；同一鼠标消息后续无法识别 checkbox，又进入空白退出路径。当前候选在布局缺失时暂存文件目标 ID 和窗口/document，事件返回后重排并重新解析控件，再进入既有 Browser picker 仲裁。已删除、disabled 或无布局目标不打开 picker；窗口/session 清理释放待处理 ID。无法提交的 checkbox 事务取消，避免下次点击被残留事务阻塞。

TEST263 新增自动分支：使用同一个 HTML 和 listener，向真实渲染窗口发送两次 checkbox 点击，用模拟 picker callback 完成选择和取消，并断言窗口存活、请求排队和完整事件序列。`tmp/device-runs/20260920-213339-test263-pointer-repro` 的 `263,999` 通过且无新 crash dump；此证据覆盖鼠标消息到脚本和 picker 接线，不代表系统对话框人工通过。路线图已复核：仍是当前交互回归修复，没有新增产品 API 或 next 候选。

## 人工验收包

新的手动包位于：

`C:\WMShare\Positron-manual-test263-deferred-id`

其中 `test_host.ini` 为 `auto=0`、`javascript=1`、`tests=263,1310,999`。旧的 checkbox-fix/fix2 包没有解决本次问题，不再用于验收。

1. 在已由 GUI 连接的 WM6 设备上启动包内 `test_host.exe`。
2. TEST263 点击“Script click file input”控件，而不是直接点击 file 控件。选择文件后，
   事件应以 `click|file;input|file;change|file;` 开头；再次触发并取消时只允许追加一个
   `click|file;`，不得新增 `input`/`change`。
3. TEST1310 选择包内的 `test_host.ini`，检查页面显示的 filename/type/size/text/query
   metadata，然后退出。

如果 TEST263 仍失败，应保留完整 `test_host.log`、失败阶段和截图，不要用自动门通过替代
人工结论。只有获得该序列的真实设备结果后，才能决定是否进入下一条公共能力候选。

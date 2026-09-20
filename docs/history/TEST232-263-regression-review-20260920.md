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

## 人工验收包

新的手动包位于：

`C:\WMShare\Positron-manual-next232-lifecycle-fix`

其中 `test_host.ini` 为 `auto=0`、`javascript=1`、`tests=232,263,1310,999`。必须使用
这个新目录，不要复用旧的 `next232-fix` 或 `next1310` 包。

1. 在已由 GUI 连接的 WM6 设备上启动包内 `test_host.exe`。
2. TEST232 选择一个小文件并确认。页面应显示文件名，事件必须恰好为
   `input|file;change|file;`。再次打开同一控件并取消，文件名和事件文本都不得改变；
   然后按页面说明退出。
3. TEST263 点击“Script click file input”控件，而不是直接点击 file 控件。选择文件后，
   事件应以 `click|file;input|file;change|file;` 开头；再次触发并取消时只允许追加一个
   `click|file;`，不得新增 `input`/`change`。
4. TEST1310 选择包内的 `test_host.ini`，检查页面显示的 filename/type/size/text/query
   metadata，然后退出。

如果 TEST232 仍失败，应保留完整 `test_host.log`、失败阶段和截图，不要用自动门通过替代
人工结论。只有获得该序列的真实设备结果后，才能决定是否进入下一条公共能力候选。

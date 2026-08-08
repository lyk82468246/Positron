# 失败实验与暂挂方向索引

更新时间：2026-08-08

这份索引集中记录曾经导致真实页面回归、设备断言失败、环境混包误报，或因风险暂时停止的工作。它不是“所有已修复 bug”的替代品：完整实现边界仍见 `KNOWN_LIMITATIONS.md`，逐版本交接见 `HANDOFF.md`，按时间排列的开发记录见 `ROADMAP.md`。

## 状态约定

- **已撤回**：代码、默认配置或主线行为已经删除；不能直接从旧包或旧提交拷贝回来。
- **暂挂**：方向仍有价值，但当前方案不再继续；重启前必须满足本条记录的门槛。
- **已替代**：实验失败或不完整，但后续方案已经解决原问题；保留记录用于防止重复踩坑。
- **环境误报**：功能代码不一定错误，失败来自旧 DLL、共享目录或工具链混用；仍要保留流程护栏。
- **开放限制**：不是一次失败分支，而是当前仍可观察、尚未达到产品完成条件的问题。

## 总原则

1. 真实 TEST13 的深层导航、旋转和人工截图优先于离线几何断言；自动 `OK` 不能覆盖明显视觉回归。
2. 恢复工作必须在独立 staging 目录完成，使用同一套 ARMV4I EXE/DLL；不得从失败包直接拷贝单个文件。
3. 重新启动暂挂方向时，一次只改变一个变量，并同时保留原有冻结回归；失败包只用于取证，不作为新的基线。

## 失败与暂挂清单

| 范围 | 状态 | 发生的问题 | 当前决定与重启门槛 |
|---|---|---|---|
| next38-next43：stylesheet metadata、`<base>`/URL 别名、redirect final origin、WinInet/TLS deadline 与加载预算 | 暂挂 | next37 之后 TEST13 无法完成，无法安全归因到单一 HTTP 或样式改动。 | 整批保存在远端 `codex/post-next37-experiments`，不得直接合并；重启时每个方向单独分支，并完整通过 `Start -> Open example -> Learn more`、旋转、滚动和失败回滚。详见 [`ROLLBACK_NEXT37.md`](./ROLLBACK_NEXT37.md)。 |
| next54：固定高度 overflow 的滚动条预留/二次 layout | 已替代 | TEST42 读到 `used=0/0/0`，右箭头还偏移；二次布局改变了已验收几何。 | next55 收窄为只对 auto-height 路径预留空间并修正箭头坐标，已由设备验收。不要恢复 next54 的全局二次预留。 |
| next60：counter/list marker 首次 staging | 环境误报，已替代 | 设备看到 `found=4 fetched=2`，原因是新 `test_host.exe` 与旧 `positron_core.dll` 混用，不是 marker 逻辑失败。 | `stage.bat` 现在先做同配置增量 Build，失败不复制；新实验必须核对 EXE/DLL 同包。 |
| next69-next72：百分比 table-row 与 inline style 首轮 | 环境误报/已替代 | TEST56 在多个共享目录包之间结果不一致，TEST57 又暴露 inline `style=` 未进入正式选择。 | 先隔离包排除 WM 全局 DLL 复用，再由 next73-75 接入 inline sheet、修复 universal ancestor 匹配；不要放宽 TEST56/57 断言掩盖混包。 |
| next155/TEST121：BMP WM_CHAR 首次设备包 | 已替代 | TEST13 与 TEST120 之前的回归通过，但 TEST121 失败；事件回调的旧安全过滤器把合法 UTF-8 高位字节清空，导致 `key`/`beforeinput.data` 丢失。 | next156 改用 JSON 字符串转义并保留 Unicode 断言；重新运行同一批完整回归后才能成为设备基线，不得使用 next155 失败包。 |
| next157-159 TEST122：UTF-16 代理对输入桥首次设备包 | 已定位，待 next160 替代 | next158 证明 WM 合并和标量代码正确；next159 又恢复两个 UTF-16 code unit，但测试 oracle 把先注册的 SELECT target 记录器错误地预期为已取消。 | next160 只修正事件顺序 oracle：target `false`、bubble `true`。完整日志 PASS 前 next156 保持设备基线；不得回退 non-BMP JSON 代理项修复。 |
| next140：TEST62 固定 96-DPI 离线控件探针 | 已替代 | `screen=480x640 dpi=192` 下，TEST62 的 checkbox/radio probe 返回 `36x36`；next140 把 probe 固定到 96 DPI，虽能绕过断言却违反动态 DPI 原则。 | next141 保留实际设备 DPI，并将原本 96-DPI 的 `14..24px` 几何范围按 `dpi/96` 等比换算；不得恢复固定 DPI。 |
| next78：layout 后递归 `scrollbar_set(...,0)` | 已撤回，高风险 | 横屏 TEST13 从单个 `Domain` 异常扩大为全部表格单元格异常，TEST56 失败，并触发系统级 `test_host.exe` 异常。 | 实现、诊断 API 和扩展 TEST59 已删除；旧包为 `C:\WMShare\Positron-next78-FAILED-DO-NOT-USE`。禁止恢复“布局末尾统一清零 scrollbar 回调”的思路。详见 `KNOWN_LIMITATIONS.md` 和 `DEBUGGING.md`。 |
| next105/next107-next108：required/reset 与空控件 geometry 首轮 | 已替代 | libdom 首次写值误记为 `defaultValue`，以及无 CSS 尺寸 text input 几何为 0，造成 TEST72/73 假失败。 | next106/next109 修复了默认值冻结和测试夹具前提；仍不宣称浏览器默认 intrinsic size 已完成。 |
| loading 条早期绘制方案：父窗口 `WM_PAINT`/`ScrollWindowEx` 复制残影、独立 `STATIC` 不可见 | 已替代/部分暂挂 | 滚动时进度条残影、轻微卡顿；`STATIC` 子窗口在 WM6 设备上不可见。 | 已改为 WM6 Common Controls `PROGRESS_CLASS`。当前真实百分比只对单响应有长度时成立，整页聚合进度与首屏 layout 卡顿暂后置，不阻塞资源事务主线。 |
| next115/next116：普通与显式 block-level float 构盒 | 已撤回，方向暂挂 | next115 的 inline probe 为零宽且 TEST13 导航扁平化；next116 收窄后仍出现导航/正文排版回归，设备 TEST79 最终失败。 | 已恢复 next114，删除 float 构盒、TEST79 和默认配置。重新启动前必须先完成完整 box construction/normalisation，并通过 TEST79、TEST13 深链和旋转人工门禁。详见 `ROADMAP.md` 第 7/8 节。 |

## 当前开放的视觉布局限制

next117 的人工复核表明主链路基本正常，剩余问题主要是局部版式：某些容器/背景框的几何尺寸偏小，但其中的文本量或换行高度较多，因而出现框与内容比例不协调。这不应通过修改断言解决，也暂时不能归因到单一 CSS 特性。

后续完成条件：收集至少三个可重复页面/测试例，记录 viewport、DPI、元素 computed style 和实际 box geometry，先定位是 intrinsic size、padding/margin、字体测量还是 normalisation，再用一个针对性回归测试和竖横屏截图修复。修复前保持 next114/next117 Browse 基线，不重新打开 float 方向。

## 复查入口

- 当前限制与“不代表什么”：[`KNOWN_LIMITATIONS.md`](./KNOWN_LIMITATIONS.md)
- 当前基线、交接和下一步：[`HANDOFF.md`](./HANDOFF.md)
- 按时间排列的路线与失败记录：[`ROADMAP.md`](./ROADMAP.md)
- next37 大回退的独立说明：[`ROLLBACK_NEXT37.md`](./ROLLBACK_NEXT37.md)

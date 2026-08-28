# 当前交接

本文件只描述当前产品基线、最近有效证据、未决风险和唯一下一步。逐批实现过程由 Git 历史保存，
历史事故见 `docs/history/`，未来方向见 `ROADMAP.md`。

## 项目使命

Positron 为 Windows Mobile 6 / Windows CE 5.2 ARMV4I 提供模块化 TLS、JSON、HTTP、图像、
脚本、渲染与浏览器会话 DLL。公共边界保持 C ABI、UTF-8、opaque handle 和显式所有权；
`test_host.exe` 只是回归宿主与示例消费者。

## 当前 Git 与工作区

- 分支：`main`，接管时与 `origin/main` 同步。
- 当前产品代码基线：`ed87f732`（next653 natural keyboard focus traversal）。
- 本次中断任务仅重整文档和文档审计，不改变产品 C/ABI、工程配置或 tracked 测试选择。
- `tmp/` 保存本地设备日志和截图，不跟踪。

## 当前中期里程碑

在保持 VS2008/WM6 约束的前提下，把已经形成的 Core、Browser 和平台宿主能力整合为可由真实
页面驱动的有界网页运行时。重点是完成用户可感知的纵向能力、把通用语义留在公共 DLL，并以
小型真实页面/交互语料库防止只增加孤立 API。

## 当前短期目标

建立一组小而固定的真实页面与交互兼容性语料，依据实际缺口选择下一个高价值纵切。不得只为
增加测试编号而拆分能力，也不得把产品语义继续堆在 `test_host`。

## 已验证产品事实

### 公共边界

- 顶层公共 DLL 为 TLS、JSON、HTTP、image、script、core 和 browser。
- NetSurf/libcss/libdom/hubbub、Expat、libsvgtiny、libjpeg 等移植工程是内部实现依赖。
- 独立脚本和浏览器脚本共用 Duktape；浏览器 JavaScript tracked 默认仍为关闭。
- 通用 URL、history、DOM、Event、表单、图像和脚本 session 语义位于对应公共 DLL；宿主保留
  WM 窗口、消息、控件、SIP/IME、picker、导航调度和资源 I/O。

### 当前网页能力

- HTML/CSS/DOM、整树 style、NetSurf layout/redraw、GDI 绘制与资源缓存已形成正式 Core 路径。
- 常用 block/inline/flex/table、图片/SVG、背景、列表、有限定位、表单控件、验证、提交与 reset
  已有设备回归；这不代表完整 CSS/HTML。
- Browser 层提供有界 history、same-document state、script session、DOM/Event/input/navigation
  callbacks，以及 timer/microtask/lifecycle 和 native 控件事务协调。
- 页面导航保留旧页到候选文档成功提交；主文档和资源网络阶段与 UI 文档操作分离。
- 深层 DOM 资源准备使用单个事务级 heap scratch，避免大批固定栈缓冲耗尽 WM6 线程栈。
- `<details>/<summary>` 支持 click 与 Enter/Space 激活、取消和 DOM 状态同步。
- 支持的链接、summary、native EDIT/SELECT/button/file 等目标按自然 DOM 顺序响应 Tab/Shift+Tab，
  并同步焦点事件、原生焦点和滚动可见性。自定义 `tabindex` 排序仍未实现。

### 当前测试入口

- `TEST_MAX_NUMBER`：1101。
- tracked `test_host/test_host.ini`：`auto=1`、`javascript=0`，选择
  `13,20,27,56,58,62,64-67,73,75,999`。
- tracked INI 是窄 smoke，不是全量目录；nightly 打包脚本从源码 dispatch 动态生成全量自动清单。
- 设备连接必须先由用户在 WMDC/Device Emulator GUI 手动完成；RAPI gate 只使用当前唯一会话。

## 最新有效设备证据

当前最新产品门为 next653：

- 本地目录：`tmp/device-runs/20260828-224321-next653-sequential-focus-r6/`；
- 选择：TEST1095–1101 与 TEST999；
- 结果：8/8，通过；唯一 `TESTBENCH PASS`，零 `ERROR`/`FAIL`；
- 设备：240x320；真实 Browse 路由哨兵判定为通过。

该门验证 disclosure keyboard activation、自然顺序 Tab/Shift+Tab、原生与非原生目标切换、焦点
事件、disabled/hidden 过滤、滚动可见性和退出提示音。它是定向门，不是全量回归。

最近一次完整编号范围基线仍是 next255，早于当前多批能力；此后主要使用定向门和相邻回归。
因此，累积风险达到路线图条件时必须安排新的全量 checkpoint，不能把多个窄门宣称为全量覆盖。

## 当前人工验收状态

以下路径已有过真实设备确认，但后续触及相邻基础设施时仍需重新评估：

- example.com → IANA 的容器边距、深层导航和旧页保留；
- SIP 候选词整词提交；
- bitmap/SVG、表格、列表和常见布局的可见结果；
- native EDIT/SELECT、真实 file picker、旋转和 DPI 路径。

允许累计的人工风险包括低风险视觉、触摸、SIP/IME、旋转、picker 和失败网络观察。崩溃、数据
损坏、严重布局破坏或核心交互阻塞必须立即人工复核。

## 当前未决风险

- 真实页面兼容性仍缺少固定、小型、可重复的 corpus；TEST13 只是单一网络哨兵。
- 自定义 `tabindex`、`contenteditable`、完整 dialog/modal/backdrop/lifecycle 尚未实现。
- float、复杂 table/position、现代 CSS 与任意畸形页面仍有明显边界。
- 浏览器 JavaScript 是有限组合，不具备完整 DOM/Web API 或现代浏览器安全沙箱。
- 多窗口、持久 history、完整下载/外部协议策略仍属于宿主或未实现范围。
- mbed TLS 2.16.12 等依赖为旧平台兼容 pin，发布前必须审查当前安全风险。
- OEM SIP/IME、系统 picker、视觉和旋转不能仅凭 synthetic 自动测试保证。

完整列表见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 唯一下一步

先建立兼容性 corpus 的第一个固定场景，并从该场景选择一个能贯穿 Core、Browser、宿主和自动
设备门的高价值缺口；不要预先假定具体 API 或测试编号。

优先场景应同时满足：

1. 可在仓库内离线固定主要 HTML/CSS/交互，避免网络内容漂移；
2. 对应一个真实页面或用户操作，而不是孤立 getter/setter；
3. 暴露当前限制中的一个核心流程缺口；
4. 通用语义进入公共 DLL，宿主只保留平台接线；
5. 可以自动断言主要结果，人工部分只保留无法机器判断的视觉/输入风险。

## 下一步完成标准

- corpus fixture、预期行为和失败分类写入测试源码或专用 fixture，不写入稳定文档流水账；
- 对应纵切在公共 DLL 和宿主之间职责清楚，无新增 test-host-only 产品语义；
- `python scripts/test_c89ize.py` 与 `python scripts/audit_repo.py` 通过；
- VS2008 ARMV4I 正式构建通过，staging 无混包；
- 定向设备门及直接相邻回归全部通过，日志唯一 PASS、零 ERROR/FAIL；
- 需要的人工风险已完成或明确进入可累计清单；
- handoff 覆盖更新为新的当前快照，ROADMAP 只保留仍未完成工作。

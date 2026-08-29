# 当前交接

本文件只描述当前产品基线、最近有效证据、未决风险和唯一下一步。逐批实现过程由 Git 历史保存，历史事故见 `docs/history/`，未来方向见 `ROADMAP.md`。

## 项目使命

Positron 为 Windows Mobile 6 / Windows CE 5.2 ARMV4I 提供模块化 TLS、JSON、HTTP、图像、脚本、渲染与浏览器会话 DLL。公共边界保持 C ABI、UTF-8、opaque handle 和显式所有权；`test_host.exe` 只是回归宿主与示例消费者。

## 当前 Git 与工作区

- 分支：`main`，接管时与 `origin/main` 同步。
- 当前产品代码基线：`c5c9d126`（next662，单元素 `contenteditable` 的 WM/native EDIT 接线）；上一已推送基线为 `9eebbef6`。
- Core 现在报告稳定的有效表单方法常量，并为显式 submitter 或单行输入隐式提交解析最近祖先 dialog id 与 submitter value。Browser 提供按 id 直接执行 `dialog.close(value)` 的会话边界；参考宿主只在 validation 和可取消 `submit` 均允许后调用它，不生成网络导航，也不错误派发 `cancel`。Core 还提供 `PCore_PaintDocumentWithModal`：普通文档绘制后覆盖有界实体色 backdrop，并按 Browser 的活动 id 重绘已打开的 dialog；next658 的 backdrop 指针策略和此前的 modal 焦点/Escape 边界保持不变。
- `tmp/` 保存本地设备日志和截图，不跟踪。

## 当前中期里程碑

在保持 VS2008/WM6 约束的前提下，把已经形成的 Core、Browser 和平台宿主能力整合为可由真实页面驱动的有界网页运行时。重点是完成用户可感知的纵向能力、把通用语义留在公共 DLL，并以小型真实页面/交互语料库防止只增加孤立 API。

## 当前短期目标

建立一组小而固定的真实页面与交互兼容性语料，依据实际缺口选择下一个高价值纵切。不得只为增加测试编号而拆分能力，也不得把产品语义继续堆在 `test_host`。

## 已验证产品事实

### 公共边界

- 顶层公共 DLL 为 TLS、JSON、HTTP、image、script、core 和 browser。
- NetSurf/libcss/libdom/hubbub、Expat、libsvgtiny、libjpeg 等移植工程是内部实现依赖。
- 独立脚本和浏览器脚本共用 Duktape；浏览器 JavaScript tracked 默认仍为关闭。
- 通用 URL、history、DOM、Event、表单、图像和脚本 session 语义位于对应公共 DLL；宿主保留 WM 窗口、消息、控件、SIP/IME、picker、导航调度和资源 I/O。

### 当前网页能力

- HTML/CSS/DOM、整树 style、NetSurf layout/redraw、GDI 绘制与资源缓存已形成正式 Core 路径。
- 常用 block/inline/flex/table、图片/SVG、背景、列表、有限定位、表单控件、验证、提交与 reset 已有设备回归；这不代表完整 CSS/HTML。
- Browser 层提供有界 history、same-document state、script session、DOM/Event/input/navigation callbacks，以及 timer/microtask/lifecycle 和 native 控件事务协调。
- 页面导航保留旧页到候选文档成功提交；主文档和资源网络阶段与 UI 文档操作分离。
- 深层 DOM 资源准备使用单个事务级 heap scratch，避免大批固定栈缓冲耗尽 WM6 线程栈。
- `<details>/<summary>` 支持 click 与 Enter/Space 激活、取消和 DOM 状态同步。
- 支持的链接、summary、native EDIT/SELECT/button/file 等目标，以及带有效非负 `tabindex` 的普通布局元素按有界顺序响应 Tab/Shift+Tab：正值升序、同值 DOM 稳定排序，随后零/缺省组；负值、disabled/hidden/stale 目标和 file picker 仍被排除。Browser 报告活动 modal id 后，宿主可用 Core 的 scoped snapshot 将顺序焦点限制在 dialog 子树；宿主仍同步焦点事件、原生焦点和滚动可见性。
- `<dialog>` 的 show/showModal/close/requestClose、returnValue、cancel/close 事件、活动 modal id 查询、宿主驱动的 Escape 请求桥接、有界 backdrop 指针策略、`method="dialog"` 默认动作和 Core modal paint 已形成契约。显式点击、脚本 `click()` 和单行输入隐式 Enter 都遵循 validation→可取消 submit→直接 close/returnValue；CSS `::backdrop`、透明合成、多个 modal 和跨文档 modal 仍未实现。
- 单元素 `contenteditable` 已形成 Core/Browser/宿主边界：Core 解析祖先继承并限制合法 UTF-8 纯文本 mutation，Browser 暴露 `isContentEditable`/`innerText` 与 typed input 事务，宿主为带 id 且已布局的有效 editing host 创建有界 WM multiline EDIT 代理。允许的 `WM_CHAR` 在默认处理完成后回读最终文本并派发 `input`；取消的 `beforeinput` 不修改 Core，重复/提前 `EN_CHANGE` 不会制造空事件。每页最多 16 个宿主、文本最多 8192 UTF-8 字节；嵌套继承后代不重复创建 host。当前不包含 caret/selection、富文本、designMode、剪贴板或完整 IME 语义。

### 当前测试入口

- `TEST_MAX_NUMBER`：1110。
- tracked `test_host/test_host.ini`：`auto=1`、`javascript=0`，选择 `13,20,27,56,58,62,64-67,73,75,999`。
- tracked INI 是窄 smoke，不是全量目录；nightly 打包脚本从源码 dispatch 动态生成全量自动清单。
- 设备连接必须先由用户在 WMDC/Device Emulator GUI 手动完成；RAPI gate 只使用当前唯一会话。

## 最新有效设备证据

当前最新产品门为 next662：

- 本地目录：`tmp/device-runs/20260829-213019-next662/`；
- 选择：TEST1109、TEST1110 与 TEST999；
- 结果：3/3，通过；唯一 `TESTBENCH PASS`，零 `ERROR`/`FAIL`；
- 设备：640x480，dpi=192；该门使用当前 WMDC GUI 会话并完成了 staging、远端启动、日志回收和退出提示音。

该门验证 Core/Browser 单元素 `contenteditable` 状态与纯文本 mutation、真实 WM multiline EDIT 代理、提前 `EN_CHANGE` 的延迟回读、宿主编排的 `beforeinput` 取消→mutation→`input` 顺序，并重跑 next661 的离线事件契约与退出提示音回归。它是定向门，不是全量回归。

最近一次完整编号范围基线仍是 next255，早于当前多批能力；此后主要使用定向门和相邻回归。因此，累积风险达到路线图条件时必须安排新的全量 checkpoint，不能把多个窄门宣称为全量覆盖。

## 当前人工验收状态

以下路径已有过真实设备确认，但后续触及相邻基础设施时仍需重新评估：

- example.com → IANA 的容器边距、深层导航和旧页保留；
- SIP 候选词整词提交；
- bitmap/SVG、表格、列表和常见布局的可见结果；
- native EDIT/SELECT、真实 file picker、旋转和 DPI 路径。
- 带 `tabindex` 的普通元素的设备焦点矩形、触摸命中和不同 DPI 视觉仍需人工观察；语义顺序已有自动断言。
- `<dialog>` backdrop 的整体色彩、边界、滚动/旋转下的视觉仍属于可累计的人工观察；Core 的绘制顺序和设备门像素契约已有自动断言。
- contenteditable 的真实 caret/selection、SIP/IME 候选词、硬键盘、滚动/旋转和不同 DPI 下的文本视觉仍属于可累计人工风险；离线 1109 覆盖状态/mutation，1110 覆盖 WM EDIT 代理、取消回滚和事件顺序。

允许累计的人工风险包括低风险视觉、触摸、SIP/IME、旋转、picker 和失败网络观察。崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即人工复核。

## 当前未决风险

- 真实页面兼容性仍缺少固定、小型、可重复的 corpus；TEST13 只是单一网络哨兵。
- `<dialog>` 已有已验证的有界脚本生命周期、`method="dialog"` 默认动作、活动 modal id、Escape→`requestClose()` 桥接、宿主顺序 Tab/Shift+Tab 子树范围、有界 backdrop 指针策略和 Core 实体色 modal paint；当前表单桥要求最近祖先 dialog 有非空 id。CSS `::backdrop`、透明合成、多个 modal 和跨文档 modal 生命周期尚未实现，初始焦点、native 窗口视觉和非顺序平台焦点仍由宿主决定。
- `contenteditable` 仅有单元素纯文本状态/mutation 与带 id、已布局 editing host 的有界 WM EDIT 代理；每页最多 16 个 host、文本最多 8192 UTF-8 字节，嵌套继承后代不重复代理。caret/selection、富文本、designMode、剪贴板、完整 IME 组合和动态焦点区域尚未实现。
- float、复杂 table/position、现代 CSS 与任意畸形页面仍有明显边界。
- 浏览器 JavaScript 是有限组合，不具备完整 DOM/Web API 或现代浏览器安全沙箱。
- 多窗口、持久 history、完整下载/外部协议策略仍属于宿主或未实现范围。
- mbed TLS 2.16.12 等依赖为旧平台兼容 pin，发布前必须审查当前安全风险。
- OEM SIP/IME、系统 picker、视觉和旋转不能仅凭 synthetic 自动测试保证。

完整列表见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 唯一下一步

以已验证的离线 `contenteditable` 语料为基础，next663 唯一推进单元素编辑的下一个高价值缺口：优先评估真实 caret/selection 或 IME 组合边界，仍不引入富文本、designMode、跨文档 modal 或无关 CSS 扩张。WM/native EDIT 代理、`beforeinput` 取消和 Core 纯文本 mutation 已由 next662 完成并有设备证据。

优先场景应同时满足：

1. 可在仓库内离线固定主要 HTML/CSS/交互，避免网络内容漂移；
2. 对应一个真实页面或用户操作，而不是孤立 getter/setter；
3. 暴露当前限制中的一个核心流程缺口；
4. 通用语义进入公共 DLL，宿主只保留平台接线；
5. 可以自动断言主要结果，人工部分只保留无法机器判断的视觉/输入风险。

## 下一步完成标准

- 固定 fixture 已自动验证单元素 `contenteditable` 的状态、文本 mutation、事件顺序和失效回滚，不依赖外网；
- next662 已让真实 WM/native 输入只提供坐标、焦点和文本替换接线，沿用 Browser 的 `beforeinput` 事务与 Core mutation；下一批不得把 caret/selection 或 IME 策略复制回 `test_host`；
- `python scripts/test_c89ize.py` 与 `python scripts/audit_repo.py` 通过；
- VS2008 ARMV4I 正式构建通过，staging 无混包；
- 定向设备门及直接相邻回归全部通过，日志唯一 PASS、零 ERROR/FAIL；
- 需要的人工风险已完成或明确进入可累计清单；
- handoff 覆盖更新为新的当前快照，ROADMAP 只保留仍未完成工作。

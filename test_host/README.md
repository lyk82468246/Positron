# `test_host.exe`

`test_host.exe` 是 Positron 公共 DLL 的回归宿主、设备验收程序和组合示例。它不是公共库，也不属于最终应用必须部署的业务核心。

宿主的职责是把 WM6 窗口/消息、native 控件、网络 I/O、设备文件系统和测试 fixture 接到公共 DLL；可复用的 URL、history、DOM、Event、表单、图像或 script-session 语义必须位于相应 DLL。

## 产物与依赖

- 工程：`test_host.vcproj`
- 输出：`bin\<Configuration>\test_host.exe`
- 配置：与 EXE 同目录的 `test_host.ini`
- 自动日志：与 EXE 同目录的 `test_host.log`
- 动态依赖：TLS、JSON、HTTP、Image、Script、Core、Browser DLL
- 平台依赖：`aygshell`、common controls、WinINet 和 WM6 GUI/IME/picker

宿主还为低层移植工程提供直接回归，但外部产品应用不应模仿这种静态库测试链接方式；应用只消费顶层公共 DLL。

## 构建与运行

使用仓库正式入口：

```bat
scripts\build.bat
scripts\stage.bat Debug C:\WMShare\Positron
```

`stage.bat` 会先构建，再把同一配置的 EXE、DLL、字体和 tracked INI 放入隔离目录。不要手工从不同配置或不同时间的输出目录拼包；Windows CE 还可能继续复用旧进程加载的 DLL。

在设备 File Explorer 中运行 staging 目录里的 `test_host.exe`，或在用户已通过 GUI 建立唯一 WMDC 连接后运行自动设备门：

```bat
scripts\device_gate.bat -Candidate local-check
```

详细操作与通过标准见 [`../docs/TESTING.md`](../docs/TESTING.md)。

## 配置

```ini
auto=1
javascript=0
tests=13,20,27,999
```

- `auto=1`：按选择运行、抑制结果对话框、覆盖写日志并自动判门。
- `auto=0`：保留启动确认、说明框、可见窗口和人工操作。
- `javascript=0`：默认不执行网页 classic script。
- `javascript=1`：显式启用实验性的 Browser script session。
- `tests=`：接受编号、范围及源码明确支持的特殊编号。

没有 INI 或 INI 无效时，宿主退回内置分组选择。移走 INI 不是“自动全量”；全量自动清单由 nightly/device tooling 从当前源码 dispatch 生成。

## 测试层次

宿主中的测试大致覆盖：

- 基础 DLL 的 ABI、所有权、错误和真实网络；
- 第三方移植库的解析/链接/设备行为；
- HTML/CSS/DOM、style/layout/paint、图片/SVG 和资源 cache；
- 表单、validation、submission、native 控件和 DOM Event；
- history、navigation、script session、DOM bridge 和平台事务；
- 固定离线 compatibility corpus，把 contenteditable、dialog/form、same-document navigation 和失败回滚组合成可重复的完整流程；
- 真实 Browse、DPI/旋转、SIP/IME、picker 和视觉 fixture。

编号只是 dispatch key，不是功能路线图。测试的准确含义应由 fixture、断言、开始提示和失败文本表达，不在 README 复制逐编号清单。

## 浏览器组合边界

### 页面导航

宿主持有后台网络 worker、loading/取消、候选文档和窗口 swap。旧页保持可绘制，直到新页面完成 parse/resource/style/layout 并可提交。较新的导航可以取代仍在准备的候选：每个候选独占自己的 worker、response 和资源队列，generation 只允许最新候选的进度、完成和提交消息生效；退休候选在 worker 收尾后才释放，退休队列达到固定上限时新导航 fail closed 并保持当前页。URL reference 解析调用 `positron_http.dll`；history 提交调用 `positron_browser.dll`。

DOM、libcss 和 NetSurf document 只在 UI 线程操作。worker 不持有 DOM node、box、computed style 或 HDC。

### Core 与 Browser callbacks

宿主把当前 `PCore` document 包装为 size-tagged callbacks，供 Browser session 查询 DOM、属性、表单、validation、`contenteditable` 状态、文本和可选原生选区。Browser 负责脚本对象、事件顺序、取消与事务状态；宿主只执行允许的 Core mutation、WM 默认动作和导航副作用。

callback 同步且不可重入。候选页面成功提交前，宿主必须在旧 document/session 仍有效时调用 `PBrowser_ScriptSessionDispatchPageTeardown`；它负责一次性的 `visibilitychange`→`pagehide`→`unload` 边界和页面队列清理。随后宿主停止新消息和事务，销毁 native 控件、Browser session 和 Core document，避免 stale token 或借用指针逃逸。失败候选不调用 teardown，旧页状态继续服务。

### Native EDIT/SELECT/button/file

WM subclass 把键盘、focus、composition、selection 和 click 转成 Browser typed transaction。只有 Browser 允许默认动作后，宿主才写入 Core/native 控件，并把提交结果送回 Browser 产生 `input`、`change`、submit/reset 等后续事件。对 contenteditable，宿主从 `PCore_ContentEditableTargetInfo` 枚举带 id 的已布局 editing host，创建最多 16 个 WM multiline EDIT 代理；`WM_CHAR` 默认处理返回后才回读最终文本并调用 Core mutation。宿主实现的 selection callback 把 WM EDIT 的 UTF-16 位置（包括 CRLF）转换为 Browser 使用的逻辑 UTF-16 位置，并只保存原生控件的短暂状态；原生范围确定后调用 `PBrowser_ScriptSessionNotifyContentEditableSelection`，由 Browser 去重并分发一次 `selectionchange`。这样可吸收 WM6 在默认处理期间提前发送的 `EN_CHANGE`，避免把旧值或空值提交为一次 input，也避免宿主经 Core 重复派发选区事件。对 `WM_PASTE`/`WM_COPY`/`WM_CUT`，宿主读取有界 `CF_UNICODETEXT`、规范化 CRLF，并把精确 data 交给 `beforeinput`；`WM_COPY` 的折叠选区保持现有剪贴板不变，允许的 paste/cut 才执行 native default 和 Core/input 提交，格式缺失或超长时 fail closed。WinCE 原生 `WM_CUT` 可能内部重入 `WM_COPY`，宿主只在该外层默认动作期间放行同一 HWND 的重入，不让它绕过自己的外部 copy 规则。键盘/拖选 anchor、Shift 状态和捕获/取消/焦点中断收尾同样只属于宿主平台接线。

系统 picker、文件路径、SIP/IME、HWND、COMBOBOX popup 和真实焦点仍属于宿主。synthetic 消息只能做自动契约，不能替代 OEM 设备人工验收。

### 单元素 `contenteditable`

`test_host` 把 `PBrowserScriptContentEditableCallbacks` 与 `PBrowserScriptContentEditableSelectionCallbacks` 接到当前 Core 文档，并负责在真实输入源中编排 `beforeinput`、允许后的 `PCore_ContentEditableSetTextById` 和 `input`。Browser 维护脚本可见的 `selectionStart`/`selectionEnd`/`selectionDirection` 与 `selectionchange` 事件；宿主只在存在原生 editing host 时读写对应 HWND，将 multiline 的 CRLF 位置转换为逻辑 UTF-16 位置，在无修饰 `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` 和 Shift/方向键期间保留短暂 anchor，并在原生消息完成或捕获/取消/焦点中断后调用 Browser 的通知入口。宿主窗口不保存第二份文本模型，也不经 Core 再派发选区事件。当前测试覆盖继承、`false`/未知值、`plaintext-only`、合法 UTF-8、失效目标、取消回滚、WM EDIT 的允许/取消顺序、selection range、原生 selectionchange、无修饰鼠标拖选的连续方向、键盘方向保持和中断收尾，以及 TEST1115 的 `CF_UNICODETEXT` paste/cut data、取消回滚、单次 Core mutation、折叠 caret 同步和空/不支持格式 fail-closed；TEST1116 覆盖 `WM_COPY` 非空选区复制、折叠选区 no-op、超长 UTF-8/非 Unicode 拒绝，以及原生 `WM_CUT` 内部重入保护。Range/Selection 对象、OEM 特有键盘自动重复与复杂行导航、富文本、designMode、ClipboardEvent/async clipboard、CF_TEXT 转换或 OEM IME 仍未实现。

### 绘制与交互

Core 提供 layout、paint、link/control/fragment geometry 和可聚焦目标。宿主持有 scrollbar、DPI/旋转、HDC、native child reposition 和 `SetFocus`。几何或 document token 不一致时，操作应 fail closed 并等待下一次有效 layout。

## 自动与人工结果

`auto=1` 的完整通过需要：每个所选测试完成、零 `ERROR`/`FAIL`、唯一 `TESTBENCH PASS`，并且真实 Browse fixture 的页面序列正确。进程退出、提示音或部分 `OK` 都不够。

下列内容仍需人工观察：

- 字体、颜色、边距、复杂布局和滚动条；
- 真实触摸、链接命中和键盘焦点；
- SIP 候选词、OEM IME 和硬键盘；
- 系统 picker 的选择/取消/返回；
- 旋转、screen/DPI 和失败网络旧页保留。

截图和日志放在本地 `tmp/`，不要加入 Git。

## 新增测试的纪律

新增一个纵向能力时：

1. 在产品 DLL 中实现通用语义；
2. 在宿主中只接平台 callback 和 fixture；
3. 为成功、取消、非法参数、资源清理和直接相邻旧路径写自动断言；
4. 更新 dispatch、开始提示、失败文本和最终汇总；
5. 只有确实需要时增加 manual-only fixture；
6. 使用 staging override 选择本批测试，不永久扩大 tracked smoke INI；
7. 不向 README、架构或测试指南追加本批设备流水。

如果实现记录只修改 `test_host`，应先检查是不是把产品能力错误地放进了测试平台。只有平台窗口、WM 消息、设备 GUI、网络调度和 fixture 本身才通常应独占宿主修改。

## 故障排查

- 先核对进程是否真正退出、EXE/DLL 是否来自同一 build、INI 是否在同目录。
- 读取完整 `test_host.log`，不要从提示音或最后一个对话框推断全批。
- 网络问题区分 DNS、TCP、TLS、证书、HTTP、redirect、resource 和 page commit。
- WMDC/RAPI、混包、SIP/IME 和高 DPI 的详细流程见 [`../docs/TROUBLESHOOTING.md`](../docs/TROUBLESHOOTING.md)。

公共所有权见 [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

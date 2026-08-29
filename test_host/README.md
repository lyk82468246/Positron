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
- 真实 Browse、DPI/旋转、SIP/IME、picker 和视觉 fixture。

编号只是 dispatch key，不是功能路线图。测试的准确含义应由 fixture、断言、开始提示和失败文本表达，不在 README 复制逐编号清单。

## 浏览器组合边界

### 页面导航

宿主持有后台网络 worker、loading/取消、候选文档和窗口 swap。旧页保持可绘制，直到新页面完成 parse/resource/style/layout 并可提交。URL reference 解析调用 `positron_http.dll`；history 提交调用 `positron_browser.dll`。

DOM、libcss 和 NetSurf document 只在 UI 线程操作。worker 不持有 DOM node、box、computed style 或 HDC。

### Core 与 Browser callbacks

宿主把当前 `PCore` document 包装为 size-tagged callbacks，供 Browser session 查询 DOM、属性、表单、validation、`contenteditable` 状态和事件。Browser 负责脚本对象、事件顺序、取消与事务状态；宿主只执行允许的 Core mutation、WM 默认动作和导航副作用。

callback 同步且不可重入。页面替换时先停止新消息和事务，再销毁 native 控件、Browser session 和 Core document，避免 stale token 或借用指针逃逸。

### Native EDIT/SELECT/button/file

WM subclass 把键盘、focus、composition、selection 和 click 转成 Browser typed transaction。只有 Browser 允许默认动作后，宿主才写入 Core/native 控件，并把提交结果送回 Browser 产生 `input`、`change`、submit/reset 等后续事件。

系统 picker、文件路径、SIP/IME、HWND、COMBOBOX popup 和真实焦点仍属于宿主。synthetic 消息只能做自动契约，不能替代 OEM 设备人工验收。

### 单元素 `contenteditable`

`test_host` 只把 `PBrowserScriptContentEditableCallbacks` 接到当前 Core 文档，并负责在真实输入源中编排 `beforeinput`、允许后的 `PCore_ContentEditableSetTextById` 和 `input`。Browser/Core 共同提供有效状态与有界纯文本替换；宿主不保存第二份编辑模型。当前测试覆盖继承、`false`/未知值、`plaintext-only`、合法 UTF-8、失效目标和取消回滚，不代表 caret/selection、富文本、designMode 或 OEM IME 已实现。

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

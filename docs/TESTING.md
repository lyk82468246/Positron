# 测试与验收

Positron 的验证分为主机静态检查、VS2008 ARMV4I 构建、自动设备门和必要的人工设备验收。单一层级通过不能替代其他层级：桌面脚本不能证明 ARM 二进制可用，自动断言和首帧也不能证明真实触摸、SIP 或视觉结果。

逐测试实现以 [`test_host/main.c`](../test_host/main.c) 为准；当前候选、设备连接状态和最近证据只写入 [`.agents/HANDOFF.md`](../.agents/HANDOFF.md)。本文只保留长期有效的操作、判定和能力分组，不维护会漂移的逐编号历史。

## 测试宿主的职责

`test_host.exe` 是公共 DLL 的回归宿主和示例消费者。它负责窗口/消息接线、测试 fixture、平台 callback、断言和日志；URL、DOM、事件、表单、资源、布局以及生命周期语义必须由对应公共 DLL 提供。测试编号是宿主实现细节，不是公共 ABI。

新增测试时应同时修改源码 dispatch、相邻断言和必要的测试说明。不要把产品实现源文件加入宿主工程，也不要为了让宿主通过而复制一份公共语义。

## `test_host.ini`

INI 必须和 `test_host.exe` 位于同一目录。最小自动配置如下：

```ini
auto=1
javascript=0
tests=13,20,27,999
```

`tests` 接受逗号或空格分隔的编号和范围，也接受源码明确支持的特殊编号，例如：

```ini
tests=1-5 7b 13 20,999
```

文件不存在时宿主进入交互式分组选择；文件为空、不可读或格式错误时提示并退回分组选择，不会静默扩大为全量。Nightly 打包和设备门从当前源码 dispatch 动态生成全量自动清单，因此不会把每天新增的测试硬编码在脚本中。

### 自动模式

`auto=1` 直接运行选择，抑制确认和结果 MessageBox，同目录 `test_host.log` 覆盖写入。自动可视测试至少绘制一帧后自行关闭；任何断言失败都会使批次失败，只有唯一的 `TESTBENCH PASS` 才是完整通过。

自动模式证明断言、资源计数、消息路径和首帧没有失败，不证明字体、边距、抗锯齿、触摸命中、系统 picker、旋转或 OEM 输入法体验。导航日志中的资源终态、失败分类、重试次数、required/optional gate 和脱敏摘要由 Browser DLL 提供，宿主只负责调度、读取和记录。

### 手动模式

`auto=0` 保留启动确认、测试说明和人工关闭流程。可视页面通常停留在设备上，验收者按页面说明操作，再使用 `Esc`、页面空白处或测试指定入口继续。交互模式不保证完整自动日志；截图、设备信息和操作记录放在本地 `tmp/`。人工观察后如需机器判门，应以相同选择再运行一轮 `auto=1`，但自动日志不能替代人工结果。

### 浏览器 JavaScript 与完成提示音

- `javascript=0` 是默认产品路径，不执行页面 classic script。
- `javascript=1` 显式启用实验性的 Browser script session、受限 DOM/Event/input/navigation bridge 和 classic script。
- 独立 `positron_script.dll` 测试不依赖这个开关；开启它也不表示支持完整 DOM、Web API、ECMAScript host environment 或浏览器安全沙箱。
- TEST999 是专用完成提示音。只有显式选中且前序批次未失败时，宿主退出前请求一次系统提示音。声音、窗口关闭或部分 `OK` 都不能替代日志判定。

## 四种常用配置

| 目标 | 配置 |
| --- | --- |
| 部分测试、自动断言 | `auto=1`，`tests=` 写编号；按需保持 `javascript=0` |
| 部分测试、人工模式 | `auto=0`，`tests=` 写编号；脚本 fixture 再启用 JavaScript |
| 所有自动安全测试 | 使用 nightly 生成的 INI，或让设备门按当前 dispatch 生成清单 |
| 所有测试、人工模式 | 在全量清单中加入发布说明列出的 manual-only fixture，并设 `auto=0` |

移走或改名 INI 只会进入旧式分组选择，不等于自动运行所有测试。manual-only fixture 不得放进 `auto=1` 清单；它们会主动拒绝自动运行，以免把未观察的人工行为伪装成通过。

## 能力回归分组

逐测试合同仍以源码和组件 README 为准，稳定文档只维护能力分组：

- 资源、导航、history、viewport、页面生命周期、脚本任务队列、焦点和窗口通知由早期资源/会话夹具覆盖；这些夹具共同验证候选 generation、required/optional gate、取消、旧页保留、滚动快照和事件顺序。
- 几何、overflow、滚动、selector、form owner、validation、submission、FormData、option/select 和图像 source 夹具覆盖 Core/Browser callback 的边界、预算、snapshot 隔离和 fail-closed 行为；真实控件、DPI、触摸和视觉仍属于人工验收。
- DOM/CharacterData 夹具覆盖 Text、Comment、CDATA、属性、`textContent`、`innerHTML`/`outerHTML`、`document.write`、title、detached Element 与 bounded DocumentFragment。Fragment 只允许文档规定的有限根数和节点形状。
- TEST1284–1288 覆盖 CDATA 创建/物化、Text 合同、Fragment CharacterData staging、Core/live Element/detached Fragment 的 `Node.normalize()`，以及 detached Element 的直接 CharacterData staging：空 Text/CDATA 被删除，相邻 Text/CDATA 合并到首个非空节点，Comment 保持边界；Text、Comment、CDATA wrapper 可在 clone、replace、直接物化、移除和再次插入之间保持有界 identity。嵌套 Element 仍在 mutation 前拒绝。

这些夹具证明的是有界公共合同，不是完整浏览器标准、任意网站兼容性、完整 live collection、MutationObserver、Range/Selection、通用嵌套 Fragment 或无限 DOM mutation。

## 本机验证

修改产品 C、移植代码或 C89 转换脚本后先运行：

```bat
python scripts\test_c89ize.py
```

提交前运行仓库审计：

```bat
python scripts\audit_repo.py
```

审计覆盖工程输入、版本 pin、许可证、Git 跟踪、UTF-8、Markdown 链接、文档职责和 `test_host` 产品边界。审计成功不代替构建、设备行为或宿主 helper 语义归属审查。

使用正式工程入口构建：

```bat
scripts\build.bat
scripts\build.bat Debug rebuild
```

局部低风险修改可以先增量构建；工程依赖、生成规则、静态库或无法解释的混包问题使用 clean rebuild。不要直接调用 ARM 编译器拼装部分目标。

## 自动设备门

### 前提与运行

先由用户在 WMDC 或 Device Emulator GUI 中手动建立恰好一个目标连接。设备门只复用当前 RAPI 会话：不枚举或选择设备，不绑定 VMID，不启动、cradle、断开、重置或强杀设备。

```bat
scripts\device_gate.bat -Candidate feature-name
```

定向批次使用 staging override，不修改 tracked INI：

```bat
scripts\device_gate.bat -Candidate feature-name ^
  -TestSelection "1284-1288,999" -EnableJavaScript
```

脚本执行正式构建、隔离 staging、整包部署、启动、有限等待、日志回收和判门。本地证据在 `tmp/device-runs/`，不进入 Git。超时后设备进程仍需由用户在设备 UI 正常结束；设备门不提供安全的通用远端终止。

### 空间、部署和日志

未指定 `-RemoteBase` 时，设备门优先使用 `\Storage Card\Temp\Positron-device-gate`，路径不可创建或无法做路径级空间查询时回退到 `\Temp\Positron-device-gate`。显式目标不自动回退。两种模式都会查询目标卷和内部 object store；目标硬性余量为 staging 总大小加 1 MiB，内部 object store 另有 64 KiB 缓存告警线。空间字段和选择原因写入预检结果。

空间不足时只回收设备门自己生成、非当前运行目录的旧目录。每个候选目录必须先把日志成功复制两次并确认稳定的 `TESTBENCH PASS` 或 `TESTBENCH FAIL`；日志缺失、仍增长、复制失败、未知目录和当前目录都保留。回收后重新查询空间，仍不足才阻断部署。

完整日志必须在清理前复制到电脑。启动头、部分 `OK`、提示音、窗口关闭或单次 RAPI 成功都不是通过证据。

### 自动通过标准

一次设备门同时满足以下条件才通过：

1. 正式构建和整包 staging 成功；
2. 日志来自本次唯一候选目录；
3. 每个所选测试都有完成记录；
4. `ERROR`、`FAIL` 均为零；
5. 恰有一个 `TESTBENCH PASS`；
6. 涉及真实 Browse 时，路由和最终页面序列符合 fixture；
7. 没有旧 EXE/DLL 混包、遗留进程或 crash dump 证据。

## 风险相称的回归范围

每批通常运行新能力、直接共享的 ABI/所有权/默认动作、一个页面或导航哨兵（若相关）以及 TEST999。多个低风险批次累计、修改公共 ABI 或生命周期、触及 layout/paint、输入、网络/TLS、资源缓存、准备里程碑或出现崩溃/超时/数据错误时，再扩大到更宽范围或全量。全量清单从当前源码生成，不复制到本文。

## 人工验收

真实设备必须观察字体 fallback、字形、颜色、渐变、左右边距、居中容器、换行、表格、列表、滚动条、触摸命中、键盘焦点、SIP/IME、旋转/DPI、系统 picker、窗口返回、剪贴板互操作、loading、失败网络、旧页保留和深层导航。低风险视觉或输入变化可以累计后集中验收；崩溃、数据损坏、严重布局破坏和核心交互阻塞必须立即复核。

每组人工记录至少包含 commit/候选名、精确 `tests=`、设备型号、screen/DPI/方向、初始页面、操作步骤、预期与实际结果，以及必要截图和同批自动日志。截图和日志只放 `tmp/`；比较截图前先确认 viewport、DPI、方向、滚动位置和二进制身份一致。

## 网络测试与候选基线

WM6 镜像时间经常过旧。证书测试前校准时间，并把失败区分为 DNS、TCP、TLS handshake、证书/hostname、HTTP status、redirect、资源获取、页面解析和最终提交。离线 fixture 用于稳定合同，真实端点只作集成哨兵；暂时不可达不能通过放宽离线断言解决。

候选写入当前 handoff 前必须满足：范围、ABI 和所有权清楚；C89 回归、仓库审计和 ARMV4I 正式构建通过；staging 来自同一批构建；风险相称的设备日志完整通过；必要人工验收已完成或明确进入允许累计清单；handoff、限制、路线图和稳定文档各自只更新自身职责。

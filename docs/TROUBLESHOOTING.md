# 故障排查

## 先确定运行的是什么

设备问题首先排除环境和二进制身份，再改源码：

1. 查看当前 Git 分支、未提交 diff 和候选目录。
2. 确认使用 `scripts\build.bat` 或 `scripts\stage.bat` 的正式配置。
3. 确认旧 `test_host.exe` 已真正退出，而不是 Smart Minimize。
4. 确认六个 DLL 与 EXE 来自同一次构建。
5. 读取完整 `test_host.log`，不要只依赖弹窗、提示音或截图。
6. 记录设备 screen、DPI、方向、系统时间和操作步骤。

Windows CE 可能在系统范围继续复用已加载 DLL。只替换一个文件、从多个 stage 目录拼包或
保留旧进程，都可能造成“新 EXE + 旧 DLL”的假回归。最可靠的做法是关闭进程、使用新的隔离
stage 目录，必要时重启模拟器。

## 构建失败

查看根目录 `vs2008-build.log` 中最早的错误。

常见类别：

- `devenv.com` 找不到：安装 VS2008，或正确设置 `VS90COMNTOOLS`。
- platform/configuration 不存在：使用正式
  `Windows Mobile 6 Professional SDK (ARMV4I)` 配置。
- 大量类型或语法错误从某个 NetSurf 文件开始：先检查缺少的前置头和 C99 声明位置，
  不要逐条修复后续连锁错误。
- unresolved external：核对 vcproj 是否包含对应转换后源文件、静态库和 WinCE shim。
- 中间块声明或 C99 初始化：修改对应移植脚本并运行 `python scripts\test_c89ize.py`。

不要用现代桌面编译器“证明”VS2008 工程可以工作；目标编译器本身就是兼容性边界。

## Stage 失败或文件无法覆盖

- 关闭设备上的测试窗口和任务管理器中的宿主进程。
- 使用 `scripts\stage.bat Debug C:\WMShare\Positron-candidate` 隔离新包。
- 不要在 build 失败后手工复制部分产物。
- 如果设备仍加载旧 DLL，重启模拟器后再运行隔离目录。
- 比较二进制时应比较同一配置和同一批构建，不以文件时间单独判定。

## 日志没有通过

按最早异常分类：

- 没有日志：确认 `auto=1`、配置文件与 EXE 同目录、目录可写。
- 只有部分测试：检查配置解析、进程异常退出、超时或前序失败。
- 有 `ERROR` 但后面继续：确认它是否是测试明确预期的失败路径；不能擅自忽略。
- 有 `FAIL`：候选不能成为基线，不通过放宽断言掩盖。
- 没有最终 `TESTBENCH PASS`：即使听到系统提示或窗口关闭，也不算完整通过。
- TEST999 没响：确认显式选择了 999、前序未失败，并检查设备系统声音设置。

## 网络和证书

先检查模拟器系统时钟。WM6 镜像常带有多年前的日期，会造成证书尚未生效或已过期的假象。

再区分：

1. DNS 是否解析；
2. TCP 是否连接；
3. TLS handshake 是否完成；
4. 证书链和 hostname 是否通过；
5. HTTP status/redirect 是否符合预期；
6. 页面是否成功提交；
7. 外部 CSS、图片和脚本资源是否完成。

不要把服务器暂时失败、网络波动或页面内容变化直接归因到布局。需要稳定断言时使用离线
fixture；真实网页仍保留为集成哨兵。

## 页面“偶尔正常、偶尔崩坏”

先确认两次操作是否真的落在同一个页面、viewport、DPI、方向和滚动位置。网络导航中旧页、
待提交页和失败回滚页可能同时存在，截图需要配合日志中的最终 URL 和页面提交遥测。

若自动几何通过但截图明显错误，以人工结果为准。记录：

- 初始 URL 和点击目标；
- 最终 URL；
- screen/DPI/方向；
- 导航前后滚动位置；
- 容器和关键 box geometry；
- 对应候选目录和完整日志。

不要用全局偏移、统一清零 scrollbar 或放宽像素断言处理尚未定位的真实页面问题。先查
[失败实验索引](../.agents/FAILED_EXPERIMENTS.md)，避免恢复已知高风险方案。

## 高 DPI 与旋转

设备物理像素不等于 CSS px。Browse、重排和交互后的 restyle 都必须重新声明当前物理
viewport 和 DPI；不能把 96 DPI 固定成产品前提。

排查时至少记录：

- `GetSystemMetrics` 得到的 screen/client 尺寸；
- 实际 DPI；
- portrait/landscape；
- CSS viewport；
- 旋转前后内容位置和滚动比例；
- 交互后是否重新 layout。

只在单一 96-DPI 模拟器通过，不能证明高 DPI 路径正确。

## SIP、键盘和 IME

自动发送 `WM_CHAR`、`WM_KEYDOWN` 或 synthetic event 只能验证桥接数据，不能替代真实 SIP。

人工复核应区分：

- 单字符按键；
- SIP 候选词一次性提交；
- UTF-16 代理对；
- composition start/update/end；
- `beforeinput`、`input`、`change` 和 key event 的次序；
- EDIT、textarea、SELECT 的不同 native control 路径。

候选词只能输入一个字符时，先记录 WM 消息序列和实际 control 文本，再判断是宿主子类、
composition bridge 还是测试 oracle 问题。

## JavaScript

先确认当前运行的是：

- 独立 `positron_script.dll` 测试；还是
- `javascript=1` 的浏览器绑定。

浏览器绑定使用有界 native callback 槽位和有预算的共享 context。出现 native function
limit、source limit、memory limit 或 timeout 时：

- 保留原有限制作为产品边界；
- 查重复注册、重复 bootstrap 和不必要源码；
- 用实际完整 bootstrap 做探针；
- 不通过提高预算或删断言掩盖生命周期错误。

默认 `javascript=0` 的 Browse 路径不应因为新增绑定而多抓脚本或改变页面行为。

## 收集一个可复现问题

最小诊断包应包含：

- commit 和 `git status --short`；
- 候选 stage 目录；
- `test_host.ini`；
- 完整 `test_host.log`；
- screen/DPI/方向和系统时间；
- 精确操作步骤；
- 必要截图；
- 预期行为与实际行为；
- 是否能在重启模拟器和全新 stage 后复现。

截图和日志留在本地 `tmp/`，不要提交。稳定结论再写入测试、限制或失败实验索引。

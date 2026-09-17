# `test_host.exe`

`test_host.exe` 是 Positron 的回归宿主和示例消费者，不是公共 API，也不是业务语义的所有者。它把 `positron_tls.dll`、`positron_json.dll`、`positron_http.dll`、`positron_image.dll`、`positron_script.dll`、`positron_core.dll` 和 `positron_browser.dll` 接到 Windows Mobile 6 / Windows CE 的窗口、消息和测试 fixture 上。

## 硬性所有权边界

宿主可以拥有窗口、消息循环、DPI/旋转、native EDIT/SELECT/button/file picker、SIP/IME、网络 worker、RAPI 部署、应用策略、fixture、日志和断言。可复用的 URL、资源、DOM、CSS、布局、Event、form、selector、CharacterData、Fragment、导航 candidate 或生命周期语义必须位于对应公共 DLL。

`test_host` 工程只能编译自己的源文件并链接公共 import library；不得 include 或编译 `positron_*` 实现 `.c`，不得定义 `PCore_*`、`PBrowser_*`、`PHttp_*` 等公共入口来“临时修复”产品行为。仓库审计会检查这条边界。

## 构建与运行

使用仓库正式入口构建：

```bat
scripts\build.bat Debug rebuild
```

设备运行由用户先在 WMDC/Device Emulator GUI 中连接恰好一个目标，再从仓库根目录调用：

```bat
scripts\device_gate.bat -Candidate test-host-smoke
```

设备门复用当前 RAPI 会话，不选择、cradle、重置或强杀设备。空间预检、外置卡优先、日志回收和旧部署清理由设备门负责；宿主只写本次运行日志和结果。

## 配置

`test_host.ini` 必须和 `test_host.exe` 同目录：

```ini
auto=1
javascript=0
tests=13,20,27,999
```

`tests` 支持逗号/空格分隔的编号和范围。`auto=1` 抑制确认框、覆盖写 `test_host.log`，并要求最终唯一 `TESTBENCH PASS`；`auto=0` 保留页面说明和人工关闭流程。`javascript=1` 只开启 Browser 的实验性页面脚本桥，不影响独立 Script DLL。

TEST999 是专用完成提示音，只有显式选中且批次没有失败时退出前请求一次系统提示音。提示音或窗口关闭不能替代日志判定。

移走 INI 会回到交互式分组选择，不等于自动运行全量。Nightly/设备门从 `run_configured_tests` 的 dispatch 动态生成全量安全清单，新增测试不应再改打包脚本的固定目录。

## 测试层次

- 低层公共 DLL：Core relation/mutation、TLS/HTTP/JSON/Image/Script 的参数、所有权、容量和错误码。
- Browser 组合：history、resource/candidate、viewport、事件、form/selector、DOM wrapper、Fragment staging 和 task checkpoint。
- 真实页面/平台：导航、布局、GDI 绘制、native 控件、SIP/IME、picker、旋转和 DPI。
- 交付门：C89 回归、仓库审计、正式 ARMV4I 构建、设备日志、空间预检、清理和 crash check。

自动断言只覆盖稳定合同和首帧；字体、边距、视觉、触摸、OEM 输入和失败网络由人工验收。崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即人工复核。

## Core 与 Browser callback 接线

宿主在创建页面时：

1. 解析 Core document，完成资源结果、style/layout，并注册 relation/interaction callback。
2. 创建 Browser script/history session，注册 DOM read/write、resource、focus、scroll、viewport、lifecycle 和 native-control callback。
3. 在 `WM_SIZE`、物理滚动、焦点、可见性和页面提交边界显式通知 Browser。
4. 在消息循环中调用任务 checkpoint，执行 Browser 发出的平台请求，再回传实际 CSS/page 结果。
5. 清理时先停止 worker/pending resource 和 callback，再 teardown Browser/Script，最后释放 Core document。

DOM write callback 只转发父/元素 id、未过滤 child index、节点类型和 UTF-8 值；宿主不遍历、合并、删除或缓存公共 DOM。`Node.normalize()` 使用 Ex5 `normalize_child_text` 进入 Core，Core 和 Browser-created Text/CDATA wrapper 的数据同步由公共 DLL 完成。

## 能力夹具

当前自动合同按能力分组维护：资源/导航/history/viewport/生命周期；几何/overflow/焦点/selector；form owner/validation/submission/FormData/option；图像 source 与元数据；Text/Comment/CDATA、属性、HTML serialization、title、document.write 和 bounded DocumentFragment。最新的 CDATA/Fragment/normalize 夹具覆盖 Core、live Element 与 detached Fragment 的空节点删除、Text/CDATA 合并、Comment 边界和 created wrapper 同步。

测试编号及断言在 `main.c` 的 dispatch 中维护。稳定文档只记录分组和边界；逐编号实现、fixture HTML、错误字符串和本地证据以源码、日志和 `.agents/HANDOFF.md` 为准。

## 新增测试纪律

新增测试必须：

- 先确定语义所有者，公共行为写入所属 DLL，宿主只接线和断言；
- 给出最小 fixture、成功与失败断言、预算/所有权说明；
- 更新 `TEST_MAX_NUMBER`、dispatch 和合适的自动选择；
- 通过 `python scripts\test_c89ize.py`、相关构建和风险相称的设备门；
- 对手工视觉、SIP、旋转、picker 等风险明确写入人工清单，而不是把未观察当作自动通过。

## 日志与故障排查

自动批次必须包含启动头、每个选中测试的完成记录、错误/失败计数和唯一 `TESTBENCH PASS`。完整日志在设备空间清理前复制到电脑；截图和临时日志只放 `tmp/`。没有完整日志、旧 EXE/DLL 混包、遗留进程或 crash dump 时，不得更新产品基线。

WMDC 连接由用户 GUI 完成。遇到 RAPI timeout，先确认只有一个设备、WMDC 会话仍为 connected、设备端旧宿主已退出，再重试设备门；不要在宿主中添加连接、选择、cradle、重置或远端杀进程逻辑。更多长期操作规则见 [`docs/TESTING.md`](../docs/TESTING.md)，公共所有权见 [`docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

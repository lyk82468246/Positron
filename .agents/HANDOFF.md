# Positron 当前交接

更新时间：2026-08-13

稳定使命、架构和公共边界见
[`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。本文件只记录当前仓库基线、最近设备证据、
当前边界和唯一下一步。

## Git 与仓库基线

- 分支：`main`，跟踪 `origin/main`。
- 最新已验证产品基线：next220。
- next220 批次包含 `test_host/main.c`、`test_host/test_host.ini`，以及新的
  `scripts/device_gate.bat`、`scripts/device_gate.ps1` 自动设备门。
- 本地设备证据位于 `tmp/device-runs/20260813-110121-next220/`；`tmp/` 不跟踪，干净 clone
  中没有该日志，不能据此假定新环境也已经连接或通过。

接管时仍须重新运行 `git status --short --branch` 和 `git diff`；以上列表不是 Git 的替代品。

## 最近已验证设备证据

### 最新全量检查点：next220

- 配置：`TEST13/20/27/43/44/56/58-77/80-187/999`，共 135 项。
- 环境：Windows Mobile 6 Professional Square QVGA Emulator，`screen=320x320 dpi=128`。
- 通道：脚本从唯一运行的 Device Emulator 读取 VMID，精确匹配既有 CoreCon 目标；没有
  启动、选择、Cradle 或重置设备。
- 结果：135 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、远端退出码 0、最终唯一 `TESTBENCH PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`。
- 能力终点：绝对 URL 中多个完整 `%2E%2E` double-dot segment 的受控顺序折叠与同文档
  fragment 导航。
- 自动证据：`python scripts/test_c89ize.py`、VS2008 ARMV4I Debug 正式构建、14 文件隔离
  staging/部署、SHA-256 清单和日志自动判门均通过。

### 最近定向检查点：next219 修正版

- 配置：`TEST13/151-186/999`，共 38 项。
- 结果：37 条标准数字 `OK`、1 条 TEST13 overview、零 `ERROR`、零 `FAIL`、最终
  `TESTBENCH PASS`。
- 能力终点：根相对 URL 的末尾半编码 double-dot segment。
- 首包 `C:\WMShare\Positron-next219` 因 20,991 字符 DOM bootstrap 在 TEST162 超过既有
  1000ms 预算而失败；修正版使用共享 `ppartial` helper 将 bootstrap 降到 19,735 字符，
  没有提高预算。旧首包不得作为基线。

### 仍有效的人工证据

- next167 已人工确认 example.com `Learn More` 后页面容器边距正常。
- next167 已人工确认真实 SIP 候选词可以完整提交，不再只输入下一个字符。
- 视觉、真实触摸、SIP、旋转和失败网络允许累计后集中复核；崩溃、数据损坏、严重布局
  破坏或核心交互阻塞必须立即检查。

## 已关闭批次：next220

目标：绝对 `location.href`、`location.assign()` 和 `location.replace()` URL 中，允许多个
完整 `%2E%2E` double-dot segment 按路径顺序分别折叠其前驱非空目录，再使用既有
origin/path/query 规则判定同文档 fragment 导航。

实现边界：

- TEST187 覆盖大小写、内嵌/末尾组合、三个入口、fragment 清除、same-value、
  state/length、hashchange、无 GET、不同 query/path、根相对重复、完整/半编码混合、
  字面父目录和 14/16 native callback 槽位。
- 根相对重复完整 `%2E%2E` 仍走普通导航；本批不宣称完整 URL Standard parser。
- 共享 `pdouble` helper 把完整 browser bootstrap 从 19,735 降到 19,486 字符。
- 默认 `javascript=0`、TEST13 行为、core 公共 ABI 和 callback 总上限不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`；
- 14 组完整 bootstrap JavaScript 正反探针；
- VS2008 ARMV4I Debug 正式构建；
- 隔离 staging 的 14 个部署文件及 SHA-256 清单；
- 当前 ini 为 1298 字节，选择
  `TEST13/20/27/43/44/56/58-77/80-187/999`，共 135 项；
- 上述完整设备日志和 TEST13 三段真实导航。

## 唯一下一步

实现一个窄纵切：评估根相对 `location.href`、`location.assign()` 和
`location.replace()` URL 中多个完整 `%2E%2E` segment 的顺序折叠。

完成标准：

- origin 根不可越过，且每个编码 segment 必须有非空前驱目录；
- origin/path/query 匹配后才进入同文档 fragment 队列；
- 绝对路径旧能力继续通过，完整/半编码混合、字面父目录、不同 query/path 仍是反例；
- 三个 location 入口、clear、same-value、state/length、hashchange 和无 GET 都有断言；
- C89、完整 bootstrap 探针、正式 ARMV4I 构建和风险相称的设备门通过；
- 不在同一批加入其他 URL 拼写、DOM、布局或性能改动。

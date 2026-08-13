# Positron 当前交接

更新时间：2026-08-13

稳定使命、架构和公共边界见
[`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。本文件只记录当前仓库基线、最近设备证据、
当前边界和唯一下一步。

## Git 与仓库基线

- 分支：`main`，跟踪 `origin/main`。
- 最新已验证产品基线：next222。
- next222 批次包含 `test_host/main.c`、`test_host/test_host.ini`，以及改用当前 WMDC/RAPI
  会话的 `scripts/device_gate.bat`、`scripts/device_gate.ps1` 自动设备门和
  `scripts/repair_wmdc_rapi.*` 环境修复脚本。
- 本地设备证据位于 `tmp/device-runs/20260813-143759-next222/`；`tmp/` 不跟踪，干净 clone
  中没有该日志，不能据此假定新环境也已经连接或通过。

接管时仍须重新运行 `git status --short --branch` 和 `git diff`；以上列表不是 Git 的替代品。

## 最近已验证设备证据

### 最新全量检查点：next222

- 配置：`TEST13/20/27/43/44/56/58-77/80-189/999`，共 137 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：137 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`。
- 能力终点：`history.pushState`/`replaceState` 支持同源根相对 path/query/fragment 和
  query-relative URL，且同文档 traversal 恢复 URL/state 并按 popstate 后 hashchange 排序；
  cross-origin、protocol-relative 和同源 absolute path 变化仍拒绝。
- 自动证据：`python scripts/test_c89ize.py`、VS2008 ARMV4I Debug 正式构建、14 文件隔离
  staging/部署、SHA-256 清单和日志自动判门均通过。定向
  `TEST189/999` 证据位于 `tmp/device-runs/20260813-143239-next222-debug189c/`；最终 gate
  安全约束冒烟位于 `tmp/device-runs/20260813-144349-next222-gate-smoke/`。
- gate 会在设备端只回收自己命名的旧候选目录；本次因旧目录占满 `\Temp` 暴露并验证了该
  回收路径。WMDC 旧 COM 注册的 5 个 RAPI 类已由正式修复脚本做 32/64 位幂等验证。

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

## 已关闭批次：next222

目标：让浏览器 history state URL 支持同源根相对 path/query/fragment 和 query-relative
写法，在不发 GET 的前提下同步更新 `location`/`document.URL`，并保持 traversal 的
state、length、popstate/hashchange 行为。

实现边界：

- JS bootstrap 的 `phistoryUrl` 解析 fragment-only、query-relative、root-relative 和受限
  absolute URL；protocol-relative、cross-origin 和同源 absolute path 变化明确抛错。
- C 侧 history bridge 以大小写不敏感的文本 scheme/authority 边界做 same-origin 门，允许
  已经由 bootstrap 解析成 absolute 的根相对/query-relative 结果写入历史。
- TEST189 覆盖 replace/push、两次 traversal、URL/state/length、事件顺序、无 GET、拒绝项和
  14/16 native callback 槽位。最初前进事件串超过 `PScript_GetResult` 255 字符上限；最终测试
  像 TEST150 一样只比较新增 `seen.slice(2)`，没有提高预算或放宽行为断言。
- 默认 `javascript=0`、TEST13 行为、公共 ABI 和 callback 总上限不变。

自动化同步完成：

- `device_gate.ps1` 改用 WMDC 当前 RAPI 会话，不再依赖 CoreCon 活动连接或 VMID；支持
  `-TestSelection` 只覆盖隔离 staging 的测试选择。
- gate 只回收 `\Temp\Positron-device-gate` 下符合自身 candidate/timestamp 规则的旧目录；
  未识别目录保留。
- `repair_wmdc_rapi.bat/.ps1` 自行请求 UAC，幂等修复 5 个已知 WMDC RAPI COM 类的 32/64
  位旧 `%windir%` 路径；未知注册值拒绝修改。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- PowerShell 解析、修复脚本 `changed=0/status=PASS` 幂等门；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST189/999` 和默认 137 项全量 WMDC/RAPI 设备门；
- TEST13 三段真实导航、零 `ERROR`、零 `FAIL`、唯一 `TESTBENCH PASS`。

## 唯一下一步

选择一个常见且边界可控的 document-relative history state URL（优先同目录单段 sibling）
作为 next223；只补齐 `pushState`/`replaceState` 的这一种解析，不顺便扩展 location 导航、
`./`、`../`、protocol-relative、DOM、布局或性能。

完成标准：

- sibling 相对 URL 明确按当前 document 目录解析；空段、`./`、`../`、cross-origin 和
  protocol-relative 保持反例或既有分类；
- replace/push、无 GET、state/length、traversal、popstate/hashchange 和 255 字符结果边界
  都有断言；
- C89、完整 bootstrap 探针、正式 ARMV4I 构建和风险相称的设备门通过；
- 不在同一批加入无关 DOM、布局或性能改动。

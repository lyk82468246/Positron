# 测试与验收

## 测试宿主

`test_host.exe` 是公共 DLL 的回归宿主和示例消费者。它覆盖基础 TLS/HTTP/JSON、图像、
HTML/CSS/layout、真实网页 Browse、表单输入、事件和实验性浏览器 JavaScript。

测试编号属于宿主实现细节。稳定文档不维护逐编号流水；当前候选选择和新增测试含义写在
[`.agents/HANDOFF.md`](../.agents/HANDOFF.md)，精确实现以
[`test_host/main.c`](../test_host/main.c) 为准。

## `test_host.ini`

配置文件必须与 `test_host.exe` 位于同一目录。核心配置项：

```ini
auto=1
javascript=0
tests=13,20,27,999
```

### `auto`

- `auto=1`：按编号运行所选测试，不弹出 Yes/No/OK；覆盖写入同目录 `test_host.log`。
- `auto=0`：保留交互式选择和确认流程。

自动可视测试会让窗口至少完成一次绘制后正常关闭。这能证明断言、资源计数和首帧绘制没有
失败，但不能证明字体、边距、抗锯齿或整体版式正确。

### `javascript`

- `javascript=0`：默认产品路径，不执行浏览器 classic script。
- `javascript=1`：显式启用实验性 inline/external classic script、页面 context 和受限
  DOM/Event/input/location/history bridge。

独立 `positron_script.dll` 的测试不要求打开浏览器 JavaScript。开启此配置也不代表完整
Web JavaScript 兼容性。

### `tests`

接受逗号或空格分隔的编号和范围，也接受特殊编号 `7b` 与 `999`：

```ini
tests=1-5 7b 13 20,999
```

历史浮动实验 TEST23、TEST78 和 TEST79 已撤回，不能通过配置重新启用。

TEST999 是专用完成提示音。只有显式选中、且前序测试没有令整个批次失败时，程序退出前才
请求一次系统提示音。它不验证其他产品能力。

配置缺失时宿主走交互流程；存在但无效的配置会提示并忽略，不会静默扩大测试范围。

### 当前默认自动选择与人工验收包（next228）

工作区当前的 `test_host/test_host.ini` 保持自动模式，并使用窄的 smoke 选择：

```ini
auto=1
javascript=0
tests=13,20,27,56,58,62,64-67,73,75,999
```

这是窄的自动 smoke 选择，不是完整自动回归基线。自动 next228 基线是
`auto=1`、`javascript=0`、`tests=13,20,27,43,44,56,58-77,80-195,999`，设备 gate
通过 `-TestSelection` 只修改隔离 staging，不改 tracked ini。

需要做人工视觉/输入验收时，临时把 staging 或工作区的 `auto` 改为 0；验收结束后务必恢复
`auto=1`，避免下一次设备门再次弹出确认框。

每项的观察目标如下：

| 测试 | 人工观察目标 |
| --- | --- |
| 13 | 从起始页打开 example.com；点击 `Learn More` 后内容仍在居中的虚拟容器内，左右边距存在，不能贴到屏幕边缘。继续进入 IANA Example Domains 和 `IANA-managed Reserved Domains`，页面不应崩坏。 |
| 20 | BMP、PNG、JPEG、GIF 四个有边框图片都显示，不能出现 fallback 文本。 |
| 27 | SVG 红块、绿块和光滑蓝色曲线显示，不能出现 SVG fallback 文本。 |
| 56 | 三个等高彩色行按 top/middle/bottom 对齐；下面两行等高，橙色 rowspan 文本在底部；不应出现垂直滚动条。 |
| 58 | 蓝色标题、160px 着色内容带、级联示例和红/绿/蓝表格显示；隐藏导航文字不可见。 |
| 62 | 复选框/单选框的选中与未选中外观正确；隐藏 input 不占可见行。 |
| 64 | 复选框可切换；禁用项保持不变；同一 radio 组互斥，不同 form/组不串状态。 |
| 65 | 四个 native EDIT 可见；首个输入框启动时可能显示 `wm-edit`（桥接探针的预置值）。点击它，用 SIP 输入并选中一个多字候选词，字段必须出现完整候选词而不是只出现下一个字母；密码框遮罩，readonly/disabled 不可修改，maxlength 生效。 |
| 66 | 可编辑 textarea 能输入多行；readonly/disabled 两个 textarea 不可修改；SIP 收起后布局不应错位。 |
| 67 | 第一个下拉框可打开并改选；禁用下拉框不能改；multiple 列表保留两个选中项并可切换第三项。 |
| 73 | checkbox、focus、disabled/enabled、button active 和 option:checked 的颜色/状态随操作变化；Reset 后回到初始状态。 |
| 75 | 灰色父框内依次看到红色 static、偏移后的绿色 relative、蓝色 absolute block、黄色 absolute inline；四个都不能跑出灰框。 |
| 999 | 所有项目完成后只听到一次系统提示音。 |

TEST190-195 是自动 history-state 断言，不属于这次需要肉眼观察的包。

## 运行自动设备门

### 一键设备门

先在 WMDC/Device Emulator GUI 中建立当前设备连接。USB 真机和 DMA emulator 均可；
RAPI 1 只暴露 WMDC 当前会话，因此 gate 不枚举设备、不绑定 VMID，也不会连接、选择、
启动、Cradle、断开或重置设备：

```bat
scripts\device_gate.bat -Candidate nextNNN
```

脚本使用 WMDC 官方 32 位 RAPI 通道，并完成正式增量构建、隔离 staging、整包部署、
`test_host.exe` 启动、有限等待、日志回收和自动判门。每次运行使用唯一的设备端
`\Temp\Positron-device-gate\<candidate>-<timestamp>`，不会读取旧日志。下一次运行只回收
这个 gate 根目录中符合自身命名规则的旧候选目录；未知目录一律保留。完整证据保存在本地
`tmp/device-runs/`。

默认测试选择来自 tracked `test_host/test_host.ini`。调试或批次定向门可只改本次隔离
staging 的 `tests=` 值，不修改 tracked ini：

```bat
scripts\device_gate.bat -Candidate nextNNN-debug ^
  -TestSelection "190-194,999"
```

`-PlatformName`/`-DeviceName` 不用于 RAPI gate；更换设备只需先在 GUI 中切换 WMDC 当前连接。
RAPI 1 没有安全的远端等待/终止接口，因此 gate 以完整 `TESTBENCH PASS/FAIL` 日志作为完成
标记；超时会保存可取得的部分日志并返回非零，但不会杀死设备进程。连接缺失、部署失败、
运行超时、日志缺失或任一判门条件失败也都会返回非零。自动门仍不替代下文列出的人工视觉
和输入检查。

如果 `CeRapiInit` 报 `0x8007007E`，先运行：

```bat
scripts\repair_wmdc_rapi.bat
```

该脚本自行请求 UAC，只把 5 个已知 WMDC/RAPI COM 类的旧 `%windir%` 注册改为现有
32/64 位 DLL 绝对路径；DLL、注册键或原值不符合预期时会拒绝修改。旧 CoreCon gate 为何
无需该修复、WMDC 已连接为何仍不能由 CoreCon 活动连接枚举发现，以及 `0x8007007E` 的完整
取证见[故障排查](TROUBLESHOOTING.md#wmdc-自动设备门不要混淆-corecon-与-rapi)。

### 手工设备门

人工包不是自动跑完即退出：每个 render 测试会停在屏幕上，必须由验收者看完并按
`Esc`（或点空白处）关闭。推荐按下面的顺序执行：

1. 先在 WMDC/Device Emulator GUI 中确认当前只有一个已连接设备；不要让脚本替你连接、
   选择或重置设备。
2. 关闭设备上已有的 `test_host.exe`，在仓库根目录执行：

   ```bat
   scripts\stage.bat Debug C:\WMShare\Positron-manual-next228
   ```

3. 在设备 File Explorer 打开 `Storage Card\Positron-manual-next228`（或共享目录映射的
   对应路径），确认 `test_host.exe` 与上表配置的 `test_host.ini` 在同一目录，然后运行
   `test_host.exe`。
4. 启动确认框必须显示这 13 个选择：
   `13, 20, 27, 56, 58, 62, 64, 65, 66, 67, 73, 75, 999`，并显示
   `Browser JavaScript: disabled.`。点击 **Yes**；点 **No** 会退回普通分组选择，不是本次流程。
5. 每项开始时先读 TEST 提示框，再点 OK 看 render 窗口。按照上表操作和观察，必要时截图；
   render 窗口用 `Esc` 或空白处关闭后才会进入下一项。TEST13 先截取 example.com
   初始页，再截取 `Learn More` 后和 IANA 页面，便于比较左右边距与居中容器。
6. TEST65/66/67 中实际点击控件；SIP 候选词必须选中完整词后再记录字段内容。若设备支持
   旋转，可在输入控件仍有值时旋转一次，确认控件没有漂移、值没有丢失，然后再关闭窗口。
7. 最后一项 TEST999 应只触发一次系统提示音，随后出现 `Configured tests passed`。如果
   出现 `FAIL`/`ERROR`、崩溃、黑屏、严重布局破坏或核心控件无法操作，立即停止，不要把
   后续项目当作通过。
8. `auto=0` 的交互路径不会创建 `test_host.log`；请保存截图、设备型号/viewport/DPI、
   操作步骤和异常描述到本地 `tmp/`。若还需要机器可判定的完整日志，应在人工记录完成后
   恢复自动三行配置，再单独运行自动 gate；自动日志不能替代人工截图。

若另行运行自动配置，日志至少应满足：

- 开头记录实际 screen 和 DPI；
- 所选测试都有预期的 `OK` 或明确的汇总记录；
- 没有非预期 `ERROR`；
- 没有 `FAIL`；
- 最终包含 `TESTBENCH PASS`；
- 网络 Browse 门完成了配置要求的页面序列，而不是只打开第一屏。

进程退出、提示音或看到部分 `OK` 都不能单独证明整批通过。

## 自动门分层

每个能力批次都要运行与风险直接相关的自动设备门。

低风险、局部变更可以运行：

- 本批新增测试；
- 直接共享的旧路径；
- TEST13 真实网页导航哨兵；
- TEST999 完成提示音。

满足以下任一情况时运行全量门：

- 累积了约五个低风险定向批次；
- 触及公共 DLL/ABI、布局/重绘、网络、输入基础设施；
- 准备交付一个里程碑；
- 出现超时、崩溃、数据错误、混包或无法解释的异常。

全量或定向选择不是稳定常量，始终以当前 handoff 和候选 ini 为准。

## 人工验收

以下风险不能由首帧自动测试替代：

- 字体 fallback、字形、抗锯齿和颜色；
- 左右边距、居中容器、换行、表格和列表观感；
- 真实触摸、链接命中、滚动和返回；
- SIP 候选词、IME composition、Unicode 输入；
- 旋转、不同 screen/DPI 和滚动位置保持；
- 失败网络、旧页保留和 loading 反馈；
- 真实页面的深层导航。

低风险视觉或输入变化可以累计若干批次后集中验收。遇到崩溃、数据损坏、严重布局破坏或
核心交互阻塞时必须立即人工复核，不能等待累计窗口。

人工截图和设备日志放在仓库本地 `tmp/`；该目录不得加入 Git。比较截图时必须记录页面、
viewport、DPI、方向、操作步骤和对应候选包，避免把偶然加载、旧 DLL 或不同滚动位置误认为
代码变化。

## 网络测试注意事项

- 运行证书测试前，把模拟器系统时钟调整到当前日期；默认旧时钟会让现役证书显示为尚未生效。
- 分清 DNS、TCP、TLS、证书、HTTP status、redirect 和页面提交阶段。
- 失败导航应保留旧页时，不能只看最终窗口是否仍有内容。
- TEST13 是回归哨兵，不等于互联网任意网站兼容性。

## 候选成为基线的条件

一个候选只有在以下条件都满足后才能写成设备基线：

1. 源码和配置范围清楚；
2. C89 回归与仓库审计通过；
3. ARMV4I 正式构建通过；
4. stage 包没有混用旧二进制；
5. 所需设备日志完整通过；
6. 本批要求的人工门已经完成，或明确进入允许累计的清单；
7. handoff、限制和路线图只更新各自职责内的事实。

如果日志不在当前工作区，仅有“已经跑完”不能替代对完整日志的读取和判定。

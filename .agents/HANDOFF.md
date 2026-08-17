# Positron 当前交接

更新时间：2026-08-17

稳定使命、架构和公共边界见
[`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。本文件只记录当前仓库基线、最近设备证据、
当前边界和唯一下一步。

## Git 与仓库基线

- 分支：`main`，跟踪 `origin/main`。
- 最新已验证产品基线：next250。
- next250 批次在 `positron_browser/` 产品 DLL 中让既有 SELECT typed dispatch callback ABI 同时承接 native SELECT input 事件；next249 批次加入 native SELECT change typed dispatch callback ABI；next248 的 focus-family typed dispatch callback ABI、next247 的 native keyboard typed dispatch callback ABI、next246 的 native input/composition typed dispatch callback ABI、next245 的同文档 history traversal/hash location 事件分发 API、next244 的 navigation callback typed registration、JSON
  分发和 session 生命周期、next243 的 form-property callback、next242 的 checked callback、next241 的 input value callback、next240 的 Event callback、next239 的 DOM attribute 三件套、next238 的 textContent 写入、next237 的 DOM 只读 callback、next236 的产品 bootstrap
  文本与求值入口、next235 的浏览器脚本 session 所有权与 host JSON callback 注册均保持通过；`test_host` 适配与工程接线继续使用当前
  WMDC/RAPI 会话的 `scripts/device_gate.bat`、`scripts/device_gate.ps1`，环境修复脚本为
  `scripts/repair_wmdc_rapi.*`。
- 当前工作区的 `test_host/test_host.ini` 保持自动模式：`auto=1`、`javascript=0`、
  `tests=13,20,27,56,58,62,64-67,73,75,999`。这是窄的自动 smoke 选择，不是完整基线；
- 165 项自动 next250 全量证据已经通过；人工视觉/输入包若需要弹窗，必须临时把 `auto` 改为 0，
  验收结束后恢复为 1。
- 本地设备证据位于 `tmp/device-runs/20260817-194231-next250-select-input-final/`；TEST217 定向证据位于
  `tmp/device-runs/20260817-193957-next250-select-input-stage-retry/`，SELECT/focus/key/input 回归证据位于
  `tmp/device-runs/20260817-194127-next250-select-input-regression-retry/`。next249 的 SELECT change 全量证据仍位于
  `tmp/device-runs/20260815-222840-next249-select-final/`；next248 的 focus 全量证据仍位于
  `tmp/device-runs/20260815-221705-next248-focus-final/`；TEST215 定向证据位于
  `tmp/device-runs/20260815-221554-next248-focus-stage/`，focus/key/input 回归证据位于
  `tmp/device-runs/20260815-221611-next248-focus-regression/`。next247 的 keyboard 全量证据仍位于
  `tmp/device-runs/20260815-154724-next247-key-final-retry/`；TEST214 定向证据位于
  `tmp/device-runs/20260815-154407-next247-key-stage/`，keyboard/input 回归证据位于
  `tmp/device-runs/20260815-154427-next247-key-regression/`。next246 的 input 全量证据仍位于
  `tmp/device-runs/20260815-152753-next246-input-final/`；TEST213 定向证据位于
  `tmp/device-runs/20260815-152635-next246-input-stage/`，input/script 回归证据位于
  `tmp/device-runs/20260815-152655-next246-input-regression/`。next245 的 location 证据仍位于
  `tmp/device-runs/20260815-150634-next245-location-final2/`；TEST212 定向证据位于
  `tmp/device-runs/20260815-150218-next245-location-stage2/`，location/history 回归证据位于
  `tmp/device-runs/20260815-150234-next245-location-regression/`。`tmp/` 不跟踪，干净 clone 中没有该日志，
  不能据此假定新环境也已经连接或通过。

接管时仍须重新运行 `git status --short --branch` 和 `git diff`；以上列表不是 Git 的替代品。

## 最近已验证设备证据

### 最新全量检查点：next250

- 配置：`TEST13/20/27/43/44/56/58-77/80-217/999`，共 165 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：165 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–216 的既有 product callback/location/input/key/focus/select 断言保持通过；TEST217 直接验证
  product SELECT input typed dispatch contract、坐标与冒泡字段、非法事件、adapter error 映射和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property、
  navigation、同文档 location/history 事件分发、Event JSON 分发、native input/composition、keyboard、
  focus-family 和 SELECT input/change dispatch entry；`test_host` 仅保留 WM 消息/控件、坐标命中、core/document/
  form/navigation/listener adapter 及窗口、网络、core 事件传播和控件默认副作用，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST217/999` 证据位于
  `tmp/device-runs/20260817-193957-next250-select-input-stage-retry/`，`TEST112-135/137-152/189-217/999`
  （70 项）位于 `tmp/device-runs/20260817-194127-next250-select-input-regression-retry/`，全量最终证据位于
  `tmp/device-runs/20260817-194231-next250-select-input-final/`。回归首次在 TEST134 遇到一次设备
  JavaScript timeout；TEST134/217/999 定向重跑和最终全量均通过，未调整执行预算或放宽断言。

### 最新全量检查点：next248

- 配置：`TEST13/20/27/43/44/56/58-77/80-215/999`，共 163 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：163 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–214 的既有 product callback/location/input/key 断言保持通过；TEST215 直接验证 product
  focus/blur/focusin/focusout typed dispatch contract、bubbles 字段、非法事件、adapter error 映射和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property、
  navigation、同文档 location/history 事件分发、Event JSON 分发、native input/composition、keyboard 和
  focus-family dispatch entry；`test_host` 仅保留 WM 消息/控件、坐标命中、core/document/form/navigation/
  listener adapter 及窗口、网络、core 事件传播和控件默认副作用，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST215/999` 证据位于
  `tmp/device-runs/20260815-221554-next248-focus-stage/`，`TEST112-135/137-152/189-215/999`
  （68 项）位于 `tmp/device-runs/20260815-221611-next248-focus-regression/`，全量最终证据位于
  `tmp/device-runs/20260815-221705-next248-focus-final/`。

### 最新全量检查点：next247

- 配置：`TEST13/20/27/43/44/56/58-77/80-214/999`，共 162 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：162 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–213 的既有 product callback/location/input 断言保持通过；TEST214 直接验证 product
  native keyboard typed dispatch contract、keydown/keyup 字段、取消、adapter error 映射和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property、
  navigation、同文档 location/history 事件分发、Event JSON 分发、native input/composition 和 keyboard
  dispatch entry；`test_host` 仅保留 WM 消息/控件、坐标命中、core/document/form/navigation/listener
  adapter 及窗口、网络、core 事件传播和控件默认副作用，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST214/999` 证据位于
  `tmp/device-runs/20260815-154407-next247-key-stage/`，`TEST112-135/137-152/189-214/999`
  （67 项）位于 `tmp/device-runs/20260815-154427-next247-key-regression/`，全量最终证据位于
  `tmp/device-runs/20260815-154724-next247-key-final-retry/`。首次全量在 TEST117 出现一次设备
  JavaScript timeout；TEST117/999 定向重跑及最终全量重试通过，未调整执行预算或放宽断言。

### 最新全量检查点：next246

- 配置：`TEST13/20/27/43/44/56/58-77/80-213/999`，共 161 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：161 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–212 的既有 product callback/location 断言保持通过；TEST213 直接验证 product native
  input/composition typed dispatch contract、取消结果、adapter error 映射和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property、
  navigation、同文档 location/history 事件分发、Event JSON 分发和 native input/composition dispatch
  entry；`test_host` 仅保留 core/document/form/navigation/listener/native-control adapter 及窗口、网络、
  core 事件传播和 history/navigation side effect，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST213/999` 证据位于
  `tmp/device-runs/20260815-152635-next246-input-stage/`，`TEST112-135/137-152/189-213/999`
  （66 项）位于 `tmp/device-runs/20260815-152655-next246-input-regression/`，全量最终证据位于
  `tmp/device-runs/20260815-152753-next246-input-final/`。

### 最新全量检查点：next245

- 配置：`TEST13/20/27/43/44/56/58-77/80-212/999`，共 160 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：160 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–211 的既有 product callback 断言保持通过；TEST212 直接验证 product same-document
  traversal/hash location dispatch、popstate/hashchange 顺序、状态更新和临时 global 清理。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property、
  navigation、同文档 location/history 事件分发和 Event JSON 分发；`test_host` 仅保留 typed
  core/document/form/navigation/listener adapter 及窗口、网络、history/navigation side effect，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST212/999` 证据位于
  `tmp/device-runs/20260815-150218-next245-location-stage2/`，`TEST112-135/137-152/189-212/999`
  （65 项）位于 `tmp/device-runs/20260815-150234-next245-location-regression/`，全量最终证据位于
  `tmp/device-runs/20260815-150634-next245-location-final2/`。
- 全量首尝在 TEST166 出现一次 JavaScript timeout；TEST166/212 定向重跑通过，最终全量重试 160/160
  通过，未修改执行预算或放宽断言。

### 最新全量检查点：next244

- 配置：`TEST13/20/27/43/44/56/58-77/80-211/999`，共 159 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：159 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–210 的既有 product callback 断言保持通过；TEST211 直接验证 product navigation JSON
  dispatch、十种 operation kind、URL/state/delta 编码、pushState 返回值、非法输入和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property、
  navigation 和 Event JSON 分发；`test_host` 仅保留 typed core/document/form/navigation/listener
  adapter 及窗口、网络、history side effect，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST211/999` 证据位于
  `tmp/device-runs/20260815-143748-next244-navigation-stage2/`，`TEST112-135/137-152/189-211/999`
  （64 项）位于 `tmp/device-runs/20260815-143813-next244-navigation-regression2/`，全量最终证据位于
  `tmp/device-runs/20260815-144551-next244-navigation-final4/`。
- 全量中途曾出现 TEST129、TEST153、TEST192 的单次 JavaScript timeout；各自前后定向回归均通过，
  最终全量重试 159/159 通过，未修改执行预算或放宽断言。

### 最新全量检查点：next243

- 配置：`TEST13/20/27/43/44/56/58-77/80-210/999`，共 158 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：158 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204–209 的既有 product callback 断言保持通过；TEST210 直接验证 product form-property JSON
  dispatch、defaultValue/defaultChecked/selectedIndex typed adapters、缺失/非法参数和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked、form-property
  和 Event JSON 分发；`test_host` 仅保留 typed core/document/value/checked/form-property/listener
  adapter，其余 form/input/location/navigation callback 实现仍在宿主，产品 session 是唯一销毁者。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST210/999` 证据位于
  `tmp/device-runs/20260815-140531-next243-form-stage/`，`TEST112-135/137-152/189-210/999`
  （63 项）位于 `tmp/device-runs/20260815-140551-next243-form-regression/`，全量最终重试证据位于
  `tmp/device-runs/20260815-140823-next243-form-final-retry/`。
- 全量第一次尝试在 TEST13 因网络 `header block read failed` 停止，未执行后续测试；重试后完整通过，
  未修改预算或放宽断言。

### 最新全量检查点：next242

- 配置：`TEST13/20/27/43/44/56/58-77/80-209/999`，共 157 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=320x320 dpi=128`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：157 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`，`completion_marker=PASS`，`test13_route_ok=True`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST204 直接验证 product DOM read JSON dispatch；TEST205 直接验证 product DOM write JSON
  dispatch；TEST206 直接验证 product DOM attribute JSON dispatch、typed get/set/remove adapters 和注销；
  TEST207 直接验证 product Event JSON registration/dispatch、typed add/remove adapters、事件数据和
  preventDefault 结果；TEST208 直接验证 product DOM value JSON dispatch、typed get/set adapters、
  缺失目标、非法参数和注销；TEST209 直接验证 product DOM checked JSON dispatch、typed get/set
  adapters、缺失目标、非法参数和注销。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期、browser
  bootstrap 文本/求值入口、DOM 只读、textContent 写入、attribute、input value、checked 和 Event JSON
  分发；`test_host` 仅保留 typed core/document/value/checked/listener adapter，defaultValue/selectedIndex、
  其余 form/input/location/navigation callback 实现仍在宿主，产品 session 是唯一销毁者，下一批迁移
  defaultValue/selectedIndex、其余表单输入和导航 callback。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I Debug
  正式构建、隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接 `TEST209/999` 证据位于
  `tmp/device-runs/20260815-134414-next242-checked-stage/`，`TEST112-135/137-152/189-209/999`
  （62 项）位于 `tmp/device-runs/20260815-134433-next242-checked-regression/`，全量证据位于
  `tmp/device-runs/20260815-134848-next242-checked-final2/`。

### 上一全量检查点：next236

- 配置：`TEST13/20/27/43/44/56/58-77/80-203/999`，共 151 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：151 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST203 直接验证 product bootstrap 的创建、求值和 document/location/history 对象持久性。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context、callback 注册/调用生命周期和
  browser bootstrap 文本/求值入口；`test_host` 的 bridge 仍保留 core/窗口/导航适配及 DOM/Event/
  form/input/location callback 实现，产品 session 是唯一销毁者，下一批迁移 callback 实现。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、15 项隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接
  `TEST203/999` 证据位于 `tmp/device-runs/20260815-100256-next236-browser-bootstrap/`，
  脚本回归 `TEST112-135/137-152/189-203/999`（56 项）位于
  `tmp/device-runs/20260815-100313-next236-browser-bootstrap-regression/`，全量证据位于
  `tmp/device-runs/20260815-100357-next236-browser-bootstrap-final/`。

### 上一全量检查点：next235

- 配置：`TEST13/20/27/43/44/56/58-77/80-202/999`，共 150 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：150 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`；
  TEST202 直接验证 product script session 的创建、求值、host JSON callback 注册/注销和销毁。
- 产品边界：`positron_browser.dll` 现在拥有 PScript context 及其 callback 注册/调用生命周期；
  `test_host` 的 bridge 只保留 core/窗口/导航适配和一个只读诊断 runtime 借用句柄，产品 session
  是唯一销毁者。bootstrap 文本和 DOM/Event/form/input/location callback 实现仍在宿主，下一批迁移。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、15 项隔离 staging/部署、SHA-256 清单和日志自动判门均通过。直接
  `TEST202/999` 证据位于 `tmp/device-runs/20260815-094753-next235-script-session/`，
  脚本回归 `TEST112-135/137-152/189-202/999`（55 项）位于
  `tmp/device-runs/20260815-094818-next235-script-session-regression/`，全量证据位于
  `tmp/device-runs/20260815-095055-next235-script-session-final/`。

### 上一全量检查点：next234

- 配置：`TEST13/20/27/43/44/56/58-77/80-201/999`，共 149 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：149 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`。
- 产品边界：新增 `positron_browser.dll`，以独立 opaque history/session handle 拥有
  history entries、state、document identity、same-origin/default-port 判断、文档导航提交、
  push/replace state、traversal target 和 pending-navigation projection。`test_host` 的旧
  history 调用现在只通过该 DLL，固定数组仅作断言镜像；TEST201 直接调用公共 API。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、15 项隔离 staging/部署、SHA-256 清单和日志自动判门均通过。定向
  `TEST201/999` 证据位于 `tmp/device-runs/20260814-234503-next234-browser-history/`，
  `TEST149-201/999` 证据位于 `tmp/device-runs/20260814-234517-next234-history201/`，
  参数边界定向证据位于 `tmp/device-runs/20260814-235415-next234-browser-history-contract/`，
  修订后全量证据位于 `tmp/device-runs/20260814-235642-next234-browser-history-final2/`。

### 更早全量检查点：next233

- 配置：`TEST13/20/27/43/44/56/58-77/80-200/999`，共 148 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：148 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`。
- 能力终点：`history.pushState`/`replaceState` 的第三个 URL 参数显式为 `undefined` 时使用当前
  document URL；显式空字符串仍表示当前 URL 的同 URL history entry。两种写法都不发 GET，保留
  state/length、traversal、popstate/hashchange 顺序和后续 replace/push 行为；既有安全
  absolute/root-relative/document-relative pathname、query/fragment 和拒绝规则不变。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、14 文件隔离 staging/部署、SHA-256 清单和日志自动判门均通过。定向
  `TEST149-200/999` 证据位于 `tmp/device-runs/20260814-231852-next233-history200/`，
  全量证据位于 `tmp/device-runs/20260814-232021-next233-final/`。

### 更早全量检查点：next232

- 配置：`TEST13/20/27/43/44/56/58-77/80-199/999`，共 147 项。
- 环境：WMDC 当前连接的 Microsoft DeviceEmulator，`screen=640x480 dpi=192`。
- 通道：32 位 RAPI 直接消费 WMDC 当前设备；没有枚举/绑定 VMID，也没有连接、选择、启动、
  Cradle、断开或重置设备。RAPI 1 不提供可靠远端退出码，完成依据为完整日志标记。
- 结果：147 个选中测试 ID 全部有 `OK`，TEST13 overview/box detail 完整；零 `ERROR`、
  零 `FAIL`、唯一 `TESTBENCH PASS`，`completion_marker=PASS`。
- TEST13：example.com、IANA Example Domains、Reserved Domains 三段导航均 `completed=1`。
- 能力终点：`history.pushState`/`replaceState` 支持安全的同源 absolute/root-relative pathname，以及同源根相对
  path/query/fragment、query-relative URL、裸单段/多段 sibling 和显式 `./` 单段/多段 sibling URL；显式
  `./?query`/`./#fragment` 会落到当前目录的 trailing-slash URL，且同文档 traversal
  恢复 URL/state 并按 popstate 后 hashchange 排序；裸 `./`、`../`、dot segment、
  重复分隔符、编码 dot segment、protocol-relative 和跨源 URL 仍拒绝；普通 percent-encoded
  pathname segment 可以保留；同源 absolute/root-relative URL 在
  path 完全相同的前提下可以更新 query/fragment，HTTP 默认端口 80 与 HTTPS 默认端口
  443 在同源比较中按无端口形式等价处理。
- 自动证据：`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、VS2008 ARMV4I
  Debug 正式构建、14 文件隔离 staging/部署、SHA-256 清单和日志自动判门均通过。定向
  `TEST149-199/999` 证据和默认全量 gate 均已保存到上述 `tmp/device-runs/` 路径。
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
- 2026-08-14 的 TEST75 纵向/横向截图（`tmp/QQ20260814-195629.png`、
  `tmp/QQ20260814-195643.png`）显示灰色定位父框和四个彩色元素均在预期坐标内；
  小色块内文字的溢出是 fixture 的预期内容，不是布局回归。
- 视觉、真实触摸、SIP、旋转和失败网络允许累计后集中复核；崩溃、数据损坏、严重布局
  破坏或核心交互阻塞必须立即检查。

## 已关闭批次：next232

目标：让根相对 history state URL 与既有安全 absolute/document-relative pathname 使用同一套
路径段规则；继续在不发 GET 的前提下保留普通 percent-encoded pathname segment，并保持
state、length、traversal、popstate/hashchange 行为可预测。编码 dot segment、重复分隔符、
跨源和 protocol-relative URL 仍明确拒绝。

实现边界：

- JS bootstrap 的 `phistoryRelativePath` 保留 raw dot segment、重复分隔符和父路径拒绝；
  根相对 URL 先剥离 query/fragment，再复用同一校验，普通 percent-encoded segment（例如
  `%2Ebook`、`file%2Ejson`、`%2Fencoded`）仍可保留，但完整编码/混合编码的 `.`、`..`
  segment 仍拒绝。
- C 侧 history bridge 未扩大 URL parser，只沿用已验证的同源判定和 HTTP `:80`、HTTPS `:443`
  默认端口等价规则，把安全 pathname 结果写入现有历史条目。
- TEST149、TEST189–194、TEST195、TEST196 的旧“任意 absolute path 都拒绝”负例继续覆盖
  真正不安全的 dot/repeated-separator path；TEST197 覆盖安全 pathname replace/push；
  TEST198 新增普通 percent-encoded segment replace/push、无 GET、state/length、traversal、
  popstate/hashchange 和编码 dot/cross-origin 拒绝覆盖；TEST199 覆盖根相对与
  document-relative 普通编码段、根相对不安全路径拒绝和同样的 traversal 事件门。
- 默认 `javascript=0`、TEST13 行为、公共 ABI 和 callback 总上限不变。

自动化同步完成：

- `device_gate.ps1` 使用 WMDC 当前 RAPI 会话，不依赖 CoreCon 活动连接或 VMID；支持
  `-TestSelection` 只覆盖隔离 staging 的测试选择。
- gate 只回收 `\Temp\Positron-device-gate` 下符合自身 candidate/timestamp 规则的旧目录；
  未识别目录保留。
- `repair_wmdc_rapi.bat/.ps1` 自行请求 UAC，幂等修复 5 个已知 WMDC RAPI COM 类的 32/64
  位旧 `%windir%` 路径；未知注册值拒绝修改。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- PowerShell 解析、修复脚本 `changed=0/status=PASS` 幂等门；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST149-199/999` 和默认 147 项全量 WMDC/RAPI 设备门；
- TEST13 三段真实导航、零 `ERROR`、零 `FAIL`、唯一 `TESTBENCH PASS`。

## 已关闭批次：next233

目标：补齐 `history.pushState`/`replaceState` 可选 URL 参数的一个明确边界：调用者显式传入
`undefined` 时默认当前 document URL，同时保持显式空字符串的同 URL entry 语义，并证明两者
都不会触发网络请求或破坏 history traversal。

实现边界：

- 浏览器脚本 bridge 只在第三个参数确实不是 `undefined` 时字符串化 URL；缺省/显式
  `undefined` 走当前 URL，显式 `''` 继续走当前 URL 的同 URL entry；未扩大 URL parser 或
  放宽既有安全路径拒绝规则。
- TEST200 覆盖 replace/push 返回值、location/document.URL、history length/state、bridge
  URL/state、无 GET、back/forward 的 popstate 顺序，以及 traversal 后再次 replace/push；
  TEST198/199 和上一批测试继续覆盖普通编码段、根相对路径与不安全 URL 拒绝。
- 默认 `javascript=0`、TEST13 行为、公共 ABI 和 callback 总上限不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST149-200/999`（53 项）和全量 `TEST13/20/27/43/44/56/58-77/80-200/999`
  （148 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、零 `ERROR`、零 `FAIL`、唯一 `TESTBENCH PASS`。

## 已关闭批次：next234

目标：建立正式浏览器产品组合层，先把无窗口、无网络、无 JavaScript 依赖的 history/session
状态机从 `test_host/main.c` 迁入 `positron_browser.dll`，并让宿主通过公共 C ABI 消费它。

实现边界：

- 新增 `positron_browser.dll`、`positron_browser.h` 和 VS2008 ARMV4I 工程；它只依赖
  `positron_json.dll` 验证 JSON state，不依赖 DOM、脚本、网络或 WM 控件。
- 公共 ABI 使用 UTF-8、opaque history handle、明确销毁函数和借用字符串生命周期；覆盖
  document commit/replace/traversal、push/replace state、same-origin/default-port、
  history length/index/state 投影和独立 document identity。
- `test_host` 的旧 history 函数保留为兼容适配与断言镜像，产品状态不再由宿主数组维护；
  TEST201 绕过适配层直接验证 DLL。JS bootstrap、DOM/Event/form bridge 仍未迁移，下一批处理。
- stage、device gate、test_host 工程和所有面向读者文档已包含第七个产品 DLL；默认
  `javascript=0`、TEST13、公共 core/script ABI 和既有人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST201/999`（2 项）、`TEST149-201/999`（54 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-201/999`（149 项）WMDC/RAPI 设备门；
- 参数边界/非法 JSON 的最新定向 `TEST201/999` 证据位于
  `tmp/device-runs/20260814-235415-next234-browser-history-contract/`；
- TEST13 三段真实导航、零 `ERROR`、零 `FAIL`、唯一 `TESTBENCH PASS`。

## 已关闭批次：next235

目标：把浏览器 JavaScript session 的 PScript context 所有权、host JSON callback 注册/调用和
销毁生命周期从 `test_host/main.c` 收拢到 `positron_browser.dll`，同时保持现有宿主适配边界。

实现边界：

- `positron_browser.dll` 新增 `PBrowser_ScriptSession*` opaque API，持有一个 PScript context，
  负责求值、全局值、JSON callback 注册/注销/调用、结果/错误读取和 native function count；
  `PBrowser_ScriptSessionRuntime` 仅作为迁移期只读诊断借用句柄。
- `test_host` 的 bridge 通过 product session API 驱动既有脚本路径；窗口、core document、导航、
  DOM/Event/form/input/location callback 实现和 bootstrap 文本仍在宿主，下一批再迁。
- TEST202 直接调用 product script session API，覆盖 context 持久求值、host callback、注销、
  参数错误和产品所有权；默认 `javascript=0`、TEST13、公共 core/script ABI 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST202/999`（2 项）、`TEST112-135/137-152/189-202/999`（55 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-202/999`（150 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST202 product session、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next236

目标：把浏览器 bootstrap 文本及其产品求值入口从 `test_host/main.c` 迁入
`positron_browser.dll`，让宿主只负责安装 `__pcore*` globals、JSON callback 和 core/窗口/导航
适配，不再拥有浏览器对象初始化脚本。

实现边界：

- `positron_browser.dll` 新增 `PBrowser_ScriptSessionEvaluateBootstrap`，持有 browser
  `window`、`document`、`location`、`history`、事件和表单对象的 bootstrap 文本；该入口复用
  已迁入的 product script session，不拥有 core document、native controls 或 host callback pw。
- `test_host` 的 bootstrap 常量已删除，既有 callback 注册和页面执行路径改为调用公共 bootstrap
  API；DOM/Event/form/input/location/navigation callback 实现仍在宿主，下一批迁移。
- TEST203 直接验证 product bootstrap 的参数错误、持久 session、document URL、location、
  history length/state 和 native callback 计数；默认 `javascript=0`、TEST13、公共 ABI 和
  人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST203/999`（2 项）、`TEST112-135/137-152/189-203/999`（56 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-203/999`（151 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST203 product bootstrap、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next237

目标：把 `__pcoreHasElement` 与 `__pcoreGetText` 的 DOM 只读 JSON callback 分发从
`test_host/main.c` 迁入 `positron_browser.dll`，让宿主只提供 typed document adapter，同时保持
现有脚本、页面和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptDomReadCallbacks` 及注册/注销 API，负责
  解析 `{id}` 参数、调用 typed `has_element`/`get_text`、处理负错误码、probe/精确分配和 JSON
  bool/string 结果编码；DOM read 绑定随 script session 创建和销毁。
- `test_host` 删除两套 DOM read JSON 实现，只保留 `PCore_NodeExistsById`、
  `PCore_NodeTextContentById` 的 typed adapter；DOM 写入/Event/form/input/location/navigation
  callback 仍留在宿主，未扩大本批范围。
- TEST204 直接验证参数错误、typed callback 注册、native callback 数量、bootstrap 读取、缺失
  元素和注销；默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST204/999`（2 项）、`TEST189-204/999`（17 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-204/999`（152 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST204 product DOM read、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next238

目标：把 `__pcoreSetText` 的 DOM textContent 写入 JSON callback 分发从 `test_host/main.c`
迁入 `positron_browser.dll`，让宿主只提供 typed document adapter，同时保持现有 bootstrap、
页面脚本和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptDomWriteCallbacks` 及注册/注销 API，
  负责解析 `{id,text}` 参数、调用 typed `set_text`、区分成功/目标缺失/adapter error 并编码
  JSON bool 结果；DOM write 绑定随 script session 创建和销毁。
- `test_host` 删除 `__pcoreSetText` 的 JSON 实现，只保留 `PCore_NodeSetTextContentById` 的
  typed adapter；attribute、value、checked、Event/form/input/location/navigation callback 仍留
  在宿主，未扩大本批范围。
- TEST205 直接验证参数错误、typed callback 注册、native callback 数量、成功/缺失目标结果、
  callback 状态更新和注销；默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST205/999`（2 项）、`TEST112-135/137-152/189-205/999`（58 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-205/999`（153 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST205 product DOM write、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next239

目标：把 `__pcoreGetAttribute`、`__pcoreSetAttribute` 和 `__pcoreRemoveAttribute` 的 DOM
attribute JSON callback 分发从 `test_host/main.c` 迁入 `positron_browser.dll`，让宿主只提供
typed document adapter，同时保持现有 bootstrap、页面脚本和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptDomAttributeCallbacks` 及注册/注销 API，
  负责解析 `{id,name,value}` 参数、处理 getter 的 `null`/字符串结果、调用 typed get/set/remove
  adapter、校验 probe 长度并编码 JSON 结果；attribute 绑定随 script session 创建和销毁。
- `test_host` 删除三个 attribute JSON 实现，只保留 `PCore_NodeAttributeById`、
  `PCore_NodeSetAttributeById`、`PCore_NodeRemoveAttributeById` 的 typed adapter；Event/form/input/
  location/navigation callback 仍留在宿主，未扩大本批范围。
- TEST206 直接验证参数错误、typed callback 注册、native callback 数量、属性缺失/设置/读取/删除、
  setter false 语义和注销；默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST206/999`（2 项）、`TEST112-135/137-152/189-206/999`（59 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-206/999`（154 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST206 product DOM attribute、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next240

目标：把 `__pcoreAddEvent`、`__pcoreRemoveEvent` 的事件注册/注销 JSON 分发和原生事件数据编码
从 `test_host/main.c` 迁入 `positron_browser.dll`，让宿主只提供 core/document/listener typed
adapter，同时保持现有 bootstrap、页面脚本和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptEventCallbacks` 和
  `PBrowserScriptEventInfo`，负责验证事件类型、解析注册/注销参数、返回宿主 listener token，
  并把同步事件元数据编码为 `__pcoreDispatchEvent` JSON；`preventDefault` 结果通过稳定 bitmask
  返回，事件绑定随 script session 创建和销毁。
- `test_host` 删除 `__pcoreAddEvent`/`__pcoreRemoveEvent` 的 JSON 实现和事件 JSON 编码，只保留
  `PCore_EventListenerAdd/Remove`、listener 生命周期和 `PCoreEventInfo` 到 product typed event
  info 的适配；form/input/location/navigation callback 仍留在宿主，未扩大本批范围。
- TEST207 直接验证参数错误、typed callback 注册/重复注册/注销、native callback 数量、事件字段
  编码、handler 发现和 `preventDefault` action；默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST207/999`（2 项）、`TEST112-135/137-152/189-207/999`（60 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-207/999`（155 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST207 product Event、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next241

目标：把 `__pcoreGetValue`、`__pcoreSetValue` 的 input value JSON 分发从
`test_host/main.c` 迁入 `positron_browser.dll`，让宿主只提供 core/document value typed
adapter，同时保持现有 bootstrap、页面脚本和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptDomValueCallbacks`，负责解析 value
  读写参数、执行 UTF-8 size-probe/结果编码，并在 script session 创建、注销和销毁时管理两个
  native globals。
- `test_host` 删除 value JSON 解析和编码，只保留 `PCore_NodeValueById`、
  `PCore_NodeSetValueById` 到 product typed value adapter 的转换；checked/defaultValue/
  selectedIndex、其余 form/input/location/navigation callback 仍留在宿主，未扩大本批范围。
- TEST208 直接验证参数错误、typed callback 注册/重复注册/注销、native callback 数量、值读取、
  更新、缺失目标和非法参数；默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST208/999`（2 项）、`TEST112-135/137-152/189-208/999`（61 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-208/999`（156 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST208 product DOM value、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。

## 已关闭批次：next242

目标：把 `__pcoreGetChecked`、`__pcoreSetChecked` 的 checked JSON 分发从
`test_host/main.c` 迁入 `positron_browser.dll`，让宿主只提供 core/document checked typed
adapter，同时保持现有 bootstrap、页面脚本和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptDomCheckedCallbacks`，负责解析 checked
  读写参数、规范化布尔值、结果编码，并在 script session 创建、注销和销毁时管理两个 native globals。
- `test_host` 删除 checked JSON 解析和编码，只保留 `PCore_NodeCheckedById`、
  `PCore_NodeSetCheckedById` 到 product typed checked adapter 的转换；defaultChecked/defaultValue/
  selectedIndex、其余 form/input/location/navigation callback 仍留在宿主，未扩大本批范围。
- TEST209 直接验证参数错误、typed callback 注册/重复注册/注销、native callback 数量、checked 读取、
  更新、缺失目标和非法参数；默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST209/999`（2 项）、`TEST112-135/137-152/189-209/999`（62 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-209/999`（157 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST209 product DOM checked、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；最新证据路径见本文件顶部。TEST144 曾有一次设备执行超时，单测重跑及最终全量均通过，
  未调整执行预算。

## 已关闭批次：next243

目标：把 `__pcoreFormProperty` 的 form-property JSON 分发从 `test_host/main.c` 迁入
`positron_browser.dll`，让宿主只提供 defaultValue、defaultChecked、selectedIndex 三组
core/document typed adapter，同时保持既有 bootstrap、页面脚本、native callback 数量和 core ABI 行为不变。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptFormCallbacks`，由产品层解析并编码
  `getDefaultValue`/`setDefaultValue`、`getDefaultChecked`/`setDefaultChecked`、
  `getSelectedIndex`/`setSelectedIndex` 六种操作；一个 `__pcoreFormProperty` global 在 session
  注册、注销和销毁时由产品层完整管理。
- `test_host` 删除 form-property JSON 解析和编码，只保留
  `PCore_NodeDefaultValueById`、`PCore_NodeSetDefaultValueById`、
  `PCore_NodeDefaultCheckedById`、`PCore_NodeSetDefaultCheckedById`、
  `PCore_NodeSelectedIndexById`、`PCore_NodeSetSelectedIndexById` 到 product typed adapter 的转换；
  其余 form/input/location/navigation callback 仍留在宿主，未扩大本批范围。
- TEST210 直接验证参数错误、typed callback 注册/重复注册/注销、单一 native callback 数量、六种
  form-property 操作、缺失目标、非法操作和资源关闭；既有 TEST112–209 的 native callback 数量断言保持 14。
  默认 `javascript=0`、TEST13 和人工验收流程不变。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST210/999`（2 项）、`TEST112-135/137-152/189-210/999`（63 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-210/999`（158 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST210 product form-property、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；完整证据路径见本文件顶部。全量首尝因 TEST13 网络瞬态失败，重试通过，未
  调整执行预算。

## 已关闭批次：next244

目标：把 `__pcoreNavigation` 的 JSON 分发从 `test_host/main.c` 迁入
`positron_browser.dll`，让产品层拥有 navigation 参数解析、结果编码和 session 生命周期；宿主只
提供 typed navigation adapter，继续保留 history、窗口、网络和导航副作用。

实现边界：

- `positron_browser.dll` 新增 size-tagged `PBrowserScriptNavigationCallbacks` 及注册/注销 API，
  负责 `replaceState`、`pushState`、`back`、`forward`、`go`、`assign`、`reload`、`replace`、
  `fragment`、`fragmentReplace` 十种 operation 的 JSON 参数校验、URL/state/delta 编码、返回值
  编码和 native global 的完整生命周期管理。
- `test_host` 删除 navigation JSON 解析和编码，只保留既有 history/base-origin/窗口副作用适配，
  通过 typed `PBrowserScriptNavigationInfo` 接收产品层结果；公共 core/script ABI、既有 URL 政策、
  native callback 数量和页面行为不变。
- TEST211 直接验证产品 navigation JSON dispatch、十种 operation、pushState 返回 history length、
  非法参数、未知 operation、重复注册和注销；既有 TEST112–210 回归保持通过。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST211/999`（2 项）、`TEST112-135/137-152/189-211/999`（64 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-211/999`（159 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST211 product navigation、零 `ERROR`、零 `FAIL`、唯一 `TESTBENCH PASS`；
  完整证据路径见本文件顶部。全量中途曾出现 TEST129、TEST153、TEST192 的单次 JavaScript
  timeout；定向回归及最终全量重试通过，未调整执行预算或放宽断言。

## 已关闭批次：next250

目标：把 native SELECT 的 `input` 事件分发入口接到已存在的
`positron_browser.dll` SELECT typed callback ABI；宿主继续拥有控件生命周期、坐标命中、core
事件传播和 SELECT 的原生默认行为。

实现边界：

- `positron_browser.dll` 扩展 `PBrowserScriptSelectEventInfo` 的事件契约，允许 `input` 与既有
  `change`，并继续由 `PBrowser_ScriptSessionDispatchSelectEvent` 负责参数校验、同步 callback
  和 adapter-error 映射；没有新增 native callback 槽位或改变旧 ABI 的结构布局。
- `test_host` 的 native SELECT input 路径改为构造产品 typed info；宿主 adapter 仍只把产品请求
  转成 `PCore_EventDispatchAt`，保留坐标命中、冒泡字段、WM 控件同步和 input/change 默认行为。
- TEST217 直接验证 input 字段、注册/重复注册、非法事件、adapter error、注销和资源关闭；既有
  TEST216 的 change 契约保持通过，没有迁移 EDIT change、WM 消息、控件默认行为或表单提交。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST217/999`（2 项）、`TEST112-135/137-152/189-217/999`（70 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-217/999`（165 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST217 product SELECT input dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。回归首尝 TEST134 出现一次 JavaScript
  timeout，定向重跑和最终全量通过，未调整预算或放宽断言。

## 已关闭批次：next249

目标：把 native SELECT 的 `change` 事件分发入口从 `test_host/main.c` 的直接 core 调用迁入
`positron_browser.dll` 的 typed callback ABI；宿主继续拥有控件生命周期、坐标命中、core 事件传播
和 SELECT 的原生默认行为。

实现边界：

- `positron_browser.dll` 新增 `PBrowserScriptSelectEventInfo`、`PBrowserScriptSelectCallbacks`、
  注册/注销 API 和 `PBrowser_ScriptSessionDispatchSelectEvent`；产品层只接受 `change` 事件，
  同步调用宿主 adapter 并统一 adapter-error 结果。
- `test_host` 的 native SELECT change 路径改为构造产品 typed info；宿主 adapter 只把产品请求
  转成 `PCore_EventDispatchAt`，保留坐标命中、冒泡字段、WM 控件同步和 input/change 默认行为。
- TEST216 直接验证注册/重复注册、change 字段、非法事件、adapter error、注销和资源关闭；没有
  迁移 EDIT change、SELECT input、WM 消息、控件默认行为或表单提交。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST216/999`（2 项）、`TEST112-135/137-152/189-216/999`（69 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-216/999`（164 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST216 product SELECT change dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next248

目标：把 native EDIT/SELECT 的 `focus`、`blur`、`focusin` 和 `focusout` 事件分发入口从
`test_host/main.c` 的直接 core 调用迁入 `positron_browser.dll` 的 typed callback ABI；宿主继续
拥有 WM 焦点消息、控件生命周期、坐标命中、core 事件传播和 SIP/控件默认行为。

实现边界：

- `positron_browser.dll` 新增 `PBrowserScriptFocusEventInfo`、`PBrowserScriptFocusCallbacks`、
  注册/注销 API 和 `PBrowser_ScriptSessionDispatchFocusEvent`；产品层限制事件类型为四种
  focus-family 事件，同步调用宿主 adapter 并统一 adapter-error 结果。
- `test_host` 的 native EDIT/SELECT 焦点路径改为构造产品 typed info；宿主 adapter 只把产品请求
  转成 `PCore_EventDispatchAt`，保留 focus/blur 非冒泡和 focusin/focusout 冒泡语义。
- TEST215 直接验证注册/重复注册、四种事件字段、非法事件、adapter error、注销和资源关闭；
  没有迁移 WM 焦点消息、SIP、SELECT 默认行为或表单提交。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST215/999`（2 项）、`TEST112-135/137-152/189-215/999`（68 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-215/999`（163 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST215 product focus-family dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next247

目标：把 native EDIT/SELECT 的 `keydown`、`keyup` 和 `keypress` 键盘事件分发入口从
`test_host/main.c` 的直接 core 调用迁入 `positron_browser.dll` 的 typed callback ABI；宿主继续
拥有 WM 消息翻译、控件坐标命中、core 事件传播和焦点/控件默认行为。

实现边界：

- `positron_browser.dll` 新增 `PBrowserScriptKeyEventInfo`、`PBrowserScriptKeyCallbacks`、
  注册/注销 API 和 `PBrowser_ScriptSessionDispatchKeyEvent`；产品层校验事件契约、同步调用
  宿主 adapter，统一 default-allowed 和 adapter-error 结果。
- `test_host` 的 EDIT/SELECT 键盘路径改为构造产品 typed info；宿主 adapter 只把产品请求转成
  `PCore_EventDispatchKeyExAt`，保留 key/keyCode/charCode、repeat、修饰键和 composing 字段。
- TEST214 直接验证注册/重复注册、keydown/keyup 字段、取消、adapter error、注销和资源关闭；
  没有迁移焦点、SELECT 默认行为、表单提交或 WM 消息处理。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST214/999`（2 项）、`TEST112-135/137-152/189-214/999`（67 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-214/999`（162 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST214 product native keyboard dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next246

目标：把 native 文本输入产生的 `beforeinput`、`input` 和 composition 事件入口从
`test_host/main.c` 的直接 core 调用迁入 `positron_browser.dll` 的 typed callback ABI；宿主继续
拥有 WM EDIT/IME、坐标命中、core 事件传播和 native 默认行为。

实现边界：

- `positron_browser.dll` 新增 `PBrowserScriptInputEventInfo`、
  `PBrowserScriptInputCallbacks`、注册/注销 API 和
  `PBrowser_ScriptSessionDispatchInputEvent`；产品层校验事件契约、同步调用宿主 adapter，
  统一 default-allowed 和 adapter-error 结果。
- `test_host` 的文本输入/composition 路径改为构造产品 typed info；宿主 adapter 只把产品请求
  转成 `PCore_EventDispatchInputExAt`，既有 WM 控件、IME、取消和页面脚本语义不变。
- TEST213 直接验证注册/重复注册、beforeinput 与 composition 字段、取消、adapter error、注销
  和资源关闭；没有迁移键盘、焦点、SELECT 或表单提交副作用。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST213/999`（2 项）、`TEST112-135/137-152/189-213/999`（66 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-213/999`（161 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST213 product native input/composition dispatch、零 `ERROR`、零 `FAIL`、
  唯一 `TESTBENCH PASS`；完整证据路径见本文件顶部。

## 已关闭批次：next245

目标：把同文档 history traversal 和 hash location 事件分发从 `test_host/main.c` 的临时全局+
源码求值迁入 `positron_browser.dll`，让产品层拥有脚本 context 内的 `popstate`/`hashchange`
状态更新和事件顺序；宿主继续拥有 history 提交/回滚、窗口、网络和导航副作用。

实现边界：

- `positron_browser.dll` 新增 `PBrowser_ScriptSessionDispatchHistoryTraversal` 和
  `PBrowser_ScriptSessionDispatchHashNavigation` 公共 API，负责临时状态注入、产品 bootstrap
  调用、事件分发和临时 global 清理；不拥有宿主 history entry 或 URL parser。
- `test_host` 删除 traversal/hash 的临时 global 设置与源码求值，只保留已提交 history 的复制、
  回滚、窗口/网络副作用和 typed navigation adapter；既有 public core/script ABI 与 native
  callback 数量不变。
- TEST212 直接验证 traversal/hash API、`popstate` 后 `hashchange` 顺序、location/history 状态、
  非法 JSON/长度和临时 global 清理；既有 TEST112–211 回归保持通过。

已经核验并提升为基线：

- `python scripts/test_c89ize.py`、`python scripts/audit_repo.py`；
- VS2008 ARMV4I Debug 正式构建；
- 定向 `TEST212/999`（2 项）、`TEST112-135/137-152/189-212/999`（65 项）和全量
  `TEST13/20/27/43/44/56/58-77/80-212/999`（160 项）WMDC/RAPI 设备门；
- TEST13 三段真实导航、TEST212 product location dispatch、零 `ERROR`、零 `FAIL`、唯一
  `TESTBENCH PASS`；完整证据路径见本文件顶部。全量首尝 TEST166 单次 timeout，定向重跑和最终
  全量重试通过，未调整预算或放宽断言。

## 唯一下一步

在 next250 基线之上，把 native EDIT `change` 事件分发入口从 `test_host/main.c` 迁入
`positron_browser.dll`，先保持现有 public core/script ABI 和宿主回调边界，不把窗口、网络、
history/navigation 副作用或完整 URL parser 一起迁入；继续保留 TEST13 和人工视觉/输入累计门。

完成标准：

- next250 的 165 项自动 gate、TEST217/999 和 SELECT/focus/key/input 定向 gate、C89、审计和正式构建均保持通过；
- 最新 TEST75 纵向/横向截图已核对无异常，其余人工包由用户报告正常；人工验收若切换为
  `auto=0` 不会创建 `test_host.log`，这部分仍以截图/操作记录为人工证据，不替代自动日志；
- 下一批为 EDIT change 产品边界增加成功/缺失/非法参数、资源关闭、页面脚本
  和旧页面回归断言，并通过定向后全量设备门；
- 若出现崩溃、数据损坏、严重布局破坏或核心交互阻塞，立即停止累计并进入 debug；
- 候选通过后覆写本文件，并从路线图中选择下一个单一代码能力。

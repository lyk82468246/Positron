# Positron 路线图

更新时间：2026-08-17

本文件只列尚未完成的目标。已提交的 next 批次不继续停留在路线图；当前候选和设备门见
[`HANDOFF.md`](HANDOFF.md)，当前能力缺口见
[`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 总原则

Positron 是给 WM6 补齐现代能力，不是拆掉 WM6 重建。

- WM6 已经足够的部分优先复用 GDI、WinInet、WM Imaging、CryptoAPI 和 native control。
- 缺失的协议、解析器、编解码器或 runtime 优先移植成熟上游，再用稳定 DLL 隔离平台差异。
- “能力是否存在”优先于观感和性能微调；崩溃、数据损坏和核心交互阻塞始终最高优先级。
- 每批只推进一个边界明确的纵向能力，保留正例、反例、共享路径和真实页面门。
- 公共 DLL 服务任意 WM 应用；浏览器和 `test_host` 只是组合消费者。

## 当前中期里程碑

在浏览器 JavaScript 默认关闭的前提下，把已存在的 Duktape 页面 context 与 DOM、事件、表单、
输入、location/history 逐步接成可预测、可回归的轻量网页运行能力，同时不破坏默认 Browse
路径和公共 DLL 边界。

该里程碑的完成不是“完整浏览器 JavaScript”，而是：

- 生命周期和所有权明确；
- 常见轻量页面脚本能够完成基本 DOM、事件、输入和导航任务；
- 关闭开关时零额外脚本发现、抓取和执行；
- 每个受支持语义都有设备正反例和真实页面哨兵；
- 未支持能力明确失败或走普通导航，不产生隐式近似。

## 短期目标

### 1. 将浏览器 JavaScript bridge 迁入产品层

next234 已将 history/session 状态机迁入 `positron_browser.dll`，next235 已迁移 PScript context
及 host JSON callback 的 session 所有权和生命周期，next236 已迁移 browser bootstrap 文本与
求值入口，next237 已迁移 DOM 只读 callback 的 JSON 分发，next238 已迁移 textContent 写入，
next239 已迁移 DOM attribute callback，next240 已迁移 Event callback，next241 已迁移 input value callback，next242 已迁移 checked callback，next243 已迁移 form-property callback（defaultValue/defaultChecked/selectedIndex），next244 已迁移 navigation JSON callback dispatch，next245 已迁移同文档 location/history 事件分发，next246 已迁移 native input/composition typed dispatch entry，next247 已迁移 native keyboard typed dispatch entry，next248 已迁移 focus-family typed dispatch entry，next249 已迁移 native SELECT change typed dispatch entry，next250 已迁移 native SELECT input typed dispatch entry，next251 已迁移 native EDIT change typed dispatch entry，next252 已迁移 native EDIT post-change input typed dispatch entry；下一批继续迁移剩余 native/form/input callback 实现。导航的窗口、网络和 history side effect、core 事件传播、焦点与控件默认行为仍由宿主 typed adapter 提供。`test_host` 只能作为
宿主适配和测试消费者；
产品层必须继续保持 opaque handle、UTF-8、明确所有权、受控 callback 数和页面生命周期，
不把窗口、网络或完整 URL Standard parser 一起塞入 core/browser DLL。

## 中期目标

### 浏览器 JavaScript 与 Web 平台

- 继续补齐有真实页面价值的 location/history 语义，而不是自写完整 URL parser。
- 评估 cookies、简单 storage 和更完整的页面生命周期；每项都需要明确持久化、配额和失败策略。
- 扩展 DOM/Event 时保持 size-tagged ABI、受控 native callback 数和文档生命周期。
- 评估模块、异步任务或计时器前，先解决取消、关闭、导航替换和执行预算。

### 页面兼容与布局

- 从可重复真实页面缺口选择基础 Grid、background size/repeat 或其他高价值能力。
- Float 只有在完整 box construction/normalisation 方案存在时才能重启；禁止恢复旧 TEST79 实验。
- 补齐复杂 positioning、table 边界、overflow 和 CSS Lists 时，继续同时验证高 DPI 与旋转。
- 保持 TEST13 深层导航和旧页失败回滚，不让离线几何通过覆盖真实页面回归。

### 输入、表单与事件

- 完整真实 SIP/IME composition、候选词、Unicode 和 preedit 生命周期。
- 类型/范围/step、custom validity、`invalid` 事件和首个无效控件反馈。
- label、Enter、multiple select、文件选择和 native control 视觉/焦点行为。
- 区分 synthetic event、WM 消息和真实用户输入的证据。

### 网络、资源与安全

- 在 MSVC9/WM6 约束下评估受维护 TLS 方案，避免永久停留在 Mbed TLS 2.16.12。
- 统一 CSS、图片、脚本和后续资源的 URL、失败、redirect、预算与缓存策略。
- 完善整页进度和失败反馈，但不在未确认线程安全前把 DOM/GDI 移到 worker。

### 公共 DLL 生态

- 让 TLS、JSON、HTTP、image、script、core 都能被独立 WM 程序使用。
- 以真实外部消费者需求决定新增 API，不让 `test_host` 私有结构泄漏。
- 继续保持 C ABI、UTF-8、opaque handle、跨 CRT 所有权和版本化结构。

## 长期目标

- 从轻量浏览器发展为可编写 Positron 应用的运行时。
- 提供受控窗口/导航、fetch、文件、本地存储和 native bridge。
- 建立固定轻量真实网页集，覆盖文本、图片、表格、flex/nav、表单和脚本。
- 持续管理图片、CSS、字体、脚本和页面缓存的内存上限与释放。
- 在真实设备数据证明需要时做重排节流、绘制剔除和热点优化。

## 每批完成标准

1. 只有一个清晰能力边界。
2. 正例、反例、共享旧路径和失败行为都有自动断言。
3. C 改动通过 C89 回归和正式 ARMV4I 构建。
4. 候选包整体 stage，不混用 DLL/EXE。
5. 风险相关设备门通过；达到阈值或出现异常时跑全量门。
6. 必需人工检查完成，或明确列入允许累计清单。
7. `HANDOFF` 覆写当前状态，`KNOWN_LIMITATIONS` 只更新剩余边界，完成项退出本路线图。
8. 只提交本批 tracked 文件并推送当前分支；`tmp/` 永不提交。

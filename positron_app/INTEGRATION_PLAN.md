# `positron.exe` DLL 接线计划

## 范围与目标

`positron.exe` 以 `test_host` 的接线顺序、callback 注册和生命周期处理为参考，成为当前公共
Positron DLL 的真实 WM6 应用宿主。第一版范围固定为 HTTP/HTTPS、EXE 内置离线页面和单窗口单
文档；不加入 `file://`、下载、外部协议、多窗口、worker、module、bfcache 或完整现代 Web API。

“全部功能”指当前公共 DLL 已实现且面向浏览器用户的有界能力，不要求应用为了覆盖导出符号而
直接调用 TLS、JSON、Image 或 Script 的低层诊断 API。Core/Browser/HTTP 继续拥有可复用的页面、
资源、DOM、事件、表单、history 和生命周期语义，EXE 只拥有窗口、消息、线程、native 控件、
输入和应用策略。

## 私有宿主分层

应用私有适配层按以下职责拆分；这些接口不导出给其他 DLL，也不复制公共 DLL 的产品语义：

- `AppHostContext`：主窗口、CommandBar、viewport、DPI、语言、消息循环和关闭状态。
- `AppPageContext`：Core document、stylesheet、Browser session、页面 URL、generation、滚动和
  native 控件集合。
- `AppNavigation`：candidate/resource transaction、worker、取消、stale、重试和提交状态机。
- `AppResources`：Core URL resolver、CSS/image/script 资源发现和 HTTP 回调。
- `AppScript`：Browser callback table、classic script、生命周期和任务 checkpoint。
- `AppControls` / `AppInput`：WM6 native 子控件、焦点、滚动、SIP/IME、剪贴板和触摸。
- `AppForms`：校验、submit/reset/formdata、GET/POST 和 multipart 提交。

callback 的 `pw` 统一指向对应 `AppPageContext`。worker 只负责 transport 和消息投递；Core、
Browser 状态修改、style/layout/paint 和页面 swap 只在 UI 线程执行。CommandBar、软键、页面和
native 子控件保持同一顶层窗口体系，滚动使用 WM6 标准窗口滚动条，禁止自绘滚动条。

## 实施阶段

### 阶段 0：宿主分层与生命周期基线

第一批只做结构性迁移，不改变现有离线页面、i18n 和主文档 GET 行为：

- 建立统一的宿主上下文、页面所有权、generation、候选和 teardown 状态。
- 将当前 `main.c` 的全局窗口/页面状态收拢到 `AppHostContext`，并通过私有 API 初始化、绑定
  窗口、释放页面和关闭宿主。
- 保持旧页保留、stale/cancel、失败回滚、history 更新和资源释放顺序。
- 不编译或复制 `test_host/main.c`，不修改公共 ABI，不提前接入脚本、外部 CSS、图片、表单或
  native 控件语义。

阶段 0 必须通过 C89、仓库审计、Debug/Release 正式构建和现有离线页面回归后，才能进入阶段 1。

### 阶段 1：网络资源事务

按 `test_host` 的提交顺序接入：

`HTTP 响应 → ParseHTML → 外部脚本发现 → 必需 CSS → 可选图片 → Browser commit gate → layout`
`→ 旧页 teardown → 页面交换 → history/滚动恢复 → native 重建`

必需 CSS 失败阻止提交；图片和脚本失败不阻止页面显示但必须记录终态；HTTP/TLS 失败、取消、
过时和预算拒绝必须保留旧页面。HTTPS 默认保持证书链和 hostname 校验。

### 阶段 2：Browser ScriptSession

注册 DOM、属性、mutation、事件、焦点、表单、输入、导航、滚动、图片、native 控件和生命周期
callback；按文档顺序执行 inline/external classic script；接入 bootstrap、timer、microtask、
message、visibility、focus、beforeunload 和 teardown。

JavaScript 默认启用，使用 `PSCRIPT_DEFAULT_BUDGET_MS * 4` 与现有 Browser 脚本堆上限。脚本
异常不使已解析页面回滚；会话初始化、桥接、超时或超限失败时，该文档脚本能力关闭并 fail
closed。

### 阶段 3：原生交互

接入 EDIT、SELECT、toggle、button、dialog、contenteditable、file picker、SIP/IME、clipboard、
焦点和标准 WM6 滚动条。native 控件只是 Core DOM 的平台代理；DOM、事件默认行为和表单状态仍
由 Core/Browser 所有。支持 DPI、旋转、viewport 和 native 子控件重排。

### 阶段 4：表单与导航

接入校验、`submit/reset/formdata`、GET/POST、URL encoded、multipart、fragment、push/replaceState、
Back/Forward、脚本导航和滚动恢复。提交取消、网络错误和候选失败必须保留旧页；native 文件
选择路径优先。JavaScript File/Blob 到 multipart 只有在真实消费者出现且公共 ABI 补齐后实现。

### 阶段 5：发布验收

每阶段运行：

- `python scripts/test_c89ize.py`
- `python scripts/audit_repo.py`
- `scripts\\build.bat Debug rebuild`
- `scripts\\build.bat Release rebuild`
- 新鲜 stage 启动验证和 ARMV4I 设备门

自动测试覆盖成功、资源失败、取消、stale、旧页保留、脚本超限、DOM mutation、事件、history、
表单、multipart、控件销毁和重复 teardown；设备门覆盖 HTTP/HTTPS、脚本、控件、SIP/IME、旋转、
DPI、软键和失败回滚。

## 公共接口与文档规则

阶段 0–4 不修改现有公共 ABI。新增内容全部为 `positron_app` 私有源文件和私有接口；若需要
File/Blob bridge，必须先在正确的 Core/Browser owner 中设计版本化 `Ex` 接口、容量、所有权和
失败语义，再由宿主接入。每阶段完成后更新 `docs/CAPABILITIES.md`、`.agents/HANDOFF.md`、
`.agents/KNOWN_LIMITATIONS.md` 和 `.agents/ROADMAP.md`；动态进度不追加到本文件。

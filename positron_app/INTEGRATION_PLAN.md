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

JavaScript 默认启用，使用 `PSCRIPT_DEFAULT_BUDGET_MS * 8` 的固定应用预算与现有 Browser 脚本堆上限。脚本
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

## 内部页面与命令地址

EXE 私有 `app_internal_pages.c/.h` 用固定注册表区分页面、别名和命令；scheme 分流仍由
`app_url_router` 完成，HTTP(S) 保留现有网络路径，不修改 DLL ABI。

| 地址 | 应用策略 |
| --- | --- |
| `positron://newtab` | 默认起始页；无参数、空地址和主页菜单均使用此页 |
| `positron://about` | 说明、可取得的版本/系统信息、内部页面目录 |
| `positron://version`、`positron://system` | 提交前规范化为 about 的 version/system 片段，仅提交一次 history |
| `positron://history` | Browser 当前导航栈的只读快照，包括仍存在的前进项，最新在前，最多 16 项 |
| `positron://downloads` | 明确说明下载管理未实现，不伪造任务或下载记录 |
| `positron://settings` | 只读显示当前语言、起始页与 JavaScript 策略 |
| `positron://quit` | 仅地址栏直接提交可以正常退出，不创建页面或 history 项 |

页面名大小写无关，接受一个可选末尾斜杠，显示规范小写地址；新页面只接受注册的片段。
`welcome`/`controls` 的原地址与关键词保留，controls 查询参数不改写。未知地址保留旧文档、
标题和已提交地址。别名章节由 Core fragment 几何定位，不把别名作为第二个页面提交。

英语/简体中文 UTF-8 模板无 BOM、以 RCDATA 嵌入 EXE；所有内部页复用 Core parse/style/layout/
paint 和统一 CSS，不创建 ScriptSession，不请求外部资源。模板沿用 i18n 预算，动态 HTML
不超过 128 KiB；调用方拥有生成缓冲区，按明确长度解析后释放。history 自身不展示；刷新/
重新进入时重新读取快照，文本和链接属性均转义，只为受支持地址生成链接。点击按 URL 发起
新导航，不依赖旧索引；不提供持久访问日志、清除、删除或搜索。新页链接使用 Core 焦点查询，
固定最多 24 个焦点槽位，并在最终布局后设置焦点目录。

导航来源由 EXE 私有枚举携带，分派处拒绝链接、脚本、启动参数、重定向、表单和历史重放
执行 quit；Debug 与 Release 一致。获准时投递现有 WM_CLOSE，沿原关闭流程取消并等待网络任务、
销毁 native 控件、释放页面与 DLL。页面切换仍先完成候选构建和 beforeunload，再提交 history、
teardown 旧页、交换页面和恢复滚动。restart/kill/hang 不属于当前实现。

About 的应用版本由 EXE 私有适配器提供：Debug 预构建时内嵌主机本地日期时间，精确到秒，
不在启动时生成；Release 只读 CAB 安装写入的 `HKLM\Software\Positron\Version`，缺失、
非字符串、超长、未终止或非四段数值时使用本地化“未提供”。注册表不作为 Debug 版本来源，
安装版本不冒充独立复制 EXE 的文件版本，公开 DLL ABI 的显示和 CAB 版本生成流程保持独立。

### 后端进入条件

本批不创建 SQLite/JSON 文件，不调整 DB 主线发布依赖。后续应用设置、访问日志和下载记录
优先使用 `positron_db.dll` 本地 SQL；表结构、migration 和存储策略归应用，Browser 会话栈保持
独立。接入前必须通过 ARMV4I 文件数据库创建、中文读写、事务回滚、关闭重开、migration 失败、
空间不足和 journal 恢复设备门，随后才加入 EXE 链接、解决方案依赖和 CAB 输入校验。未通过时
后续才考虑版本化、原子替换的本地 JSON，不在当前批次引入回退存储。

真实下载另立纵切：HTTP 先提供有界流式响应与取消接口，应用负责文件保存和任务调度；不使用
现有 1 MiB 完整响应体模拟通用下载。

### 内部页面验收

Debug 私有自检使用独立 Browser history 验证路由来源、别名、16 项与前进栈、转义、自过滤、
容量失败、Core 解析和布局焦点；测试夹具与日志不编译进 Release。完整 Debug 包部署后，
`scripts/internal_pages_gate.bat` 复用已有 RAPI helper 自动检查实际启动导航、规范化、章节定位、
单次 history 提交、无脚本、焦点和新增 crash dump；需要显式同意精确进程清理，门后留下 newtab。
自动门不替代真实地址栏输入、菜单、history 点击/刷新、加载中 quit、中英文、触摸、键盘、
软键、滚动、旋转和 DPI 人工验收。所有构建串行，使用正式 Debug/Release 配置与匹配完整包。

## 公共接口与文档规则

阶段 0–4 不修改现有公共 ABI。新增内容全部为 `positron_app` 私有源文件和私有接口；若需要
File/Blob bridge，必须先在正确的 Core/Browser owner 中设计版本化 `Ex` 接口、容量、所有权和
失败语义，再由宿主接入。每阶段完成后更新 `docs/CAPABILITIES.md`、`.agents/HANDOFF.md`、
`.agents/KNOWN_LIMITATIONS.md` 和 `.agents/ROADMAP.md`；动态进度不追加到本文件。

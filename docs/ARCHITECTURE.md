# Positron 架构与公共边界

本文定义 Positron 的稳定工程边界：哪些能力属于公共 DLL，哪些属于宿主，数据和资源由谁拥有，以及 WM6/VS2008 对实现施加的约束。精确函数签名以各项目公开头文件为准；当前开发状态见 [`.agents/HANDOFF.md`](../.agents/HANDOFF.md)。

## 设计目标

Positron 为 Windows Mobile 6 / Windows CE 5.2 提供可以单独消费、也可以组合使用的基础 DLL。公共接口刻意保持窄而明确：

- C ABI，不向调用方暴露 C++、NetSurf、Duktape、cJSON 或 mbed TLS 内部类型；
- 所有跨边界文本均为 UTF-8；
- 状态通过 opaque handle 表示；
- 每个分配结果都有明确的释放者；
- 平台窗口、消息循环和网络调度留给宿主；
- 资源预算和功能上限可预测，失败时 fail closed。

项目不以完整现代浏览器、完整 Web 标准或桌面级并发运行时为目标。

## 总体分层

```text
WM6 应用 / test_host.exe
        │
        ├── 平台窗口、消息循环、WM 控件、SIP/IME、文件选择器
        ├── 导航事务、后台网络调度、资源获取与页面提交
        │
        ├── positron_browser.dll ── history、script session、DOM/Event 协调
        ├── positron_core.dll    ── HTML/CSS/DOM、style、layout、paint、表单
        ├── positron_script.dll  ── 有预算的通用 JavaScript runtime
        ├── positron_image.dll   ── bitmap/SVG decode、draw、encode
        ├── positron_http.dll    ── HTTP/1.1 与 URL reference 解析
        ├── positron_json.dll    ── JSON parse/query/serialize
        └── positron_tls.dll     ── TLS client、peer、listener
```

`test_host.exe` 只是上述组合的一种实现。可复用的业务语义必须位于适当的公共 DLL；宿主只保留 Windows Mobile 平台适配、应用策略和测试夹具。

## 公共 DLL

### `positron_tls.dll`

TLS 层拥有 mbed TLS context、socket 会话、证书链、peer identity 和 listener。它提供：

- verified 与显式 insecure 客户端连接；
- 运行时追加根证书；
- 读、写、关闭和线程安全错误复制；
- identity 创建/载入、DER SHA-256 指纹；
- 可选或强制客户端证书的 listener；
- pin 校验的 peer 连接。

调用方拥有 host/path 等输入字符串；连接、identity 和 listener handle 必须分别由对应 close API 释放。`PTls_LastError` 返回借用存储，跨线程或跨后续调用保留错误时应使用复制接口。

TLS 版本与信任数据受 WM6 工具链和 vendored 版本限制。verified 是默认产品方向，insecure 入口只用于明确的诊断和受控环境。

### `positron_json.dll`

JSON 层把 cJSON 隐藏在 opaque handle 后。顶层 parse handle 由 `PJson_Free` 释放；对象成员和数组元素是借用子节点，随父树失效，不能单独释放。序列化字符串使用 `PJson_FreeString`，不能假设它与调用方 CRT heap 相同。

### `positron_http.dll`

HTTP 层建立在 TLS 层之上，负责 HTTP/1.1 GET/POST、响应解析、有限 redirect、body 上限、进度回调和 reference URL 解析。它不拥有浏览窗口、历史记录或页面提交。

`PHttp_Get*`/`PHttp_Post*` 返回的 response 必须用 `PHttp_FreeResponse` 释放。网络失败通过 `status_code == 0` 与错误文本表达，调用方不得把非空 response 指针误判为请求成功。

### `positron_image.dll`

图像层是位图/SVG 的公共边界。它复制调用方输入字节或像素，内部持有解码对象，并提供：

- BMP/PNG/JPEG/GIF 等位图解码与信息查询；
- 从 BGR24/BGRA32 像素创建 retained bitmap；
- 绘制到调用方 HDC；
- PNG/JPEG/BMP/GIF 编码和配对 buffer 释放；
- SVG 解析、尺寸查询和绘制。

bitmap/SVG handle 由创建方通过对应 free API 释放。编码 buffer 必须用 `PImage_FreeBuffer`。HDC 仍属于调用方，图像层不创建或管理宿主窗口。

### `positron_script.dll`

脚本层封装一个受预算约束的 Duktape context。它提供 UTF-8 source 求值、JSON 结果桥、受限 native function、CommonJS 风格模块 provider 和内存/执行统计。

每个 script handle 独立拥有 heap、模块缓存、native function 注册和错误/result 缓冲。回调同步运行在调用线程，不得重入、销毁当前 context 或保存借用参数指针。宿主应使用有界预算和内存上限，不把该运行时当作完整浏览器沙箱。

### `positron_core.dll`

Core 是渲染和文档模型的产品边界，内部静态链接移植后的 NetSurf 支持库。主要职责包括：

- UTF-8 HTML 解析为 libdom 文档；
- CSS 解析、cascade、媒体条件和整树 computed style；
- 外链 CSS、`@import`、图片和 script 资源发现与有界缓存；
- NetSurf box construction、layout、hit testing 和 GDI paint；
- 在宿主提供活动 modal id 时，把普通文档、实体色 backdrop 和指定 `<dialog open>` 按固定顺序组合绘制；
- 表单值、约束验证、提交、reset 和 successful controls；
- 单元素 `contenteditable` 的祖先继承、有效模式、有界 UTF-8 纯文本 mutation，以及供宿主创建编辑表面的已布局 editing-host 快照；剪贴板数据不进入 Core 文档状态；
- 交互状态、DOM 事件、焦点候选和支持控件的默认动作；
- 给脚本/浏览器层使用的有界 DOM、属性、关系、表单与导航查询。

Core 不执行网络请求。资源获取通过调用方提供的 resolve/fetch/free 回调完成；Core 在回调返回前复制需要保留的字节，再按契约调用 free。Core 也不执行 JavaScript，只发现、缓存和枚举脚本。

文档 handle 拥有 DOM、computed styles、box tree、资源缓存、image carriers、表单和交互状态。释放文档会使从它借用的节点、字符串、资源字节和几何信息全部失效。style/layout/paint 通常属于同一 UI 线程；不得在后台 worker 并发操作同一个文档。

`PCore_ContentEditableTargetInfo` 是宿主的 native editing-host 快照：它只返回已布局、可见且带非空 id 的有效 editing host，按 DOM 顺序限制为每页 16 个，文本限制为 8192 个 UTF-8 字节。嵌套且仅继承编辑状态的后代归最近 editing host 所有，不会产生第二个窗口。快照的几何和字符串只在当前文档/layout 仍有效时使用；文档 mutation 或重排后宿主应重新枚举并重建窗口。

Core 不保存编辑选区，也不重新派发 `selectionchange`。Browser 以 JavaScript UTF-16 code-unit 偏移提供 `selectionStart`、`selectionEnd` 和 `selectionDirection`，并在范围实际改变时分发一次非冒泡、不可取消的 `selectionchange`；宿主可通过 `PBrowserScriptContentEditableSelectionCallbacks` 把这些范围映射到原生 EDIT，再用 `PBrowser_ScriptSessionNotifyContentEditableSelection` 报告原生范围变化。WM EDIT 宿主对无修饰鼠标拖选和 Shift/方向键扩展保留短暂 anchor，在默认消息完成后报告有序范围和方向；捕获丢失、取消模式或焦点切换会先结束未完成手势，再由 Browser 去重通知。无法物化原生窗口时 Browser 保留脚本侧回退。宿主不得在 Core 中复制第二份文本或选区模型。

受限剪贴板事务同样由宿主负责平台接线：宿主从 `WM_PASTE` 读取 `CF_UNICODETEXT`，从原生选区取得 `WM_COPY` data，或从 `WM_CUT` 取得 data，将 CRLF 规范化为 UTF-8 后交给 Browser 的 `beforeinput`；`WM_COPY` 的折叠选区不改写剪贴板，粘贴/复制数据超长、格式缺失或读取失败时在 native mutation 前 fail closed。对 `WM_CUT`，事件未取消时才允许 native default，再调用 Core 文本 mutation、Browser `input` 和选区通知；WinCE 原生 EDIT 在该默认动作中可能内部重入 `WM_COPY`，宿主只对同一外层剪切临时放行该重入。Core 不读取系统剪贴板、不保存 clipboard handle。

### `positron_browser.dll`

Browser 层拥有无窗口的浏览器会话语义，而不是渲染器：

- 有界 history entries、same-document state 和 traversal；
- 浏览器 script session 与 bootstrap；
- DOM/属性/表单/validation adapter 的 JSON 与 typed dispatch；
- `isContentEditable`/`innerText` 的有界单元素纯文本桥、脚本侧 `selectionStart`/`selectionEnd`/`selectionDirection` 和去重后的 `selectionchange`；
- Event、input、keyboard、focus、composition、click 和导航协调；
- timer、animation frame、microtask、idle、message 和页面生命周期队列；
- native EDIT/SELECT/button/file/disclosure 等平台控件事务状态。

它通过 callback table 与 Core 和宿主交换信息，不直接依赖窗口、网络或设备控件。callback 必须同步、有界、不可重入，并遵守头文件中的借用缓冲规则。history 与 script-session handle 相互独立，销毁顺序由宿主明确管理。

浏览器 JavaScript 与 `positron_script.dll` 共用 Duktape 实现，但角色不同：Script DLL 是通用嵌入服务；Browser DLL 负责把有限 Web 对象和事件语义组合到一个页面 session。浏览器脚本仍需要宿主提供真实 DOM、平台默认动作、导航和窗口生命周期。

## 内部静态库

以下工程是实现细节，不是供第三方应用直接链接的顶层 ABI：

- `positron_netsurf`、`positron_hubbub`、`positron_libcss`、`positron_libdom`；
- `positron_expat`、`positron_libsvgtiny`、`positron_libjpeg`；
- 其他只为公共 DLL 提供目标文件的移植工程。

它们按“一库一工程”隔离上游 include 命名冲突和对象名冲突。外部应用若直接链接这些静态库，将绕过 Positron 的 ABI、所有权和兼容性保证。

## 宿主职责

宿主拥有所有与具体应用或 Windows Mobile UI 绑定的行为：

- 顶层窗口、消息循环、滚动条和 DPI/旋转通知；
- native EDIT、COMBOBOX、按钮、文件选择器和 SIP/IME；contenteditable 的 WM EDIT 代理也由宿主创建、定位、销毁并跟踪其平台鼠标/键盘选区；受限 `WM_PASTE`/`WM_COPY`/`WM_CUT` 的 `CF_UNICODETEXT` 读取、写入和所有权也只属于宿主；
- 后台线程、导航取消、loading 状态与页面 swap；
- DNS/TCP/TLS/HTTP 组合策略和资源调度；
- 新窗口、外部协议、下载和文件系统权限策略；
- 把 Core 文档回调注册到 Browser session；
- 把 Core 的 contenteditable 状态/文本 callback 和 selection callback 注册到 Browser session，并把 WM/native 输入接到 `beforeinput`→Core mutation→`input` 顺序；宿主只保留窗口、焦点、坐标、键盘/拖选 anchor、Shift 状态和原生选区，在范围变化或捕获/焦点中断后调用 Browser 的通知入口，不经 Core 重复派发 `selectionchange`；对 `WM_PASTE`/`WM_COPY`/`WM_CUT`，宿主读取并规范化有界 `CF_UNICODETEXT`，让 Browser 决定取消后再执行 native default，折叠复制保持原剪贴板不变，并对 WinCE `WM_CUT` 的同一 HWND 内部 `WM_COPY` 重入做局部放行；
- 从 Browser 读取活动 modal id，并在 WM_PAINT 中调用 Core 的 modal paint 组合入口；
- 决定何时启用浏览器 JavaScript；
- 应用级崩溃恢复、持久化和日志。

宿主可以实现这些策略，但不得复制已经属于公共 DLL 的 URL、history、DOM、事件、表单或图像业务语义。发现可复用语义仍滞留在 `test_host` 时，应把它视为架构债务并迁移到相应 DLL。

## 页面加载与提交

推荐的主文档事务如下：

1. UI 线程记录导航意图和当前页面，但不立即销毁旧文档。
2. worker 获取主文档及可并行准备的网络资源；网络层不触碰 DOM/NetSurf 状态。
3. UI 线程解析 HTML，创建候选文档。
4. 通过 Core 的 resolver/fetch 回调准备 CSS、`@import`、图片和 script cache。
5. UI 线程完成 style、layout 和首帧可绘制性检查。
6. 候选成功后原子提交页面与 history；失败则释放候选并保留旧页。
7. 交互、旋转或动态 DOM 修改按需重新 style/layout/paint。

任何后台线程都不能持有 DOM 节点、computed style、box tree 或 HDC。失败日志应区分 DNS、TCP、TLS、证书、HTTP、资源、解析、style、layout 和提交阶段。

## 脚本与事件组合

浏览器脚本默认关闭。显式启用时，宿主按以下原则组合：

1. Core 发现并缓存 classic script；
2. Browser session 注册有界 DOM/Event/platform callbacks；
3. 按文档顺序执行允许的 inline/external script；
4. native Windows 消息先形成 typed event，再由 Browser 决定取消或允许默认动作；WM6 `EDIT` 可能在 `WM_CHAR` 默认处理完成前发送 `EN_CHANGE`，宿主必须在默认处理返回后读取最终值，避免把旧值提交为一次 mutation；`WM_PASTE`/`WM_CUT`/`WM_COPY` 只在可取得有界 `CF_UNICODETEXT` data 时进入 contenteditable 事务，折叠复制直接 no-op；
5. Core 执行 DOM/form/default mutation；对 contenteditable，只有 `beforeinput` 未取消时才执行有界纯文本 mutation，Browser 的脚本 selection API 在可用时同步宿主原生选区；原生 WM EDIT 的无修饰 down/move/up 或 Shift/方向键默认处理返回后，宿主用短暂 anchor 计算方向，并在捕获/取消/焦点中断时收尾，再通知 Browser；paste/copy/cut 还必须在 native default 前完成 clipboard data 的格式和容量检查，且不得让宿主复制一份 Browser 的 Range/Selection 模型；
6. Browser 派发 mutation 后的 `input`、`change`、focus、`selectionchange` 或 lifecycle 事件；原生选区通知必须先由 Browser 去重，不能由宿主和 Core 各发一次；
7. 宿主按需重新 layout/paint；活动 modal 时先让 Core 画普通文档，再组合实体色 backdrop 和 dialog；
8. 页面替换时销毁 session 与文档。

事件顺序、取消和状态提交必须由产品层确定，不能依赖 test fixture 的偶然消息顺序。真实 SIP、OEM IME、系统 picker 和窗口创建仍需要设备人工验收。

## ABI 与所有权规则

### 字符串

- 跨公共边界的字符串一律 UTF-8，除非参数明确是 Win32 `WCHAR`/HDC 等平台类型。
- 输入字符串在调用返回后仍由调用方拥有，DLL 不保存指针，除非 API 明确说明会复制。
- 借用字符串只在头文件规定的 mutation 或 handle 生命周期内有效。
- probe/capacity API 必须 NUL 终止可写缓冲，并报告完整所需字节数。

### Handle 与内存

- opaque handle 不是 Win32 kernel handle，不使用 `CloseHandle`。
- 创建与销毁 API 必须配对；子节点和查询结果若为借用值，不得单独 free。
- DLL 分配的公开 buffer 必须由同一 DLL 的配对函数释放，不能跨 CRT heap 使用 `free`。
- 销毁父对象后，所有借用节点、字符串、buffer 和 callback token 立即失效。

### ABI 演进

- 已发布结构体通过 `cbSize` 或显式 ABI 版本演进；新字段追加，不改变旧字段布局。
- 新能力优先新增函数或 `Ex` 入口，不静默改变旧入口含义。
- 无效参数、容量不足、越界、错 origin 和错状态必须返回稳定错误，不能部分提交。
- 公共头文件是精确契约；README 只解释调用模式，不复制整套声明。

## 线程与重入

- TLS/HTTP 可以在宿主 worker 使用，但每个连接/response 的并发所有权必须唯一。
- Core document、layout、paint 和 Browser script session 默认由单一 UI 线程串行驱动。
- 同步 callback 不得重入触发它的 session，也不得在回调中销毁父 handle。
- 页面替换、取消和关闭必须先阻止新回调，再释放平台控件、script session、Core document 和 history/app state；具体顺序以拥有关系为准。

## 平台与移植约束

所有产品 C 代码必须兼容 VS2008 的 C89 方言和 WM6 ARMV4I：

- 不使用块中声明、`for (int ...)`、designated initializer 或 C99-only CRT；
- 缺失 CRT/Win32 API 通过 `compat/` 中可审计 shim 解决；
- 第三方 C99 降级应由可重复、幂等的转换脚本完成；
- 使用正式 `.sln`/`.vcproj` 构建，不用现代桌面编译结果代替目标构建；
- vendored 源码保持版本、许可证、生成步骤和本地补丁记录。

移植代码的正确性需要三层证据：转换器回归、VS2008 ARMV4I 构建、真实设备行为。只满足其中一层不足以成为产品基线。

## 明确非目标

- TLS 1.3、HTTP/2、HTTP/3 或现代浏览器级网络栈；
- 完整 WHATWG URL、DOM、HTML、CSSOM、Web API 或 ECMAScript host environment；
- 完整 CSS Grid、任意 float/position/table 边界和桌面级字体排版；
- 通用 ClipboardEvent、async clipboard、CF_TEXT/富文本转换或跨应用剪贴板格式互操作；当前宿主只提供有界 `CF_UNICODETEXT` contenteditable paste/copy/cut 接线；
- 多窗口浏览器、完整现代 modal dialog/backdrop（仅支持有界实体色组合）或持久化浏览历史；
- 在 DLL 内接管应用消息循环、系统 picker、OEM IME 或设备连接；
- 把 `test_host.exe` 变成产品依赖。

当前具体支持范围与剩余缺口见[已知限制](../.agents/KNOWN_LIMITATIONS.md)。

# Positron 架构与公共边界

Positron 面向 Windows Mobile 6 / Windows CE 5.2 ARMV4I，提供可组合的 TLS、JSON、HTTP、图像、脚本、文档模型、渲染和浏览器会话 DLL。公共接口统一使用稳定的 C ABI、UTF-8、opaque handle 和明确的内存所有权；宿主不能通过复制产品语义来绕过这些边界。

## 设计目标

- 在 VS2008、C89 和 WM6 资源预算下保持可预测的大小、错误码和生命周期。
- 让公共 DLL 拥有可复用的 URL、资源、DOM、事件、表单、图像、脚本和生命周期语义。
- 让 `test_host.exe` 以及未来应用只负责平台接线、调度、fixture、应用策略和断言。
- 对不支持、超限、失效句柄和回调缺失的情况 fail closed，不以静默扩大预算或私有状态补齐标准行为。

## 总体分层

```text
应用宿主 / test_host.exe
    ├─ WM 窗口、消息、DPI/旋转、native 控件、SIP/IME、picker
    ├─ 网络 worker、RAPI/设备部署、应用策略和测试断言
    └─ callback 接线与 Core/Browser 生命周期调度
公共 DLL
    ├─ positron_tls.dll       TLS 会话与证书/握手状态
    ├─ positron_json.dll      有界 JSON 解析与生成
    ├─ positron_http.dll      HTTP 消息、header、body 与 transport 边界
    ├─ positron_image.dll     有界图像解码/元数据接口
    ├─ positron_script.dll    独立 Duktape 嵌入服务
    ├─ positron_core.dll      文档、CSS、布局、命中、资源与 Core mutation
    └─ positron_browser.dll   页面 session、有限 Web 对象、事件和队列
内部静态库
    └─ NetSurf/libcss/libdom/hubbub、Expat、libsvgtiny、libjpeg 等移植依赖
```

宿主可以组合 DLL，但不能直接把内部静态库当作公共 ABI。Core 不访问窗口，Browser 不访问 HWND 或网络；宿主通过头文件中的 callback table 把两者与平台连接起来。

## 公共 DLL

### `positron_tls.dll`

提供 TLS context、证书链、握手、读写和错误分类。调用方拥有输入 buffer 和连接策略，库只在文档规定的 handle 生命周期内借用它们；证书校验、时间、hostname 和 transport 失败必须由调用方记录明确分类。Mbed TLS 版本固定在仓库 pin，升级需单独审查内存、算法和 WM6 兼容性。

### `positron_json.dll`

提供有界 JSON token/value 解析和生成。输入长度、嵌套深度、字符串和数字预算在入口处检查；库不负责网络、文件、线程或业务 schema。输出 buffer 的所有权和 size-probe 规则以 `positron_json.h` 为准。

### `positron_http.dll`

提供 request/response、header、body、状态码和 transport 结果的窄接口。DNS、TCP、TLS、重试时机、worker 和缓存策略由宿主或上层 Browser 事务拥有；HTTP 层不执行页面脚本、布局或导航提交。

### `positron_image.dll`

提供有界图像解码和元数据读取。Core 负责来源选择、资源状态、cache key 和 layout 投影；图像 DLL 不决定 URL、CORS、页面事件或 native 绘制窗口。

### `positron_script.dll`

是独立的 Duktape 嵌入服务，负责 context、注册 native function、值转换、任务执行和错误边界。它不拥有 DOM、URL、窗口或设备消息循环。`positron_browser.dll` 可以复用同一引擎，但浏览器对象、事件、导航和宿主 pump 仍属于 Browser session。

### `positron_core.dll`

Core 是文档和渲染的产品边界，内部使用移植后的 NetSurf 组件。它负责：

- UTF-8 HTML 解析、CSS cascade、媒体条件、computed style、资源发现和有界 cache；
- `<img>`、`srcset`、`picture/source` 的有限候选选择，以及 image-map 几何、布局、命中和 GDI paint；
- page extent、元素几何、overflow retained scroll、form owner、validation、successful controls、submission/reset 和 modal paint；
- 以 ID 或受控 child index 执行有界 DOM mutation，并在成功变化后使 retained layout 失效。

HTML parser mutation 只接受头文件声明的节点类型、深度、节点数、direct-child 和 UTF-8 预算。Core 不派发 DOM 事件、不创建 native 控件、不执行页面 script、不暴露 fragment handle，也不提供完整 live collection。

#### DOM 与 CharacterData 边界

`PCore_NodeNormalizeById` 只整理一个 Element 的 direct children：删除空 Text/CDATA，并把连续 Text/CDATA 合并到首个非空节点；Element、Comment、processing-instruction 和其他节点都是边界。`PCore_NodeSplitTextChildById`、`PCore_NodeReplaceWholeTextChildById` 以及 CharacterData 的 insert/replace/remove/create 入口共享 UTF-8、索引、返回码和 retained-layout 规则。调用方必须在成功 mutation 后重新 style/layout/paint。

`PCore_NodeSetTextContentById`、title、innerHTML、outerHTML、insertAdjacentHTML 和有限的 child-list replacement 都是原子、有界的 Core 操作。失败不留下部分树；成功保留头文件承诺的节点身份，Browser 再负责 wrapper/snapshot reconciliation。

### `positron_browser.dll`

Browser 把 Core 与有限的页面脚本组合成一个显式驱动的 session。它负责：

- history、navigation candidate/resource observation、页面生命周期、viewport、visualViewport、scroll restoration 和任务检查点；
- 有界 DOM/Element/CharacterData wrapper、属性 facade、selector、form/option metadata、image metadata、Event 和 validation 对象；
- `document.activeElement`、focus/blur、`scrollIntoView`、native callback 请求及页面脚本的同步/异步队列。

Browser 不创建窗口、不直接读写网络、不替宿主 clamp 物理坐标，也不决定系统 picker、SIP/IME 或 native 控件默认动作。宿主必须显式调用 resize、scroll、focus、lifecycle 和 task checkpoint 通知；没有 pump，页面异步队列不会自行推进。

#### DOM wrapper 与 Fragment

普通 live wrapper 以 Core id/关系为真值；detached wrapper 保存有界快照，连接、移除和失败 mutation 必须同步 owner、childNodes/children snapshot 与 identity。`DocumentFragment` 只提供文档声明的 bounded staging：最多四个 direct 根，允许的 Element/Text/Comment/CDATA 形状和容量在 mutation 前检查；不支持的嵌套、重复 id、跨 owner 或超限输入 fail closed。

`Node.normalize()` 在 live Element 中通过 Ex5 callback 调用 Core，再同步 Browser-created Text/CDATA wrapper；在 detached Fragment 中直接整理 staging。两条路径都删除空 Text/CDATA、合并相邻 Text/CDATA、保留首个非空节点，并以 Comment 作为边界。Fragment 的 `textContent` 排除 Comment，消费时复用既有 Core creation callback，不新增 Core fragment ABI。

`Element.getElementsByTagName()` 是 Browser 当前唯一的 bounded live collection：每次方法调用
返回一个新的 `HTMLCollection`，已连接 owner 的同一对象在读取时按需重扫，最多访问 256 个
节点并返回 64 项；索引、`item()`、`namedItem()`、`forEach()` 和 iterator 共用该结果，
Browser-created wrapper 也保持 identity。刷新超过预算时保留上一次成功结果，detached owner
返回空集合；其他 collection 仍按各自合同提供 bounded snapshot，因此这不是完整 live DOM。

#### 导航、资源与脚本

Browser 持有 candidate generation、取消/退休状态、资源终态、required/optional gate、重试预算和脱敏失败摘要。宿主拥有 worker、response、网络策略和页面提交时机；提交前通过 Browser snapshot 检查 candidate 与 resource gate，清理前复制 cleanup snapshot，再释放 handle。旧页保留、过时消息隔离和 pending 终态不能由宿主另造一套分类。

Script session 的 native function 数量、listener、collection、Fragment、selector、字符串和任务队列都受固定预算约束。超限、参数错误、回调缺失、stale handle 和不支持的 Web API 均按头文件约定返回安全失败或 no-op，不伪造完整浏览器行为。

## 内部静态库

`positron_netsurf`、`positron_hubbub`、`positron_libcss`、`positron_libdom`、`positron_expat`、`positron_libsvgtiny`、`positron_libjpeg` 以及其他移植工程只产生公共 DLL 所需的目标文件。它们隔离上游 include/object 命名冲突；外部应用直接链接它们会绕过 Positron 的 ABI、所有权和兼容性保证。

## 宿主职责

宿主拥有所有与具体应用或 Windows Mobile UI 绑定的行为：

- 顶层窗口、消息循环、DPI/旋转、page viewport clamp、native child reposition 和 GDI invalidation；
- EDIT、COMBOBOX、button、file picker、SIP/IME、contenteditable 的 WM 代理以及受限剪贴板；
- DNS/TCP/TLS/HTTP worker、响应和取消时机、资源调度、页面 swap、外部协议、下载与文件权限；
- Core/Browser callback 注册、style/layout/paint 调度、平台焦点和 native 控件默认动作；
- 测试 fixture、断言、日志和设备部署。宿主不得编译公共 DLL 的实现源文件，不得把可复用 URL/DOM/Event/表单/资源语义放进 `test_host`。

页面提交遵循固定顺序：worker 完成后按 generation 收敛资源终态，读取 Browser candidate/resource snapshot，成功后由宿主执行 Core style/layout、创建 native child 并提交窗口；失败或过时候选保留旧页。清理顺序是 join worker、终止 pending 资源、复制 Browser cleanup snapshot、停止回调、最后释放 handle。

## ABI、所有权与重入

- 字符串在公共边界使用 UTF-8；size-probe 必须先返回所需字节数，容量不足不得部分改写输出。
- handle 是 opaque，创建者负责销毁，借用 buffer 只在同步调用期间有效；回调不得保存指针、跨线程调用或重入同一个 script session。
- 错误码区分成功、参数/容量、目标不可用和 DOM/分配失败；缺失 callback 与 stale handle 不能被解释为成功。
- 新能力优先追加 callback table/Ex 版本，保留旧 ABI 的字段和语义；测试宿主只消费已公开头文件。

## 线程与移植约束

Core、Browser 和 Script session 的 DOM/脚本状态由宿主在受控线程驱动；worker 只能通过消息传递结果，不能直接碰 DOM、窗口或脚本 runtime。实现必须保持 C89、VS2008、WM6 ARMV4I 兼容，避免隐式 64 位假设、无界分配、C99 初始化和不可解释的编译器扩展。正式构建只能使用 `scripts\build.bat` 或 `scripts\stage.bat`。

## 明确非目标

Positron 不承诺现代浏览器完整标准、任意网站兼容性、完整 CSS/Selectors、通用 DocumentFragment/Node tree mutation、MutationObserver、Range/Selection、完整 live collection、bfcache、复杂滚动树、pinch zoom、transforms、CORS/绝对 URL 策略或 OEM 视觉。真实触摸、SIP/IME、picker、旋转、DPI、字体和失败网络仍须按测试文档进行人工验收。

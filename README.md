# Positron

Positron 为 Windows Mobile 6 Professional / Windows CE 5.2 补齐现代网络、数据、
图像、脚本和网页运行能力。项目既提供可被普通 WM6 C/C++ 程序独立使用的 DLL，也在这些
DLL 之上建设轻量浏览器与应用运行时。

项目仍处于持续开发阶段。基础 DLL 和一批浏览器纵向能力已经在 WM6 ARMV4I 模拟器或设备上
验证，但 Positron 还不是完整、兼容现代 Web 标准的通用浏览器。

## 能力概览

| 组件 | 用途 | 当前边界 |
|---|---|---|
| `positron_tls.dll` | TLS 1.2、证书链和主机名验证、嵌入式 CA bundle | 基于固定版本 Mbed TLS 2.16.12；不安全连接仅供诊断 |
| `positron_json.dll` | UTF-8 JSON 解析和序列化 | cJSON 1.7.18 的稳定 opaque-handle C ABI |
| `positron_http.dll` | HTTP/1.1 GET/POST、进度回调、重定向 | HTTPS 使用 Positron TLS，明文 HTTP 使用 WinInet |
| `positron_image.dll` | BMP/PNG/JPEG/GIF、SVG、像素缓冲和编码 | 设备位图格式依赖 WM Imaging codec；SVG 是受限子集 |
| `positron_script.dll` | 独立 JavaScript 执行服务 | Duktape 2.7.0；有时间、内存、源码和 native callback 上限 |
| `positron_core.dll` | HTML/DOM、CSS、布局、绘制、命中、表单和资源发现 | 基于移植的 NetSurf 3.11 组件；网页兼容性仍在扩展 |
| `positron_browser.dll` | 浏览器 session、history、脚本 session/bootstrap、受控 DOM/Event/关系/Attr/childNodes/Node identity 集合、表单/输入、URL、storage、编码、Headers、同步 Request/Response、bounded Promise、AbortSignal、timer/message pump、端口/广播、性能快照和窗口别名数据模型 | 不拥有窗口、网络或原生校验提示；这些 Web API 是 session 内 bounded 兼容切片，Promise 需宿主显式 microtask pump，DOM/Attr/childNodes/Node position 关系只读且按 id 寻址，完整 DOM、fetch/stream、core 事件传播及控件副作用仍由宿主提供 |

所有公共接口都使用稳定 C ABI、UTF-8 字符串、opaque handle 和明确的释放函数。NetSurf、
Duktape、Mbed TLS 等实现细节不暴露给调用者。

### 浏览器运行时

`test_host.exe` 是回归宿主和示例浏览器消费者，不是发布时的浏览器运行时。它已经接通：

- verified HTTPS 与明文 HTTP 页面加载；
- HTML/CSS 解析、外部样式和图片资源、分阶段异步抓取；
- GDI 绘制、滚动、动态 viewport/DPI、横竖屏重排；
- 常见 block、inline、flex、table、list、图片和基础 positioning；
- 链接导航、有限历史、表单控件、文本输入和一组 DOM 事件；
- 显式开启时的 classic inline/external JavaScript 与受限 DOM/Event/location/history bridge。

浏览器 JavaScript 默认关闭。`positron_script.dll` 是独立的 JavaScript 引擎封装；浏览器
运行时由 `positron_browser.dll` 与 `positron_core.dll`、`positron_script.dll` 及宿主回调
组合。目前 history/session、浏览器脚本 context 的所有权、host JSON callback 注册、browser
bootstrap、DOM 只读（按 id 查询与 textContent 读取）、textContent 写入、attribute、input value、checked、
disabled、表单属性（含 `name`/`action`/`method`/`enctype`/`target`/`autocomplete`/`acceptCharset`）与 submitter `formAction`/`formMethod`/`formEnctype` 反射、控件属性 `placeholder`/`autocomplete`/`inputMode`/`type`、约束属性（含 `pattern`/`minLength`/`maxLength` 反射）、控件与受限 form-level `checkValidity()`/`reportValidity()`/`willValidate`/`validity` 查询、
`setCustomValidity()`/`validationMessage`（含固定英文内置 fallback，不做本地化）、form property（defaultValue/defaultChecked/selectedIndex）、navigation JSON 分发、同文档
location/history 事件分发、event 回调分发、native input/composition、keyboard、focus-family、
EDIT change/post-change input、click、programmatic `HTMLElement.click()`（file input 只到 typed click，系统 picker 仍由宿主 GUI 负责）、submit/reset、invalid/reportValidity、file-input input/change、checkbox/radio input/change 和 SELECT input/change typed dispatch entry 已迁入；其余 form/input 适配、core 事件传播以及窗口、网络、控件和
history/navigation side effect 仍由宿主提供。
`reportValidity()` 只执行当前受支持的约束查询并派发可寻址控件的 `invalid` 事件；它不显示
原生提示、不自动聚焦/滚动，也不提交表单。
当前脚本 session 还提供受限的 `dataset`、FormData/Headers/Storage/DOM iterator、同步
Request/Response JSON 与 one-shot body、clone ownership、Blob/File metadata/slice/JSON、URL
authority/default-port 与 URLSearchParams pair/delete-value/按值查询和 mutation-safe snapshot、
cookie Max-Age 删除、TextEncoder/TextDecoder 选项、AbortSignal 静态工厂和 tags、setImmediate、
MessagePort/BroadcastChannel、structuredClone、PerformanceObserver/EntryList 快照与选项校验、
navigator 方法、`screen.orientation`、window aliases/open-close no-op 和稳定 element wrapper
identity；这些能力不发起网络、不创建后台线程，并由宿主显式 timer/message pump 或同步 snapshot
驱动。
当前还提供 bounded Promise 构造器、`then`/`catch`/`finally`、`resolve`/`reject` 和四种组合器；
reaction 必须由宿主显式调用 `PBrowser_ScriptSessionRunMicrotasks()` 推进，handler 与组合器输入均受
64 项上限。
当前还提供按 DOM id 的父子/兄弟、tag/name、form owner、`children`、`contains()`、基础
`compareDocumentPosition()`、受限 `matches()`/`closest()`、元素作用域 querySelector 和
`form.elements` collection；这些是同步、只读、session-scoped snapshot，不是完整 live DOM。
`childNodes`、`children`、`form.elements` 和元素作用域 `querySelectorAll()` 结果还提供
有界的 `forEach()`、`keys()`、`values()`、`entries()`、默认 iterator 与 `Symbol.toStringTag`；
`children`/`form.elements` 保留 `namedItem()`，不承诺 live 更新。
next585 又把 `document.documentElement`、`document.head` 和 `document.body` 接入同一受控
DOM snapshot：它们拥有稳定 wrapper identity、`parentNode`/`parentElement`、child/sibling、
`children`/`childNodes`、基础 root selector 和 Node position/contains 视图。core 通过三个保留
结构 token 识别这些没有 HTML `id` 的节点；这仍是同步、只读、session-scoped 边界，不是通用
DOM 创建、mutation、live collection 或完整 selector 引擎。
next586 又增加 browser-owned 的 `document.doctype` 只读 snapshot，并明确 document 的
`childNodes` 顺序为 `[doctype, documentElement]`、`children` 仍为 element-only；它不新增 core
ABI 或通用 doctype parser/mutation。对应 `TEST662–681` 已由自动设备门覆盖。
next587 又为 document、DocumentType、HTML element、CharacterData 和 Attr wrapper 增加受控的
`baseURI`、HTML/XML namespace、`prefix`、`lookupNamespaceURI()` 与 `isDefaultNamespace()`；
`baseURI` 随当前 session URL 更新，未知 namespace 请求 fail closed，不实现 namespace parser、
节点创建或 mutation。对应 `TEST682–701` 已由自动设备门覆盖。
next588 又在 document 与 HTML element wrapper 上增加受控的 `getElementsByTagName()` 和
`getElementsByClassName()`：结果按当前 snapshot 的 DFS 文档顺序返回 HTMLCollection，支持
大小写归一、`*`、多 class token、`item()`/`namedItem()` 与迭代协议；element 查询排除 owner，
document 查询包含 `documentElement`，空白/未知输入 fail closed。它仍是静态 snapshot，不是
live collection、通用 selector 或 mutation API；对应 `TEST702–721` 已由自动设备门覆盖。
next589 又把同一受限 matcher 接入 document 的 `querySelector()`/`querySelectorAll()`，支持
tag、`#id`、class、有限 attribute、compound、`*` 和 `:root`，按 DFS 顺序返回首个匹配或
NodeList snapshot；空白和组合器仍 fail closed。对应 `TEST722–741` 已由自动设备门覆盖。
next590 又补齐 document named collection projection：`document.getElementsByName()` 按显式
`name` 精确匹配返回 NodeList snapshot，`document.forms`、`document.images`、`document.scripts`
返回静态 HTMLCollection；四者复用 `item()`/`namedItem()`、迭代器和 wrapper identity，但不
提供 live collection、通用 named properties 或 DOM mutation。对应 `TEST742–761` 及兼容/回归门
已由自动设备门覆盖。
next591 又补齐 `document.links` 与 `document.anchors`：前者按 DFS 收集显式 `href` 的 `a`/`area`，
后者按 DFS 收集显式 `name` 的 `a`，均返回静态 HTMLCollection，并复用既有集合协议和
wrapper identity；属性变化只影响后续查询。对应 `TEST762–781` 及缩减兼容/回归门已由自动
设备门覆盖，本批未重复旧的全量回归。
next592 又补齐 document 与 element 的 `getElementsByTagNameNS()`：支持通配或精确 namespace、
通配或大小写敏感 localName，document 包含 root、element 排除 owner，返回静态
HTMLCollection 并复用集合协议和 wrapper identity；空/未知输入 fail closed，不引入 XML/SVG
parser、live collection 或 mutation。对应 `TEST782–801` 及兼容/缩减回归门已由自动设备门覆盖，
本批仍未重复旧的全量回归。
next593 又在同一属性 snapshot 上提供只读的 `getAttributeNS()`、`hasAttributeNS()`、
`getAttributeNodeNS()` 及 Attr 的 `namespaceURI`/`prefix`/`localName` 元数据；null/空 namespace
表示无 namespace，`xml`/`xmlns` 映射已知 XML/XMLNS namespace，未知输入 fail closed。它不引入
namespace mutation、XML/SVG parser、live collection 或节点创建；对应 `TEST802–821` 及兼容/缩减
回归门已由自动设备门覆盖，本批仍未重复旧的全量回归。
next594 又为既有 `NamedNodeMap` 增加只读的 `getNamedItemNS()`：它复用 null/空 namespace、
XML/XMLNS 已知前缀、未知输入 fail closed、大小写敏感 localName、coercion、Attr identity 和
属性增删后的 map 观察语义；不引入 namespace mutation、XML/SVG parser 或节点创建。对应
`TEST822–841` 及缩减回归门已由自动设备门覆盖。
next595 又补齐受控的 `lookupPrefix(namespace)`：document、DocumentType、HTML element、
CharacterData 和 Attr wrapper 都能读取已知 XML 的 `xml` 映射；只有对应 `xmlns:*` Attr 返回
XMLNS 的 `xmlns`，HTML default、null/空值和未知 namespace 均返回 `null`。参数只做有限
String coercion，不解析 namespace declaration，不提供 prefix mutation 或新的 core ABI；
`TEST842–861` 定向门与 `TEST389,390–448,540,549,642–861` 缩减回归均已通过。
next596 又补齐元素 wrapper 的有界 `setAttributeNS()`/`removeAttributeNS()`：null/空 namespace
只接受无前缀名称，XML/XMLNS 只接受对应 `xml`/`xmlns` 前缀；未知 URI、未知前缀、空名和多重
冒号安全无操作。成功写入复用既有 attribute bridge，不实现完整 NamespaceError、namespace
declaration、XML/SVG parser 或新的 core ABI；`TEST862–881` 定向门和
`TEST389,390–448,540,549,642–881` 缩减回归均已通过。
next597 又补齐 `NamedNodeMap.setNamedItemNS()`/`removeNamedItemNS()` 与元素
`setAttributeNodeNS()`：跨 owner Attr 只复制名称和值，不转移 source owner 或 wrapper identity；
未知/非法 namespace、qualified name、非 Attr 输入和缺失删除均 fail closed。该批不引入新的
core ABI、完整 NamespaceError、namespace declaration、XML/SVG parser、节点创建或 live
collection；`TEST882–901` 定向门和 `TEST802-901` namespace 缩减回归均已通过。
next598 又补齐 Attr 的 bounded leaf-node 语义：`isId`、live `textContent`、空
`childNodes`/`hasChildNodes()`、null parent/sibling/child relations，以及 identity-based
`isSameNode()` 和受控 name/value `isEqualNode()`。Attr 的 `ownerElement` 仍只是 owner metadata，
不伪造成 element tree parent；`TEST902–921` 定向门和 `TEST802-921` 缩减回归均已通过。
next599 又补齐 Attr 的 detached-node relation 语义：`isConnected=false`、`getRootNode()` 返回
自身、`contains()` 只包含自身，`compareDocumentPosition()` 对非自身对象返回固定的
`DISCONNECTED|IMPLEMENTATION_SPECIFIC`。这仍不把 `ownerElement` 伪造成 parent，也不引入新的
core ABI；`TEST922–941` 定向门和 `TEST802-941` 缩减回归均已通过。
next600 又让 `childNodes` 返回的文本、注释和无 id 子节点 wrapper 暴露 bounded `contains()`：
wrapper 自包含，父元素可包含直接子节点，owner/兄弟/document/非法对象均 fail closed；实现复用
受控 relation bridge，不引入文本 mutation、节点创建或新的 core ABI。`TEST942–961` 定向门和
`TEST802-961` 缩减回归均已通过。
next601 又为 `document.doctype` snapshot 补齐只读 `publicId`、`systemId`、`internalSubset`：
HTML doctype 默认分别为 `""`、`""`、`null`，且不可重定义或删除；它不改变既有
DocumentType 的关系、identity、namespace 或 baseURI。`TEST962–981` 定向门和
`TEST802–981` 缩减回归均已通过。
当前还提供按 DOM id 的属性 count/name/value，以及 `getAttributeNames()`、`attributes`/`Attr`
和受限 NamedNodeMap lookup/iterator；`Attr.value`/`nodeValue` 复用既有同步 attribute bridge，
同 owner 的普通 map 更新可用，普通 `setNamedItem()` 跨 owner 仍 fail closed；namespace-node
入口的跨 owner 行为是受控名称/值复制，indexed access 只保证 0–7。浏览器 bootstrap
当前还提供按 DOM id 的有界 `childNodes` 快照：它保留文本、注释和无 id 元素的直接子节点顺序，
暴露 `Node`/`CharacterData` 的只读元数据、父子/兄弟及 element-sibling 关系、`item()`/iterator、
`firstChild`/`lastChild`/`hasChildNodes()` 和稳定 wrapper identity；next583 又补充受控
`isSameNode()`/`isEqualNode()`、`getRootNode()`、文档节点元数据、位置常量以及同一快照树内的
`compareDocumentPosition()`/`contains()`。这些方法只读、session-scoped 且对未知对象 fail closed；
它不创建通用 DOM 节点，也不写回文本节点。浏览器 bootstrap 使用十三个顺序 IIFE 和 608 KiB
session heap ceiling；独立
`positron_script.dll` 默认堆仍为
512 KiB。它不是第二套引擎。
两者的关系和所有权见
[架构说明](docs/ARCHITECTURE.md)。

## 快速开始

### 前置环境

- Visual Studio 2008 SP1
- Windows Mobile 6 Professional SDK
- WM6 Professional Emulator 或兼容 ARMV4I 设备
- Python 3（用于移植脚本和仓库审计）

微软工具链和设备镜像不能随仓库分发，必须单独安装。

### 构建

在仓库根目录运行：

```bat
scripts\build.bat Debug build
```

正式脚本会调用 VS2008 的解决方案配置：

```text
Debug|Windows Mobile 6 Professional SDK (ARMV4I)
```

不要绕过解决方案直接拼装工具链。完整参数、输出位置和常见故障见
[构建与部署](docs/BUILDING.md)。

### 手工部署到模拟器共享目录

关闭仍在运行的 `test_host.exe`，然后运行：

```bat
scripts\stage.bat
```

脚本会先增量构建，再把七个产品 DLL、`test_host.exe`、`test_host.ini` 和 fallback
字体复制到 `C:\WMShare\`。在模拟器中把该目录配置为 Shared Folder，然后从对应的
Storage Card 路径启动 `test_host.exe`。

如需隔离候选包：

```bat
scripts\stage.bat Debug C:\WMShare\Positron-candidate
```

### 自动设备门

先在 WMDC/Device Emulator GUI 中连接一个设备；USB 真机和 DMA emulator 均可，当前连接
必须已经可用。gate 通过 32 位 RAPI 直接消费 WMDC 的当前设备，不枚举或绑定 VMID，也不会
连接、选择、启动、Cradle、断开或重置设备。连接完成后，在仓库根目录运行：

```bat
scripts\device_gate.bat -Candidate nextNNN
```

脚本会使用正式工程配置增量构建，创建隔离 staging，部署完整候选包，启动
`test_host.exe`，有限等待并回收日志，最后检查所选测试、设备指标、`ERROR`/`FAIL`、
TEST13 导航和 `TESTBENCH PASS`。每次运行前只回收设备端由 gate 自己命名的旧候选目录；
完整本地证据保存在被 Git 忽略的 `tmp/device-runs/`。

如果 RAPI 初始化报告 `0x8007007E`，WMDC 的旧式 COM 路径可能未被现代 Windows 正确展开。
运行一次下列脚本并确认 UAC；脚本只修复已知 WMDC/RAPI COM 注册，遇到未知值会拒绝修改：

```bat
scripts\repair_wmdc_rapi.bat
```

`-Candidate` 只命名本次设备端目录和证据目录；它不会切换 Git 版本，也不会改变测试选择。
实际代码来自当前工作区，实际测试选择来自当前 `test_host/test_host.ini`。定向测试、失败行为
和人工验收边界见[测试指南](docs/TESTING.md)。

## 仓库结构

```text
Positron.sln              VS2008 主解决方案
positron_tls/             TLS 1.2 与证书验证
positron_json/            JSON 公共 DLL
positron_http/            HTTP 公共 DLL
positron_image/           位图和 SVG 公共 DLL
positron_script/          独立 JavaScript 公共 DLL
positron_core/            HTML/CSS/DOM/layout/paint 产品边界
positron_browser/         浏览器 session/history 产品组合层
test_host/                回归宿主和示例消费者
samples/                  独立 DLL 示例
scripts/                  正式构建、stage、移植和审计脚本
docs/                     面向维护者和使用者的文档
.agents/                  仅供 agent 接管的动态状态
third_party/              直接 vendoring 的第三方源码和资源
netsurf-all-3.11/         NetSurf 3.11 上游源码快照
```

解决方案还包含若干移植后的内部静态库（`positron_expat`、`positron_hubbub`、
`positron_libcss`、`positron_libdom`、`positron_libjpeg`、`positron_libsvgtiny`、
`positron_netsurf`）。调用者应只依赖产品 DLL 的公共头文件，不应直接链接或包含
NetSurf、Expat、libjpeg-turbo 或其他内部接口。

### 子项目 README

每个解决方案工程的目录 README 都说明了工程输出、边界和调用方式：

- 公共 DLL：[`positron_tls/`](positron_tls/README.md)、[`positron_json/`](positron_json/README.md)、
  [`positron_http/`](positron_http/README.md)、[`positron_image/`](positron_image/README.md)、
  [`positron_script/`](positron_script/README.md)、[`positron_core/`](positron_core/README.md)、
  [`positron_browser/`](positron_browser/README.md)；
- 内部静态库：[`positron_expat/`](positron_expat/README.md)、[`positron_hubbub/`](positron_hubbub/README.md)、
  [`positron_libcss/`](positron_libcss/README.md)、[`positron_libdom/`](positron_libdom/README.md)、
  [`positron_libjpeg/`](positron_libjpeg/README.md)、[`positron_libsvgtiny/`](positron_libsvgtiny/README.md)、
  [`positron_netsurf/`](positron_netsurf/README.md)；
- 消费者：[`test_host/`](test_host/README.md) 和 [`samples/positron_image_demo/`](samples/positron_image_demo/README.md)。

`netsurf-all-3.11/`、`positron_tls/mbedtls/` 和 `third_party/` 下的 README 是上游或
第三方说明，保持其来源语境；它们不是 Positron 公共 API 文档。

## 文档

- [文档索引](docs/README.md)
- [架构与公共边界](docs/ARCHITECTURE.md)
- [构建与部署](docs/BUILDING.md)
- [测试与验收](docs/TESTING.md)
- [故障排查](docs/TROUBLESHOOTING.md)
- [历史里程碑](docs/history/README.md)
- [第三方组件与许可证](THIRD_PARTY.md)

Agent 的工作纪律由 [AGENTS.md](AGENTS.md) 定义，动态交接只维护在 `.agents/`。这些文件
不是面向普通仓库读者的产品说明。

## 开发约束

- 目标编译器是 MSVC 9.0；项目源码和移植产物必须满足 C89/VS2008 约束。
- 公共 DLL 保持 C ABI、UTF-8、opaque handle 和跨 CRT 安全的内存所有权。
- Windows Mobile 已有能力优先复用 WinInet、GDI、WM Imaging 和 CryptoAPI。
- 新协议、解析器、编解码器或 runtime 优先移植成熟上游项目，并记录固定版本、来源和许可证。
- 自动测试不能替代字体、布局、触摸、SIP、旋转和真实网页的人工观察。

贡献前至少运行：

```bat
python scripts\test_c89ize.py
python scripts\audit_repo.py
```

涉及 C 源码时，还应使用正式解决方案构建，并按风险运行对应设备门。

## 许可证

Positron 自有代码采用 [MIT License](LICENSE)。仓库包含 GPLv2、Apache-2.0、MIT、OFL、
zlib/IJG 等不同许可证的第三方组件；分发前必须同时遵守
[THIRD_PARTY.md](THIRD_PARTY.md) 和各上游目录中的许可证文件。

# 测试与验收

Positron 的验证分为主机静态检查、VS2008 ARMV4I 构建、自动设备门和必要的人工设备验收。单一层级通过不能替代其他层级：桌面脚本不能证明 ARM 二进制可用，自动断言和首帧也不能证明真实触摸、SIP 或视觉结果。

逐测试实现以 [`test_host/main.c`](../test_host/main.c) 为准；当前候选、设备连接状态和最近证据只写入 [`.agents/HANDOFF.md`](../.agents/HANDOFF.md)。本文只保留长期有效的操作、判定和能力分组，不维护会漂移的逐编号历史。

## 测试宿主的职责

`test_host.exe` 是公共 DLL 的回归宿主和示例消费者。它负责窗口/消息接线、测试 fixture、平台 callback、断言和日志；URL、DOM、事件、表单、资源、布局以及生命周期语义必须由对应公共 DLL 提供。测试编号是宿主实现细节，不是公共 ABI。

新增测试时应同时修改源码 dispatch、相邻断言和必要的测试说明。不要把产品实现源文件加入宿主工程，也不要为了让宿主通过而复制一份公共语义。

## 能力矩阵与未实现入口

九个顶层 DLL 的主干能力状态、预算、错误边界和提升条件统一见
[`CAPABILITIES.md`](CAPABILITIES.md)。矩阵中的“有界待扩展”不是已支持行为；它只表示已有
相邻公共边界，或已确认值得调查但仍缺少消费者证据。

以后若公开一个暂未实现的入口，必须在同一模块的离线夹具中证明：非法参数、容量不足、stale
handle、缺失 callback 和 unsupported 输入都会在状态改变前失败；没有伪造成功 handle、部分
body、部分 DOM mutation 或意外 callback。新增入口应优先使用 size/version 或 `Ex` 形式，旧
ABI 的字段和成功语义不得被改写。

图像资源的异步消费者现在使用 `PCore_FetchImageResourcesEx()` 的显式
READY/PENDING/TERMINAL_FAIL 合同；旧的 `PCore_FetchImageResources()` 保持同步
成功/终态失败语义。PENDING 不写入终态 failed cache，worker 完成后必须再次扫描并重新
layout/paint。Core 的离线回归见 TEST1313；它只证明 cache/retry/decode 边界，不替代
应用设备门上的真实图片可见性。

HTTP 消费者接线使用 URL-aware `PHttp_GetUrl[Ex]`/`PHttp_PostUrl[Ex]`，并在资源成功后
查询 `PHttp_ResponseGetFinalUrl()`，以便重定向后的 CSS、图片和 `@import` 继续以最终 URL
解析相对引用。旧 `PHttp_Get[Ex]`/`PHttp_Post[Ex]` 只保留给 ABI 回归；新增 HTTP 响应字段
不得直接扩展 `PHttpResponse`。HTTP body 读取、Content-Length 截断、chunked 解码、TLS→HTTP
降级和容量失败都必须在宿主看到 body 前变成可判定的失败。

数据库同步消费者使用 `positron_db.dll` 的离线 contract，不在 `test_host` 中复制 SQLite
或 REST 业务实现。TEST1321 当前覆盖本地完整 SQL 打开、同步 migration、单列主键约束、
同步表注册、typed bind/column、整数边界、REAL/NULL/TEXT/BLOB 行写入、dirty queue/outbox
事务合并、request size-probe、accepted/pull/conflict 响应、失败响应原子回滚、服务器权威行、
retry-local 冲突处理、冲突列表、模式隔离和文件重开持久化。继续扩展该纵切时必须保持以下边界：

- 直接 DDL（包括 virtual table）、`ATTACH`/`DETACH`、事务/savepoint、非法 `PRAGMA`、扩展加载和多语句用户 SQL 在同步模式
  fail closed；migration 中途失败不得推进 schema version；
- 非 2xx、malformed/超限 JSON、schema hash/version 不匹配和错误 typed value 不得部分推进
  本地行、outbox 或 cursor；
- HTTP worker、HTTPS、Bearer Token、重试和 UI 消息属于宿主；DB DLL 不保存 token、不创建
  线程，也不把 SQL 文本发到服务器；
- 设备验收还需覆盖真实 SQLite 文件重开、journal 恢复、断网重连、401/5xx、分页、tombstone
  和断电后的 outbox/cursor 持久化；没有这些证据时只能写成主机 contract 已通过。

独立 REST contract 可用 `python scripts\db_sync_fixture.py --token fixture-token --port 0` 启动；
它只在内存中保存 typed row、版本和 tombstone，支持 `op_id` 幂等、版本冲突、分页和 Bearer
Token 检查，不执行远程 SQL。端口由启动输出给出，测试完成后由启动它的宿主停止；它不是生产
服务器，也不替代 WM6 上的 `positron_http.dll`、DB worker、HTTPS 和断电门。

## `test_host.ini`

INI 必须和 `test_host.exe` 位于同一目录。最小自动配置如下：

```ini
auto=1
javascript=0
tests=13,20,27,999
```

`tests` 接受逗号或空格分隔的编号和范围，也接受源码明确支持的特殊编号，例如：

```ini
tests=1-5 7b 13 20,999
```

文件不存在时宿主进入交互式分组选择；文件为空、不可读或格式错误时提示并退回分组选择，不会静默扩大为全量。Nightly 打包和设备门从当前源码 dispatch 动态生成全量自动清单，因此不会把每天新增的测试硬编码在脚本中。

### 自动模式

`auto=1` 直接运行选择，抑制确认和结果 MessageBox，同目录 `test_host.log` 覆盖写入。自动可视测试至少绘制一帧后自行关闭；任何断言失败都会使批次失败，只有唯一的 `TESTBENCH PASS` 才是完整通过。

自动模式证明断言、资源计数、消息路径和首帧没有失败，不证明字体、边距、抗锯齿、触摸命中、系统 picker、旋转或 OEM 输入法体验。导航日志中的资源终态、失败分类、重试次数、required/optional gate 和脱敏摘要由 Browser DLL 提供，宿主只负责调度、读取和记录。

### 手动模式

`auto=0` 保留启动确认、测试说明和人工关闭流程。可视页面通常停留在设备上，验收者按页面说明操作，再使用 `Esc`、页面空白处或测试指定入口继续。交互模式不保证完整自动日志；截图、设备信息和操作记录放在本地 `tmp/`。人工观察后如需机器判门，应以相同选择再运行一轮 `auto=1`，但自动日志不能替代人工结果。

### 浏览器 JavaScript 与完成提示音

- `javascript=0` 是默认产品路径，不执行页面 classic script。
- `javascript=1` 显式启用实验性的 Browser script session、受限 DOM/Event/input/navigation bridge 和 classic script。
- 独立 `positron_script.dll` 测试不依赖这个开关；开启它也不表示支持完整 DOM、Web API、ECMAScript host environment 或浏览器安全沙箱。
- TEST999 是专用完成提示音。只有显式选中且前序批次未失败时，宿主退出前请求一次系统提示音。声音、窗口关闭或部分 `OK` 都不能替代日志判定。

## 四种常用配置

| 目标 | 配置 |
| --- | --- |
| 部分测试、自动断言 | `auto=1`，`tests=` 写编号；按需保持 `javascript=0` |
| 部分测试、人工模式 | `auto=0`，`tests=` 写编号；脚本 fixture 再启用 JavaScript |
| 所有自动安全测试 | 使用 nightly 生成的 INI，或让设备门按当前 dispatch 生成清单 |
| 所有测试、人工模式 | 在全量清单中加入发布说明列出的 manual-only fixture，并设 `auto=0` |

移走或改名 INI 只会进入旧式分组选择，不等于自动运行所有测试。manual-only fixture（当前为 TEST232、TEST263、TEST1310）不得放进 `auto=1` 清单；它们会主动拒绝自动运行，以免把未观察的人工行为伪装成通过。

## 能力回归分组

逐测试合同仍以源码和组件 README 为准，稳定文档只维护能力分组：

- 资源、导航、history、viewport、页面生命周期、脚本任务队列、焦点和窗口通知由早期资源/会话夹具覆盖；这些夹具共同验证候选 generation、required/optional gate、取消、旧页保留、滚动快照和事件顺序。
- 几何、overflow、滚动、selector、form owner、validation、submission、FormData、option/select 和图像 source 夹具覆盖 Core/Browser callback 的边界、预算、snapshot 隔离和 fail-closed 行为；参考宿主 TEST39 另以离线 column-flex fixture 断言 `flex-basis:0` 子项的实际高度计入自动高度父容器，footer 不得覆盖前置 section；真实控件、DPI、触摸和视觉仍属于人工验收。
- TEST24 验证文档内外部 CSS 解析缓存：相同 URL/bytes/resolver 的再次 style 不重复解析资源 URL，
  但 viewport 媒体条件与 class mutation 仍重新选择，resolver 身份变化先重新解析，下一次可复用新缓存。
  TEST45 保留 `@import` 树的逐次解析与原始 bytes 缓存回归；fixture callback 的 `pw` 必须匹配
  其上下文类型，不能把测试计数器传给要求导航 request 的生产适配器。
- DOM/CharacterData 夹具覆盖 Text、Comment、CDATA、属性、`textContent`、`innerHTML`/`outerHTML`、`document.write`、title、detached Element 与 bounded DocumentFragment。Fragment 只允许文档规定的有限根数和节点形状。
- TEST1322 覆盖 Browser 脚本 session 的有界内存 profile：旧 `Create` 仍为 1.5 MiB，应用使用 additive 的 `CreateEx` 3 MiB profile；低于默认或超过上限的请求 fail closed，约 120 KiB 的保留 classic script 可在 bootstrap 后执行，GC 后状态仍可用。该门证明预算和 ABI 合同，不证明任意第三方脚本兼容；固定版本 jQuery/Popper/Bootstrap 的真实执行状态另由应用 Debug 日志和 TEST1325–1327 记录，bootstrap-multiselect 仍保留为独立语法限制。
- TEST1284–1296 覆盖 CDATA 创建/物化、Text 合同、Fragment CharacterData staging、Core/live Element/detached Fragment 的 `Node.normalize()`、detached Element 的直接 CharacterData staging，以及 detached Element 物化后的有界 primitive `before()`/`after()`/`replaceWith()`、attached HTML mutation coherence、element-child projection、四位置 `insertAdjacentText()`/`insertAdjacentHTML()`、四位置 `insertAdjacentElement()`、直接 Element-child 的 `appendChild()`/`insertBefore()`/`removeChild()` 与 primitive-only `replaceChildren()`：空 Text/CDATA 被删除，相邻 Text/CDATA 合并到首个非空节点，Comment 保持边界；Text、Comment、CDATA wrapper 和物化 Element wrapper 可在 clone、replace、直接物化、移除、再次插入、同级文本/Element 突变、parser-backed `innerHTML`/`outerHTML` 替换、`children`/first-last element accessor 读取和相邻 mutation 之间保持有界 identity，Element 脱离后 direct 普通 Text/CDATA 快照仍保留数据。未物化 Element 的 relative/adjacent/child mutation 保持 inert，任意对象参数和完整 detached HTML parser 仍在 mutation 前拒绝或不承诺。
- TEST1297 在同一离线夹具中覆盖唯一的 bounded live `Element.getElementsByTagName()` collection：每次调用返回新对象，已连接 owner 的同一 collection 在子节点、id/name mutation 后刷新，created wrapper 保持 identity，索引、`item()`、`namedItem()`、`entries()` 和结束标记保持一致；最多访问 256 个节点并返回 64 项，刷新超限保留最近成功结果。其他 collection 仍按各自合同使用 bounded snapshot；这不是完整浏览器 live collection。
- TEST1298 覆盖 detached Browser-created Element 的 nested graph：最多 4 层、64 个 Element、每个 Element 64 个 child，所有 Element 需要唯一非空 id；递归 `parentNode`/`children`/`textContent`、`cloneNode(true)`、Core 物化、wrapper registry、remove/reinsert 和失败后的 detached owner 保持一致。循环、重复/缺失 id、超出预算和 nested Fragment 仍在 Core 触碰前拒绝。
- TEST1299 覆盖图像终态表与 `Element.id` setter 改名的边界：图像完成一次 `load` 后连续改名 80 次，并在每个新 id 上发送 host `load` 通知；旧 image/source 终态 key 必须回收，64 项上限不能因改名泄漏，最终 `currentSrc` 保持稳定。
- TEST1300 覆盖 generation-aware image source replacement：宿主以非零 generation 通知
  source A→B→A，旧 `decode()` 以 `EncodingError` 退休，Core 的 `currentSrc` 保持权威；
  legacy/旧 generation/相反终态通知 fail closed，失败候选只接受匹配 generation 的
  `error`。source Ex 只接受能解析到带 id `<img>` 的有界 `<picture>` 关系；Core 选择、
  fetch、decode、layout 和 paint 仍由宿主/Core 负责。
- TEST1301 覆盖 `PCore_MultipartSubmissionEncode()` 的公共 Core 合同：size probe、body/
  `Content-Type` 容量不足时无部分写入、成功控件顺序、quoted 字段/文件名、binary file
  bytes、缺少 file callback 的 fail-closed，以及 1 MiB body 上限。宿主只提供同步文件
  读取/释放 callback；Core 负责 boundary、CRLF 和 wire serialization。网络发送、文件
  权限和 native 表单视觉仍不在该夹具范围。
- TEST1302 覆盖 `PCore_FormDataEncode()` 的独立快照合同：默认 GET/urlencoded form 仍能
  通过 `PCore_FormDataByIdEx()` 生成 multipart body，successful-control 顺序、文件名、
  binary bytes、size probe、容量不足时无部分输出和缺少 file callback 均由 Core 断言；
  这证明 FormData 编码不依赖 form 的默认 method/enctype/action。它只编码 Core-owned
  snapshot，不接收 Browser JavaScript `FormData` pairs。文件 I/O、网络发送、Browser
  `formdata` 事件修改和 native 表单视觉仍由宿主或人工矩阵负责。
- TEST1303 覆盖 Browser 脚本 `FormData` 的固定 64 项预算：满容量时 `append()`、新键
  `set()` 和数组构造均抛出 `QuotaExceededError`，失败不改变既有 pairs；替换已有键以及
  删除后再次追加仍然成功。该夹具只证明脚本对象的内存边界，不扩展 File/Blob 内容、网络
  发送或 native 表单视觉；它也不证明脚本对象可以交给 `PCore_FormDataEncode()`。
- TEST1304 覆盖 Browser 脚本 `URLSearchParams` 的固定 64 项预算：满容量时
  `append()`、新键 `set()` 和 pair-sequence 构造均抛出 `QuotaExceededError`，失败不改变
  既有 pairs；替换已有键以及删除后再次追加仍然成功。该夹具只证明脚本对象的内存边界，
  不扩展 URL 解析、导航、网络发送或 native 表单视觉。
- TEST1305 覆盖 Browser 脚本 `sessionStorage`/`localStorage` 的固定配额：64 项满容量、
  256 字符键和 4096 字符值的新增或 named-property 写入抛出 `QuotaExceededError`，失败
  保留旧状态；既有键替换、删除后重新占用容量和独立 Storage map 仍然有效。该夹具不承诺
  持久磁盘、跨 session 同步或系统设置存储。
- TEST1306 覆盖 Storage 对 object-property 名称的安全处理：`hasOwnProperty`、`__proto__`、
  `constructor` 和 `toString` 通过 `setItem()`/`getItem()` 与 `toJSON()` 保持值、顺序和
  原型隔离，Storage 方法仍可调用，`clear()` 后旧值全部消失。
- TEST1307 覆盖 Browser `Headers` 的 object-property 名称：`set()`/`get()`、对象初始化和
  `toJSON()` 对 `hasOwnProperty`、`__proto__`、`constructor`、`toString` 保持 canonical
  header 值和原型隔离；`Request`/`Response` 的 metadata snapshot 复用这条 facade。
- TEST1308 覆盖 Browser 作者可控字符串 registry：特殊 DOM id 的 lookup/cache、事件 listener
  dispatch、`DOMStringMap.set()`/`toJSON()` 的 `__proto__`/`constructor`/`toString` 名称，以及
  `BroadcastChannel('__proto__')` 的消息隔离；这些断言只证明原型安全和同一 session 内的
  有界 identity，不扩展完整 named-property、异步网络或多窗口语义。
- TEST1309 覆盖参考宿主的顶层可见性接线：`PCoreWndProc` 收到
  `WM_SHOWWINDOW(FALSE/TRUE)` 时调用 Browser 的公开 visibility lifecycle API；重复的隐藏/显示
  消息由 Browser 去重，并保持 `visibilitychange`→`pagehide`/`pageshow` 顺序。该夹具只证明
  平台消息到公共 Browser 合同的映射，不把窗口可见性、bfcache 或系统 shell 策略扩展为
  Browser 自主行为。
- TEST1310 是 manual-only 的真实页面证据夹具：它通过 WM6 文件选择器选择 `test_host.ini`，
  在 `multipart/form-data` form 上构造 Browser `FormData`，并在页面显示文件名、类型、大小、
  `text()` 与 query 摘要，同时记录 `input`/`change` 顺序。当前预期是脚本只能看到有界的
  `File` metadata（通常 size/text 为空），本地路径和文件 bytes 不会进入 Browser；这条证据
  用来审查待取证的 Browser→Core 上传组合，不等于 multipart body 已经发送。
- TEST1311 是自动的 Core/GDI 高 DPI 回归：同一个多行 inline 文本在显式的 96 与 192 DPI
  device-backed viewport 中分别 style、layout 和离屏 paint，再按 Core 返回的每个 visual
  fragment 检查实际 glyph 行。它验证测量与绘制共用 `PCore_SetDeviceViewport` 的 DPI、字号
  定点精度不会因 paint HDC 的 `LOGPIXELS` 分叉，以及高 DPI 不会退化为少数裁剪像素；它不
  取代真实设备的字体、边距、旋转和触摸视觉验收。
- TEST1313 是自动的 Core 异步图片资源回归：第一次扫描让一个 SVG 返回
  `PCORE_IMAGE_FETCH_PENDING`、另一个返回终态失败；第二次扫描必须只重试 pending URL，
  把成功 bytes 写入 Core cache，完成 SVG 解码并生成 image box，同时终态失败不得重复
  callback；第三次扫描还必须保持缓存命中。该夹具不证明 HTTP 下载、相对 URL 解析或真实
  设备上的图片绘制。
- TEST1314 是自动的 `positron_image` 直接绘制回归：它使用从当前 IANA 首页与 header SVG
  提取的离线最小夹具，覆盖 `<style>` class 的绿色/蓝色 paint、渐变引用、无显式
  width/height 的 viewBox viewport、`preserveAspectRatio`、重复 retained draw，以及坏 SVG
  不留下句柄。它验证 class 样式缺口属于 Image DLL，并不把 Core 的 CSS background-image
  尺寸/定位或 `positron.exe` 真实页面视觉写成已完成。
- TEST1315 是自动的 exact-IANA Image 回归：它从可执行文件旁的 tracked fixture 读取完整
  `iana-logo-homepage.svg` 与 `iana-logo-header-notext.svg`，保留 XML prolog/DOCTYPE、
  `450x175` 与负 viewBox、多个 class、六个渐变、`display:none` 的 `Text_Paths` group，
  并检查可见 path 数量、非白像素边界、绿/蓝像素、重复绘制和释放后的重新解析；额外以
  `viewport_w=0, viewport_h=0` 验证 homepage 使用 `450x175`、负 viewBox 的 header 使用
  `128x50` 作为自然画布，而不是历史 300x150 或过大的 300x117 tile。TEST1314
  只使用三条人工构造的简化 path，无法触发真实文件的长路径、隐藏 group 和多渐变边界，因而
  即使它通过也不能证明 IANA 首页 Logo 完整。
- TEST1316 是自动的 Core/GDI 背景图回归：一个 intrinsic `160x80`、只在下半部绘制绿色路径
  的 SVG 被放入 `160x40` 的非重复 CSS background。测试同时断言资源 fetch/free、盒尺寸和离屏
  像素，要求 Core 在绘制前按 preserve-ratio contain 缩小并保留 `background-position`，使原本
  会被 intrinsic 高度裁掉的下半部可见。它覆盖的是 Core 的有界响应式 SVG 背景适配，不宣称
  完整 `background-size`/`cover`、多层背景、重复背景或浏览器级 CSS 兼容；真实 `positron.exe`
  页面仍需设备视觉门确认。
- TEST1317 是自动的 exact-IANA Core 背景回归：它使用完整 homepage/header fixture 和首页
  响应式 `h1` 规则，在 320px、128-DPI 的 device-backed viewport 下必须选中 `128x50` CSS
  像素的 header SVG，实际物理 h1 为 `171x67`，并断言完整宽高边界及绿/蓝像素。它逐像素
  比较物理 h1 背景与 Image DLL 按同一尺寸直接绘制的结果，防止默认 `background-repeat` 把
  300x117 的旧 tile 或未缩放的 128x50 tile 裁成残片。它证明 Image 自然尺寸、Core 高 DPI
  重复 tile 和背景绘制已接线，不替代 `positron.exe` 的网络页面视觉验收。
- TEST1318 是自动的完整 CSS data-URI 图片回归：同一离线页面的两个背景分别使用
  percent-encoded 与 Base64 `data:image/svg+xml`，必须从 libcss computed style 经过 Core
  资源发现、document image cache、`positron_image.dll` SVG 解码到离屏 GDI paint；Core 的
  host fetch callback 调用次数必须为零，两个黑色汉堡条像素必须可见。第三个损坏 URI 和
  第四个含 65 个 `path` 的复杂度超限 URI 必须被发现但不写入成功 cache、不影响前两个背景。
  URL/decoded/复杂度预算分别由 Core 强制；该 fixture 不修改应用接线，也不把 `data:` 送进
  HTTP。TEST19/20 继续覆盖网络 callback/cache 的 BMP、PNG、JPEG、GIF，TEST1316/1317
  继续覆盖普通网络 SVG 与 CSS background 适配。
- TEST1319 是自动的精确 Image paint 回归：它把 WinWorld navbar 的 30x30 SVG 直接交给
  `PImage_CreateSvgFromMemory()`/`PImage_DrawSvg()`，断言三条 `rgba(0, 0, 0, 0.5)`、2px
  路径在白底上产生半透明灰色像素，并检查首尾端点在 `stroke-linecap="round"` 下延伸、
  端点外仍保持白色。这个断言覆盖 libsvgtiny 的 rgba/stroke-opacity 解析、linecap 传递、
  NanoSVG alpha 合成和 GDI paint；仅创建句柄或 data URI 解码成功不能使它通过。普通不透明
  stroke、PNG/JPEG/GIF 和已有 IANA SVG 回归仍须保持通过。
- TEST1329 是自动的精确 Bootstrap CSS→Core→Image→GDI 回归：它保留
  `.navbar-light .navbar-toggler-icon` 后代选择器、显式 `type=button`、
  Bootstrap 的 `inline-block` button、30x30 percent-encoded `rgba()`/round-cap SVG，以及
  button 作者子树。测试
  要求 data URI 不调用宿主网络 callback，Core 产生普通 button gadget 和 span 盒，Image
  解码计数增加，并在最终离屏背景中检测三条灰色汉堡线。TEST1319 只验证 Image DLL，
  TEST1329 才覆盖 CSS computed style、Core image cache、作者内容保留和背景 paint 的完整
  链路；它仍不等于 `positron.exe` 真实网络页面的视觉/触摸验收。
- TEST1320 是自动的 Browser/Core document delegated-click 回归：离线页面在普通
  `button` 上注册 document capture/bubble listener，点击由 Core 事件目标派发后必须保留
  `event.target`、document `event.currentTarget` 和 1/3 capture/bubble phase；Bootstrap
  风格 handler 还要修改 `classList` 与 `aria-expanded`。夹具断言相同 callback/capture 的
  重复注册被忽略、`removeEventListener` 生效、`once` 只执行一次，并以 64 项固定 document
  listener 预算验证第 65 项 fail closed。该门只证明 DLL 的离线事件合同，不证明外部
  Bootstrap 脚本已经下载、执行或在 `positron.exe` 的真实 WinWorld 页面中改变菜单；这部分
  由后续 TEST1325–1327、应用 Debug `executed` 日志和匹配 DLL 设备门补足，TEST1320 单独不
  代替这些证据。
- TEST1325–1329 是同一条 WinWorld navbar 纵切：1325 用未修改的 jQuery 3.5.1 和 Bootstrap
  4.6.2 通过 `Element.click()` 验证 delegated collapse；1326 把普通 button 放入直接 flex
  容器，通过 Core 布局和可信原生坐标事务验证 gadget 命中；1327 将两者合并，在显式
  `type=button`、没有 `id` 且包含 `.navbar-toggler-icon` 子 span 的生产形状 button 上通过原生
  click/commit 后用有界 `PBrowser_ScriptSessionRunTimers()` 推进过渡，再断言 `nav` 含 `show`、
  按钮 `aria-expanded=true`，再重新 style/layout 并要求菜单实际高度大于零；随后收起和再次展开，
  同时断言 class/ARIA 与隐藏/可见几何。class 已变但 CSS class-token cache 未同步仍会失败，
  不能把属性变化当作可见结果。目标菜单嵌套在 navbar 内，初始使用 `.collapse:not(.show){display:none}`，
  避免仅验证本来就可见的目标。Core 不把装饰性 span 变成事件目标的持久 DOM id；同步事件桥
  只提供短生命周期 token，使 Bootstrap 的 delegated selector 能读到真实按钮属性。1328 在
  flex、普通 block（仍保留一个默认 submit 兼容项）和定位 inline 路径中让 button 的 span 使用
  CSS data-URI 图标，分别对三个区域断言像素数量，避免其他区域的成功掩盖图标丢失；1329 进一步
  使用 Bootstrap 的精确 hamburger URI，并贯穿 Core 到最终 GDI 像素。夹具改变后须用匹配 DLL
  重跑；当前证据与包路径见 agent handoff，不在此维护运行时间线。该门证明 Core
  作者内容、flex/block/定位控件和 Browser/Core 事件链，不承诺 bootstrap-multiselect 等其他
  站点脚本、完整触摸视觉或现代 JavaScript 兼容。
- 应用 ScriptSession 另须验证实际 callback 适配：关系 callback 的数值请求允许
  `out_bytes=NULL`，UTF-8 长度探测/复制允许 `out_number=NULL`。接线错误可能让
  `getElementById()` 成功但 `querySelector()` 失败，`children` 也错误地为空；仅运行 test_host
  无法发现另一消费者的这类错误。使用应用 `--eval` 对同一稳定 id 比较直接查找、selector 和
  children，再以 `--click` 走真实 native-button transaction，检查目标 class/ARIA mutation 与
  timer 后重排日志。真实视觉和点按仍保留人工门。
- TEST232 是 manual-only 的真实 file-input 交互验收：选择成功后应保留 filename/path，并且
  页面事件 trace 必须恰好为 `input|file;change|file;`；再次打开 picker 后取消不得改变
  filename 或 trace。若 `input` 监听器先更新页面文字导致 Core retained layout 失效，参考
  宿主会在这条 `insertFromFile` 连续事务的两个事件之间执行一次有界重排；该调度只属于
  宿主的窗口/layout 接线，不把 file bytes 或 picker 语义复制进公共 DLL。

### Media 回归与设备门

`positron_media.dll` 的离线 fixture 应通过公开 source/output callback 验证：可 seek 与不可 seek
输入、`WOULD_BLOCK`、EOF、read/seek error、损坏/截断输入、重复 close、非法状态和 16 MiB
上限。WAV PCM/IMA ADPCM 还要断言 S16LE block 的 sample rate、声道、时间戳、暂停/恢复、seek
和 EOF；host fixture 不得把媒体实现源文件编译进 `test_host.exe`。

FFmpeg fixture 只使用仓库固定的 3.4.14 ARMV4I archive，覆盖 MP4/H.264/AAC、AVI/MJPEG/MP3、
WAV/PCM、MPEG-PS/TS、AMR 及选定裸流，并断言 I420、S16LE、profile/layout 拒绝、640×480
上限、回调停止和关闭后无回调。没有样本时不能把 `pm_probe()` 的桌面识别结果写成设备能力。

设备门在用户手动连接恰好一个 WM6 Emulator/真实 ARMV4I 目标后，使用同一批 staging 验证 WaveOut
格式接受、软解首帧/音频输出、启动延迟、帧率、丢帧、音频 underrun、峰值内存、时间戳与关闭耗时。
DirectShow graph 存在只能证明图创建，不足以证明 filter/codec、callback source filter 或 native
视频 renderer 可用；失败或 RAPI 阻塞必须保留为未验证，不放宽断言。

这些夹具证明的是有界公共合同，不是完整浏览器标准、任意网站兼容性、除 TEST1297 外的完整 live collection、MutationObserver、Range/Selection、通用嵌套 Fragment 或无限 DOM mutation。

## 本机验证

修改产品 C、移植代码或 C89 转换脚本后先运行：

```bat
python scripts\test_c89ize.py
```

提交前运行仓库审计：

```bat
python scripts\audit_repo.py
```

审计覆盖工程输入、版本 pin、许可证、Git 跟踪、UTF-8、Markdown 链接、文档职责和 `test_host` 产品边界。审计成功不代替构建、设备行为或宿主 helper 语义归属审查。

使用正式工程入口构建：

```bat
scripts\build.bat
scripts\build.bat Debug rebuild
```

局部低风险修改可以先增量构建；工程依赖、生成规则、静态库或无法解释的混包问题使用 clean rebuild。不要直接调用 ARM 编译器拼装部分目标。

## 自动设备门

### 前提与运行

先由用户在 WMDC 或 Device Emulator GUI 中手动建立恰好一个目标连接。设备门只复用当前 RAPI 会话：不枚举或选择设备，不绑定 VMID，不启动、cradle、断开、重置或默认强杀设备。

```bat
scripts\device_gate.bat -Candidate feature-name
```

定向批次使用 staging override，不修改 tracked INI：

```bat
scripts\device_gate.bat -Candidate feature-name ^
  -TestSelection "1284-1294,999" -EnableJavaScript
```

脚本执行正式构建、隔离 staging、整包部署、启动、有限等待、日志回收和判门。本地证据在 `tmp/device-runs/`，不进入 Git。未显式启用下方清理开关时，超时后的设备进程仍需由用户在设备 UI 正常结束；设备门不提供安全的通用远端终止。

当确认需要清除旧的 `positron.exe` 进程或同名 DLL 持有者时，可显式启用有界的强制清理：

```bat
scripts\device_gate.bat -Candidate feature-name -ForceTerminatePositron
```

该选项先部署并启动独立的 `device_tools/positron_process_cleanup.exe`，只匹配精确基名
`positron.exe` 或本设备门生成的 `test_host-run-*` 进程，记录每个 PID 的终止结果，并在摘要缺失或任一终止失败时阻断后续
`test_host.exe`。随后 test host 会再次执行同样的精确检查并把摘要写入 `test_host.log`。
未指定该开关时不会结束进程；它不能结束 `test_host.exe`、任意 DLL 名称或其他应用。
强制清理不是“设备门通过”的替代证据，仍需完整日志、模块路径和 crash 检查。

无论是否指定强制清理，设备门都会在启动 `test_host.exe` 前运行同一 helper 的只读
`--audit-modules` 模式。该模式不加载任何 Positron DLL，而是通过设备端 Toolhelp
枚举所有进程及其模块，检查本次 stage 中的 `positron_tls.dll`、`positron_json.dll`、
`positron_media.dll`、`positron_http.dll`、`positron_core.dll`、`positron_image.dll`、
`positron_script.dll`、`positron_browser.dll` 和 `positron_db.dll`。只有日志完整且
`module_audit holders=0 unavailable=0` 时才继续启动宿主；任一模块被持有、任一进程
模块快照不可用或摘要缺失都会 fail closed。审计证据保存在本地 run 目录的
`module-audit.log`，结果文件记录 `module_audit_check=PASS/FAIL`。因此模拟器进程是否存在
不能代替 guest 内模块审计；发现持有者时应由用户在设备端结束对应应用或重启设备后重跑。

只需要确认设备当前是否为空闲模块状态时，可使用审计专用模式；它完成部署和 guest 审计后
立即结束，不启动 `test_host.exe`，也不能和 `-ForceTerminatePositron` 同时使用：

```bat
scripts\device_gate.bat -Candidate module-audit -ModuleAuditOnly -PreserveDeployment
```

### 空间、部署和日志

未指定 `-RemoteBase` 时，设备门优先使用 `\Storage Card\Temp\Positron-device-gate`，路径不可创建或无法做路径级空间查询时回退到 `\Temp\Positron-device-gate`。显式目标不自动回退。两种模式都会查询目标卷和内部 object store；目标硬性余量为 staging 总大小加 1 MiB，内部 object store 另有 64 KiB 缓存告警线。空间字段和选择原因写入预检结果。

空间不足时只回收设备门自己生成、非当前运行目录的旧目录。每个候选目录必须先把日志成功复制两次并确认稳定的 `TESTBENCH PASS` 或 `TESTBENCH FAIL`；日志缺失、仍增长、复制失败、未知目录和当前目录都保留。回收后重新查询空间，仍不足才阻断部署。

完整日志必须在清理前复制到电脑。启动头、部分 `OK`、提示音、窗口关闭或单次 RAPI 成功都不是通过证据。

Windows CE 会按 DLL 基名复用已加载模块。自动宿主在日志头记录实际的 `Core module path`，
设备门必须确认它等于本次 staging 目录中的 `positron_core.dll`；缺失或不一致会以
`core_module_check=UNAVAILABLE/STALE_MODULE` 拒绝本批，不能把旧模块的断言或像素结果当作源码证据。
启动前的全 DLL 模块审计也必须通过；此时先在设备上结束持有旧 DLL 的进程，必要时重启设备，
再重跑设备门。

### 自动通过标准

一次设备门同时满足以下条件才通过：

1. 正式构建和整包 staging 成功；
2. 日志来自本次唯一候选目录；
3. 每个所选测试都有完成记录；
4. `ERROR`、`FAIL` 均为零；
5. 恰有一个 `TESTBENCH PASS`；
6. 涉及真实 Browse 时，路由和最终页面序列符合 fixture；
7. 启动前模块审计确认所有目标 Positron DLL 的 holder 数为零；
8. 没有旧 EXE/DLL 混包、遗留进程或 crash dump 证据。

## 风险相称的回归范围

每批通常运行新能力、直接共享的 ABI/所有权/默认动作、一个页面或导航哨兵（若相关）以及 TEST999。多个低风险批次累计、修改公共 ABI 或生命周期、触及 layout/paint、输入、网络/TLS、资源缓存、准备里程碑或出现崩溃/超时/数据错误时，再扩大到更宽范围或全量。全量清单从当前源码生成，不复制到本文。

## 人工验收

### 交互性能

测量菜单、表单或滚动卡顿时，以同一页面、设备、DPI 和构建配置比较前后结果。Debug 默认关闭
逐 DOM getter 取证，日志按 session 复用有界文件句柄；需要排查桥接时才编译启用
`APP_DEBUG_DOM_TRACE=1`，并注明它会干扰性能。保留点击/脚本、style、layout、控件同步和 paint
的低频计时，并用 Release 复核普通产品路径；不能只测脚本返回或 class mutation。

模拟器 guest `GetTickCount` 与用户的 PC 墙钟不能直接等同。自动门须同时证明展开/收起后的
布局几何和事件语义；人工仍须观察点击至画面稳定的时间、期间地址栏/菜单是否可响应。
已知核心交互阻塞不能因局部计时下降而标成完成。Debug 捕获脚本的显式进程清理必须等待
helper 的成功摘要，不用固定短延迟推断进程已经退出，并在覆盖旧日志前先回收证据。

### 输入与视觉

真实设备必须观察字体 fallback、字形、颜色、渐变、左右边距、居中容器、换行、表格、列表、滚动条、触摸命中、键盘焦点、SIP/IME、旋转/DPI、系统 picker、窗口返回、剪贴板互操作、loading、失败网络、旧页保留和深层导航。低风险视觉或输入变化可以累计后集中验收；崩溃、数据损坏、严重布局破坏和核心交互阻塞必须立即复核。

每组人工记录至少包含 commit/候选名、精确 `tests=`、设备型号、screen/DPI/方向、初始页面、操作步骤、预期与实际结果，以及必要截图和同批自动日志。截图和日志只放 `tmp/`；比较截图前先确认 viewport、DPI、方向、滚动位置和二进制身份一致。

## 网络测试与候选基线

WM6 镜像时间经常过旧。证书测试前校准时间，并把失败区分为 DNS、TCP、TLS handshake、证书/hostname、HTTP status、redirect、资源获取、页面解析和最终提交。离线 fixture 用于稳定合同，真实端点只作集成哨兵；暂时不可达不能通过放宽离线断言解决。

候选写入当前 handoff 前必须满足：范围、ABI 和所有权清楚；C89 回归、仓库审计和 ARMV4I 正式构建通过；staging 来自同一批构建；风险相称的设备日志完整通过；必要人工验收已完成或明确进入允许累计清单；handoff、限制、路线图和稳定文档各自只更新自身职责。

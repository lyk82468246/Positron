# `positron_core.dll`

`positron_core.dll` 是 Positron 的文档、样式、布局、命中、资源和有界 DOM mutation 层。它适用于 Windows Mobile 6 / Windows CE 5.2 ARMV4I，使用稳定 C ABI、UTF-8、opaque document handle 和明确错误码。Core 不创建窗口、不执行页面脚本、不派发 Browser 事件，也不直接访问网络。

## 产物与依赖

正式工程输出 `positron_core.dll` 与 import library。调用方只包含 `positron_core.h`，通过公开 relation、mutation、layout、resource 和 interaction API 读取/更新文档。NetSurf、libcss、libdom、hubbub、libsvgtiny 和 libjpeg 是内部静态实现，外部应用不应直接链接。

## 最小调用流程

```c
HANDLE document;

document = PCore_ParseHTML(utf8_html, utf8_length);
if (document != NULL) {
    PCore_Style(document, viewport_width, viewport_height);
    PCore_Layout(document);
    /* read relations, paint/hit-test, or call bounded mutations */
    PCore_FreeDocument(document);
}
```

实际宿主通常在 parse 后提供资源结果、style/layout 尺寸、GDI paint 和 pointer/focus 输入，再把 Core relation 结果转给 Browser。失败路径必须释放 handle，不把 libdom 指针泄漏到 ABI。

WM6 宿主有设备缩放时，应在首次 style/layout 前用实际物理客户区尺寸调用
`PCore_SetDeviceViewport(device_width, device_height, dpi)`，随后继续使用同一物理尺寸调用
`PCore_LayoutDocument` 和 `PCore_PaintDocument`。Core 会把该 DPI 同时用于布局测量和 GDI
绘制；paint HDC 的 `LOGPIXELS` 不再作为另一份字号来源。窗口尺寸或 DPI 改变后，宿主必须
重新设置 viewport、style/layout 并重绘，不能只替换 HDC。

### 同页锚点坐标

`PCore_FragmentInfoById()` 与 `PCore_FragmentInfoByToken()` 返回文档 CSS 像素，即使最近一次布局使用了设备 viewport。转换依据该文档的布局 DPI 快照，不受另一个文档随后改变全局 viewport 的影响。ByToken 优先查找字面 ID，再回退到旧式 `<a name>`；已有但不可布局的 ID 不会被同名 anchor 替代。各输出指针可省略，目标缺失、隐藏或尚未布局时失败且不改写输出。

宿主负责 URL fragment 解码、滚动钳制和平台坐标转换。物理滚动偏移应由返回的 CSS 坐标按当前页面 DPI 转换一次，例如 `MulDiv(css_y, dpi, 96)`；不要直接把 CSS 坐标用于 GDI/native 命中，也不要对已经转换的设备坐标重复缩放。这两个查询不执行滚动、布局或导航，不改变其他几何接口。

## 解析、样式与资源

外部 CSS 原始字节按文档缓存：最多 32 项，每项 256 KiB、总计 512 KiB。完整解析且不含
`@import` 的顶层样式表还可复用其 libcss 解析结果，累计保留量以 `css_stylesheet_size()` 报告
值计，最多 1 MiB；它随 document 一起释放。缓存要求 URL、原始 bytes 和 resolver 身份一致，
不缓存 callback buffer 或借用其所有权。resolver 身份改变时先重新解析，完整结果符合预算后
替换旧缓存，避免导航候选提交后的上下文切换永久失去复用；超出保留预算或含 import 时仍使用
原有逐次解析路径，不丢弃样式。每次重排仍重算媒体条件、selector、继承和交互状态；缓存不是冻结
computed style，也不免除 mutation 后的 style/layout。宿主必须保持 resolver 与 pw 的身份和
行为一致；需要新的资源策略时应创建新文档，而不是复用同一缓存并悄悄改变 resolver 行为。

Core 负责 UTF-8 HTML/CSS parse、cascade、媒体条件、computed style、页面 extent、常见 block/inline/flex/table/replaced layout、命中和 GDI paint。普通 `<button>` 是保留作者后代的样式盒，Core 在同一盒上附加有界 form gadget，因此按钮内的 span、文字和 CSS background image 仍由正常布局/绘制路径负责；`input` 的 submit/reset/button 类型继续使用原有 replaced-control fallback。资源发现与 cache 由 Core 维护有界状态；宿主负责 DNS/TCP/TLS/HTTP、worker、取消、重试和把成功/失败结果提交回 Core。

`img`、`srcset` 和 `picture/source` 只支持头文件规定的候选、URL、祖先、source、节点和 `sizes` 预算。Core 可投影 `naturalWidth`、`naturalHeight`、`complete`、`currentSrc`、image-map 几何和 area link metadata，但 relation 查询不会自行 fetch、decode 或 layout。CORS、完整媒体查询、绝对 URL、loading 策略和图像事件由上层决定。

异步图片资源使用 `PCore_FetchImageResourcesEx()`。同步消费者可以继续使用旧的
`PCore_FetchImageResources()`：其中 `0` 加非空 body 表示成功，非零表示终态失败；旧 ABI
不变。Ex callback 返回 `PCORE_IMAGE_FETCH_READY`、`PCORE_IMAGE_FETCH_PENDING` 或
`PCORE_IMAGE_FETCH_TERMINAL_FAIL`。READY 的 body 会复制到 Core image cache，随后调用方的
`freefn` 可以释放原 buffer；PENDING 不会建立 failed cache entry，宿主在 worker 完成后再次
扫描即可重试同一 URL；TERMINAL_FAIL（以及未知状态或不完整的 READY 输出）保留终态 fallback。
PENDING callback 不应返回 body。Core 不解析相对 URL，也不访问文件系统；宿主/HTTP resolver
仍负责把资源引用变成可请求 URL。资源进入 cache 后，调用方必须重新 layout/paint，才能得到
image box 与 SVG/PNG/JPEG/GIF 的解码结果。

CSS `background-image` 还支持一条明确有界的本地路径：`data:image/svg+xml,` 后跟
percent-encoded bytes，或 `data:image/svg+xml;base64,` 后跟标准 Base64。Core 在资源发现阶段
解码这两种形式，把结果写入同一份 document image cache，再沿现有 `positron_image.dll` SVG
创建、intrinsic-size、background-position/size/repeat 和 GDI paint 路径处理；不会把 `data:`
交给宿主 HTTP callback，也不在应用中自绘图标。单个 data URL 最大 256 KiB，解码结果最大
64 KiB，SVG 最多 128 个元素和 64 个 `path`；非法 MIME、percent/Base64、控制字符、超限或
复杂度超限都拒绝并保持旧页面状态（能够建立 cache key 的 URI 会记录终态失败）。普通网络 SVG 以及网络 PNG/JPEG/GIF 仍沿原
callback/cache/decode 路径，不会因为该本地适配获得隐式协议或 MIME 推断。

对于已解码的 SVG CSS `background-image`，Core 还有一个有界的响应式适配：当背景不重复且
intrinsic 尺寸超出背景定位区域时，redraw 会按 preserve-aspect-ratio 的 contain 规则缩小，
并保留 computed `background-position` 的相对位置。设备 viewport 由
`PCore_SetDeviceViewport()` 提供时，重复 SVG background tile 还会按同一 DPI 从 CSS 像素缩放
到物理绘制尺寸，避免高 DPI 页面把一个自然尺寸 tile 重复成错位或裁剪的 Logo；该缩放只在
Core 的重复背景绘制路径生效，不改变 Image DLL 返回的自然尺寸或 `<img>` relation。PNG/JPEG/GIF、
非重复背景以及没有超出区域的图像继续走 NetSurf 的 intrinsic 尺寸路径；这不是完整的
`background-size`/`cover`、多层背景或其他现代 CSS 实现，未支持的声明仍按有界规则处理。

布局 relation 提供 page width/height、元素 border/client/scroll 尺寸、有限 inline fragments、overflow retained scroll 和几何快照。relation 是最近一次 layout 的只读 snapshot；查询不会触发 reflow，mutation 成功后会使 retained layout 失效，调用方必须重新 style/layout/paint。

## DOM 与关系 bridge

常用 relation 包括 Element/attribute、未过滤 `childNodes`、节点类型和值、HTML serialization、form owner/effective-disabled、option default-selected、焦点和交互状态。关系读取支持 size-probe 和容量检查，缺失目标、过小 buffer、非法 UTF-8、越界 child index 和 stale document 都安全失败。

Core 维护 form owner、validation、successful-control、reset、submission 和 modal paint 的产品语义。`form="id"` 的跨树 owner、fieldset first-legend exemption、optgroup→option disabled 继承和 option live/default-selected 状态由同一 Core 状态提供给 Browser，不在宿主复制。

Native button 的同步事件桥使用 `PCore_EventDispatchFormControlEx()`。宿主按最近一次 layout
得到的 form-control index 派发 trusted `click`，并可传入一个有界、短生命周期的 target token；
Core 只在本次同步回调中把它作为 `event.target` 的桥接标识，不向页面写入 `id`。Browser/宿主
可在该回调期间用 `PCore_FormControlAttributeByIndex()` 读取或用对应的 Set/Remove 入口更新
按钮属性，随后由原有 mutation callback 决定重排和重绘。token 不得保存、跨事件复用或当作普通
DOM id；没有 listener 时该入口仍保留原生按钮的默认动作。

## Multipart 提交

对于 `enctype="multipart/form-data"` 的 form，Core 先把成功控件捕获为 opaque
submission，再由 `PCore_MultipartSubmissionEncode()` 生成可直接交给 HTTP 层的二进制
body 和完整 `Content-Type` header。调用方通过 `PCore_MultipartSubmissionById()`、
`PCore_MultipartSubmissionAt()` 或 `PCore_MultipartSubmissionForTextInput()` 取得
submission；Core 决定字段顺序、submitter、boundary、quoted `name`/`filename`、CRLF
分隔符和文件 bytes。`test_host` 以及其他应用只能提供文件 I/O callback，不应重新拼装
multipart wire format。

```c
static int read_file(void *pw, const char *path,
        char **out_data, int *out_len);
static void free_file(void *pw, char *data);
PCoreMultipartEncodeInfo info;
char *body;
char *content_type;
HANDLE submission;

body = NULL;
content_type = NULL;
submission = PCore_MultipartSubmissionById(document, "upload", "send");
if (submission != NULL &&
        PCore_MultipartSubmissionEncode(submission, read_file, free_file, NULL,
                &info, NULL, 0, NULL, 0) == 2) {
    body = (char *) malloc((size_t) info.body_bytes);
    content_type = (char *) malloc((size_t) info.content_type_bytes + 1);
    if (body != NULL && content_type != NULL &&
            PCore_MultipartSubmissionEncode(submission, read_file, free_file,
                NULL, &info, body, info.body_bytes, content_type,
                info.content_type_bytes + 1) == 1) {
        /* pass body/content_type to the application's HTTP request */
    }
    free(body);
    free(content_type);
}
PCore_FreeMultipartSubmission(submission);
```

`read_file` 必须同步返回一块新分配的 bytes buffer，`free_file` 必须释放同一块 buffer；
Core 不保存这两个 callback、路径或 buffer。size probe 或任一容量不足返回 `2`，成功复制
返回 `1`，参数、callback、读取、分配或固定 1 MiB body 上限失败返回 `0`；返回 `2` 时
不会部分写入输出。文件内容是 binary-safe 的，空路径不调用 callback。网络、文件权限、
请求取消和重试不属于 Core。

`FormData(form[, submitter])` 是独立于 form 的 `method`、`action` 和 `enctype` 的成功控件
快照。应用可以用 `PCore_FormDataById()` 或 `PCore_FormDataByIdEx()` 创建它，再调用
`PCore_FormDataEncode()` 生成同样有界的 multipart body；因此一个默认 GET/urlencoded
form 也能由应用自行构造 multipart HTTP 请求。这个入口与 submission encoder 共享
boundary、字段顺序、quoted metadata、binary file bytes、size-probe、1 MiB 上限和
失败原子性合同，但不会执行 form 默认动作、validation、事件或导航。文件仍只在同步
encode 调用中通过应用提供的 read/free callback 读取；`PCore_FormDataEntryInfo()` 只
返回文件名和类型元数据，不把本地路径或文件 buffer 暴露为长期状态。

这里的 `FormData` handle 只来自 Core 文档的 successful-control snapshot；它不是 Browser
脚本里的 `FormData` 对象。Browser 的 `append()`/`set()` pairs 和 `File`/`Blob` metadata
当前没有公共转换入口进入 Core encoder。应用若需要 multipart body，必须在 Core 侧取得
snapshot，并在同步 encode 调用中提供自己的 file read/free callback；文件 picker、权限、
网络发送和取消仍由应用宿主负责。

## 有界 DOM mutation

所有 mutation 都在头文件声明的节点、深度、节点数、direct-child、UTF-8 和文本预算内执行，并在提交前完成预检。失败不留下部分树；成功保留 API 承诺的节点身份并使 retained layout 失效。Core 不派发 mutation 事件、不执行 script、不创建 native 控件、不暴露 fragment handle。

主要入口包括：

- `PCore_NodeSetTextContentById`、`PCore_DocumentSetTitle`；
- `PCore_NodeSetInnerHTMLById`、`PCore_NodeInsertAdjacentHTMLById`、`PCore_NodeSetOuterHTMLById`；
- direct Element、Text、CharacterData 的 create/insert/replace/remove，以及有限的 child-list replacement；
- `PCore_NodeSplitTextChildById`、`PCore_NodeReplaceWholeTextChildById`、`PCore_NodeNormalizeById`；
- 表单 reset、validation、submission 和按 id 的 focus/autofocus/interaction primitives。

`PCore_NodeNormalizeById` 只整理一个 Element 的 direct children：删除空 Text/CDATA，并把连续 Text/CDATA 合并到首个非空节点。Element、Comment、processing-instruction 和其他节点都是边界。该实现使用显式 sibling walk，不依赖 WM6 上可能留下 stale cursor 的 libdom helper。

`PCore_NodeSplitTextChildById` 接受 Text/CDATA，以 UTF-16 code-unit offset 在 UTF-8 code-point 边界分割，并插入一个 Text suffix。`PCore_NodeReplaceWholeTextChildById` 保留目标节点身份，把相邻 Text/CDATA 段合并后移到段首并删除其他 CharacterData。三条路径都不扩展为通用 Node、Range 或 live collection。

HTML parser mutation 接受 Element/Text/Comment/CDATA 的 bounded 子集；重复/冲突 id、结构 token、非法 UTF-8、超限输入和 context-sensitive parser 内容在 mutation 前 fail closed。Browser 负责 detached wrapper、snapshot 和后续重排。

## 调用方所有权

Core document handle 的创建者负责销毁；输入字符串和输出 buffer 由调用方拥有，借用指针只在同步调用期间有效。size-probe 必须先返回所需字节数，容量不足不得部分改写。Core 不保存宿主 HWND、线程、response、脚本 value 或回调 buffer。

成功 mutation 后宿主必须决定何时 style/layout/paint、何时重建 native child、何时通知 Browser。宿主不得根据私有 libdom 指针重建 DOM 语义；Browser 应使用公开 relation/mutation callback。

## 当前边界

Core 不保证完整 CSS/HTML 标准、任意网站兼容性、通用 DocumentFragment、嵌套/无限 DOM mutation、MutationObserver、完整 live collection、Range/Selection、复杂 transforms、pinch zoom、完整滚动树、CORS/绝对 URL 或 OEM 控件视觉。真实触摸、SIP/IME、旋转、DPI、picker、字体、边距和失败网络按 [`docs/TESTING.md`](../docs/TESTING.md) 验收。

## 其他项目如何调用

Browser 或应用宿主应只通过 `positron_core.h` 调用 Core：解析并持有 document handle，注册/读取 relation，提交有界 mutation，成功后重新 style/layout/paint，再把快照通过 Browser callback 或应用 UI 消费。`test_host` 的职责仅是把这些调用接到 fixture、日志和断言；它不能编译或替代 Core 实现。

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

## 解析、样式与资源

Core 负责 UTF-8 HTML/CSS parse、cascade、媒体条件、computed style、页面 extent、常见 block/inline/flex/table/replaced layout、命中和 GDI paint。资源发现与 cache 由 Core 维护有界状态；宿主负责 DNS/TCP/TLS/HTTP、worker、取消、重试和把成功/失败结果提交回 Core。

`img`、`srcset` 和 `picture/source` 只支持头文件规定的候选、URL、祖先、source、节点和 `sizes` 预算。Core 可投影 `naturalWidth`、`naturalHeight`、`complete`、`currentSrc`、image-map 几何和 area link metadata，但 relation 查询不会自行 fetch、decode 或 layout。CORS、完整媒体查询、绝对 URL、loading 策略和图像事件由上层决定。

布局 relation 提供 page width/height、元素 border/client/scroll 尺寸、有限 inline fragments、overflow retained scroll 和几何快照。relation 是最近一次 layout 的只读 snapshot；查询不会触发 reflow，mutation 成功后会使 retained layout 失效，调用方必须重新 style/layout/paint。

## DOM 与关系 bridge

常用 relation 包括 Element/attribute、未过滤 `childNodes`、节点类型和值、HTML serialization、form owner/effective-disabled、option default-selected、焦点和交互状态。关系读取支持 size-probe 和容量检查，缺失目标、过小 buffer、非法 UTF-8、越界 child index 和 stale document 都安全失败。

Core 维护 form owner、validation、successful-control、reset、submission 和 modal paint 的产品语义。`form="id"` 的跨树 owner、fieldset first-legend exemption、optgroup→option disabled 继承和 option live/default-selected 状态由同一 Core 状态提供给 Browser，不在宿主复制。

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

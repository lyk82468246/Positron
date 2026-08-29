# `positron_core.dll`

`positron_core.dll` 是 Positron 的 HTML/CSS/DOM/layout/paint 产品边界。它把移植后的 NetSurf、libdom、libcss、hubbub 和图像适配隐藏在 UTF-8、opaque `HANDLE` 的公共 C ABI 后。

Core 不创建窗口、不运行消息循环、不连接网络，也不执行 JavaScript。应用只应链接公共 import library，不应直接包含 NetSurf/libdom/libcss 头或依赖内部静态库符号。

## 产物与依赖

- 工程：`positron_core.vcproj`
- 公共头：`positron_core.h`
- 输出：`bin\<Configuration>\positron_core.dll` 与 import library
- 内部静态依赖：hubbub、NetSurf support、libcss、libdom、image adapter
- 运行时产品依赖：`positron_image.dll`
- 可选组合：宿主用 `positron_http.dll` 实现 Core 的 fetch/resolve callbacks

其他项目链接 `positron_core.lib`，部署 Core、Image 及应用实际使用的其他 DLL。

## 最小调用流程

```c
#include "positron_core.h"

HANDLE doc;
HANDLE sheet;

if (PCore_Init() != 0) {
    return 1;
}

doc = PCore_ParseHTML(html_utf8, html_bytes);
sheet = PCore_ParseCSS(css_utf8, css_bytes, page_url_utf8);
if (doc != NULL && sheet != NULL &&
        PCore_StyleDocument(doc, sheet) == 0 &&
        PCore_LayoutDocument(doc, viewport_w, viewport_h) == 0) {
    PCore_PaintDocument(doc, hdc, scroll_x, scroll_y);
}

PCore_FreeStylesheet(sheet);
PCore_FreeDocument(doc);
PCore_Shutdown();
```

DOM 或控件状态改变后，调用方负责重新执行所需的 style/layout，再 paint。HDC、viewport、滚动位置和失效区域始终属于宿主。

## 资源获取

真实页面通常使用 `PCore_StyleDocumentEx2`：

- resolver 把 stylesheet、`@import` 和 `url()` reference 与 owning base URL 合并；
- fetch 返回临时资源字节；
- Core 在同步 callback 返回前复制需要保留的数据；
- Core 随后调用配对 free callback；
- 同一文档的成功资源进入有界 cache，重排可复用而不重新联网。

图片和 script discovery 使用对应的 fetch/enumeration API。Core 只负责发现、缓存和解释，不调度 worker、不拥有 HTTP response，也不执行 script。网络失败应由宿主分类和记录。

## 能力分组

### 解析、样式与布局

- HTML → libdom document；
- CSS parse、UA/author cascade、media 条件与 inheritance；
- `<style>`、外链 stylesheet、`@import` 和 inline style；
- box construction、normalisation、layout、geometry、scroll extent；
- GDI paint、clip、文字、border、背景和 retained image carrier。

Core 支持项目当前经过验证的 HTML/CSS 子集，但不是完整现代浏览器。具体缺口见已知限制。

### 查询与命中

布局后可按坐标或 DOM id 查询链接、片段、控件、summary 和 box geometry。扩展查询把 href、target、rel 等 UTF-8 元数据复制到调用方缓冲；容量不足或 stale layout 会 fail closed。

片段 token 按支持的 id/name 规则解析，但 history、URL percent-decoding、viewport scroll 和窗口副作用仍属于 Browser/宿主。

### DOM 与关系 bridge

`PCore_Node*ById` 提供有界 text、attribute、value、checked 和结构查询。关系 API 覆盖：

- parent/child/sibling 与结构 root tokens；
- element attributes 与 childNodes snapshot；
- form owner、form controls 和 label/control；
- script/runtime 所需的有限 element metadata。

结果是同步 UTF-8 snapshot，不暴露 libdom 指针，也不承诺完整 live collection、namespace、MutationObserver、Shadow DOM 或通用 selector engine API。

### 单元素 `contenteditable`

`PCore_ContentEditableInfoById` 解析元素及其祖先的 `contenteditable` 枚举值：空值/`true` 进入普通纯文本模式，`plaintext-only` 进入显式纯文本模式，`false` 禁用，未知值继续向祖先继承。返回值同时报告有效模式和当前 `textContent` 的 UTF-8 字节数；查询不要求 style/layout，id 不存在或结构不完整时 fail closed。

`PCore_ContentEditableSetTextById` 只允许有效的可编辑元素写入不超过 `PCORE_CONTENTEDITABLE_TEXT_MAX_BYTES` 的合法 UTF-8 文本，并用一个文本节点替换该元素的子内容。它不派发事件，也不保存 caret/selection；这些脚本语义由 Browser 的 `selectionStart`/`selectionEnd`/`selectionDirection` 和有界 `selectionchange` 提供，原生窗口同步则通过 Browser 的 selection callback 由宿主完成。参考宿主对无修饰 WM EDIT 拖选的范围和方向跟踪也留在平台接线，不进入 Core 文档状态。宿主应先通过 Browser 的现有 `beforeinput` 事务，获准后调用该 mutation，再派发 `input` 并按需重新 style/layout/paint。该 API 的失败码区分 DOM/目标、不可编辑和文本边界，避免把普通 `textContent` 写入误当作用户编辑。

`PCore_ContentEditableTargetInfo` 为 WM/native 宿主提供已布局 editing host 的有界快照：只枚举带非空 `id`、可见且有正尺寸 box 的有效 host，按 DOM 顺序最多 `PCORE_CONTENTEDITABLE_TARGET_MAX`（16）个；每个 host 的文本最多 8192 个 UTF-8 字节。嵌套但仅继承编辑状态的后代不单独出现。宿主可用快照中的几何和复制出的 id/text 创建代理窗口；任何 DOM mutation、重排或页面替换后都必须重新查询，不能缓存旧几何或字符串。

### 表单与 validation

Core 持有控件值、checked/selected、disabled/fieldset 继承、required/range/step/pattern/custom validity、submission、multipart、reset 和 successful controls 等产品语义。

Browser/宿主在 dispatch 可取消事件后调用 Core mutation/default action，再按结果派发 input、change、submit/reset 或 invalid。系统 picker、native validation UI、本地化提示、SIP/IME 和 WM 控件视觉不属于 Core。

`PCore_FormSubmission*` 使用 `PCORE_FORM_METHOD_*` 常量报告有效提交方法。`method="dialog"` 或 submitter 的 `formmethod="dialog"` 不生成网络 action/body；调用方改用 `PCore_FormDialogSubmissionAt` 或 `PCore_FormDialogSubmissionForTextInput` 两阶段查询，取得最近祖先 dialog 的 UTF-8 id 和 submitter value。Core 在查询时执行约束验证，但不派发 `submit`/`close`、不改变 `open`，这些事务仍由 Browser 和宿主完成。当前 Browser 组合按 id 寻址，因此无 id 或不在 dialog 内的目标会被消费并 fail closed。

### 交互与事件

Core 提供 hit testing、hover/focus/active/checked 状态、DOM listener/dispatch 和可聚焦目标枚举。Browser 决定脚本事件/默认动作事务，宿主把 WM 消息和 native 控件状态映射进来。

`PCore_FocusTargetInfo` 返回有界的键盘焦点快照：正值 `tabindex` 按数值升序排列，同值保持 DOM 顺序，随后是缺省/零值组的 DOM 顺序；负值、disabled、hidden、未布局目标和 file picker 会被排除。除支持的链接、表单控件和 `summary` 外，带有效非负 `tabindex` 的普通布局元素也会以 `PCORE_FOCUS_TARGET_GENERIC` 返回。`PCore_FocusTargetInfoWithin` 用 UTF-8 DOM id 将同一快照限制在某个已布局祖先及其后代内，顺序和预算保持一致，适合宿主实现 modal 的 Tab/Shift+Tab 范围。两者都只返回几何与 kind，不改变焦点；focus scroll、native HWND 切换以及把 Browser 报告的活动 modal id 接入消息循环仍由宿主完成；Core 不调用 `SetFocus`，也不创建控件窗口。

### Modal presentation

宿主先从 Browser session 读取活动 modal 的 UTF-8 id，再在普通文档绘制后调用 `PCore_PaintDocumentWithModal`：

```c
int paint_result;

paint_result = PCore_PaintDocumentWithModal(doc, hdc, scroll_x, scroll_y,
        active_modal_id);
```

该入口先完成普通 `PCore_PaintDocument`，随后在当前 viewport 上覆盖不透明的 `RGB(192,192,192)` WM6 安全遮罩，并把指定的 `<dialog open>` 盒子限制在其几何区域内重绘。`PCORE_MODAL_PAINT_APPLIED` 表示对话框也已重绘；id 过期、对话框已关闭、尚未 layout 或没有可用盒子时仍返回 `PCORE_MODAL_PAINT_BACKDROP_ONLY`，以免把 modal 后的页面泄露出来。传入 `NULL` 或空 id 等同普通绘制并返回 `PCORE_MODAL_PAINT_NONE`。

`dialog_id` 是借用的 UTF-8 字符串，长度必须小于 `PCORE_MODAL_DIALOG_ID_MAX`；文档、HDC、滚动位置和失效区域仍由宿主拥有。实现尊重 HDC 的 GDI invalid-region clip，并在组合步骤间恢复绘制状态。该能力是有界的实体色遮罩与盒子重绘，不等同于 CSS `::backdrop`、透明合成、多个 modal 或跨文档 top layer。

## 所有权

- `PCore_Init`/`PCore_Shutdown` 成对；实现为进程级引用计数。
- `PCore_ParseHTML` 的 document 用 `PCore_FreeDocument`。
- `PCore_ParseCSS` 的 stylesheet 用 `PCore_FreeStylesheet`。
- 文档拥有 DOM、computed styles、box tree、资源 cache、image carriers、表单和交互状态。
- 从文档查询得到的借用数据在 document free、相关 mutation 或重新 layout 后失效。
- fetch 输入由宿主持有；Core 只保留自己复制的内容，并按契约调用宿主 free callback。
- DLL 返回的专用 buffer/handle 必须使用头文件指定的 `PCore_Free*`，不能跨 CRT heap 释放。

同一 document 默认只由一个 UI 线程串行操作。不要从网络 worker 并发调用 parse/style/layout/ paint，也不要在同步 Core callback 中销毁当前文档。

## 宿主应负责什么

- HWND、消息循环、DPI/旋转、scrollbar、invalid region 和 HDC；
- HTTP/TLS、后台 worker、取消、loading 和候选页面提交；
- native EDIT/SELECT/button/file picker 与 SIP/IME；
- contenteditable 的 WM EDIT 窗口、焦点、键盘和 IME 接线；宿主按 Browser 的 `beforeinput` 取消结果调用 Core 的受限文本 mutation，使用 Browser 的 selection callback 同步原生范围，并在原生范围改变后调用 `PBrowser_ScriptSessionNotifyContentEditableSelection`。无修饰鼠标拖选的 anchor 与默认消息跟踪也由宿主维护。Core 只提供 editing-host 快照和文本状态，不创建窗口，也不保存第二份编辑模型或重新派发 `selectionchange`；Range/Selection 对象、富文本和完整 IME 仍不在此边界内；
- history、浏览器 script session 和新窗口/外部协议策略；
- 失败回滚、日志、持久化和应用生命周期。

完整浏览器组合可参考 [`../test_host/README.md`](../test_host/README.md)，但通用 DOM、表单、layout 或事件策略不得复制回宿主。

## 当前边界

- 不支持完整现代 HTML/CSS/DOM；float、复杂 table/position、Grid、custom properties 等仍有限。
- 字体、SVG、图像格式和高 DPI 结果受 WM6 GDI/依赖版本限制。
- Core resource cache、DOM bridge、表单集合和深度/数量均有固定预算。
- 焦点快照支持正值/零值/负值 `tabindex` 的有界排序和普通布局元素，并提供按 DOM id 限定祖先范围的 `PCore_FocusTargetInfoWithin`；`PCore_PaintDocumentWithModal` 可按宿主提供的活动 id 在已 layout 的 `<dialog open>` 之上组合实体色遮罩与对话框重绘。Core 不自行决定初始焦点、背景点击或跨窗口焦点策略；脚本生命周期、活动 modal id 和实际关闭仍由 `positron_browser.dll` 提供。
- Core 不执行 JavaScript；请与 `positron_browser.dll`/`positron_script.dll` 组合。
- 精确 API、返回码、结构布局和借用期限以 [`positron_core.h`](positron_core.h) 为准。

整体所有权见 [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

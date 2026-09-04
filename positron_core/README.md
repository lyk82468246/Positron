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

DOM 或控件状态改变后，调用方负责重新执行所需的 style/layout，再 paint。HDC、page-level viewport 和失效区域属于宿主；支持的嵌套 overflow 元素滚动位置由 Core retained，宿主通过公开 API 接线到窗口和脚本。

布局成功后，`PCore_DocumentWidth` 和 `PCore_DocumentHeight` 返回最近一次 layout 的 page-level extent。宽度和高度包含页面内容溢出 viewport 的部分，至少不小于传入的 viewport；宿主据此决定页面级滚动条、把 `(scroll_x, scroll_y)` clamp 到 client area，并把相同坐标传给 paint、命中测试和 native child 定位。对带有效 DOM `id` 的常见 block/replaced/flex box，Core 还保留 CSS `overflow` 滚动条及其位置：宿主可用 `PCore_NodeOverflowScrollToById` 设置并读取两个轴，也可用 `PCore_OverflowPointer` 转发文档坐标指针和 `PCore_OverflowScrollSnapshot` 取得最近的目标与位置。Core 不创建窗口；Browser/宿主负责把这些状态映射为脚本属性、事件和 WM 重绘。

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
- box construction、normalisation、layout、geometry、page-level scroll extent；
- GDI paint、clip、文字、border、背景和 retained image carrier。

Core 支持项目当前经过验证的 HTML/CSS 子集，但不是完整现代浏览器。具体缺口见已知限制。

### 查询与命中

布局后可按坐标或 DOM id 查询链接、片段、控件、summary 和 box geometry。扩展查询把 href、target、rel 等 UTF-8 元数据复制到调用方缓冲；容量不足或 stale layout 会 fail closed。

片段 token 按支持的 id/name 规则解析，但 history、URL percent-decoding、viewport scroll 和窗口副作用仍属于 Browser/宿主。

### DOM 与关系 bridge

`PCore_Node*ById` 提供有界 text、attribute、value、checked 和结构查询。关系 API 覆盖：

- parent/child/sibling 与结构 root tokens；
- element attributes 与 childNodes snapshot；
- form owner、form controls 和 label/control。支持的 input、select、textarea、button
  控件会按最近祖先 form 归属；存在 `form="id"` 时改为解析文档中对应的 form，空值或
  无效目标没有 owner，也不回退到祖先。`form.elements` 关系按文档顺序包含这种跨树
  显式关联的控件，但仍是每次查询生成的有界 snapshot。Core 的 validation、successful
  control/multipart、dialog/default-submit、reset 和原生激活路径复用同一 owner 解析，
  因此跨树控件不会只在 Browser 关系查询中出现；
- form-control 的 effective-disabled relation（关系 44）：input、button、select、
  textarea、option 和 optgroup 返回 UTF-8 `"0"`/`"1"`，并统一 disabled fieldset 的
  first-legend exemption 与 optgroup→option 继承；fieldset 自身及其他元素返回
  unavailable。该 relation 是只读 size-probe 快照，不触发 layout；
- option default-selected relation（关系 45）：仅对 `option` 返回数值 `1`/`0`，表示
  parser/default-selected 状态而不是 live `selected` 状态；其他元素返回 unavailable，
  查询同样不触发 layout；
- script/runtime 所需的有限 element metadata。

结果是同步 UTF-8 snapshot，不暴露 libdom 指针，也不承诺完整 live collection、namespace、MutationObserver、Shadow DOM 或通用 selector engine API。

布局完成后，`PCore_NodeRelationById` 的 `PCORE_NODE_RELATION_LAYOUT_RECT_*`
关系返回元素 border-box union 的整数 CSS 像素。对应的
`PCORE_NODE_RELATION_LAYOUT_FRAGMENT_COUNT` 与
`PCORE_NODE_RELATION_LAYOUT_FRAGMENT_*_AT` 关系返回有界视觉片段：块级元素通常
只有一个，inline flow 按实际行分割，最多
`PCORE_NODE_LAYOUT_FRAGMENT_MAX`（16）个。索引越界、未布局、隐藏或没有可用
box 时返回 unavailable；成功返回的数值只在当前 document/layout 有效。Browser
可以直接用这些关系实现 `getClientRects()` 和其 union，但其他宿主不应依赖 NetSurf
的内部 `struct box` 或复制第二份布局模型。

同一 relation bridge 还提供六个只读布局尺寸快照：
`PCORE_NODE_RELATION_LAYOUT_OFFSET_WIDTH/HEIGHT`、
`PCORE_NODE_RELATION_LAYOUT_CLIENT_WIDTH/HEIGHT` 和
`PCORE_NODE_RELATION_LAYOUT_SCROLL_WIDTH/HEIGHT`。它们是最近一次成功 layout 的
整数 CSS 像素；offset 尺寸包含 border，client 尺寸表示 retained scrollport 的
padding 区域（本实现的滚动条覆盖在边缘，不从 client 尺寸再扣除固定宽度），scroll
尺寸包含有界后代内容 extent。该快照只对已布局的 block、
replaced、常见 table/flex box 有效；inline/text、隐藏、无 box 或未完成 layout
返回 unavailable。查询不会触发 relayout，也不会为没有 retained scrollbar 的元素伪造
滚动范围。带有效 `id` 的支持 box 可以通过 `PCore_NodeOverflowScrollToById` 进行有界、
立即生效的 `scrollLeft`/`scrollTop` 读写；关系 38/39 返回当前 CSS 像素偏移。
关系 40/41（`LAYOUT_SCROLLABLE_X/Y`）报告 retained scrollbar 是否拥有对应轴，
关系 42/43（`LAYOUT_CLIENT_X/Y`）报告该 box 的 padding/client edge 文档坐标。
这四个只读关系让 Browser 在不暴露 `struct box` 的情况下实现有限的嵌套
`Element.scrollIntoView()`：它只遍历最多 64 层、且能被 DOM relation 寻址的祖先，
选择最近的 retained overflow box，并在目标轴上对齐；找不到可用祖先时仍由 Browser
回退到 page-level scroll。Core 仍不实现完整 scroll tree、scroll chaining、
scroll-margin、平滑/惯性滚动或匿名目标的宿主指针归因。

### 单元素 `contenteditable`

`PCore_ContentEditableInfoById` 解析元素及其祖先的 `contenteditable` 枚举值：空值/`true` 进入普通纯文本模式，`plaintext-only` 进入显式纯文本模式，`false` 禁用，未知值继续向祖先继承。返回值同时报告有效模式和当前 `textContent` 的 UTF-8 字节数；查询不要求 style/layout，id 不存在或结构不完整时 fail closed。

`PCore_ContentEditableSetTextById` 只允许有效的可编辑元素写入不超过 `PCORE_CONTENTEDITABLE_TEXT_MAX_BYTES` 的合法 UTF-8 文本，并用一个文本节点替换该元素的子内容。它不派发事件，也不保存 caret/selection；这些脚本语义由 Browser 的 `selectionStart`/`selectionEnd`/`selectionDirection` 和有界 `selectionchange` 提供，原生窗口同步则通过 Browser 的 selection callback 由宿主完成。参考宿主对无修饰 WM EDIT 拖选、Shift/方向键扩展和捕获/焦点中断收尾的范围和方向跟踪也留在平台接线，不进入 Core 文档状态。宿主应先通过 Browser 的现有 `beforeinput` 事务，获准后调用该 mutation，再派发 `input` 并按需重新 style/layout/paint。系统剪贴板同样不属于 Core：宿主可把经过容量检查和 CRLF 规范化的 `CF_UNICODETEXT` paste/cut/copy data 交给 Browser 事务，折叠复制由宿主在事务外判定为 no-op，Core 只接收最终 UTF-8 文本 mutation。该 API 的失败码区分 DOM/目标、不可编辑和文本边界，避免把普通 `textContent` 写入误当作用户编辑。

`PCore_ContentEditableTargetInfo` 为 WM/native 宿主提供已布局 editing host 的有界快照：只枚举带非空 `id`、可见且有正尺寸 box 的有效 host，按 DOM 顺序最多 `PCORE_CONTENTEDITABLE_TARGET_MAX`（16）个；每个 host 的文本最多 8192 个 UTF-8 字节。嵌套但仅继承编辑状态的后代不单独出现。宿主可用快照中的几何和复制出的 id/text 创建代理窗口；任何 DOM mutation、重排或页面替换后都必须重新查询，不能缓存旧几何或字符串。

### 表单与 validation

Core 持有控件值、checked/selected、effective-disabled（含 fieldset first-legend exemption
和 optgroup→option 继承）、required/range/step/pattern/custom validity、submission、
multipart、reset 和 successful controls 等产品语义。

支持的控件统一按最近祖先 form 或显式 `form="id"` 解析 owner。这个解析同时服务
`PCore_FormValidation*`、successful-control/urlencoded 或 multipart submission、
`method="dialog"` 默认动作、reset 和按坐标的 submit/reset 激活；空值或无效目标没有
owner，也不会回退到祖先。Browser 的 `Element.form`/`form.elements` 关系与这些 Core
路径共享同一规则。

脚本宿主可通过 `PCore_NodeCheckedById` 读取带 id 的 `input.checked` 或 `option.selected`
实时状态；后者会随 `PCore_NodeSetSelectedIndexById`、`PCore_SelectSetOptionSelected`
和 native SELECT 提交的选择变化。`PCore_NodeSetCheckedById`/`defaultChecked` 仍只
服务 checkbox/radio input，option 的选择应使用 select/option API。

`PCore_NodeRelationById` 的 `PCORE_NODE_RELATION_FORM_CONTROL_DISABLED` 返回上述
effective-disabled 状态；`PCORE_NODE_RELATION_FORM_OPTION_DEFAULT_SELECTED` 返回
option 的默认 selected 快照。Browser 可注册同一 relation，把 `:disabled`/`:enabled`
与 Core 的 fieldset/optgroup 规则保持一致，并让 `:default` 与 live selected state
保持分离；宿主不应在测试 helper 或 native 控件适配层复制这些判断。
`PCore_SelectOptionInfo`、`PCore_SelectSetOptionSelected` 和 successful form submission
同样消费 effective-disabled/live selected 状态，因此 disabled option 不会被选中或提交。

Browser/宿主在 dispatch 可取消事件后调用 Core mutation/default action，再按结果派发 input、change、submit/reset 或 invalid。系统 picker、native validation UI、本地化提示、SIP/IME 和 WM 控件视觉不属于 Core。

对于不依赖点击坐标的应用流程，宿主可以在 reset 事件获准后调用
`PCore_FormResetById(doc, "form-id")`。该 state-only 入口使用同一套 owner 规则恢复表单
子树内以及带 `form="form-id"` 的外部 input、select、textarea、button；成功返回 0，
目标不存在、不是 form 或 DOM 恢复失败返回非零。调用方在成功后必须重新 layout/paint，
不能把该入口当作事件 dispatch 或 native 控件策略。

脚本 `requestSubmit([submitter])` 可使用 `PCore_FormValidationSubmitById` 先解析 form
和可选的 enabled submit button，并按 `novalidate`/`formnovalidate` 规则取得验证结果；
随后用 `PCore_FormSubmissionById`、`PCore_FormDialogSubmissionById` 或
`PCore_MultipartSubmissionById` 读取对应的 urlencoded、dialog 或 multipart 默认动作。
这些 by-id 入口都只解析/快照 DOM，不派发 `submit`、不导航、不关闭 dialog；Browser 负责
事件取消顺序，宿主负责把获准结果接到网络或 native/dialog 策略。multipart 返回的
opaque handle 仍须由调用方配对 `PCore_FreeMultipartSubmission`。

`PCore_FormSubmission*` 使用 `PCORE_FORM_METHOD_*` 常量报告有效提交方法。`method="dialog"` 或 submitter 的 `formmethod="dialog"` 不生成网络 action/body；调用方改用 `PCore_FormDialogSubmissionAt` 或 `PCore_FormDialogSubmissionForTextInput` 两阶段查询，取得最近祖先 dialog 的 UTF-8 id 和 submitter value。Core 在查询时执行约束验证，但不派发 `submit`/`close`、不改变 `open`，这些事务仍由 Browser 和宿主完成。当前 Browser 组合按 id 寻址，因此无 id 或不在 dialog 内的目标会被消费并 fail closed。

脚本 `HTMLFormElement.submit()` 使用三个独立的无验证 by-id 入口：
`PCore_FormSubmissionNoValidationById` 构造 urlencoded 或报告 multipart/dialog 方法，
`PCore_FormDialogSubmissionNoValidationById` 返回最近祖先 dialog 的 id（不含 submitter
value），`PCore_MultipartSubmissionNoValidationById` 返回由调用方释放的 multipart
snapshot。它们跳过 constraint validation、submitter 选择和事件，不导航、不关闭 dialog、
不操作 native 控件；Browser 决定脚本方法和事件边界，宿主在 direct callback 中采用对应
结果并执行网络或 dialog 策略。空/无效 form id、容量不足或不满足方法条件均安全失败。

`PCore_FormDataById` 为脚本 `new FormData(form)` 提供独立的 successful-control snapshot；
`PCore_FormDataByIdEx` 另外接受可选 `submitter_id`，空值表示没有显式 submitter，非空值必须
指向一个启用、归属于该 form 的 submit-type input 或 button（包括有效的外部
`form="id"` 控件），并按文档顺序把它纳入快照。两者复用同一套 form-owner、disabled、
checkbox/radio、select 和文件控件收集规则，但不读取 method/enctype、不做 constraint
validation、不派发事件，也不导航；无效 submitter 或 owner 安全失败。
`PCore_FormDataInfo`/`PCore_FormDataEntryInfo` 按文档顺序读取字符串或文件名，调用方必须
用 `PCore_FreeFormData` 释放 handle；文件的本地 picker 路径不会从该 API 暴露。快照与
后续 DOM/value mutation 脱离，空值或无效 form id 安全失败。完整 live `FormData`、文件
读取和其他 form-associated 元素仍不在 Core 合同内。

### 交互与事件

Core 提供 hit testing、hover/focus/active/checked 状态、DOM listener/dispatch 和可聚焦目标枚举。Browser 决定脚本事件/默认动作事务，宿主把 WM 消息和 native 控件状态映射进来。

`PCore_InteractionStateElementId` 是对应交互状态的只读 UTF-8 id 查询。调用方以
`PCORE_INTERACTION_FOCUS`、`PCORE_INTERACTION_ACTIVE` 或
`PCORE_INTERACTION_HOVER` 之一读取当前节点；size-probe、固定容量、完整字节数和
fail-closed 返回码与 `PCore_InteractionFocusElementId` 相同。它不改变状态，也不要求
重新 style/layout。Browser 宿主可把 active/hover 查询接到
`PBrowserScriptInteractionCallbacks`，使脚本 selector 的 `:active`/`:hover` 读取同一份
Core 状态；pointer 事件、状态更新、重绘和重新 style 仍由宿主决定。

`PCore_FocusTargetInfo` 返回有界的键盘焦点快照：正值 `tabindex` 按数值升序排列，同值保持 DOM 顺序，随后是缺省/零值组的 DOM 顺序；负值、disabled、hidden、未布局目标和 file picker 会被排除。除支持的链接、表单控件和 `summary` 外，带有效非负 `tabindex` 的普通布局元素也会以 `PCORE_FOCUS_TARGET_GENERIC` 返回。`PCore_FocusTargetInfoWithin` 用 UTF-8 DOM id 将同一快照限制在某个已布局祖先及其后代内，顺序和预算保持一致，适合宿主实现 modal 的 Tab/Shift+Tab 范围。两者都只返回几何与 kind，不改变焦点；页面级 focus reveal、native HWND 切换以及把 Browser 报告的活动 modal id 接入消息循环仍由宿主完成；Core 不调用 `SetFocus`，也不创建控件窗口。

脚本主动聚焦使用两个按 id 的同步入口：`PCore_FocusTargetInfoById` 解析一个已布局且符合相同资格规则的目标，返回其 geometry/kind；`PCore_InteractionFocusById` 只更新 Core 的 focus node。无 id、disabled、hidden、stale、未布局或不支持的目标均 fail closed，重复聚焦返回 no-op。Core 不切换 native HWND、不派发 focus family、不滚动目标、不画焦点矩形；Browser 的 `PBrowserScriptFocusRequestCallbacksEx` 消费这份 geometry 实现 page-level reveal，并在发现可寻址的 retained overflow 祖先时完成有界的嵌套 reveal。Ex 的 `prevent_scroll` 让宿主在 Browser 处理嵌套链前保持页面 viewport 不变，回调结果仍用于同步宿主实际 page scroll。宿主仍负责 native HWND、focus family 和窗口策略，并在文档或 layout 改变后重新查询，不能缓存旧快照。

页面提交后若应用需要遵循 HTML 的 `autofocus` 提示，宿主可在 style/layout 完成并创建 native 子控件后显式调用 `PCore_AutofocusTargetInfo`。Core 按 DOM 顺序扫描带 boolean `autofocus` 属性的元素，复用现有焦点资格、可见性、布局和深度预算；查询只复制 geometry/kind 与可选 UTF-8 id，不改变文档焦点。随后宿主调用 `PCore_InteractionFocusAutofocus` 更新 Core focus node，再自行完成 page-level reveal、native HWND、focus/focusin 事件和重绘。重复调用是 no-op；没有合格目标、过期布局、无 id 或 id 超出宿主桥接容量时必须安全回退。为无 id 目标派发事件时使用 `PCore_EventDispatchFocus`，它在同步 dispatch 期间保留当前焦点节点引用，保证监听器修改焦点不会使目标悬空。该路径是宿主触发的一次性、有界初始焦点事务，不是 Core 自主运行的完整焦点算法；Browser 的 `document.activeElement` 仍只投影可寻址 id，无 id 时按既有合同回退到 `document.body`。

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

- HWND、消息循环、DPI/旋转、invalid region 和 HDC；宿主在 layout 后读取 `PCore_DocumentWidth/Height`，维护 page-level `(scroll_x, scroll_y)`，并负责页面级 clamp、滚动条消息和 native child reposition；对于嵌套 overflow，宿主只把 WM 指针换算为 document 坐标，调用 `PCore_OverflowPointer`，用 `PCore_OverflowDirtyRect` 做局部失效，再把 `PCore_OverflowScrollSnapshot` 的目标/位置通知给 Browser。元素滚动位置、范围和 clamp 规则属于 Core，宿主不得维护第二份模型；
- HTTP/TLS、后台 worker、取消、loading 和候选页面提交；
- native EDIT/SELECT/button/file picker 与 SIP/IME；
- contenteditable 的 WM EDIT 窗口、焦点、键盘和 IME 接线；宿主按 Browser 的 `beforeinput` 取消结果调用 Core 的受限文本 mutation，使用 Browser 的 selection callback 同步原生范围，并在原生范围改变后调用 `PBrowser_ScriptSessionNotifyContentEditableSelection`。无修饰鼠标拖选、Shift/方向键的 anchor 与默认消息跟踪，以及捕获/取消/焦点中断收尾也由宿主维护；受限 `WM_PASTE`/`WM_COPY`/`WM_CUT` 的 `CF_UNICODETEXT` 读取、所有权和 native default 包裹也由宿主维护。Core 只提供 editing-host 快照和文本状态，不创建窗口，也不保存第二份编辑模型或重新派发 `selectionchange`；Range/Selection 对象、富文本和完整 IME 仍不在此边界内；
- 页面提交后的初始焦点策略；若启用 `autofocus`，宿主须在 style/layout 和 native 子控件创建完成后调用 `PCore_AutofocusTargetInfo`/`PCore_InteractionFocusAutofocus`，按结果完成 page-level reveal、native HWND、focus/focusin 事件和重绘。无 id 目标的 focus family 使用 `PCore_EventDispatchFocus`；Core 不在后台或页面解析期间自动抢占焦点；
- history、浏览器 script session 和新窗口/外部协议策略；
- 失败回滚、日志、持久化和应用生命周期。

完整浏览器组合可参考 [`../test_host/README.md`](../test_host/README.md)，但通用 DOM、表单、layout 或事件策略不得复制回宿主。

## 当前边界

- 不支持完整现代 HTML/CSS/DOM；float、复杂 table/position、Grid、custom properties 等仍有限。
- 字体、SVG、图像格式和高 DPI 结果受 WM6 GDI/依赖版本限制。
- Core resource cache、DOM bridge、表单集合和深度/数量均有固定预算。
- 焦点快照支持正值/零值/负值 `tabindex` 的有界排序和普通布局元素，并提供按 DOM id 限定祖先范围的 `PCore_FocusTargetInfoWithin`；`PCore_AutofocusTargetInfo`/`PCore_InteractionFocusAutofocus` 让宿主在页面 layout 和 native 子控件创建后显式选择第一个合格的 `autofocus` 目标，`PCore_EventDispatchFocus` 可为无 id 的当前焦点节点保留目标并派发事件。`PCore_InteractionStateElementId` 还以同一 size-probe 合同读取 focus/active/hover 的当前非空 id，供 Browser 的可选 interaction callback 投影 `:active`/`:hover`；查询不改变交互状态。`PCore_PaintDocumentWithModal` 可按宿主提供的活动 id 在已 layout 的 `<dialog open>` 之上组合实体色遮罩与对话框重绘。Core 不自行启动初始焦点、背景点击或跨窗口焦点策略；脚本生命周期、活动 modal id、native 焦点、页面 reveal 和实际关闭仍由宿主/`positron_browser.dll` 组合完成。
- Core 不执行 JavaScript；请与 `positron_browser.dll`/`positron_script.dll` 组合。Browser 的 `Element.scrollLeft`/`scrollTop`/`scrollTo()`/`scrollBy()` 只覆盖有 id 的、已布局支持 box，并通过本页的 Core API 接线；`Element.scrollIntoView()` 另可使用关系 40–43 沿最多 64 层可寻址父链，从最近到最外处理 `container:"all"` 的有限 reveal，`HTMLElement.focus()` 复用同一条路径。完整滚动树、scroll chaining、scroll-margin、平滑/惯性滚动和匿名目标仍不在范围内。
- 精确 API、返回码、结构布局和借用期限以 [`positron_core.h`](positron_core.h) 为准。

整体所有权见 [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

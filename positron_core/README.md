# `positron_core`

`positron_core.dll` 是 Positron 的 HTML/CSS/DOM/rendering 产品边界。它把移植的
NetSurf、libdom、libcss、hubbub、parserutils 和绘制适配封装起来，向应用提供
UTF-8、opaque `HANDLE` 的解析、样式、布局、绘制、命中、表单、资源和 DOM 事件 API。

## 输出与依赖

- 工程：`positron_core.vcproj`
- 输出：`bin\Debug\positron_core.dll`、对应 `.lib`
- 公共头：`positron_core.h`
- 链接的内部静态库：`positron_hubbub`、`positron_netsurf`、`positron_libcss`、
  `positron_libdom`、`positron_image`
- 网络不内置：由调用者通过 fetch/resolve callback 提供，可使用 `positron_http`

应用只应链接 `positron_core.lib` 并部署 `positron_core.dll` 及其产品 DLL 依赖；
不要直接包含 NetSurf/libdom/libcss 头文件。Core 不创建窗口、不实现消息循环，也不
自行连接网络。

## 其他项目如何调用

最小的文档到绘制流水线是：

```c
#include "positron_core.h"

HANDLE doc;
HANDLE sheet;

if (PCore_Init() != 0) {
    return 1;
}
doc = PCore_ParseHTML(html_utf8, html_bytes);
sheet = PCore_ParseCSS(css_utf8, css_bytes, page_url);
if (doc != NULL && sheet != NULL &&
        PCore_StyleDocument(doc, sheet) == 0 &&
        PCore_LayoutDocument(doc, viewport_w, viewport_h) == 0) {
    PCore_PaintDocument(doc, hdc, scroll_x, scroll_y);
}
PCore_FreeStylesheet(sheet);
PCore_FreeDocument(doc);
PCore_Shutdown();
```

典型调用者还会使用 `PCore_StyleDocumentEx2` 配合 URL resolver/fetch/free 回调，
`PCore_FetchImageResources` 获取图片，`PCore_LinkAt` 做链接命中，
`PCore_FormActivateAt` / `PCore_InteractionSetAt` 驱动控件状态，
`PCore_EventListenerAdd` / `PCore_EventDispatchAt` 接入同步 DOM 事件，以及
`PCore_Node*ById` 读取或修改脚本需要的 DOM 属性、值和表单状态；
`PCore_FormControlInfoById` 可在布局后按 DOM id 查询控件几何、kind、selected 和 disabled
状态，供浏览器宿主实现程序化控件 activation；`PCore_FormControlValidationById` 可在布局前后
查询控件的 `valid`、`will_validate` 和 `PCORE_VALIDITY_*` flags，供脚本 bridge 或其他宿主
读取约束状态；`PCore_FormValidationById` 可在布局前后按 form 的 DOM id 聚合查询当前控件的
约束结果（忽略 form `novalidate`，跳过 disabled/readonly/submit 等不参与候选），供脚本
bridge 实现受限的 form-level `checkValidity()`；`PCore_FormReportValidityById` 在同一候选规则上
收集 invalid controls，并按 DOM 顺序向有非空 id 的控件派发 trusted、non-bubbling、cancelable
`invalid` 事件。`PCore_FormSetCustomValidityById` 与 `PCore_FormGetCustomValidityById` 可按 id
设置/读取 application-owned custom validity message，覆盖当前 form-control candidates；
`PCore_FormGetValidationMessageById` 在 custom message 为空时按当前 validity flags 返回固定的
英文 fallback，并提供完整字节长度和安全截断。查询/设置不应用 form-level no-validate 提交按钮
绕过，也不提供 native invalid UI、焦点/滚动、提交或本地化错误提示；report API 的 boolean
结果不受 `preventDefault()` 改变。
`PCore_FormResetAt` 只提交 reset 的 DOM 初始状态；可取消的 reset 事件由宿主先分发。

`PCore_NodeRelationById` 为浏览器或其他宿主提供一个稳定、只读的 DOM 关系切片：按元素 id
查询 parent/first-child/last-child/previous-sibling/next-sibling、child count、tag/name、
form owner，以及按 DOM 顺序查询 form-control count/index。它还提供 attribute count、name-at
和 value-at 关系，供浏览器层构造 parser-order 的 `Attr`/NamedNodeMap 视图。字符串结果遵循
UTF-8 probe 和安全截断约定，计数通过 `out_number` 返回；缺失 id、越界索引和不支持的关系会
fail closed。`CHILD_NODE_*` 关系另提供所有直接 childNodes 的数量、类型、name/value、
textContent 和可用子元素 id，因此文本、注释和无 id 元素不会被旧的 element-only collection
过滤；next585 另外为没有 HTML `id` 的 document root、直接 `head` 和直接 `body` 提供三个
保留结构 token（`PCORE_DOCUMENT_ELEMENT_TOKEN`、`PCORE_DOCUMENT_HEAD_TOKEN`、
`PCORE_DOCUMENT_BODY_TOKEN`）。真实 id 查找优先，token 只作为结构 fallback，因此可以在
同一关系桥上构造 `documentElement`/`head`/`body` 的稳定 wrapper。该 API 不暴露 libdom
对象，不实现通用节点创建或 mutation、属性 namespace、live
collection、shadow tree、复杂 selector、layout 或 native control 状态；结果只在同步 callback
期间作为 UTF-8 snapshot 借用。

`PCore_Init` 与 `PCore_Shutdown` 成对使用；文档、样式表和其他返回句柄分别用
`PCore_FreeDocument`、`PCore_FreeStylesheet` 及头文件指定的 `PCore_Free*` 释放。
字符串和 callback 缓冲的借用期限以头文件为准。DOM/GDI 操作遵循宿主的 UI 线程
纪律；布局后才能绘制，DOM 写入后需要重新样式/布局。

## 当前边界

这是轻量网页运行核心，不承诺完整 HTML/CSS/DOM/Web 标准。URL 解析和资源传输策略由
宿主决定；脚本执行由 `positron_script`/`positron_browser` 负责。`test_host.exe` 是
回归消费者，不是 Core 的公共 API 所有者。

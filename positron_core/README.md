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
`PCore_FetchImageResources` 获取图片，`PCore_LinkAt` 做坐标命中，
`PCore_LinkInfoById` 在布局后按 DOM id 读取带非空 `href` 的锚点几何和 UTF-8 URL；
`PCore_LinkInfoByIdEx` 与 `PCore_LinkAtEx` 另外返回有界的 raw `target`/`rel` 元数据，
缺失属性为空、容量不足 fail closed。`PCore_FragmentInfoById` 在布局后按 literal UTF-8
DOM id 读取同页片段目标几何；新增的
`PCore_FragmentInfoByToken` 在同一布局契约下先按 id 查找，再兼容旧式 `<a name>` 锚点。
链接查询供浏览器 bridge 激活链接，片段查询供宿主滚动视口；这些 API 都不暴露 libdom/box
指针，也不执行 URL 解析、percent-decoding、网络、历史或窗口副作用。
`PCore_FormActivateAt` / `PCore_InteractionSetAt` 驱动控件状态，
`PCore_EventListenerAdd` / `PCore_EventDispatchAt` 接入同步 DOM 事件，以及
`PCore_Node*ById` 读取或修改脚本需要的 DOM 属性、值和表单状态；
`PCore_FormControlInfoById` 可在布局后按 DOM id 查询控件几何、kind、selected 和 disabled
状态，供浏览器宿主实现程序化控件 activation；`PCore_FormControlValidationById` 可在布局前后
查询控件的 `valid`、`will_validate` 和 `PCORE_VALIDITY_*` flags，供脚本 bridge 或其他宿主
读取约束状态；`PCore_FormValidationById` 可在布局前后按 form 的 DOM id 聚合查询当前控件的
约束结果（忽略 form `novalidate`，跳过自身或 disabled fieldset 继承的 disabled、readonly、
submit 等不参与候选），供脚本
bridge 实现受限的 form-level `checkValidity()`；`PCore_FormReportValidityById` 在同一候选规则上
收集 invalid controls，并按 DOM 顺序向有非空 id 的控件派发 trusted、non-bubbling、cancelable
`invalid` 事件。`PCore_FormSetCustomValidityById` 与 `PCore_FormGetCustomValidityById` 可按 id
设置/读取 application-owned custom validity message，覆盖当前 form-control candidates；
`PCore_FormGetValidationMessageById` 在 custom message 为空时按当前 validity flags 返回固定的
英文 fallback，并提供完整字节长度和安全截断。查询/设置不应用 form-level no-validate 提交按钮
绕过，也不提供 native invalid UI、焦点/滚动、提交或本地化错误提示；report API 的 boolean
结果不受 `preventDefault()` 改变。fieldset disabled 的有效状态由 core 统一计算：第一个
legend 后代豁免，嵌套 fieldset 逐层继承；该判定也用于 successful-control 序列化、默认
submitter、`PCore_FormControlInfo*`、表单激活和交互闸门。HTML `control.disabled` 的属性反射
仍由浏览器层按自身属性提供，不把祖先 fieldset 状态写回 DOM。
`PCore_FormResetAt` 只提交 reset 的 DOM 初始状态；可取消的 reset 事件由宿主先分发。

`PCore_StyleDocumentEx2` 在收集页面作者 CSS 时也读取 `<style media>` 与
`<link rel="stylesheet" media>`，并把 UTF-8 media query 交给 libcss 的 stylesheet
selection context。调用者只需在每次 viewport 变化后重新调用样式事务；同一文档的外部
CSS 字节会从 Core-owned cache 重用，fetch/free callback 的所有权和生命周期保持不变。
外部 `<link rel="stylesheet">` 若存在 `disabled` 属性（包括值为 `false`），则在收集阶段
跳过，不会 fetch、解析或附加 CSS；启用 link 仍沿用相同的 cache 和 callback 所有权。
`rel` 按 ASCII whitespace 分隔的 token、ASCII 大小写不敏感地匹配 `stylesheet`；含
`alternate` token 的 link 继续跳过，避免在没有备用样式表选择策略时覆盖页面。
这项支持只控制当前 CSS selection，不新增 ABI、不产生 `MediaQueryList` 事件，也不替宿主
决定其他 link type 的下载策略。

`PCore_NodeRelationById` 为浏览器或其他宿主提供一个稳定、只读的 DOM 关系切片：按元素 id
查询 parent/first-child/last-child/previous-sibling/next-sibling、child count、tag/name、
form owner，以及按 DOM 顺序查询 form-control count/index。它还提供 attribute count、name-at
和 value-at 关系，供浏览器层构造 parser-order 的 `Attr`/NamedNodeMap 视图；next614 增加
label-control、control-label-count 和 control-label-at 关系，支持显式 `for` 与嵌套 label，
仅覆盖 input（排除 hidden）、select、textarea、button。字符串结果遵循
UTF-8 probe 和安全截断约定，计数通过 `out_number` 返回；缺失 id、越界索引和不支持的关系会
fail closed；label-at 只返回有可寻址 DOM id 的 label。`CHILD_NODE_*` 关系另提供所有直接 childNodes 的数量、类型、name/value、
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

next643/645/646 的页面样式资源选择属于 Core：`<style media>` 与
`<link rel="stylesheet" media>` 只在当前 viewport 匹配时参与 libcss selection；存在
`disabled` 属性的外部 stylesheet link 在 fetch 前跳过，rel token 组合会按 stylesheet
语义进入同一流程，同一文档的再次样式事务复用已缓存的外部 CSS 字节。TEST1091/1093/1094
通过公开的
`PCore_StyleDocumentEx2` fetch/free callback 约定，在 320px/299px 视口验证两类条件和
禁用 link 不产生 fetch、启用 link 只产生 1 次 fetch/1 次 free、alternate link 不产生
fetch 的缓存边界；相关 TEST21、TEST24、TEST1091、TEST1093、TEST1094 与 TEST999 设备门
通过 6/6；这不代表脚本侧动态媒体事件、`type`、alternate sheet 切换或其他 link-type 行为。

next644 的 `media` DOM 反射属于 `positron_browser.dll`，Core 不新增接口或承担脚本
property。Core 继续只负责 `<style media>`/`<link rel="stylesheet" media>` 的 viewport
选择和 document-owned CSS cache；browser setter 改变 raw attribute 后，调用者如需视觉
更新必须显式重新样式/布局。TEST1092 只验证 browser 消费者的 metadata 反射，不把它写成
Core 自动重排保证。

next647 又在 Core UA stylesheet 中提供 `[hidden] { display:none; }`。因此存在 `hidden`
属性的元素在默认样式和布局中不生成盒，未隐藏元素保持原有流程；这与 browser 已有的
`HTMLElement.hidden` attribute reflection 配套，但不实现 mutation observer、自动重排或
辅助技术语义。TEST1095 用隐藏/可见对照和后续段落的 `PCore_NodeBox()`/几何断言验证该
呈现边界；相关设备门 TEST21、TEST24、TEST1091、TEST1093、TEST1094、TEST1095、TEST999
通过 7/7。Core 没有为此新增公共 C ABI。

next648 又在 Core UA stylesheet 中加入披露控件的静态默认呈现：`details`/`dialog` 为 block，
`summary` 为 list-item；无 `open` 的 details 隐藏非 summary 子项，无 `open` 的 dialog 不生成
布局盒，带 `open` 的控件恢复布局。TEST1096 在 closed/open 对照文档中验证 details body、
summary 和 dialog 的盒状态；相关设备门 TEST21、TEST24、TEST1091、TEST1093、TEST1094、
TEST1095、TEST1096、TEST999 通过 8/8。该能力不新增公共 C ABI，不负责 summary 激活、属性
变化后的自动重排、模态焦点或 backdrop。

next649 又在同一 UA stylesheet 中加入 `pre[wrap] { white-space: pre-wrap; }`。带 `wrap` 属性
的 `<pre>` 在窄视口沿用 Core 的保留空白换行路径，普通 `<pre>` 仍保持 `white-space: pre`；
TEST1097 通过 120px 视口的代码块/文档高度对照，相关设备门 TEST21、TEST24、TEST1091、
TEST1093、TEST1094、TEST1095、TEST1096、TEST1097、TEST999 通过 9/9。该能力不新增公共
C ABI，也不保证完整 CSS whitespace、tab 度量或跨字体像素一致性。

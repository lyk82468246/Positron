# `positron_core.dll`

`positron_core.dll` 是 Positron 的 HTML/CSS/DOM/layout/paint 产品边界。它把移植后的 NetSurf、
libdom、libcss、hubbub 和图像适配隐藏在 UTF-8、opaque `HANDLE` 的公共 C ABI 后。

Core 不创建窗口、不运行消息循环、不连接网络，也不执行 JavaScript。应用只应链接公共 import
library，不应直接包含 NetSurf/libdom/libcss 头或依赖内部静态库符号。

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

DOM 或控件状态改变后，调用方负责重新执行所需的 style/layout，再 paint。HDC、viewport、滚动
位置和失效区域始终属于宿主。

## 资源获取

真实页面通常使用 `PCore_StyleDocumentEx2`：

- resolver 把 stylesheet、`@import` 和 `url()` reference 与 owning base URL 合并；
- fetch 返回临时资源字节；
- Core 在同步 callback 返回前复制需要保留的数据；
- Core 随后调用配对 free callback；
- 同一文档的成功资源进入有界 cache，重排可复用而不重新联网。

图片和 script discovery 使用对应的 fetch/enumeration API。Core 只负责发现、缓存和解释，不
调度 worker、不拥有 HTTP response，也不执行 script。网络失败应由宿主分类和记录。

## 能力分组

### 解析、样式与布局

- HTML → libdom document；
- CSS parse、UA/author cascade、media 条件与 inheritance；
- `<style>`、外链 stylesheet、`@import` 和 inline style；
- box construction、normalisation、layout、geometry、scroll extent；
- GDI paint、clip、文字、border、背景和 retained image carrier。

Core 支持项目当前经过验证的 HTML/CSS 子集，但不是完整现代浏览器。具体缺口见已知限制。

### 查询与命中

布局后可按坐标或 DOM id 查询链接、片段、控件、summary 和 box geometry。扩展查询把 href、
target、rel 等 UTF-8 元数据复制到调用方缓冲；容量不足或 stale layout 会 fail closed。

片段 token 按支持的 id/name 规则解析，但 history、URL percent-decoding、viewport scroll 和窗口
副作用仍属于 Browser/宿主。

### DOM 与关系 bridge

`PCore_Node*ById` 提供有界 text、attribute、value、checked 和结构查询。关系 API 覆盖：

- parent/child/sibling 与结构 root tokens；
- element attributes 与 childNodes snapshot；
- form owner、form controls 和 label/control；
- script/runtime 所需的有限 element metadata。

结果是同步 UTF-8 snapshot，不暴露 libdom 指针，也不承诺完整 live collection、namespace、
MutationObserver、Shadow DOM 或通用 selector engine API。

### 表单与 validation

Core 持有控件值、checked/selected、disabled/fieldset 继承、required/range/step/pattern/custom
validity、submission、multipart、reset 和 successful controls 等产品语义。

Browser/宿主在 dispatch 可取消事件后调用 Core mutation/default action，再按结果派发 input、
change、submit/reset 或 invalid。系统 picker、native validation UI、本地化提示、SIP/IME 和 WM
控件视觉不属于 Core。

### 交互与事件

Core 提供 hit testing、hover/focus/active/checked 状态、DOM listener/dispatch 和可聚焦目标枚举。
Browser 决定脚本事件/默认动作事务，宿主把 WM 消息和 native 控件状态映射进来。

键盘顺序、focus scroll 和 native HWND 切换需要三层共同完成；Core 不调用 `SetFocus`，也不创建
控件窗口。

## 所有权

- `PCore_Init`/`PCore_Shutdown` 成对；实现为进程级引用计数。
- `PCore_ParseHTML` 的 document 用 `PCore_FreeDocument`。
- `PCore_ParseCSS` 的 stylesheet 用 `PCore_FreeStylesheet`。
- 文档拥有 DOM、computed styles、box tree、资源 cache、image carriers、表单和交互状态。
- 从文档查询得到的借用数据在 document free、相关 mutation 或重新 layout 后失效。
- fetch 输入由宿主持有；Core 只保留自己复制的内容，并按契约调用宿主 free callback。
- DLL 返回的专用 buffer/handle 必须使用头文件指定的 `PCore_Free*`，不能跨 CRT heap 释放。

同一 document 默认只由一个 UI 线程串行操作。不要从网络 worker 并发调用 parse/style/layout/
paint，也不要在同步 Core callback 中销毁当前文档。

## 宿主应负责什么

- HWND、消息循环、DPI/旋转、scrollbar、invalid region 和 HDC；
- HTTP/TLS、后台 worker、取消、loading 和候选页面提交；
- native EDIT/SELECT/button/file picker 与 SIP/IME；
- history、浏览器 script session 和新窗口/外部协议策略；
- 失败回滚、日志、持久化和应用生命周期。

完整浏览器组合可参考 [`../test_host/README.md`](../test_host/README.md)，但通用 DOM、表单、
layout 或事件策略不得复制回宿主。

## 当前边界

- 不支持完整现代 HTML/CSS/DOM；float、复杂 table/position、Grid、custom properties 等仍有限。
- 字体、SVG、图像格式和高 DPI 结果受 WM6 GDI/依赖版本限制。
- Core resource cache、DOM bridge、表单集合和深度/数量均有固定预算。
- Core 不执行 JavaScript；请与 `positron_browser.dll`/`positron_script.dll` 组合。
- 精确 API、返回码、结构布局和借用期限以 [`positron_core.h`](positron_core.h) 为准。

整体所有权见 [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

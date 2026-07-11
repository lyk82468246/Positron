# WM6 / Positron Debugging Notes

## 先查环境，再查代码

这个项目里大量“故障”最终都是环境问题。用户已经多次纠正过：不要一听失败就直接改源码。

先检查：

- WMDC 是否 still connected。
- WMDC Connection Settings 里是否允许设备联网，且 host 连接类型是 The Internet。
- 模拟器 IE Mobile 能否打开一个已知网站。
- WM6 的 X 按钮只是最小化，不是关闭；是否有旧 `test_host.exe` 僵尸进程。
- `scripts\stage.bat` 是否真的复制了新二进制到 `C:\WMShare`。
- 模拟器共享目录是否还挂载在 `\Storage Card`。
- 是否 Rebuild whole Solution，尤其是改了静态库或 vendored NetSurf 代码时。
- 首选用 `scripts\build.bat`；默认是 `Debug` 增量 Build，退出码和 `vs2008-build.log` 可供 agent 直接判定结果。改了工程依赖、生成规则或需要干净基线时运行 `scripts\build.bat Debug rebuild`。脚本使用 `devenv.com`，不要直接调用 ARM `cl.exe` 拼装整套工程。
- 设备上是否在跑旧的 VS Deploy 目录，例如 `\Program Files\test_host\`。

## 没有 console，MessageBox 就是调试器

WinCE/WM6 上没有 stdout。定位崩溃/卡死时：

- 用 `MessageBoxW`，不要用 `MessageBoxA`。
- 一律带 `MB_TOPMOST | MB_SETFOREGROUND`。
- 插入编号 stage probes，例如 `BUILD 7 / step 3`。
- 必须带 BUILD stamp。看不到新 stamp 就说明跑的是旧二进制，先修部署。
- 跨 DLL / 静态库边界可以在 DLL 对象里放非 static trace hook，再从 vendored 源码 extern 调它。

诊断完成后：

- 删除所有 probe。
- 只留下真实修复。
- 用 `git diff --stat` 确认没有诊断垃圾。

## C89 / VS2008 规则

- 不写 mid-block declarations。
- 不写 `for (int i = ...)`。
- 不写 designated initializers，除非后续有转换脚本处理。
- 谨慎使用 `scripts/c89ize.py`：它主要处理块中声明和 for 声明，不能包治 designated initializer / static aggregate initializer。
- 新移植 NetSurf content-handler `.c` 时，把 include 列表先对齐已经编过的 `layout.c`，否则容易出现 `private.h` 类型未定义连锁错误。
- 2026-07-08 复盘：`redraw_border.c` 编译时报 `html/private.h` 里 `dom_document` / `dom_node` / `bool` 连锁语法错误，根因仍是单文件 include 前置依赖不足，不是 `c89ize.py` 应处理的问题。修法是像 `layout_flex.c` / `table.c` 一样补齐 `layout.c`/`redraw.c` 的 dom/css/content 前置 include，再跑 `c89ize.py` 确认 0 change。

## test_host 文案纪律

改 TEST 时，同步修改：

- 分组选择器里的 TEST 范围。
- 测试开始前 `show_info` 的预期画面说明。
- 失败 `show_error` 文案。
- 最终 success summary。

设备端 MessageBox 是用户看到的全部 UI，错误文案就是用户可见 bug。

## 引擎切换后先审计旧几何断言

2026-07-10 复盘：TEST 11 的 `body.y=8` / `p.y=24` 来自旧手写布局器，它把 body 的 8px margin 与首段 1em margin 分开处理。M6 改走 NetSurf `layout_document()` 后会执行父子 margin collapse，当前测试 DOM 中两者的报告原点都在 `y=16`。因此看到 `body=(8,16,224,304)`、`p=(8,16,224,20)` 时，不是联网失败，也不是布局器没有运行，而是旧测试预期落后于引擎语义。

ENGINE 的基础测试 6-10 仍 fail-fast；引擎初始化成功后，TEST 11/15/16/18 应各自运行并收集失败，避免一条几何断言遮住后续结果。修改测试范围时还要检查最终 success summary，不能在 `rc != 0` 时误报全部通过。

TEST 11 不能只接受设备当前坐标：同时保留默认折叠组和 `padding-top:1px` 阻断组。前者预期 `body.y=p.y=16`，后者预期 `body.y=8,p.y=25`；两组一起通过才证明 margin-collapse 路径和停止条件都工作。

2026-07-10：用户真机截图已确认 TEST 11 OK，显示 collapse 组 `body box = (8,16) 224x304`、`first <p> = (8,16) 224x20`；阻断组也在同一 TEST 11 内部断言。

图片路径分层记忆：TEST 18 只验证 `<img src>` 资源发现、fetch、document user-data 字节缓存和 URL 去重；TEST 19 只验证 WM Imaging API 能从内存 BMP 取尺寸并通过 `IImage::Draw` 画到 HDC；TEST 20 验证缓存字节变成 `box->object`，经 `content_redraw -> plot_bitmap` 真正绘制 `<img>`。用户于 2026-07-11 确认 TEST 18（first/second=2/2，fetch calls=2）与 TEST 20 均通过。

2026-07-10 真机反馈：首次 TEST 19 使用内存 PNG 时，`PCore_ImageInfoFromMemory` 失败并显示“could not decode”。后续处理：去掉手写 `_WIN32_DCOM` 避免重定义警告，增加 `PCore_ImageLastError(stage, hr)`，并把 TEST 19 改为 2x2 BMP 基线。第二次真机反馈为 `stage=2 hr=0x80070057`，即 COM init invalid argument；WM6 SDK `winx.h` 把 `CoInitialize(x)` 映射为 `CoInitializeEx(x, COINIT_MULTITHREADED)`，所以 `pcore_wmimage.cpp` 已改用 `COINIT_MULTITHREADED`。2026-07-10 用户确认 BMP 基线正常；后续若 WM Imaging 又失败，看 stage：2=COM init，3=CoCreate factory，4=CreateImageFromBuffer，5=GetImageInfo，6=Draw。

2026-07-10：旧 TEST 18 已真机得到 `found=2 fetched=2`。后续缓存版测试必须再扫描同一文档，并确认结果仍为 `2/2`、fetch callback 总调用数仍为 2；只看第二次也成功不足以证明去重。

2026-07-11：TEST 13 打开 IANA Reserved Domains 的截图暴露正文、导航和页脚向左裁切。根因不是页面固定宽度：`pcore_select.c` 仅设置 `css_media.type=screen`，却把 `css_media.width/height` 留为 0；libcss 的 `@media (min/max-width)` 直接比较这些字段，于是所有断点按 0px 选规则。修复是从 `PCore_SetViewport` 的 unit context 填充媒体宽高，并在 Browse 的 `StyleDocumentEx` 前设置实际 client viewport。TEST 21 在 320px/299px 分别断言 `min-width:300px`/`max-width:299px`；若又退回 0px 就会选错规则并失败。这里的 299/300/320 仅为测试边界，运行时宽高和 DPI 都从设备 client/HDC 动态取得。

同一页面的第二轮截图仍有约 25px 左侧裁切，但 TEST 21 已通过，所以继续检查真实 CSS：IANA 的窄屏 `article.sidenav` 是 `flex-direction:row-reverse`、左右 padding 25px，且侧栏 `display:none`。`layout_flex.c` 反向主轴起点把 content width 减去 opposite padding，导致唯一的 main item 左移一个 padding。修复为 `leading padding + content size`，TEST 22 断言 224px viewport 中 main 必须为 `x=25,width=174`。

2026-07-11：用户真机截图确认 TEST 22 OK，随后 IANA TEST 13 的 `Example Domains` 左缘不再裁切。不要把这条结论扩大成“页面版式完整”：同一组截图的页脚/导航仍有拥挤、局部错位与替代方框。后续必须先用 computed style + `PCore_NodeBox` 缩成最小复现，不能仅凭观感继续修改 clip 或硬编码页面尺寸。已验证范围与完成条件统一记录在 `KNOWN_LIMITATIONS.md`。

2026-07-11：当前 IANA CSS 确认大量使用 `(width <= 1000px)` / `(width < 1200px)`，libcss 3.11 不解析这类 MQ4 范围。`PCore_ParseCSS` 现只在字符串/注释之外改写整数 px 的 `width <=` 和 `width <`，严格小于按 WM6 整数 client px 转成 `max-width:N-1px`。首次实现使用 `wsprintfA`，VS2008 编译通过但 WinCE 链接报 LNK2019；已改为内部十进制格式化并加溢出保护，随后全解决方案 9/9 构建通过。不要因构建通过跳过扩展 TEST21 的设备边界验证。

同日用户截图确认扩展 TEST21 OK。TEST13 中 `a□number`、`maintained□for`、`are□provided`、`require□the` 与线上 HTML 的源码 LF 精确对应；`Homepage□` 是 SVG alt fallback 后的格式化换行。根因不是字体或网络，而是 `pcore_make_text_box` 未移植上游 `box_construct_text` 的 normal/nowrap 空白折叠。现已折叠 ASCII 空白并用 `box->space` 保存词间距，TEST15 同时要求 `normal_ws=ok` 与 `pre_lf=kept`，防止全局删除换行；设备结果待确认。

首次设备结果为 `normal_ws=ok pre_lf=FAIL`，同时 TEST13 方框已消失且词间距正常。这不是普通空白修复回归，而是最小 `PCORE_UA_CSS` 漏了 HTML 默认 `pre { white-space:pre }`，导致 `<pre>` computed style 仍为 normal。已按上游 `resources/default.css` 补齐 `font-family:monospace; white-space:pre; margin-bottom:1em`；不得删除 `pre_lf` 反例来让测试变绿。

补齐 UA 默认后的用户截图确认 TEST15 OK：`normal_ws=ok pre_lf=kept`。随后 `WM_SIZE` 的滚动恢复从“旧像素值裁剪”改为按旧/新可滚动范围保持比例；扩展 TEST24 要求 0%、50%、100% 分别映射到新范围的 0%、50%、100%，并继续要求 restyle 不联网。

2026-07-11 用户截图确认扩展 TEST24 OK；真实 TEST13 在竖屏 `Further Reading / Domain Names` 区域旋转到横屏后仍保持同一阅读位置，旋转响应式与阅读进度闭环。随后导航第一阶段改为 worker 主文档 GET + `WM_APP` 完成消息；若设备仍卡顿，先区分“主 GET 期间进度条是否停止”和“GET 完成后资源/style/layout 提交是否短暂停止”，两者属于不同阶段。

导航第一阶段首次设备反馈：loading 条持续移动，等待主 GET 时旧页可滚动且成功后正常换页；网络失败未测。滚动时 loading 条会被 `ScrollWindowEx` 复制并随页面下移，timer 也因每 100ms 调用一次文档绘制造成轻微卡顿。修复后滚动矩形排除顶部 5px，纯 loading invalid region 只清除/重画固定条带，不再调用 `PCore_PaintDocument`；待设备复测复制残影与流畅度。

第二次设备反馈仍可见多条 loading 残影。下一版不再由父窗口 `WM_PAINT` 绘制进度条，改用独立 `STATIC` 子窗口并给 render window 加 `WS_CLIPCHILDREN`；timer 只 `MoveWindow` 子窗口。若该版仍复现，按已知视觉缺陷挂起，不再阻塞资源异步化主线。

`STATIC` 子窗口在设备上完全不可见。复核 WM6 SDK 后改用正确的 Common Controls 路径：`InitCommonControlsEx(ICC_PROGRESS_CLASS)`、`PROGRESS_CLASS`、`PBM_SETRANGE/PBM_SETPOS`，并链接 `commctrl.lib`。控件位于 render client 的 `y=0`，不是系统任务栏坐标；高度来自 `SM_CYHSCROLL` 且至少 6px。若 `CreateWindowExW` 失败，窗口标题会显示 `Positron render - loading`，便于区分创建失败与绘制/遮挡。

旋转调试注意：`WM_SIZE` 当前会调用 `PCore_SetViewport` 和 `PCore_LayoutDocument`，所以几何会重新 flow；它不会调用 `PCore_StyleDocumentEx`，因此跨 CSS 断点时媒体规则可能仍是旋转前的选择结果。排查旋转问题时先区分“layout 没更新”和“style 没重选”。

2026-07-11：用户截图显示最终汇总 `Tests passed`，明确列出 ENGINE 的 TEST 6-11、15、16、18、21、22 全部通过。它是离线 HTML parse/select/style/layout、media-query viewport、反向 flex、box tree 及图片资源发现/document cache 的回归证据；不覆盖网络 Browse 或 GDI Render 组。

2026-07-11：IANA 页脚的 HTML 是 table cell 内的 `<ul>`，其条目规则为 `display:inline; float:left`。曾向 `pcore_box.c` 加入仿 NetSurf 的 `BOX_FLOAT_LEFT/RIGHT` 匿名包装，TEST23 最小复现通过；但真实 Browse 截图随即出现全页严重错位和替代方框。结论：该最小测试没有覆盖 pcore 精简构盒与上游 normalisation/list-marker 等前后条件。实现已撤回，TEST23 不再运行；必须先恢复真实页面基线，再以端到端回归重新设计 float 移植。

同日线上 IANA CSS 的文件名与此前版本不同，并使用 CSS custom properties 和 `@media (width <= 1000px)`。这不是 TEST21 的 `min-width` / `max-width` 断言所覆盖的语法；定位真实页问题前先确认实际抓取的是哪一个 CSS 版本，不能把旧截图结论外推到新站点资源。

2026-07-11：为旋转跨断点新增 document-owned 外链 CSS 原始字节缓存。首次 `StyleDocumentEx` 成功 fetch 后复制数据；后续 restyle 只从缓存重新解析，`WM_SIZE` 传入的 callback 永远失败，作为“禁止联网”的防线。缓存上限为 32 份、单份 256 KiB、每 document 合计 512 KiB。重样式替换 node user-data 时还会显式释放旧 `css_computed_style`，因为 libdom 替换 user-data 不调用旧析构回调。用户已确认 TEST24：320px 首次 fetch 选绿色，299px cache-only restyle 选蓝色，fetch/free 计数都保持 1。真实 Browse 旋转仍需验收。

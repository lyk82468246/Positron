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

图片路径分层记忆：TEST 18 只验证 `<img src>` 资源发现、fetch、document user-data 字节缓存和 URL 去重；TEST 19 只验证 WM Imaging API 能从内存 BMP 取尺寸并通过 `IImage::Draw` 画到 HDC；TEST 20 验证缓存字节变成 `box->object`，经 `content_redraw -> plot_bitmap` 真正绘制 `<img>`。TEST 20 尚待真机验证。

2026-07-10 真机反馈：首次 TEST 19 使用内存 PNG 时，`PCore_ImageInfoFromMemory` 失败并显示“could not decode”。后续处理：去掉手写 `_WIN32_DCOM` 避免重定义警告，增加 `PCore_ImageLastError(stage, hr)`，并把 TEST 19 改为 2x2 BMP 基线。第二次真机反馈为 `stage=2 hr=0x80070057`，即 COM init invalid argument；WM6 SDK `winx.h` 把 `CoInitialize(x)` 映射为 `CoInitializeEx(x, COINIT_MULTITHREADED)`，所以 `pcore_wmimage.cpp` 已改用 `COINIT_MULTITHREADED`。2026-07-10 用户确认 BMP 基线正常；后续若 WM Imaging 又失败，看 stage：2=COM init，3=CoCreate factory，4=CreateImageFromBuffer，5=GetImageInfo，6=Draw。

2026-07-10：旧 TEST 18 已真机得到 `found=2 fetched=2`。后续缓存版测试必须再扫描同一文档，并确认结果仍为 `2/2`、fetch callback 总调用数仍为 2；只看第二次也成功不足以证明去重。

# Core/DLL 交接：WM6 高 DPI 换行文字被裁剪

本文件是给负责 `positron_core.dll` / GDI layout-paint 的 agent 的故障交接和可直接复制的
处理 prompt。它记录一次真实 WM6 截图观察，不把尚未完成的设备复测写成产品基线；截图所属
的确切二进制版本仍须由接手 agent 先用 stage manifest 和 SHA-256 核对。

## 现象证据

- 截图：`tmp/QQ20260921-152149.png`（本地诊断附件，不加入 Git）
- 文件时间：2026-09-21 15:21:51；PNG 尺寸 585×643。外层包含 WM6 设备皮肤，应用客户区约
  480 像素宽；从字号和既有 640×480/192-DPI 设备记录看，优先按高 DPI 设备调查，但不能只凭
  皮肤图片断言实际 `LOGPIXELSX/Y`。
- WM6 外壳本身大致正常：系统 caption 在顶部，地址 EDIT 紧随其下，页面白色区域和右侧
  scrollbar 存在，底部 Shell softkey command bar 的 `Back` / SIP / `Menu` 也对齐；因此第一
  轮不要把问题归因于 command bar 的位置或窗口客户区分割。
- 页面正文不正常：第一段的第一行完整可见，换行后的第二行只剩约 2–3 个像素高的黑色残片；
  下一段重复同样现象。截图中约为第一段完整文字 y=256–276、第二行残片 y=292–294，下一段
  完整文字 y=344–364、残片 y=380–382。页面背景、滚动条和第一行没有同样的破坏，表现更像
  line box/clip/font metric 不一致，而不是整块 WM_PAINT 丢失。
- 页面文字在 192-DPI 设备上相对客户区偏大，首屏可见信息偏少；这可能是正确的物理像素缩放，
  也可能放大了字体度量不一致。不要仅凭“缩小 CSS 字号”来掩盖裁剪 bug。
- caption 当前显示应用内部状态 `Ready - offline welcome...` 并被截断，这是应用壳层的次要
  观感问题，不应与正文 glyph 裁剪混为一个 DLL 根因。

## 当前代码路径

### 应用侧（只用于重现和边界确认）

- [`positron_app/main.c`](../../positron_app/main.c) 的 `app_device_dpi()` 读取窗口设备 DPI，
  `app_style_and_layout()` 调用 `PCore_SetDeviceViewport(g_page_width, g_page_height, g_dpi)`，
  然后调用 `PCore_StyleDocument()` 和 `PCore_LayoutDocument()`。
- `app_paint_page()` 对 WM6 HDC 设置 page viewport origin 和 `IntersectClipRect()`，再调用
  `PCore_PaintDocument()`。页面滚动量按 public header 约定作为 device-pixel offset 传入。
- 当前离线 CSS 为 `body{...font-size:14px}`、`h1{font-size:22px}`、`h2{font-size:17px}`。
  这些字号可以用于重现，但不是优先修复手段。
- 应用壳层的代码不应在 DLL agent 的修复中复制 layout、文字度量或 clip 语义。除非需要临时
  诊断，优先保持 `positron_app` 不变。

### Core/GDI 侧（优先调查）

- [`positron_core/pcore_plot_gdi.c`](../../positron_core/pcore_plot_gdi.c) 的
  `pcore_measure_dc()` 使用 `CreateCompatibleDC(NULL)`；布局宽度、split 和字体度量通过该
  DC 测量。
- 同文件的 `pcore_plot_font()` / `plot_text()` 在实际 paint HDC 上读取
  `GetDeviceCaps(hdc, LOGPIXELSY)`，创建 `LOGFONTW`，再用 `GetTextMetricsW()` 计算 ascent，
  以 `y - tm.tmAscent` 作为绘制 top。
- 这形成一个需要实证的高 DPI 风险：layout measurement DC 与实际窗口 HDC 的 DPI、font height、
  ascent 或字体 fallback 可能不同；line box 按较小度量布局，而 glyph 按较大度量绘制，最终被
  行/inline clip 裁掉。
- 同文件的 bounded font cache key 目前包含计算后的 px、weight、italic、family 和 font kind，
  但没有显式保存 DPI/DC identity；需要确认它不会跨 DPI 或跨设备复用不兼容的 HFONT。
- [`positron_core/pcore_box.c`](../../positron_core/pcore_box.c) 的
  `PCore_PaintDocument()` 从 HDC `GetClipBox()` 取得 redraw clip，设置 `data.x/y` 为负的
  device scroll offset，并调用 NetSurf `html_redraw()`。GDI plotter 的 `plot_clip()` 通过
  `SelectClipRgn()` 安装局部 clip。需要验证这些 clip 坐标与 HDC viewport origin 是同一坐标系。
- `PCore_SetDeviceViewport()`、device↔CSS 几何转换、`data.scale=1.0`、line baseline 和
  `PCore_DocumentHeight()` 必须一起核对；不能只改 paint 的一个坐标换算而破坏 hit-test、scroll
  或 geometry API。

## 可验证假设（按优先级）

1. **测量/绘制 DPI 不一致。** 记录实际 WM6 window HDC、`CreateCompatibleDC(NULL)` 和临时
   measure DC 的 `LOGPIXELSX/Y`、font px、`TEXTMETRICW.tmAscent/tmHeight`；在 96 与 192 DPI
   下比较同一段文字的 layout line height 与 paint glyph bounds。
2. **layout 的 device/CSS 单位与 redraw clip 单位混用。** 在 `PCore_LayoutDocument`、
   `html_redraw`、`plot_clip` 和 `plot_text` 临时记录第一、第二行的 baseline、clip top/bottom、
   viewport origin 和 scroll offset，确认第二行不是被一个只有几像素高的局部 clip 截断。
3. **GDI clip 被替换而非相交。** `plot_clip()` 当前使用 `SelectClipRgn()`；确认 NetSurf 传入
   的局部 clip 是否需要与调用方 page/update clip 相交，且在下一次绘制前能正确恢复。
4. **字体缓存或 fallback 不完整。** 检查 cache 是否把 DPI、font face/fallback kind 和实际
   HDC 度量纳入等价条件；确认每次 layout 使用的 font 与 paint 使用的 font 具有相同 ascent、
   descent 和 advance width。
5. **仅有应用 CSS/字号问题。** 只有在前四项被排除后，才评估默认 line-height、CSS px 到设备
   px 的预期比例；不能用减小字号、隐藏换行或扩大页面高度来伪装修复。

## 最小复现 fixture

使用不依赖网络和字体 fallback 的白底文本页，在 480×640 与 640×480、96 与 192 DPI 分别
style/layout/paint：

```html
<!doctype html>
<style>
body { margin: 12px; color: #202020; background: #fff; font-family: sans-serif; font-size: 14px; }
p { margin: 0 0 8px 0; }
h1 { margin: 0 0 8px 0; font-size: 22px; }
</style>
<h1>Positron</h1>
<p>This is the first independent Positron browser shell.</p>
<p>The page below is rendered by positron_core.dll.</p>
<p>Use a long enough sentence to force a deterministic second line in the viewport.</p>
```

复现时必须确认以下输入来自同一套 DLL/EXE，而不是旧 stage 目录混包：应用 SHA-256、七个 DLL
SHA-256、设备分辨率、`LOGPIXELSX/Y`、viewport width/height、scroll=(0,0)。截图应保留 96
和 192 DPI 两组对照，并同时记录 Core geometry/API 返回值。

## 修复完成标准

- 96 DPI 和 192 DPI 下，第一行及所有换行行的 glyph 均完整可见，不再出现 2–3 像素高残片。
- layout 的 baseline、line height、text advance 与实际 paint 字形一致；普通 fallback 字符也不能
  破坏整行。
- `PCore_DocumentWidth/Height`、scrollbar、`PCore_LinkAt`、focus geometry 和 page paint 使用
  同一 device-pixel 坐标合同；滚动后不出现新的裁剪或偏移。
- GDI HFONT、HDC、HRGN 等资源在重复 style/layout/paint、旋转和窗口销毁后无增长；失败路径不
  泄漏、不留下半个 document。
- 不以放宽 clip、跳过第二行、降低应用字号、扩大 viewport 或修改 test_host 断言作为修复。
- 通过相关自动测试、`python scripts/test_c89ize.py`、`python scripts/audit_repo.py`、Debug/
  Release ARMV4I 正式构建，并在同包 WM6 设备/模拟器上留下 96/192 DPI 截图证据。

## 可直接交给 DLL agent 的 Prompt

```text
你负责 Positron 的 positron_core.dll / GDI layout-paint。请调查并修复
docs/history/CORE_WM6_HIGH_DPI_TEXT_CLIP_PROMPT.md 描述的 WM6 高 DPI 换行文字裁剪问题。

证据截图是 tmp/QQ20260921-152149.png：WM6 caption、地址栏、底部 Shell command bar 基本
对齐，但正文每个换行后的第二行只剩约 2–3 像素高的黑色残片。不要先调整 positron_app
控件位置，也不要通过缩小 CSS 字号、隐藏换行或放宽断言掩盖问题。先核对同一 stage 的 EXE/DLL
SHA-256、设备分辨率和 LOGPIXELSX/Y。

优先检查：
1. pcore_plot_gdi.c 的 CreateCompatibleDC(NULL) measurement DC 与实际 paint HDC 的 DPI、
   HFONT、TEXTMETRICW ascent/descent/height 是否一致；
2. PCore_SetDeviceViewport 的 device/CSS 转换、layout line box/baseline、html_redraw 的
   data.scale=1.0 与 PCore_PaintDocument 的 device scroll offset 是否一致；
3. pcore_plot_gdi.c::plot_clip 的 SelectClipRgn 是否把 page/update clip 或第二行的局部 clip
   错误替换，GetClipBox 与 HDC viewport origin 是否在同一坐标系；
4. bounded font cache 是否跨 DPI、字体 fallback 或不同 HDC 复用了不兼容的度量。

请先用一个固定白底、sans-serif、包含确定换行的离线 HTML，在 480x640/640x480 和 96/192 DPI
下记录：viewport、DPI、measurement/paint font metrics、每行 baseline/clip bounds、document
extent、scroll offsets。修复必须保持 96 DPI 回归，并让 192 DPI 每一行 glyph 完整可见；不能只
改应用 CSS。需要改公共 ABI 时先说明原因，否则保持现有 ABI。完成后运行 C89、audit、Debug/
Release ARMV4I 构建和同包 WM6 设备/模拟器截图验证，并在结果中报告根因、修改文件、度量前后
对照和资源泄漏检查。
```

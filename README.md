# Positron

面向 **Windows Mobile 6 Professional**（Windows CE 5.2, ARMv4i）的现代基础设施与应用运行时。

Positron 一方面提供可被任意 WM 程序独立调用的现代 DLL 集合，包括 TLS、HTTP、JSON、图片与渲染核心等能力；另一方面在这些基础设施上建设自带浏览器内核和 Electron-like 应用运行时。当前主线已经进入 HTML/CSS 真实渲染：NetSurf 3.11 的解析、样式、layout/redraw、GDI 绘制和点击导航都在 `positron_core.dll` 后面跑通；JavaScript 是长期必须实现的目标，但尚不是当前可用能力。

公共 DLL 是正式产品，不只是 `test_host.exe` 或浏览器的内部依赖。架构与 ABI 原则见 [.agents/ARCHITECTURE.md](.agents/ARCHITECTURE.md)。

> **稳定性基线（2026-07-15）**：`main` 的产品源码已完整恢复到 `9c5c7c7`/next37，并打包为 next44；用户已确认 TEST13 从 start page 到 IANA 深层导航的全流程完全正常。next38 之后的 stylesheet metadata、base URL、redirect origin 和 timeout 实验历史保存在 `codex/post-next37-experiments`，短期挂起；详见 [.agents/ROLLBACK_NEXT37.md](.agents/ROLLBACK_NEXT37.md)。

---

## 当前状态

| Phase | 内容 | 状态 |
|---|---|---|
| **1** | `positron_tls.dll` — TLS 1.2 客户端（mbedTLS 2.16 LTS） | ✅ 完成，WM6 Emulator 验证 |
| **2** | `positron_json.dll` (cJSON 1.7.18) + `positron_http.dll` (HTTP/1.1：HTTPS via mbedTLS，明文 HTTP via WinInet) | ✅ 完成，WM6 Emulator 验证 |
| **3** | 嵌入式 CA bundle + verified TLS (`PTls_ConnectVerified`) + CryptGenRandom 熵源 | ✅ 完成，WM6 Emulator 验证 |
| **4** | `positron_core.dll` — NetSurf 内核移植（HTML/CSS 渲染层） | 🚧 正式 Browse 路径已走 NetSurf `layout.c/redraw.c`；flex、table、border、selector、缓存图片链、CSS 背景图与 NetSurf overflow scrollbar 已真机验证，窄屏复杂布局仍待补 |
| **5** | `positron_image.dll` — 可复用图片基础设施 | 🚧 retained 解码、SVG、PNG/JPEG 与原始像素入口均已真机闭环；ABI 1.4 原生 BMP/GIF 编码及 next52 原生标题栏 OK 真退出均已真机确认 |

Phase 3 验证：`test_host.exe` 的通信组——HTTPS GET（`checkip.amazonaws.com`，大陆直连纯文本 IP）、POST（postman-echo）、badssl.com 正样本 + expired + self-signed 三连测，全部真机通过。详见 [PHASE3.md](PHASE3.md)。

Phase 4 进展：vendoring NetSurf 3.11，五个底层库（libwapcaplet / libparserutils / libhubbub / libdom / libcss）全部在 VS2008 / WinCE / ARM 下编译通过（C99→C89 脚本化转换，见 `scripts/c89ize.py` 等）。`positron_core.dll` 已作为产品级引擎边界立起，公开 `PCore_ParseHTML/ParseCSS/StyleDocumentEx/StyleDocumentEx2/LayoutDocument/PaintDocument/LinkAt` 等小巧 opaque-HANDLE API。HTML→DOM、CSS 解析、CSS select/computed style、整树样式、外部 `<link rel="stylesheet">` 抓取、GDI 窗口绘制、垂直滚动、viewport/DPI 自适应、点击命中与导航、HTTPS verified fetch、明文 `http://` via WinInet、跨协议重定向、完整 Mozilla CA bundle 均已真机验证。`StyleDocumentEx2` 新增文档基准 URL 与宿主解析回调；CSS `@import` 使用 libcss 原生 pending/register API，WM 宿主用 `InternetCombineUrlA` 规范化相对 URL，TEST45 已真机通过。

当前 Browse 正式路径已经从早期手写块流布局切到 **NetSurf 真实布局/重绘引擎**：`PCore_LayoutDocument` / `PCore_PaintDocument` / `PCore_LinkAt` 走 `pcore_box_construct` → NetSurf `layout_document` → `html_redraw` → GDI plotter。M7-flex/table、M5f border、CSS attribute/sibling selector 与 `:link` / `:lang()` 已由 TEST 9/17 真机验证。TEST 11 的 margin collapse 与 `padding-top:1px` 阻断折叠成对断言已于 2026-07-10 真机通过。`<img>` alt fallback 已由 TEST 17 验证；TEST 18 的文档级资源缓存与 URL 去重、TEST 20 的 BMP/PNG/JPEG/GIF 缓存 replaced box/`content_redraw`/`plot_bitmap` 绘制均已真机通过。TEST 21 已验证运行时 viewport/DPI 及整数像素 MQ4 `(width <= Npx)` / `(width < Npx)` 的 320/300/299px 边界。TEST13 已确认 `white-space:normal/nowrap` 的源码换行被正确折叠、词间距正常；TEST15 又确认 `<pre>` 换行仍保留。TEST 22 已验证反向 flex 的 25px leading padding；TEST38-39 进一步关闭了 IANA 顶层根变量造成的窄屏间距问题，当前截图中的导航、正文和注册表列均已可读，但其他真实子页仍需持续观察。Browse host 在布局前使用同一 HTTP 获取器填充 `<img>` 缓存，失败仍保留 alt/src 回退。SVG parse/draw/cache/fallback 已由 TEST25-28 真机通过，TEST13 的 HTTPS HTML + 相对 SVG 网络 fixture 也已显示正确。详见 [PHASE4.md](PHASE4.md)、[.agents/ROADMAP.md](.agents/ROADMAP.md) 和 [.agents/KNOWN_LIMITATIONS.md](.agents/KNOWN_LIMITATIONS.md)。

当前可用能力：TLS/HTTP/JSON 通信栈；HTML/CSS/DOM 解析；CSS select + computed style；整树样式；外链 CSS；NetSurf real layout/redraw；GDI plotter；滚动、viewport/DPI 自适应、点击链接导航；flex、常见 table、border、CSS attribute/sibling/static-pseudo selector、`<img>` alt fallback 与 `<img src>` 资源发现/fetch。WM Imaging 的 BMP/PNG/JPEG/GIF 与缓存 `<img>` 链已真机验证。`positron_image.dll` 公共 C ABI 已接通 WM Imaging、Expat、libdom XML、libsvgtiny 与 NanoSVG rasterizer；`PImage_CreateBitmapFromMemory/BitmapGetInfo/DrawBitmap/FreeBitmap` retained 位图对象会复制输入字节，NetSurf 图片载体也复用同一解码对象。2026-07-15 的 TEST19/20 已确认四格式颜色、清空调用方缓冲后重复绘制、损坏输入拒绝、旧核心 ABI 转发与正式缓存链；TEST26/27 和 TEST13 同批无回归。

最新设备反馈（2026-07-16）：next44 的 TEST13 全流程确认 next37 恢复点可作为 Browse 冻结基线；next45 又确认公共位图 ABI 的 TEST19 四格式、TEST20 缓存图片、TEST26/27 SVG 与 TEST13 全流程均正常。next46 的 ABI 1.0 独立 WM 示例也已在横竖屏确认，SVG 曲线缩放后的平滑观感略逊于先前大图但可接受。next47 已确认 ABI 1.1 的 PNG/JPEG 内存编码、DLL 配套释放及重新解码闭环；next48 证明 WM `EncoderQuality=100` 仍无法修复小图色度串扰。next49 已确认静态 libjpeg-turbo 1.5.3 的显式 quality JPEG 为正确 4:4:4，行方向、红绿蓝黄颜色与 PNG 一致，大面积色带消失。Debug DLL 相比 next48 增加 243,712 字节（约 238 KiB），设备不需额外 JPEG DLL；额外 CPU 与约 `width*height*3` 的主要中间像素内存只发生在显式 JPEG 编码。next50 的六项截图又确认 ABI 1.3 的 padded BGR24、straight-alpha BGRA32、RGB/alpha PNG、JPEG 与 SVG 一致。next51 进一步证明 ABI 1.4 的 BMP/GIF 隐藏编码、签名与回读检查通过，但也证明标题栏 X 是 Shell Smart Minimize，不保证发送 `WM_CLOSE`。next52 改用 WM/Pocket PC 原生 `SHDoneButton(SHDB_SHOW)` 标题栏 OK，并在 `WM_COMMAND/IDOK` 销毁窗口；用户已确认点击 OK 后进程消失且可以正常再次启动。next53 又确认 TEST46 四行三列表格 span 颜色/位置正确，TEST13/17/41/42 其余功能正常。next54 虽让 TEST41 的 auto-height 横条获得独立空间并去掉短页无效纵条，却因第二次整树 layout 同时改变 fixed-height overflow 几何而令 TEST42 自动断言失败；next55 已限制二次 layout 只影响 auto-height 容器并修正右箭头坐标，用户现已确认 TEST41/42、短页纵条与色块页全部正常。next56 的 TEST47 红/白、绿/蓝两行及同批其他测试现已确认正常。next57 的 TEST48 已确认列表层级和有序计数语义；next58/59 的随包字体最终让基础箭头、marker 与五个单色 emoji 可见。next61 的 TEST50 已确认 IV/z/aa/09 计数、绿色缓存 SVG marker 与圆形失败回退全部通过。next62/TEST51 与 next63/TEST52 又依次确认 inline-first 和 block-first/空条目/嵌套/图片的 inside marker 流在横竖屏符合预期。next64/TEST53 的纵横屏截图现已确认 collapsed border 的宽度、样式、hidden、来源 tie 与 separate 对照均符合预期；next65/TEST54 又确认 finite/auto rowspan、colspan 与 row-group 四组终止边正确。上述改动均没有重启 next38 之后的导航实验。

当前明确缺口：位图四格式与 SVG 网络/缓存/fallback/fill-rule/基础渐变缓存链已经闭环，但径向焦点 `fx/fy` 与 spread method 仍是 NanoSVG 光栅器的显式 TODO。CSS Variables 兼容层只替换同一 stylesheet 顶层精确 `:root` token，不支持元素作用域、跨 stylesheet cascade 或 `@property`。现代值兼容只处理数值型 `oklch()` 到裁剪 sRGB，以及无需布局上下文即可完全求值的同单位 `calc()`；混合单位、`color-mix()` 和完整 CSS Color/Values 仍未支持。CSS Grid 目前只是保持文档顺序的单列 block 降级，TEST41 只防止 grid 内宽表格推走整个 flex 页面，不代表网格轨道或 gap 已实现。标准 NetSurf overflow scrollbar 已由 TEST42/next55 验收，但不包含触摸惯性或 overlay scrollbar。CSS `@import` 的嵌套解析、失败空表回退和文档缓存已由 TEST45 验收；它尚不代表跨源策略、完整缓存失效或整页资源进度已完成。有效表格的 span 占位和匿名 row/cell 已由 TEST46/47 真机确认；next64/TEST53 已验证 NetSurf 原生 cell/row/row-group/table collapsed-border 基本冲突规则，next65/TEST54 又验收 span 终止边与 row-group 边界；next66/TEST55 正验证 cell vertical alignment 与 separated-table `empty-cells`，`col`/`colgroup` 来源仍是 NetSurf 3.11 及当前官方源码的 TODO。列表 marker 的 47 种上游 counter formatter、缓存 `list-style-image`、失败回退和 inside 首行流已由 next61-63/TEST50-52 验收；float 邻接 marker、自定义 `@counter-style` 与所有语言字体仍未完成。`background-size`、多层背景、web fonts 和脚本资源仍未实现。UI 提交已在 parse/style/image-discovery/layout 四个调用之间让出 WM 消息循环，单个不可重入的 NetSurf 调用仍可能短暂卡顿。复杂 SVG text、动态状态伪类、float、forms/widgets 仍不完整；JavaScript 尚未实现但属于长期必做目标。

next59 的 TEST49 已确认四个箭头不再 tofu、marker 和五个单色 emoji 均可见，视觉比 next58 稍有改善。next60 首次 TEST50 显示 `found=4`，核查确认不是当前源码逻辑失败，而是 staging 在最后一次 Debug 增量编译后又修改了 `pcore_select.c`，最终错误地组合了新 `test_host.exe` 与旧 `positron_core.dll`。next61 重新增量编译后 TEST50 已通过；`stage.bat` 现会先执行同配置增量 Build，失败时不再复制任何产物。next62 的 TEST51 与 next63 的 TEST52 均已由横竖屏截图确认，inside 文本/图片 marker、悬挂换行、block-first、空条目和嵌套布局符合预期。

next58/59 的随包单色 symbol/emoji fallback 是宿主基础字体，不等于网页字体：`@font-face` 下载、复杂 emoji ZWJ/variation shaping、彩色字体仍未实现。`ANTIALIASED_QUALITY` 是向 WM GDI 提出的灰度抗锯齿请求，最终效果仍由设备/OEM 字体光栅器决定。

next66 的 TEST55 首次真机运行读到 `FFFFFF/00C300/C6C300`：隐藏格为白、强制 show 格为绿、filled 格为青，功能分类正确；失败是 WM compatible bitmap 的 3-6 色阶量化与过严精确 RGB 断言共同造成的假阴性。next67 只将 TEST55 改为逐通道紧容差，core layout/redraw 不动。TEST13 Further Reading 新出现的圆点来自 IANA 页面真实列表项与已验收的 marker 支持，不是表格对齐回归。

next67 的 TEST55 已通过自动断言，设备截图也确认 top/middle/bottom、大小字体共基线、rowspan 底对齐及白/绿/青 empty-cells 正确。截图另外暴露测试页的四组固定高度刚好超出 WM 客户区十几像素，产生几乎填满轨道的纵向滚动条。next68 已压缩 TEST55 的行高/间距并显式设定标题行高，没有用滚动条掩盖可见内容。

---

## 工具链

- **编译器**：MSVC 9.0（VS2008 SP1，C89-only，无 C99/C++11）
- **SDK**：Windows Mobile 6 Professional SDK (ARMV4I)
- **目标 Subsystem**：`windowsce,5.02`
- **链接库**：`ws2.lib`（WinCE 版 Winsock2，非桌面 `ws2_32.lib`）
- **加密库**：mbedTLS **2.16.12 LTS**（最后一条真正 C89 兼容的主线；2.28+ 用了 C99 mid-block 声明 VS2008 编不动）
- **JSON 库**：cJSON **1.7.18**（C89 兼容）

---

## 仓库结构

```
Positron/
  Positron.sln                  VS2008 solution（含 NetSurf 静态库与产品 DLL）
  README.md                     本文件
  PHASE1.md                     Phase 1 经验/坑记录
  PHASE2.md                     Phase 2 设计 + 已知风险点

  positron_tls/                 TLS 1.2 DLL
    positron_tls.h              公开 API
    positron_tls.c              实现（DllMain + Winsock2 BIO + 熵源 + API）
    mbedtls_config.h            裁剪后的 mbedTLS 配置
    ca_bundle.h                 嵌入的完整 Mozilla 根集（~140 根，脚本生成）
    gen_ca_bundle.py            从 curl cacert.pem 提取根证书的生成脚本
    positron_tls.vcproj
    mbedtls/                    mbedTLS 2.16.12 源（不入 git，见下）

  positron_json/                cJSON 包装 DLL
    positron_json.h / .c / .vcproj
    cjson/                      cJSON 1.7.18 源（已入 git）

  positron_http/                HTTP/1.1 客户端 DLL
    positron_http.h / .c / .vcproj

  positron_core/                NetSurf 引擎共享 DLL 边界
    positron_core.h / .c          PCore_* API（Parse/Style/Layout/Paint/LinkAt）
    pcore_box.c                   DOM+computed style → NetSurf box tree；正式 layout/paint/link path
    pcore_plot_gdi.c              NetSurf plotter + GDI 字体量度表
    pcore_talloc.c                精简 talloc 垫片
    nsshim/                       NetSurf layout/redraw 依赖的精简 shim 头
    positron_core.vcproj          DLL，静态链入 NetSurf 库与移植的 layout/redraw 源

  assets/fonts/                 WM GDI 随包静态 symbols/mono emoji fallback 字体

  positron_expat/               Expat 2.8.2 静态库及 WM/VS2008 适配
  positron_libsvgtiny/          NetSurf libsvgtiny 静态库
  positron_image/               可供任意 WM 程序调用的图片 DLL（WM 位图 + SVG retained C ABI）

  samples/positron_image_demo/  仅依赖 positron_image.dll 的独立 WM C 示例

  test_host/                    端到端测试 EXE（分通信/引擎/GDI渲染/Browse 组）
    main.c
    test_host.vcproj

  compat/                       VS2008 + WinCE 缺的 C99 shims
    stdint.h
    inttypes.h

  scripts/
    stage.bat                   一键把 5 个二进制拷到 C:\WMShare\
    stage_image_demo.bat        只打包图片 DLL 与独立示例

  .agents/                      Codex 接手交接、调试纪律、路线图
```

---

## 编译

### 一次性准备

1. 安装 **VS2008 SP1** + **Windows Mobile 6 Professional SDK** + **WM6 Pro Emulator**
2. 下载 mbedTLS 2.16.12：
   ```
   https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/mbedtls-2.16.12.zip
   ```
3. 解压，把 `include/` 和 `library/` 整体复制到 `positron_tls/mbedtls/` 下：
   ```
   positron_tls/mbedtls/include/mbedtls/...
   positron_tls/mbedtls/include/psa/...
   positron_tls/mbedtls/library/*.c
   positron_tls/mbedtls/library/*.h
   ```
   （`mbedtls/` 不入 git，开发机自备）

cJSON 已经入 git，无需额外下载。

### 构建

命令行构建（agent 和日常开发的首选入口）：

```cmd
scripts\build.bat                 :: 默认 Debug 增量 Build
scripts\build.bat Debug rebuild   :: Debug 全量 Rebuild
scripts\build.bat Debug build     :: Debug 增量 Build
scripts\build.bat Release rebuild :: Release 全量 Rebuild
scripts\build.bat Debug clean     :: 清理 Debug
```

脚本调用 VS2008 的 `Common7\IDE\devenv.com`，按解决方案中的工程依赖构建
`Debug|Windows Mobile 6 Professional SDK (ARMV4I)`，并将完整输出写入
`vs2008-build.log`。ARM 编译器本体位于 `VC\ce\bin\x86_arm\cl.exe`，但不应绕过
`.sln` 直接逐文件调用它，否则必须手工复制 SDK include/lib、宏、链接参数和工程顺序。

也可以打开 `Positron.sln`，确认顶部工具栏：
- Solution Configuration = `Debug`
- Solution Platform = `Windows Mobile 6 Professional SDK (ARMV4I)`

**生成 → 生成解决方案** (F7)。产物：

```
positron_tls/bin/Debug/positron_tls.dll
positron_json/bin/Debug/positron_json.dll
positron_http/bin/Debug/positron_http.dll
positron_core/bin/Debug/positron_core.dll
test_host/bin/Debug/test_host.exe
```

---

## 部署到模拟器

> ⚠ **VS2008 内置 Smart Device 部署对当前工程不可用**——无论 RemoteDirectory 怎么配都被部署引擎覆盖。详见 [PHASE1.md](PHASE1.md)。改用模拟器共享文件夹。

### 一次性配置

1. 启动 WM6 Pro Emulator（VS → 工具 → Device Emulator Manager）
2. Cradle 它（连 WMDC）
3. 模拟器 → File → Configure → **Shared folder** 选 `C:\WMShare\`
4. 主机 Windows Mobile Device Center → Mobile Device Settings → Connection Settings：
   - 勾 "Allow data connections on device when connected to PC"
   - "This computer is connected to" 选 **The Internet**
   - Uncradle / Cradle 重挂一次

### 每次构建后

```cmd
scripts\stage.bat         :: 默认先增量构建 Debug，再打包
scripts\stage.bat Release :: 或 Release
scripts\stage.bat Debug C:\WMShare\Positron-next :: 旧进程锁文件时隔离 staging
```

把 6 个二进制、测试配置及 `fonts` 子目录拷到 `C:\WMShare\`。

模拟器内 Start → File Explorer → **Storage Card** → 双击 `test_host.exe`。

### 测试入口

`test_host.exe` 启动后先选择测试组。部署时必须保留 EXE/DLL 同级的 `fonts` 子目录：

快速复测可在 `test_host.exe` 同目录放置 `test_host.ini`：

```ini
# 支持逗号、空格、范围，以及特殊编号 7b
tests=31,32
```

读取到有效配置后，程序先提示是否只运行列出的 TEST；选 **Yes** 直接按编号升序执行，选 **No** 完整保留下面原有的 All/四组选择流程。文件缺失时直接走旧流程；文件存在但无效时提示并忽略。TEST23 已撤回，配置中出现 23 会被拒绝。`scripts\stage.bat` 会先调用同配置的 VS2008 增量 Build，再复制默认 `test_host\test_host.ini` 及三份静态 fallback 字体；构建失败不会留下混合版本包。当前批次运行 TEST13、17、41、42、46-56；可在共享目录直接修改后重新启动。第 3 组 GDI Render 提示已改为能力类别和编号范围，避免 WM6 小屏 MessageBox 被逐项说明撑坏。

测试交付默认按能力批次进行：先积累多项相关实现、自动像素/资源/安全断言和直绘/正式链两层回归，再请求一次设备验收。只有真实编译错误、高风险回归定位或设备特有故障才临时拆成单项包，避免每个微小改动都要求人工截图。

- **Communication**：TEST 1-5，TLS / HTTP / JSON，需要网络。
- **Engine**：TEST 6-11、15、16、18、21、22、24、25、38、40-45，HTML/CSS/DOM/select/style/layout/box tree/image resource cache、responsive media viewport、row-reverse flex padding、cached CSS restyle、SVG parse、受约束的 `:root` token、现代 CSS 值、grid 宽度隔离、overflow scrollbar、分阶段导航资源事务、主文档失败回滚与 CSS import tree，离线。TEST40-45 已真机通过；TEST23 float 最小样例已因真实 Browse 回归撤回。
- **GDI Render**：TEST 12、14、17、19、20、26-37、39、46-56，覆盖 WM Imaging、SVG path/cache/fallback/fill-rule、CSS background-image、原生 GDI text、线性/径向渐变、继承/透明 stop、缓存复用、IANA token 间距、table span/匿名归一化/collapsed border/cell alignment/height distribution、列表 marker/counter/image/inside flow 与随包字体 fallback 正式 redraw，离线；TEST48-55 已验收，TEST56/next68 待设备验收。
- **Browse**：TEST 13，真实页面抓取 + 渲染，需要网络；HTTPS 走 mbedTLS verified，明文 HTTP 走 WinInet。

当前关键 smoke test：

- TEST 17：内置 NetSurf real layout + redraw 页面。预期看到深红 H1 及红色下边框、带边框的三色 flex 横排、带可见单元格边框的 2×2 table。
- TEST 13：打开 start page，点击 `Open example.com`，走真实 Browse 路径；页面内链接可继续点击导航，有限 3xx 与 http/https 切换已覆盖。

> ⚠ **跑 TEST 5 之前先把模拟器系统时钟设到当前**（开始 → 设置 → 系统 → Clock & Alarms）。WM6 Emulator 默认是 2005-2007 年某个时间，会让所有现役证书都看着像"尚未生效"。

---

## 已知限制 / 注意事项

- **熵源**：默认 `CryptGenRandom`（Phase 3 起）；CSP 不可用时自动退回 QPC+GetTickCount+tid/pid jitter，CTR-DRBG 兜底。
- **HTTP 限制**：单连接 `Connection: close`、无 keep-alive、无 gzip 解码、响应体 cap 1 MB；GET 已有有限 3xx follow，明文 `http://` 经 WinInet。
- **导航卡顿**：主文档、外链 CSS、CSS `@import`、`<img>` 与已计算 CSS 背景资源的 GET 已组成分阶段 worker 事务，旧页在网络等待时可滚动。`PHttp_GetEx/PostEx` 在请求线程报告已解码正文大小和可选 `Content-Length`；父窗口进度条对已知总长显示当前响应的真实百分比，对 chunked/无长度响应保持活动动画，TEST3/13 已真机确认。每个资源响应会开始自己的进度序列，所以这不是整页资源总字节百分比。HTML parse、style、图片 cache copy、layout 仍严格留在 UI 线程，但现通过一次性 WM timer 分成四个提交阶段，让触摸、旋转、绘制和进度控件可在阶段之间运行；单个 NetSurf 调用仍可能短暂卡顿。TEST44 已确认主文档失败保留旧页与事务收尾，TEST45 已确认嵌套导入与失败回退。网页字体/脚本资源仍待后续。`test_host` 暂存最多 64 个 URL、合计 2 MiB 原始字节；这是可替换的宿主预算，不是 `positron_core` ABI 限制。
- **渲染限制**：TEST25-37 与 TEST13 fixture 已确认 SVG parse/draw/cache/fallback/fill-rule/网络链、CSS 单背景图、基础 SVG text、线性/径向渐变、继承/透明 stop 及缓存复用；复杂 SVG text、径向焦点、spread method、background-size 和多层背景仍未完成。TEST38-39 已确认受约束的 `:root` token，TEST40 已确认数值型 OKLCH/可求值 calc；两者都不代表完整 CSS Variables/Color/Values。TEST41 只验证 grid 单列降级不会把反向 flex 主内容推至负坐标；TEST42 验证的是 overflow scrollbar，不是完整 Grid。TEST46/47 覆盖有效表格 span 与有限匿名包装。TEST48 只覆盖常见列表 marker 和十进制 HTML 计数，不代表完整 CSS Lists/Counter Styles。TEST23 浮动实现已因 Browse 回归撤回。完整范围见 [.agents/KNOWN_LIMITATIONS.md](.agents/KNOWN_LIMITATIONS.md)。
- **WM6 X 按钮 = 最小化不是关闭**。每次启动 test_host 前确认任务管理器没有遗留实例，否则 stage.bat 替换 exe 时会产生 image 不一致。
- **WMDC 桥会静默断**：host 待机 / 模拟器长跑后偶尔失联，表现是 `PTls_Connect` 拿到 `-0x004C [BIO: recv WSA=...]`。修法：重启 WMDC（任务栏 → 退出 → 重启）。**联网测试前先在 IE Mobile 打开 baidu 验证一遍**。
- **模拟器时钟**：跑 verified TLS 前必须校准（见上）。证书 notBefore/notAfter 都按 UTC 比对当前时间。

---

## License

代码本身 MIT。mbedTLS 是 Apache 2.0（详见 `mbedtls/LICENSE`，不入 git，跟随上游）。cJSON 是 MIT。

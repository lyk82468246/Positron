# Core HTML 图片尺寸取证

本文件保存 WinWorld 图片尺寸缺陷的取证与回归依据，不是当前路线图。稳定使用合同见
[Core README](../../positron_core/README.md)，当前设备证据见
[HANDOFF](../../.agents/HANDOFF.md)。

## 消费者证据与归属

消费者在 `https://winworldpc.com/library/operating-systems` 报告 OS/2 图片过大并挤出视口。
原始 HTML 的 DOS、Windows、Apple 和 OS/2 图片均声明 `width="80px"`；对应自然尺寸为
390×408、150×122、150×150、355×355。问题属于 Core cascade/layout，不属于 EXE DPI
除法、HTTP 传输或 Image 重采样。用户手工截图位于本地 tmp，不提交到 Git。

## 确认的根因与最小修正

1. `pcore_select.c` 的 `node_presentational_hint()` 原来恒定返回零项，img 的 width/height
   完全未参与 cascade。现在只为 img 提供两个有界、同步借用的长度 hint，作者 CSS 仍可覆盖。
   属性采用 HTML dimension 数字前缀规则，不把兼容尾缀当成 CSS 单位；超限值不产生 hint。
2. `pcore_construct_flex()` 把直接 img 作为普通块递归构造；img 无 DOM children，因此丢失
   replaced object。现在保留缓存图像 carrier 并 blockify；资源不可用时仍有 alt/src fallback。
   相关 NetSurf min/max 分支同时允许 replaced flex item 使用声明尺寸，避免原始 bitmap
   宽度覆盖 80 px 声明。
3. `<img>` 自然尺寸 carrier 未按设备 DPI 投影。192 DPI 下 `width:auto` 的 150 px 图像
   被布局为 150 设备像素，公开 CSS 几何只有 75 px。现在仅对 img-specific carrier 转换，
   Image handle、共享 cache metadata、naturalWidth/Height 与背景 tile 不变。
4. img width/height attribute Set/Remove 原来保留旧布局。成功操作现在使 retained layout
   失效，后续正常 style/layout 重建几何，不触发图像重复解析。

未修改公共 ABI、EXE、Browser、Image 或网络接线，没有网站特判。NetSurf 改动通过 Core
正式工程编译，不让测试宿主编译产品实现。

## 自动合同与误判防护

TEST1340 使用与消费者四种图片相同的自然尺寸、纯色 SVG fixture；它是 sizing 回归，
不冒充真实 PNG 文件或整站视觉验收。96/192 DPI 分别验证声明宽高、只有一边、CSS auto、
外部/style block/行内覆盖、空/非法/带符号/整数溢出/129 字节属性、零、小数、百分比、
兼容尾缀、min/max、直接 flex 及其 CSS 覆盖、物理 paint 与链接命中。
width 修改、移除与 height 修改后重排验证新尺寸，重复 layout 保持四个独立 SVG 的解析次数。

DOS 高度按布局整数比例量化：96 DPI 为 80×83 设备像素，192 DPI 为 160×167，公开
CSS rect 分别为 80×83；不能把后一高度硬写成 166。80.5 px 在设备像素量化后再投影为
CSS 整数，两个 DPI 的结果也不能机械写成相同值。

早期夹具误用了零面积 relation 的输出、旧 LinkAt 成功返回值，并在同一 inline 行上把
第二张图片的红色像素当作第一张的越界绘制。夹具现按公开合同检查零面积不可用/清零输出，
使用独立 block link 隔离 paint/hit；没有通过缩减产品断言掩盖这些错误。原始空 hint、flex
对象丢失/自然宽度覆盖与高 DPI auto 半尺寸均各有设备失败证据，保存在本地 tmp。

相邻回归覆盖 PNG/SVG、异步 pending、真实 IANA SVG、CSS background/data URI、rgba、
column flex、overflow 与 Fragment DPI；Release 另保留 TEST13 的真实网络三段导航哨兵。
正式 VS2008 曾在编译期提前退出，Windows Application Event 1000 确认 devenv.exe 异常；
保留失败构建日志并按同一正式入口重试，不把桌面异常当成设备崩溃或更换工具链。

## 仍未覆盖

本批不实现解码前的现代 aspect-ratio/source 占位、所有 replaced-flex 转移尺寸规则或完整
现代图片加载模型。设备门的几何/像素证明不等于真实应用四张 PNG 的视觉证明；必须由 app
会话以匹配完整包复核真实页面，不使用缩小解码 bitmap 或 EXE 特判绕过 Core。

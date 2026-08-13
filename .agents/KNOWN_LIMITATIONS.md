# Positron 当前限制

更新时间：2026-08-13

这里只记录当前仍存在的产品或验收边界。已完成批次和设备流水不保留在本文件；最近证据见
[`HANDOFF.md`](HANDOFF.md)，稳定架构见
[`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

## 浏览器 JavaScript

当前状态：

- 默认 `javascript=0`。
- 显式开启时已有 classic inline/external script、页面 context，以及受限的
  DOM、Event、form、input、location 和 history bridge。

尚未完成：完整 DOM/window、module、异步任务、CSP、同源策略、任意 Web API 和完整
URL Standard。

完成方法：每个上游能力单独做纵向测试；关闭路径不得新增脚本请求；完整自动门和对应
人工门都通过后才能扩大声明。

## 独立 JavaScript DLL

当前状态：Duktape 2.7.0 的持久 context、执行预算、内存限制、模块、global/JSON 和
native callback 已存在。

尚未完成：它不是浏览器环境；context 不支持并发或 callback 重入；源码、模块、内存和
callback 均有硬上限。

完成方法：只在外部消费者有明确需求时扩展稳定 ABI，不以浏览器私有对象污染公共 DLL。

## URL 与 History

当前状态：已有有限的成功-GET 宿主历史、后退/前进/go、受控 state、
fragment/hashchange，以及逐步扩展的相对 URL 分类。

尚未完成：

- 不是完整 URL parser；
- 没有页面缓存和持久历史；
- 没有滚动/表单恢复；
- 没有完整跨文档 state 生命周期或 POST 恢复；
- 绝对和根相对 URL 中多个完整编码 double-dot segment 尚未支持。

完成方法：每种规范化语义都要有正反例、无 GET、state/length 和事件门；设备全量通过后
才能提升基线。

## HTML/CSS 布局

当前状态：block/inline、常见 flex/table/list、基础 relative/absolute、overflow 和 hover
可以覆盖一批轻量页面。

尚未完成：float 已撤回；Grid、sticky、复杂 containing block、完整 table 边界、
完整 CSS Lists 和大量高级 CSS 未实现。

完成方法：优先移植或复用上游数据流，并同时通过离线几何、TEST13 深链、旋转和人工截图。

## 页面视觉

当前状态：example.com/IANA 主链已有多轮设备与人工证据。

尚未完成：这些证据不能外推为任意网站兼容。局部容器尺寸、字体测量、抗锯齿以及
高 DPI/旋转组合仍可能异常。

完成方法：为可重复页面记录 viewport/DPI、computed style、box geometry 和前后截图，
再建立针对性回归。

## 表单与输入

当前状态：已有 native EDIT/SELECT、textarea、checkbox/radio、提交/reset、基础 constraint
validation、键盘和部分 composition bridge。

尚未完成：任意 OEM IME、完整 composition/preedit、类型/范围/step、custom validity、
`invalid` UI、完整 activation 和文件选择体验。

完成方法：synthetic event 与真实 SIP 分开验收；至少覆盖候选词、Unicode、旋转和
native control 生命周期。

## 图像与 SVG

当前状态：BMP/PNG/JPEG/GIF、缓存 `<img>`、SVG retained draw、损坏 fallback 和部分编码
已经验证。

尚未完成：位图 codec 受设备影响；没有完整 SVG/CSS Images、动画、全部滤镜/字体或通用
web-font 系统。

完成方法：公共 `positron_image` 直测和 core 正式链同时通过，包含损坏输入、所有权和
多次 redraw。

## 网络与安全

当前状态：HTTPS 默认验证证书链和 hostname；明文 HTTP 使用 WinInet；页面资源分轮加载。

尚未完成：Mbed TLS 固定在停止维护的 2.16.12；HTTP/1.1 能力有限；设备旧时钟、OEM
网络栈和站点变化会影响结果。

完成方法：中期评估仍兼容 MSVC9 的受维护 TLS；网络失败必须按
DNS/TCP/TLS/HTTP/页面提交阶段取证。

## 性能与线程

当前状态：主文档和资源网络工作已移出 UI 线程；已有阶段遥测和文档内/重叠文档图像复用。

尚未完成：parse/style/layout 和部分 image create 仍在 UI 提交阶段；没有完整浏览器缓存、
后台 DOM/layout 或全面内存策略。

完成方法：只有设备热点阻塞可用性时插队；先用阶段数据定位，不跨线程共享 DOM/GDI。

## 字体

当前状态：随包提供 symbol 和单色 emoji fallback，缺失时可以诊断并降级。

尚未完成：它不是普通语言 web-font 系统；覆盖范围和 OEM 字体渲染有限。

完成方法：新字体必须有明确页面需求、来源/许可证、内存预算和设备视觉证据。

## 验收边界

- 自动首帧和数值断言不能替代字体、边距、抗锯齿、触摸、SIP 和旋转人工检查。
- 设备日志不在工作区时，不能仅凭口头“跑完了”把候选升级为基线。
- 真实网站是集成哨兵，其远端内容和网络状态可能变化；稳定语义还需要离线 fixture。
- 96 DPI 不是产品常量。每次 viewport、旋转或 interaction restyle 都要保留真实设备 DPI。
- `test_host.exe` 是消费者，不能把只适合宿主的私有接口伪装成公共 DLL 能力。

## 当前 URL 分类缺口

以下仍按普通导航或不支持处理：

- 绝对 URL 中重复完整编码 double-dot segment；
- 根相对 URL 中重复完整编码 double-dot segment；
- 完整与半编码 double-dot 混合；
- 字面 `..` 与编码 segment 混合；
- 规范化后 query/path 不同的 URL；
- 越过 origin 根或没有非空前驱目录的折叠。

下一批只选择其中一个边界，并按 [`HANDOFF.md`](HANDOFF.md) 的完成标准验收。

## 不得用限制掩盖回归

限制表示能力尚未完成，不表示可以接受新崩溃、数据损坏、旧页面严重布局破坏或核心交互
阻塞。遇到这些情况立即回退候选路径并查
[`FAILED_EXPERIMENTS.md`](FAILED_EXPERIMENTS.md)，不能通过删除测试、扩大预算或放宽断言
把失败写成“已知限制”。

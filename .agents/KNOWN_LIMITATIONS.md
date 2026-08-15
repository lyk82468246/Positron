# Positron 当前限制

更新时间：2026-08-15

这里只记录当前仍存在的产品或验收边界。已完成批次和设备流水不保留在本文件；最近证据见
[`HANDOFF.md`](HANDOFF.md)，稳定架构见
[`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md)。

## 浏览器 JavaScript

当前状态：

- 默认 `javascript=0`。
- `positron_browser.dll` 已拥有独立 history/session 产品层、PScript context、host JSON callback
  的 session 注册/调用生命周期、产品 bootstrap 文本和求值入口，以及 DOM 只读（按 id 查询与
  textContent 读取）JSON 分发；显式开启时仍有 classic inline/external script、页面 context，
  以及一套尚在宿主迁移中的 DOM 写入、Event、form、input、location 和 history bridge。

尚未完成：完整 DOM/window、DOM 写入/Event/form/input/location/navigation callback 实现、module、
异步任务、CSP、同源策略、任意 Web API 和完整 URL Standard；JavaScript bridge 仍有一部分
实现位于 `test_host/main.c`，尚未成为可供正式浏览器应用复用的 browser-layer API。

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
- 仍不是完整 URL Standard parser。

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
- WMDC RAPI 1 不暴露可靠远端退出码/等待/终止；自动 gate 以完整日志标记判定完成，超时后
  不会杀死设备进程，需要人工确认进程状态。

## 当前 URL 分类边界

绝对和根相对 URL path 中多个完整 `%2E%2E` segment 的受控折叠已由 next221 全量设备门
验证；history state 的根相对 path/query/fragment 与 query-relative 写法已由 next222 验证，
当前 document 目录下的单段 sibling、显式 `./` 单段/多段 sibling、裸多段
document-relative sibling、显式 `./?query`/`./#fragment` trailing-slash 写法，以及
同源 absolute URL 在 path 不变时的 query/fragment 变化已由 next228 验证。
HTTP 的 `:80`/无端口和 HTTPS 的 `:443`/无端口同源等价已由 next229 验证。
安全的同源 absolute pathname 变化已由 next230 的 TEST197 和 145 项全量设备门验证；
普通 percent-encoded pathname segment 已由 next231 的 TEST198 和 146 项全量设备门验证；
根相对 pathname 的同一安全校验已由 next232 的 TEST199 和 147 项全量设备门验证；
显式 `undefined` history URL 默认当前 document URL、显式空字符串保持同 URL entry 的语义
已由 next233 的 TEST200 和 148 项全量设备门验证，均不发 GET；history/session 状态机迁入
`positron_browser.dll` 已由 next234 的 TEST201、TEST149-201 定向门和 149 项全量门验证；
浏览器脚本 session 的 context 所有权、JSON callback 注册/调用和销毁已由 next235 的 TEST202、
55 项脚本回归门和 150 项全量门验证；bootstrap 文本与求值入口已由 next236 的 TEST203、56
项脚本回归门和 151 项全量门验证；DOM 只读 callback adapter 已由 next237 的 TEST204、
`TEST189-204,999`（17 项）定向门和 `TEST13/20/27/43/44/56/58-77/80-204/999`（152 项）
全量门验证，DOM 写入/Event/form/input/location/navigation callback 实现仍在宿主，尚未计入
产品层完成项。
以下仍按普通导航或不支持处理：

- 完整与半编码 double-dot 混合；
- 字面 `..` 与编码 segment 混合；
- 含 literal/mixed/complete encoded dot segment、重复分隔符的 absolute/root-relative pathname
  （普通 percent-encoded segment 已受限支持）；
- 越过 origin 根或没有非空前驱目录的折叠。
- 裸 `./`、`.` 和 `../` history state URL；
- protocol-relative history state URL；
- 同源 absolute/root-relative history URL 的不安全 path 变化（安全 pathname、普通
  percent-encoded segment、同 path query/fragment 变化已受限支持）；
- IDN、userinfo 和其他完整 URL Standard origin 规范化；默认端口只支持上述 HTTP/HTTPS
  两组等价形式。

## 不得用限制掩盖回归

限制表示能力尚未完成，不表示可以接受新崩溃、数据损坏、旧页面严重布局破坏或核心交互
阻塞。遇到这些情况立即回退候选路径并查
[`FAILED_EXPERIMENTS.md`](FAILED_EXPERIMENTS.md)，不能通过删除测试、扩大预算或放宽断言
把失败写成“已知限制”。

# 当前已知限制

本文件只列仍然存在、会影响设计或验收的限制。已解决问题不保留在这里；失败路线见 [`FAILED_EXPERIMENTS.md`](FAILED_EXPERIMENTS.md)，旧事故见 [`docs/history/`](../docs/history/README.md)。

## 平台与工具链

- 目标是 Windows Mobile 6 / Windows CE 5.2 ARMV4I，不支持现代桌面 Windows API 假设。
- 正式构建依赖 Visual Studio 2008 SP1 与 Windows Mobile 6 Professional SDK。
- 产品 C 代码受 C89 约束；部分第三方源码依赖仓库转换器和 WinCE CRT shim。
- VS/WMDC/Device Emulator 属于外部专有工具链，仓库不能提供或重现完整安装环境。
- WM6 的 Smart Minimize 可能保留进程和系统级 DLL 映射；跨 stage 运行存在混用旧 DLL 的风险。

## TLS 与 HTTP

- mbed TLS 固定在 2.16.12，已结束上游支持；没有 TLS 1.3，发布前必须审查当前漏洞与信任数据。
- verified 客户端仍受旧平台证书、时钟、密码套件和根证书快照限制。
- insecure 连接入口仍为兼容/诊断而保留，调用方若误用会失去证书与 hostname 认证。
- peer TLS pin 的空值只适合受控 discovery/TOFU，不代表已认证对端。
- identity 文件由消费者负责持久化、访问控制、备份和轮换，DLL 不提供系统密钥库。
- HTTP 只覆盖有界 HTTP/1.1，不提供 HTTP/2、HTTP/3、连接池、完整缓存、cookie jar 或浏览器级代理策略。
- URL reference resolver 是保守的 HTTP(S) 子集，不是完整 WHATWG URL 实现；userinfo、IPv6、非 HTTP(S) scheme 和异常 authority 会 fail closed。
- 真实网络测试仍受设备时钟、DNS、TLS、代理和外部站点变化影响，离线契约不能替代网络哨兵。

## HTML、CSS 与布局

- HTML/CSS/DOM 由固定版本 NetSurf 支持库移植而来，不等于现代浏览器当前实现。
- CSS Grid、完整 float、完整 positioned layout、复杂 table/caption/column/baseline、完整 generated content 与自定义 counter style 未覆盖。
- 仅支持一部分媒体条件、selector、字体与单位；custom properties、`var()` 和大量现代函数缺失。
- `details`/`summary`、`hidden` 等只有受限静态或交互子集；`dialog` 已有 Browser 脚本的 show/showModal/close/requestClose、returnValue、cancel/close 事件、活动 modal id、宿主驱动的 Escape 请求桥接和参考宿主的有界 backdrop 点击策略。Core/Browser 组合支持有 id 祖先 dialog 的显式、脚本和隐式 `method="dialog"` 提交，包括 validation、可取消 `submit`、submitter value 与直接 close；无 id、无祖先 dialog 或跨文档目标会 fail closed。宿主可以组合 Core 的 scoped focus snapshot 实现顺序 Tab/Shift+Tab 子树范围，并调用 `PCore_PaintDocumentWithModal` 得到实体色遮罩和指定 dialog 重绘；这不是 CSS `::backdrop`、透明合成或跨文档 top layer。Browser 不自动接管平台消息；宿主必须显式调用这些边界。
- Core 已支持有界的自定义 `tabindex` 顺序：正值升序（同值保持 DOM 顺序），随后是零/缺省组；`PCore_FocusTargetInfoWithin` 可按已知 DOM id 限定到一个祖先子树；负值、disabled/hidden/stale 目标和 file picker 仍会被排除。完整浏览器焦点策略、自动初始焦点、动态焦点区域和跨窗口焦点仍未实现。
- `contenteditable` 目前覆盖单元素的祖先继承、`isContentEditable`、有界纯文本 mutation、宿主编排的 `beforeinput`/`input`，Browser 的 `selectionStart`/`selectionEnd`/`selectionDirection`，以及去重后的非冒泡、不可取消 `selectionchange`。带 id 且已布局的有效 editing host 可由宿主映射为最多 16 个 WM multiline EDIT 代理；无修饰鼠标拖选和 Shift/方向键扩展会把 CRLF 位置转换为逻辑 UTF-16 并报告 forward/backward 方向，捕获丢失、取消模式和焦点切换会结束未完成手势而不重复通知。文本上限为 8192 UTF-8 字节，嵌套继承后代不重复代理。宿主另有受限 `CF_UNICODETEXT` paste/cut 事务，但 Range/Selection 对象、ClipboardEvent/async clipboard、CF_TEXT/富文本转换、OEM 特有的自动重复与复杂行导航、design mode 和完整 IME 组合仍未实现。
- 字体 fallback 使用 bundled 子集与系统 GDI，不能保证桌面浏览器字形、kerning、emoji 彩色渲染或抗锯齿一致。
- WM6 高 DPI、字体度量和设备色深会产生量化差异；自动像素断言不能取代整体视觉判断。

## 图像与 SVG

- 位图能力受 WM Imaging 与固定 libjpeg-turbo 版本限制，不宣称支持所有损坏或渐进编码边界。
- SVG 是 libsvgtiny 与 NanoSVG 的有限组合，不支持完整 SVG DOM、filter、mask、animation、script、external resource、完整 paint server 和任意文本排版。
- 径向渐变焦点、spread method、复杂继承和部分 alpha/compositing 边界仍不完整。
- Core 的页面 image cache 有固定数量和字节预算，超限或解码失败时降级为 alt/src 文本。

## DOM、表单与事件

- DOM bridge 以有界 ID/结构 token 和 snapshot collection 为主，不是完整 live DOM/CSSOM。
- 大量 IDL reflection、namespace、mutation observer、range/selection 和 shadow DOM 不存在。
- 表单实现覆盖常用控件、validation、submission、reset 和 successful controls，但没有完整本地化 validation UI、所有 input type 的系统 picker 或桌面浏览器级 editing 行为。
- `labels`、form collections 和若干 NodeList 是静态 snapshot；文档 mutation 后调用方应重新查询。
- 事件系统覆盖常用 capture/target/bubble、取消和默认动作，但不支持所有 DOM Event 子类、pointer/touch/drag/drop/clipboard 或浏览器手势。宿主对单元素 `contenteditable` 另有受限 `CF_UNICODETEXT` paste/cut/copy 接线：非空选区才复制，折叠选区保持剪贴板不变，超长或非 Unicode 格式在 native mutation 前拒绝；它不是通用 DOM ClipboardEvent 或 async clipboard API。
- native 控件状态由 Core、Browser 和宿主共同提交；回调错误、stale token 或几何变化会 fail closed，可能表现为本次默认动作不执行。

## JavaScript

- 浏览器 JavaScript 默认关闭，启用后仍是实验性的有界 classic-script 组合。
- 独立 script 和浏览器 script 共用 Duktape 2.7.0，不存在第二套引擎；两者提供的 host objects 与生命周期不同。
- 不支持 ES module、dynamic import、WebAssembly、worker、service worker 或完整现代 ECMAScript host environment。
- Browser bootstrap 只暴露当前已接线的 DOM/Event/form/navigation/timer 子集；缺失 API 通常 fail closed 或为 `undefined`。
- script heap、native function、module/source、timer、queue 和执行时间都有固定预算；复杂页面可能因资源上限失败。
- Browser session callback 同步且不可重入；宿主若在 callback 中销毁或重入 session，行为不受支持。
- 该运行时不是完整浏览器安全沙箱，不能直接执行不可信互联网脚本并假定与现代浏览器等价隔离。

## History、导航与窗口

- history 是进程内、有界条目集合，不持久化到磁盘，也不恢复跨进程页面状态。
- same-document 与跨文档 scroll restore 只覆盖当前宿主保存的有界 viewport 信息。
- 当前是单窗口/单 browsing context 组合；`_blank`、未知 named target、第二个 global、opener、跨窗口 history 和真实窗口复用未实现或保守拒绝。
- `window.open()` 仅在允许复用当前 context 的受限 target 上工作，不创建新的 WM 顶层窗口。
- download、外部协议、权限、文件系统和应用跳转策略仍由宿主决定。
- 后台网络阶段已经与 UI 文档提交分开，但 DOM parse/style/layout/paint 仍在单一 UI 线程，复杂页面可能造成短时卡顿。

## Native 控件、SIP 与设备 UI

- Windows Mobile EDIT/COMBOBOX/button/file picker 的真实行为因 ROM、OEM 和输入法而异。
- synthetic `WM_CHAR`/key/composition/mouse 测试可以证明 WM EDIT 代理的事务边界、有界脚本选区同步、selectionchange 去重、无修饰拖选方向、Shift/捕获/焦点中断的收尾，以及 TEST1115 的受限 `CF_UNICODETEXT` paste/cut data、取消和 fail-closed 路径、TEST1116 的 `WM_COPY` 非空/折叠选区和超长/非 Unicode 拒绝；由于 WinCE 的直接 `SendMessage` 不会更新键盘状态表，TEST1114 对 Shift 扩展在 key-up 前注入有界原生范围，不能替代 OEM 默认键盘行为。CF_TEXT/富文本转换、不同应用的剪贴板互操作、候选词窗口、完整 IME、真实硬键盘、自动重复或 SIP 视觉仍需人工验收。
- 文件选择器的权限、取消、窗口返回和路径显示需要真实设备人工验收。
- select popup、焦点矩形和滚动可见性仍可能受控件窗口层级与 DPI 影响；next670 已修复并在 192-DPI 设备验证 block 文本 label forwarding，复杂嵌套 label 或其他窗口层级组合仍需人工观察。
- 旋转、不同 screen/DPI、软键盘占用区域和系统非客户区只能通过设备观察确认。

## WMDC 与自动化

- 设备连接必须由用户在 WMDC/Device Emulator GUI 中完成；设备门不能自动选择、cradle 或重置。
- RAPI 1 只暴露当前连接，不支持安全枚举多个设备；自动门假定恰好一个当前目标。
- 主机 WMDC 重装可能恢复旧 RAPI COM 注册值并触发 `0x8007007E`，应使用严格、幂等的修复脚本取证，不要手改未知注册项。
- RAPI 没有可靠的通用远端强杀语义；超时后可能需要用户在设备上关闭遗留进程。
- 自动可视门只保证首帧和断言，不保证边距、字体、触摸、SIP、picker、旋转或失败网络体验。

## 测试覆盖

- next670 已建立一次 1080 项动态全量自动设备 checkpoint；后续定向门仍不能替代下一次按风险触发的全量范围基线。
- 已有一个离线 compatibility-corpus 场景（TEST1117），把固定 HTML/CSS 中的 contenteditable、dialog validation、`method="dialog"` close、same-document history 和失败候选回滚串成一条自动流程；它只覆盖这一条流程，不代表任意真实网站或完整 Web 标准。
- tracked INI 是快速 smoke，不是测试全集；全量自动清单由打包/门脚本从源码 dispatch 生成。
- manual-only fixture 必须在 `auto=0` 下运行，不能放入自动全量并把主动跳过视为通过。
- TEST13 是一个真实网页哨兵，不代表任意互联网网站兼容性。
- 人工风险可以按规则累计，但崩溃、数据损坏、严重布局破坏和核心交互阻塞必须立即复核。

## 明确不保证

- 现代浏览器标准符合性或任意网站可用性；
- 在未审查旧依赖安全状态时用于高风险生产环境；
- 由 `test_host.exe` 提供可复用产品 API；
- 通过单次截图、提示音、部分日志或桌面构建证明设备基线；
- 绕过公开头文件、直接链接内部 NetSurf 静态库后的 ABI 稳定性。

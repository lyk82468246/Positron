# `positron_browser.dll`

`positron_browser.dll` 是 Positron 的页面 session 层。它把 `positron_core.dll` 的文档/布局关系和 `positron_script.dll` 的 Duktape runtime 组合成一个由宿主显式驱动的、有界浏览器对象模型。它不是窗口库、网络库或完整浏览器。

## 产物与依赖

正式工程输出 `positron_browser.dll` 与对应 import library。调用方只包含 `positron_browser.h`、`positron_core.h` 和需要的 `positron_script.h`，通过公开 callback table 接入 Core 和宿主；不要直接链接 NetSurf/libdom 静态库或引用 Browser 的内部结构。

Browser 依赖 Core 的 document handle、ID/关系 callback 和布局快照，依赖 Script 的 context/value 生命周期。宿主仍拥有 HWND、消息循环、网络 worker、native 控件、SIP/IME、文件 picker 和实际物理滚动。

## 最小调用流程

1. 宿主创建 Core document 并完成 parse/style/layout。
2. 宿主创建 Browser script/history session，注册 Core read/write、resource、focus、scroll、viewport 和 lifecycle callbacks。
3. 宿主在页面提交、`WM_SIZE`、滚动、焦点和可见性变化时显式通知 Browser。
4. 宿主按消息循环调用任务 checkpoint，读取 Browser 的 callback 请求并执行平台默认动作。
5. 关闭页面时先停止回调和 worker，再 teardown session，最后释放 Core/Script handle。

回调必须同步、有界、不可重入同一 session；借用 buffer 只在调用期间有效。缺失 callback、stale handle、超限参数和不支持的对象返回安全失败或 no-op。

## 页面 session 能力

- history entry、fragment、push/replace state、viewport scroll snapshot 与 `scrollRestoration`；
- navigation candidate/resource observation：generation、取消/退休、终态、required/optional gate、transport retry 预算和脱敏 failure/fallback 摘要；
- `document.readyState`、DOMContentLoaded/load/pageshow、visibility、beforeunload、focus/blur 和显式 teardown；
- `window`、`screen`、`visualViewport`、有限 `matchMedia`、timer/animation-frame/message/idle/microtask queue；
- Browser→宿主的 page scroll、element scroll、resize、focus 和 native-control 请求。

Browser 只报告 CSS/page 坐标和语义结果。宿主负责 clamp、物理坐标换算、style/layout/paint、窗口和实际消息调度；Browser 不直接访问 HWND、网络或设备。

## DOM、表单与资源 facade

Browser wrapper 以 Core ID/关系为真值。live Element、Text、Comment、CDATA 和属性 wrapper 在 mutation 后必须同步 owner、childNodes/children snapshot 与 identity；detached wrapper 只保存 bounded snapshot，不能伪造 connected 关系。

支持的 DOM facade 包括：

- `textContent`、非编辑元素的 `innerText`、`innerHTML`/`outerHTML`、`document.write` 和 title；
- selector 的 `matches`、`closest`、`querySelector(All)` 有界子集；
- form owner、validation、`form.elements`/`FormData` snapshot、同步 `formdata` 事件、option/select metadata 和有限 `HTMLImageElement` metadata；脚本 `FormData` 的 `append()`、新键 `set()` 与数组构造共享 64 项上限，超限抛出 `QuotaExceededError` 并保留旧 pairs；脚本 `URLSearchParams` 的 `append()`、新键 `set()` 与 pair-sequence 构造共享 `PBROWSER_SCRIPT_URL_SEARCH_PARAMS_MAX_PAIRS`（当前为 64）项上限，同样 fail closed，已有键替换和删除后的追加仍可用；
- `Headers`、`Request` 和 `Response` 的有界 metadata/body facade；Header 名称按 ASCII 不敏感规则 canonicalize，最多 128 个 pair，`toJSON()` 与对象初始化对 `hasOwnProperty`、`__proto__`、`constructor`、`toString` 等合法 header 名保持安全，不污染快照原型。
- `sessionStorage` 与 `localStorage` 是 session-owned、彼此独立的 Storage facade；每个对象最多 `PBROWSER_SCRIPT_STORAGE_MAX_ENTRIES`（当前 64）项，键和值分别限制为 256/4096 个 UTF-16 code units。新增条目、named-property 写入或超长键值会在 mutation 前抛出 `QuotaExceededError`；替换既有键和删除后重新占用容量仍可用。`setItem()`、`getItem()` 和 `toJSON()` 对 `hasOwnProperty`、`__proto__`、`constructor`、`toString` 等 object-property 名称保持独立，不破坏 Storage 方法或对象原型。
- DOM id、Browser-created wrapper、事件监听器、dataset JSON 和 BroadcastChannel registry 使用原型安全的内部 map；`__proto__`、`constructor`、`toString` 等作者可控字符串不会返回错误 wrapper、覆盖事件注册表或改变快照原型。`DOMStringMap.set()` 会把有界名称纳入 `keys()`/`toJSON()`，JSON 快照按 own property 定义；这不扩展为完整 named-property 或无限 DOM registry。
- `HTMLImageElement.decode()` 以及 host 驱动的 `load`/`error` 终态桥：宿主必须先让 Core 的
  current source、complete 和 natural size 就绪，再调用通知入口；source 改变或支持的
  `Element.id` setter 改名会回收旧 pending/终态 key，每个 session 的终态映射最多 64 项，
  Browser 不自动抓取、选择、解码或绘制图像。使用
  `PBrowser_ScriptSessionNotifyImageSourceChangeEx` 和
  `PBrowser_ScriptSessionNotifyImageEventEx` 时，宿主为每次 replacement 提供递增的非零
  generation；source A→B→A 的旧 promise/终态以及缺失、过旧或不匹配事件会 fail closed。
  source Ex 只对能解析到带 id `<img>` 的有界 `<picture>` 关系生效，否则宿主应改通知
  该 img；Core 仍拥有 source 选择，宿主仍拥有资源 I/O、解码、layout 和 paint；
- Text/CDATA 的 `data`、CharacterData mutator、`wholeText`、`splitText`、`replaceWholeText`、remove/reinsert 和 wrapper 关系。
- `document.createElement(tag)` 的 detached staging：每个 wrapper 可保存有界属性和最多 64 个直接 child，child 可以是 Text、Comment、CDATA 或另一个 Browser-created Element。nested graph 限制为最多 4 层、64 个 Element、每个 Element 64 个 child；每个 Element 都必须有唯一非空 id。`cloneNode(true)`、递归 `textContent`、attach、remove/reinsert 以及带唯一 id 的递归 Core 物化都会保留 wrapper/alias identity；循环、重复/缺失 id、超限或不支持的节点在 mutation 前拒绝。
- 已物化的 Browser-created Element wrapper 还支持 1–4 个 primitive 参数的 `before()`、`after()` 和 `replaceWith()`；同级文本插入复用现有 Core mutation callback，替换成功后 wrapper 回到 detached 状态。未物化目标保持 inert，Element、Fragment、CharacterData 或其他对象参数不在这条窄路径内。
- 已物化的 Browser-created Element wrapper 的 `innerHTML` setter 会在 Core parser 成功后原地刷新 `childNodes` snapshot 并使旧 child wrapper detached；`outerHTML` setter 通过 public wrapper alias 完成既有 Element replacement，成功后清理 alias 并允许同一 staged wrapper 重新设置 id、插入和使用。它不提供第二个 detached HTML parser，也不执行脚本、资源或 mutation event。
- 同一已物化 wrapper 的 `children` 会从 direct-child snapshot 生成有界 HTMLCollection，提供 `item()`、`namedItem()`、`childElementCount`、`firstElementChild` 和 `lastElementChild`；parser-backed child mutation 后这些读取与 `childNodes` 同步。detached staging 的 `children` 只反映直接 Element child，collection 不承诺完整 live 更新。
- 同一已物化 wrapper 的 `insertAdjacentText()` 与 `insertAdjacentHTML()` 覆盖 `beforebegin`、`afterbegin`、`beforeend` 和 `afterend` 四个位置，分别复用既有 Core Text-child callback 与 Ex9 parser 路径；成功后原地同步 `childNodes`、parser-created children 和 CharacterData wrapper identity。wrapper 脱离后仍保留 direct 普通 Text/CDATA 的最近一次数据快照；非法位置、对象/arity、重复 id、超限片段和 detached 调用 fail closed。
- 同一已物化 wrapper 的 `insertAdjacentElement()` 也覆盖四个位置，复用既有 Core element-child mutation；移动 regular 或另一个已物化 Browser-created Element 后，目标/来源的有界 snapshot 与 wrapper identity 会同步。未物化 source、非法位置、对象/arity、自引用和 detached target 在 mutation 前 fail closed；不扩展为任意 detached Node graph 或完整 live collection。
- 同一已物化 wrapper 的 `appendChild()`、`insertBefore()` 和 `removeChild()` 现在也能处理直接 Element child：既支持 regular source，也支持 detached/已物化 Browser-created source 的有界插入、移动和移除，并在成功后保留目标 `childNodes` 与可寻址 source snapshot、wrapper identity 和返回值。detached target、错误 parent/reference、无效对象和超限结构在 mutation 前 fail closed；nested graph 只沿上面的 4 层/64 Element 合同工作。
- 同一已物化 wrapper 的 `replaceChildren()` 对零至四个 primitive 值在 Core 替换后原地重建 Browser-owned Text wrapper，保留 `childNodes` collection identity，并使旧 child wrapper 脱离；对象、Element、CharacterData 或 Fragment 参数继续使用既有有界路径，超限和无效目标 fail closed。detached nested graph 仍受固定预算约束，不提供通用 live collection。

这些路径通过 Ex callback table 把父 ID、未过滤 child index、节点类型和 UTF-8 值转给 Core。宿主不遍历、合并或删除产品节点，也不复制第二份 form/selector/resource 语义。

## `DocumentFragment` 与 normalize

Browser-owned Fragment 是 bounded staging，不是 Core fragment handle。它最多保存公开合同允许的 direct 根和节点形状；嵌套、不支持类型、重复 id、跨 owner、超长文本或超过容量在消费前 fail closed。Element mutation 消费 Fragment 时复用 Core parser/creation callback，并保留 staged wrapper identity。

Detached Element 的 nested staging 与 Fragment 是两条不同边界：前者可在固定预算内保存并递归物化 Browser-created Element/CharacterData graph；Fragment 中的 Element 根仍沿既有 parser 路径，只接受 direct Text 形状，不能借此获得 nested Element、任意 Comment/CDATA 子树或第二套 detached parser。

Element relative mutation 只对已经以唯一 id 物化到 live Element parent 的 Browser-created wrapper 开放 primitive 文本参数。`before()`/`after()` 保留目标 wrapper，`replaceWith()` 成功后同步清除其 live alias；未物化或已脱离的目标不伪造 parent，直接返回 inert。该能力不扩展任意 detached Node graph、Fragment、CharacterData source 或完整节点列表语义。

`Node.normalize()` 有两条实现路径：live Element 通过 Ex5 `normalize_child_text` 调用 `PCore_NodeNormalizeById`，再同步 Browser-created Text/CDATA wrapper；detached Fragment 直接整理 staging。两条路径都删除空 Text/CDATA、合并相邻 Text/CDATA 到首个非空节点，并以 Element、Comment 和其他节点作为边界。Comment 不计入 Fragment `textContent`。成功 mutation 后宿主必须重新 style/layout/paint。

## 事件、输入与 native 桥

Browser 创建有限 Event、listener、属性 handler、validation 和 focus 对象，维护 target/currentTarget、冒泡/取消、可信标志及规定的顺序。它不自动接管 native default action；宿主决定按钮、SELECT、EDIT、file picker、SIP/IME、触摸和键盘行为，再用 typed callback 或通知入口回传实际结果。

contenteditable 只支持单元素、纯文本、UTF-16 selection offset 和有界 WM EDIT 代理。`beforeinput` 取消不会修改 Core；允许的 native mutation 由宿主提交后再通知 Browser。完整 IME composition、Range/Selection、async clipboard 和富文本不在边界内。

## 导航与资源组合

宿主为每个请求创建 candidate handle，并用 generation 隔离过时 worker 消息。Browser 持有 resource URL 去重、attempt/终态、required/optional gate 和清理前快照；宿主拥有 DNS/TCP/TLS/HTTP、worker、重试时机、response、页面 swap 和旧页保留策略。提交前调用 commit snapshot，释放前调用 cleanup snapshot，不能在宿主另建分类表。

## 预算与错误

native function、listener、collection、Fragment 根、selector 深度、字符串、FormData pairs、资源项和任务队列均有固定 WM6 预算。所有 public entry 都检查 NULL、UTF-8、容量、索引、句柄和 owner；size-probe 不部分写出，超限不部分 mutation。错误码和 callback table 版本以 `positron_browser.h` 为准，新增能力应追加 Ex 版本而不是改变旧字段含义。

## 宿主应负责的事情

宿主必须：

- 在 Core layout 后提供 viewport/geometry/scroll/focus 结果并安排重绘；
- 驱动 page lifecycle、resize、scroll、visibility、focus 和任务 checkpoint；
- 执行网络/资源 worker、native 控件默认动作、SIP/IME、picker、剪贴板和窗口策略；
- 在 teardown 前停止回调，按 candidate/resource 清理顺序释放对象。

`test_host.exe` 只能消费这些公开 DLL 接口，提供 fixture、callback 接线和断言；可复用的 URL、DOM、Event、form、resource 或生命周期语义不得回到宿主。

## 不保证

Browser 不承诺完整 DOM/Web API、任意网站兼容性、完整 CSS Selectors、通用 Node/Fragment mutation、MutationObserver、完整 live collection、Range/Selection、bfcache、复杂滚动树、pinch zoom、transforms、完整媒体查询、CORS/绝对 URL 策略或 OEM 视觉。真实设备的字体、边距、触摸、SIP/IME、旋转、DPI、picker、失败网络和布局观感按 [`docs/TESTING.md`](../docs/TESTING.md) 人工验收。

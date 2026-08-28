# Positron 当前已知限制

更新时间：2026-08-25

本文件只记录当前仍然成立的边界，以及解除边界所需的证据。已修复问题和旧 next 流水由 Git 历史、`docs/history/` 与相关测试保存；最新候选和设备证据见 `.agents/HANDOFF.md`。

“限制”不等于缺陷：部分项目是 Windows Mobile 6 的资源约束下有意采用的有界设计。只有源码、测试和设备证据证明边界已解除后，才能删除对应条目。

## 1. 产品所有权与浏览器 JavaScript

### 当前边界

- `test_host.exe` 已经是公共 DLL 的消费者；next607 已将程序化 `HTMLElement.click()` 的
  disabled 抑制、typed click、submit/reset 事件顺序、submit 验证与取消策略迁入
  `positron_browser.dll`，next608 又将 native EDIT 的 beforeinput pending、native commit
  到 input、dirty tracking 和 blur/change 顺序迁入同一 DLL，next609 再将 native SELECT
  commit 后的 input→change 顺序与 bounded target-shape state 迁入同一 DLL，next610 又将
  native SELECT 的 focus→focusin / blur→focusout 成对顺序和 bounded focus state 迁入同一
  DLL，next611 再将单选下拉的 begin/candidate/confirm/cancel 事务状态迁入同一 DLL，next612
  又将 native SELECT 的 keydown/keyup dispatch、取消结果和 Enter/Arrow 元数据入口迁入同一
  DLL，next613 又将 native EDIT 的 compositionstart/update/end 顺序、有界 preedit 和
  UPDATE pending metadata 迁入同一 DLL。next618 在宿主 WM6 边界把完整 `GCS_RESULTSTR`
  一次性写入当前 native EDIT composition selection，避免部分 WinCE EDIT 默认过程只提交
  首个字符。仍由宿主持有 native SELECT 的 WM 控件默认动作、原生下拉窗口与视觉、Core
  selection mutation、取消回滚，以及 native EDIT 的 WM_IME/SIP、原生文本 mutation 和
  平台副作用胶水；必须逐项区分产品语义与宿主副作用，不能笼统搬迁。
- 产品级 HTML、CSS、DOM、表单和脚本语义应归入 `positron_browser`、`positron_core` 或 `positron_script`；窗口生命周期、原生控件、设备网络和测试编排仍由宿主持有。
- 浏览器 JavaScript 与独立脚本 API 共用 `positron_script` 中唯一的 Duktape 引擎。当前默认关闭，启用必须显式配置。
- 浏览器会话脚本 heap 上限为 624 KiB，独立脚本会话默认上限为 512 KiB。预算是当前设备实测基线，不代表任意页面都能装入。
- 定时器、promise job、网络回调和其他异步任务依赖宿主显式泵送，不具备现代浏览器常驻事件循环的全部语义。
- `PBrowser_ScriptSessionRegisterProgrammaticClickCallbacksEx()` 只迁移程序化表单激活的
  策略与顺序；target lookup、core validation、default action、native SELECT、系统 picker、
  窗口和导航副作用仍由宿主 callback 提供。`PBrowser_ScriptSessionRegisterNativeEditCallbacksEx()`
  只迁移 native EDIT 的有界输入事务策略；`PBrowser_ScriptSessionDispatchNativeEditComposition()`
  只迁移 composition phase/data 的顺序与 bounded preedit；WM EDIT/WM_IME 消息、文本 mutation、
  SIP/候选词窗口和 OEM 行为仍由宿主提供。next618 的 `GCS_RESULTSTR` 完整候选落地只覆盖
  已有 WM6 native EDIT 路径，不等于所有 OEM 输入法或视觉均兼容；next619 的
  `PBrowser_ScriptSessionDispatchNativeEditResult()` 只持有完整 result 的事件事务和 pending
  metadata，不拥有原生文本替换或 SIP 窗口。next614 还把显式/嵌套 label-control 关联和
  labelable 控件的静态 `labels` snapshot 放入 core/browser relation bridge；next615 又把
  disabled ancestor fieldset 的有效状态（含第一个 legend 豁免和嵌套 fieldset）接入产品 core 的
  验证、successful controls、控件信息和交互闸门，但不承诺 live labels、native invalid UI 或
  完整成功控件边缘规则。`PBrowser_ScriptSessionRegisterNativeSelectCallbacksEx()`
  只迁移 commit 后的 input→change 顺序、target-shape 校验、next610 的焦点族 dispatch、
  next611 的单选下拉事务闸门以及 next612 的 typed key dispatch/default-allowed policy；WM
  SELECT 控件真正的键盘默认动作、下拉窗口/视觉、Core selection mutation、原生取消回滚和
  OEM SIP/IME 行为仍由宿主提供，不把 OEM 行为误标为产品兼容性。
  next620 的 `PBrowser_ScriptSessionDispatchNativeFileSelection()` 只持有 picker 结果的
  BEGIN/COMMIT/CANCEL 事务、一次 `input(insertFromFile)` → `change` 顺序和 16-token
  bounded state；系统 picker、文件系统、路径写入、权限和视觉仍由宿主提供。
  next621 又由 `PBrowser_ScriptSessionDispatchNativeFilePicker()` 持有 programmatic
  picker 的单 pending/active request、重复点击合并、OPEN/CLOSE/CANCEL 和 reset；
  `PostMessage`、系统 picker、文件系统、路径写入、权限和视觉仍由宿主提供。
  next622 又由 `PBrowser_ScriptSessionDispatchAnchorClick()` 持有启用脚本时受信任
  物理锚点的 click 取消与 ASSIGN 导航接线；`PCore_LinkAt` 命中、网络、窗口和
  文档替换仍由宿主提供，程序化 anchor click 和 target/rel/window 行为尚未由此入口覆盖。
  next623 又由 `PBrowser_ScriptSessionDispatchNativeToggle()` 持有 checkbox/radio 直接激活
  的 click 取消、有限 token 事务和提交后的 input→change 顺序；Core checked-state mutation、
  WM 鼠标/键盘默认动作、label 转发、重绘和原生视觉仍由宿主提供。
  next624 又由 `PBrowser_ScriptSessionDispatchNativeButton()` 持有 submit/reset 原生按钮的
  CLICK/COMMIT/CANCEL、click 取消和 submit/reset 事件顺序；宿主在 click 后提供 Core 校验，
  仍负责默认提交/重置、导航、窗口、label 转发、重绘和原生视觉。程序化 click 与直接物理
  label 激活不由 next624 单独定义；next627 仅把 button label 转发接入该既有入口。
  next625 又扩展该入口覆盖 ordinary `<button type="button">` 的 CLICK/COMMIT：普通按钮不
  派发 submit/reset，宿主消费已接受的默认动作，因此物理点击不再误关闭页面；普通按钮的
  键盘焦点/激活现在由宿主以 TEST1074 的消息级契约覆盖，Enter/Space 事务不再落入关闭
  窗口 fallback。next627 又让启用脚本时 label 命中的 kind=7..9 button 复用同一
  CLICK/COMMIT 事务；label 命中、label 自身 click、Core 坐标和默认动作仍由宿主提供，
  disabled/stale target fail closed。next628 又让启用脚本时 label 命中的 kind=1/2
  checkbox/radio 复用 `PBrowser_ScriptSessionDispatchNativeToggle()` CLICK/COMMIT；Core
  checked/radio mutation 和重绘仍由宿主提供，browser layer 继续持有目标 click 取消及
  input→change 顺序。next629 又让启用脚本时 label 命中的 text/password/textarea/select/file
  先经过既有 browser click adapter；目标未取消且有效时宿主才执行 native EDIT/SELECT focus
  或系统 file picker。TEST1077 已覆盖 text/textarea/select 的真实 native 子窗口事件、焦点、
  取消和 disabled 闸门，但真实 label 触摸坐标、文件 picker 模态对话框、路径写入、SIP/IME、
  OEM 视觉和其他未列入 fixture 的 labelable 边缘仍需单独人工或独立门验收。
  next630 又把脚本直接 `HTMLElement.click()` 的 text/password/textarea/select target-kind
  接入同一 browser-owned typed click 入口；browser layer 负责 disabled 抑制、click 传播和
  取消，宿主按 DOM id 提供身份派发并执行 `PBROWSER_SCRIPT_CLICK_DEFAULT_FOCUS` 的 native
  焦点副作用。TEST1078 已在真实 render window 上覆盖 click/focus/focusin、select 取消和
  disabled 静默；select 下拉弹窗、文件 picker、真实触摸、SIP/IME、焦点视觉与 OEM 行为仍
  由宿主负责，不能从该自动门扩张为通用浏览器保证。
  next631 又以独立的 programmatic-anchor callback 补齐带 href 锚点的脚本激活：
  `positron_browser.dll` 复用 cancelable click 与 ASSIGN navigation，`positron_core.dll` 的
  `PCore_LinkInfoById()` 按 DOM id 提供已布局几何和非空 UTF-8 href；未知/无 href 元素回到
  generic click。网络请求、窗口替换、文档生命周期、target/rel/window、fragment 视觉和
  真实触摸仍由宿主/后续范围负责。TEST1079 已覆盖接受导航、preventDefault、容量/缺失和无
  href 边界，TEST1070 继续覆盖导航适配器拒绝；这不等于完整 anchor activation 或现代
  浏览器导航策略。
  next636 在此路径上增加 `PCore_LinkAtEx()` 与 `PCore_LinkInfoByIdEx()`，按有界 UTF-8
  缓冲返回 href、target、rel，并由 `PBrowser_ScriptSessionDispatchAnchorClickEx()` 将
  元数据随 programmatic/物理受信任点击传给既有导航 adapter；`HTMLElement.rel` 也已反射。
  缺失属性为空，容量不足和非法元数据 fail closed。该批只完成产品到宿主的元数据契约，
  不创建新窗口，也不拥有 `_blank`/named target 的窗口复用、跨窗口 history 或关闭策略；
  URL 解析、网络、文档替换、窗口生命周期、真实触摸和视觉仍由宿主或后续范围负责。
  TEST1084 覆盖 id/坐标查询、脚本反射、导航传递和取消，1079–1083 的 fragment/历史回归
  继续保留。
  next637 在 `PBrowserScriptNavigationInfo` 追加 `target_kind`，由 browser layer 统一分类
  空/空白、`_self`、`_parent`、`_top`、`_blank` 和 named target；当前单窗口 `test_host`
  对前四者继续加载当前文档，对后两者在 admission 与消息边界 fail closed，不把缺少窗口
  管理器的请求静默降级为当前页替换。真实多窗口创建、窗口复用/生命周期、跨窗口 history、
  opener/noopener 安全策略和视觉仍未实现；target_kind 只是可复用的产品到宿主策略输入。
  TEST1085 覆盖该分类和拒绝边界。
  next638 又在同一导航 callback 上增加了 `PBROWSER_SCRIPT_NAVIGATION_OPEN`：bootstrap 的
  `window.open()` 只有在宿主接受显式 `_self`/`_parent`/`_top` 时才返回当前 bounded global，
  默认、`_blank`、named、空 URL 或 callback 注销时返回 null。features 不产生窗口特性，
  真实窗口创建、复用、关闭、opener/noopener、跨窗口 history、网络和视觉仍未实现；这
  只是安全的当前-context 导航语义，不等于多窗口兼容。TEST1086 覆盖 callback metadata、
  宿主 admission 和注销后的 fail-closed。
  next639 又追加了 `PBrowserScriptNavigationInfo.context_name`：OPEN callback 会收到当前
  有界 `window.name` 快照，`test_host` 只在 named target 与该名称精确匹配时复用现有单窗口，
  并在同一 context 的新 script session 中恢复名称。未知 named、`_blank`、空名称和没有
  匹配 context 的请求仍 fail closed；真实多窗口、window manager、opener/noopener、close、
  跨窗口 history、持久化生命周期和视觉仍未实现。名称恢复不代表第二个 JS global，也不把
  单窗口实现扩张为多窗口保证。TEST1087 覆盖该 bounded 契约。
  next640 将同一单窗口 admission 扩展到普通 anchor 的非 OPEN 导航：宿主读取活动 session 的
  `window.name`，只有 named target 与其精确匹配时才允许 ASSIGN/REPLACE/FRAGMENT 等当前 context
  路由，并在窗口消息边界再次校验；未知或空名称仍 fail closed。browser DLL 的 `context_name`
  仍是 OPEN-only 兼容尾字段，真实多窗口、窗口 manager、opener/noopener、跨窗口 history 和
  视觉仍未实现。TEST1088 覆盖该消费者边界。
  next641 又在 browser bootstrap 中提供了 `a`/`area`/`link`/`form` 的稳定 `relList` wrapper，
  支持 bounded DOMTokenList 读取、ASCII 大小写不敏感去重、mutation、value 反射和迭代；
  `rel` 属性桥仍是唯一数据源，其他元素返回 null。next642 又增加了保守的 `supports()`：
  只有 `<link>` 的 `stylesheet` 返回 true，其他关系词和 `a`/`area`/`form` 返回 false；
  非 rel 元素仍为 null。该能力不实现完整 link-type processing、noopener/opener 窗口安全或
  live DOM mutation；关系词不会自动改变宿主窗口策略。TEST1090 与 1080–1089、999 的
  Debug 门已通过。
  next643 只把 `<style media>` 与 `<link rel="stylesheet" media>` 的 UTF-8 条件用于当前
  Core CSS selection；同文档重新样式时复用已缓存的外部字节。next645 又让 Core 在收集
  外部 stylesheet 时按 HTML 属性存在性跳过 `<link disabled>`，因此被禁用的 link 不 fetch、
  不解析也不参与 CSS selection。next646 又按 ASCII whitespace token、大小写不敏感地
  识别 `stylesheet` rel，并对含 `alternate` 的 link 保持 fail closed。仍不实现脚本侧动态
  `MediaQueryList` 事件、完整 link 下载策略、`type` 等其他 link processing 或 media 相关
  DOM mutation。next647 又在 Core UA stylesheet 中加入 `[hidden] { display:none; }`，使存在
  `hidden` 属性的元素不生成布局盒；这只覆盖默认呈现，不实现脚本侧 mutation observer、完整
  CSS cascade 或隐藏状态相关的辅助技术语义。next648 又为 `details`/`dialog`/`summary` 提供
  静态 open-state 默认布局：关闭 details 隐藏非 summary 子项，关闭 dialog 不生成布局盒。
  next651 又为首个直接 summary 提供 Core 的 id/坐标查询与 open toggle，并让 browser 的
  `HTMLElement.click()` typed adapter 传播可取消 click、执行 Core default 和安排活动页重排；
  next652 再将合法 summary 纳入 Core focus interaction，由宿主通过既有 key/click bridge
  接入 Enter/Space 激活，并在有界几何快照失配时 fail closed。toggle 仍是 DOM-only，调用者
  必须显式 style/layout。尚不实现 Tab 焦点遍历、键盘焦点滚动、完整 disclosure/辅助技术
  事件、dialog modal focus、backdrop 或 dialog 生命周期。
  next649 又把 HTML `pre[wrap]` 接入 Core UA stylesheet 的 `white-space: pre-wrap`，窄视口
  代码块会换行并增加布局高度；这不等于完整 CSS whitespace、tab 度量或所有字体下的像素一致性。
  next650 又把递归资源收集中的 stylesheet reference/URL 自动数组改为单次 style transaction
  共享的有界 heap scratch；真实 fetch/cache/parse/attach/free 路径不变，TEST13 的 IANA 三跳和
  TEST1098 深层 DOM 外链 CSS/cache 契约已在 WM6 通过。资源遍历本身仍是递归实现；极端或恶意
  深度的 DOM 尚无独立硬深度上限，不能把这次修复外推为任意深度文档都安全。
  TEST1091、TEST1093、TEST1094、TEST1095、TEST1096、TEST1097、TEST1098、TEST1099 与 TEST999 的 Debug 门已通过。next644 又在 browser bootstrap 中提供
  `<link>` 与 `<style>` 的 bounded `media` 属性反射：缺失值为空串，setter 经既有
  UTF-8 attribute bridge 写回并支持 `removeAttribute()` 恢复，其他元素返回 `undefined`
  且 setter 不改变 raw 属性。该反射不触发脚本侧 MediaQueryList 事件、自动重排或 link
  下载策略；TEST1092 与 1090/1091/1093/1094/999 的 Debug 门已通过。
   next632 又把 fragment-only href（以 `#` 开头）分类为同页
  `PBROWSER_SCRIPT_NAVIGATION_FRAGMENT`：宿主绑定当前 URL，调用
  Core 片段查询取得已解码 token 的目标几何并移动自己的 viewport；unknown token 保持当前
  位置且不发起网络请求。next635 的 `PCore_FragmentInfoByToken()` 按 id 优先并兼容旧式
  `<a name>`，宿主只做有界 `%HH` 解码，`+` 保持字面，非法 escape/NUL fail closed。
  TEST1080 仍证明分类和窄接线，TEST1083 证明 token 解析及失败不变式。
  next633 已让同文档 history `back()`/`forward()`/`go()` 在 traversal 成功后恢复该目标
  几何；未知目标保持当前位置，跨文档 traversal 仍走既有网络/窗口路径。该恢复只覆盖当前
  已布局文档和 token 目标，不等于持久滚动位置、跨文档恢复或完整浏览器 history 策略；
  TEST1081 只证明宿主窄接线。

### 解除或推进条件

- 每批迁移一个完整用户行为，公共 DLL 持有语义和状态，`test_host` 只调用稳定 C ABI。
- 用目标设备上的峰值内存、失败行为和相关回归证明预算变化；不能通过移除断言掩盖资源不足。
- JavaScript 默认值只有在安全性、内存和页面兼容基线重新评估后才能改变。

## 2. DOM 与 Web API

### 当前边界

- DOM 主要是浏览器会话内的有界、静态快照，不是随所有修改实时更新的完整 live DOM。
- 尚不支持通用节点创建和变更、完整 Shadow DOM、完整 namespace/XML，以及现代浏览器的全部 selector 行为。
- 若接口返回集合、列表或映射，通常具有固定容量、快照寿命或有限索引范围；例如 NamedNodeMap 的索引槽当前只覆盖 0–7。
- `form.elements`、`RadioNodeList` 和 `labels` 都是有界快照；没有完整的 live
  `HTMLFormControlsCollection`/labels、labelable 控件全部类型或同名控件全部边缘规则。next615
  的 fieldset disabled 只覆盖产品 core 已接入的验证、successful-control、控件信息和交互路径，
  不等于完整 live DOM 或所有宿主 native 副作用。当前 label 关联只覆盖有 ID 的 input（排除
  hidden）、select、textarea、button，显式 `for` 和嵌套控件。
- Request、Response、Headers 等对象是内存模型；创建对象本身不会发起网络。
- storage 和 cookie 主要是会话内存模型，没有持久化、跨会话隔离、完整 quota、安全属性和浏览器级策略。
- API 存在不代表完整标准兼容。参数强制转换、异常类型、属性描述符、可枚举性、原型链和跨 realm 行为仍可能不同。

### 解除或推进条件

- 以真实目标页面或完整用户流程驱动能力选择，避免仅为了增加 API 数量添加互不关联的薄封装。
- 每个公共能力同时覆盖成功、边界、失败、容量和会话销毁后的行为。
- 只有同时验证对象语义、生命周期和真实页面消费，才可宣称对应标准子集已支持。

## 3. URL、导航与历史

### 当前边界

- `positron_http.dll` 的 `PHttp_ResolveReference` 为主文档导航、CSS/图片子资源和 HTTP GET
  重定向提供统一的有界 HTTP(S) 解析：目录相对、`.`/`..`、query-only、network-path、
  绝对 URL 会按 WinINet 规则合并，产品 API 随后校验 scheme/authority/端口/路径并去除
  fragment。它仍不是完整 WHATWG URL Standard；userinfo、IPv6、非法端口、非 HTTP(S)
  scheme、无 origin 的普通相对引用和输出容量不足会 fail closed。`test_host` 只消费该
  API，不再拥有同一套业务解析副本。
- 没有完整页面缓存、持久历史、跨进程恢复、表单状态恢复和 POST 重提交模型。next634 只在
  当前 `test_host` 进程内以有界宿主镜像保存跨文档 history 条目的 scroll offset，并在目标
  文档布局完成后恢复/按 viewport 上限裁剪；该状态不进入 `positron_browser.dll`，进程重启
  后不会保留。
- 导航是否实际发起请求，以及窗口如何替换，仍需要宿主网络和窗口层参与。
- 产品 URL/history 状态与宿主实际 I/O 仍是分层边界；HTTP resolver 的重定向链上限、旧设备
  编码、特殊 scheme、同源策略和跨文档 history 的边缘行为覆盖有限。

### 解除或推进条件

- 用固定页面语料覆盖绝对/相对 URL、跳转、失败、重定向和 history 前进后退。
- 继续把纯 URL/历史状态语义留在产品 DLL，把实际 I/O 和窗口副作用留在宿主边界；为重定向
  链、失败响应和 history 前进后退补齐固定页面语料。
- 对不支持的 scheme 和恢复语义返回明确、稳定的失败，而不是静默近似成功。

## 4. 布局与视觉兼容

### 当前边界

- 现有布局覆盖项目已验证的块、行内、盒模型及部分定位/弹性行为，但不具备完整 Grid、sticky、复杂包含块、表格和列表布局。
- float 实验已撤回；在没有完整算法、内存预算和回归计划之前不得恢复为默认路线。历史原因见 `.agents/FAILED_EXPERIMENTS.md`。
- CSS 解析和级联仍是有界子集，复杂选择器、伪元素、字体度量和浏览器默认样式可能与桌面浏览器不同。
- 自动几何断言不能发现所有裁剪、字体替换、DPI、横竖屏、滚动与重绘问题。
- 已验页面只能证明特定设备、方向、字体和内容组合，不能外推为任意网页兼容。

### 解除或推进条件

- 每项布局能力需要最小算法测试、真实页面回归、内存上限和至少一个目标设备视觉证据。
- 触及 viewport、滚动、字体、原生控件叠放或旋转时安排集中人工验收。
- 崩溃、内容不可达、严重裁剪或主交互阻塞必须立即人工复核，不能累计到以后。

## 5. 表单、输入与设备交互

### 当前边界

- 基础表单值、选择、单选/复选、提交和部分约束验证已覆盖，但不是完整 HTML 表单标准。
- 任意 OEM IME/SIP、候选词整词提交、composition/preedit、硬键盘组合和焦点切换仍可能有设备差异。
- 原生 invalid UI、完整 type/range/step 规则、output/meter/progress 等未纳入的 labelable 类型
  与复杂成功控件集合仍不完整。next615 已覆盖 fieldset disabled 对产品 core 验证、successful
  controls、控件信息和交互闸门的有效状态；native 窗口样式、invalid UI、SIP/IME、picker 和
  其他宿主副作用仍不由该语义自动完成，当前 label/control 也只保证上述有界关系。
- 文件输入依赖 WM 文件选择器；next620/621 已把选择结果事务和 programmatic request
  仲裁放入 browser DLL，但自动化仍无法替代真实选择、取消、重入、权限及可见性体验。
- 旋转、软键盘弹出、原生控件与 HTML 视图重排存在需要人工观察的风险。

### 解除或推进条件

- 自动化断言负责值、事件顺序、取消/重入、状态保持和错误码。
- SIP、候选词、真实触摸、文件选择器和旋转在形成一批风险后集中人工验收。
- 如果输入阻塞、内容消失、事件重复或状态损坏，必须立即停止晋升并 debug。

## 6. 图像与字体

### 当前边界

- 图像解码格式、尺寸、内存和错误恢复受目标设备资源限制；超大图、损坏流和少见格式并非完整覆盖。
- 字体依赖设备已安装字体、GDI 度量和 OEM 渲染行为；fallback、复杂文字 shaping 和桌面浏览器像素一致性不保证。
- 部分图像创建、解码或布局提交仍可能占用 UI 路径，长页面或大资源会造成卡顿。

### 解除或推进条件

- 为每个新增格式或渲染路径提供尺寸/内存边界、损坏输入和释放测试。
- 视觉声明必须绑定具体设备、DPI、方向、字体和截图证据。
- 性能优化不能改变公共 ABI、所有权或失败语义。

## 7. 网络与安全

### 当前边界

- 网络能力受 WMDC/设备连接、OEM 网络栈、代理、DNS、证书存储和设备时钟共同影响。
- Mbed TLS 2.16.12 已停止维护；它不能作为现代 TLS 与证书生态的长期安全基线。
- `positron_tls` ABI v2 已支持持久 ECDSA P-256 peer 身份、双向证书、DER SHA-256 pin 和
  TLS server，但仍只有 TLS 1.2/IPv4；没有 TLS 1.3、IPv6、证书撤销或现代硬件密钥库。
- peer 私钥是未加密 PEM，保密性取决于应用选择的设备目录和系统/OEM 文件权限；删除或
  复制文件会改变或复制设备身份，上层必须拥有配对、迁移和重置策略。
- peer connect 的期限覆盖 TCP connect 与握手，但 WinCE 5.2 的同步 DNS 解析本身不能可靠
  取消；每个 listener 同时只允许一个 active accept，同一 connection 的并发顺序由调用方
  串行化。
- NULL/空 peer pin 只适合 discovery/TOFU，不能作为已认证会话；普通 HTTPS 仍必须使用
  CA/hostname 验证，不能把 peer 自签名放行逻辑复用到互联网信任模型。
- 已从官方 LocalSend/rustls 源码确认 TLS 1.2、P-256 ECDSA 与 AES-128-GCM 的配置交集，但
  尚未在本仓库运行 LocalSend/rustls↔WM6 两个方向的跨栈互操作；当前设备证据是 Positron
  loopback，不能替代消费者集成门。
- HTTP/TLS、重定向、取消、压缩、缓存、cookie 和认证仍只覆盖有限子集。
- 自动测试中的内存 Request/Response 模型不等于真实网络端到端成功。
- 当前项目没有完整现代浏览器沙箱、同源策略、CSP 和权限模型。

### 解除或推进条件

- 真实网络能力必须分别测试成功、DNS/连接失败、超时、取消、证书错误和资源释放。
- 安全相关升级需要明确第三方来源、许可证、补丁范围、工具链兼容和目标设备验证。
- 不得把关闭证书验证或接受所有错误作为默认兼容方案。

## 8. 性能、线程与内存

### 当前边界

- WM6 地址空间和堆较小；DOM、脚本、图片和布局容量均需要显式上限。
- HTML/CSS 解析、样式、布局及部分图像工作仍会进入 UI 提交路径；没有完整后台 DOM/layout 管线。
- 缓存、增量布局、页面冻结恢复和长期后台运行尚未形成完整产品策略。
- 异步对象依赖调用者正确泵送和销毁；错误线程或遗漏泵送可能表现为停滞而非立即错误。

### 解除或推进条件

- 任何预算提高都必须有设备峰值数据、失败边界和回归证据。
- 优化应先建立可重复测量，再改变调度、缓存或生命周期。
- 线程所有权、回调时机和销毁顺序必须进入公共契约及自动测试。

## 9. 验证与发布置信度

### 当前边界

- 最近一次全范围自动设备基线是 next255；后续能力有针对性门和相关回归门，但不能称为最新 TEST1–1100 全覆盖。
- 自动日志与几何断言不能替代视觉、真实触摸、SIP、旋转、文件选择器和失败网络的人工判断。
- `tmp/` 中设备日志和截图只在本机存在，不进入 Git；丢失本机证据后只能依赖提交中的结论和可重跑测试。
- WMDC/RAPI 自动化默认假设已有且独占的设备连接；连接和配对本身通常仍是 GUI 操作。
- 旧 WMDC 主机的十个 32/64 位 RAPI COM 路径在既有修复后曾再次恢复成安装时旧值；gate 已做
  严格预检并只为精确旧值请求 UAC 修复，但当前证据尚不能把写回动作归因于换设备本身。
- 单次通过不能消除时序、设备型号、OEM 输入法或网络环境差异。

### 解除或推进条件

- 每批至少有针对性自动设备门和相关回归门；风险累计到合理数量后集中人工验收。
- 在重要里程碑、重大内存/调度变化或相关风险积累后运行新的全范围设备基线。
- 候选只有在日志唯一 PASS、无 error/fail、路由正确且结果文件完整后才能晋升。
- 发布级结论必须列出设备、构建、测试范围、人工门和仍存在的限制。

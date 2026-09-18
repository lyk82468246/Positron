# 当前交接

本文件只描述当前产品基线、最近有效证据、未决风险和唯一下一步。逐批实现过程由 Git 历史保存，历史事故见 `docs/history/`，未来方向见 `ROADMAP.md`。

## 项目使命

Positron 为 Windows Mobile 6 / Windows CE 5.2 ARMV4I 提供模块化 TLS、JSON、HTTP、图像、脚本、渲染与浏览器会话 DLL。公共边界保持 C ABI、UTF-8、opaque handle 和显式所有权；`test_host.exe` 只是回归宿主与示例消费者。

## 当前 Git 与工作区

工作区仍在 `main`。当前设备门基础设施使用唯一 `.part-*` 文件、同卷原子改名、16 KiB RAPI 传输块和有界的超时后会话重开；某些 DMA 镜像在 512 KiB 边界启动 32 KiB 写入时会复现 `0x80072746`，已由 16 KiB 传输并在更换后的仿真器上验证。日志复制期间的瞬时 `CeReadFile` 失败仍只视为可重试快照。产品侧的 Duktape Dragon4 数值转换上下文移出原生线程栈，参考宿主也在同步嵌套 `WM_SIZE` 期间暂缓 Browser 脚本通知和 native child 重建，并在最外层完成布局后按顺序发布 scroll/resize。

- next790–next817 已完成有界 CharacterData/HTML parser mutation、text-only fragment、
  Ex13–Ex15 replacement、detached Text/Element/Comment 生命周期、属性 facade、body.text、
  cookie、document.write 和 Core-backed title；逐批证据由 Git 历史保存，稳定合同见
  [`docs/TESTING.md`](../docs/TESTING.md)。设备门的外置优先、双空间预检、日志回收和清理
  规则保持现行基线。
- next817 的 title 纵切已在更换后的仿真器上通过；Core 只处理首个直接 `<head><title>`，
  Browser 负责 wrapper 与字符串化边界，不占用 Element id。
- bounded DocumentFragment 已完成 Element/Text staging、clone、查询、Fragment 组合和
  detached Element/Fragment 的有界 normalize、HTML、Attr/NamedNodeMap、textContent 与
  sibling 关系；这些路径复用既有 Core parser/callback，不新增 Core fragment ABI。
- Fragment 的 clone、getElementById、selector/query 和消费路径保持源/副本 owner、wrapper、
  collection identity 隔离；重复 id、嵌套 Element、过长 selector 或不支持的结构在消费前
  fail closed。
- Fragment-to-Fragment 组合、detached Element 的 `normalize`/replaceChildren/replaceChild、
  Attr/NamedNodeMap、HTML serialization 和 Fragment `textContent` 原子替换均已通过对应
  自动门；超限、重复 id、结构不支持和自引用在 mutation 前 fail closed。
- Fragment-owned Element/Text 的 sibling/element-sibling 关系、Browser-created Text/Comment
  的 CharacterData aggregate/split/replace 和 existing-source relative mutation 已覆盖；
  live、detached、物化后和移除后的 owner/snapshot 均保持有界一致性。
- CDATASection 的创建、物化、CharacterData、wholeText、splitText 和 replaceWholeText
  已由 TEST1284–1285 覆盖；外置卡设备门、日志回收、空间预检、清理和 crash check 均通过。
- next846–next847 补齐 Browser-owned `DocumentFragment` 的 CharacterData 根 staging：最多四个
  detached Text/Comment/CDATA 根保持关系、clone/normalize、data mutation 和消费后的 wrapper
  identity；Comment/CDATA 复用 Core creation callback 物化。next847 统一 Fragment
  `textContent` 为排除 Comment。next848 将 Core、live Browser Element 与 detached Fragment
  的 `Node.normalize()` 统一为 Text/CDATA 连续运行：删除空节点、合并到首个非空 wrapper，
  保留 Comment 边界并同步 created CharacterData 数据；TEST1287 与 `1287,999` 门通过，证据
  为 `tmp/device-runs/20260917-204745-next848`，外置卡 2/2 PASS，日志、双空间预检、清理和
  `crash_check` PASS，dump=0。
- next849 扩展 detached Element 的直接 staging，从仅 Text 扩展为 Text/Comment/CDATA：
  `normalize()`、clone、replace、`textContent`、移除/重插入和带唯一 id 的直接 Core
  物化共享同一有界 wrapper/owner 合同；嵌套 Element、任意 detached Node graph 和
  Fragment 内的嵌套 CharacterData 子树仍 fail closed。TEST1288 与 `1287-1288,999`
  门通过，证据见下方。
- next850 为已物化的 Browser-created Element wrapper 补齐有界 primitive
  `before()`/`after()`/`replaceWith()`：同级文本插入复用现有 Browser/Core mutation
  callback，目标 wrapper 在插入前后保持 identity，替换成功后回到 detached；未物化目标
  保持 inert，Element、Fragment、CharacterData 与其他对象参数在 mutation 前拒绝。TEST1289
  与 `1288-1289,999` 门通过，证据见下方。
- next851 补齐已物化 Browser-created Element 的 HTML mutation coherence：`innerHTML`
  setter 在 Core parser 成功后原地刷新 created wrapper 的 child snapshot 与旧 child
  detached 状态；`outerHTML` setter 通过 public alias 调用既有 Core replacement，成功后
  清理 alias 映射并让 staged wrapper 可再次设置 id、插入和使用。TEST1290 与
  `1289-1290,999` 门通过，证据见下方。
- 设备门复用 WMDC RAPI；超时进程需在设备端结束，`tmp/` 证据不入库。
- `TEST_MAX_NUMBER` 已为 1290。tracked `test_host/test_host.ini` 仍是窄 smoke：
  `auto=1`、`javascript=0`、选择 `13,20,27,56,58,62,64-67,73,75,1217-1290,999`。
- 设备门继续假定用户已在 WMDC/Device Emulator GUI 手动连接恰好一个目标；RAPI 只复用
  当前会话，不连接、选择、cradle、重置或强杀设备。

## 近期已完成能力摘要

最近的产品纵切已覆盖导航/资源、history/viewport、生命周期、脚本、焦点、滚动/几何、
表单、selector、图像和有界 DOM/CharacterData mutation。HTML parser mutation、
text-only 与 bounded Element/Text `DocumentFragment`、detached Text/Comment/Element、
属性 facade、cookie、document.write 和 document.title 的合同与预算集中在
[`docs/TESTING.md`](../docs/TESTING.md)；宿主仍只拥有平台接线、调度、fixture 和断言。

Browser script session 由宿主显式推进，不复制 URL、DOM、Event、表单、图像或生命周期
语义。设备门的外置优先/内置回退、双空间预检、日志回收、超时恢复和完成后清理集中在
`scripts\device_gate.ps1`；`tmp/` 只保存本地证据。

## 当前中期里程碑

在保持 VS2008/WM6 约束的前提下，把已经形成的 Core、Browser 和平台宿主能力整合为可由真实页面驱动的有界网页运行时。重点是完成用户可感知的纵向能力、把通用语义留在公共 DLL，并以小型真实页面/交互语料库防止只增加孤立 API。

## 当前短期目标

- 当前基线含表单、selector、滚动/几何、生命周期、焦点、图片和有界 DOM；
  next790–next815 的 parser、fragment、replaceChildren、detached Text/Element/Comment、
  CharacterData offset、clone/style/reflected-attribute facade、body.text、session cookie
  和 document.write 纵切已有自动合同。稳定边界见
  [`docs/TESTING.md`](../docs/TESTING.md) 与 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。
- next817–next837 的 document.title、DocumentFragment staging/组合、Node 关系、集合快照、
  detached normalize/replaceChildren/replaceChild、Attr/NamedNodeMap facade、textContent
  同步、text-only HTML facade 与 Fragment textContent 原子替换均已完成构建、断言和设备门；
  next838 又补齐 Fragment-owned detached Element sibling/element-sibling 关系，next839 补齐
  Browser-owned Text/Comment 的 element-sibling 关系，next840 补齐 Browser-created Text 的
  CharacterData aggregate/split/replace 视图，next841 补齐 relative primitive mutation 并完成
  相邻门，next842 补齐 Browser-created Comment 的 relative primitive mutation，next843 补齐
  Browser-created Text/Comment 的单一 existing CharacterData relative mutation，next844 又
  补齐 Browser-created CDATASection 的 bounded 创建、物化和 CharacterData 生命周期，next845
  补齐 CDATASection 的 Text 结构合同，next846 补齐 Fragment 的 Comment/CDATA 根 staging 与
  Core 物化，next847 修正 Fragment `textContent` 的 Comment 排除规则，next848 统一 Core、
  live Element 与 Fragment 的 Text/CDATA normalize 并同步 created wrapper 数据，next849 又
  补齐 detached Element 的直接 Text/Comment/CDATA staging，next850 又补齐已物化
  Browser-created Element 的 primitive relative mutation；下一步继续从可复现缺口进入公共 DLL。
- `test_host` 只保留 callback 接线、平台调度、fixture 和断言；可复用的 URL、DOM、Event、
  表单、图像和生命周期语义必须继续位于对应公共 DLL。
- fixed-buffer 数值转换、原子部署、RAPI 日志恢复和最近的 DOM 纵切均已通过正式设备门；
  历史 next 细节由 Git 与 `docs/history/` 保存。不得把测试宿主扩展当作产品语义实现。

## 已验证产品事实

### 公共边界

- 顶层公共 DLL 为 TLS、JSON、HTTP、image、script、core 和 browser。
- NetSurf/libcss/libdom/hubbub、Expat、libsvgtiny、libjpeg 等移植工程是内部实现依赖。
- 独立脚本和浏览器脚本共用 Duktape；浏览器 JavaScript tracked 默认仍为关闭。
- 通用 URL、history、DOM、Event、表单、图像和脚本 session 语义位于对应公共 DLL；宿主保留 WM 窗口、消息、控件、SIP/IME、picker、导航调度和资源 I/O。

### 当前网页能力

- HTML/CSS/DOM、整树 style、NetSurf layout/redraw、GDI 绘制与资源缓存已形成正式 Core 路径。
- 常用 block/inline/flex/table、图片/SVG、背景、列表、有限定位、表单控件、验证、提交、reset 与 FormData successful-control snapshot（含可选 submitter、formdata 事件）已有设备回归；这不代表完整 CSS/HTML 或完整 Web API。
- Core 的有界 image-map 命中已与链接、area 几何、active/hover 和坐标事件 target 统一：
  已布局 `<img usemap>` 最多解析 64 个 linked `<area>` 和 64 个坐标，支持
  default/rect/circle/poly，并把自然坐标缩放到渲染尺寸；Browser 只接收 Core/宿主的
  click 事务，不复制 map 解析。
- `<img srcset>` 的 Core 选择最多接受 16 个、每个 URL 最多 2047 字节的同类候选：正
  密度 `x` 按 viewport DPI 选择，正宽度 `w` 按 `sizes` 解析出的源尺寸与 DPI 选择；
  `sizes` 仅支持 px/vw/vh 及单一 `(min-width|max-width: <length>)` 条件，缺失或不支持
  时按 100vw。图片发现、缓存、retained decode、布局自然尺寸、complete 和 Browser
  `currentSrc` 共用该结果；混合/畸形候选安全回退。绝对 URL、CORS/referrer 和完整
  loading 策略仍不支持。
- `textContent` 与非编辑元素的 `innerText` setter 通过 Browser 的既有 text callback
  调用 Core；空元素的 getter 返回成功的零字节字符串。成功 setter 后 Core 丢弃 retained
  layout，Browser 刷新目标的
  `children`/`childNodes`/query snapshot，并让旧的无 id 文本 wrapper 保留数据但变为
  detached。contenteditable 的 `innerText` 复用同一失效规则；Text 的
  `Text`/`Comment`/`CDATA` 的 `nodeValue`/`data`/`textContent` 与四个 CharacterData
  mutator 保持 child list 和连接中 wrapper 身份，非法/失效目标 fail closed。`Text`/`CDATASection.splitText()`
  通过 Ex3/`PCore_NodeSplitTextChildById` 对 direct CharacterData child 插入紧邻 Text sibling，保留
  原 wrapper、旧 NodeList snapshot，并对 UTF-16→UTF-8 不可表示边界 fail closed；关系 50 的
  `wholeText` 由 Core/libdom 拼接逻辑相邻 Text/CDATA，非文本边界和无效 child fail closed，
  detached wrapper 保留最近一次数据快照。Ex4/`PCore_NodeReplaceWholeTextChildById` 将一个
  有界相邻 Text/CDATA 段替换为目标 wrapper，目标移动到段首，其他 CharacterData wrapper
  变为 detached，成功后使 retained layout 失效。
  `Node.normalize()` 通过 Ex5/`PCore_NodeNormalizeById` 删除空 Text/CDATA，并将每段连续
  Text/CDATA 合并到第一个非空节点；Browser 对带 id 的后代 wrapper 按受控顺序递归并保持
  首个非空 wrapper 与旧 snapshot。write Ex6 的 `Element.append()`/`prepend()` 按零至四值为带 id 元素
  创建 Text child；mutation Ex6 另按未过滤位置移动 existing element。Ex2 的 `Text.remove()`
  与 `Element.removeChild(Text)` 删除连接中的
  direct Text child，Ex3 再为 Comment/CDATA 提供相同的 `remove()`/`removeChild()`
  路径。各路径都保留 detached wrapper、刷新父级 snapshot，并由宿主在成功后重排；
  Browser 现在另支持最多四个 primitive Text 的 text-only `DocumentFragment` staging，及
  最多四个 detached Element/Text/Comment/CDATA 根的 bounded staging；后者可由指定的 Element mutation
  一次性 parser-backed 消费并保留 staged wrapper identity，或在 Fragment 自身上与另一
  Fragment 做有界组合。Fragment-owned detached Element 的 sibling/element-sibling getter
  按当前 staging 顺序实时读取；未归属的 detached Element 不伪造关系。通用节点/嵌套
  fragment、超出有界元素约束的 reparent、其他删除、MutationObserver 与完整 live collection
  仍未实现。detached Element/Fragment 的 `normalize()` 仅限最多 64 个 direct CharacterData
  （Text/Comment/CDATA）或四个 Fragment 根；嵌套 Element、溢出和不支持节点在 mutation
  前 fail closed。Detached Element 的 replace/clone/textContent 也只接受同一组 direct
  CharacterData。
- Browser 的 `Node.cloneNode(deep)` 返回 Browser-owned detached snapshot：浅/深克隆保留
  有界 element 属性、子节点顺序、parent links 和独立数据，超限或不支持类型 fail closed；
  它不改变 Core 文档、retained layout 或事件 listener。
- `Element.innerHTML` setter 通过 Browser write Ex8 调用 Core 的
  `PCore_NodeSetInnerHTMLById`。Core 在同一 document 中用 UTF-8 fragment parser 预检节点、
  深度、direct-child 数量和 id 冲突后再替换子树；失败不改变原子树，成功保留目标身份并
  使 retained layout 失效。Browser 只在其 256 节点/64 层 wrapper reconciliation 预算内
  让旧 wrapper 变为 detached，再刷新目标的 `children`/`childNodes`/query snapshot；
  同一 parser 边界的 Ex9 `insertAdjacentHTML()` 在四个位置插入片段并刷新受影响
  target/parent snapshot；Ex10 `outerHTML` setter 则以单一 Element 根替换目标或以空字符串
  移除目标，并刷新父级/id cache；三条 HTML 路径都不是通用 `DocumentFragment`、mutation
  event 或资源执行 API。Browser 的 fragment staging 另有 text-only 与 bounded Element/Text
  子集，但不新增 Core fragment ABI。
- Browser 层还提供由宿主显式驱动的 viewport resize 合同：`PBrowser_ScriptSessionNotifyResize` 更新 CSS viewport/DPR 和动态 `screen` 方向，值变化时同步派发一次 window `resize`；同一 session 的 `screen.orientation` 对象保持身份稳定，方向翻转时在媒体列表刷新后先派发一次可信 `change`，再进入 visual/window `resize`；调用不负责 Core relayout 或 frame scheduling。
- 同一 Browser session 还提供布局视口对应的 `visualViewport`：`width`/`height` 与 CSS viewport 同步，`pageLeft`/`pageTop` 与 page scroll 同步，`scale` 为 1、offset 为 0；有效 resize/scroll 先派发 visual viewport 事件，再派发 window 事件，并对重复快照去重。TEST1133 覆盖该合同。
- Browser history entry 同时拥有非负的 `(scroll_x, scroll_y)` viewport snapshot；新 document entry 和同 URL 新 document 从零开始，`replaceState`/traversal 保留目标值，`pushState` 新 entry 从零开始，history 裁剪会同步搬移 snapshot。Browser 不访问窗口、不知道 Core 的页面 extent；宿主读取 `PCore_DocumentWidth/Height` 后保存/读取并对两个轴 clamp/apply。
- Browser script session 的 `PBrowser_ScriptSessionGetScrollRestoration` 暴露脚本的 `auto`/`manual` 策略。宿主在非 fragment history traversal 前只对 `AUTO` 自动读取并应用 entry snapshot；`MANUAL` 保留当前 viewport，查询失败按默认 `AUTO` 处理。fragment reveal 与显式脚本滚动不受该自动恢复门影响。
- Browser script session 的 `window.scrollTo`/`scrollBy` 经过 `PBrowserScriptScrollCallbacks` 交给活动宿主；宿主返回实际 page 坐标后，Browser 只派发一次 `scroll`。宿主的物理滚动路径用 `PBrowser_ScriptSessionNotifyScroll` 反向同步，重复坐标不派发事件，回调内不会重入 runtime。
- Core 的布局 relation 在成功 layout 后提供单元素 border-box union、最多 16 个
  inline 行片段以及 retained overflow 的滚动/scrollport 快照；Browser 用这些有界
  快照生成 viewport-relative `getBoundingClientRect()`/`getClientRects()`，并执行
  页面级或最近 addressable ancestor 的有限 `scrollIntoView()`，也支持显式
  `container:"all"` 的有界祖先链。未布局、无对应 box
  或没有正尺寸片段时分别返回全零/空集合；不承诺 transforms、Range/Selection、完整
  scroll tree、scroll chaining、pinch zoom、平滑滚动或视觉像素精度。
- Core 的 `PCore_DocumentWidth` 与 `PCore_DocumentHeight` 在最近一次 layout 后报告 page-level extent；宽度包含页面内容的水平溢出且不小于 layout viewport。宿主把同一 `(scroll_x, scroll_y)` 用于 paint、命中测试、fragment reveal、滚动条和 native child reposition；嵌套 overflow 的完整树、chaining/anchoring 和匿名目标仍未实现。
- next702–704 已补齐有界的元素 overflow 滚动：Core 对带 DOM `id` 的常见 block/replaced/flex box 保留 scrollbar offset，关系 38/39 返回/设置 CSS 像素并执行 clamp，关系 40–43 为 Browser 提供 axis availability 和 client-edge origin；`PCore_OverflowPointer`/`PCore_OverflowScrollSnapshot` 把 WM pointer 的目标和位置交给宿主。Browser 通过 `PBrowserScriptScrollInfo.element_id` 接入 `Element.scrollLeft`/`scrollTop`/`scrollTo()`/`scrollBy()` 和有限 nested `scrollIntoView()`；默认选择最近祖先，`container:"all"` 沿最多 64 层向外处理，`PBrowser_ScriptSessionNotifyElementScroll` 更新脚本状态并去重派发目标元素 scroll。完整 scroll tree、scroll chaining/anchoring、scroll-margin、smooth/inertia 和非 addressable/匿名目标仍不支持。
- 页面导航保留旧页到候选文档成功提交；主文档和资源网络阶段与 UI 文档操作分离。Browser candidate handle 拥有 generation、取消/退休、提交资格和结果分类，宿主用它门控 worker 完成/进度消息并在 worker 收尾后回收旧候选；旧候选不能越过 generation 门。layout/swap 前，宿主通过 `PBrowser_NavigationCommitGetInfo` 读取独立 candidate/resource 的组合 decision 与 `can_commit`，不在宿主复制失败/过时提交规则。
- Browser 资源事务按 URL 去重并拥有 `pending`、`ready`、`failed`、`cancelled` 四种终态、成功字节、失败分类、required/optional gate、transport 重试预算、最多 4 项 hash-only 摘要和 fallback family 计数。宿主负责网络 I/O、worker、取消/重试时机和页面提交，只保留 URL→resource-index 短引用；HTTP、resolve、budget、memory 和取消不重试，取消也不会重新暴露为可用缓存。
- 导航 request 在 worker join 后由宿主先收敛失败/过时资源，再调用 Browser 的 `PBrowser_NavigationCleanupGetInfo` 复制 cleanup decision、candidate/resource 终态、pending、`can_release`、hash-only failure summary 和 fallback 计数；复制值在 candidate/resource handle 销毁后仍可用于日志。TEST1127 同时覆盖 pending/terminal decision、required failure、optional fallback、取消、stale、清理前复制、释放后快照存活，以及成功/失败 `pcore_navigation_finish` 的真实回收路径。
- Core 的 form owner relation 对支持的 input、select、textarea、button、fieldset、img、object、output 解析
  最近祖先或显式 `form="id"` 目标；Browser 的 `Element.form` 与 `form.elements` 复用这条
  规则，后者从整棵文档按顺序返回跨树 listed form-associated 元素的有界 snapshot，img
  仅保留 owner、不进入 collection，空值/无效目标不回退祖先。fieldset/object/output 只进入
  relation collection（以及 output 的 labels），
  不进入 successful-control、提交或 FormData；Core validation、reportValidity、
  successful-control/multipart submission、
  dialog/default-submit、reset 和按坐标的 submit/reset 激活仍只消费可提交控件 owner。
- `<option>.form` 由 Browser 沿最多 64 层可寻址父链定位所属 select，再复用其 form owner；
  嵌套 optgroup、显式 `select form="id"` 和 attribute mutation 已由 TEST1185 及设备门验证，
  缺失或无效 owner 安全返回 `null`，不改变 Core ABI。
- `select.type` 依据 live `multiple` attribute 提供只读的 `select-one`/
  `select-multiple` 模式，`optgroup.label` 反映 label attribute 且缺失回退为空字符串；
  `option.label` 的文本 fallback 保持不变。TEST1186 及设备门已验证这组 Browser metadata，
  不创建 native SELECT 或改变 layout/paint。
- 支持的链接、summary、native EDIT/SELECT/button/file 等目标，以及带有效非负 `tabindex` 的普通布局元素按有界顺序响应 Tab/Shift+Tab：正值升序、同值 DOM 稳定排序，随后零/缺省组；负值、disabled/hidden/stale 目标和 file picker 仍被排除。Browser 报告活动 modal id 后，宿主可用 Core 的 scoped snapshot 将顺序焦点限制在 dialog 子树；宿主仍同步焦点事件、原生焦点和滚动可见性。
- `<dialog>` 的 show/showModal/close/requestClose、returnValue、cancel/close 事件、活动 modal id 查询、宿主驱动的 Escape 请求桥接、有界 backdrop 指针策略、`method="dialog"` 默认动作和 Core modal paint 已形成契约。显式点击、脚本 `click()` 和单行输入隐式 Enter 都遵循 validation→可取消 submit→直接 close/returnValue；CSS `::backdrop`、透明合成、多个 modal 和跨文档 modal 仍未实现。
- 单元素 `contenteditable` 已形成 Core/Browser/宿主边界：Core 解析祖先继承并限制合法 UTF-8 纯文本 mutation，Browser 暴露 `isContentEditable`/`innerText`、`selectionStart`/`selectionEnd`/`selectionDirection` 与 typed input 事务，宿主为带 id 且已布局的有效 editing host 创建有界 WM multiline EDIT 代理。允许的 `WM_CHAR` 在默认处理完成后回读最终文本并派发 `input`；取消的 `beforeinput` 不修改 Core，重复/提前 `EN_CHANGE` 不会制造空事件。Browser 选区偏移使用 UTF-16 code unit；宿主将 WM EDIT 的 CRLF 位置转换为逻辑 LF，并在可用时同步原生 HWND。无修饰鼠标拖选以及 Shift/方向键扩展由宿主短暂保存 anchor；捕获丢失、取消模式和焦点切换会先结束手势，再通过 Browser 的去重通知入口刷新范围。宿主对 `WM_PASTE`/`WM_CUT` 只接受有界 `CF_UNICODETEXT`，把规范化后的 UTF-8 data 交给 `beforeinput`，允许后执行 native default，再提交 Core/input 和折叠选区；`WM_COPY` 只写入非空的有界 Unicode 选区，折叠选区保持现有剪贴板不变；格式缺失、超长或读取失败时 fail closed。为兼容 WinCE 原生剪切的内部重入，宿主只在外层 `WM_CUT` 默认处理期间放行同一 HWND 的嵌套 `WM_COPY`。每页最多 16 个宿主、文本最多 8192 UTF-8 字节；嵌套继承后代不重复创建 host。
离线 compatibility corpus 已覆盖导航资源事务、候选提交与回滚、history/viewport、页面生命周期、脚本调度、焦点、滚动、Core/Browser 几何和显式 form-owner 组合。每项测试的 fixture 与断言说明统一见 [`docs/TESTING.md`](../docs/TESTING.md)；handoff 只保留当前门和仍未完成的边界。

### 当前测试入口

- `TEST_MAX_NUMBER`：1290。
- tracked `test_host/test_host.ini`：`auto=1`、`javascript=0`，选择 `13,20,27,56,58,62,64-67,73,75,1217-1290,999`。
- tracked INI 是窄 smoke，不是全量目录；nightly 打包脚本从源码 dispatch 动态生成全量自动清单。
- 设备连接必须先由用户在 WMDC/Device Emulator GUI 手动完成；RAPI gate 只使用当前唯一会话。

## 最新有效设备证据

`tmp/device-runs/20260918-154440-next851` 是本批最新有效证据：Debug ARMV4I
`1289-1290,999`，3/3 PASS；外置卡双空间预检、完整日志回收、清理、`crash_check` PASS，dump=0。
日志明确记录 TEST1289、TEST1290 和 TEST999 均为 OK，`TESTBENCH PASS`，无缺失或额外测试。
上一个稳定证据为 `tmp/device-runs/20260918-152259-next850` 的 `1288-1289,999`，3/3 PASS。
再上一个稳定证据为 `tmp/device-runs/20260917-212821-next849` 的 `1287-1288,999`，3/3 PASS。
更早的稳定证据为 `tmp/device-runs/20260917-204745-next848` 的 `1287,999`，2/2 PASS。
此前 next841 的证据仍保留在 `tmp/device-runs/20260916-205600-next841`。

不完整日志不算通过；旧失败由 Git 与历史文档保留。

## 当前人工验收状态

以下路径已有过真实设备确认，但后续触及相邻基础设施时仍需重新评估：

- example.com → IANA 的容器边距、深层导航和旧页保留；
- SIP 候选词整词提交；
- bitmap/SVG、表格、列表和常见布局的可见结果；
- native EDIT/SELECT、真实 file picker、旋转和 DPI 路径。
- 带 `tabindex` 的普通元素的设备焦点矩形、触摸命中和不同 DPI 视觉仍需人工观察；语义顺序已有自动断言。
- `<dialog>` backdrop 的整体色彩、边界、滚动/旋转下的视觉仍属于可累计的人工观察；Core 的绘制顺序和设备门像素契约已有自动断言。
- contenteditable 的 OEM 硬键盘/自动重复、SIP/IME 候选词、跨应用剪贴板互操作、滚动/旋转和不同 DPI 下的文本视觉仍属于可累计人工风险；1113 已在真实 WM EDIT 上验证无修饰鼠标拖选的连续范围/方向通知，1114 验证了 Shift/方向键、捕获丢失和焦点切换的有界通知收尾，1112 覆盖脚本 `selectionchange` 去重，1115 覆盖宿主自备的 `CF_UNICODETEXT` 连续 paste/cut（包括 retained layout 暂失时的按 id 事件派发），1116 覆盖宿主 `WM_COPY` 与格式/容量拒绝。完整 ClipboardEvent/async clipboard、CF_TEXT/富文本转换仍不在契约内。
- TEST1151 autofocus 夹具仅证明 DOM/焦点桥合同；初始焦点矩形、native HWND、触摸/SIP、滚动条裁剪和不同 DPI 仍需宿主观察。
- TEST1152–1169 是离线的 Browser selector、validation、焦点和 placeholder 夹具，
  自动门已证明各自的有界查询、mutation、顺序、callback 边界和非法输入 fail-closed；
  真实页面完整 Selectors、native 输入、SIP/IME、触摸、布局视觉和不同 DPI 仍由宿主观察。
- TEST1170–1188 是离线的 Core/Browser form-owner、validation、submission、reset、
  FormData、selector 默认状态、option 属性/collection、fieldset projection 与
  `form.elements` 夹具；自动门已证明跨树 owner、成功控件排除、默认动作顺序、snapshot
  隔离和有界错误回退。它们不保证完整 live collection、native 表单/SELECT 视觉、picker、
  键盘/触摸、SIP/IME 或不同 DPI 行为；逐测试合同见 [`docs/TESTING.md`](../docs/TESTING.md)。
- 低号 TEST118 是 native SELECT 键盘桥的例外：本批自动设备门覆盖关闭态 COMBOBOX 的
  ArrowDown `keydown`/`keyup`、target/bubble 顺序和 layout invalidation 后的 live
  selection 回退；它不覆盖展开 popup、触摸、SIP/IME、OEM 重复键或视觉保证。
- TEST1189–1199 的 form-owner、output/object/img metadata、image-map、srcset/picture
  选择和 source lifecycle 夹具均已有自动门证据；详细合同、边界和逐项结果统一见
  [`docs/TESTING.md`](../docs/TESTING.md)，这里不重复维护历史清单。
- TEST1201–1290 的 DOM/CharacterData、HTML parser、detached wrapper、属性 facade、
  body.text、session cookie、document.write、Core-backed document.title 与 bounded
  DocumentFragment lookup/clone/selector/relations/replace/composition/collection/normalize、
  detached Element HTML serialization、Fragment textContent 原子替换、Fragment-owned
  detached Element sibling 关系、Browser-owned Text/Comment element-sibling 关系、Fragment
  CharacterData 根 staging/textContent 投影及 Browser-created Text/Comment/Element 的
  CharacterData/relative primitive/现有 CharacterData source、Text/CDATA normalize 夹具均已有相邻设备门；
  逐项合同、预算和选择集中在 [`docs/TESTING.md`](../docs/TESTING.md)，本文件不重复维护历史清单。
  通用节点、observer、完整 live collection、native/OEM 视觉和 SIP/IME 仍不在自动门范围。
- 允许累计的人工风险包括低风险视觉、触摸、SIP/IME、旋转、picker 和失败网络观察；
  崩溃、数据损坏、严重布局破坏或核心交互阻塞必须立即人工复核。

## 当前未决风险

- next700 的 `Element.getClientRects()` 已能把普通 inline flow 的实际行片段暴露为最多
  16 个 viewport-relative 矩形，并以同一集合计算 union。它不是完整的 CSSOM 几何算法：
  Range/Selection、transforms、nested overflow、pinch zoom、平滑滚动、复杂 inline
  嵌套、字体精确度量和视觉像素仍需宿主集成观察。TEST1145 只证明离线窄容器中的
  Core/Browser 一致性、顺序、identity 和 union。

- next701 的六个布局尺寸 getter 只消费最近一次 Core layout 的有界快照。支持范围是
  常见 block、replaced、table/flex box；完整 CSSOM box model、实时 reflow、transforms、
  pinch zoom、字体精确度量和真实滚动条视觉仍未实现。next702–704 只在带 id 的常见
  overflow box 上增加 retained 两轴滚动和有限 nested `scrollIntoView()`；默认选择最近
  ancestor，`container:"all"` 才沿最多 64 层向外处理，完整滚动容器树、scroll
  chaining/anchoring、scroll-margin、smooth/inertia 和匿名目标仍未实现。
  next705 又让 `HTMLElement.focus()` 复用同一条 Browser-owned 嵌套 reveal 路径，并由
  Ex `prevent_scroll` 让宿主延后 page-level reveal；next706 再增加宿主显式触发的
  `autofocus` 查询/设置和无 id 目标事件 dispatch，但 Browser 不自主执行初始焦点，
  完整滚动树和焦点导航仍未实现。TEST1146–1151 只证明离线 fixture 中 Core/Browser
  的整数值、clamp、事件、size-probe 和 fail-closed 回退一致。

- 已建立固定、小型、可重复的离线 corpus 流程，但它们仍不能代表任意真实网站；TEST13 仍只是单一网络哨兵。TEST1119–TEST1150 已覆盖导航事务、资源 gate、页面生命周期、滚动/几何、布局尺寸、元素 overflow、媒体/焦点和脚本调度的有界合同。取消仍是协作式的，脚本队列仍依赖宿主调度；任意真实站点的 fallback 视觉、复杂布局、Range/Selection、inline 嵌套、完整滚动容器树、scroll chaining、pinch zoom、精确逐元素归因和自定义 prompt 仍未保证。
- `<dialog>` 已有已验证的有界脚本生命周期、`method="dialog"` 默认动作、活动 modal id、Escape→`requestClose()` 桥接、宿主顺序 Tab/Shift+Tab 子树范围、有界 backdrop 指针策略和 Core 实体色 modal paint；当前表单桥要求最近祖先 dialog 有非空 id。CSS `::backdrop`、透明合成、多个 modal 和跨文档 modal 生命周期尚未实现，初始焦点、native 窗口视觉和非顺序平台焦点仍由宿主决定。
- `contenteditable` 具有单元素纯文本状态/mutation、Browser 的 bounded selectionStart/End/Direction、去重后的 `selectionchange` 和带 id、已布局 editing host 的有界 WM EDIT 代理；宿主在无修饰 `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` 以及键盘扩展后报告范围与 forward/backward 方向，捕获/取消/焦点中断会收尾而不重复派发，每页最多 16 个 host、文本最多 8192 UTF-8 字节，嵌套继承后代不重复代理。当前另有宿主级受限 `CF_UNICODETEXT` 粘贴/剪切/复制事务：`WM_COPY` 的非空选区才写入剪贴板，折叠选区是 no-op；不支持的格式和超长数据在 native mutation 前 fail closed。Range/Selection 对象、完整 ClipboardEvent/async clipboard、CF_TEXT/富文本转换、OEM 特有键盘自动重复与复杂行导航、designMode、完整 IME 组合尚未实现。
- float、复杂 table/position、现代 CSS 与任意畸形页面仍有明显边界。
- 浏览器 JavaScript 是有限组合，不具备完整 DOM/Web API 或现代浏览器安全沙箱。
- Browser selector 仍是有界子集：支持列表/关系/属性/结构伪类、表单状态、focus/link/visited/target/lang、`:not()`/`:is()`/`:where()`/`:has()`、可选 interaction 的 `:active`/`:hover`、Core validation 的 `:in-range`/`:out-of-range`、依据 readonly/effective-disabled 和可选 contenteditable callback 判定的 `:read-only`/`:read-write`、text-like input/textarea 的 `:placeholder-shown`、依据默认 checked/default-selected 与首个 submit control 的 `:default`，以及直接、无参数的 `:scope` context。TEST1152–1169、TEST1179–1183 已覆盖这些路径的查询、mutation、预算和非法输入回退。范围伪类只接受非空且受约束的 input number/range/date/month/week/time/datetime-local，underflow/overflow 才构成 out-of-range；空值、bad/type mismatch、disabled/readonly、无范围限制、非 input 和单独 stepMismatch 安全不匹配。显式 contenteditable 在 callback 缺失或查询失败时两种编辑伪类都不匹配；placeholder 伪类不匹配空 placeholder、其他 input 类型、普通元素或带参数形式。`:visited` 只由宿主 Ex callback 明确批准，Browser 不保存或推断 history；`:scope` 的 receiver/document owner 规则不扩展为嵌套参数或完整 Selectors；`:default` 不提供完整默认按钮算法，relation 45 缺失时保守不匹配。完整 CSS Selectors、visited 的持久化/隐私隔离/真实颜色、伪元素/namespace/shadow DOM、`:has()` 链式关系、`:target` reveal 以及复杂页面的 1.5 MiB Browser heap 预算边界仍未承诺；详细合同见 [`docs/TESTING.md`](../docs/TESTING.md)。
- 图片资源的候选选择覆盖 Core 的最多 16 个同类正密度 `x` 或正宽度 `w` 候选（每个
  URL 最多 2047 字节），以及每个 `<picture>` 最多 8 个 preceding `<source>`、16 层
  ancestor 和 64 个 direct-child 节点的有界扫描。source 先按 document order 过滤
  media/type，再用同一 `srcset`/`sizes` 选择器；结果由 fetch/cache/layout/currentSrc
  共用。`sizes` 只支持 px/vw/vh 和单一 min/max-width 条件，完整媒体查询、绝对 URL、
  CORS/referrer enforcement、完整 loading/fetch-priority 策略或 native 图像视觉仍未
  实现。Core 的 image-map 只有有界的 default/rect/circle/poly 命中和 area
  几何；不覆盖 transforms、完整 HTML image-map 算法或 pointer/touch 手势。`decode()`、
  `load`/`error` 只覆盖 Browser 的有界 Promise/事件桥，必须由宿主在 Core 的当前
  complete/natural-size relation 就绪后显式通知；它不提供后台加载、自动事件或完整图像
  生命周期。
- `<option>` 的 `selected`/`defaultSelected`、`value`/`label`/`text` 与 select 的
  `options`/`selectedOptions`/`length`、option `index` 是可选的 Browser 扩展：前者由
  Core 维护 live 选择并执行单选互斥/多选规则，后三项复用通用 DOM attribute/text
  callback，显式属性优先、缺失时回退到 option 文本；集合是按可寻址 id 遍历得到的有界
  snapshot，selected mutation 会在下一次读取时反映，snapshot 自身的数组修改不回写 DOM。
  集合最多遍历 256 个节点并返回 64 个 option，缺失 id 的元素不可被当前 wrapper 寻址；
  不实现完整 live HTMLCollection、option form/disabled 全部算法、append/remove、native
  popup、键盘/触摸、SIP/IME 或视觉结果，缺失 callback、非目标和无效 id 均安全失败。
- 多窗口、持久 history、完整下载/外部协议策略仍属于宿主或未实现范围。
- mbed TLS 2.16.12 等依赖为旧平台兼容 pin，发布前必须审查当前安全风险。
- OEM SIP/IME、系统 picker、视觉和旋转不能仅凭 synthetic 自动测试保证。

Core/Browser form owner 目前覆盖 input、select、textarea、button、fieldset、img、object 和 output；这些元素
（包括显式 `form="id"` 的跨树元素）按文档顺序把 listed 项加入有界 `form.elements` snapshot；img
只提供 owner，不进入 form collections。fieldset、object 和 output 只属于 DOM relation enumeration，
仍不进入 successful-control visitor、submission 或 FormData；output 的 labels、descendant-text `value`、
独立 default override、只读 `type` 和 reset 恢复已覆盖。validation、
submission/multipart、dialog/default-submit、reset、按坐标的 submit/reset 激活和脚本
`HTMLFormElement.submit()` direct path 以及 `new FormData(form[, submitter])` snapshot 也
复用这条 owner 规则。direct path 和 FormData bridge 仅支持有 id form；前者跳过 validation、
submit event 和 submitter，后者的 Ex 路径只接受目标 form 的 enabled submit-type input/button，
最多返回 64 项且文件只返回 filename/type metadata；Browser 构造成功后同步派发非冒泡、
不可取消的 `formdata` 事件，监听器可修改返回对象。完整 live collection、文件读取、
复杂 parser 重构和 native 表单视觉仍未实现。

完整列表见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)。

## 唯一下一步

next851 已完成：为已物化 Browser-created Element wrapper 提供 attached `innerHTML`
child-snapshot reconciliation 与 public-alias `outerHTML` replacement/detach，补充
TEST1290 和相邻设备门。唯一下一步是选择并实现 next852：仍须先从 compatibility corpus、
源码、设备日志或截图固定一个新的、可复现的用户可见组合缺口，再建立最小离线 fixture 和
自动断言。完成标准是可复现的产品侧纵切、直接相邻回归、风险相称的正式设备门（完整日志、
双空间预检、清理和 crash check）以及职责文档更新；若触及崩溃、数据损坏、严重布局破坏或
核心交互阻塞，另须立即人工复核。
新批次仍须把可复用语义放入公共 DLL，宿主只保留平台接线、调度、fixture 与断言，并附带
相邻回归和职责文档更新。超出 bounded Element/Text 子集的通用节点、混合/嵌套
DocumentFragment 插入、
超出有界元素约束的 reparent、其他删除、
Range/Selection、完整 live collection、MutationObserver、完整滚动容器树、pinch zoom、
transforms、scroll-margin、平滑/惯性滚动、完整媒体查询语法、bfcache、绝对 URL、CORS、
完整图像 loading 和 image-map 扩展仍是候选限制，不能在证据之前写成已支持行为。

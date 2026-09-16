# Positron 路线图

本文件只描述未来工作和选择优先级。已经完成的能力不保留在这里；当前事实见 [`HANDOFF.md`](HANDOFF.md)，限制见 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)，逐批历史见 Git。

## 长期目标

让 Windows Mobile 6 / Windows CE 应用能够按需组合一组稳定、资源有界、可部署的公共 DLL：

- verified TLS、JSON、HTTP、图像和脚本基础设施；
- 可嵌入的 HTML/CSS/DOM/layout/paint Core；
- 无窗口依赖的 Browser history、script session 与平台事务协调；
- 由应用宿主提供窗口、消息循环、网络调度、native 控件和产品策略；
- 在旧工具链约束下仍有清楚 ABI、所有权、安全边界与真实设备证据。

长期成功不以测试编号数量衡量，而以真实应用能否通过公共 DLL 完成完整流程、宿主是否保持轻薄、以及回归是否能稳定发现用户可感知问题衡量。

## 中期里程碑

形成一个可由固定真实页面/交互语料驱动的轻量网页运行时：

1. 为常见页面建立小型、离线、可重复的 compatibility corpus；
2. 用 corpus 选择纵向能力，而不是零散增加 API；
3. 把通用 HTML/CSS/DOM/Event/form/history/script 语义放入 Core 或 Browser；
4. 宿主只保留 WM 平台接线、网络 I/O 和应用策略；
5. 为资源、生命周期和失败回滚建立可重复的全量 checkpoint；
6. 对无法自动判断的视觉、触摸、SIP、picker 和旋转做成批人工验收。

## 近期目标

### 真实页面组合缺口

现有 Browser/Core 已形成可复用的页面组合基线：page-level 与有限 nested scroll、布局
几何和尺寸快照、WM_SIZE/`matchMedia()`/`visualViewport`/`screen.orientation`、
`history.scrollRestoration`、页面生命周期、窗口焦点、activeElement、focus/autofocus、
以及有界 selector 子集。

selector 当前包含顶层列表、四种关系组合器、六类属性操作符、
有限结构伪类、Core effective-disabled 表单状态、option live selected 的 `:checked`、通过 validation
callback 的 `:valid`/`:invalid`、范围验证的 `:in-range`/`:out-of-range`、通过 activeElement callback 的 `:focus`/`:focus-within`
、静态的 `:link`/`:any-link`、当前 fragment 与元素 id 对齐的有界 `:target`、沿父链
  继承语言的有界 `:lang()`、单一简单 compound 参数的 `:not()` 以及最多 16 个简单
  compound 分支的 `:is()`/`:where()`、最多 16 个后代/子代/兄弟相对分支的 `:has()`，
  以及通过可选 interaction callback 读取 Core 精确状态的 `:active`/`:hover`、通过
  interaction Ex callback 读取宿主明确批准 history 的 `:visited`、依据
  readonly/effective-disabled 与可选 contenteditable callback 判定的 `:read-only`/
  `:read-write`、text-like input/textarea 空 value 与非空 placeholder 的
  `:placeholder-shown`、有界 `:default`（默认 checked/default-selected 与 form 首个
  submit control），以及以 query receiver/document root 为 context 的直接、无参数
  `:scope`；Core 的 form-owner relation 还支持 input、select、textarea、button、fieldset、
  img、object、output 的 `form="id"` 显式跨树归属，并让 Browser `form.elements` 按文档顺序返回包含有 id
  fieldset/object/output 的有界 snapshot；img 提供 owner 与有界元数据但不进入 form collections；fieldset/object/output 不进入 successful-control 或
  FormData visitor；

  Core validation、submission、multipart、dialog/default-submit、reset 和按坐标的
  submit/reset activation 也共享该 owner 解析；Browser 另提供可选的
  `HTMLFormElement.reset()` 与 `requestSubmit([submitter])` 可取消事件/默认动作桥接，
  前者调用 Core reset，后者调用 Core validation/submission primitives；另有
  `HTMLFormElement.submit()` 的 direct bridge，调用 Core NoValidationById primitives，
 跳过 validation、submit event 和 submitter 选择；`new FormData(form[, submitter])` 通过
 独立的 Core successful-control snapshot bridge 生成脱离 DOM 的有界对象，保留显式 owner、
 重复项和文件 metadata；Ex callback 只允许目标 form 的 enabled submit-type input/button，
 不暴露 picker 路径。Browser 在构造成功后同步派发非冒泡、不可取消的 `formdata` 事件，
`FormDataEvent.formData` 指向正在返回的对象，监听器可在构造返回前修改它；

`<option>` 还提供有界的 `selected`/`defaultSelected` 与 `value`/`label`/`text`
属性桥：前者由 Core 维护 live/default 选择状态，后者复用通用 attribute/text
callback 并在缺失属性时回退到 option 文本；`select.options`、`selectedOptions`、
`length` 和 `option.index` 另外提供按可寻址 id 生成的有限 snapshot；`select.type` 根据
live `multiple` attribute 提供只读的 `select-one`/`select-multiple` 模式，`optgroup.label`
反映 label attribute（缺失为空字符串），而 `option.label` 的文本 fallback 保持不变。

`fieldset.type`、`fieldset.form`、`fieldset.elements`、`img.form`、`object.form` 和 `output.form`/`output.labels` 也通过公共 bridge 提供有界的
只读元数据与子树控件 snapshot：form owner 复用 Core 的祖先/显式 `form="id"` 规则，
集合按 DOM 顺序返回含嵌套 fieldset 的可寻址 input/select/textarea/button/object/output，最多遍历
256 个节点、返回 64 项；`form.elements` 另外按同一 owner 规则包含有 id 的 fieldset/object/output；
output 的 label association 复用 Core relation；object 只提供 owner/collection 元数据，不
创建 plugin 或替代内容窗口；img 提供 owner 与有界加载元数据；output 的 `type`、descendant-text `value`、带
独立 default override 的 `defaultValue` 和 form reset 恢复由 Core/Browser 统一提供，
但不扩展其他 listed elements、完整图像加载语义或 live child mutation。

`HTMLImageElement` 还提供有界的属性与资源状态投影：Browser 反映 raw
`src`/`srcset`/`sizes`/`useMap`、`crossOrigin`、boolean/尺寸属性和加载提示属性，Core
relation 46–49 提供 `naturalWidth`/`naturalHeight`/`complete`/`currentSrc`。Core 当前
按 viewport DPI 在最多 16 个、每个最多 2047 字节的同类候选中选择最小的足够来源或最高
来源：`x` 描述符按密度选择，`w` 描述符按 `sizes` 解析出的 px/vw/vh 源尺寸选择，且
可选单一 `(min-width|max-width: <length>)` 条件按当前视口求值；缺失或不支持的 `sizes`
按 100vw。`<picture>` 还会在最多 16 层祖先、8 个 preceding `<source>` 和 64 个
direct-child 节点内按文档顺序过滤有界 `media`/`type`，再复用同一选择器；无可用 source
时才回到 img 的候选。选择结果驱动资源发现、缓存、retained decode 和布局；无 source、
候选不可用及终态 fetch failure 的状态分别保持可观察且 fail-closed。Browser 另提供有界 `decode()`
Promise、source mutation/teardown 拒绝和由宿主在 Core 终态就绪后触发的 trusted、非冒泡
`load`/`error` 通知。绝对 URL、CORS/referrer enforcement、完整 loading 策略或图像视觉仍
未实现。source 的 `media`/`type`/`srcset`/`sizes` mutation 由 Browser 通过
`PBrowser_ScriptSessionNotifyImageSourceChange` 接收；它只失效 source identity 已改变的
pending decode 和旧终态。有效 viewport resize 会在媒体与 resize 事件前刷新同一 identity，
而不会代替宿主执行 fetch、选择、layout 或 paint。脚本 attribute mutation 还可通过复用
既有 DOM attribute slot 的 typed callback 通知宿主；它只提供 borrowed 元数据，不改变
宿主负责 replacement pipeline 的边界。Core
另已提供有界 image-map 命中：已布局 `<img usemap>` 按 DOM 顺序解析最多 64 个 linked
`<area>`，支持 `default`、`rect`、`circle` 和 `poly`/`polygon`，将自然坐标缩放到
渲染尺寸，并把链接、区域几何、active/hover 和按坐标事件 target 统一接入 Core 命中
路径；坏坐标、`nohref`、未知形状和超出预算安全忽略。

以上桥不扩展
native SELECT popup、完整 live collection 或完整 HTML option 算法；`option.form` 通过
最多 64 层可寻址父链定位所属 select 并复用 `select.form`，显式 form owner 与 mutation
可见，缺失或无效 owner 返回 `null`；这仍不是完整 HTML form-owner 算法。

对应自动合同见
`docs/TESTING.md` 与当前交接文件。上述语义必须继续
由 Core/Browser 提供，不能退回到 `test_host` 的业务 helper。

结构 mutation 目前承诺这些窄路径：带 id 元素的 `Element.append()`/`prepend()` 零至四值文本/元素插入，
连接中 direct Text wrapper 的 `Text.remove()`/`Element.removeChild(Text)`，连接中
 direct Comment/CDATA wrapper 的 `remove()`/`Element.removeChild()`，以及 Ex4 的已有
 element `Node.insertBefore()`/`appendChild()`、Ex5 的已有 element
 `Node.replaceChild()`/`Element.replaceWith()`、Ex6 的目标位置插入和相对单值 primitive
 文本插入，以及 Ex7 的 `Element.replaceWith(value)` 原位 primitive 文本替换，和 Ex8 的
 `Element.replaceWith(...values)` 2–4 值 primitive 原子替换；Ex9 的 CharacterData
 `replaceWith(value[, ...])` 对 Text/Comment/CDATA 提供 1–4 primitive 原子 Text 替换。write Ex6
负责 primitive Text，Ex2/Ex3 负责 CharacterData 删除，Ex4/Ex5 负责 existing-element
 插入/替换，mutation Ex6 按未过滤 `childNodes` 支持同父重排、跨父迁移、四位置
`insertAdjacentText()`/`insertAdjacentElement()`，并让 Text/Comment/CDATA wrapper 以同一
write Ex6 callback 做单值 primitive `before()`/`after()`；Ex7/Ex8 保留旧 wrapper 为 detached。
这些路径都由 Core 拥有 DOM 语义并在成功后使 retained layout 失效；relative primitive 列表的 write Ex7
支持 element 与 CharacterData 的 2–4 值原子 Text 插入，element `before()`/`after()` 另支持
2–4 值 mixed existing-element/primitive 列表，由 Browser 预检后复用 Ex6 callbacks 按序插入；
`Element.replaceWith(...values)` 也支持 2–4 值 mixed existing-element/primitive 组合，复用
Ex5–Ex7 callbacks。
Ex10 再为 Text/Comment/CDATA 的 `Node.insertBefore()`/`appendChild()` 提供现有节点
同父重排、跨父迁移和 wrapper owner 更新；Ex11 追加 `Node.replaceChild()` 的现有
CharacterData 同父/跨父替换，Ex12 再让 element target 用现有 CharacterData 完成
`replaceChild()`/`replaceWith()`；next794 又让 CharacterData 的 `before()`/`after()` 和单节点
`replaceWith()` 复用 Ex10/Ex11，支持同父/跨父 existing-node relative mutation。Core 按
source/target 未过滤索引提交，旧 Ex10 及更早 ABI 布局不变。next799 在 Browser 增加
最多四个 primitive Text 的 text-only `DocumentFragment` staging，并复用现有 text-list
mutation；next800 再以 Ex13 和 `PCore_NodeReplaceChildrenWithTextListById` 提供
`Element.replaceChildren()` 的 0–4 primitive Text 或单 fragment 原子替换，成功后才
消费 fragment，失败保留旧 direct children。next801 在同一 Ex14/Core 边界增加最多四项
primitive Text 与当前目标的已连接 direct Element 混合列表：只允许同一父级的既有 element
重排，保留被选节点及其后代 identity，并在提交失败时恢复完整旧树；跨父、重复、自身、
fragment、CharacterData 和超限输入继续 fail closed。next802 在新增 Ex15/Core 边界支持
最多四项 primitive Text、同父 direct Element 和按替换前未过滤 `childNodes` 索引指定的
Text/Comment/CDATA；Browser 保留选中 CharacterData/Element wrapper identity，Core
原子暂存并在失败时恢复完整旧树。通用节点/fragment、observer 和 live collection 仍需由
真实页面缺口驱动，不能从窄路径外推。

next803 根据 NetSurf compatibility corpus 中现有的 `document.createTextNode()` 用法补齐
Browser-owned detached Text：节点先在脚本侧保留数据、owner/index、snapshot 与 identity，
再通过既有 Core Text insertion、CharacterData move、Text setter 和删除入口进入 live Element。
实现覆盖 `insertBefore()`、`appendChild()`、只含 primitive/created Text 的有界 `append()`/
`prepend()`、`nodeValue`/`data`/`textContent`/`appendData()`、`remove()` 与 detached
`cloneNode()`；通用 Node、DocumentFragment、含 element/fragment 的混合 append 和其他动态
树语义仍 fail closed。TEST1244 已用 Debug ARMV4I 设备门验证，未新增 Core ABI。

next804 根据同一 compatibility corpus 中的 `document.createElement()` 用法补齐一个有界的
Browser-owned detached Element staging。Core 新增 `PCore_NodeCreateElementChildAtById`，
Browser 以 DOM write Ex11 复用既有 `__pcoreSetText` native slot；wrapper 在物化前保存
标签、唯一 id、本地 attribute 和 direct Text，成功后按未过滤 `childNodes` 索引由 Core
创建空 Element，再同步 staged 数据。remove/reinsert/id rename 保留 wrapper/alias identity；
嵌套 Element、通用 detached Core handle、DocumentFragment、事件/资源/observer 不在边界内。
TEST1245 与 `1245,999` Debug ARMV4I 设备门验证节点形状、属性/Text、生命周期、失败原子性
和容量限制。

next805 根据同一 compatibility corpus 中 `document.createComment()` 及
`parameter-error.html` 的调用缺口补齐一个有界的 Browser-owned detached Comment staging。
Core 新增 `PCore_NodeCreateCommentChildAtById`，按未过滤 `childNodes` 索引在已连接 Element
下创建 Comment；Browser 追加 DOM write Ex12 的 `create_comment_child_at` callback，并保留
detached wrapper 的节点形状、data/length、clone、appendData、remove 和 identity，后续更新、
重排与重插入复用既有 CharacterData callbacks。无效参数/reference、对象、超限输入、通用
detached Core handle、Fragment 和相对 mutation 均 fail closed。TEST1246 的
`1244,1245,1246,999` Debug ARMV4I 设备门验证成功/失败原子性、生命周期和 identity。

next806 根据源码审查发现的 CharacterData API 不对称，补齐 detached Comment wrapper 的
`insertData()`、`deleteData()`、`replaceData()` 和 `substringData()`。四个方法在 Browser
内按 UTF-16 code-unit offset/count 运行：detached 状态只更新本地快照，connected 状态复用
既有 CharacterData write callback，删除范围按末尾截断，非法参数、超长结果或 Core 失败不
部分提交；相对 `before()`/`after()`/`replaceWith()` 仍不纳入本批。TEST1247 的
`1246,1247,999` Debug ARMV4I 设备门验证 surrogate、生命周期、同步和失败不变性。

next807 延续同一不对称缺口，补齐 detached Text wrapper 的 `insertData()`、`deleteData()`、
`replaceData()` 和 `substringData()`。这些方法复用既有 Text setter/Core callback：detached
状态只更新 Browser 快照，connected 状态同步 live Element，offset/count 按 UTF-16 code
unit 校验，删除范围按末尾截断，非法参数或 callback 失败不部分提交；不新增 Core ABI，也
不扩展通用 Node/Fragment。TEST1248 的 `1247,1248,999` Debug ARMV4I 设备门验证 detached/
attached 生命周期、surrogate、同步和失败不变性。

next808 根据源码审查发现的 detached Element clone 不对称，补齐 Browser-owned
`document.createElement()` wrapper 的 `cloneNode(false/true)` staging。浅克隆复制有界属性，
深克隆复制 direct Text child；克隆保持独立的 Browser wrapper、数据和 detached root，连接源的
克隆必须先改为唯一 id，随后复用既有 Ex11/Core 物化、`insertBefore()` 和 identity 路径。
嵌套 Element、通用 Node/Fragment、事件、资源和视觉行为继续 fail closed，不新增 Core ABI。
TEST1249 以两个低堆峰值离线 fixture 覆盖浅/深复制、attached clone、重复 id 原子拒绝、改名
插入、顺序和源数据隔离；`1248,1249,999` Debug ARMV4I 设备门已通过。

next809 根据源码审查发现的 detached Element 关系不对称，修正 Browser 通用关系函数对
Browser-owned wrapper 的身份判断：不同 staging wrapper 即使标签、文本或 id 相同，也不
会被 `isSameNode()`、`contains()` 或 `compareDocumentPosition()` 按空/重复 id 合并；
物化到同一 parent、同父排序和移除后的 disconnected 结果继续使用同一 wrapper identity。
该修补不新增 Core ABI，也不扩展通用 Node/Fragment、事件、资源或视觉语义。TEST1250 的
两个低堆峰值 fixture 覆盖 detached/disconnected、ancestor/sibling position、document
root 与物化/移除生命周期；`1249,1250,999` Debug ARMV4I 设备门已通过。

next810 根据源码审查发现的 detached Element child-query 不对称，补齐 Browser-owned
wrapper 的 `hasChildNodes()`。它直接读取同一份有界 direct-Text staging，因此 detached、
物化、`textContent` 清空、再次追加和移除后的 `hasChildNodes()` 与 `childNodes`、首尾 child
和文本快照保持一致；`children`/`childElementCount` 仍明确表示 text-only 边界。不新增 Core
ABI，也不扩展通用 Node/Fragment、嵌套 Element、事件、资源或视觉语义。TEST1251 的两个
低堆峰值 fixture 与 `1250,1251,999` Debug ARMV4I 设备门覆盖空状态、追加/清空、物化和
移除生命周期；该设备门已通过。

next811 根据源码审查发现的 detached Element style facade 不对称，修正
`CSSStyleDeclaration.cssText` setter：Browser-owned wrapper 不再把没有 Core id 的写入直接
送到 native attribute callback，而是沿已有属性 facade 更新 detached/attached/removed 的
同一 style snapshot；live Element 仍使用原有 Core mutation 和有界声明解析。TEST1252 的
fixture 覆盖 `cssText` 读取、`setProperty()`、物化、移除和清空；不新增 Core ABI，也不扩大
CSS parser、嵌套 Element、事件、资源或视觉语义。`1251,1252,999` Debug ARMV4I 设备门已通过。

next812 根据 NetSurf compatibility corpus 的 `dom-html-div-element.html` 缺口，修正
Browser-owned detached Element 的 reflected attribute setter。已有 string、boolean、integer、
非负 length 反射以及新增的 `align` 在未物化/已移除 wrapper 上经属性 facade 暂存，物化后的
live wrapper 仍调用 Core；非法 integer/length 输入在写入前拒绝且不改变旧值。TEST1253 的
fixture 覆盖 staging、物化、live mutation、移除后的再次写入和错误不变性；不新增 Core ABI，
也不扩大嵌套 Element、通用 Node/Fragment、事件、资源或视觉语义。`1252,1253,999` Debug
ARMV4I 设备门已通过。

next813 根据 NetSurf compatibility corpus 的 `idl-treatnullas-emptystring.html` 缺口，补齐
live `HTMLBodyElement.text` 的遗留属性投影。getter 反映 `text` attribute，缺失时返回空
字符串；setter 对 `null` 使用 `[TreatNullAs=EmptyString]`，其他输入按 JavaScript `String`
转换，并保留既有 `option.text` 与非 body 元素的安全边界。TEST1254 覆盖初值、null/普通
值转换、set/removeAttribute、option 回归和非 body 拒绝；不新增 Core ABI，也不扩展
deprecated presentation-color、完整 body 接口或通用 DOM。`1253,1254,999` Debug ARMV4I
设备门已通过。

Element 的 `innerHTML`/`outerHTML` 现在通过 Core relation 51/52 提供只读、有界序列化；
`innerHTML` setter 另通过 Core 的 parser-backed `PCore_NodeSetInnerHTMLById` 和 Browser
write Ex8 在同一 document 中有界替换 direct children（16,384 字节、256 节点、64 层、每个
元素 64 个 direct child），并在成功后保留目标身份、失效旧 wrapper 与 retained layout。
同一 parser 边界的 `PCore_NodeInsertAdjacentHTMLById` 和 Browser write Ex9 还在四个相邻
位置插入片段，保持目标/既有节点身份并刷新受影响 snapshot；重复/冲突 id、非法 UTF-8、
未知/超限节点 fail closed。Browser write Ex10 另由
`PCore_NodeSetOuterHTMLById` 以一个 Element 根替换目标或以空字符串移除目标，保留原父级/
索引并让旧目标及后代 wrapper detached；重复/冲突 id、非法 UTF-8、顶层文本/Comment、
多根和超限输入同样在 mutation 前拒绝。三条 HTML mutation 路径都不执行 script、不抓取
资源、不派发 mutation event；Core 不暴露 fragment ABI，Browser 在脚本侧维护 text-only 与
bounded detached Element/Text staging；通用或嵌套 clone insertion、通用 DocumentFragment
和 context-sensitive parser 仍不在边界内。detached Element 的属性/direct-Text clone 与
bounded fragment consumer 都沿既有有界物化路径插入。

next815 已完成 NetSurf compatibility corpus 对 `document.write()`/`writeln()` 缺口的
有界纵切。Browser 提供可选 document-write callback 和当前 classic-script 发现索引，Core
以既有 UTF-8 fragment parser 将不含 `<script>` 的有界片段原子插在该脚本之后；解析前再
以标签名边界和 ASCII 不区分大小写的 `<script...` 源扫描 fail closed，避免惰性 parser
表示绕过安全边界。宿主只负责索引接线、重排请求和断言；`PScript_CollectGarbage()` 作为
长 bootstrap/page-script 批次的显式维护入口。TEST1256 覆盖多参数字符串化、`writeln()`
换行、脚本位置和 script-fragment 拒绝，TEST80 提供相邻回归。更换后的仿真器已通过
`80,1236-1239,1244-1256,999` 的 19/19 扩展设备门；门使用 16 KiB RAPI 传输块以避开
部分 DMA 镜像在 512 KiB 边界启动 32 KiB 写入时的 `0x80072746`。

未实现边界仍包括完整滚动容器树、scroll chaining/anchoring、scroll-margin、Range/
Selection、pinch zoom、平滑/惯性滚动、匿名焦点目标、pointer capture 和完整交互/链接
状态（包括持久化 visited history、隐私隔离与真实 visited 颜色）、伪元素、属性大小写修饰符、namespace、
shadow DOM、完整 Selectors 语法，以及
完整的媒体查询和 Web API。不能把有限 reveal、autofocus 或 selector 子集误写成完整
浏览器行为。

next816 已补齐设备门的部署护栏：默认优先使用外置卡的
`\Storage Card\Temp\Positron-device-gate`，目录、路径级容量或路径安全性失败时自动回退到
`\Temp\Positron-device-gate`；显式 `-RemoteBase` 仍保持严格固定目标。该变化不新增公共
DLL ABI，选择策略、预检字段和路径布局由配置测试及 `1256,999` 设备门覆盖。

next817 已补齐 Core-backed `document.title` metadata 纵切：Core 以
`PCore_DocumentTitle`/`PCore_DocumentSetTitle` 读取或替换首个直接 `<head><title>`，按
4 KiB 有效 UTF-8 setter 边界运行，缺失 title 时在现有 head 下创建；Browser 以新的读写
扩展 callback 映射该 metadata，不占用 Element id，不派发事件、脚本或资源副作用，旧宿主
仍可使用局部回退。TEST1257 覆盖解码、probe/截断、替换、创建和 JavaScript 字符串化；
桌面构建已通过；`tmp/device-runs/20260915-223010-next817` 在当前 GUI 连接的仿真器上以
Debug ARMV4I、外置卡自动模式运行 `1256,1257,999`，3/3 PASS、零 ERROR/FAIL，完整日志
回收、空间预检、完成后清理和 `crash_check` 均通过，新增 dump=0。

next818 补齐 Browser-owned `DocumentFragment` 的 bounded Element/Text staging。纯文本
fragment 保留既有 Ex13/text-list 与 CharacterData 消费；结构 fragment 最多承载四个
detached Element/Text 根，Element 必须有唯一非空 id、最多一个 direct Text child，顶层
Text 不能相邻。`append()`、`prepend()`、以 Element 为 reference 的 `insertBefore()`、
`appendChild()` 和 `replaceChildren()` 通过既有 Core HTML parser 原子物化，成功后保留
staged wrapper identity 并消费 fragment；嵌套/connected node、重复/缺失 id、结构标签、
超限和 parser/context 输入 fail closed。实现不新增 Core fragment ABI，不执行脚本、资源或
mutation event。TEST1258 与相邻 TEST1240–1257 已通过；`tmp/device-runs/20260916-003555-next818`
在当前 GUI 连接的仿真器上以 Debug ARMV4I、外置卡自动模式运行 `1240-1258,999`，20/20
PASS、零 ERROR/FAIL，完整日志回收、双空间预检、完成后清理和 crash check 均通过，新增
dump=0。

next819–next824 已在 Browser-owned `DocumentFragment` staging 上依次补齐 bounded
clone、lookup、selector、`children`/命名属性和 Node 关系；当前事实、设备证据和剩余
限制以 [`HANDOFF.md`](HANDOFF.md) 与 [`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md)
为准。公共 DLL 仍不暴露 fragment handle，关系预算和 fail-closed 边界也保持稳定。

下一批（next825）的选择必须先从 compatibility corpus、源码、设备日志或截图固定一个新的、可
复现的用户可见组合缺口，再为该缺口建立最小离线 fixture 或稳定哨兵。实现时明确旧页
保留、失败回滚、资源所有权和生命周期预期；通用语义进入对应公共 DLL，宿主只保留 WM、
线程、网络、native 控件和应用策略。任何新增结构都要保持 C ABI、UTF-8、opaque
ownership、固定容量和 VS2008/WM6/C89 兼容。

每一批都应包含自动断言、直接相邻回归、风险相称的设备门和职责文档更新。视觉、触摸、
SIP/IME、picker 或旋转可累计后人工验收；崩溃、数据损坏、严重布局破坏和核心交互阻塞
必须立即人工复核。不要为增加测试编号拆分能力，也不要在没有缺口证据时提前选择方向。

### 继续清理产品所有权

每批都审查 `test_host` 是否仍拥有可复用语义。若某段逻辑决定 URL、history、DOM、Event、表单默认动作、validation、图像或脚本 session 行为，应迁入对应公共 DLL；窗口、WM 消息、HDC、picker、SIP/IME 和应用导航策略仍留在宿主。

## 中期工作流

### JavaScript 与 Web 组合

- 依据 corpus 补齐高价值 DOM/Event/form/navigation 对象，不追求一次性完整 Web API。
- 继续在 Browser 中按真实页面缺口扩展有界 selector/DOM 组合；当前承诺简单 compound
  selector 的列表、四种关系组合器、六类属性操作符、有限结构伪类、表单状态伪类（含
  option live selected 的 `:checked`、Core effective-disabled relation、validation `:valid`/`:invalid`、范围验证的 `:in-range`/`:out-of-range` 和 activeElement
  驱动的 `:focus`/`:focus-within`、静态链接 `:link`/`:any-link`、有界 `:target`、单一语言
  标签的 `:lang()`、单一简单 compound 参数的 `:not()`、最多 16 个简单 compound 分支的
  `:is()`/`:where()`、最多 16 个后代/子代/兄弟相对分支的 `:has()`，以及显式 interaction
  callback 驱动的 `:active`/`:hover`、依据 readonly/effective-disabled 与可选
  contenteditable callback 判定的 `:read-only`/`:read-write`、宿主批准的 `:visited`、text-like
  input/textarea 的有界 `:placeholder-shown`、form 默认状态的 `:default`，以及直接、无参数的 `:scope` context；完整 CSS
  Selectors 语法仍不作为默认目标。
- 明确 script session 与 document/window 生命周期，继续验证取消、过时导航和 queue 清理的组合顺序。
- 为 timer、microtask、animation frame、message 和 lifecycle 的组合顺序增加真实页面断言。
- 保持浏览器 JavaScript 显式 opt-in，并持续验证关闭时不抓取或执行页面脚本。
- 评估可信/不可信脚本边界，避免把有限 Duktape host 误称为现代浏览器安全沙箱。

### Layout、绘制与字体

- 用真实页面缺口驱动 float、position、table、media、字体和复杂 inline 行为。
- 优先修复严重错位、内容不可达、错误滚动和交互几何，不做脱离语料的全面 CSS 扩张。
- 在继续扩展前，保持 next704–705 的边界：默认 `scrollIntoView()` 只 reveal 最近
  retained overflow 祖先；显式 `container:"all"` 和默认 focus 的嵌套路径才沿最多 64 层
  可寻址父链向外依次 reveal，并在每次滚动后重读目标矩形。focus Ex callback 的有效
  `prevent_scroll` 只负责让宿主延后 page-level reveal，显式 `preventScroll` 则不滚动。
  完整滚动容器树、scroll chaining/anchoring、scroll-margin、smooth/inertia 和匿名目标
  不因关系 40–43 bridge 而被误称为已实现。真实滚动条裁剪和触摸视觉进入人工验收矩阵。
- 继续降低深 DOM、资源树和重排路径的栈/heap 峰值，并为失败清理添加资源断言。
- 建立多 viewport/DPI 截图基线，但把设备量化和字体差异与语义断言分开。

### 表单、输入与可访问交互

- 依据实际流程在已有有界 `dialog` 脚本生命周期、`method="dialog"` 默认动作、活动 modal id、Escape 请求桥接、宿主顺序 Tab 子树范围、实体色 modal paint、backdrop 指针策略和单元素 contenteditable WM EDIT 接线、去重 `selectionchange` 通知、无修饰连续鼠标拖选以及 Shift/键盘、捕获和焦点中断收尾之上，继续用 compatibility corpus 选择相邻缺口。next667 已实现受限 paste/cut 事务与选区同步，next668 已完成 `WM_COPY` 非空选区、折叠选区 no-op、超长/非 Unicode fail-closed 及 WinCE `WM_CUT` 内部重入保护；next696 已把按 id 的 `HTMLElement.focus()`/`blur()` 请求桥接到 Core/native focus 事务，next697 又加入 Ex focus request 的 page-level reveal 与 `preventScroll`，next705 补齐了 Browser-owned 的嵌套 focus reveal 与宿主 page-reveal defer，next706 补齐了宿主显式 `autofocus` 的 Core 目标发现与无 id 事件保持；保持已实现的有界 `tabindex` 排序与 Core/宿主事务边界。
- 保持 native 控件 mutation、Browser event policy 与 Core form state 的事务顺序。
- 扩充真实 SIP/IME、硬键盘、SELECT popup、file picker 和旋转的成批人工矩阵。
- 对 disabled/hidden/stale target 一律 fail closed，不为通过测试绕过生命周期检查。

### 网络、安全与资源

- 维护 Mozilla CA snapshot 和旧 mbed TLS 风险评估，记录可接受的部署威胁模型。
- 根据真实消费者需求评估 TLS peer identity 轮换、错误分类和 listener 资源上限。
- 为 redirect、失败旧页保留、资源取消和页面提交建立离线/loopback 测试，降低外网依赖。
- 只有测量显示收益时再考虑 keep-alive、缓存或性能优化；先保护所有权和失败回滚。

### 公共 DLL 生态

- 保持公开头文件、README 调用模式、sample 和 ABI 测试一致。
- 为每个顶层 DLL 提供最小独立消费示例，避免只能通过 `test_host` 理解调用方式。
- 逐步减少宿主私有桥和重复业务规则；内部静态库继续隐藏在公共 DLL 后。
- 发布前持续审计 third-party 版本、许可证、生成步骤和 GPL 组合义务。

## 批次选择优先级

每次只选一个边界清楚、可验证的纵向能力，按以下顺序取舍：

1. 崩溃、数据损坏、严重布局破坏或核心流程阻塞；
2. 产品语义仍错误地滞留在 `test_host`；
3. compatibility corpus 暴露的高频真实缺口；
4. 安全、ABI、所有权、资源或生命周期风险；
5. 有测量证据的性能/内存问题；
6. 其他孤立 API 或观感优化。

一个批次应包含产品实现、宿主接线、自动断言、风险相称的设备门和文档职责更新。不要把同一个子功能拆成多个只增加编号的提交，也不要为追求“大步”把互不相关的能力塞入同一批。

## 全量与人工门触发条件

满足任一条件时运行更宽自动门或全量 checkpoint：

- 多个低风险定向批次已经累计；
- 修改公共 ABI、所有权或 session/document 生命周期；
- 修改 layout/paint、输入、网络/TLS、资源缓存或设备自动化基础设施；
- 准备里程碑或 nightly 交付；
- 出现无法解释的超时、混包、崩溃或数据错误。

低风险视觉/触摸/SIP/picker/旋转风险可以累计后集中人工验收。崩溃、数据损坏、严重布局破坏和核心交互阻塞必须立即人工复核。

## 每批完成标准

- 能力对应一个完整用户/消费者结果，职责落在正确 DLL；
- 公共 ABI、UTF-8、opaque handle、所有权、VS2008/WM6/C89 兼容性不退化；
- C89 回归、正式 ARMV4I 构建和仓库审计通过；
- staging 来自同一批构建，无旧进程或 DLL 混包；
- 定向门及直接相邻回归唯一 PASS、零 ERROR/FAIL；
- 必要人工门完成，或明确进入允许累计清单；
- `HANDOFF.md` 覆盖为新快照，限制与本路线图删除已经完成的条目；
- 稳定 README/架构/测试文档只在长期读者事实改变时更新，不追加批次流水；
- 只提交本批 tracked 文件并推送当前分支。

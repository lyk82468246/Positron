# Positron 路线图

本文件只描述尚未完成的目标、候选能力和选择规则。当前产品事实见
[HANDOFF.md](HANDOFF.md)，仍存在的边界见 [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md)，
稳定的架构与公共 DLL 所有权见 [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)，七个公共 DLL
的能力状态见 [docs/CAPABILITIES.md](../docs/CAPABILITIES.md)。已经
完成的批次不在这里建立时间线；具体实现由 Git 保存，只有会影响未来取舍的失败实验才进入
[FAILED_EXPERIMENTS.md](FAILED_EXPERIMENTS.md) 或 docs/history/。

## 如何使用这份路线图

路线图不是任务日志，也不是 API 清单；能力状态和当前边界统一见 `docs/CAPABILITIES.md`。
每次接管时先读交接、限制和路线图，再用源码、
compatibility corpus、自动测试和设备证据核对候选。路线图中的候选只表示“值得调查或
具备进入条件”，不等于已经承诺实现；只有完成一次取证并选定纵向能力后，才分配下一批的
内部编号。

候选使用四种状态：

- **准备取舍**：已有可复现的用户/消费者缺口、明确的公共 DLL 所有者、最小 fixture 形状和
  可验证的完成标准，可以在下一次规划时直接选择。
- **待取证**：问题真实存在或边界明确，但还缺少当前源码与 corpus 的组合证据；只能先做
  离线调查，不能直接改产品代码。
- **人工 backlog**：语义已有自动合同或明确的宿主边界，但真实触摸、SIP/IME、picker、
  旋转、DPI 或视觉结果必须由人观察；它不是自动产品批次。
- **暂缓**：需要完整浏览器能力、无法在 WM6 约束下给出有界合同，或主要依赖人工/厂商环境。
  暂缓不是已实现，也不应被下一批默认复活。

每个候选必须回答同一组问题：

1. 哪个真实页面、消费者或已复现失败证明它值得做？证据在哪里？
2. 语义由哪个顶层 DLL 拥有？宿主只需要哪些 callback、窗口或调度接线？
3. 预算、所有权、失败回滚、旧页保留和生命周期边界是什么？
4. 最小离线 fixture 如何证明成功、失败不变性和相邻回归？
5. 是否需要设备门或人工门？通过后的稳定事实应写入哪个文档？

如果没有候选能回答这些问题，本轮只做“发现和更新候选”，不为了增加内部编号而编写
产品代码。

## 长期目标

让 Windows Mobile 6 / Windows CE 应用能够按需组合一组稳定、资源有界、可部署的公共 DLL：

- positron_tls.dll、positron_json.dll、positron_http.dll、positron_image.dll 和
  positron_script.dll 提供可独立消费的基础设施；
- positron_core.dll 提供无窗口依赖的 HTML/CSS/DOM/layout/paint 与表单基础；
- positron_browser.dll 提供 Browser session、history、资源事务和脚本到 Core 的协调；
- 应用宿主只负责 WM 窗口、消息循环、native 控件、网络/线程调度、设备输入和产品策略；
- 所有公开边界都具有稳定 C ABI、UTF-8、opaque handle、明确所有权和真实设备证据，并能
  在 VS2008 / WM6 ARMV4I / C89 约束下长期维护。

长期成功不以测试编号数量衡量，而以真实应用能否通过公共 DLL 完成完整流程、宿主是否保持
轻薄、失败是否安全可解释、以及回归是否能稳定发现用户可感知问题衡量。

## 中期里程碑

### 1. 形成全顶层 DLL 的主干能力覆盖

TLS、JSON、HTTP、Image、Script、Core 和 Browser 都要有明确的主干能力状态。公开入口可以
分阶段实现，但每个入口必须先确定 owner、固定预算、所有权、错误分类和失败不变性；未实现入口
只能以稳定的 unsupported 结果 fail closed，不能用假成功填补矩阵。

### 2. 形成可复用的页面组合基线

用小型、离线、可重复的 compatibility corpus 驱动导航、资源、脚本 session、DOM、表单、
图像、滚动和布局的纵向能力。每项能力都必须同时说明旧页面保留、候选 generation、取消、
失败回滚和资源释放，而不是只增加一个孤立的 JavaScript 方法。

### 3. 保持产品所有权边界

可由其他 WM6 应用复用的 URL、资源事务、DOM、Event、表单、图像、脚本和生命周期语义必须
进入对应公共 DLL 的源文件和公共头文件。test_host 只允许拥有平台接线、调度、fixture、
断言和示例策略；如果宿主代码决定了产品语义，应先迁移再补测试。

### 4. 把有限合同做成可持续的发布基线

每个纵向能力都要有固定容量、失败状态和所有权说明；公共 ABI、构建、设备部署、日志回收、
空间预检和清理必须可重复。自动断言负责语义，视觉、触摸、SIP/IME、picker 和旋转由
可累计的人工矩阵负责；崩溃、数据损坏、严重布局破坏和核心交互阻塞不得延后。

### 5. 让文档反映职责而不是开发流水

根 README 只讲项目入口；架构文档只讲边界；组件 README 只讲调用；测试文档只讲测试合同；
交接只讲当前事实和唯一下一步；限制文档只保留仍未完成的边界。历史批次不再复制到多个
当前文档。

## 当前短期目标与实现指导

当前短期目标是完成“主干能力覆盖”，而不是按测试编号继续堆叠孤立功能：

1. 维护 `docs/CAPABILITIES.md`，为七个顶层 DLL 标注已实现、有界待扩展、宿主职责和暂缓，
   并为每项能力写明入口、预算、失败边界、fixture、设备/人工门和提升条件。
2. 审计公开头文件和导出入口，发现缺失的主干类别时只提出有 owner、有预算、有错误分类的
   `Ex`/size-version 边界；本阶段不声明完整现代 Web API，也不改变旧 ABI。
3. 若要先声明后实现，入口必须在未实现阶段返回稳定 unsupported 类错误，且在返回前不改状态、
   不创建伪 handle、不调用 callback、不产生部分 body 或部分 DOM mutation。
4. 独立 `positron.exe` 的阶段 A 离线消费者已经落地，包含 EXE 内嵌的英语/简体中文资源和
   非目标语言回退；阶段 B 的主文档 HTTP(S) GET 与阶段 1 外部资源纵切已接入：Browser 负责
   generation/resource gate 和页面生命周期，HTTP/TLS 负责公共 transport 边界，应用只负责
   worker、消息泵、窗口、策略和页面 swap。required CSS/`@import` 阻止提交，optional 脚本/图片
   允许 Core fallback。阶段 2 已在 `app_script.c/.h` 建立 EXE 私有 ScriptSession 适配层：
   网络候选按 DOM 顺序执行有界 classic inline/external script，并接入 DOM 读写、属性、有限
   form value、事件、history/fragment、focus、scroll、resize、visibility、任务 checkpoint
   和 teardown；脚本异常不回滚页面，session 初始化失败则关闭脚本能力。阶段 1/2 仍需设备
   网络、失败回滚、stale/cancel 和脚本运行人工门；阶段 3 的第一条宿主纵切也已接入：
   `text`、`password`、`textarea` 使用同一窗口体系下的 native `EDIT`，单选/多选 `SELECT`
   使用 native `COMBOBOX`/`LISTBOX`，checkbox/radio 使用 native `BUTTON`；普通 `type=button`
   保持 Core 绘制，由宿主把点按和 Space/Enter 接到 Browser native-button click transaction。
   带 id、已布局的 contenteditable host 另有纯文本 EDIT 投影，Browser 处理 `beforeinput`/`input`，
   并通过现有 callbacks 同步当前已提交 native EDIT 的 selection；普通编辑不保留富文本子树。Core
   拥有 value/选项/checked/radio-group 状态与几何，Browser 拥有 native edit/select/toggle/button 事件事务，宿主拥有窗口、消息和 teardown；
   mutation 后的 option 集合/标签变化会在 UI 消息返回后按 fingerprint 重建 SELECT，并保留
   EDIT/SELECT 焦点。该部分仍需设备输入、DPI、旋转和软键盘人工门；动态 DOM 插入能力仍受 Browser
   当前有界 mutation callback 限制。阶段 4 已接入 native `type=reset`，并新增 native `type=submit` 的
   click→Core validation→Browser 可取消 submit→Core successful-control URL-encoded GET 路径；EXE 只组合
   GET 目标并调用既有 navigation candidate。另已把 ScriptSession `form.reset()` 接到 Browser 按 id
   派发的 cancelable reset 与 Core state-only reset；活动页 relayout 后，reset 专用 reconcile 同步
   EDIT 值至原有窗口，并由现有路径同步 SELECT/toggle，普通 mutation 不覆盖 EDIT。
   ScriptSession `form.requestSubmit([submitter])` 由 EXE 接到 Core by-id validation/submission
   primitives；direct `form.submit()` 由 Browser direct callback 接到 Core `PCore_FormSubmissionNoValidationById`，并复用同一
   GET candidate；空/相对 action 以候选文档 URL 为基准。direct 方法按合同跳过 validation、submit event 和 submitter。
   离线 controls 表单覆盖 required、命名字段、select、submitter 和带查询的本地路由。
   POST/multipart/dialog、隐式 Enter 和 FormData default action 仍未接入。native 与两种脚本 GET 路径均待设备验收。
   内置离线页不创建 ScriptSession，inline script 不执行；脚本取消和 selection 示例须使用网络
   ScriptSession 页面验收。阶段 4 的脚本提交设备门仍按用户决定等待 RAPI 恢复，不据此写入设备基线。
5. File/Blob→FormData→multipart 仍需真实上传消费者证据：Browser 负责 bounded metadata 和
   对象生命周期，Core 继续负责 wire encoding，宿主只负责同步 file read/free、权限和网络调度。
   没有证据时不进入产品实现。
6. 每个被提升的能力必须先有离线成功/失败不变性/容量/stale/cancel fixture，再运行 C89、正式
   ARMV4I 构建、仓库审计和相称的设备门；`test_host` 只增加接线、fixture 和断言。

短期完成标准是：七个 DLL 的主干状态没有空白项；公开或计划入口都有 owner、预算和失败语义；
独立应用阶段 A（包括英语/简体中文/回退英语语言矩阵）有设备证据，阶段 B 网络候选有明确
owner/预算/回滚和最小 fixture；路线图能指出下一条实现纵切，而不是只写“继续寻找”。

## 当前选择边界

当前已形成的 Browser/Core 基线和设备门事实以 HANDOFF.md 为准。路线图不复制 DOM、表单、
资源或测试编号清单，只保留下一批选择需要的缺口。以下约束在所有候选中都不变：

- `test_host` 的默认配置仍可保持关闭；`positron.exe` 的网络候选已显式启用有界 classic-
  script session，并继续使用固定 heap、source、native-function 和任务预算。离线页面没有
  脚本时不创建 session；不支持类型、资源失败、脚本异常和 bridge 初始化失败都必须安全
  忽略或关闭脚本能力，不得替换旧页面。
- Core、Browser 和宿主的 callback 同步且不可重入；失败必须 fail closed，并保留旧页面或
  旧资源状态。
- 所有容量必须固定且可断言；不能用扩大数组、跳过检查或放宽断言掩盖 WM6 资源问题。
- 不为通用 detached DOM、完整 live collection、完整 HTML parser、Range/Selection、
  MutationObserver、shadow DOM、worker/module 或现代浏览器安全沙箱做无证据的全面扩张。
  `Element.getElementsByTagName()` 的 bounded live collection 是当前唯一已形成的例外；
  其他 collection 仍按各自合同使用 bounded snapshot。
- 代码改动必须兼容 VS2008 / WM6 ARMV4I / C89；公共接口保持 UTF-8、opaque handle 和
  明确内存所有权。

## 当前规划结论

history fallback、Core URL callback 的重复解析和参考宿主的 visibility lifecycle 接线仍按
既有公共边界维护；它们没有把产品语义搬回 `test_host`。本轮已经出现真实的独立应用消费者：
`positron.exe` 用公开 Core/Browser/HTTP ABI 完成阶段 A 的离线导航、绘制、滚动、焦点、有限
history、阶段 B 主文档网络 GET、阶段 1 外部资源事务和阶段 2 classic ScriptSession 基线，
并由 EXE 私有资源提供英语/简体中文 UI；阶段 3 已接入 text/password/textarea 的 native
`EDIT`、单选/多选 `SELECT`（native `COMBOBOX`/`LISTBOX`）、checkbox/radio（native
`BUTTON`）、Core 绘制普通按钮的 click transaction，以及带 id、已布局 contenteditable host 的
纯文本 EDIT 投影和按 id 的 `beforeinput`/`input`。EXE 现已用已有 Browser selection callbacks
接入已提交 native EDIT 的选区读写、CRLF↔逻辑 LF/UTF-16 换算、鼠标/键盘变化通知和
`selectionchange`；此源码接线尚未设备验收，不代表 OEM 真实输入通过。阶段 4 的 native reset、
URL-encoded GET submit、ScriptSession `form.reset()`、`form.requestSubmit()` 与 direct `form.submit()` GET 源码路径均已通过
C89、Debug/Release 构建与审计；设备门仍未完成。POST/multipart、formdata default action、隐式 Enter、富文本与
Range/Selection 对象仍未接入。RAPI 设备传输按用户决定暂停，不据此
宣布设备基线；恢复设备传输后验收 native/script submit 的 validation、取消、GET query 与旧页保留，再取舍
下一个阶段 4 能力。
dialog、SIP/IME、clipboard 或 picker 不因候选排序自动进入实现。
File/Blob→FormData→multipart 仍没有真实应用证据，继续保留在待取证状态。

候选发现仍只允许读取源码、公开头文件、测试 dispatch、组件 README、限制和真实设备日志；
不要把人工输入 backlog 或测试宿主扩展当作产品语义。阶段 A 的语言矩阵、触摸、旋转、DPI、
字体、OEM 硬键盘和失败网络观察仍属于人工/设备门，不能由桌面构建或 synthetic 消息替代。

## 候选队列

### 准备取舍

#### A. 独立应用阶段 B：连续网络导航与页面提交

**状态：准备取舍，阶段 1/2 源码已接入，等待阶段 B/脚本资源设备门。** `positron.exe` 已证明真实应用消费者会组合
Core 的 document/style/layout/paint、链接/焦点几何、Browser history/candidate gate 和 HTTP
transport；当前实现已支持主文档 HTTP(S) 导航，把外部 CSS/`@import`、脚本发现和图片发现
纳入同一事务，并在候选提交前按 DOM 顺序执行有界 classic script。下一步用户结果是确认
真实页面、脚本 mutation/事件/导航、失败、取消或过时响应时保留旧页。

- **Owner：** Browser navigation/resource transaction 与 HTTP/TLS transport；应用只拥有
  worker、WM 消息泵、窗口重绘、配置策略和页面 swap。
- **边界：** 复用 generation、required/optional resource gate、取消、旧页保留和清理快照；
  不在应用中复制 URL、history、资源终态或 multipart 语义。预算沿用公开 Browser/HTTP 上限，
  新增 worker 消息、页面候选和错误摘要必须有固定上限。
- **最小 fixture：** loopback/offline response 的成功 HTML、required stylesheet 失败、
  optional image 失败、取消/过时响应和地址栏失败后旧页保持；同时保留当前两个内置页面回归。
- **门：** Debug/Release ARMV4I 构建、仓库审计、自动旧页/取消断言，以及设备网络、旋转、
  DPI 和真实输入观察；外网不作为唯一证据。

#### B. 独立应用阶段 3：剩余原生控件与输入

**状态：阶段 3 源码接线基本完成，设备门未完成。** `positron.exe` 已有
`AppControlsContext`，把 `text`、`password`、`textarea` 投影为 native `EDIT`，把单选/多选
`SELECT` 投影为 native `COMBOBOX`/`LISTBOX`，把 checkbox/radio 投影为 native `BUTTON`；普通
`type=button` 仍由 Core 绘制，宿主接入点按、Space/Enter 和 Browser click transaction；带 id 且已布局的
contenteditable host 使用多行 EDIT 代理，Core 持有有界纯文本，Browser 接收可取消 `beforeinput`
和按 id 派发的 `input`；EXE 通过已有 callback 同步当前 native EDIT 的 selectionStart/End/Direction、
`setSelectionRange()`，并将鼠标拖选、Shift+方向键和中断收尾通知 Browser。controls 离线资源包含
selectionchange 计数、当前选区报告和脚本全选标记，但离线页不创建 ScriptSession，这些 inline script
不运行；设备人工门仍须确认真实 WM EDIT 的键盘、拖选、
程序化选区和方向。普通编辑可能将 contenteditable 子树压平为纯文本。
动态 option 列表/标签变化也已接入延迟检测与原生重建。阶段 3 device gate 仍需恢复后完成；reset
default action 已另列为阶段 4 纵切。不得把剩余原生交互一次性合并。

- **Owner：** Core/Browser 负责控件状态、事件/default-action 和生命周期语义；应用负责
  WM6 原生窗口、消息、输入法/系统 picker 调度及失败策略；不新增公共 ABI，除非出现
  File/Blob 上传的真实消费者。
- **边界：** native 子控件必须属于同一顶层窗口体系，旧页保留、stale/cancel、隐藏/禁用、
  geometry、DPI、旋转和重复 teardown 都要 fail closed；禁止自绘滚动条替代系统控件。
- **最小 fixture：** `controls` 离线页的文本/密码/多行输入、单选/多选 SELECT 和
  checkbox/radio toggle 成功、退格/Delete/换行、Space/Enter、焦点变化、脚本取消
  beforeinput/click、普通按钮点按及 Space/Enter（不提交）、下拉 commit/cancel、option mutation
  后的标签/数量重建、禁用项、页面切换销毁和旧页保留；contenteditable 另验证原生纯文本输入、
  `beforeinput` 取消、按 DOM id 的 `input`、换行与布局更新，以及 logical LF/UTF-16 选区读写、拖选/
  Shift+方向键方向和脚本 selection setter。富文本、Range/Selection 对象与完整 ClipboardEvent 不属于当前
  接线。reset default action、native submit 与脚本 `form.requestSubmit()` 的 URL-encoded GET
  分属阶段 4 纵切；POST/multipart 与 implicit Enter 仍未接入。
  新增控件必须补相邻失败和容量断言。
- **门：** C89、正式 ARMV4I Debug/Release、仓库审计后，设备人工验收真实键盘、SIP/IME、
  触摸、旋转、DPI、软键和控件销毁；桌面 synthetic 消息不能替代设备证据。

#### C. 独立应用阶段 4：native / ScriptSession submit 与 URL-encoded GET

**状态：native、`requestSubmit()` 与 direct `form.submit()` 的 GET 源码路径及本地门已完成，设备门按用户决定暂缓。**
Core/Browser 已提供有界校验、successful-control snapshot、可取消 submit 顺序与 URL-encoded 编码；
`positron.exe` 的 native submit、ScriptSession `form.requestSubmit([submitter])` 和 direct `form.submit()` 都复用
这些语义，EXE 只组合 GET 目标并调用既有 navigation candidate，不复制验证、事件或字段编码。direct 方法
通过 Core no-validation snapshot 跳过 validation、submit event 和 submitter；空/相对 action 以提交文档 URL
为基准。离线 controls 页覆盖 native required/select/submitter GET，两个脚本方法须用网络 ScriptSession 页面验收。

- **Owner：** Core 拥有 validation 和成功控件快照，Browser 拥有 native click、脚本
  `requestSubmit()` 事务与 direct `form.submit()` 方法语义，EXE 负责激活、URL-encoded GET 请求调度和候选页面提交。
- **边界：** native click/Space/Enter 与脚本 requestSubmit 遵循 validation→可取消 submit→GET；direct
  `form.submit()` 则按 Browser/Core 合同跳过 validation、submit event 与 submitter，目前只接 GET；
  无效值或取消不发请求并保留旧页，网络失败、取消、stale 和解析失败也保留旧页。首批限制为 GET；
  POST、multipart、dialog、隐式 Enter 和 File/Blob 上传另立纵切。
- **最小 fixture：** 离线表单覆盖 native required 控件、submitter 和 URL 参数；Browser/Core 自动合同
  覆盖 requestSubmit 顺序，以及 direct submit 无 validation/event/submitter 的 Core snapshot。网络
  ScriptSession 页面需确认 requestSubmit 的无效值/取消、direct submit 对无效必填值的 GET、空/相对
  action、submitter 排除、最终 URL 和导航失败后旧页保留；内置离线页不执行脚本。
- **门：** C89、正式 Debug/Release ARMV4I 构建与仓库审计已通过；TEST1174 验证 Browser/Core
  `requestSubmit()` 顺序、TEST1175 验证 direct submit Core/Browser 合同，但均不运行 EXE callback。设备恢复后
  仍须验收 native 点击/Space/Enter、requestSubmit 的校验/取消和 direct submit 的跳过语义、GET URL、软键与旧页保留；
  本轮不因 RAPI 暂停而写成设备基线。

### 待取证

#### A. 公共 DLL 消费者缺口审查

**状态：已完成当前审计，未发现 File/Blob 生产消费者。** 这是当前短期的发现和覆盖工作，不是产品实现任务。审查公开头文件、现有组件 README、
`test_host` 的 callback 使用和兼容性 corpus，寻找仍由宿主临时决定、但应由 Core/Browser/HTTP/
Image/Script 拥有的可复用语义。必须记录一个具体调用场景或失败行为，不能只把“现代 API 缺失”
当作缺口。本轮只在 `test_host` fixture 中找到 FormData/File/multipart 的调用；排除宿主、历史、
第三方和生成证据后，新增 `positron.exe` 只使用离线导航、Core paint 和 Browser history，仍
没有生产应用调用 File/Blob/FormData/multipart 上传入口，因此没有可进入该候选的具体阻塞。

已完成的 history fallback 和 Core URL callback 重复规则审查不再属于待实现范围：宿主现在只
读取 Browser 的 history 快照，并通过 HTTP DLL 解析 Core 的 URL reference；创建失败或解析
失败时均 fail closed。其余消费者缺口仍需独立证据，不得因为这两项边界修订而自动扩大到完整
history、URL 或 Web API。

下一步只有在真实消费者或可复现失败出现后，才能给出一个顶层 DLL 所有者、最小 C ABI/Ex 入口、固定预算、失败回滚、最小
fixture、直接相邻回归和设备/人工门；若只发现宿主输入或视觉差异，则转入人工 backlog，不进入
产品批次。

#### B. 资源策略缺口

**状态：待取证。** `absolute URL`、CORS/referrer、loading/fetch-priority 和更完整的网络
资源策略仍在限制文档中，但目前不能仅凭“标准尚未实现”立项。只有真实页面、消费者或可复现
失败证明它阻塞目标应用时，才分别为 Core/Browser/HTTP 划定所有权；网络、缓存、旧页保留和
取消必须保持可解释的 generation/失败合同。外网不可达本身不构成产品回归证据。

#### C. 文件对象与表单消费者缺口

**状态：待取证，且本轮审计未发现生产消费者。** Core 已拥有成功控件 snapshot 和 multipart wire encoding，Browser 的
`FormData` 仍是有界对象/metadata 合同，完整 File/Blob、异步文件读取和浏览器式上传尚未实现。
只有消费者确实需要“从 WM6 文件选择到 multipart body”的完整流程，且能在同步 callback、容量、
权限和取消边界内形成最小 fixture，才进入候选；系统 picker 本身仍属于宿主人工 backlog。
在当前候选中，它是第一优先调查方向，但在证据出现前仍不是“准备取舍”。

### 人工 backlog

这些方向不自动产生 next：

- OEM 键盘、SIP/IME 候选词整词提交、contenteditable 自动重复和跨应用剪贴板；
- native SELECT popup 的真实 OEM 键盘/触摸行为、动态 option 重建、真实 file picker、触摸命中、旋转、DPI、字体、边距、容器居中、表格/列表、
  应用英语/简体中文/回退语言矩阵和失败网络的整体视觉；
- `example.com`/IANA 深层导航、旧页保留等真实网页观察。

它们可以按风险累计后集中验收，但出现崩溃、数据损坏、严重布局破坏或核心交互阻塞时必须立即
复核。人工结果只有在形成可复现、可归属、可有界的公共 DLL 语义缺口后，才转为“待取证”或
“准备取舍”。

### 暂缓

以下方向不作为当前开发目标：完整现代 Web API、通用 detached DOM/Node tree mutation、完整
live collection、MutationObserver、Range/Selection、shadow DOM、worker/module、bfcache、
多窗口持久 history、完整滚动树、pinch zoom、transforms、复杂媒体查询、完整图像 loading、
任意 CORS/安全沙箱和无界 CSS/HTML 兼容。除非目标应用提供新的有界证据，否则保持在限制文档中。

## 长期工作流

### JavaScript 与 Web 组合

- 继续以真实页面缺口扩展有界 DOM、Event、form、navigation 和 script session；不追求一次性
  完整 Web API。
- 优先处理取消、过时导航、生命周期和异步队列的组合顺序；任何新 host object 都要说明
  heap、native-function、source 和执行时间预算。
- 保持脚本 opt-in，缺少 callback、过时 id、不可用 layout 或不支持参数时 fail closed。

### Layout、绘制与字体

- 用真实页面优先修复严重错位、内容不可达、错误滚动和交互几何；不做脱离语料的全面 CSS
  扩张。
- 继续保护 nested overflow 的有限 reveal、坐标换算、layout/paint 资源峰值和失败清理。
- 多 viewport/DPI 截图应与语义断言分开；字体、触摸和 OEM 视觉进入人工矩阵。

### 表单、输入与可访问交互

- 保持 Core form state、Browser event/default-action 和 native control mutation 的事务顺序。
- 依据具体流程选择 dialog、focus、contenteditable、clipboard、SIP/IME、SELECT popup、
  file picker 和旋转缺口；disabled、hidden、stale target 必须 fail closed。
- 自动门证明公共合同，不能替代真实控件和输入法验收。

### 网络、安全与资源

- 维护 CA snapshot、旧 mbed TLS 风险评估、TLS peer identity、错误分类和 listener 资源上限。
- 以 loopback/offline fixture 验证 redirect、取消、失败旧页保留、资源 generation 和清理；
  外网行为不作为唯一证据。
- 只有测量显示收益时才考虑 keep-alive、缓存或性能优化，先保护所有权和失败回滚。

### 公共 DLL 生态

- 保持公开头文件、组件 README、sample、ABI 测试和实际调用方式一致。
- 逐步减少宿主私有桥和重复业务规则；内部静态库继续隐藏在公共 DLL 后。
- 发布前审查第三方版本、许可证、生成步骤和 GPL 组合义务。

## 批次选择规则

按以下顺序取舍：

1. 崩溃、数据损坏、严重布局破坏或核心流程阻塞；
2. 可复用语义仍错误地滞留在 test_host；
3. compatibility corpus 暴露且能形成有界合同的高频缺口；
4. 安全、ABI、所有权、资源或生命周期风险；
5. 有测量证据的性能/内存问题；
6. 其他孤立 API 或观感优化。

选择前必须检查候选是否仍符合当前源码、限制和设备环境。若候选与当前未提交改动重叠，
保留用户改动并重新划分边界；若候选需要新的权限、设备连接或人工决定，先停在取证阶段。
不为“路线图看起来有进度”而预先消耗内部编号，也不把互不相关的能力合并成一个大批次。

## 自动、设备与人工门

每个产品批次至少需要：

- 相关 C 改动先通过 python scripts/test_c89ize.py；
- 使用 scripts\build.bat 或 scripts\stage.bat 的正式 VS2008 配置构建；
- python scripts/audit_repo.py 通过文档结构、UTF-8、test_host 边界和项目输入门；
- 由同一批构建产物运行定向自动门，并保留唯一 TESTBENCH PASS、完整日志、双空间预检、
  完成后清理和 crash_check 证据；
- 设备门继续假定用户已经在 WMDC/Device Emulator GUI 手动连接唯一目标，脚本不连接、选择、
  cradle、重置或强杀设备；
- 视觉、触摸、SIP/IME、picker 和旋转可以累计后人工检查，但崩溃、数据损坏、严重布局破坏
  或核心交互阻塞必须立即人工复核。

准备 nightly 或里程碑时，再运行更宽的自动 checkpoint；多次低风险批次、公共 ABI/所有权/
生命周期变化、layout/paint/输入/网络基础设施变化或无法解释的超时、混包、崩溃和数据错误
也会触发更宽检查。

## 完成与交接标准

一个批次只有同时满足以下条件才算完成：

- 能力对应一个完整的用户/消费者结果，并由正确的公共 DLL 拥有；
- 公共 ABI、UTF-8、opaque handle、所有权和 VS2008/WM6/C89 约束没有退化；
- 自动断言覆盖成功、失败不变性、容量和生命周期；
- 定向门及直接相邻回归唯一 PASS，零 ERROR/FAIL，设备证据可追溯；
- 所需人工门完成，或明确进入允许累计的人工 backlog；
- HANDOFF.md 覆盖为当前快照，KNOWN_LIMITATIONS.md 删除已完成边界并保留未完成边界；
- 只有长期读者需要知道的稳定行为才更新 README、架构或测试文档；
- 每轮开发结束都复核本路线图：已完成或过时的候选必须移除或降级，仍有效的候选必须更新状态，
  有证据的新方向才可加入；若无需改动，也要在 HANDOFF 的交接检查中明确记录已复核；
- 只提交本批 tracked 文件并推送当前分支，临时截图、日志和 tmp/ 不入 Git。

完成后，候选从本路线图移除或改写为新的未完成边界；不得把完成段落重新追加回来。下一批
从 backlog 重新取证和选择，而不是沿用旧编号或上一批的实现细节。

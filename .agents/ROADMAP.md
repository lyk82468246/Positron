# Positron 路线图

本文件只描述尚未完成的目标、候选能力和选择规则。当前产品事实见
[HANDOFF.md](HANDOFF.md)，仍存在的边界见 [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md)，
稳定的架构与公共 DLL 所有权见 [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)。已经
完成的批次不在这里建立时间线；具体实现由 Git 保存，只有会影响未来取舍的失败实验才进入
[FAILED_EXPERIMENTS.md](FAILED_EXPERIMENTS.md) 或 docs/history/。

## 如何使用这份路线图

路线图不是任务日志，也不是 API 清单。每次接管时先读交接、限制和路线图，再用源码、
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

### 1. 形成可复用的页面组合基线

用小型、离线、可重复的 compatibility corpus 驱动导航、资源、脚本 session、DOM、表单、
图像、滚动和布局的纵向能力。每项能力都必须同时说明旧页面保留、候选 generation、取消、
失败回滚和资源释放，而不是只增加一个孤立的 JavaScript 方法。

### 2. 保持产品所有权边界

可由其他 WM6 应用复用的 URL、资源事务、DOM、Event、表单、图像、脚本和生命周期语义必须
进入对应公共 DLL 的源文件和公共头文件。test_host 只允许拥有平台接线、调度、fixture、
断言和示例策略；如果宿主代码决定了产品语义，应先迁移再补测试。

### 3. 把有限合同做成可持续的发布基线

每个纵向能力都要有固定容量、失败状态和所有权说明；公共 ABI、构建、设备部署、日志回收、
空间预检和清理必须可重复。自动断言负责语义，视觉、触摸、SIP/IME、picker 和旋转由
可累计的人工矩阵负责；崩溃、数据损坏、严重布局破坏和核心交互阻塞不得延后。

### 4. 让文档反映职责而不是开发流水

根 README 只讲项目入口；架构文档只讲边界；组件 README 只讲调用；测试文档只讲测试合同；
交接只讲当前事实和唯一下一步；限制文档只保留仍未完成的边界。历史批次不再复制到多个
当前文档。

## 当前选择边界

当前已形成的 Browser/Core 基线和设备门事实以 HANDOFF.md 为准。路线图不复制 DOM、表单、
资源或测试编号清单，只保留下一批选择需要的缺口。以下约束在所有候选中都不变：

- 默认不执行浏览器 JavaScript；启用脚本仍是明确 opt-in 的有界 classic-script session。
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

next869 已完成 Browser 特殊字符串 registry 与 dataset snapshot 的公共 DLL 纵切，相关
Browser/Core 基线和设备证据以 [`HANDOFF.md`](HANDOFF.md) 为准。对现有源码、测试入口和已知
限制进行整理后，当前没有一张能够直接进入产品实现的“准备取舍”候选卡；这不是缺陷，也不
意味着可以随意扩大 Web API。下一步应先完成一次候选发现审查，再决定是否分配新的 next。

本轮候选发现只允许读取源码、公开头文件、测试 dispatch、组件 README、限制和真实设备日志，
不得顺手修改产品代码。若审查仍没有可复现的公共 DLL 缺口，就保持路线图的空实现队列，优先
执行累计人工验收或维护发布基线。

## 候选队列

### 准备取舍

当前为空。已有的 Browser/Core、FormData、Storage、Headers 和特殊键 registry 合同已经取得
相邻设备门；没有新的用户失败、消费者需求或源码缺口同时满足候选卡的五项要求。下一次实现
批次必须先把某一张“待取证”卡提升到这里，不能从已完成测试编号顺延出功能。

### 待取证

#### A. 公共 DLL 消费者缺口审查

**状态：待取证。** 这是发现工作，不是产品实现任务。审查公开头文件、现有组件 README、
`test_host` 的 callback 使用和兼容性 corpus，寻找仍由宿主临时决定、但应由 Core/Browser/HTTP/
Image/Script 拥有的可复用语义。必须记录一个具体调用场景或失败行为，不能只把“现代 API 缺失”
当作缺口。

取证完成的条件是：给出一个顶层 DLL 所有者、最小 C ABI/Ex 入口、固定预算、失败回滚、最小
fixture、直接相邻回归和设备/人工门；若只发现宿主输入或视觉差异，则转入人工 backlog，不进入
产品批次。

#### B. 资源策略缺口

**状态：待取证。** `absolute URL`、CORS/referrer、loading/fetch-priority 和更完整的网络
资源策略仍在限制文档中，但目前不能仅凭“标准尚未实现”立项。只有真实页面、消费者或可复现
失败证明它阻塞目标应用时，才分别为 Core/Browser/HTTP 划定所有权；网络、缓存、旧页保留和
取消必须保持可解释的 generation/失败合同。外网不可达本身不构成产品回归证据。

#### C. 文件对象与表单消费者缺口

**状态：待取证。** Core 已拥有成功控件 snapshot 和 multipart wire encoding，Browser 的
`FormData` 仍是有界对象/metadata 合同，完整 File/Blob、异步文件读取和浏览器式上传尚未实现。
只有消费者确实需要“从 WM6 文件选择到 multipart body”的完整流程，且能在同步 callback、容量、
权限和取消边界内形成最小 fixture，才进入候选；系统 picker 本身仍属于宿主人工 backlog。

### 人工 backlog

这些方向不自动产生 next：

- OEM 键盘、SIP/IME 候选词整词提交、contenteditable 自动重复和跨应用剪贴板；
- native SELECT popup、真实 file picker、触摸命中、旋转、DPI、字体、边距、容器居中、表格/列表
  和失败网络的整体视觉；
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
- 只提交本批 tracked 文件并推送当前分支，临时截图、日志和 tmp/ 不入 Git。

完成后，候选从本路线图移除或改写为新的未完成边界；不得把完成段落重新追加回来。下一批
从 backlog 重新取证和选择，而不是沿用旧编号或上一批的实现细节。

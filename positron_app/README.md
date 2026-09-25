# Positron 浏览器应用

`positron_app` 是 Positron 的独立 WM6 Professional 应用消费者，输出固定名称
`positron.exe`。它只通过 `positron_core.dll`、`positron_browser.dll` 和
`positron_http.dll` 的公开 import library 访问产品能力；WM6 窗口、地址栏、Shell command
bar、菜单、输入优先级和页面导航策略属于应用。

## 当前阶段 A/B 范围

当前版本提供一个内置离线欢迎页和一个键盘/焦点验收页：

- 使用标准 WM6 caption 和 `SHCreateMenuBar` softkey command bar；左 softkey 为 `Back`，
  右 softkey 打开原生菜单，菜单包含前进、主页、地址栏、刷新和明确退出；
- caption 下只有一行紧凑 native EDIT 地址栏；Enter 提交，Escape 恢复最近一次已提交地址；
- 内置离线页使用应用私有地址 `positron://welcome` 与 `positron://controls`，只解析这两个
  嵌入页面路由；地址栏仍接受 `welcome`/`controls` 快捷输入，外部网页继续使用 HTTP(S)。
- Core 负责 HTML/CSS 解析、style、layout 和 GDI paint；页面支持垂直/水平滚动；
- Browser DLL 负责应用使用的有界 history handle；失败的导航不会替换当前页面；
- 地址栏和页面链接支持绝对 HTTP(S) URL。主文档请求在 worker 中通过
  `positron_http.dll` 执行，Browser candidate/resource transaction 负责 generation、取消、
  stale 和 required-document commit gate；网络页面只有在 Core 完成 parse/style/layout 后才替换
  当前页面；请求失败时保留旧页面；
- 页面空白点击不会关闭窗口；页面链接可用触摸或鼠标点击激活；页面焦点可用
  Up/Down/Enter 操作，Backspace 保留给 native 地址栏编辑。

网络页面的外部 CSS/`@import` 属于 required 资源，图片和 classic script 属于 optional
资源；它们都在同一个 Browser candidate/resource transaction 中发现、下载和释放。网络
候选现在会创建 EXE 私有 `AppScriptContext`，按 DOM 顺序执行有界的 inline 与已下载的
external classic script；脚本异常不回滚已解析页面，session/bridge 初始化失败则关闭该
候选的脚本能力而继续走页面提交。脚本可以使用当前已接入的 DOM 读写、属性、有限表单值、
事件监听/取消默认动作、history/fragment 导航、focus、visibility、resize、scroll、timer
和页面 teardown 生命周期桥。`text`、`password` 和 `textarea` 已由同一窗口体系下的
native `EDIT` 子控件承载；单选和多选 `SELECT` 分别使用原生 `COMBOBOX`/`LISTBOX`；
checkbox/radio 使用同一窗口体系下的 WM6 `BUTTON` 子控件。有界 DOM mutation 改变 option
集合/显示标签后，宿主在脚本 callback 返回后的 UI 消息中检测 fingerprint，延迟重建原生
SELECT，并保留正在编辑的 EDIT 与 SELECT 焦点。Core 仍拥有 value、选项状态、
checked/radio-group 状态和几何，Browser native-edit/native-select/native-toggle bridge
负责输入、选择、可信 click、input/change 和 focus 事务，宿主负责 WM6 消息、重排和销毁。
Core 绘制的普通 `type=button` 也已接入：点按命中后（有 ScriptSession 时）经 Browser 派发
click，按下 Space/Enter 可激活当前按钮；不额外创建 WM6 子窗口。Native-button transaction 和
Core click callback 复用于 toggle 路径。Core 绘制的 `type=reset` 按钮也已接入：Browser
依次处理可取消的 click/reset 事务，获准后 Core 恢复表单初值，宿主重建 native 控件以同步
EDIT、SELECT 和 toggle；不增加 DLL ABI。`type=submit` 按钮现在也复用同一激活路径：Core
执行约束校验并生成成功控件数据，Browser 在有效时派发可取消的 submit，EXE 目前只接入
URL-encoded GET，再交给既有导航候选；校验失败、事件取消、目标过长或候选失败都不会替换旧页。
网络 ScriptSession 还接入 `form.requestSubmit([submitter])`：Browser 保持 validation→可取消
submit→默认动作顺序，EXE 只读取 Core 的成功控件快照并将 URL-encoded GET 目标交给同一导航候选；
空 action 和相对 action 以当前文档 URL 为基准。脚本 `form.submit()` 也已通过 Browser direct-submit
callback 接到 Core 的 no-validation successful-control snapshot，并复用同一 GET 导航候选；它按合同
跳过校验、submit 事件和 submitter。POST、multipart、dialog 和隐式 Enter 提交仍未接入。
网络页面的 ScriptSession 另已接入带 id 表单的 `form.reset()`：Browser 按表单 id 派发可取消
reset 事件，获准后由 Core 恢复初值；活动页面随后重新 layout，并在 UI 消息中 reconcile
native 控件：SELECT/toggle 沿用现有 Core 同步，EDIT 值只在 reset 专用路径写回现有窗口；
表单结构未变时不因值同步而重建控件并保留焦点。若 reset 事件处理器改变结构，仍按通用
reconcile 规则处理。普通 DOM mutation 不会因此覆盖用户正在编辑的 EDIT。内置离线页不创建
ScriptSession，因此该脚本方法只能在启用脚本的网络页面上验收。
内置 controls 页面有离线 GET 表单，提交成功会在地址栏显示编码后的查询并重新载入该页。
带 id 且已布局的 `contenteditable` editing host 现在也
投影为同一窗口体系下的原生多行 EDIT；Core 保存最多 8192 UTF-8 字节的纯文本，Browser 的
`beforeinput` 可取消输入，并按 DOM id 派发接受后的 `input`。离线 controls 页的
`plaintext-only` 示例展示控件外观；但内置离线页不创建 ScriptSession，因此 HTML inline script
不执行，脚本驱动的取消、状态文字和 selection 示例尚不能在离线页验收。网络页有 ScriptSession
时，原生 caret/selection 才通过现有
Browser callbacks 同步：仅当前已提交页面中已物化的带 id EDIT 提供 getter/setter，WM EDIT 的
CRLF 索引会换算为 Browser 使用的逻辑 LF/UTF-16 偏移；鼠标拖选、Shift+方向键、焦点/捕获结束
通知 Browser，并由 Browser 去重 `selectionchange`。脚本的 `selectionStart`/`selectionEnd`/
`selectionDirection` 与 `setSelectionRange()` 可同步到 native EDIT；候选页或失效 EDIT 不提供原生选区，
Browser 保留其有界脚本回退；本批没有新增 ABI。将普通 `contenteditable` 编辑提交到 Core 时仍会
以纯文本替换其子树，不支持富文本编辑；Range/Selection 对象不在本批范围，设备端 OEM 键盘、
触摸和视觉尚待验收。脚本 `form.reset()` 的 native 控件同步、脚本 `requestSubmit()` 与 direct
`form.submit()` 路径仍未设备验收；POST/multipart/dialog、隐式 Enter 与提交期 FormData default action
仍未接入；SIP/IME、文件选择器、
书签、持久偏好和 WM6 Standard 仍未接入；完整 ClipboardEvent/async clipboard 也不在范围内；缺少
`positron.ini` 不影响启动，当前没有需要用户编辑的
配置项。

## 界面语言

启动时从 WM6 的 UI 语言选择 EXE 私有资源：简体中文（中国大陆、新加坡）使用 `zh-CN`，
其他语言统一使用 `en-US`。菜单、softkey、状态栏标题、启动错误框以及 welcome/controls
两个离线页面都随该选择切换；资源直接嵌入 `positron.exe`，stage 目录不需要语言文件。
语言在进程启动时确定，设备语言改变后需要重启应用。Browser DLL 的
`navigator.language` 等语义不在本应用批次内修改。

## 构建与运行

从仓库根目录使用正式入口：

```bat
scripts\build.bat Debug rebuild
scripts\stage.bat Debug C:\WMShare\Positron-app
```

stage 目录中运行 `positron.exe`。同目录必须保留本次构建对应的七个公共 DLL 和
`fonts\`；不要把 `test_host.ini` 当作应用配置，也不要从不同 stage 目录混用 DLL。

## 阶段 A/B 验收

在 WM6 Professional 设备或模拟器上确认：

1. 在英语设备和简体中文设备上分别直接启动 `positron.exe`，不出现测试选择界面，确认欢迎页、
   地址栏、softkey、菜单和状态栏标题使用对应语言；在其他语言设备上确认回退英语；
2. 点按对应语言的键盘与焦点页面链接，再用 Back/Home/Menu 返回或退出；
3. 在页面区域点空白，窗口仍保持打开；拖动滚动条或使用方向键/PageUp/PageDown，页面
   位置随之改变；
4. 只用硬键盘/方向键时，用 Tab 经过 native 控件；页面获得焦点后用 Up/Down 选择链接、
   Enter 激活；
5. 地址栏中输入 `positron://controls` 或 `positron://welcome`，按 Enter 导航；确认 `controls`/`welcome`
   快捷输入仍可用；在 `controls` 页分别点击或用
   Tab 进入文本、密码和多行文本框，输入、退格、Delete、Enter/换行并离开焦点，确认页面
   值、光标焦点和滚动位置保持一致；再分别操作单选和多选 SELECT 以及 checkbox/radio，
   确认选择、checked 状态和 radio-group 规则回写页面，Space/Enter 不产生重复切换；点按普通
   按钮后再按 Space/Enter，确认不导航也不提交；在 URL-encoded GET 表单中先清空必填项并激活
   submit，确认地址和页面不变；填入值后激活 submit，确认页面重新加载且地址栏包含 Core 编码的
   `q`、`scope` 和 submitter `mode` 参数。再修改 reset 示例的文本、SELECT 和 checkbox，激活重置按钮，
   确认初值恢复。内置离线页面不执行 inline script；`beforeinput`/可取消 submit、脚本选区和
   option mutation 应在启用 ScriptSession 的网络页面上另行验收。滚动、旋转和页面切换后没有残留
   native 控件；
6. 编辑地址栏时按 Backspace 删除，按 Escape 取消编辑并恢复已提交地址；
7. 菜单中的 `Exit`/`退出` 真正结束应用，重复启动/关闭不新增崩溃。
8. 在设备网络可用时输入绝对 `http://` 或 `https://` 地址；加载期间旧页面保持可见，
   成功后才替换页面。检查一个包含 inline/classic external script 的页面：脚本 DOM
   mutation、事件监听和 timer 在提交后生效，并让 click listener 更新页面确认按钮事件到达；
   对有效 native submit button 检查 click→validation→submit 顺序，并用 click/submit
   `preventDefault()` 确认取消后没有 GET 请求；对带 id 的脚本表单调用
   `requestSubmit()`，检查 required 校验、取消 submit 不发请求、显式 submitter 参数及相对 action
   生成的 URL-encoded GET；调用直接 `form.submit()` 时确认它跳过 validation、submit 事件和 submitter，
   即使 required 字段无效也只把 Core 成功控件数据导航为 GET；候选失败仍保留旧页。尝试 native/script
   POST 表单确认其安全拒绝，确认 direct submit 的 POST/multipart/dialog 同样 fail closed。脚本错误、optional script/image 失败、
   取消或输入另一个地址时不显示半成品页面，旧页面仍可用。对带 id 的脚本表单调用
   `form.reset()`，分别验证 reset 事件以该表单为 target、`preventDefault()` 保留原值，以及
   允许默认动作后 Core 值和 native 控件恢复初值。

真实设备的触摸命中、SIP、旋转、DPI 和 OEM 键盘行为仍属于人工验收；本阶段不把桌面
构建或 synthetic 消息当作这些门的替代证据。

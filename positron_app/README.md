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
和页面 teardown 生命周期桥。真实表单 native 控件、form submit/formdata、SIP/IME、文件
选择器、书签、持久偏好和 WM6 Standard 仍未接入；缺少 `positron.ini` 不影响启动，当前
没有需要用户编辑的配置项。

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
5. 地址栏中输入 `controls` 或 `welcome`，按 Enter 导航；编辑时按 Backspace 删除，
   按 Escape 取消编辑并恢复已提交地址；
6. 菜单中的 `Exit`/`退出` 真正结束应用，重复启动/关闭不新增崩溃。
7. 在设备网络可用时输入绝对 `http://` 或 `https://` 地址；加载期间旧页面保持可见，
   成功后才替换页面。检查一个包含 inline/classic external script 的页面：脚本 DOM
   mutation、事件监听和 timer 在提交后生效；脚本错误、optional script/image 失败、
   取消或输入另一个地址时不显示半成品页面，旧页面仍可用。

真实设备的触摸命中、SIP、旋转、DPI 和 OEM 键盘行为仍属于人工验收；本阶段不把桌面
构建或 synthetic 消息当作这些门的替代证据。

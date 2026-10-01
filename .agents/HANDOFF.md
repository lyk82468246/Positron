# 当前交接

本文件只保留接管当前工作所需的事实、证据、风险和唯一下一步。稳定能力合同见
[docs/CAPABILITIES.md](../docs/CAPABILITIES.md)、[docs/TESTING.md](../docs/TESTING.md)、
[docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md) 及组件 README。已完成批次与历史部署由 Git
和本地日志保存；这里不重复测试流水账。

## 项目使命

Positron 为 Windows Mobile 6 / Windows CE 5.2 ARMV4I 提供模块化 TLS、JSON、SQLite DB、HTTP、
图像、媒体、Script、Core 与 Browser DLL，以及独立消费者 positron.exe。公共边界保持稳定
C ABI、UTF-8、opaque handle、固定预算和明确所有权。test_host 只拥有平台接线、fixture 和断言。

## 当前里程碑

中期里程碑是把公共 DLL 和 WM6 应用组合为可持续浏览、输入、导航的有界运行时。当前纵切修复
WinWorld 窄视口 hamburger 的点击失效和按钮作者内容保留；没有增加站点特判或脚本预算。

本批已取得匹配设备回归和真实应用自动点击证据，尚缺用户对当前新窗口的真实点按和视觉确认。
不能把离线门或属性变化直接写成最终界面验收。

## 根因与当前修复

点击失效的直接原因在应用 AppScript 的 callback 接线：app_script_get_relation 错误地要求
out_bytes 和 out_number 同时非空。Browser 的数值关系调用以 NULL out_bytes 请求计数，
字符串关系以 NULL out_number 做长度探测/复制，因此应用把合法请求全部拒绝。

修复前的真实页面探针记录 direct=true;query=false;all=0;bodykids=0;barkids=0：
getElementById 能找到 navbarNav，而 querySelector 和 children 无法遍历到它。Bootstrap 已执行，
按钮也命中 native-button transaction，但 delegated handler 得不到目标，因此不会修改菜单。

app_script.c 现接受合同规定的可选输出；Browser 头文件明确写出 NULL 输出规则，公开 ABI 不变。
这是宿主 callback 适配修复，DOM 查询语义仍由 Core/Browser 拥有。

Core 同时补齐了真实 button 的定位 inline 构造缺口：display:inline 加 absolute/fixed 的按钮
优先保留作者子树和 form gadget，form-control 兜底也不再为真实 button 合成 Button 文本。
TEST1328 分别断言 flex、block、定位 inline 三个区域的图标像素，避免一个区域通过掩盖其他区域丢失。

## 本批文件

- positron_app/app_script.c：关系 callback 参数修正，以及 Debug-only 事件/属性取证日志。
- positron_browser/positron_browser.h：关系 callback 的可选输出合同注释。
- positron_core/pcore_box.c：visual-button 判定、定位 inline 与兜底构造的作者子树保留。
- test_host/main.c：TEST1327 使用 type=button、嵌套的初始 display:none 菜单和原版脚本；
  TEST1328 分区像素断言与定位按钮。
- test_host/image_tests.c：TEST1329 使用显式 type=button，验证精确 Bootstrap SVG 从 CSS 到最终像素、
  重排后的按钮与 span 几何。
- docs/TESTING.md、ROADMAP.md 和本文件：同步测试合同、当前证据及待验收项。

这些改动均属于当前纵切；不应恢复成旧的输出指针检查或用 test_host 的通过替代应用证据。

## 已验证证据

### 源码与构建

C89 检查和仓库审计已通过。正式 Debug 构建的 Core、应用、test_host 均为零错误；
Core/test_host 保留已有 libcss 转换警告。正式 Release 构建中三个工程也编译链接成功，
但完整解决方案仍在 CabWiz 的 Data files could not be created 处失败，不能写成全量 Release 通过。

### 匹配设备回归

本地证据：tmp/device-runs/20261001-215056-bootstrap-hidden-target/。

同一 Debug ARMV4I 包的 TEST1327、1328、1329、999 全部 OK，selected/observed=4/4，
唯一 TESTBENCH PASS、零 ERROR/FAIL、core_module_check=PASS、crash_check=PASS、无新增 dump。
目标为 480x640、DPI 192；外置卡和内部对象存储预检均通过。

远端目录为：
`\Storage Card\Temp\Positron-device-gate\bootstrap-hidden-target-20261001-215056`。
目录因 PreserveDeployment 保留，门的总标签为 DIAGNOSTIC_ONLY；测试与模块检查均通过，
不能把该标签误报成门已清理并正式归档。

### 真实应用自动点击

修复前探针：tmp/device-runs/debug-capture-20261001-214649/positron-debug.log。
修复后证据：tmp/device-runs/debug-capture-20261001-215206/positron-debug.log，
PID 3599644494，以 --url https://winworldpc.com/home --click .navbar-toggler 启动。

修复后同 PID 日志证明：

- jQuery、Popper、Bootstrap 均 executed；heap peak 2651504/3145728，没有增加预算。
- Core 普通按钮 kind=9，真实坐标事务命中并进入 document delegated click。
- 按钮 aria-expanded 被设为 true；navbarNav 由 collapsing 最终变为 navbar-collapse collapse show。
- 两次 script-refresh complete result=0，应用执行了提交后的重排。

这证明生产页面查询、点击、mutation、timer 和重排链条已运行，仍不能单凭日志证明最终图标外观。
bootstrap-multiselect 的 SyntaxError(line 912) 是独立限制，未在本批处理。

当前设备上保留并启动的是：
`\Storage Card\Temp\Positron-device-gate\debug-capture-20261001-215206\positron.exe`。
自动点击已展开菜单；用户随后应点击同一按钮检查收起，再点击检查展开。

## 其他有效基线与边界

- HTTP URL-aware Ex API 和 final-URL 查询已接入应用与真实网络 test_host 路径；HTTPS 不静默降级，
  fragment 不进入请求，userinfo、IPv6、非法端口/scheme 和超限输入 fail closed。旧 ABI 保持。
- Core 图片 Ex callback 区分 READY/PENDING/TERMINAL_FAIL；pending 不写终态失败 cache。
  CSS percent/base64 SVG data URI 有独立 URL/decoded/复杂度预算，复用 Image cache/paint。
- Image 的有限 class style、viewBox 自然尺寸、rgba stroke、alpha 与 round/square linecap 已有
  fixture/设备证据；真实 IANA 页面和不同 DPI 的应用视觉仍按人工矩阵确认。
- 应用 retained-pixel 滚动、nested overflow、动态 scrollbar、native EDIT/SELECT/toggle、
  picker、reset/submit 和 ScriptSession 有源码接线；SIP/IME、旋转、OEM 按键和真实网络回滚仍须设备/人工门。
- TEST232/263/1310 已有用户通过记录，不无故重跑；TEST262/264 与 TEST293 的既有表单限制仍独立保留。
- Media 仅 WAV PCM callback/AUTO smoke 有 WM6 Emulator 证据，压缩音视频实时播放、
  WaveOut underrun 和 DirectShow source filter 尚未验收。
- DB 本地/同步 contract 和 REST fixture 已有；HTTP worker、401/5xx/分页/重试及设备 journal/
  断电恢复未形成基线。
- 脚本 File/Blob→Core multipart、完整 module/Shadow DOM、富文本 Range/Selection 等仍受
  KNOWN_LIMITATIONS.md 约束。WinWorld bsky-embed 不属于当前 Image/HTTP 缺陷。
- 页面横向滚动条几乎铺满轨道的既有现象按用户决定暂缓。

## 设备与取证纪律

WMDC 连接由用户在 GUI 手动完成，只使用当前唯一目标。此次连接已恢复且 RAPI 可用；
不要继续把先前的 CeRapiInitEx 超时写成当前阻塞。门默认不连接、cradle、重置或结束设备进程。
显式 ForceTerminatePositron 可运行无产品 DLL 依赖的 helper，精确结束 positron.exe 或本门
test_host-run-*；本次已记录清理成功。

外置卡 Temp 优先、内部 Temp 回退；检查目标卷和内部对象存储，完整回收日志后才能清理旧部署。
已存在的诊断目录不要直接覆写：本次 debug_capture 的 CeMoveFile 对已存在目标返回 device=5，
改用新目录部署成功。

tmp 当前没有可读的 png/jpg/bmp 截图。用户告知有新截图时再查找和读取；不能假称已看到图片。
Debug 日志按 PID 和包路径核对，旧目录或旧门不能证明新二进制。

## 文档与路线图复核

本轮已复核 ROADMAP：点击 callback 缺口和定位按钮回归取得自动证据后，剩余目标收束为当前
应用的真实触摸/键盘/视觉门。无新公共 API 候选。TESTING 同步隐藏 collapse、分区像素断言和
宿主 callback 合同；此前大量部署流水从当前交接移除，证据保留在 Git 与 tmp/device-runs。

## 唯一下一步

在当前已经打开的 WinWorld 新窗口中，确认按钮仍为三条汉堡线，点击应收起菜单，再点击应展开。
若视觉或真实点击失败，用户保存一张新截图并告知；继续回收 PID 3599644494 的 Debug 日志，
核对属性变化和 refresh，不重复假设脚本没有执行或盲目增加 heap。

真实任务确认通过后再关闭本回归；在此之前只能声明自动链条恢复。正式发布仍另受完整 Release
CabWiz 门约束，本批不自动发布 nightly。

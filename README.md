# Positron

Positron 为 Windows Mobile 6 Professional / Windows CE 5.2 补齐现代网络、数据、
图像、脚本和网页运行能力。项目既提供可被普通 WM6 C/C++ 程序独立使用的 DLL，也在这些
DLL 之上建设轻量浏览器与应用运行时。

项目仍处于持续开发阶段。基础 DLL 和一批浏览器纵向能力已经在 WM6 ARMV4I 模拟器或设备上
验证，但 Positron 还不是完整、兼容现代 Web 标准的通用浏览器。

## 能力概览

| 组件 | 用途 | 当前边界 |
|---|---|---|
| `positron_tls.dll` | TLS 1.2、证书链和主机名验证、嵌入式 CA bundle | 基于固定版本 Mbed TLS 2.16.12；不安全连接仅供诊断 |
| `positron_json.dll` | UTF-8 JSON 解析和序列化 | cJSON 1.7.18 的稳定 opaque-handle C ABI |
| `positron_http.dll` | HTTP/1.1 GET/POST、进度回调、重定向 | HTTPS 使用 Positron TLS，明文 HTTP 使用 WinInet |
| `positron_image.dll` | BMP/PNG/JPEG/GIF、SVG、像素缓冲和编码 | 设备位图格式依赖 WM Imaging codec；SVG 是受限子集 |
| `positron_script.dll` | 独立 JavaScript 执行服务 | Duktape 2.7.0；有时间、内存、源码和 native callback 上限 |
| `positron_core.dll` | HTML/DOM、CSS、布局、绘制、命中、表单和资源发现 | 基于移植的 NetSurf 3.11 组件；网页兼容性仍在扩展 |
| `positron_browser.dll` | 浏览器 session、history、脚本 session/bootstrap、受控 DOM/Event、表单/输入、URL、storage、编码、Headers、同步 Request/Response、AbortSignal、timer/message pump、端口/广播、性能快照和窗口别名数据模型 | 不拥有窗口、网络或原生校验提示；这些 Web API 是 session 内 bounded 兼容切片，完整 DOM、Promise/fetch/stream、core 事件传播及控件副作用仍由宿主提供 |

所有公共接口都使用稳定 C ABI、UTF-8 字符串、opaque handle 和明确的释放函数。NetSurf、
Duktape、Mbed TLS 等实现细节不暴露给调用者。

### 浏览器运行时

`test_host.exe` 是回归宿主和示例浏览器消费者，不是发布时的浏览器运行时。它已经接通：

- verified HTTPS 与明文 HTTP 页面加载；
- HTML/CSS 解析、外部样式和图片资源、分阶段异步抓取；
- GDI 绘制、滚动、动态 viewport/DPI、横竖屏重排；
- 常见 block、inline、flex、table、list、图片和基础 positioning；
- 链接导航、有限历史、表单控件、文本输入和一组 DOM 事件；
- 显式开启时的 classic inline/external JavaScript 与受限 DOM/Event/location/history bridge。

浏览器 JavaScript 默认关闭。`positron_script.dll` 是独立的 JavaScript 引擎封装；浏览器
运行时由 `positron_browser.dll` 与 `positron_core.dll`、`positron_script.dll` 及宿主回调
组合。目前 history/session、浏览器脚本 context 的所有权、host JSON callback 注册、browser
bootstrap、DOM 只读（按 id 查询与 textContent 读取）、textContent 写入、attribute、input value、checked、
disabled、表单属性（含 `name`/`action`/`method`/`enctype`/`target`/`autocomplete`/`acceptCharset`）与 submitter `formAction`/`formMethod`/`formEnctype` 反射、控件属性 `placeholder`/`autocomplete`/`inputMode`/`type`、约束属性（含 `pattern`/`minLength`/`maxLength` 反射）、控件与受限 form-level `checkValidity()`/`reportValidity()`/`willValidate`/`validity` 查询、
`setCustomValidity()`/`validationMessage`（含固定英文内置 fallback，不做本地化）、form property（defaultValue/defaultChecked/selectedIndex）、navigation JSON 分发、同文档
location/history 事件分发、event 回调分发、native input/composition、keyboard、focus-family、
EDIT change/post-change input、click、programmatic `HTMLElement.click()`（file input 只到 typed click，系统 picker 仍由宿主 GUI 负责）、submit/reset、invalid/reportValidity、file-input input/change、checkbox/radio input/change 和 SELECT input/change typed dispatch entry 已迁入；其余 form/input 适配、core 事件传播以及窗口、网络、控件和
history/navigation side effect 仍由宿主提供。
`reportValidity()` 只执行当前受支持的约束查询并派发可寻址控件的 `invalid` 事件；它不显示
原生提示、不自动聚焦/滚动，也不提交表单。
当前脚本 session 还提供受限的 `dataset`、FormData/Headers/Storage/DOM iterator、同步
Request/Response JSON 与 one-shot body、clone ownership、Blob/File metadata/slice/JSON、URL
authority/default-port 与 URLSearchParams pair/delete-value/按值查询和 mutation-safe snapshot、
cookie Max-Age 删除、TextEncoder/TextDecoder 选项、AbortSignal 静态工厂和 tags、setImmediate、
MessagePort/BroadcastChannel、structuredClone、PerformanceObserver/EntryList 快照与选项校验、
navigator 方法、`screen.orientation`、window aliases/open-close no-op 和稳定 element wrapper
identity；这些能力不发起网络、不创建后台线程，并由宿主显式 timer/message pump 或同步 snapshot
驱动。
它不是第二套引擎。
两者的关系和所有权见
[架构说明](docs/ARCHITECTURE.md)。

## 快速开始

### 前置环境

- Visual Studio 2008 SP1
- Windows Mobile 6 Professional SDK
- WM6 Professional Emulator 或兼容 ARMV4I 设备
- Python 3（用于移植脚本和仓库审计）

微软工具链和设备镜像不能随仓库分发，必须单独安装。

### 构建

在仓库根目录运行：

```bat
scripts\build.bat Debug build
```

正式脚本会调用 VS2008 的解决方案配置：

```text
Debug|Windows Mobile 6 Professional SDK (ARMV4I)
```

不要绕过解决方案直接拼装工具链。完整参数、输出位置和常见故障见
[构建与部署](docs/BUILDING.md)。

### 手工部署到模拟器共享目录

关闭仍在运行的 `test_host.exe`，然后运行：

```bat
scripts\stage.bat
```

脚本会先增量构建，再把七个产品 DLL、`test_host.exe`、`test_host.ini` 和 fallback
字体复制到 `C:\WMShare\`。在模拟器中把该目录配置为 Shared Folder，然后从对应的
Storage Card 路径启动 `test_host.exe`。

如需隔离候选包：

```bat
scripts\stage.bat Debug C:\WMShare\Positron-candidate
```

### 自动设备门

先在 WMDC/Device Emulator GUI 中连接一个设备；USB 真机和 DMA emulator 均可，当前连接
必须已经可用。gate 通过 32 位 RAPI 直接消费 WMDC 的当前设备，不枚举或绑定 VMID，也不会
连接、选择、启动、Cradle、断开或重置设备。连接完成后，在仓库根目录运行：

```bat
scripts\device_gate.bat -Candidate nextNNN
```

脚本会使用正式工程配置增量构建，创建隔离 staging，部署完整候选包，启动
`test_host.exe`，有限等待并回收日志，最后检查所选测试、设备指标、`ERROR`/`FAIL`、
TEST13 导航和 `TESTBENCH PASS`。每次运行前只回收设备端由 gate 自己命名的旧候选目录；
完整本地证据保存在被 Git 忽略的 `tmp/device-runs/`。

如果 RAPI 初始化报告 `0x8007007E`，WMDC 的旧式 COM 路径可能未被现代 Windows 正确展开。
运行一次下列脚本并确认 UAC；脚本只修复已知 WMDC/RAPI COM 注册，遇到未知值会拒绝修改：

```bat
scripts\repair_wmdc_rapi.bat
```

`-Candidate` 只命名本次设备端目录和证据目录；它不会切换 Git 版本，也不会改变测试选择。
实际代码来自当前工作区，实际测试选择来自当前 `test_host/test_host.ini`。定向测试、失败行为
和人工验收边界见[测试指南](docs/TESTING.md)。

## 仓库结构

```text
Positron.sln              VS2008 主解决方案
positron_tls/             TLS 1.2 与证书验证
positron_json/            JSON 公共 DLL
positron_http/            HTTP 公共 DLL
positron_image/           位图和 SVG 公共 DLL
positron_script/          独立 JavaScript 公共 DLL
positron_core/            HTML/CSS/DOM/layout/paint 产品边界
positron_browser/         浏览器 session/history 产品组合层
test_host/                回归宿主和示例消费者
samples/                  独立 DLL 示例
scripts/                  正式构建、stage、移植和审计脚本
docs/                     面向维护者和使用者的文档
.agents/                  仅供 agent 接管的动态状态
third_party/              直接 vendoring 的第三方源码和资源
netsurf-all-3.11/         NetSurf 3.11 上游源码快照
```

解决方案还包含若干移植后的内部静态库（`positron_expat`、`positron_hubbub`、
`positron_libcss`、`positron_libdom`、`positron_libjpeg`、`positron_libsvgtiny`、
`positron_netsurf`）。调用者应只依赖产品 DLL 的公共头文件，不应直接链接或包含
NetSurf、Expat、libjpeg-turbo 或其他内部接口。

### 子项目 README

每个解决方案工程的目录 README 都说明了工程输出、边界和调用方式：

- 公共 DLL：[`positron_tls/`](positron_tls/README.md)、[`positron_json/`](positron_json/README.md)、
  [`positron_http/`](positron_http/README.md)、[`positron_image/`](positron_image/README.md)、
  [`positron_script/`](positron_script/README.md)、[`positron_core/`](positron_core/README.md)、
  [`positron_browser/`](positron_browser/README.md)；
- 内部静态库：[`positron_expat/`](positron_expat/README.md)、[`positron_hubbub/`](positron_hubbub/README.md)、
  [`positron_libcss/`](positron_libcss/README.md)、[`positron_libdom/`](positron_libdom/README.md)、
  [`positron_libjpeg/`](positron_libjpeg/README.md)、[`positron_libsvgtiny/`](positron_libsvgtiny/README.md)、
  [`positron_netsurf/`](positron_netsurf/README.md)；
- 消费者：[`test_host/`](test_host/README.md) 和 [`samples/positron_image_demo/`](samples/positron_image_demo/README.md)。

`netsurf-all-3.11/`、`positron_tls/mbedtls/` 和 `third_party/` 下的 README 是上游或
第三方说明，保持其来源语境；它们不是 Positron 公共 API 文档。

## 文档

- [文档索引](docs/README.md)
- [架构与公共边界](docs/ARCHITECTURE.md)
- [构建与部署](docs/BUILDING.md)
- [测试与验收](docs/TESTING.md)
- [故障排查](docs/TROUBLESHOOTING.md)
- [历史里程碑](docs/history/README.md)
- [第三方组件与许可证](THIRD_PARTY.md)

Agent 的工作纪律由 [AGENTS.md](AGENTS.md) 定义，动态交接只维护在 `.agents/`。这些文件
不是面向普通仓库读者的产品说明。

## 开发约束

- 目标编译器是 MSVC 9.0；项目源码和移植产物必须满足 C89/VS2008 约束。
- 公共 DLL 保持 C ABI、UTF-8、opaque handle 和跨 CRT 安全的内存所有权。
- Windows Mobile 已有能力优先复用 WinInet、GDI、WM Imaging 和 CryptoAPI。
- 新协议、解析器、编解码器或 runtime 优先移植成熟上游项目，并记录固定版本、来源和许可证。
- 自动测试不能替代字体、布局、触摸、SIP、旋转和真实网页的人工观察。

贡献前至少运行：

```bat
python scripts\test_c89ize.py
python scripts\audit_repo.py
```

涉及 C 源码时，还应使用正式解决方案构建，并按风险运行对应设备门。

## 许可证

Positron 自有代码采用 [MIT License](LICENSE)。仓库包含 GPLv2、Apache-2.0、MIT、OFL、
zlib/IJG 等不同许可证的第三方组件；分发前必须同时遵守
[THIRD_PARTY.md](THIRD_PARTY.md) 和各上游目录中的许可证文件。

# Positron

Positron 是面向 Windows Mobile 6 / Windows CE 5.2 ARMV4I 的模块化基础设施与轻量网页运行时。它把 TLS、JSON、HTTP、图像、脚本、HTML/CSS/DOM/layout 和浏览器会话能力封装为可由旧式 Win32/WinCE 应用调用的 DLL，同时保持 VS2008、C89 和设备资源约束。

项目的目标不是复刻现代桌面浏览器，而是在 WM6 上提供一组可组合、可测试、边界清楚的公共能力。`test_host.exe` 是这些 DLL 的回归宿主和示例消费者，不是产品 API 的所有者。

## 当前定位

- 目标平台：Windows Mobile 6 Professional / Windows CE 5.2，ARMV4I。
- 工具链：Visual Studio 2008 SP1 与 Windows Mobile 6 Professional SDK。
- 公共接口：稳定 C ABI、UTF-8 字符串、opaque handle、显式内存所有权。
- 运行方式：各 DLL 可独立使用，也可由宿主组合成受限的网页浏览体验。
- 成熟度：持续开发中的兼容层；已经有真实设备回归，但不能视为现代 Web 平台或通用安全浏览器。

浏览器 JavaScript 默认关闭。独立 `positron_script.dll` 与浏览器脚本绑定共用同一套 Duktape 引擎；前者是通用嵌入式脚本服务，后者是由 `positron_browser.dll` 和宿主共同提供的受限 Web 组合，不是第二个 JavaScript 引擎。

## 发布的公共 DLL

| DLL | 用途 | 典型调用入口 |
|---|---|---|
| `positron_tls.dll` | TLS 客户端、双向认证 peer 与 listener | `PTls_Init`、`PTls_ConnectVerified`、`PTls_ServerListen` |
| `positron_json.dll` | cJSON 的 opaque-handle 包装 | `PJson_Parse`、读取函数、`PJson_Free` |
| `positron_http.dll` | 基于 TLS 的 HTTP/1.1 GET/POST 与 URL reference 解析 | `PHttp_Init`、`PHttp_GetEx`、`PHttp_PostEx` |
| `positron_image.dll` | 位图/SVG 解码、绘制与编码 | `PImage_CreateBitmapFromMemory`、`PImage_DrawBitmap` |
| `positron_script.dll` | 有预算的独立 JavaScript context、JSON bridge 与模块加载 | `PScript_CreateEx`、`PScript_Evaluate`、`PScript_Destroy` |
| `positron_core.dll` | HTML/CSS/DOM、样式、layout、绘制、表单和交互模型 | `PCore_Init`、`PCore_ParseHTML`、`PCore_LayoutDocument`、`PCore_DocumentWidth/Height`、paint API |
| `positron_browser.dll` | history/session 与每项 viewport snapshot、脚本 bootstrap、DOM/Event、候选导航生命周期与结果摘要、资源/候选提交及清理快照、平台回调协调 | `PBrowser_HistoryCreate`、`PBrowser_HistoryEntryScroll`、`PBrowser_HistorySetEntryScroll`、`PBrowser_ScriptSessionCreate`、`PBrowser_NavigationCandidateCreate`、`PBrowser_NavigationCandidateGetResult`、`PBrowser_NavigationCommitGetInfo`、`PBrowser_NavigationCleanupGetInfo` |

精确签名、错误码和所有权以各项目的公开头文件为准。调用示例和边界说明见对应子目录的 README；整体所有权与数据流见[架构文档](docs/ARCHITECTURE.md)。

## 快速开始

### 前置条件

构建主机需要：

- Windows；
- Visual Studio 2008 SP1；
- Windows Mobile 6 Professional SDK；
- 用于构建辅助脚本的 Python 3；
- 需要真机/模拟器验证时，已由 GUI 建立连接的 WMDC 会话。

仓库已经包含解决方案所需的第三方源码；正常 clone 后不需要在构建时下载依赖。

### 构建

在仓库根目录执行：

```bat
scripts\build.bat
```

默认执行 Debug 增量构建。也可以显式选择配置或 clean rebuild：

```bat
scripts\build.bat Release
scripts\build.bat Debug rebuild
```

不要绕过解决方案直接拼装 ARM 编译器命令。完整工具链、产物目录和常见错误见 [构建指南](docs/BUILDING.md)。

### 生成可运行目录

`stage.bat` 会先用正式配置构建，再把同一批 EXE、DLL、字体和配置复制到目标目录：

```bat
scripts\stage.bat Debug C:\WMShare\Positron
```

在设备的共享目录中运行 `test_host.exe`。若要使用自动设备门，必须先在 WMDC 或 Device Emulator GUI 中手动连接唯一目标，然后执行：

```bat
scripts\device_gate.bat -Candidate local-check
```

设备门只消费 WMDC 当前连接，不选择设备、不绑定 VMID，也不替用户启动或 cradle 设备。测试模式、INI 配置、自动与人工验收的区别见[测试指南](docs/TESTING.md)。

### Nightly 包

`scripts\package_nightly.bat` 从当前各项目最新构建产物生成仅存储 ZIP，并覆盖 GitHub 上固定的 nightly pre-release。它不会触发构建，因此运行前应先完成所需配置的 build/stage。详情见 [Nightly 发布说明](docs/NIGHTLY_RELEASE.md)。

## 仓库结构

| 路径 | 内容 |
|---|---|
| `positron_*` | 公共 DLL、内部移植静态库及其公开头文件 |
| `test_host/` | 回归宿主、设备 fixture 和示例消费者 |
| `samples/` | 独立 DLL 消费示例 |
| `compat/` | WinCE/VS2008 缺失 CRT 与 C99 兼容层 |
| `netsurf-all-3.11/` | 固定版本的 NetSurf 及支持库源码 |
| `third_party/` | 其他固定版本第三方源码、字体和移植记录 |
| `scripts/` | 构建、转换、审计、设备门和打包脚本 |
| `docs/` | 面向使用者与维护者的稳定文档 |
| `.agents/` | agent 当前交接、限制、未来路线和失败实验索引 |
| `tmp/` | 本地截图、日志和诊断材料；不跟踪 |

## 文档入口

- [文档索引](docs/README.md)：按读者和任务选择文档。
- [架构与公共边界](docs/ARCHITECTURE.md)：DLL 职责、数据流、ABI 与平台原则。
- [构建](docs/BUILDING.md)：VS2008/WM6 正式构建和 staging。
- [测试与验收](docs/TESTING.md)：INI、自动设备门、人工验收和通过标准。
- [故障排查](docs/TROUBLESHOOTING.md)：构建、部署、WMDC/RAPI、网络和输入问题。
- [第三方清单](THIRD_PARTY.md)：版本、来源、许可证与再发布注意事项。
- [当前开发状态](.agents/HANDOFF.md)：仅用于接管当前工作，不是长期产品说明。

逐批开发记录不写入 README。需要追溯某次变更时，请使用 Git 历史；需要理解旧阶段或事故时，查看 [`docs/history/`](docs/history/README.md)。

## 兼容性与安全边界

Positron 的一些依赖为了 VS2008/C89 兼容而固定在旧版本，其中包括已结束上游支持的组件。发布者必须结合自己的威胁模型审查证书、协议、解析器和第三方安全公告。项目不提供 TLS 1.3、完整现代 Web API、完整 CSS 或任意网站兼容性保证。

默认应使用 verified TLS/HTTP 路径；不安全连接入口只用于明确的诊断或受控环境。浏览器 JavaScript 仍是显式 opt-in，启用它不等于获得完整浏览器安全沙箱。

## 许可证

原创 Positron 代码适用根目录 [LICENSE](LICENSE)。vendored 依赖保留各自许可证；其中编入 NetSurf browser 源码的二进制受 GPLv2 义务约束。分发前请阅读 [THIRD_PARTY.md](THIRD_PARTY.md) 及依赖目录中的原始许可证。该说明不是法律意见。

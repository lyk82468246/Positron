# Nightly CAB 发布

这条发布通道生成 Windows Mobile 6 Professional / ARMV4I 的标准 Smart Device CAB。它与现有的 nightly ZIP 独立：使用滚动 `nightly-cab` tag/release，不修改 `nightly`，也不上传 ZIP。

## 构建入口

项目入口是 `positron_cab\positron_cab.vddproj`，已经加入 `Positron.sln`。CAB 项目只消费 Release 产物，项目配置为：

- `ProductName = Positron`；`Manufacturer = Positron`；
- `OSVersionMin = 5.02`，`OSVersionMax = 6.99`；VS2008 不接受空的上限值，`6.99` 是面向 WM6 的合法有效上界；
- `Compress = true`，允许卸载；
- 不使用 `Setup.dll`、`CESetupDLL`、自注册、COM 注册或数字签名；
- Debug 配置不参与发布，发布只使用 `Release|Windows Mobile 6 Professional SDK (ARMV4I)`。

解决方案只为 CAB 的 Release 配置设置 `Build.0`，Debug 不设置；普通 Debug 构建不会生成 CAB。现有 nightly ZIP 脚本不调用该 Release CAB 构建，ZIP 行为保持不变。VS2008 的并行解决方案构建有时会在上游 DLL/EXE 完成前尝试 CabWiz，因此一次 `Release build` 可能只完成源码项目，下一次才完成 CAB；发布脚本会把源码构建失败后的普通 Build 作为最多 4 次的受限重试，仍失败就停止。

使用脚本时，默认模式先运行 `scripts\build.bat Release rebuild`；如果 VS2008 因并行依赖调度返回失败，会自动最多运行 4 次 `scripts\build.bat Release build`，直到解决方案报告成功。也可以在 VS2008 中打开解决方案，选择 `Release|Windows Mobile 6 Professional SDK (ARMV4I)`，右键 `positron_cab` 执行 Build。部署项目会在 `positron_cab\Release\` 生成 CabWiz 使用的 INF 和中间 CAB。

`.vddproj` 是 VS2008 的旧式部署项目，必须保持 ASCII + CRLF；脚本和项目文件已经按此格式维护。命令行构建如果长时间没有退出，应先检查 `vs2008-build.log` 和 VS 进程状态，不要把一个卡住的 `devenv.com` 当作已完成的 Release 构建。

## 后处理与发布

从仓库根目录运行本地校验：

```bat
scripts\package_nightly_cab.bat -SkipSourceBuild -SkipUpload
```

脚本会：

1. 读取 `positron_cab\Release\` 中最近生成的 INF；
2. 准备三个具有唯一源文件名的 Noto 许可证副本，避免 VS2008/CabWiz 将三个同名 `OFL.txt` 合并成冲突的 INF 键；
3. 把 `__POSITRON_CAB_VERSION__` 替换为 `YYYY.MM.DD.NN`，把 `__POSITRON_CAB_BUILD_DATE__` 替换为 `YYYY-MM-DD`，并注入 `ProcessorType=2577`；
4. 核对 ARMV4I (`ProcessorType=2577`)、`VersionMin=5.02`、安装目录、7 个 DLL、3 个字体、许可证、快捷方式、HKLM 注册表和值类型；
5. 使用 VS2008 Smart Devices SDK 的 `cabwiz.exe /compress` 生成固定文件名；
6. 检查 CAB 的 `MSCF` 标识和禁止文件，并生成说明与 SHA-256 清单。

输出默认位于 `tmp\nightly-cab\`：

```text
positron-nightly-cab-wm6-armv4i.cab
NIGHTLY-CAB-README.md
SHA256SUMS.txt
positron-nightly-cab-wm6-armv4i.inf
```

确认设备验收通过后，去掉 `-SkipUpload` 更新滚动发布：

```bat
scripts\package_nightly_cab.bat
```

首次使用或指定仓库时可以追加：

```bat
scripts\package_nightly_cab.bat -Repository owner/repo -BuildNumber 1
```

上传需要已登录的 GitHub CLI，以及允许更新 `nightly-cab` tag/release 的 Git 凭据。脚本只强制更新 `nightly-cab`；不会移动或重建 `nightly`。

如果需要脚本先调用正式源码构建，使用默认模式：

```bat
scripts\package_nightly_cab.bat
```

如果源码已经由 VS2008 GUI 或 `scripts\build.bat Release build` 完成，也可以使用 `-SkipSourceBuild` 只执行 INF 后处理、CabWiz 和内容验收。`-SkipSourceBuild` 不会绕过 CAB 内容检查；它只复用现有的 Release 产物。

## CAB 设备布局

| 设备路径 | 内容 |
|---|---|
| `\Program Files\Positron` | `positron.exe` |
| `\Program Files\Positron\licenses` | `LICENSE`、`THIRD_PARTY.md`、三个 Noto 字体许可证 |
| `\Windows` | `positron_tls.dll`、`positron_json.dll`、`positron_http.dll`、`positron_core.dll`、`positron_image.dll`、`positron_script.dll`、`positron_browser.dll` |
| `\Windows\fonts` | `PositronSymbolsBasic.ttf`、`PositronSymbols.ttf`、`PositronEmoji.ttf` |
| `\Windows\Start Menu\Programs` | `Positron.lnk`，目标为 `positron.exe` |

`positron_media.dll`、`test_host.exe`、`test_host.ini`、fixtures、PDB、LIB 和源码不进入产品 CAB。字体固定在 `\Windows\fonts`，因为当前 Core 会从 `positron_core.dll` 所在目录拼接 `fonts\` 加载它们。

DLL 放在 `\Windows` 是本方案的明确选择，但 Windows CE 可能对不同路径的同名 DLL 做全局复用。安装验收必须覆盖同名 DLL 冲突、覆盖顺序、卸载残留和升级过程；运行应用时不允许直接覆盖正在使用的 DLL。

## 注册表与快捷方式

CAB 只写 `HKLM\Software\Positron`，值均为 `REG_SZ`：

| 值名 | 内容 |
|---|---|
| `Version` | `YYYY.MM.DD.NN` |
| `BuildDate` | `YYYY-MM-DD` |
| `Channel` | `nightly-cab` |
| `InstallDir` | `\Program Files\Positron` |

`Version` 和 `BuildDate` 是 Positron 自定义元数据，不是 Windows CE 官方 OS 版本字段。快捷方式由 CAB 项目的 Programs Folder 生成，不手写 `.lnk` 文件内容；不写 `Run`、`Startup`、服务、文件关联或 `HKCR`。

## 设备验收

发布前至少在干净的 WM6 Professional ARMV4I 设备或等价模拟器上完成：

- 安装后确认目录、7 个 DLL、3 个字体、注册表值和 Programs 快捷方式；
- 从 Programs 菜单启动应用，确认字体加载和核心运行；
- 关闭应用后再次安装较新的 CAB，确认升级成功且没有旧 DLL/字体混用；
- 应用运行时尝试升级，必须得到可解释的失败或要求先退出，不得产生半更新状态；
- 卸载后确认 EXE、DLL、字体、快捷方式和 `HKLM\Software\Positron` 值都被移除；
- 解包或审查 CAB 清单，确认没有 `positron_media.dll`、`test_host`、fixtures、PDB、LIB 或源码。

设备通过前，`nightly-cab` 只应作为本地候选，不应被当作已验收产品发布。

## 参考

- [Smart Device Development（Visual Studio）](https://download.microsoft.com/download/1/6/d/16d24ada-5317-4de1-b2b2-890b51813d6e/VS2005_DeviceDev_en-us.pdf)
- [DefaultInstall](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms906505%28v%3Dmsdn.10%29)
- [AddReg](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms938375%28v%3Dmsdn.10%29)
- [CEShortcuts](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms938387%28v%3Dmsdn.10%29)
- [CEStrings](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms938390%28v%3Dmsdn.10%29)
- [CEDevice](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms938382%28v%3Dmsdn.10%29)
- [LoadLibrary](https://learn.microsoft.com/en-us/previous-versions/ms911520%28v%3Dmsdn.10%29)

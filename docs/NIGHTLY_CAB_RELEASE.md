# Nightly CAB 发布

这条发布通道生成 Windows Mobile 6 Professional / ARMV4I 的标准 Smart Device CAB。它与现有的 nightly ZIP 独立：使用滚动 `nightly-cab` tag/release，不修改 `nightly`，也不上传 ZIP。

这是主线安装发布，故不包含尚未进入主线的 `positron_db.dll`。`positron_db.dll` 只保留在绿色版 nightly ZIP/stage 发布链路中。

## 构建入口

项目入口是 `positron_cab\positron_cab.vddproj`，已经加入 `Positron.sln`。CAB 项目只消费 Release 产物，项目配置为：

- `ProductName = Positron`；`Manufacturer = Positron`；
- `OSVersionMin = 5.02`，`OSVersionMax = 6.99`；VS2008 不接受空的上限值，`6.99` 是面向 WM6 的合法有效上界；
- `Compress = true`，允许卸载；
- 不使用 `Setup.dll`、`CESetupDLL`、自注册、COM 注册或数字签名；
- Debug 配置不参与发布，发布只使用 `Release|Windows Mobile 6 Professional SDK (ARMV4I)`。

解决方案的 Release 配置包含 CAB 的 `Build.0`，Debug 不生成 CAB。项目依赖按实际链接输入维护，基础静态库先于公共 DLL，公共 DLL 先于应用/测试宿主，应用/测试宿主完成后才构建 CAB；因此普通 `scripts\build.bat Release build` 会包含 CAB，且不会在输入 DLL/EXE 完成前抢跑。现有 nightly ZIP 脚本不调用该 Release CAB 构建，ZIP 行为保持不变。

使用脚本时，默认模式临时注入版本和日期，然后调用 VS2008 的 `devenv.com /Build ...` 做包含 `positron_cab` 的 Release 全解决方案增量构建；源码构建失败时只重试普通 `Release build`，不会执行 `rebuild`。若使用 `-SkipSourceBuild`，脚本才调用 `devenv.com /Build ... /Project positron_cab\positron_cab.vddproj` 单独构建 CAB。VS2008 负责生成 INF 和 CAB，脚本不直接调用底层 `cabwiz.exe`。也可以在 VS2008 中打开解决方案，选择 `Release|Windows Mobile 6 Professional SDK (ARMV4I)` 执行 Build Solution。

`.vddproj` 是 VS2008 的旧式部署项目，必须保持 ASCII + CRLF；脚本和项目文件已经按此格式维护。命令行构建如果长时间没有退出，应先检查 `vs2008-build.log` 和 VS 进程状态，不要把一个卡住的 `devenv.com` 当作已完成的 Release 构建。

## 后处理与发布

从仓库根目录运行本地校验：

```bat
scripts\package_nightly_cab.bat -SkipSourceBuild -SkipUpload
```

脚本会：

1. 核对版本库中的唯一命名许可证、字体和 Release 源码输出是否完整，防止把缺失输入交给部署项目后才得到模糊的 CabWiz 错误；
2. 临时把 `__POSITRON_CAB_VERSION__` 替换为 `YYYY.MM.DD.NN`，把 `__POSITRON_CAB_BUILD_DATE__` 替换为 `YYYY-MM-DD`，再通过 VS2008 `devenv.com` 增量构建全解决方案或单独的 `positron_cab` 项目；
3. 恢复未写入版本值的 `.vddproj`，读取 VS2008 生成的 INF/CAB，并核对 `VersionMin=5.02`、安装目录、7 个 DLL、3 个字体、许可证、快捷方式、HKLM 注册表和值类型；
4. 检查 CAB 的 `MSCF` 标识和禁止文件，并生成 SHA-256 清单。

输出默认位于 `tmp\nightly-cab\`：

```text
positron-nightly-cab-wm6-armv4i.cab
SHA256SUMS.txt
positron-nightly-cab-wm6-armv4i.inf
```

发布时，构建信息、commit、版本、SHA-256、安装布局和验收提示直接写入 GitHub Release body；不再生成或上传独立的 `NIGHTLY-CAB-README.md` 资产。已有 `nightly-cab` release 会先删除再重新创建，因为仅编辑标题或正文不会刷新 GitHub 网页显示的发布时间。

确认设备验收通过后，去掉 `-SkipUpload` 更新滚动发布：

```bat
scripts\package_nightly_cab.bat
```

首次使用或指定仓库时可以追加：

```bat
scripts\package_nightly_cab.bat -Repository owner/repo -BuildNumber 1
```

上传需要已登录的 GitHub CLI，以及允许更新 `nightly-cab` tag/release 的 Git 凭据。脚本只强制更新并重建 `nightly-cab`；不会移动或重建 `nightly`。

如果需要脚本先调用正式源码构建，使用默认模式：

```bat
scripts\package_nightly_cab.bat
```

如果源码已经由 VS2008 GUI 或 `scripts\build.bat Release build` 完成，可以使用 `-SkipSourceBuild` 跳过全解决方案构建，但仍由 VS2008 增量构建 CAB。若源码和 CAB 都已经由 VS2008 GUI 构建完成，可同时使用 `-SkipSourceBuild -SkipCabBuild` 只校验现有 VS 输出。两个开关都不会绕过 CAB 内容检查；它们只控制是否复用已有产物。

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
- 解包或审查 CAB 清单，确认没有 `positron_db.dll`、`positron_media.dll`、`test_host`、fixtures、PDB、LIB 或源码。

设备通过前，`nightly-cab` 只应作为本地候选，不应被当作已验收产品发布。

## 参考

- [Smart Device Development（Visual Studio）](https://download.microsoft.com/download/1/6/d/16d24ada-5317-4de1-b2b2-890b51813d6e/VS2005_DeviceDev_en-us.pdf)
- [DefaultInstall](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms906505%28v%3Dmsdn.10%29)
- [AddReg](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms938375%28v%3Dmsdn.10%29)
- [CEShortcuts](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms938387%28v%3Dmsdn.10%29)
- [CEStrings](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms938390%28v%3Dmsdn.10%29)
- [CEDevice](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms938382%28v%3Dmsdn.10%29)
- [LoadLibrary](https://learn.microsoft.com/en-us/previous-versions/ms911520%28v%3Dmsdn.10%29)

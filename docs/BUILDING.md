# 构建与部署

## 支持的工具链

Positron 的正式目标是：

- Visual Studio 2008 SP1 / MSVC 9.0；
- Windows Mobile 6 Professional SDK；
- `Windows Mobile 6 Professional SDK (ARMV4I)` solution platform；
- Windows Mobile 6 Professional Emulator 或兼容 ARMV4I 设备；
- Python 3，用于移植、生成、回归和仓库审计脚本。

微软编译器、SDK、模拟器和设备镜像是外部专有依赖，仓库不能分发。第三方开源组件本身已经 vendored，正常 clone 不应在构建时临时下载源码。

## 构建前检查

从仓库根目录运行：

```bat
python scripts\audit_repo.py
```

审计会检查 VS2008 工程引用、Git 跟踪状态、关键版本、第三方许可证和本地 Markdown 链接。若修改了 C 移植代码或转换脚本，再运行：

```bat
python scripts\test_c89ize.py
```

这两个命令不能代替 ARMV4I 正式构建。

## 正式构建入口

```bat
scripts\build.bat [Debug|Release] [build|rebuild|clean]
```

常用组合：

```bat
scripts\build.bat
scripts\build.bat Debug build
scripts\build.bat Debug rebuild
scripts\build.bat Release rebuild
scripts\build.bat Debug clean
```

默认值是 `Debug build`。脚本定位 VS2008 的 `devenv.com`，然后构建：

```text
Positron.sln
Debug|Windows Mobile 6 Professional SDK (ARMV4I)
```

完整 VS2008 输出写入根目录 `vs2008-build.log`。构建失败时先查看该文件中最早的编译或链接错误，不要只处理末尾的连锁报错。

正式工程配置是构建结果的权威来源。不要直接调用编译器拼接源文件，不要维护一个绕过 `Positron.sln` 的“临时可用”二进制路径。

## 主要输出

成功构建后，产品 DLL 和宿主分别位于各工程的 `bin\<Config>\`：

```text
positron_tls.dll
positron_json.dll
positron_db.dll
positron_media.dll
positron_http.dll
positron_core.dll
positron_image.dll
positron_script.dll
positron_browser.dll
positron.exe
test_host.exe
```

解决方案中的 NetSurf、DOM、CSS、SVG、JPEG、Expat 等工程主要生成内部静态库。它们被产品 DLL 封装，不是应用程序的部署接口。

## Stage

默认部署入口：

```bat
scripts\stage.bat
```

也可以指定配置和目标目录：

```bat
scripts\stage.bat Release
scripts\stage.bat Debug C:\WMShare\Positron-candidate
```

`stage.bat` 会先调用同配置的增量构建。只有构建成功后才复制：

- 九个产品 DLL；
- `positron.exe` 独立浏览器应用；
- `test_host.exe` 回归宿主；
- `test_host.ini`；
- `fonts\` 下的 Positron symbol/emoji fallback 字体及许可证。

这条“先构建、后整体复制”的规则用于避免新 EXE 与旧 DLL 混包。不要从多个 candidate 目录手工拼出一个包。

## 配置 WM6 Emulator

1. 启动 WM6 Professional Emulator。
2. 在 Emulator 的设置中配置 Shared Folder，指向 `C:\WMShare\` 或本次指定的 stage 根目录。
3. 在设备 File Explorer 中打开对应的 Storage Card 共享路径。
4. 运行 `positron.exe` 验收独立浏览器应用；需要回归测试时再运行 `test_host.exe`。

VS2008 Smart Device deploy 会覆盖当前工程需要的部署路径，项目正式流程不依赖它。

## 更新正在运行的包

Windows Mobile 的关闭按钮通常只是 Smart Minimize。重新 stage 前：

1. 在设备任务管理器确认旧 `positron.exe` 或 `test_host.exe` 已真正退出；
2. 如 DLL 仍被系统进程加载，关闭相关窗口或重启模拟器；
3. 优先 stage 到一个新的隔离目录；
4. 确认同一目录中的十一个运行时二进制来自同一次构建。

文件锁、旧进程和系统级 DLL 复用都可能让源码正确但设备运行错误版本。

## 修改第三方移植代码

上游 C99 代码必须通过仓库中相应的生成/移植脚本转换，并保持结果可重复。通用 C89 转换器位于 `scripts/c89ize.py`，但部分组件还有自己的固定版本 port 脚本。

修改时遵循：

- 优先修改可重复的 port/generator，再重新生成；
- 不在生成结果中维护无法复现的手工差异；
- 只对当前需要的上游文件做最小补丁；
- 运行 `python scripts\test_c89ize.py`；
- 使用正式 solution 配置构建；
- 更新对应 `UPSTREAM.md` 或 `POSITRON_PORT.md` 中的本地差异。

## Release 构建

Release 使用相同 solution platform：

```bat
scripts\build.bat Release rebuild
scripts\stage.bat Release C:\WMShare\Positron-release
```

Release 包仍需通过与风险相称的设备测试。成功编译不代表网络、布局、SIP、旋转或真实页面已经验收。

## Nightly 预发布包

打包脚本只读取已经存在的 `bin\Debug\`/`bin\Release\` 产物，不会触发编译，也不会调用 `stage.bat`：

```bat
scripts\package_nightly.bat
```

默认自动比较两套完整产物，选择所有十一个运行时文件中“最旧的那个”仍然最新的一套；因此通常会选中最近一次完整的 Debug 构建（`build.bat`/`stage.bat` 默认就是 Debug），如果最近一次完整构建是 Release 则会选 Release。也可以显式固定配置。测试清单从当前 `test_host/main.c` 的 `run_configured_tests` dispatch 动态生成；明确标记为 `manual-only` 的测试会从默认 `auto=1` 清单排除，新增并接入 dispatch 的自动测试会自动进入下一次包。脚本不复制 tracked smoke INI 中的缩减选择。随后脚本补入字体、许可证、说明和 SHA-256 清单，创建不压缩的 `tmp\nightly\positron-nightly.zip`。可选参数：

```bat
scripts\package_nightly.bat -Configuration Debug
scripts\package_nightly.bat -Configuration Release
scripts\package_nightly.bat -SkipUpload
scripts\package_nightly.bat -Repository owner/repo
```

不带 `-SkipUpload` 时，脚本要求 GitHub CLI 已登录；它会把滚动 `nightly` tag 对齐到当前源 commit，重建固定的 pre-release 以刷新发布日期，并用 `--clobber` 替换同名 `positron-nightly.zip`。它不会创建版本号，也不会移动版本化产品 tag。首次使用先运行 `gh auth login -h github.com`。发布说明正文来自 [`NIGHTLY_RELEASE.md`](NIGHTLY_RELEASE.md)，包含如何编辑/移走 INI 来选择全量或部分的自动/手动模式。这里的 `gh` 登录与 `git push` 使用的 Git Credential Manager 是两套独立凭据；能 push 不代表 `gh release` 已登录。若固定 release 已 immutable，脚本会在删除或移动前停止；其他失败不会上传不完整的 ZIP。`tmp\nightly\` 只保存本机生成物，不进入 Git。

## Nightly CAB 安装包

标准安装包由 VS2008 Smart Device CAB 项目产生，项目文件是 `positron_cab\positron_cab.vddproj`。只构建 `Release|Windows Mobile 6 Professional SDK (ARMV4I)`；不要构建 Debug CAB，也不要把 CAB 内容混入现有 ZIP 流程。

主线 CAB 不包含尚未进入主线的 `positron_db.dll`；数据库 DLL 只随绿色版 nightly ZIP/stage 发布。CAB 的输入校验也会把 `positron_db.dll` 视为禁止内容。

Release 配置的全解决方案构建包含 `positron_cab`；Debug 配置不生成 CAB。解决方案中的项目依赖按实际链接输入维护，顺序是基础静态库、公共 DLL、应用/测试宿主，最后是 CAB。这样 CAB 只能在它需要的 Release EXE/DLL 完成后启动。

CAB 必须由 VS2008 的部署项目接口生成。可以在 VS2008 图形界面中选择上述 Release 配置执行 Build Solution，或右键 `positron_cab` 项目执行 Build；发布脚本也只调用等价的 VS2008 `devenv.com` 接口，不直接运行 `cabwiz.exe`。三个 Noto 许可证以唯一文件名保存在版本库的 `positron_cab\cab-source` 中，GUI 全解决方案构建不依赖脚本预处理。

从仓库根目录运行：

```bat
scripts\package_nightly_cab.bat -SkipUpload
```

脚本会临时注入本次版本和日期，运行包含 `positron_cab` 的 Release 全解决方案增量构建，构建完成后恢复 `.vddproj`；CAB 和 INF 始终由 VS2008 项目生成。输出为 `positron-nightly-cab-wm6-armv4i.cab`、对应 INF 和 `SHA256SUMS.txt` 到 `tmp\nightly-cab\`。确认本地结果后，去掉 `-SkipUpload` 可更新滚动 `nightly-cab` tag/release；已有 release 会被重新创建，以刷新网页显示的发布时间；它不会修改 `nightly` tag，也不会上传 ZIP。需要指定 GitHub 仓库时追加 `-Repository owner/repo`。

如果源码已经由 VS2008 GUI 或 `scripts\build.bat Release build` 完成，可以跳过源码阶段：

```bat
scripts\package_nightly_cab.bat -SkipSourceBuild -SkipUpload
```

如果源码和 CAB 都已经由 VS2008 GUI 构建完成，可以同时使用 `-SkipSourceBuild -SkipCabBuild`，脚本只校验现有 VS 输出并生成校验清单。`rebuild` 仅用于明确要求的源码全量重建，nightly CAB 发布流程不会调用它。不要同时启动多个 `devenv.com` 构建同一工作区；所有 VS2008 构建共享 `Release` 输出目录和日志。

CAB 的设备布局、注册表、快捷方式、升级/卸载和验收要求见 [Nightly CAB 发布说明](NIGHTLY_CAB_RELEASE.md)。

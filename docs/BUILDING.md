# 构建与部署

## 支持的工具链

Positron 的正式目标是：

- Visual Studio 2008 SP1 / MSVC 9.0；
- Windows Mobile 6 Professional SDK；
- `Windows Mobile 6 Professional SDK (ARMV4I)` solution platform；
- Windows Mobile 6 Professional Emulator 或兼容 ARMV4I 设备；
- Python 3，用于移植、生成、回归和仓库审计脚本。

微软编译器、SDK、模拟器和设备镜像是外部专有依赖，仓库不能分发。第三方开源组件本身已经
vendored，正常 clone 不应在构建时临时下载源码。

## 构建前检查

从仓库根目录运行：

```bat
python scripts\audit_repo.py
```

审计会检查 VS2008 工程引用、Git 跟踪状态、关键版本、第三方许可证和本地 Markdown 链接。
若修改了 C 移植代码或转换脚本，再运行：

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

完整 VS2008 输出写入根目录 `vs2008-build.log`。构建失败时先查看该文件中最早的编译或链接
错误，不要只处理末尾的连锁报错。

正式工程配置是构建结果的权威来源。不要直接调用编译器拼接源文件，不要维护一个绕过
`Positron.sln` 的“临时可用”二进制路径。

## 主要输出

成功构建后，产品 DLL 和宿主分别位于各工程的 `bin\<Config>\`：

```text
positron_tls.dll
positron_json.dll
positron_http.dll
positron_core.dll
positron_image.dll
positron_script.dll
positron_browser.dll
test_host.exe
```

解决方案中的 NetSurf、DOM、CSS、SVG、JPEG、Expat 等工程主要生成内部静态库。它们被产品
DLL 封装，不是应用程序的部署接口。

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

- 七个产品 DLL；
- `test_host.exe`；
- `test_host.ini`；
- `fonts\` 下的 Positron symbol/emoji fallback 字体及许可证。

这条“先构建、后整体复制”的规则用于避免新 EXE 与旧 DLL 混包。不要从多个 candidate
目录手工拼出一个包。

## 配置 WM6 Emulator

1. 启动 WM6 Professional Emulator。
2. 在 Emulator 的设置中配置 Shared Folder，指向 `C:\WMShare\` 或本次指定的 stage 根目录。
3. 在设备 File Explorer 中打开对应的 Storage Card 共享路径。
4. 运行 `test_host.exe`。

VS2008 Smart Device deploy 会覆盖当前工程需要的部署路径，项目正式流程不依赖它。

## 更新正在运行的包

Windows Mobile 的关闭按钮通常只是 Smart Minimize。重新 stage 前：

1. 在设备任务管理器确认旧 `test_host.exe` 已真正退出；
2. 如 DLL 仍被系统进程加载，关闭相关窗口或重启模拟器；
3. 优先 stage 到一个新的隔离目录；
4. 确认同一目录中的八个二进制来自同一次构建。

文件锁、旧进程和系统级 DLL 复用都可能让源码正确但设备运行错误版本。

## 修改第三方移植代码

上游 C99 代码必须通过仓库中相应的生成/移植脚本转换，并保持结果可重复。通用 C89 转换器
位于 `scripts/c89ize.py`，但部分组件还有自己的固定版本 port 脚本。

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

Release 包仍需通过与风险相称的设备测试。成功编译不代表网络、布局、SIP、旋转或真实页面
已经验收。

## Nightly 预发布包

打包脚本只读取已经存在的 `bin\Debug\`/`bin\Release\` 产物，不会触发编译，也不会调用
`stage.bat`：

```bat
scripts\package_nightly.bat
```

默认自动比较两套完整产物，选择所有八个运行时文件中“最旧的那个”仍然最新的一套；因此通常
会选中最近一次完整的 Debug 构建（`build.bat`/`stage.bat` 默认就是 Debug），如果最近一次完整
构建是 Release 则会选 Release。也可以显式固定配置。测试清单从当前
`test_host/main.c` 的 `run_configured_tests` dispatch 动态生成；明确标记为 `manual-only` 的
测试会从默认 `auto=1` 清单排除，新增并接入 dispatch 的自动测试会自动进入下一次包。脚本不
复制 tracked smoke INI 中的缩减选择。随后脚本补入字体、许可证、说明和 SHA-256 清单，创建
不压缩的 `tmp\nightly\positron-nightly.zip`。可选参数：

```bat
scripts\package_nightly.bat -Configuration Debug
scripts\package_nightly.bat -Configuration Release
scripts\package_nightly.bat -SkipUpload
scripts\package_nightly.bat -Repository owner/repo
```

不带 `-SkipUpload` 时，脚本要求 GitHub CLI 已登录，并把固定 `nightly` tag 更新为 pre-release，
用 `--clobber` 替换同名 `positron-nightly.zip`；它不会创建版本号。首次使用先运行
`gh auth login -h github.com`。发布说明正文来自
[`NIGHTLY_RELEASE.md`](NIGHTLY_RELEASE.md)，包含如何编辑/移走 INI 来选择全量或部分的自动/手动
模式。这里的 `gh` 登录与 `git push` 使用的 Git Credential Manager 是两套独立凭据；能 push
不代表 `gh release` 已登录。脚本失败时不会上传不完整的 ZIP；`tmp\nightly\` 只保存本机生成物，
不进入 Git。

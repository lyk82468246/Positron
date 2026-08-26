# Positron nightly pre-release

这个 nightly 包只提取已经编译好的产物，不会触发编译。包内的 DLL、`test_host.exe`、字体和
`test_host.ini` 必须来自同一个配置；默认脚本打包现有的 Release 输出。ZIP 使用 store 模式，
不压缩文件内容。

## 运行包

把 ZIP 解压到 Windows Mobile 6 / Windows CE 设备或模拟器的共享目录，保持下面的结构不变，
然后运行 `test_host.exe`：

```text
positron_tls.dll
positron_json.dll
positron_http.dll
positron_core.dll
positron_image.dll
positron_script.dll
positron_browser.dll
test_host.exe
test_host.ini
fonts\...
```

`test_host.ini` 必须和 `test_host.exe` 在同一目录。WMDC/RAPI 连接、设备窗口和真实人工操作
仍按项目的设备说明手动完成；这个包不会自动连接设备。

## 四种测试方式

包内 INI 默认是“当前源码实际 dispatch 且可自动运行的所有测试 + 自动断言”：`auto=1`，
`javascript=0`，`tests=` 由脚本读取 `test_host/main.c` 的 `run_configured_tests` 自动生成，
并补上特殊的 TEST7b 和 TEST999。源码明确标记为 `manual-only` 的测试（当前是 TEST232、
TEST263）会从 `auto=1` 清单中排除，避免启动后被测试自身判为失败；源码中没有实际 dispatch
的编号（包括历史撤回或尚未接入的编号）也不会被伪造成可用测试。自动模式不弹测试确认框，
结果写入同目录的 `test_host.log`，失败会在日志中留下 `ERROR`/`FAIL`。

因此，新增自动测试时只要在源码中完成测试函数、`TEST_MAX_NUMBER`（如需）和
`run_configured_tests` dispatch，并重新编译对应的一套完整产物，下一次打包就会自动看到新编号。
若新增测试是 manual-only，只会进入手动覆盖说明，不会进入默认自动清单。脚本不编译；若源码
和二进制不是同一提交，测试清单可能领先于二进制，发布前应先完成构建。

可以直接编辑或移走 INI：

| 目标 | 操作 | 结果 |
|---|---|---|
| 所有可自动测试、自动模式 | 保留 `test_host.ini`，保持 `auto=1` 和原 `tests=` | 全量自动断言，写 `test_host.log`；manual-only 测试不在此模式运行 |
| 部分测试、自动模式 | 只修改 `tests=`，例如 `tests=64-67,1097,999`，保持 `auto=1` | 只跑指定编号，仍自动断言并写日志 |
| 所有测试、手动模式 | 在 `tests=` 后追加发布说明列出的 manual-only 编号，把 `auto=0`；需要时设 `javascript=1` | 运行自动清单加手动测试，显示确认/信息框，按提示人工观察页面和控件 |
| 部分测试、手动模式 | 修改 `tests=`（可包含 manual-only 编号）后把 `auto=0` | 只运行指定编号，并保留手动提示和人工验收流程 |
| 旧式手动分组模式 | 暂时移走或改名 `test_host.ini` | 进入宿主内置的分组选择流程；这不是全量保证，也不保证产生自动化日志 |

`javascript=1` 仍是显式实验开关；它不由 nightly 包默认开启。TEST999 是退出前的一次系统
提示音，不代替日志或其他测试断言。视觉、真实触摸、SIP/IME、旋转、文件选择器和失败网络
仍需人工验收，自动模式不能把它们变成像素级保证。

## 发布方式

仓库使用固定的 `nightly` pre-release tag 和固定的 `positron-nightly.zip` 资产，不使用版本号。
下一次上传会更新同一个 release 的说明并用 `--clobber` 替换同名 ZIP；包内
`NIGHTLY-README.md` 和 `SHA256SUMS.txt` 会记录配置、生成时间和源 commit。
上传依赖 GitHub CLI 的独立登录状态；它与 Git 推送所用的 Git Credential Manager 凭据分开，
所以 Git push 成功不等于 `gh release` 已认证。

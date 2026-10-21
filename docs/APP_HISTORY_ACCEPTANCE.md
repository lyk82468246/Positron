# 历史搜索与记录操作的人工验收

## 数据与隔离

不要用零星的真实访问记录代替分页、搜索和删除验收。使用
[`prepare_app_history_fixture.py`](../scripts/prepare_app_history_fixture.py) 生成固定的 80 条记录，
并保存同名 JSON 预期清单。它只修改新建的隔离副本，不修改源库、DLL metadata 或 schema；
输入使用已关闭的设备自建测试库，例如完整下载门留下的 `app-downloads/ui.db`，不要选用户库。
设置在副本中恢复为系统语言、newtab 与 JS 启用，下载表为空；不发网络请求。

先正常退出应用并协调编译/设备门，单独备份用户数据库和日志。通过正式
`device_gate.ps1 -Configuration Debug -ModuleAuditOnly -PreserveDeployment` 创建新的隔离完整包，
在本地运行目录新建 `history-fixture` 子目录，再执行：

```powershell
python scripts/prepare_app_history_fixture.py --base "<closed-fixture.db>" --output "<run-root>\history-fixture\history-fixture.db"
```

用 32 位 Windows PowerShell 运行 `scripts/app_history_gate.ps1`，传入该包的 `-RemoteRoot`、
`-LocalRunRoot`、`-StartupUrl positron://history` 及上述完整 `-FixtureDatabase` 路径。
门验证 EXE/九 DLL 和数据库回读哈希，运行原有 UI 自检，等待真实历史页加载出 16 条及较早入口，
保留应用供人工操作。数据库只以拒绝覆盖的远端移动发布；目录已有数据库时停止，不删除它。
生成器也拒绝覆盖既有输出或接受带 journal/WAL/SHM 的输入。

每次重试用新的 `-AttemptName app-history-...`，不覆盖先前日志。仅在远端数据库与初始清单哈希
完全一致时可用 `-ReuseFixture`，它只复核、不复制或重置数据库。人工入口可显式加
`-ManualFixtureReview` 普通启动：此模式只报告 `READY`/`ui_selftests=NOT_RUN`，不运行全套启动自检，
不能用它覆盖或消除完整自动门失败；原有失败证据与未通过状态必须保留。

主机 SQL/数据检查不等于 C worker、设备耐久性或人工视觉通过；自动门的首屏通过也不能替代
下述实际搜索、翻页和确认框操作。所有夹具数据库、JSON 和设备日志留在忽略的 `tmp/`。

## 搜索与分页

以下预期只适用于尚未删除记录的初始夹具，按最新 ID 在前展示；JSON 另列出每页对应的稳定 ID。

| 搜索词/操作 | 总命中 | 各页记录数 |
|---|---:|---|
| 显示全部 | 80 | 16 / 16 / 16 / 16 / 16 |
| `alpha` 或 `ALPHA` | 40 | 16 / 16 / 8 |
| `中文` | 17 | 16 / 1 |
| `Duplicate` | 3 | 3 |
| `%` 或 `_` | 各 1 | 1 |
| `' OR 1=1 --` | 1 | 1 |
| `no-such-fixture-result` | 0 | 0 |

先测试搜索按钮和输入框回车，再用“较早”走完匹配结果；末页不应再有较早入口，重读应回到
相同筛选的第一页。清空输入并搜索、或点击“显示全部”，都应恢复全部记录。
连续搜索、结果尚未刷新时继续编辑，确认新筛选不会被旧结果替换，输入和选区不会丢失。
另在不同标签使用不同筛选，切回后检查各自状态。

`MiXeD CaSe Token` 验证 ASCII 大小写；`Ä` 与 `ä` 各只命中对应记录，不承诺 Unicode 折叠。
JSON 中提供恰好 128 字节的边界词及 50 个“中”（150 UTF-8 字节）的超限词：前者可搜索，
后者必须提示拒绝且不替换原筛选，不得截断后偷偷执行。

## 删除、重开与视觉

完成数量核对后再搜索 `Duplicate`。取消单删应保留 3 条，确认只删 1 条后应剩 2 条；
“显示全部”应为 79 条，同 URL 的其他访问仍在，Back/Forward、设置和下载不受影响。
正常退出并重开同一测试包，确认删除仍生效；记录变化后以操作后的数量为准。
确认清除是最后的破坏性夹具操作：它清除全部访问，不只清除筛选结果。

长 URL、长英文/中文标题、连续单词、空标题，以及引号、`<b>`、`&` 等特殊字符都已准备。
检查它们的文字回退、转义、自然折行、双轴/斜向拖动、SIP、旋转及 DPI；不能出现花屏或输入裁剪。
夹具 URL 不应自动加载；只有重复项使用明确的 IANA 地址，其他记录不需要点击联网。

## 结束与恢复

测试后正常退出。测试库和原用户库保持分离，不能把夹具记录合并进用户历史，也不能把更旧
用户备份恢复到新库。需要重置夹具时重新生成到新目录并新建部署包；恢复日常使用时，先核验
退出/零 DLL 引用，再在新的匹配包中恢复本轮人工测试前保存的最新用户库。

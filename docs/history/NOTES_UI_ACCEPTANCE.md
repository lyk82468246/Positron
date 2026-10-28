# Notes 页面与持久化验收

本文保存 2026-10-28 的夹具修订和设备证据；当前状态见
[HANDOFF](../../.agents/HANDOFF.md)，稳定操作见
[Notes 示例](../../positron_app/examples/notes/README.md)。自动通过不代替真实触摸、输入或 Release 人工门。

## 验收范围

在全新正式 Debug 包中复制原 Notes HTML/JS/CSS/manifest/迁移；只为测试入口追加独立驱动，
示例源码不改。EXE 的 Debug dispatcher 每秒驱动一次，作者夹具使用原按钮事件处理器，
不在宿主重写笔记业务或 SQL 权限。声明的迁移、受限 SQL、队列、身份仍由公共 DLL 拥有。

每根初始为空，分批插入 25 条带中文、引号和 HTML 特殊字符的记录，覆盖八/八/八/一条四页及
Previous；按正文独有内容搜索、Edit 读取、新增并修改持久笔记、两次确认删除临时笔记，最终保留
26 条。第二进程重开验证八/八/八/两条四页、标题搜索及修改后正文，不重放写入。
两目录使用相同 manifest ID，数据带 one/two 区分，按 one/two/one/two 交替启动，验证另一根数据不可见。

## 失败夹具与修订

所有目录均位于 `tmp/device-runs/`，保持原失败证据，不覆盖或删库后重新判门。

| 正式包 | 结果及原因 |
|---|---|
| `20261028-172655-app-runtime-sql` | 首轮夹具在 SQL callback 中点击按钮，记录 `click failed`，随后 stage=1 失败；未写作产品通过。驱动改为下一 UI tick 执行，不在点击前改进度 DOM。 |
| `20261028-172951-app-runtime-sql` | 通用 Debug 日志达到 128 KiB 上限，终态无法保留，门超时；事后只读审计零占用，不能由此追认页面通过。 |
| `20261028-173701-app-runtime-sql` | 两根首次运行通过；重开前 RAPI `CeMoveFile` 不能覆盖已有配置，device=5，完整门未通过。 |

最终使用独立、不足 448 字节的原生终态文件，严格匹配启动 PID，成功关闭后才收取完整诊断。
通用日志仍保持原上限；不扩大执行时间或放宽断言。测试配置及入口替换只在正常退出零占用后，
验证并保留先前字节，再替换精确的夹具文件；不改数据库、manifest 身份或用户资源。

正式 VS 预生成/Release 编译中断日志保留在 `tmp/notes-acceptance-*.log`，确认无残留后按
原正式配置串行重试成功，不绕过工程或将中断写作通过。首次 BAT 的 WMDC 注册修复 UAC 被取消，
随后直接使用已有正式 PowerShell 入口和当前连接完成；本轮未执行 WMDC 注册修复或重置设备。

## 最终设备证据

正式包为 `tmp/device-runs/20261028-174156-app-runtime-sql/`，详细日志、PID 终态、资源回读、
模块审计、空间及总结果在其 `app-runtime-notes/` 子目录。

| 顺序 | PID | 原生终态 |
|---|---:|---|
| one 首次 | 2188410838 | PASS，26 条 |
| two 首次 | 2542756938 | PASS，26 条 |
| one 重开 | 2724908298 | PASS，26 条 |
| two 重开 | 57731598 | PASS，26 条 |

全部同时满足 11/11 EXE/DLL 哈希、双空间预检、正常退出零引用及等待 dump writer 后 crash PASS。
Notes 新门的隔离与页面操作通过，不关闭
[旧 Runtime SQL 启动故障](RUNTIME_STORAGE_STARTUP_REVIEW.md)，也不代表旧设备间歇存储问题已修复。
C89、Runtime 接线守卫、Notes/SDK 映射测试、脚本语法及仓库审计通过；正式 Debug/Release/CAB
通过，Release 二进制排除新增自检命令、终态文件路径和断言文本。

## 人工入口与未完成边界

自动门结束后，两根均恢复原 Notes HTML，不再引用驱动；26 条测试数据保留。
存储卡 `Positron Notes Check.lnk` 普通启动该包的 `notes-one`，仍显示原生授权框。
原 Notes 快捷方式与用户库未修改。人工进程、入口及状态保存在同目录的 `manual.txt`。

用户在本对话逐项反馈“基本正常”：首屏八条及中文/按钮、Next 三次和 Previous、SIP 中文与
多行正文输入/保存/搜索/修改、正常退出重开保留修改、两次确认删除、SIP 显隐与横竖屏保留
未保存输入，以及拒绝授权不执行 Notes、再次同意后数据仍在。这些项目记为 Debug 当前设备
人工基本通过，不扩大到全部 ROM、字体或语言。

Release 普通启动及宿主双语矩阵仍待验收。自动脚本触发 `.click()` 不冒充人工操作，
正常重开也不证明 FULL、I/O、物理断电或 PXI 安装。用户随后修订安装风险规则，见运行时计划；
本次每次提示属于开发目录模式，不作为已安装 PXI 的最终使用流程。

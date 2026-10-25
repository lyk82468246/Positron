# Notes 目录应用

将整个目录复制到设备，使用 `positron.exe --app-dir "<目录>"` 显式启动并确认 SQL 权限。
这是纯 HTML/JS/CSS/manifest/迁移示例，不含可执行程序或第三方二进制。需已接通 SQL SDK 的
匹配 Positron 完整包；源码示例不代表当前设备版本已验收，状态见[交接](../../../.agents/HANDOFF.md)。

应用提供新增、编辑、两次点击确认删除、标题/正文 LIKE 搜索及每页八条的绑定游标分页。
标题最多 40 个、正文最多 512 个 JavaScript UTF-16 code units；中文可直接保存。
搜索使用 SQLite LIKE，`%`/`_` 保持通配含义，不提供 Unicode 大小写折叠或全文索引。
SQL 错误、结果超限或队列拒绝显示失败，不自动重放写入；编辑字段和已有列表保留。

宿主在确认后先运行 `schema/1.sql`，再为有效页面安装 SDK。作者只调用
`Positron.db.query/execute(sql, typedParams, callback)` 或 `transaction(statements, callback)`；
callback 为 `(ok, result)`，不是 Promise。参数及结果使用 Runtime 的 `type`/`value` 标签；
int64 必须是十进制字符串，不转为 Number。完整结果与预算见[Runtime](../../../positron_runtime/README.md)。

数据由 Runtime 绑定开发目录的真实身份，存放在 EXE 旁的 `data` 下，不在作者资源目录；
复制到不同路径会形成独立数据，不自动迁移旧数据。正常退出重启后保留，不宣称断电恢复。
应用不能指定数据库路径，也不能获得浏览器 `positron.db`；开发目录是可信代码模式，
没有已验证发布者或现代浏览器安全沙箱保证。修改资源前须正常退出。

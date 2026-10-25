# Runtime 的 WM 平台适配器

本文只说明 EXE 私有 `app_runtime_platform.c/.h` 的平台调用与边界；应用身份、manifest、
资源引用、迁移、SQL 和页面服务合同由公共
[Runtime](../positron_runtime/README.md)拥有。目录应用的整体设计见
[运行时计划](../docs/RUNTIME_PLAN.md)，实际进度和设备证据见
[HANDOFF](../.agents/HANDOFF.md)。平台自检不表示 `--app-dir`、JS SDK 或笔记应用已经接通。

## 调用与所有权

在拥有目录实例和存储的 worker 上调用 `AppRuntimePlatform_Init`，再通过
`AppRuntimePlatform_DirectoryProvider`、`AppRuntimePlatform_StorageProvider` 取得公共
callback table。`AppRuntimePlatform` 必须活到全部回调结束；Runtime 负责按合同释放
借用字节、存储 location、目录 root 和独占 lease，EXE 不接管 Runtime handle 的语义。
先关闭页面队列和存储，再关闭目录实例；不能把这些 handle 交给 UI 或另一个 worker。

适配器不解析 manifest、不维护应用 ID 白名单，不拼 SQL，不重新计算开发身份或 storage key。
不在普通启动时扫描磁盘、授予权限或运行测试；Release 不提供诊断命令。

## 目录与读取

- 只接受有界的本地绝对根路径；逐级枚举真实长文件名，拒绝相对路径、空/dot 段、尾部点或
  空格、通配符、原生 stream/device/UNC 形状、别名及可检测的 reparse/link。
- 准入时递归扫描，保留每个普通文件的只读句柄，不分享写入/删除；读取来自保留句柄，
  不按作者路径重新打开。读取前后核对原生文件身份、大小及修改时间，新增文件不进入本实例。
- 包内 key 已由 Runtime 解码并验证；平台只做宽字符路径转换和真实条目核对，不再次解码 URL。
  bytes 是明确长度的二进制数据，不追加 NUL；超限或 I/O 失败释放临时缓冲，不发布部分结果。
- 当前 EXE provider 最多保留 128 个文件，目录递归最多 16 层，原生路径受 `MAX_PATH` 约束；
  超限拒绝整个目录。该宿主上限小于未来 PXI 的 1024 文件设计上限，不代表安装预算已验收。
  单资源仍受 Runtime 和 Core/Browser 更严格预算约束。
- 独占锁使用 Runtime 给出的完整不透明名字创建原生 named mutex；已存在就返回 BUSY，
  不加 PID 后缀、不等待，也不因重复启动而绕过身份。

这些规则适用于明确确认的可信开发目录，不是恶意代码沙箱，也不证明所有 ROM、映射卷或
不可检测的文件系统别名都具有相同保证。原生能力不足或身份无法验证时拒绝，不退回词法前缀检查。

## 存储 location

从实际 EXE 路径取得同目录 `data` 根，逐层创建和核对 Runtime 提供的相对 storage key。
`ERROR_ALREADY_EXISTS` 不能单独证明目录存在；不得重试失败 mkdir、切换卷、删除数据库或重放 SQL。
保留根 anchor 的只读句柄至 location 关闭；不预先打开或创建 `app.db`，DB 的完整身份核对和
创建/迁移仍由 Runtime/DB 完成。已存在叶必须是实际包含的普通文件；父目录验证后，只在精确
枚举明确报告未找到时允许创建。CE 的无效属性值加错误码 0 不足以分类为文件不存在。

原生路径和诊断只写入 Debug 日志，不交给作者 JS；浏览器的 `positron.db` 不参与此映射。
正常重开不证明文件 FULL、I/O 故障、真实断电恢复或旧 ROM 的间歇存储故障已经解决。

## 显式 Debug 平台门

通过正式 `scripts/device_gate.ps1 -Candidate app-runtime-platform -Configuration Debug
-ModuleAuditOnly -PreserveDeployment` 建立全新完整包后，用 32 位 Windows PowerShell 运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/app_runtime_platform_gate.ps1 -RunRoot <本轮正式目录>
```

脚本调用 EXE 的 `--selftest-runtime-platform "<目录>"`，在独立 worker 消费公共 Runtime API；
两份中文目录各运行两个独立进程，每进程关闭重开两次，覆盖中文资源、重复实例 BUSY、文件写入
拒绝、路径逃逸失败不改输出、无授权拒绝、显式 SQL grant、受限 SQL 和正常恢复。该重复实例检查
发生在同进程，不冒充两个同时运行进程的 lease 验收；原生 mutex 的跨进程场景仍需专门组合门。

包内 EXE/十个 DLL 必须回读哈希一致。终态后只允许精确测试 PID 在 30 秒内正常卸载，其他
holder、不可用快照和超时均失败，最终要求零引用且无新 dump。测试证据不可覆盖；可用
`-EvidenceName app-runtime-platform-<标识>` 保留新的尝试，已存在夹具只读核验，不覆盖旧数据。
脚本只改变隔离部署目录与专用 Debug 日志，不强杀、不操作 WMDC、不改用户库；失败日志保留。
`scripts/test_app_runtime_platform_gate.ps1` 离线覆盖等待退出的成功、拒绝和超时路径，不访问设备。

这不是作者页面、Core 资源事务、跨页面队列、真实输入或 SDK 的端到端门；它们必须在目录
应用接线后独立验收，不能用此平台门代替。

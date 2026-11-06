# EXE PXI 首次安装候选取证

本文记录PXI首次安装的自动证据、未验收项和失败边界。稳定平台合同见
[PXI宿主接线](../../positron_app/RUNTIME_HOST.md#pxi-输出与首次安装接线)，当前状态归交接。

## 范围与已有检查

候选新增`--install`，通过公开Archive reader把PXI完整输出到新建apps子目录，再复用既有
PrepareRevision、原生确认、InstallIntent/Data/Commit和独立installed重开。不改公共DLL或ABI、
主库schema、网页安装权限或开发目录每次确认，不包含系统关联/更新/卸载。
确认前只清本次创建且身份仍匹配的对象；可能写意图前标记保留，失败/未知结果不自动清引用目录。

正式Debug和Release/CAB构建通过，EXE零错误/警告；最终Release经正式stage输出。
C89四项和Runtime服务六项通过，PowerShell门语法检查通过。Python独立生成14个PXI输入，
逐包testzip证明只有明确破坏的CRC控制包损坏。Release排除PXI原生自检、碰撞注入和诊断文字。
14项参数互斥/Unicode断言及原生phase10已通过下述设备门，不扩大为原生确认或Release人工验收。

## 自动设备门

通过包为`tmp/device-runs/20261106-222650-app-pxi/`，明确内置Temp，
`scripts/app_pxi_gate.ps1`记录EXE/十一DLL哈希12/12、双空间、原生phase10、
两个独立进程真实installed页面、相邻设置存储及退出零holder/crash全部PASS。
原生门覆盖完整CRC、目录/中文/空文件、危险路径/大小写冲突、取消、已有外来文件不覆盖不删除、
128条目/4MiB精确与超限、物理路径超长、manifest缺失、确认前关闭清理、成功保留与重复安装拒绝。
同时验证删除原包仍能普通重开、未知提交结果和失败迁移后资源保留。
闭库主库只有两条READY和一条FAILED，generation均为2、schema目标为1；
解出资源回拉后由Python zipfile逐字节比较，三个SHA-256资源身份3/3精确匹配。
SQL页面两个进程均验证32行typed结果、队列/回滚、失败旧页保留、离页撤销和关闭owner。
原始SQL PXI已由隔离夹具删除，普通installed页面仍正常重开；未知提交结果项READY且未重放。

普通入口的后续日志已收取为同包manual-pxi/observed-install.log：PID2745176246从中文文件名笔记安装.pxi进入confirmation-required，随后commit-terminal为READY（state=2/outcome=1），open-installed返回0，Notes作者脚本executed且errors=0。这证明普通确认链与Unicode包路径实际成功，不证明触摸观感、笔记保存或退出后免提示重开。

普通--app在新进程PID3820850954免提示重开，按新PID核对作者script-summary errors=0、页面排版/绘制完成；证据在同包notes-installed-reopen。此前零holder后冷备份主库integrity_check为ok，Notes记录READY/generation2/schema1。该证据不证明笔记内容保存、原生触摸或正常退出；普通拒绝及Notes保存仍需独立验收。
恢复确认UI、双语、Release普通启动、其他OEM/存储与挂起的FULL/断电保持原边界。

## 作者打包工具消费门

`scripts/package_app.py`通过标准库生成固定顺序/时间戳的PXI，正常valid夹具改为消费该工具，
危险/损坏包仍独立构造。七项离线测试覆盖精确资源/中文/空文件、可重复输出、已有目标不覆盖、
目录内输出拒绝、manifest缺失及128文件/4MiB精确与超限；不代替Runtime语义准入。
新包`tmp/device-runs/20261107-010021-app-pxi`原生phase10、两进程installed页面、设置存储、
12/12哈希、3/3资源摘要/ZIP字节、正常退出零引用及crash检查全部PASS。

首次部署`20261107-005928`在空间门停止，未降低18MiB需求或删除数据库。
旧`222650`完整二进制核对原stage归档后精确移除，新门结束同样归档新包二进制，
再从归档恢复旧包并逐项回读；记录分别位于各包archived-before/after-author-gate与
restored-after-author-gate。所有主库、应用库和已安装资源留在原路径，不迁移用户笔记。

## 构建与连接失败

首两次Debug以及一次补充自检构建在VS2008预生成阶段退出，Application事件确认
devenv/msenv.dll访问冲突；正式原配置重试通过，不修改工具链或断言。日志位于
tmp/app-pxi-debug-build*.log及tmp/app-pxi-debug-final-build*.log，失败不计作编译通过。

RAPI首两次Connect30秒超时，原Notes包归档未开始。恢复后当前连接无十一月Notes包，仅有九月测试目录，
内置空闲2152448字节；未按旧PID或缺失路径删除、恢复或移植旧用户库。
用户明确授权清理当前目标内置空间并重新部署。先用无产品依赖helper取得零holder，
将七个明确Positron Temp目录的94个文件逐个双回读SHA-256校验归档，复核整个清单未变后精确删除文件与空目录，
释放至29698048字节；完整归档/清单位于`tmp/device-runs/20261106-222146-pxi-space-cleanup/`。
未清Windows或其他未知目录，不重置/选择/重连设备，不强杀进程。原Notes冷备份仍保留本地，未自动迁入当前目标。

首个包`20261106-222252-app-pxi`停在原生phase1。夹具原先用缺失apps目录的glob结果推断空目录，
已改为已知部署父目录的完整枚举，并增加逐案例rc/cleanup日志；原产品解包/清理策略和断言未改。
新包phase10通过，原失败日志、主库和输入保留；只把回读与原stage哈希相同的重复二进制归档后释放设备副本。

内置存储不足时，只有明确授权且已核对的归档才能释放设备副本；不因已绑定数据自动换卷。
后续正式部署仍须重新核对空间与引用，原失败不能追认为通过。

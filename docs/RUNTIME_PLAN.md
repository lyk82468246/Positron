# Positron 纯资源应用运行时计划

## 目标与边界

第三方开发者只提供 manifest、HTML、JS、CSS 和资源，不编译或携带 EXE、DLL、原生插件。
统一由 `positron.exe` 执行；它既是浏览器，也是资源应用的宿主。首版以独立的离线笔记示例
验证运行时，再评估内置页面迁移，不先重写 settings/history/downloads。

本文件记录已批准的设计和分工，不表示功能已经实现。动态进度、证据及唯一下一步归
[HANDOFF](../.agents/HANDOFF.md)，公共所有权服从[架构](ARCHITECTURE.md)。

- 应用页面视作普通网页，复用现有窗口、地址栏、标签页、软键、native 控件、焦点、滚动和导航；
  不建立无地址栏的第二套应用窗口，不自绘另一套控件或滚动条。
- 同一 EXE 可用独立进程承载指定应用，但仍使用现有浏览器 UI；这不等于完整进程沙箱。
- 首版支持解压目录和 `.pxi`（Positron eXtension Installer）包；先完成目录模式，再完成安装。
- 首版为用户明确确认的可信应用，不做签名、不证明发布者身份、不宣称能安全执行恶意代码。
- 首批只有离线资源和受限 SQL 存储；不开放网络服务、任意文件访问、进程启动、Node.js、
  原生插件、worker/module、自动更新或热更新。现有普通 HTTP(S) 浏览不因此禁用。

## 分层与工作分配

```text
资源应用：HTML / JS / CSS / manifest
        ↓ 版本化 JS SDK
Browser 受控异步服务桥
        ↓
positron_runtime.dll：应用身份、包规则、资源解析、服务授权、存储服务
        ↓
既有 Core / Browser / DB / JSON 等公共 DLL
```

| 负责人 | 交付边界 |
|---|---|
| 主线与DLL会话 | 新 Runtime DLL、manifest/身份/资源/授权合同、DB 受限执行与预算、Core/Browser 的 `app://` 公共接线、系统 ZIP 取证与公共安装合同、DLL 构建及设备门 |
| EXE会话 | 命令入口、现有页面/标签接线、平台 worker 与 I/O 回调、安装确认、JS SDK、纯资源示例、宿主回归与完整包部署 |
| EXE发布接线 | 沿已有 CAB 工程加入 Runtime DLL 与 `.pxi` 关联；遵守部署工程 ASCII/CRLF/无 BOM 规则，不改发布版本策略 |

Runtime 不复制 HTML/CSS/DOM、SQLite 或网络实现，不使用其他 DLL 的内部源码。
EXE 拥有 WM 窗口、消息、线程和平台文件操作，通过版本化回调提供平台能力；公共安装规则
与资源校验仍由 Runtime 拥有。DB handle 由 worker 单线程持有，live Core/Browser/JS handle
不送入 worker。公共接口保持 C89、VS2008、WM6 ARMV4I、稳定 C ABI、UTF-8、opaque handle、
size/version、明确长度和所有权。旧 ABI/消费者不变。

公共 DLL 工作投递给主线与DLL会话，EXE会话不代做。若接口不足，停止对应 EXE 接线并协调，
不在 EXE 或 test_host 临时复制 URL、授权或 SQL 产品语义。各阶段独立提交、串行构建和验收；
同一时刻只有一个会话占用正式构建、stage 和设备门。test_host 仅承载 fixture、接线与断言。

## 应用标识与 manifest

### 应用 ID

参考 Java/Android 包名形状，但以下规则才是本项目的权威合同：

- 至少两段，由 ASCII `.` 分隔；点不在首尾，不允许空段。
- 每段以 ASCII 字母开头，其余字符仅允许 ASCII 字母、数字、下划线。
- 完整 ID 最多 63 字节，不接受 Unicode 字母、空白、连字符或经过 URL 编码伪装的 ID。
- 接受 ASCII 大小写，统一转小写作为规范身份；安装冲突、更新、锁、资源 authority 和数据
  命名空间都使用同一规范值，不按 locale 做大小写折叠。
- 等价语法为 `[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+`，并单独检查总长度。

`com.example.notes`、`org.demo_app.v2` 合法；`notes`、`.com.demo`、`com.demo.`、
`com..demo`、`com.2demo`、`com.demo-app`、`com._demo` 不合法。
`Com.Example.Notes` 与 `com.example.notes` 是同一身份，不得形成两份安装或数据。
ID 是逻辑标识，不直接拼为设备文件路径；合法 ID 不因设备保留文件名而被错误解释为路径。

### 根 manifest

根目录使用 UTF-8 `manifest.json`，字段为：

| 字段 | 首版含义 |
|---|---|
| `manifestVersion` | 固定为整数 `1`，未知格式拒绝 |
| `id` | 上述应用 ID |
| `name` | 应用显示名称，不替代规范身份 |
| `version` | 应用版本，更新时递增，不使用文件时间代替 |
| `runtimeApiVersion` | 请求的 SDK/Runtime API 版本；不支持则拒绝启动 |
| `entry` | 包内 HTML 相对路径，不接受设备绝对路径或远端 URL |
| `permissions` | 首版仅接受 `storage.sql`；未知能力拒绝，不静默扩大授权 |
| `migrations` | 按递增 schema 版本登记的包内 SQL 文件 |

最终 C 类型、manifest 字段容量/版本编码和 JS 消息形状由主线与DLL会话先固化为版本化公共
合同及测试，再供 EXE 消费；不能让两侧各写一套解析器或以隐含默认值绕过版本拒绝。

## app 资源与页面生命周期

资源地址为 `app://<规范应用ID>/<路径>`，不使用 `positron-app://`。
`positron://` 继续表示原浏览器内部页面；HTTP(S) 继续走 HTTP DLL，包内资源不送入网络层。
`app` authority 是应用标识而不是 DNS 名称，不套用不允许下划线的网络 hostname 校验。

Runtime 统一解析相对引用、根路径、查询和片段，返回验证后的包内资源映射；EXE 仅执行平台
读取。Core/Browser 仍拥有文档、资源 gate、脚本、导航与 history 语义。拒绝路径穿越、编码后
逃逸、绝对设备路径、跨应用资源、非法 authority 和不支持协议；不能靠字符串前缀判断目录归属。

应用身份来自 Runtime 已验证的实例，不来自地址字符串。普通网页输入、链接或重定向到
`app://` 不自动授权，也不得自动安装包。只有显式加载/安装流程产生的有效身份，才能在对应
session 初始化完成、作者代码运行前注册服务。页面/标签切换、重载及离页继续使用现有
generation/撤销/teardown 流程，回到应用时必须重新验证当前实例和文档，不沿用旧 token。

应用包加载 JS、CSS、图片仍使用统一资源事务和 ScriptSession，不复用“内置演示页不执行
脚本”的快捷路径。网页 JS 设置与显式启动可信应用的作者代码策略分离，不扩大现有脚本
heap/执行预算。应用页面不加载远端脚本、样式、图片；不注册网络/任意文件服务。
用户显式进入 HTTP(S) 网页时沿普通浏览路径运行，离开应用前撤销应用能力；地址栏与菜单
本身不根据应用页面替换为另一套设计。

## 目录、PXI 与安装

### 命令入口与身份隔离

- `positron.exe --app-dir "<目录>"`：运行解压后的开发目录。
- `positron.exe --install "<文件.pxi>"`：确认安装后启动。
- `positron.exe --app "<应用ID>"`：运行已安装应用。

保留现有浏览器 URL/命令行兼容性，不把普通 URL 误判为应用目录。命令路径支持 Unicode
和正确引号处理；来自网页的路径不能成为上述入口的授权替代。

EXE 旁使用 `apps`、`data` 子目录；不可写、缺卡或损坏时明确失败，不静默改目录或删库。
各应用拥有独立数据，不能访问浏览器的 `positron.db`。开发身份绑定规范目录路径及规范 ID，
与已安装身份分离；仅声明相同 ID 不获得已安装应用的数据。应用身份所用目录映射由 Runtime
管理，不向 JS 暴露数据库或物理资源路径。

同一身份只允许一个运行进程，重复启动明确提示；首版不引入实例 IPC。资源在运行期间不热
更新，修改开发目录后须正常退出重启。首版不要求把现有浏览器改为多进程架构。

EXE 的 WM 平台 callback、保留句柄策略与更严格的目录准入预算见
[宿主适配器](../positron_app/RUNTIME_HOST.md)。其显式 Debug 平台门与完整目录应用门分开，
不能以原生读取/SQL 正常重开代替页面资源、授权队列、SDK 或笔记示例验收。

### PXI 系统 ZIP 前置门

PXI 是标准 ZIP 容器，根目录含同一 manifest，不增加 CRX 签名头、不实现 Chrome 扩展 API。
仅调用经目标设备验证的系统 ZIP 能力，不自行编写 ZIP/Deflate 解码，不引入上一方案的 zlib
或其他解压库，也不擅自改为 CAB 容器。

主线与DLL会话先确认系统组件、接口、支持格式、完成通知、错误获取及清理合同。本地 SDK
的 Shell 自动化接口声明不等于 ROM 已提供 ZIP provider；桌面 CAB 解包 API 不是 ZIP 能力。
须证明 Unicode 文件名、输出目录约束、取消/失败清理、不覆盖、路径逃逸拒绝及解包规模上限。

若系统 ZIP 能力不存在，或不能满足约束及可靠完成检测，停止 PXI 设备安装并反馈；目录应用
继续，不以自动引入第三方库作为 fallback。更改此决定必须重新取得用户授权。

首版包预算为压缩包 16 MiB、解包总量 32 MiB、单文件 8 MiB、1024 文件、16 层目录；
Core/Browser 对单项资源已有更严格限制时仍遵守更严格限制。拒绝加密、多卷、ZIP64、链接、
重复/大小写冲突路径、损坏归档及不能验证的头/目录不一致，不以“能解出文件”代表安装成功。

### 安装、更新与系统关联

安装前展示名称、规范 ID、版本、能力及“发布者未验证”提示；无签名意味着 ID 不证明作者。
同 ID 更新须用户确认、版本递增且应用已退出；新代码写入新版本目录，全部验证通过后切换
活动版本。失败保留旧版本及用户数据，不覆盖运行中的资源，不自动回退数据 schema。

系统 `.pxi` 关联进入 `--install` 确认流程，不直接执行归档内容；沿既有 CAB 工程接入，不
修改其他应用关联或全局压缩设置。卸载只移除仍归 Positron 所有的关联，保留应用数据。
目录模式不依赖 PXI/系统关联完成；安装、更新的原子发布规则不能宣称物理断电已验收。

## SQL 服务与 JS SDK

### DB 前置合同

先由 DB DLL 提供默认关闭的版本化受限执行能力，不把现有 local-full SQL 直接暴露给 JS：

- 通过 SQLite authorizer 控制语句，EXE 不通过搜索关键词模拟限制。
- 普通请求只允许参数化查询、数据修改；DDL 只允许 manifest 登记的迁移。
- 禁止附加数据库、连接策略修改、扩展加载、私有 metadata 访问及应用自行控制宿主事务。
- 每请求 VM 指令预算和协作取消回调；回调仅读取宿主原子取消状态，不跨线程调用 DB handle，
  不重入数据库，不把预算描述成可抢占 VFS/I/O 的硬墙钟期限。
- 旧 ABI、同步模式和现有浏览器数据行为不变；迁移失败不推进 schema、不删文件。

### SDK 和结果预算

SDK v1 用现有 callback 异步服务桥，不强制 Promise：

```javascript
Positron.db.query(sql, params, callback);
Positron.db.execute(sql, params, callback);
Positron.db.transaction(statements, callback);
```

版本化 SDK 将上述方法映射到显式 allowlist；submit 只复制/入队，不阻塞 UI。
作者 JS 不指定数据库路径或其他应用身份。迁移在作者代码前由 Runtime 从 manifest 读取并
执行，不开放任意文件路径的迁移接口；失败不启动作者代码，不支持 schema 降级。

| 项目 | 首版预算 |
|---|---|
| 请求和结果 JSON | 各最多 4096 字节，服从现有 Browser 桥容量 |
| Runtime 每会话未完成请求 | 最多 8 个，不扩大 Browser 自身上限 |
| SQL / 参数 | 每条最多 2048 UTF-8 字节、16 个绑定参数 |
| 批事务 | 最多 8 条，仍满足整体请求大小限制 |
| 查询结果 | 最多 16 行，JSON 最多 4096 字节；超限失败，不静默截断 |
| 执行预算 | 每请求累计最多一百万 VM 指令，批事务共享 |
| 数据库 | 保持既有约 16 MiB 配额，不新增无限缓存 |

支持 NULL、UTF-8 文本、有限实数、显式类型的十进制 int64；不能把任意 int64 转成会丢精度
的 JS Number。首版不开放 BLOB。应用用绑定游标和 LIMIT 分页，不把数据库行/statement
句柄送给 JS。写入只在 COMMIT 成功后返回成功；错误/预算/取消不留下部分事务或假成功。

worker 只执行复制请求，不接触 DOM/ScriptSession；结果回 UI owner 线程核对应用身份、
tab、generation、token 后交付。离页先撤销服务，迟到结果丢弃且不重新执行写入；关闭先
协作取消、收敛事务并等待 worker，再释放页面和 DLL，不 TerminateThread。

搜索框和记录列表是普通 HTML/CSS/JS 可复用示例，沿现有控件投影；不新增搜索 DLL 或另一套
原生搜索控件。查询执行和权限由原生层实现，笔记、历史、下载各自拥有业务 schema，
不建立混用所有业务的万能记录表。浏览器专属服务不向第三方 SQL 开放。

## 阶段与验收

| 阶段 | 交付与完成条件 |
|---|---|
| 计划与交接 | 本计划进入仓库，架构/交接/路线图按职责引用；审计、提交、推送后向主线与DLL投递前置任务 |
| 公共 DLL 前置 | 主线与DLL完成受限 DB、Runtime、app 资源合同及系统 ZIP 取证；版本化头文件、示例调用、预算/失败/生命周期和设备门可供宿主消费 |
| 目录应用 | EXE接入原窗口/页面/标签/服务桥及worker；纯资源笔记覆盖新增、编辑、删除、文字搜索、分页、正常重启恢复，两个应用数据隔离 |
| PXI安装与关联 | 仅系统 ZIP 门通过后接入确认安装、更新、启动和扩展名关联；失败保持旧安装/数据，目录模式独立可用 |
| 内置页面复用评估 | 示例通过后先评估settings，再独立评估history/downloads，不在首批同时迁移 |

每阶段独立提交、推送及串行正式 Debug/Release 构建；C 改动先运行
`python scripts/test_c89ize.py`，提交前运行 `python scripts/audit_repo.py`、相邻回归与公共 ABI
检查。功能修改必须有定向设备门；文档批准不冒充源码、设备或发布基线。

验收覆盖：

- ID合法/非法/边界长度、大小写等价、相同ID开发/安装隔离、普通网页及伪造URL无授权。
- 目录与PXI资源一致性、中文路径、相对JS/CSS/图片、片段、跨应用/编码目录逃逸拒绝。
- 系统ZIP缺失、损坏/超限包、安装取消、版本冲突、更新失败保持旧版本，用户数据不覆盖。
- 参数绑定、中文/引号、int64精度、16行分页、JSON超限、危险SQL拒绝、预算、事务回滚、
  迁移失败/拒绝降级及正常重开；准备足量隔离记录，不只使用零星真实数据。
- session/generation/token撤销、排队拒绝、迟到结果、离页/关闭/正常退出零引用、无新崩溃。
- 现有浏览器地址栏/软键/标签/导航/脚本策略回归，以及真实原生输入、SIP、滚动、旋转和DPI。

使用同一正式stage的完整包，不混用DLL、不重置WMDC、不改用户数据库。
验收要求正常退出；经用户授权的进程恢复清理不能冒充正常退出或事务恢复通过。
原像素间歇失败及完整UI门超时继续独立记录，不借本计划关闭；用户已挂起的断电/FULL
验收不重新开启。稳定调用合同归组件文档，动态证据归 `.agents/`，临时材料不入Git。

## 参考与已知前置事实

- [架构与所有权](ARCHITECTURE.md)、[Browser服务桥](../positron_browser/README.md#受控异步应用服务)、
  [应用数据设计](../positron_app/APPLICATION_DATA_PLAN.md)是既有能力边界，不表示Runtime已交付。
- [Chromium CRX3格式](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/components/crx_file/crx3.proto)
  的额外头部含签名证明；PXI只借鉴manifest加归档，不复制签名格式。
- [Microsoft FDICopy说明](https://learn.microsoft.com/en-us/windows/win32/api/fdi/nf-fdi-fdicopy)
  描述CAB解包和桌面平台要求，不能据此保证WM6系统提供ZIP API。

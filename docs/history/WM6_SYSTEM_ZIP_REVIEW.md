# WM6 系统 ZIP 取证与 WM6.5 参考

本文保存 2026-10-29 对本机 SDK 的离线检查和当前已连接设备的只读取证，不是 PXI 安装验收。
当前安装规则见 [Runtime 计划](../RUNTIME_PLAN.md#pxi-系统-zip-前置门)。

## 结论与证据层级

兼容性准入以 WM6 为基线。四个本机 WM6 英文 SDK 镜像的 IMGFS 和 XIP 目录、实际 Explorer
委托组件、Shell/Office 导入导出与三类 hive 已检查，目前没有找到完整的系统 ZIP 归档调用
路径。WM6 自带 zlib 解压原语已确认，但它不提供 ZIP 文件枚举和解包策略。

WM6.5 镜像的 ZipView/MCF 导出只保留为后文参考，不作为产品依赖，不把它的发现外推到 WM6。
也不能由四个 WM6 SDK 镜像的结果断言所有 WM6 ROM 没有解压功能。用户设备实际能够解压的
实现仍须从该设备的文件关联和组件取得证据，不能用另一版本的系统 DLL 替代。

用户观察到的 Explorer 解压功能值得沿真实关联追踪；上述 SDK 镜像中的实现不能直接代替
用户当前 ROM 的事实。原 TEST1372 只扫描 ZIP 文件名和少数注册项，未执行解压，也未检查
`mcf.dll` 的导出或 Explorer 的实际委托实现。其未命中结果不能关闭本方向。

## SDK 头文件和库

本机 `C:\Program Files (x86)\Windows Mobile 6 SDK` 的 Samples 有 1511 个文件，未找到
直接使用 ZIP 解压的示例；`multicabinstall` 通过 WCELoad/Setup DLL 安装 CAB，不是 ZIP。
PocketPC ARMV4I 的 `shldisp.idl`/`shldisp.h` 声明了 `IShellDispatch::NameSpace`、
`Folder::CopyHere`、`CLSID_Shell` 和 `CLSID_ShellDispatchInproc`；`uuid.lib` 中也有这些 GUID。
这些是编译声明和常量，不是对应 COM 实现或 ZIP namespace 的证明。

`ceshell.exp` 提供 `DllGetClassObject`、`SHGetDesktopFolder` 和 `SHFileOperationW` 等入口。
因此以后设备验证应分别检查 Shell automation 和 IShellFolder 路径，不能只查一个 ProgID。
桌面 [CopyHere 文档](https://learn.microsoft.com/en-us/windows/win32/shell/folder-copyhere)
明确没有完成通知，且 ZIP namespace 可能忽略 flags；不能直接套用成 WM6 的同步解压保证。

## WM6 SDK 目录与组件核验

解析本机镜像 IMGFS 的完整目录链，不以原始字符串扫描代替目录结果：

| 镜像文件 | IMGFS 目录块 / 文件项 | IMGFS 中的 MCF / ZipView |
|---|---|---|
| `Deviceemulation/0409/PPC_USA.BIN` | 41 / 950 | 未发现 |
| `Deviceemulation/0409/PPC_USA_GSM_QVGA_VR.BIN` | 44 / 1023 | 未发现 |
| `Deviceemulation/0409/PPC_USA_GSM_VGA_VR.BIN` | 44 / 1028 | 未发现 |
| `Deviceemulation/0409/PPC_USA_GSM_VR.BIN` | 44 / 1023 | 未发现 |

路径相对于 SDK 的 PocketPC 目录。每个 WM6 镜像另有物理偏移 0 与 0x30000 的两个 XIP
目录，分别含 1 个模块/0 文件和 25 个模块/9 文件；两者均未发现 MCF、ZipView 或其他名称
含 zip 的模块。boot.hv 和 boot.rgu 已从 XIP 原样提取，不再只检查 IMGFS。

进一步提取并解码实际组件的 e32/o32/section 数据，检查导出及导入表：

- `fexplore.exe` 自身只依赖 FBROWSER.DLL 和 COREDLL，其浏览器入口委托给前者 ordinal 10；
  只看这个小 EXE 不足以检查 Explorer 功能。FBROWSER.DLL 已一并核查，它依赖 Shell、OLE、
  控件及原生系统组件，没有导入 MCF/zlib 或 ZIP 归档函数，也未发现相关字符串或 Shell ZIP CLSID。
- `ceshell.dll` 的 24 个命名导出提供通用 Shell/文件操作。实际 COM 登记可读出三个指向
  ceshell.dll 的类，但不含 Shell.Application、ShellDispatchInproc 或 CompressedFolder。
  不能因 DllGetClassObject 存在就假定支持桌面 Shell automation。
- `office.dll` 的完整 PE 导入未出现 MCF/ZIP 路径，未发现 `.zip`/zipfile/ZipView 字符串。
- `zlib.dll` 有 18 个命名导出，包含 inflate、inflateInit2_、inflateEnd、crc32、uncompress
  等压缩流接口。它没有归档枚举/文件名/输出目录接口；不能把这些函数当成 UnzipFile，
  也没有因此实施当前计划禁止的自写 ZIP 容器解析。

四种镜像中的 Explorer、FBROWSER、CEShell、zlib 分别具有相同的解码分析映像 SHA256，
Office 完整文件也相同。因此它们是四个镜像配置中的同一套实现，不能夸大为四套独立解压实现
均经过验证。静态导入缺失和字符串负结果不排除动态扩展，必须与真实关联结合判断。

## WM6 注册表结构核验

没有只对 hive 搜索关键词。本地只读 walker 按
[hvtool 格式参考](https://github.com/nlitsme/hvtool/blob/master/tools/hvtool.cpp)
解析 section/index、roots、key sibling/child 和 value 链。12 个 hive 均通过原有 MD5、
长度、引用/环检测与全部 key/value 可达性检查，总计 13454 个键、22161 个值。

| WM6 镜像 | default.hv 键/值 | user.hv 键/值 | boot.hv 键/值 |
|---|---|---|---|
| PPC_USA | 2983 / 4797 | 154 / 293 | 82 / 200 |
| PPC_USA_GSM_QVGA_VR | 3135 / 4993 | 195 / 430 | 82 / 200 |
| PPC_USA_GSM_VGA_VR | 3135 / 4996 | 195 / 431 | 82 / 200 |
| PPC_USA_GSM_VR | 3134 / 4990 | 195 / 431 | 82 / 200 |

这些 hive 均没有 HKCR 下的 `.zip`、zipfile、CompressedFolder、Shell.Application，
也没有对应 Shell/ShellDispatchInproc/ZIP folder CLSID。对照能正确解析 `.cab → cabfile`
及其 `wceload.exe "%1" /nodelete` 命令和 `.txt → txtfile`，因此负结果并非 walker 没读到 HKCR。
这证明原始 SDK 登记状态，不代表用户设备的后装/OEM 登记，也未实测 CoCreateInstance。

本轮 WM6 证据在 `tmp/wm6-zip-review-20261029/`：`sdk-imgfs-inventory.json`、
`xip-inventory.json`、`binary-review.json`、`hive-review.json` 及每镜像的只读解码材料。
脚本分别为 `tmp/wm6_imgfs_readonly.py`、`tmp/wm6_xip_readonly.py`、
`tmp/wm6_binary_readonly.py`、`tmp/wm6_hive_readonly.py`。原始镜像 SHA256 保存在目录清单，
所有输入只读；没有挂载 hive 到电脑注册表或执行 ARM 系统组件。

## WM6.5 参考：ZipView 和 Mcf.DLL

以下来自 `DeviceemulationV650/0804/PPC_CHS_GSM_VR.BIN`，不是 WM6 兼容性证据。
其 IMGFS 有 63 个目录块、1705 个文件项，存在独立 ZipView.exe 和 mcf.dll。
ZipView 实际导入 McfInitialize、McfCreateArchiveOnFile、McfUninitialize。
本机 SDK 未找到 MCF 头文件、import library、示例或公开生命周期说明；未完成完整调用合同。

WM6.5 镜像 SHA256：`7c04e8d61132a504c289ae018dff6d85ae6c84b316c0898ea7646d0f47d3bf6f`。
其 IMGFS 位于物理偏移 0x300000，使用 XPR；本机镜像的 flash 索引页须先与逻辑数据分离。
目录格式参考 [eimgfs 源码](https://github.com/nlitsme/eimgfs/blob/master/eimgfs.cpp)，
本地脚本逐个检查目录魔数、链环、名称指针、数据索引和解码长度。解码调用宿主 Windows 的
XPRESS 服务，只用于离线 ROM 取证；没有给 Positron 添加 ZIP/Deflate 实现或运行镜像代码。

提取的 ZipView 长 53496 字节，SHA256 为
`d6c0e6632f23aa8028318aa9aebae4bab4857ea34110add25b89cf52b406f4a1`。
它是 ARM/Thumb COFF machine 0x01c2 的 PE，不是桌面 x86；自身没有 export directory。
它除 MCF 外还依赖 OFFICE、OLE32、AYGSHELL 等组件，不能当成独立可分发的 ZIP 库复制部署。

MCF 同镜像导出表中已恢复的名称与 ordinal：

| ordinal | 名称 |
|---|---|
| 1 | CreateFileStream |
| 2 | McfCreateArchiveOnFile |
| 3 | McfCreateControlledPackageOnStream |
| 4 | McfCreatePackage |
| 5 | McfCreatePackageOnFile |
| 6 | McfCreatePackageOnStream |
| 7 | McfInitialize |
| 8 | McfOpenControlledPackageOnStream |
| 9 | McfOpenPackage |
| 10 | McfOpenPackageOnStream |
| 11 | McfUninitialize |

ordinal 只属于本镜像，不能作为跨 ROM 调用合同。导出名称不包含返回对象的完整方法表；
Create 成功也不证明枚举、目录项安全、CRC、尺寸限制或取消已可用。

ZipView 的导入跳板和实际 ARM call site 已交叉核对：VA 0x16750 调用归档创建跳板
0x173b0，IAT 为 0x18180；初始化/反初始化跳板分别为 0x173c0/0x173d0。
归档创建调用具有七个机器参数，其中可见只读打开相关常量及输出指针；MCF 对应函数检查
初始化状态和输出参数，随后经对象方法调用交付结果。它不是仅传一个文件名的无参数猜测。
归档出口 QueryInterface 使用的 IID 为 `{3c7e806c-2a93-428c-bf7c-fd8028806e75}`，
可见临时对象 Release 调用；它不是标准 IStorage IID，不能直接套用其方法表。
尚未恢复全部参数类型、对象方法和所有权，因此没有生成可供产品使用的 C prototype。

ROM default.hv 的离线字符串还包含 `.zip`、`zipfile` 和 `zipview.exe %1`，与独立查看器
链路一致；没有把相邻字符串当作完整的 registry tree 解析或当前设备关联查询。
未找到特定桌面 Shell CLSID 字符串，也不等于已经实测 CoCreateInstance 失败。

## 当前设备与剩余验证

此前 RAPI 初始化曾遇到权限错误和超时；用户手动连接后，本次提权只读查询已经成功。
设备报告 CE 5.2.318、platform 3，AKU 为 `.0.0.0`。这些字段不能单独证明 ROM 身份或
所有系统功能；用户尚需确认它是否就是此前观察到 Explorer 可解压 ZIP 的同一设备和 ROM。

当前设备的 HKCR `.zip`、zipfile、CompressedFolder 及所查 Shell automation/ZIP folder
CLSID 均在打开键时返回 2。对照 `.cab → cabfile`、`wceload.exe "%1" /nodelete` 和
`.txt → txtfile`、`pword.exe %1` 正常，因此不是整次注册表查询失败。
`\Windows\mcf.dll`、ZipView.exe、ia_zip.exe 和 zipfldr.dll 的文件属性查询也返回文件不存在。
这些结果只适用于当前目标及已查询路径，不能否定其他 ROM、其他名称的组件或用户此前的观察。

Explorer、FBROWSER、CEShell、Shell32、AYGShell 和 zlib 等组件存在；其中 ROM 模块不能
通过当前 RAPI 文件复制路径回读，不能把复制失败写成组件不存在。可成功回读的 office.dll
SHA256 为 `092318dbdbe50c80bdc6f4410284cb589c42b4a4b1a96ae9a12a3abcb8d413c0`，
与四个 WM6 SDK 镜像中的该文件一致；这只确认一个文件相同，不证明整个 ROM 相同。

本次设备证据在 `tmp/wm6-zip-device-20261029-143952/`：`os-version.txt`、
`registry.json`、`windows-components.json`、`rom-copy-results.json` 和成功回读的 Office 文件。
全过程没有连接/选择/重置设备、设备写入、部署、启动程序或结束进程，也没有执行解压。

下一步先确认此前实际解压的设备和 ROM；若是当前目标，则沿用户实际操作所启动的程序继续
定位，而不是假定处理程序必然名为 ZipView。找到提供者后，再在隔离原生 fixture 中验证
ABI、只读枚举、逐项受控输出与释放，最后测试指定目录、Unicode、损坏/超限、取消和完成检测。
不能直接用 SDK 镜像替换设备系统文件，也不能把 OEM/后续版本特性标为通用 WM6 基线。
若恢复不到可靠的对象生命周期，保留为有实现但未合格的系统接口，不猜签名上线。

本地证据在 `tmp/zip-shell-review-20261029/`：`sdk-imgfs-inventory.json`、
`sdk-rom-strings.json`、`zipview-pe.txt`、`sdk65-module-mcf.dll/metadata.json`、
`mcf-zipview-disasm.txt`、`mcf-interface-evidence.json` 及相应提取内容；复现脚本为 `tmp/zip_imgfs_readonly.py`、
`tmp/zip_disasm_readonly.py`、`tmp/zip-shell-readonly.ps1`。ROM、提取二进制、参考源码和
分析工具仅在本地，不能提交仓库或混入应用包。本轮没有产品源码/ABI 修改，也未跑设备门。

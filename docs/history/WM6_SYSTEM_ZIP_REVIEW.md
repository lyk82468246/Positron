# WM6 系统 ZIP 与 MCF 接口取证

本文保存 2026-10-29 对本机 SDK 的离线检查，不代表当前设备具备该组件，也不是 PXI 安装验收。
当前安装规则见 [Runtime 计划](../RUNTIME_PLAN.md#pxi-系统-zip-前置门)。

## 结论与证据层级

本机 SDK 的 WM6.5 中文镜像确实包含独立 `ZipView.exe`，其 PE 导入表调用 `Mcf.DLL` 的
`McfInitialize`、`McfCreateArchiveOnFile`、`McfUninitialize`。同一镜像的 `mcf.dll`
目录与导出表也已核对。这是实际二进制入口，不只是名称推测或桌面 Shell 示例。

尚未找到本机 SDK 对 MCF 的头文件、import library、调用示例或公开生命周期说明。
所以正确判断是“发现系统组件的未确认公开程度的导出接口，需要继续恢复合同并验证”，
不能写成“WM6 没有 ZIP API”，也不能提前写成“可作为安装 provider”。

用户观察到的 Explorer 解压功能值得沿真实关联追踪；上述 SDK 镜像中的实现不能直接代替
用户当前 ROM 的事实。原 TEST1372 只扫描 ZIP 文件名和少数注册项，未执行解压，也未检查
`mcf.dll` 的导出。其未命中结果不能关闭本方向。

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

## SDK 镜像目录核验

解析本机镜像 IMGFS 的完整目录链，不以原始字符串扫描代替目录结果：

| 镜像文件 | IMGFS 目录块 / 文件项 | IMGFS 中的 MCF / ZipView |
|---|---|---|
| `Deviceemulation/0409/PPC_USA.BIN` | 41 / 950 | 未发现 |
| `Deviceemulation/0409/PPC_USA_GSM_QVGA_VR.BIN` | 44 / 1023 | 未发现 |
| `Deviceemulation/0409/PPC_USA_GSM_VGA_VR.BIN` | 44 / 1028 | 未发现 |
| `Deviceemulation/0409/PPC_USA_GSM_VR.BIN` | 44 / 1023 | 未发现 |
| `DeviceemulationV650/0804/PPC_CHS_GSM_VR.BIN` | 63 / 1705 | `mcf.dll`、`ZipView.exe` |

上述路径都相对于 SDK 的 PocketPC 目录。结果不证明所有 WM6 英文 ROM 缺少功能，不扫描
目标设备的后装文件，也不把某个 IMGFS 目录的负结果扩大为所有分区或所有 ROM 的结论。

WM6.5 镜像 SHA256：`7c04e8d61132a504c289ae018dff6d85ae6c84b316c0898ea7646d0f47d3bf6f`。
其 IMGFS 位于物理偏移 0x300000，使用 XPR；本机镜像的 flash 索引页须先与逻辑数据分离。
目录格式参考 [eimgfs 源码](https://github.com/nlitsme/eimgfs/blob/master/eimgfs.cpp)，
本地脚本逐个检查目录魔数、链环、名称指针、数据索引和解码长度。解码调用宿主 Windows 的
XPRESS 服务，只用于离线 ROM 取证；没有给 Positron 添加 ZIP/Deflate 实现或运行镜像代码。

## ZipView 和 Mcf.DLL

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

设备端只尝试已有 RAPI helper 的只读会话：沙箱内返回 0x80070005，提权后初始化等待
30 秒超时。未取得本轮设备 registry/module 证据，不能归因为 WMDC GUI 损坏。
未连接/选择/重置设备、写注册表、部署、启动解压器或结束用户应用。继续依赖用户手动连接。
用户随后明确选择本轮先完成离线分析，不继续重试设备。

连接可用后，先读 `.zip` 的真实关联，再读取对应组件及 `mcf.dll`/Shell 注册，不能直接用
SDK 镜像替换设备系统文件。然后在隔离原生 fixture 中验证真实版本的 ABI、只读枚举、
逐项受控输出与释放，最后才测试指定目录、Unicode、损坏/超限、取消和完成检测。
若恢复不到可靠的对象生命周期，保留为有实现但未合格的系统接口，不猜签名上线。

本地证据在 `tmp/zip-shell-review-20261029/`：`sdk-imgfs-inventory.json`、
`sdk-rom-strings.json`、`zipview-pe.txt`、`sdk65-module-mcf.dll/metadata.json`、
`mcf-zipview-disasm.txt`、`mcf-interface-evidence.json` 及相应提取内容；复现脚本为 `tmp/zip_imgfs_readonly.py`、
`tmp/zip_disasm_readonly.py`、`tmp/zip-shell-readonly.ps1`。ROM、提取二进制、参考源码和
分析工具仅在本地，不能提交仓库或混入应用包。本轮没有产品源码/ABI 修改，也未跑设备门。

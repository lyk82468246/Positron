# WinWorld 脚本初始化阻塞调查

## 结论与证据边界

2026-09-29 用户连续报告 hamburger 无反应。当前证据证明，菜单所依赖的 jQuery 和 Bootstrap 没有完成初始化；不能把单独 document click 合同通过解释为真实页面菜单可用，也不能继续要求用户重复点击来验证尚未执行成功的脚本。

设备日志 `tmp/device-runs/debug-capture-20260929-230751/positron-debug.log` 全程属于 PID 2527611114，主文档最终提交到 `https://winworldpc.com/home`。jQuery 3.5.1 的 89476 字节源码已取得，返回 `-3`，异常为 `native callback '__pcoreFormProperty' failed`。Bootstrap 4.6.2 随后明确报告缺少 jQuery；两个站点脚本报告 `$` 未定义。

该次 heap peak 为 2420783 字节，固定上限为 3145728 字节。当前首个已知阻塞不是此前的 `-6` 内存上限错误；这不保证补齐 DOM 后执行完整库仍能满足同一预算。不得继续放大 heap 来掩盖 DOM 异常。

## Browser 与 detached DOM 的不一致

jQuery 初始化会创建未插入页面的 input、textarea、select 和 option，检测属性、克隆及 HTML fragment 行为。Browser `document.createElement()` 创建的 wrapper 没有 Core 页面 id，但 `selected` getter 仍调用 `__pcoreFormProperty`。

本地探针提取实际 Browser bootstrap 字符串，在 Node VM 中用抛错 sentinel 替代 native callbacks。下述独立表达式可重现空 id 调用：

```javascript
var select = document.createElement('select');
var option = select.appendChild(document.createElement('option'));
option.selected;
// __pcoreFormProperty({id: '', op: 'getSelected'})
```

这与设备异常以及 jQuery 的 `support.optSelected` 探测吻合，是当前首个阻塞点的强证据；设备日志尚未记录完整 JS 栈，因此不声称已经取得设备级精确源行。Node sentinel 不是 Duktape/Core 集成测试，不能用于宣称整份 jQuery 执行成功或失败。

继续审查还发现 `document.implementation.createHTMLDocument` 未实现，而 jQuery 的初始化会调用它。只绕过 `selected` 仍不足以完成初始化。

## 前几轮临时补丁的问题

未提交 Browser 实验修改了 detached checked/value 和 innerHTML。自动探针揭示：

- `.checked=false` 后设置 checked attribute 会错误覆盖当前值，缺少 dirty 状态区分。
- 设置再移除 checked attribute 后，当前 checked 仍为 true。
- `.value='live'` 后设置 `.defaultValue='default'` 会把当前值一并覆盖。
- `<textarea>x</textarea>` 的克隆 defaultValue 为空，未保留文本默认值。
- 临时 Browser HTML parser 没有 Core 的实体、上下文解析合同，并与既有深度/节点预算不一致。

这些是未完成实验，不是已验收基线。不能继续以“让特性检测不抛异常”为完成标准，也不能将第二套 HTML parser 或伪 document 作为兼容性修复提交。当前三份未提交源文件保留供后续审查，本次调查未部署新的产品候选。

## 其他独立错误及验证盲区

设备日志另有 bootstrap-multiselect 的第 912 行 parse error；调查时线上对应位置包含 `let clickableSelector`。它需要单独的引擎语法复现，不能与 jQuery 初始化错误混为一谈，也尚未证明是 navbar 的必要依赖。

TEST1320 验证有限 document delegated click；TEST1322 验证生成脚本、heap profile 和 GC。二者均未执行原版 jQuery/Bootstrap，不构成真实库兼容证据。

debug capture 的文件回读只能证明远端文件内容；PID 只能区分日志进程。它们不能证明进程实际加载的每个 DLL 路径。重复启动目录还可能留下旧进程，下一次正式验证必须检查实际模块匹配，不能假定“旧实例无需退出”。本次没有证据把混包认定为当前根因。

## 下一条完整纵切

先建立固定版本原始 jQuery 3.5.1 + Bootstrap 4.6.2 的离线公共 DLL 集成回归，分开记录每个库初始化、异常和 heap；再修复其真实需要的 bounded detached DOM 合同。HTML fragment 解析归 Core，Browser 负责 wrapper 身份、生命周期和桥接；不得在 EXE 中加站点特判。

先覆盖 live/default/dirty 表单状态、clone 和失败不变性，再覆盖 fragment、独立 document 所有权及释放。只有原始库初始化成功、实际 Bootstrap collapse 通过 delegated click 改变 class/aria、相邻回归与匹配 ARMV4I 设备门通过，才安排一次真实触摸验收。自动失败期间不再让用户重复点击。

调查使用的原始文件校验值：

- jQuery minified SHA-256：`F7F6A5894F1D19DDAD6FA392B2ECE2C5E578CBF7DA4EA805B6885EB6985B6E3D`。
- Bootstrap minified SHA-256：`423217ABF8775CEA2DC30FA1FE3E1C5E24DC359A80F1C37AD29A86094BFE81D1`。

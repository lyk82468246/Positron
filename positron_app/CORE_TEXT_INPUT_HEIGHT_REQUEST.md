# Core 单行文本输入框自动高度修复请求

本请求已由 Core 的自然高度修复落实，公共 ABI 不变；EXE 不强撑固定高度。
尺寸合同见 [Core README](../positron_core/README.md#单行文本控件尺寸)，当前应用验收见
[交接](../.agents/HANDOFF.md)。以下保留消费者复现和回归要求，不作为待开发任务。

## 故障与最小复现

`positron://settings` 的“Startup page / Home”下只有横线，已有网址不可见、无法正常编辑。用户截图为本地 `tmp/QQ20261007-202522.png`。旧门只验证 HWND 存在、enabled、内容正确，不是视觉通过。

实际双语模板在 `positron_app/resources/en-US/settings.html` 与 `resources/zh-CN/settings.html`。使用应用既有 body 14px sans-serif/line-height 1.35 CSS，无 input 高度声明：

```html
<p><label for="settings-startup">Startup page / Home</label></p>
<p><input id="settings-startup" type="text" size="18" maxlength="1023" disabled></p>
<p>Enter an HTTP(S) URL or positron://newtab.</p>
```

可信页读配置后 `.value` 填充并移除 disabled。覆盖初始空值、value 属性和后续 value mutation，不只修此页面。

## 根因与职责

- `positron_core/pcore_box.c` 的 `pcore_make_form_control_box` 创建 BOX_INLINE_BLOCK / IS_REPLACED 的 text/password gadget，保留 value；该分支没有生成撑开高度的文本子树，也未设置固有高度。
- 原 `netsurf-all-3.11/netsurf/content/handlers/html/layout.c` text/password auto height 缺少自然高度，旧 Core 空 input 在设备上为 140×0。修复在 inline-block/block 路径使用 computed line-height，只补 height:auto，保留作者 height/min/max、box-sizing 和零值。
- `PCore_TextInputInfo` 返回布局 box 的 border/padding/content 矩形。EXE `app_controls.c` 原样创建/定位 EDIT，非正高度兜底为 1 像素。EXE 强行撑高不会同步后续段落、命中和 extent，不能作为修复。
- EXE 验收必须测量原生客户区与实际字体，不能用 Core 数值或 HWND 存在替代文字可见性。OEM 字体、SIP、真实触摸与旋转继续人工验收。

## 修复与验收要求

1. 未指定高度的单行 text/password 应有与字体/DPI 一致的自然高度；空值不能塌缩，value 改变不影响单行自然高度。
2. 保持作者 height/min-height/max-height 语义，不强制全局固定像素高度；不改变其他控件、URL、设置、DB 业务。
3. 96/128/192 DPI、窄/宽/旋转，覆盖空值/非空/disabled→enabled/value mutation/重复 layout、中文和 descender；验证 TextInputInfo、真实 paint/hit、后续段落不重叠、extent 一致。
4. test_host 只提供 fixture、平台接线和断言；保护 controls、表单、焦点、value/reset 和显式 CSS 高度约束。
5. 正式串行 Debug/Release 和设备门通过后给出提交/匹配包。EXE 再部署完整包，运行真实设置页客户区/字体高度门，用户确认输入、SIP、保存。

## EXE 自动门

Debug 私有 `main.c/app_settings_live_native` 使用 GetClientRect 和当前 EDIT 字体 GetTextMetricsW，要求客户区至少容纳 tmHeight。实际设置页依次读取保存值、清空、重新填值，再保存和重启；日志门须验证当前进程的 `settings-input-height` 终态，Release 不编入。这只是断言，不改变产品高度。

部署需使用完整匹配包，重新检查空间与 guest 引用；不混用已加载 DLL、不删除用户数据库。失败和最终设备证据只维护在 HANDOFF；旧诊断失败不能追认为通过。

# 测试与验收

## 测试宿主

`test_host.exe` 是公共 DLL 的回归宿主和示例消费者。它覆盖基础 TLS/HTTP/JSON、图像、
HTML/CSS/layout、真实网页 Browse、表单输入、事件、`positron_browser.dll` history/session 与
script-session，以及实验性浏览器 JavaScript。

测试编号属于宿主实现细节。稳定文档不维护逐编号流水；当前候选选择和新增测试含义写在
[`.agents/HANDOFF.md`](../.agents/HANDOFF.md)，精确实现以
[`test_host/main.c`](../test_host/main.c) 为准。

## `test_host.ini`

配置文件必须与 `test_host.exe` 位于同一目录。核心配置项：

```ini
auto=1
javascript=0
tests=13,20,27,999
```

### `auto`

- `auto=1`：按编号运行所选测试，不弹出 Yes/No/OK；覆盖写入同目录 `test_host.log`。
- `auto=0`：保留交互式选择和确认流程。

自动可视测试会让窗口至少完成一次绘制后正常关闭。这能证明断言、资源计数和首帧绘制没有
失败，但不能证明字体、边距、抗锯齿或整体版式正确。

### `javascript`

- `javascript=0`：默认产品路径，不执行浏览器 classic script。
- `javascript=1`：显式启用实验性 inline/external classic script、页面 context 和受限
  DOM/Event/input/location/history bridge。

独立 `positron_script.dll` 的测试不要求打开浏览器 JavaScript。开启此配置也不代表完整
Web JavaScript 兼容性。

### `tests`

接受逗号或空格分隔的编号和范围，也接受特殊编号 `7b` 与 `999`：

```ini
tests=1-5 7b 13 20,999
```

历史浮动实验 TEST23、TEST78 和 TEST79 已撤回，不能通过配置重新启用。

TEST999 是专用完成提示音。只有显式选中、且前序测试没有令整个批次失败时，程序退出前才
请求一次系统提示音。它不验证其他产品能力。

配置缺失时宿主走交互流程；存在但无效的配置会提示并忽略，不会静默扩大测试范围。

### 当前默认自动选择与人工验收包（next349 基线）

工作区当前的 `test_host/test_host.ini` 保持自动模式，并使用窄的 smoke 选择：

```ini
auto=1
javascript=0
tests=13,20,27,56,58,62,64-67,73,75,999
```

这是窄的自动 smoke 选择，不是完整自动回归基线。最近一次完整自动基线仍是 next255：
`auto=1`、`javascript=0`、`tests=13,20,27,43,44,56,58-77,80-222,999`；next295
采用定向门，不要求每批重复全量。设备 gate 通过 `-TestSelection` 只修改隔离 staging，
不改 tracked ini：

```bat
scripts\device_gate.bat -Candidate next264-file-picker-stage ^
  -TestSelection "231,999"
scripts\device_gate.bat -Candidate next264-file-picker-regression ^
  -TestSelection "70,189-231,999"
scripts\device_gate.bat -Candidate next266 ^
  -TestSelection "233,999"
scripts\device_gate.bat -Candidate next267 ^
  -TestSelection "233,234,999"
scripts\device_gate.bat -Candidate next268 ^
  -TestSelection "233-235,999"
scripts\device_gate.bat -Candidate next269 ^
  -TestSelection "233-236,999"
scripts\device_gate.bat -Candidate next270 ^
  -TestSelection "233-237,999"
scripts\device_gate.bat -Candidate next271 ^
  -TestSelection "233-238,999"
scripts\device_gate.bat -Candidate next272 ^
  -TestSelection "233-239,999"
scripts\device_gate.bat -Candidate next273 ^
  -TestSelection "233-240,999"
scripts\device_gate.bat -Candidate next274 ^
  -TestSelection "233-241,999"
scripts\device_gate.bat -Candidate next275 ^
  -TestSelection "233-242,999"
scripts\device_gate.bat -Candidate next276 ^
  -TestSelection "233-243,999"
scripts\device_gate.bat -Candidate next277 ^
  -TestSelection "233-244,999"
scripts\device_gate.bat -Candidate next278 ^
  -TestSelection "233-245,999"
scripts\device_gate.bat -Candidate next279 ^
  -TestSelection "233-246,999"
scripts\device_gate.bat -Candidate next280 ^
  -TestSelection "233-247,999"
scripts\device_gate.bat -Candidate next281 ^
  -TestSelection "233-248,999"
scripts\device_gate.bat -Candidate next282 ^
  -TestSelection "233-249,999"
scripts\device_gate.bat -Candidate next283 ^
  -TestSelection "233-250,999"
scripts\device_gate.bat -Candidate next284 ^
  -TestSelection "233-251,999"
scripts\device_gate.bat -Candidate next285 ^
  -TestSelection "233-252,999"
scripts\device_gate.bat -Candidate next286 ^
  -TestSelection "233-253,999"
scripts\device_gate.bat -Candidate next287 ^
  -TestSelection "233-254,999"
scripts\device_gate.bat -Candidate next288 ^
  -TestSelection "233-255,999"
scripts\device_gate.bat -Candidate next289 ^
  -TestSelection "233-256,999"
scripts\device_gate.bat -Candidate next290 ^
  -TestSelection "233-257,999"
scripts\device_gate.bat -Candidate next291 ^
  -TestSelection "233-258,999"
scripts\device_gate.bat -Candidate next292 ^
  -TestSelection "233-259,999"
scripts\device_gate.bat -Candidate next293 ^
  -TestSelection "233-260,999"
scripts\device_gate.bat -Candidate next294 ^
  -TestSelection "233-261,999"
scripts\device_gate.bat -Candidate next295-file-programmatic-picker ^
  -TestSelection "262,999"
scripts\device_gate.bat -Candidate next295-file-programmatic-picker-regression ^
  -TestSelection "70,189-231,233-262,999"
scripts\device_gate.bat -Candidate next296-disabled-property ^
  -TestSelection "264,999"
scripts\device_gate.bat -Candidate next296-disabled-property-regression ^
  -TestSelection "70,189-231,233-262,264,999"
scripts\device_gate.bat -Candidate next297-form-properties ^
  -TestSelection "265,999"
scripts\device_gate.bat -Candidate next297-form-properties-regression ^
  -TestSelection "68-73,189-231,233-262,264-265,999"
scripts\device_gate.bat -Candidate next298-validation-query ^
  -TestSelection "266,999"
scripts\device_gate.bat -Candidate next298-validation-query-regression ^
  -TestSelection "189-231,233-262,264-266,999"
scripts\device_gate.bat -Candidate next299-custom-validity ^
  -TestSelection "267,999"
scripts\device_gate.bat -Candidate next299-custom-validity-regression ^
  -TestSelection "68-73,189-231,233-262,264-267,999"
scripts\device_gate.bat -Candidate next299-script-limit ^
  -TestSelection "93,999"
scripts\device_gate.bat -Candidate next300-form-validation ^
  -TestSelection "268,999"
scripts\device_gate.bat -Candidate next300-form-validation-regression ^
  -TestSelection "68-73,189-231,233-262,264-268,999"
scripts\device_gate.bat -Candidate next301-report-validity ^
  -TestSelection "269,999"
scripts\device_gate.bat -Candidate next301-report-validity-regression ^
  -TestSelection "68-73,189-231,233-262,264-269,999"
scripts\device_gate.bat -Candidate next302-validation-message ^
  -TestSelection "270,999"
scripts\device_gate.bat -Candidate next302-validation-message-regression ^
  -TestSelection "68-73,189-231,233-262,264-270,999"
scripts\device_gate.bat -Candidate next303-constraint-reflection-js ^
  -TestSelection "271,999"
scripts\device_gate.bat -Candidate next303-constraint-reflection-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-271,999"
scripts\device_gate.bat -Candidate next304-name-reflection-js ^
  -TestSelection "272,999"
scripts\device_gate.bat -Candidate next304-name-reflection-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-272,999"
scripts\device_gate.bat -Candidate next305-submission-reflection-js ^
  -TestSelection "273,999"
scripts\device_gate.bat -Candidate next305-submission-reflection-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-273,999"
scripts\device_gate.bat -Candidate next306-enctype-reflection-js ^
  -TestSelection "274,999"
scripts\device_gate.bat -Candidate next306-enctype-reflection-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-274,999"
scripts\device_gate.bat -Candidate next307-submit-action-js ^
  -TestSelection "275,999"
scripts\device_gate.bat -Candidate next307-submit-action-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-275,999"
scripts\device_gate.bat -Candidate next308-submit-method-js ^
  -TestSelection "276,999"
scripts\device_gate.bat -Candidate next308-submit-method-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-276,999"
scripts\device_gate.bat -Candidate next309-submit-enctype-js ^
  -TestSelection "277,999"
scripts\device_gate.bat -Candidate next309-submit-enctype-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-277,999"
scripts\device_gate.bat -Candidate next310-implicit-submitter-js ^
  -TestSelection "278,999"
scripts\device_gate.bat -Candidate next310-implicit-submitter-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-278,999"
scripts\device_gate.bat -Candidate next311-target-reflection-js ^
  -TestSelection "279,999"
scripts\device_gate.bat -Candidate next311-target-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-279,999"
scripts\device_gate.bat -Candidate next312-form-autocomplete-js ^
  -TestSelection "280,999"
scripts\device_gate.bat -Candidate next312-form-autocomplete-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-280,999"
scripts\device_gate.bat -Candidate next313-accept-charset-js ^
  -TestSelection "281,999"
scripts\device_gate.bat -Candidate next313-accept-charset-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-281,999"
scripts\device_gate.bat -Candidate next314-placeholder-js ^
  -TestSelection "282,999"
scripts\device_gate.bat -Candidate next314-placeholder-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-282,999"
scripts\device_gate.bat -Candidate next315-input-autocomplete-js ^
  -TestSelection "283,999"
scripts\device_gate.bat -Candidate next315-input-autocomplete-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-283,999"
scripts\device_gate.bat -Candidate next316-inputmode-js ^
  -TestSelection "284,999"
scripts\device_gate.bat -Candidate next316-inputmode-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-284,999"
scripts\device_gate.bat -Candidate next317-type-js ^
  -TestSelection "285,999"
scripts\device_gate.bat -Candidate next317-type-regression-js ^
  -TestSelection "68-73,189-231,233-262,264-285,999"
scripts\device_gate.bat -Candidate next318-textarea-placeholder-js ^
  -TestSelection "286,999"
scripts\device_gate.bat -Candidate next318-textarea-placeholder-recent-js ^
  -TestSelection "264-286,999"
scripts\device_gate.bat -Candidate next319-select-autocomplete-js ^
  -TestSelection "287,999"
scripts\device_gate.bat -Candidate next319-select-autocomplete-recent-js ^
  -TestSelection "264-287,999"
scripts\device_gate.bat -Candidate next320-button-type-js ^
  -TestSelection "288,999"
scripts\device_gate.bat -Candidate next320-button-type-recent-js ^
  -TestSelection "264-288,999"
scripts\device_gate.bat -Candidate next321-unknown-method-js ^
  -TestSelection "289,999"
scripts\device_gate.bat -Candidate next321-unknown-method-recent-js ^
  -TestSelection "264-289,999"
scripts\device_gate.bat -Candidate next322-unknown-enctype-js ^
  -TestSelection "290,999"
scripts\device_gate.bat -Candidate next322-unknown-enctype-recent-js ^
  -TestSelection "264-290,999"
scripts\device_gate.bat -Candidate next323-case-boundary-js ^
  -TestSelection "291,999"
scripts\device_gate.bat -Candidate next323-case-boundary-recent-js ^
  -TestSelection "264-291,999"
scripts\device_gate.bat -Candidate next324-metadata-relayout-js ^
  -TestSelection "292,999"
scripts\device_gate.bat -Candidate next324-metadata-relayout-recent-js ^
  -TestSelection "264-292,999"
scripts\device_gate.bat -Candidate next325-reset-metadata-js ^
  -TestSelection "293,999"
scripts\device_gate.bat -Candidate next325-reset-metadata-recent-js ^
  -TestSelection "264-293,999"
scripts\device_gate.bat -Candidate next326-js-baseline ^
  -TestSelection "68-73,189-231,233-262,264-293,999"
scripts\device_gate.bat -Candidate next327-title-reflection-js ^
  -TestSelection "294,999"
scripts\device_gate.bat -Candidate next327-title-reflection-recent-js ^
  -TestSelection "264-294,999"
scripts\device_gate.bat -Candidate next328-lang-reflection-js ^
  -TestSelection "295,999"
scripts\device_gate.bat -Candidate next328-lang-reflection-recent-js ^
  -TestSelection "264-295,999"
scripts\device_gate.bat -Candidate next329-dir-reflection-js ^
  -TestSelection "296,999"
scripts\device_gate.bat -Candidate next329-dir-reflection-recent-js ^
  -TestSelection "264-296,999"
scripts\device_gate.bat -Candidate next330-hidden-reflection-js ^
  -TestSelection "297,999"
scripts\device_gate.bat -Candidate next330-hidden-reflection-recent-js ^
  -TestSelection "264-297,999"
scripts\device_gate.bat -Candidate next331-accesskey-reflection-js ^
  -TestSelection "298,999"
scripts\device_gate.bat -Candidate next331-accesskey-reflection-recent-js ^
  -TestSelection "264-298,999"
scripts\device_gate.bat -Candidate next332-role-reflection-js ^
  -TestSelection "299,999"
scripts\device_gate.bat -Candidate next332-role-reflection-recent-js ^
  -TestSelection "264-299,999"
scripts\device_gate.bat -Candidate next333-aria-label-reflection-js ^
  -TestSelection "300,999"
scripts\device_gate.bat -Candidate next333-aria-label-focus-retry-js ^
  -TestSelection "299-300,999"
scripts\device_gate.bat -Candidate next333-aria-label-reflection-recent-retry-js ^
  -TestSelection "264-300,999"
scripts\device_gate.bat -Candidate next334-contenteditable-reflection-js ^
  -TestSelection "301,999"
scripts\device_gate.bat -Candidate next334-contenteditable-reflection-recent-js ^
  -TestSelection "264-301,999"
scripts\device_gate.bat -Candidate next335-draggable-reflection-js ^
  -TestSelection "302,999"
scripts\device_gate.bat -Candidate next335-draggable-reflection-recent-js ^
  -TestSelection "264-302,999"
scripts\device_gate.bat -Candidate next336-tabindex-reflection-js ^
  -TestSelection "303,999"
scripts\device_gate.bat -Candidate next336-tabindex-reflection-recent-js ^
  -TestSelection "264-303,999"
scripts\device_gate.bat -Candidate next337-input-accept-reflection-js ^
  -TestSelection "304,999"
scripts\device_gate.bat -Candidate next337-input-accept-reflection-recent-js ^
  -TestSelection "264-304,999"
scripts\device_gate.bat -Candidate next338-input-capture-reflection-js ^
  -TestSelection "305,999"
scripts\device_gate.bat -Candidate next338-input-capture-reflection-recent-js ^
  -TestSelection "264-305,999"
scripts\device_gate.bat -Candidate next339-input-dirname-reflection-js ^
  -TestSelection "306,999"
scripts\device_gate.bat -Candidate next339-input-dirname-reflection-recent-js ^
  -TestSelection "264-306,999"
scripts\device_gate.bat -Candidate next340-input-list-reflection-js ^
  -TestSelection "307,999"
scripts\device_gate.bat -Candidate next340-input-list-reflection-recent-js ^
  -TestSelection "264-307,999"
scripts\device_gate.bat -Candidate next341-textarea-wrap-reflection-js ^
  -TestSelection "308,999"
scripts\device_gate.bat -Candidate next341-textarea-wrap-reflection-recent-js ^
  -TestSelection "264-308,999"
scripts\device_gate.bat -Candidate next342-html-for-reflection-js ^
  -TestSelection "309,999"
scripts\device_gate.bat -Candidate next342-html-for-reflection-recent-js ^
  -TestSelection "264-309,999"
scripts\device_gate.bat -Candidate next343-slot-reflection-js ^
  -TestSelection "310,999"
scripts\device_gate.bat -Candidate next343-slot-reflection-recent-js ^
  -TestSelection "264-310,999"
scripts\device_gate.bat -Candidate next344-itemid-reflection-js ^
  -TestSelection "311,999"
scripts\device_gate.bat -Candidate next344-itemid-reflection-recent-js ^
  -TestSelection "264-311,999"
scripts\device_gate.bat -Candidate next345-itemprop-reflection-js ^
  -TestSelection "312,999"
scripts\device_gate.bat -Candidate next345-itemprop-reflection-recent-js ^
  -TestSelection "264-312,999"
scripts\device_gate.bat -Candidate next346-itemref-reflection-js ^
  -TestSelection "313,999"
scripts\device_gate.bat -Candidate next346-itemref-reflection-recent-js ^
  -TestSelection "264-313,999"
scripts\device_gate.bat -Candidate next347-itemscope-reflection-js ^
  -TestSelection "314,999"
scripts\device_gate.bat -Candidate next347-itemscope-reflection-recent-js ^
  -TestSelection "264-314,999"
scripts\device_gate.bat -Candidate next348-itemtype-reflection-js ^
  -TestSelection "315,999"
scripts\device_gate.bat -Candidate next348-itemtype-reflection-recent-js ^
  -TestSelection "264-315,999"
scripts\device_gate.bat -Candidate next349-nonce-reflection-js ^
  -TestSelection "316,999"
scripts\device_gate.bat -Candidate next349-nonce-reflection-recent-js ^
  -TestSelection "264-316,999"
```

next298 的两组定向门分别覆盖新测试和启用 JavaScript 的 form/script/constraint 回归，已分别通过
2/2 和 77/77，均无 ERROR/FAIL；回归门的 staging INI 临时使用 `javascript=1`，仓库 tracked
配置仍保持 `javascript=0`。next295 的 TEST263 真实 GUI 已由用户验收，证据目录和当前状态见
[`.agents/HANDOFF.md`](../.agents/HANDOFF.md)。
next299 的两组定向门覆盖 custom-validity 新测试和启用 JavaScript 的表单/脚本回归，已分别通过
2/2 和 84/84，均无 ERROR/FAIL；回归门的 staging INI 临时使用 `javascript=1`，仓库 tracked
配置仍保持 `javascript=0`。该批不涉及视觉、触摸、SIP 或系统 picker，不需要人工页面验收。
独立 `positron_script` native callback 上限扩展另由 TEST93/999 2/2 通过验证。
next300 的两组定向门覆盖 form-level validation 新测试和启用 JavaScript 的表单/脚本回归，已分别通过
2/2 和 85/85，均无 ERROR/FAIL；证据位于 `tmp/device-runs/20260819-223518-next300-form-validation/`
和 `tmp/device-runs/20260819-223614-next300-form-validation-regression/`。回归门的 staging INI
临时使用 `javascript=1`，仓库 tracked 配置已恢复 `javascript=0`。该批只提供查询型
form `checkValidity()`，不涉及视觉、触摸、SIP、系统 picker 或 `reportValidity()`，不需要人工页面验收。
next301 的定向门覆盖 form/control `reportValidity()`、invalid-event 顺序、cancelable/trusted
事件字段、动态恢复、`novalidate` 不绕过以及 disabled/readonly 跳过；`TEST269/999` 已在
`tmp/device-runs/20260819-231341-next301-report-validity-final/` 以 2/2 通过。该批不涉及视觉、触摸、
SIP、系统 picker 或 native validation UI，不需要人工页面验收；启用 JavaScript 的回归门仍应在
提交前按上面的 regression 选择执行；本次 `68-73,189-231,233-262,264-269,999` 回归已以
86/86 通过，证据位于 `tmp/device-runs/20260819-231431-next301-report-validity-regression/`。
next302 的定向门覆盖内置 `validationMessage` fallback、custom message 优先级、required/
range/type mismatch、动态清除和 UTF-8 安全截断；`TEST270/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260819-233450-next302-validation-message-final/`。启用 JavaScript 的相关回归
已以 87/87 通过，证据位于 `tmp/device-runs/20260819-232921-next302-validation-message-regression/`；
消息是固定英文，不涉及视觉、触摸、SIP、系统 picker、本地化或 native validation UI，不需要人工
页面验收。
next303 的定向门覆盖 `pattern`、`minLength`、`maxLength` 的 getter/setter 反射、动态
`tooShort`/`tooLong`/`patternMismatch` flags 和非法负数/非有限 setter；`TEST271/999` 已以 2/2
通过，证据位于 `tmp/device-runs/20260820-103112-next303-constraint-reflection-final/`。
启用 JavaScript 的相关回归已以 88/88 通过，证据位于
`tmp/device-runs/20260820-103233-next303-constraint-reflection-regression-final/`；不涉及视觉、
触摸、SIP、系统 picker 或 native validation UI，不需要人工页面验收。
next304 的定向门覆盖 input、textarea、select、button 和 form 的 `name` 属性 getter/setter、
attribute round-trip、动态改名后的 successful-control submission；`TEST272/999` 已以 2/2
通过，证据位于 `tmp/device-runs/20260820-105247-next304-name-reflection-js/`。启用 JavaScript 的
相关回归原配置重试后以 89/89 通过，证据位于
`tmp/device-runs/20260820-105715-next304-name-reflection-regression-retry-js/`；不涉及视觉、
触摸、SIP、系统 picker 或 native validation UI，不需要人工页面验收。
next305 的定向门覆盖 form `action`/`method` 的 getter/setter、attribute round-trip、动态
GET/urlencoded-POST submission 目标和 method 判定；`TEST273/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-113226-next305-submission-reflection-js/`。启用 JavaScript 的相关
回归已以 90/90 通过，证据位于
`tmp/device-runs/20260820-113331-next305-submission-reflection-regression-js/`；不涉及视觉、
触摸、SIP、系统 picker、完整 URL parser 或 multipart，不需要人工页面验收。
next306 的定向门覆盖 form `enctype` 的 getter/setter、attribute round-trip、动态
urlencoded/multipart submission 切换和恢复；`TEST274/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-115956-next306-enctype-reflection-js/`。启用 JavaScript 的相关
回归已以 91/91 通过，证据位于
`tmp/device-runs/20260820-120021-next306-enctype-reflection-regression-js/`；不涉及视觉、
触摸、SIP、系统 picker、完整 enctype 规范化或 multipart 传输，不需要人工页面验收。
next307 的定向门覆盖 submitter `formAction` 的 getter/setter、attribute round-trip、动态
urlencoded/multipart action 覆盖和移除恢复；`TEST275/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-121644-next307-submit-action-final/`。启用 JavaScript 的相关回归
已以 92/92 通过，证据位于
`tmp/device-runs/20260820-121703-next307-submit-action-regression-js/`；不涉及视觉、触摸、
SIP、系统 picker、完整 URL parser 或其他 submitter override 属性，不需要人工页面验收。
next308 的定向门覆盖 submitter `formMethod` 的 getter/setter、attribute round-trip、动态
GET/POST method 覆盖和移除恢复；`TEST276/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-122437-next308-submit-method-js/`。启用 JavaScript 的相关回归
已以 93/93 通过，证据位于 `tmp/device-runs/20260820-122501-next308-submit-method-regression-js/`；
不涉及视觉、触摸、SIP、系统 picker、完整 method 规范化或 multipart submitter override，
不需要人工页面验收。
next309 的定向门覆盖 submitter `formEnctype` 的 getter/setter、attribute round-trip、动态
urlencoded/multipart 覆盖、multipart snapshot eligibility 和移除恢复；`TEST277/999` 已以 2/2
通过，证据位于 `tmp/device-runs/20260820-123830-next309-submit-enctype-js-retry/`。启用
JavaScript 的相关回归已以 94/94 通过，证据位于
`tmp/device-runs/20260820-123929-next309-submit-enctype-regression-js/`；不涉及视觉、触摸、
SIP、系统 picker、未知 enctype 规范化或 multipart 传输边界，不需要人工页面验收。
next310 的定向门覆盖 text-input 隐式 Enter 与显式首个 submitter 的 action/method/enctype
override、multipart snapshot action/part count 一致性；`TEST278/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-124937-next310-implicit-submitter-js/`。启用 JavaScript 的相关
回归最终以 95/95 通过，证据位于
`tmp/device-runs/20260820-125241-next310-implicit-submitter-regression-js-retry2/`；前两次
长链在既有 TEST189/262 bootstrap 处超时，未作为基线；本批不涉及真实键盘、SIP 或视觉页面，
不需要人工页面验收。
next311 的定向门覆盖 form `target` 的 getter/setter、attribute round-trip、移除恢复，并确认
submission action/method 不变；`TEST279/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-125830-next311-target-reflection-js/`。启用 JavaScript 的相关回归
已以 96/96 通过，证据位于 `tmp/device-runs/20260820-125854-next311-target-regression-js/`；
不涉及新窗口、target browsing context、导航副作用、视觉或人工页面验收。
next312 的定向门覆盖 form `autocomplete` 的 getter/setter、attribute round-trip、移除恢复，
并确认 submission 不变；`TEST280/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-130448-next312-form-autocomplete-js/`。启用 JavaScript 的相关回归
已以 97/97 通过，证据位于 `tmp/device-runs/20260820-130514-next312-form-autocomplete-regression-js/`；
不涉及自动填充、凭据存储、控件级 autocomplete、视觉或人工页面验收。
next313 的定向门覆盖 form `acceptCharset` ↔ `accept-charset` 的 getter/setter、attribute
round-trip、移除恢复，并确认 submission 不变；`TEST281/999` 重试后以 2/2 通过，证据位于
`tmp/device-runs/20260820-131112-next313-accept-charset-js-retry/`。启用 JavaScript 的相关回归
已以 98/98 通过，证据位于 `tmp/device-runs/20260820-131135-next313-accept-charset-regression-js/`；
首尝定向门的单次 bootstrap timeout 未作为基线；不涉及字符集转换、编码协商、视觉或人工页面验收。
next314 的定向门覆盖 input `placeholder` 的 getter/setter、attribute round-trip、移除恢复，并
确认 current value 和 submission 不变；`TEST282/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-131719-next314-placeholder-js/`。启用 JavaScript 的相关回归已以
99/99 通过，证据位于 `tmp/device-runs/20260820-131742-next314-placeholder-regression-js/`；
不涉及 placeholder 绘制、SIP、原生提示 UI、视觉或人工页面验收。
next315 的定向门覆盖 input `autocomplete` 的 getter/setter、attribute round-trip、移除恢复，并
确认 current value 和 submission 不变；`TEST283/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-133705-next315-input-autocomplete-js/`。启用 JavaScript 的相关回归
已以 100/100 通过，证据位于 `tmp/device-runs/20260820-133726-next315-input-autocomplete-regression-js/`；
不涉及自动填充、凭据存储、原生提示 UI、视觉或人工页面验收。
next316 的定向门覆盖 input `inputMode` ↔ `inputmode` 的 getter/setter、attribute round-trip、移除
恢复，并确认 current value 和 submission 不变；`TEST284/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-134204-next316-inputmode-js/`。启用 JavaScript 的相关回归重试后以
101/101 通过，证据位于 `tmp/device-runs/20260820-134336-next316-inputmode-regression-js-retry/`；
首尝既有 TEST277 bootstrap timeout 未作为基线；不涉及 SIP、键盘布局、输入法策略、视觉或人工页面验收。
next317 的定向门覆盖 input `type` raw getter/setter、attribute round-trip、移除恢复，并确认
既有 text-control current value 与 GET submission 不变；`TEST285/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-134938-next317-type-js/`。启用 JavaScript 的相关回归已以 102/102
通过，证据位于 `tmp/device-runs/20260820-135001-next317-type-regression-js/`；不涉及动态控件
重建、完整 Web IDL type 规范、native type UI、视觉或人工页面验收。
next318 的定向门覆盖 textarea `placeholder` 的 getter/setter、attribute round-trip、移除恢复，并
确认 current value 与 GET submission 不变；`TEST286/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-135849-next318-textarea-placeholder-js/`。完整启用 JavaScript 链的多次
尝试均只在既有 TEST192/227/272/277/279 的 bootstrap timeout 处提前结束，未作为基线；最近
`TEST264-286/999` 相关段已以 24/24 通过，证据位于
`tmp/device-runs/20260820-140648-next318-textarea-placeholder-recent-js-retry2/`。该批不涉及
placeholder 绘制、SIP、原生提示 UI、视觉或人工页面验收。
next319 的定向门覆盖 select `autocomplete` 的 getter/setter、attribute round-trip、移除恢复，并
确认选中值与 GET submission 不变；`TEST287/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-141139-next319-select-autocomplete-js/`。最近
`TEST264-287/999` 相关段已以 25/25 通过，证据位于
`tmp/device-runs/20260820-141208-next319-select-autocomplete-recent-js/`；不涉及自动填充、
凭据存储、视觉或人工页面验收。
next320 的定向门覆盖 button `type` 的 getter/setter、attribute round-trip、移除恢复，并在脚本
恢复 `type=submit` 后确认 submitter successful-control submission；`TEST288/999` 已以 2/2 通过，
证据位于 `tmp/device-runs/20260820-142003-next320-button-type-js/`。最近
`TEST264-288/999` 相关段已以 26/26 通过，证据位于
`tmp/device-runs/20260820-142025-next320-button-type-recent-js/`；不涉及动态控件重建、native
button UI、视觉或人工页面验收。
next321 的定向门覆盖未知 form `method` 的 getter/setter、attribute round-trip、移除恢复，并确认
submission 安全回落为 GET；`TEST289/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-142613-next321-unknown-method-js/`。最近
`TEST264-289/999` 相关段已以 27/27 通过，证据位于
`tmp/device-runs/20260820-142636-next321-unknown-method-recent-js/`；不涉及其他 HTTP 方法、
规范化、导航副作用、视觉或人工页面验收。
next322 的定向门覆盖未知 form `enctype` 的 getter/setter、attribute round-trip、移除恢复，并确认
POST submission 安全回落为 urlencoded；`TEST290/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-143307-next322-unknown-enctype-js/`。最近
`TEST264-290/999` 相关段已以 28/28 通过，证据位于
`tmp/device-runs/20260820-143433-next322-unknown-enctype-recent-js/`；不涉及 enctype 规范化、
multipart 传输、其他编码格式、视觉或人工页面验收。
next323 的定向门覆盖 method/enctype mixed-case raw getter/setter、attribute round-trip、移除恢复，
并确认大小写不敏感匹配下的 urlencoded POST action/body；`TEST291/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-143927-next323-case-boundary-js/`。最近
`TEST264-291/999` 相关段已以 29/29 通过，证据位于
`tmp/device-runs/20260820-144007-next323-case-boundary-recent-js/`；不涉及规范化 getter、完整
Web IDL 枚举语义、导航副作用、视觉或人工页面验收。
next324 的定向门覆盖动态 action/method/value 更新后两次 viewport 重排的 submission metadata，
包括 method、action/body 及 `action_bytes/body_bytes`；`TEST292/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-144727-next324-metadata-relayout-js/`。最近
`TEST264-292/999` 相关段已以 30/30 通过，证据位于
`tmp/device-runs/20260820-144750-next324-metadata-relayout-recent-js/`；不涉及导航提交、异步
任务、视觉或人工页面验收。
next325 的定向门覆盖脚本更新后的 form reset、动态 action/method 保留、默认控件值恢复以及
submission metadata 重建；`TEST293/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-145244-next325-reset-metadata-js/`。最近回归首尝在既有 TEST266
bootstrap timeout 处仅完成 2/31，未作为基线；重试后 `TEST264-293/999` 以 31/31 通过，证据位于
`tmp/device-runs/20260820-145332-next325-reset-metadata-recent-js-retry/`；不涉及额外导航、视觉
或人工页面验收。
next326 是本次累计检查点：启用 JavaScript 的
`TEST68-73,189-231,233-262,264-293/999` 共 110 项已全部通过，证据位于
`tmp/device-runs/20260820-145654-next326-js-baseline/`；零 ERROR/FAIL、唯一 TESTBENCH PASS、
`test13_route_ok=True`。该批不新增产品语义、不需要人工页面验收；后续批次可继续以该选择作为
相关回归基线，只有出现风险或达到检查点时再扩展范围。
next327 的定向门覆盖 `HTMLElement.title` raw UTF-8 getter/setter、attribute round-trip 和移除
恢复；`TEST294/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-152157-next327-title-reflection/`。最近 `TEST264-294/999` 已以
32/32 通过，证据位于 `tmp/device-runs/20260820-152312-next327-title-reflection-recent/`；
该批不涉及 tooltip 绘制、原生提示 UI、视觉或人工页面验收。
next328 的定向门覆盖 `HTMLElement.lang` raw UTF-8 getter/setter、attribute round-trip 和移除
恢复；`TEST295/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-153036-next328-lang-reflection/`。最近 `TEST264-295/999` 已以
33/33 通过，证据位于 `tmp/device-runs/20260820-153155-next328-lang-reflection-recent/`；
该批不涉及语言解析、本地化、视觉或人工页面验收。
next329 的定向门覆盖 `HTMLElement.dir` raw UTF-8 getter/setter、attribute round-trip 和移除
恢复；`TEST296/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-154035-next329-dir-reflection/`。最近 `TEST264-296/999` 已以
34/34 通过，证据位于 `tmp/device-runs/20260820-154115-next329-dir-reflection-recent/`；
该批不涉及 CSS 方向布局、视觉或人工页面验收。
next330 的定向门覆盖 `HTMLElement.hidden` 布尔 getter/setter、attribute round-trip 和移除
恢复；`TEST297/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-155105-next330-hidden-reflection/`。最近 `TEST264-297/999` 已以
35/35 通过，证据位于 `tmp/device-runs/20260820-155138-next330-hidden-reflection-recent/`；
该批不涉及隐藏布局算法、视觉或人工页面验收。
next331 的定向门覆盖 `HTMLElement.accessKey` raw UTF-8 getter/setter、attribute round-trip
和移除恢复；`TEST298/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-155713-next331-accesskey-reflection/`。最近 `TEST264-298/999` 已以
36/36 通过，证据位于 `tmp/device-runs/20260820-155741-next331-accesskey-reflection-recent/`；
该批不涉及 WM 快捷键、焦点副作用、视觉或人工页面验收。
next332 的定向门覆盖 `HTMLElement.role` raw UTF-8 getter/setter、attribute round-trip 和移除
恢复；`TEST299/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-160251-next332-role-reflection/`。最近 `TEST264-299/999` 已以
37/37 通过，证据位于 `tmp/device-runs/20260820-160315-next332-role-reflection-recent/`；
该批不涉及辅助技术树、语义计算、视觉或人工页面验收。
next333 的定向门覆盖 `HTMLElement.ariaLabel` ↔ `aria-label` raw UTF-8 getter/setter、attribute
round-trip 和移除恢复；`TEST300/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-160826-next333-aria-label-reflection/`。独立 `TEST299-300/999`
以 3/3 通过，证据位于 `tmp/device-runs/20260820-161029-next333-aria-label-focus-retry/`；
近期链首次在 TEST299 的环境 timeout 处 35/38，重试后的 `TEST264-300/999` 以 38/38 通过，
证据位于 `tmp/device-runs/20260820-161116-next333-aria-label-reflection-recent-retry/`；
该批不涉及 ARIA 语义树、视觉或人工页面验收。
next334 的定向门覆盖 `HTMLElement.contentEditable` raw UTF-8 getter/setter、attribute
round-trip 和移除恢复；`TEST301/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-161630-next334-contenteditable-reflection/`。最近 `TEST264-301/999`
已以 39/39 通过，证据位于 `tmp/device-runs/20260820-161711-next334-contenteditable-reflection-recent/`；
该批不涉及 layout、编辑控件、native IME、视觉或人工页面验收。
next335 的定向门覆盖 `HTMLElement.draggable` raw UTF-8 getter/setter、attribute round-trip 和
移除恢复；`TEST302/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-162725-next335-draggable-reflection/`。最近 `TEST264-302/999` 已以
40/40 通过，证据位于 `tmp/device-runs/20260820-162804-next335-draggable-reflection-recent/`；
该批不涉及拖放手势、视觉或人工页面验收。
next336 的定向门覆盖 `HTMLElement.tabIndex` 有限整数 getter/setter、attribute round-trip、
非法/缺失 raw attribute 回落和 setter 边界；`TEST303/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-163640-next336-tabindex-reflection/`。近期链前两次分别在既有
TEST298、TEST266 的 DOM bootstrap timeout 处停止，均未命中 TEST303；最终
`TEST264-303/999` 以 41/41 通过，证据位于
`tmp/device-runs/20260820-163847-next336-tabindex-reflection-recent-retry2/`，零 ERROR/FAIL。
该批不涉及焦点导航、视觉、触摸或人工页面验收。
next337 的定向门覆盖 `HTMLInputElement.accept` raw UTF-8 getter/setter、attribute round-trip 和
移除恢复；`TEST304/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-164631-next337-input-accept-reflection/`。最近 `TEST264-304/999`
已以 42/42 通过，证据位于
`tmp/device-runs/20260820-164656-next337-input-accept-reflection-recent/`；该批不涉及文件
类型过滤、系统 picker、视觉或人工页面验收。
next338 的定向门覆盖 `HTMLInputElement.capture` raw UTF-8 getter/setter、attribute round-trip 和
移除恢复；`TEST305/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-165204-next338-input-capture-reflection/`。最近 `TEST264-305/999`
已以 43/43 通过，证据位于
`tmp/device-runs/20260820-165256-next338-input-capture-reflection-recent/`；该批不涉及摄像头、
麦克风、系统 picker、视觉或人工页面验收。
next339 的定向门覆盖 `HTMLInputElement.dirname` raw UTF-8 getter/setter、attribute round-trip 和
移除恢复；`TEST306/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-165825-next339-input-dirname-reflection/`。最近 `TEST264-306/999`
已以 44/44 通过，证据位于
`tmp/device-runs/20260820-165845-next339-input-dirname-reflection-recent/`；该批不涉及提交
方向、编码、视觉或人工页面验收。
next340 的定向门覆盖 `HTMLInputElement.list` raw UTF-8 getter/setter、attribute round-trip 和
移除恢复；`TEST307/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-170256-next340-input-list-reflection/`。近期链首次在既有 TEST296
的 DOM bootstrap timeout 处停止，未作为基线；重试后的 `TEST264-307/999` 已以 45/45 通过，
证据位于 `tmp/device-runs/20260820-170415-next340-input-list-reflection-recent-retry/`，零
ERROR/FAIL。该批不涉及 datalist、自动完成、视觉或人工页面验收。
next341 的定向门覆盖 `HTMLTextAreaElement.wrap` raw UTF-8 getter/setter、attribute round-trip 和
移除恢复；`TEST308/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-170809-next341-textarea-wrap-reflection/`。最近 `TEST264-308/999`
已以 46/46 通过，证据位于
`tmp/device-runs/20260820-170830-next341-textarea-wrap-reflection-recent/`；该批不涉及软/硬
换行布局、提交编码、视觉或人工页面验收。
next342 的最终定向门覆盖 `HTMLElement.htmlFor` ↔ `for` raw UTF-8 getter/setter、attribute
round-trip 和移除恢复；一次 `className` 重定义候选在 bootstrap 阶段因已有不可配置 descriptor
报 `TypeError: not configurable`，证据位于
`tmp/device-runs/20260820-193424-next342-class-name-reflection/`，已撤回且不作为基线。改用
htmlFor 后，`TEST309/999` 以 2/2 通过，证据位于
`tmp/device-runs/20260820-193609-next342-html-for-reflection-retry/`；最近
`TEST264-309/999` 以 47/47 通过，证据位于
`tmp/device-runs/20260820-193636-next342-html-for-reflection-recent/`。该批不涉及 label 关联、
焦点转移、视觉或人工页面验收。
next343 的定向门覆盖 `HTMLElement.slot` ↔ `slot` raw UTF-8 getter/setter、attribute round-trip
和移除恢复；`TEST310/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-194058-next343-slot-reflection/`。最近 `TEST264-310/999` 已以
48/48 通过，证据位于 `tmp/device-runs/20260820-194117-next343-slot-reflection-recent/`；该批
不涉及 Shadow DOM、slot 分配、视觉或人工页面验收。
next344 的定向门覆盖 `HTMLElement.itemId` ↔ `itemid` raw UTF-8 getter/setter、attribute
round-trip 和移除恢复；`TEST311/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-194457-next344-itemid-reflection/`。最近 `TEST264-311/999` 已以
49/49 通过，证据位于 `tmp/device-runs/20260820-194515-next344-itemid-reflection-recent/`；
该批不涉及 microdata 解析、语义树、视觉或人工页面验收。
next345 的定向门覆盖 `HTMLElement.itemProp` ↔ `itemprop` raw UTF-8 getter/setter、attribute
round-trip 和移除恢复；`TEST312/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-194831-next345-itemprop-reflection/`。最近 `TEST264-312/999` 已以
50/50 通过，证据位于 `tmp/device-runs/20260820-194850-next345-itemprop-reflection-recent/`；
该批不涉及 microdata token 解析、语义树、视觉或人工页面验收。
next346 的定向门覆盖 `HTMLElement.itemRef` ↔ `itemref` raw UTF-8 getter/setter、attribute
round-trip 和移除恢复；`TEST313/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-195216-next346-itemref-reflection/`。最近 `TEST264-313/999` 已以
51/51 通过，证据位于 `tmp/device-runs/20260820-195233-next346-itemref-reflection-recent/`；
该批不涉及 microdata 引用解析、语义树、视觉或人工页面验收。
next347 的定向门覆盖 `HTMLElement.itemScope` ↔ `itemscope` 布尔 getter/setter、attribute
round-trip 和移除恢复；`TEST314/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-200253-next347-itemscope-reflection/`。最近 `TEST264-314/999` 已以
52/52 通过，证据位于 `tmp/device-runs/20260820-200311-next347-itemscope-reflection-recent/`；
该批不涉及 microdata item 解析、语义树、视觉或人工页面验收。
next348 的定向门覆盖 `HTMLElement.itemType` ↔ `itemtype` raw UTF-8 getter/setter、attribute
round-trip 和移除恢复；`TEST315/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-200637-next348-itemtype-reflection/`。最近 `TEST264-315/999` 已以
53/53 通过，证据位于 `tmp/device-runs/20260820-200656-next348-itemtype-reflection-recent/`；
该批不涉及 microdata vocabulary 解析、语义树、视觉或人工页面验收。
next349 的定向门覆盖 `HTMLElement.nonce` ↔ `nonce` raw UTF-8 getter/setter、attribute
round-trip 和移除恢复；首个 `<script>` 夹具没有产生 probe 结果，证据位于
`tmp/device-runs/20260820-201025-next349-nonce-reflection/`，已撤回并改用普通 `<div>`。最终
`TEST316/999` 已以 2/2 通过，证据位于
`tmp/device-runs/20260820-201118-next349-nonce-reflection-retry/`；最近 `TEST264-316/999`
已以 54/54 通过，证据位于
`tmp/device-runs/20260820-201314-next349-nonce-reflection-recent/`；该批不涉及 CSP、安全
策略、脚本执行、视觉或人工页面验收。

只有出现回归、设备环境变化或累计达到下一个检查点时，才需要再次运行完整链。

需要做人工视觉/输入验收时，临时把 staging 或工作区的 `auto` 改为 0；验收结束后务必恢复
`auto=1`，避免下一次设备门再次弹出确认框。

每项的观察目标如下：

| 测试 | 人工观察目标 |
| --- | --- |
| 13 | 从起始页打开 example.com；点击 `Learn More` 后内容仍在居中的虚拟容器内，左右边距存在，不能贴到屏幕边缘。继续进入 IANA Example Domains 和 `IANA-managed Reserved Domains`，页面不应崩坏。 |
| 20 | BMP、PNG、JPEG、GIF 四个有边框图片都显示，不能出现 fallback 文本。 |
| 27 | SVG 红块、绿块和光滑蓝色曲线显示，不能出现 SVG fallback 文本。 |
| 56 | 三个等高彩色行按 top/middle/bottom 对齐；下面两行等高，橙色 rowspan 文本在底部；不应出现垂直滚动条。 |
| 58 | 蓝色标题、160px 着色内容带、级联示例和红/绿/蓝表格显示；隐藏导航文字不可见。 |
| 62 | 复选框/单选框的选中与未选中外观正确；隐藏 input 不占可见行。 |
| 64 | 复选框可切换；禁用项保持不变；同一 radio 组互斥，不同 form/组不串状态。 |
| 65 | 四个 native EDIT 可见；首个输入框启动时可能显示 `wm-edit`（桥接探针的预置值）。点击它，用 SIP 输入并选中一个多字候选词，字段必须出现完整候选词而不是只出现下一个字母；密码框遮罩，readonly/disabled 不可修改，maxlength 生效。 |
| 66 | 可编辑 textarea 能输入多行；readonly/disabled 两个 textarea 不可修改；SIP 收起后布局不应错位。 |
| 67 | 第一个下拉框可打开并改选；禁用下拉框不能改；multiple 列表保留两个选中项并可切换第三项。 |
| 73 | checkbox、focus、disabled/enabled、button active 和 option:checked 的颜色/状态随操作变化；Reset 后回到初始状态。 |
| 75 | 灰色父框内依次看到红色 static、偏移后的绿色 relative、蓝色 absolute block、黄色 absolute inline；四个都不能跑出灰框。 |
| 232 | 真实 WM6 文件选择器：选择一个文件后页面显示文件名，事件 trace 恰好为 `input|file;change|file;`；再次打开并点 Cancel 后文件名和 trace 不变。 |
| 233 | input type=number 的 min/max 为 inclusive；下溢、上溢和 malformed value 阻止提交，malformed range 属性忽略，恢复到边界值后提交成功。 |
| 234 | input type=number 的 min-based step、默认 step=1、step=any 和非法 step 回退；动态不对齐值阻止提交，恢复到合法值后提交成功。 |
| 235 | input type=email 的单地址和 multiple 逗号列表拒绝 malformed token；动态修复后约束解除并正常提交。 |
| 236 | input type=url 拒绝空 authority/空白值，接受 scheme、relative、network-path，动态修复后正常提交；不代表完整 URL Standard。 |
| 237 | input type=range 默认 0..100，显式 min/max 与 step 生效；下溢、上溢和 step mismatch 阻止提交，恢复后成功。 |
| 238 | input type=date 采用 bounded YYYY-MM-DD 日历校验；闰年/无效日和 min/max 越界阻止提交，恢复后成功。 |
| 239 | input type=time 采用 bounded HH:MM/seconds/fraction 校验；无效时间和 min/max 越界阻止提交，恢复后成功。 |
| 240 | input type=month 采用 bounded YYYY-MM 校验；非法月份和 min/max 越界阻止提交，恢复后成功。 |
| 241 | input type=week 采用 bounded ISO YYYY-Www 校验；非法周/week-53 和 min/max 越界阻止提交，恢复后成功。 |
| 242 | input type=datetime-local 组合 bounded date/time 校验；非法时间和 min/max 越界阻止提交，恢复后成功。 |
| 243 | input type=color 采用 bounded #RRGGBB 校验；非法十六进制值阻止提交，恢复后成功。 |
| 244 | input type=date 采用 min-based step 天数；step mismatch 阻止提交，默认/any/非法和非正 step 回退后成功。 |
| 245 | input type=time 采用 min-based step 秒数；step mismatch 阻止提交，any/非法和非正 step 回退后成功。 |
| 246 | input type=month 采用 min-based step 月数；step mismatch 阻止提交，any/非法和非正 step 回退后成功。 |
| 247 | input type=week 采用 min-based step 周数；step mismatch 阻止提交，any/非法和非正 step 回退后成功。 |
| 248 | input type=datetime-local 采用 min-based step 秒数；step mismatch 阻止提交，any/非法和非正 step 回退后成功。 |
| 249 | product text/password custom validity setter 可阻止提交；清空消息后恢复成功。 |
| 250 | input type=date 无 min 时以有效 value 为 step base；不对齐值阻止提交，恢复后成功。 |
| 251 | input type=time 无 min 时以有效 value 为 step base；不对齐值阻止提交，恢复后成功。 |
| 252 | input type=month 无 min 时以有效 value 为 step base；不对齐值阻止提交，恢复后成功。 |
| 253 | input type=week 无 min 时以有效 value 为 step base；不对齐值阻止提交，恢复后成功。 |
| 254 | input type=datetime-local 无 min 时以有效 value 为 step base；不对齐值阻止提交，恢复后成功。 |
| 255 | required 空 text input 同时保留 valueMissing 与 product customError；填值后清空消息恢复提交。 |
| 256 | textarea 的 product custom validity 可阻止 required 验证；清空消息后恢复提交。 |
| 257 | text/password custom validity getter 返回完整 UTF-8 字节长度，并在小缓冲区安全截断。 |
| 258 | textarea custom validity getter 返回完整 UTF-8 字节长度，并在小缓冲区安全截断。 |
| 259 | custom validity 在 `PCore_LayoutDocument` 重排后仍阻止验证；清空后恢复提交。 |
| 260 | range 缺少 value 时以默认 0..100 中点 50 提交；设置显式值后使用显式值。 |
| 261 | range 的有效显式 min/max 缺省中点 25 通过 text-control bridge 读回并提交；显式值 35 覆盖。 |
| 262 | 自动验证 file input 的程序化 click 只排队一次宿主 picker；选择后发出一次 input/change，取消、disabled 和文档替换不改文件状态。 |
| 263 | 手工验证脚本 `file.click()` 在真实 WM6 窗口中延迟打开 picker；选择后显示 value 和 input/change，再次 Cancel 保持状态。 |
| 264 | 自动验证 `HTMLElement.disabled` getter/setter 与既有 attribute bridge 同步；禁用 required 控件跳过 validation/submission，启用后恢复阻断与成功提交，重新禁用后再次排除。 |
| 265 | 自动验证约束相关反射属性：`required`、`readOnly`、`multiple`、`noValidate`、`formNoValidate` 和 `min/max/step`；动态范围下溢/上溢/step mismatch、readonly 与 form/button no-validate 绕过/恢复，以及成功提交字段均须符合预期。 |
| 266 | 自动验证脚本可查询控件 `checkValidity()`、`willValidate` 与基础 `validity` flags；覆盖 required/disabled/readonly/select、number 下溢/上溢/step mismatch，以及动态恢复。 |
| 267 | 自动验证脚本 `setCustomValidity()`/`validationMessage` 与 `validity.customError` 的 set/get/clear；覆盖 text、number、textarea、select、checkbox 和 core 直接 API 的 unsupported button 边界。 |
| 268 | 自动验证按 DOM id 聚合的 form `checkValidity()`；覆盖 required、disabled、readonly、custom validity、number 下溢、动态恢复和 `novalidate` 不绕过查询。 |
| 269 | 自动验证 form/control `reportValidity()`；覆盖 invalid 事件目标与顺序、non-bubbling/cancelable/trusted 字段、preventDefault 不改变 boolean 结果、动态恢复、`novalidate`、disabled/readonly 跳过。 |
| 270 | 自动验证 `validationMessage` 的固定英文内置 fallback、custom message 优先级、required/range/type mismatch、动态清除和安全截断。 |
| 271 | 自动验证 `pattern`、`minLength`、`maxLength` 的脚本属性反射、动态 tooShort/tooLong/patternMismatch flags 和非法负数/非有限 setter。 |
| 272 | 自动验证表单 `name` 属性的脚本反射、attribute round-trip，以及 input/textarea/select/button 动态改名后的 successful-control submission。 |
| 273 | 自动验证 form `action`/`method` 的脚本反射、attribute round-trip，以及动态修改后的受限 GET/urlencoded-POST submission 目标和 method 判定。 |
| 274 | 自动验证 form `enctype` 的脚本反射、attribute round-trip，以及动态切换 urlencoded/multipart submission 并恢复的行为。 |
| 275 | 自动验证 submitter `formAction` 的脚本反射、attribute round-trip，以及动态覆盖 urlencoded/multipart action 并移除恢复 form action。 |
| 276 | 自动验证 submitter `formMethod` 的脚本反射、attribute round-trip，以及动态覆盖 GET/POST method 并移除恢复 form method。 |
| 277 | 自动验证 submitter `formEnctype` 的脚本反射、attribute round-trip，以及动态覆盖 urlencoded/multipart、snapshot eligibility 并移除恢复 form enctype。 |
| 278 | 自动验证 text-input 隐式 Enter 与显式首个 submitter 共享 action/method/enctype override，并保持 multipart snapshot action 与 part count 一致。 |
| 279 | 自动验证 form `target` 的脚本反射、attribute round-trip、移除恢复，并确认 submission action/method 保持不变。 |
| 280 | 自动验证 form `autocomplete` 的脚本反射、attribute round-trip、移除恢复，并确认 submission 保持不变。 |
| 281 | 自动验证 form `acceptCharset` ↔ `accept-charset` 的脚本反射、attribute round-trip、移除恢复，并确认 submission 保持不变。 |
| 282 | 自动验证 input `placeholder` 的脚本反射、attribute round-trip、移除恢复，并确认 current value 与 submission 保持不变。 |
| 283 | 自动验证 input `autocomplete` 的脚本反射、attribute round-trip、移除恢复，并确认 current value 与 submission 保持不变。 |
| 284 | 自动验证 input `inputMode` ↔ `inputmode` 的脚本反射、attribute round-trip、移除恢复，并确认 current value 与 submission 保持不变。 |
| 285 | 自动验证 input `type` raw 属性的脚本反射、attribute round-trip、移除恢复，并确认既有 text-control submission 保持不变。 |
| 286 | 自动验证 textarea `placeholder` 的脚本反射、attribute round-trip、移除恢复，并确认 current value 与 submission 保持不变。 |
| 287 | 自动验证 select `autocomplete` 的脚本反射、attribute round-trip、移除恢复，并确认选中值与 submission 保持不变。 |
| 288 | 自动验证 button `type` 的脚本反射、attribute round-trip、移除恢复，并确认恢复为 submit 后 submission 保持不变。 |
| 289 | 自动验证未知 form `method` 的脚本反射、attribute round-trip、移除恢复，并确认 submission 安全回落为 GET。 |
| 290 | 自动验证未知 form `enctype` 的脚本反射、attribute round-trip、移除恢复，并确认 POST 安全回落为 urlencoded。 |
| 291 | 自动验证 form method/enctype mixed-case 的脚本反射、attribute round-trip、移除恢复，并确认大小写不敏感的 urlencoded POST。 |
| 292 | 自动验证动态 action/method/value 更新后两次重排的 submission metadata、action/body 及 size 字段保持一致。 |
| 293 | 自动验证 form reset 恢复控件默认值、保留动态 action/method，并重新生成正确 submission metadata。 |
| 999 | 所有项目完成后只听到一次系统提示音。 |

TEST190-231 是自动 history/script-session/bootstrap/DOM-read/DOM-write/DOM-attribute/value/checked/form-property/navigation/location/event/input/key/focus/edit/select/click/form-event/invalid/file-input/checkbox-radio-input/change/label-click/toggle-key/programmatic-click/form-button/file-input-click/file-picker-boundary 断言，不属于这次需要肉眼观察的包；TEST232 和 TEST263 是 manual-only 的真实 WM6 picker 入口，不能放入自动设备门；TEST233 是自动的 type=number min/max/bad-input constraint-validation 门，覆盖下溢、上溢、非法值、malformed 属性忽略和边界恢复；TEST201
直接调用 `positron_browser.dll` 公共 history API，TEST202 直接验证 product script session，
TEST234 是自动的 number step mismatch/default/any constraint-validation 门，和 TEST233 一样只需
定向设备门，不需要人工页面观察。
TEST235 是自动的 email typeMismatch 门，覆盖单地址、multiple 列表、malformed token 和动态
恢复，不需要人工页面观察。
TEST236 是自动的保守 url typeMismatch 门，覆盖 scheme/relative/network-path 正例、空 authority
和空白负例；它不替代完整 URL Standard 或人工真实导航验收。
TEST237 是自动的 range 边界门，覆盖默认/显式 min/max、step mismatch 和动态恢复，不需要
人工页面观察；它不替代 native slider 的视觉/触摸验收。
TEST238 是自动的 bounded date 门，覆盖 ISO 语法、闰年/无效日、min/max 和动态恢复，不需要
人工页面观察；它不替代 native date picker 的视觉/触摸验收。
TEST239 是自动的 bounded time 门，覆盖 HH:MM/seconds/fraction、无效时间、min/max 和动态
恢复；需要 sub-minute fraction 时显式使用 step=any，不需要人工页面观察；它不替代 native
time picker 的视觉/触摸验收。
TEST240 是自动的 bounded month 门，覆盖 YYYY-MM、非法月份、min/max 和动态恢复，不需要
人工页面观察；它不替代 native month picker 的视觉/触摸验收。
TEST241 是自动的 bounded week 门，覆盖 ISO YYYY-Www、week-53、min/max 和动态恢复，不需要
人工页面观察；它不替代 native week picker 的视觉/触摸验收。
TEST242 是自动的 bounded datetime-local 门，覆盖 date/time 组合、非法时间、min/max 和动态
恢复，不需要人工页面观察；它不替代 native datetime picker 的视觉/触摸验收。
TEST243 是自动的 bounded color 门，覆盖 #RRGGBB 语法、非法十六进制值、动态恢复和
submission，不需要人工页面观察；它不替代 native color picker 的视觉/触摸验收。
TEST244 是自动的 date step 门，覆盖 min 作为步长基准、默认 step=1、step=any、非法/非正
step 回退和动态恢复，不需要人工页面观察；它不替代 native date picker 的视觉/触摸验收。
TEST245 是自动的 time step 门，覆盖 min 作为步长基准、默认 step=60 秒、step=any、非法/非正
step 回退和动态恢复，不需要人工页面观察；它不替代 native time picker 的视觉/触摸验收。
TEST246 是自动的 month step 门，覆盖 min 作为步长基准、默认 step=1 月、step=any、非法/非正
step 回退和动态恢复，不需要人工页面观察；它不替代 native month picker 的视觉/触摸验收。
TEST247 是自动的 week step 门，覆盖 min 作为步长基准、默认 step=1 周、step=any、非法/非正
step 回退和动态恢复，不需要人工页面观察；它不替代 native week picker 的视觉/触摸验收。
TEST248 是自动的 datetime-local step 门，覆盖 min 作为步长基准、默认 step=60 秒、step=any、
非法/非正 step 回退和动态恢复，不需要人工页面观察；它不替代 native datetime picker 的视觉/触摸验收。
TEST249 是自动的 product custom validity 门，覆盖 text-input setter、customError 阻断、清空消息
和成功 submission，不需要人工页面观察；它不代表完整 DOM `setCustomValidity()` 或 native
`invalid` UI 已完成。
TEST250 是自动的 date step-base 门，覆盖没有 min 时使用有效 value 作为基准的规范语义和动态
恢复，不需要人工页面观察；它不替代 native date picker 的视觉/触摸验收。
TEST251 是自动的 time step-base 门，覆盖没有 min 时使用有效 value 作为基准的规范语义和动态
恢复，不需要人工页面观察；它不替代 native time picker 的视觉/触摸验收。
TEST252 是自动的 month step-base 门，覆盖没有 min 时使用有效 value 作为基准的规范语义和动态
恢复，不需要人工页面观察；它不替代 native month picker 的视觉/触摸验收。
TEST253 是自动的 week step-base 门，覆盖没有 min 时使用有效 value 作为基准的规范语义和动态
恢复，不需要人工页面观察；它不替代 native week picker 的视觉/触摸验收。
TEST254 是自动的 datetime-local step-base 门，覆盖没有 min 时使用有效 value 作为基准的规范语义和动态
恢复，不需要人工页面观察；它不替代 native datetime picker 的视觉/触摸验收。
TEST255 是自动的 custom validity 组合门，覆盖 required 空值同时保留 valueMissing 与 customError、
填值后仅保留 customError、清空消息后成功提交；不需要人工页面观察，也不代表完整 DOM
`setCustomValidity()` 或 native invalid UI 已完成。
TEST256 是自动的 textarea custom validity 门，覆盖 textarea setter、required/customError 阻断、
清空消息和成功 submission；不需要人工页面观察，也不代表完整 DOM `setCustomValidity()` 或
native invalid UI 已完成。
TEST257 是自动的 custom validity getter 门，覆盖 text/password 读回、UTF-8 字节长度、容量不足时
的 NUL 截断和清空后的空消息；不需要人工页面观察，也不代表完整 DOM `validationMessage`。
TEST258 是自动的 textarea custom validity getter 门，覆盖 textarea 读回、UTF-8 字节长度、容量
不足时的 NUL 截断和清空后的空消息；不需要人工页面观察，也不代表完整 DOM `validationMessage`。
TEST259 是自动的 custom validity 生命周期门，覆盖设置消息、文档 re-layout 后继续阻断验证、
清空消息后成功提交；不需要人工页面观察，也不代表完整 DOM 生命周期。
TEST260 是自动的 range 默认值门，覆盖缺少 value 时默认范围中点 50 的成功控件提交，以及
显式 value 覆盖默认值；不需要人工页面观察，也不替代 native slider 视觉/触摸验收。
TEST261 是自动的 range bridge 默认值门，覆盖有效 min=10/max=40 时中点 25 的 core 读回、验证和
提交，以及显式值 35 的动态覆盖；不需要人工页面观察，也不替代 native slider 视觉/触摸验收。
TEST262 是自动的 programmatic file-picker queue 门，覆盖 click 取消、disabled 静默、单次排队、
注入选择后的 input/change、再次取消保留 value/path，以及文档替换后的安全丢弃；它不打开系统
GUI。TEST263 是 manual-only 的真实 WM6 programmatic picker fixture，验证有 render window 时
`file.click()` 在脚本回调返回后打开系统 picker，并验证选择/同窗口取消的页面状态。
TEST246 是自动的 month step 门，覆盖 min 作为步长基准、默认 step=1 月、step=any、非法/非正
step 回退和动态恢复，不需要人工页面观察；它不替代 native month picker 的视觉/触摸验收。
TEST203 直接验证 product bootstrap，TEST204 直接验证 product DOM read callback adapter，TEST205
直接验证 product DOM write callback adapter，TEST206 直接验证 product DOM attribute callback adapter，TEST207
直接验证 product event callback adapter、事件数据编码和 preventDefault 结果；TEST208
直接验证 product DOM value callback adapter；TEST209 直接验证 product DOM checked callback adapter；TEST210
直接验证 product form-property JSON dispatch、defaultValue/defaultChecked/selectedIndex typed adapters；TEST211
直接验证 product navigation JSON dispatch、十种 operation kind、URL/state/delta 编码和 typed adapter；TEST212
直接验证 product same-document traversal/hash location dispatch、事件顺序、状态更新和临时 global 清理；TEST213
直接验证 product native input/composition typed dispatch contract、取消结果、错误映射和注销，避免只验证
test_host 私有适配；TEST214 直接验证 product native keyboard typed dispatch contract、keydown/keyup
字段、取消结果、错误映射和注销；TEST215 直接验证 product focus/blur/focusin/focusout typed
dispatch contract、bubbles 字段、非法事件、错误映射和注销，避免只验证 test_host 私有适配。
TEST216 直接验证 product native SELECT change typed dispatch contract、坐标与冒泡字段、非法事件、错误映射和注销，避免只验证
test_host 私有适配；TEST217 直接验证 product native SELECT input typed dispatch contract、坐标与冒泡字段、非法事件、错误映射和注销；TEST218 直接验证
product native EDIT change typed dispatch contract、坐标与冒泡字段、非法事件、错误映射和注销；TEST219 直接验证
product native EDIT post-change input typed dispatch contract、坐标与冒泡字段、非法事件、错误映射和注销；TEST220 直接验证
product native click typed dispatch contract、坐标与冒泡字段、取消结果、非法事件、错误映射和注销；TEST221 直接验证
product native submit/reset typed dispatch contract、坐标与冒泡字段、取消结果、非法事件、错误映射和注销；TEST222 直接验证
product native invalid typed dispatch contract、坐标与冒泡字段、取消结果、非法事件、错误映射和注销。
TEST223 直接验证 file-input 复用既有 input/select typed callback、`insertFromFile` metadata、
`input`/`change` 顺序、取消结果、非法参数、adapter error 和注销。
TEST224 通过真实 core form activation 路径验证 checkbox/radio 只有在状态提交后才发出
非可取消、可冒泡的 `change`，已选 radio 和 disabled checkbox 不误报，并验证脚本事件目标。
TEST225 通过同一真实 activation 路径验证 checkbox/radio 在状态提交后按 `input` → `change`
顺序分发，`input` 使用空 `inputType/data`、`bubbles=1`、`cancelable=0`、
`isComposing=false`，并验证已选 radio 与 disabled checkbox 静默。
TEST226 通过真实 label activation 验证 label click、目标 checkbox click、`input`、`change`
顺序；目标 click 被取消时不改变 radio 状态，disabled 控件不接收合成 click。
TEST227 通过真实渲染窗口的主窗口 WM_KEYDOWN/WM_KEYUP 验证 checkbox/radio 的 Space/Enter
activation：`keydown` → `click` → `input` → `change` → `keyup` 顺序、keydown/click
取消、重复 keydown 不重复切换以及 disabled 控件静默。
TEST228 直接验证 product programmatic-click callback 的重复注册、非法参数、adapter error、
注销和 native function 资源关闭，并通过真实脚本 `HTMLElement.click()` 验证 checkbox/radio
的 click 目标、`click` → `input` → `change` 顺序、取消、disabled/no-op、radio 互斥和状态。
TEST229 通过真实脚本 `HTMLElement.click()` 验证 native submit/reset/button 的 click 目标、
submit/reset form-event 顺序、取消、reset 初值恢复、generic button 和 disabled no-op；同时
确认 reset 默认路径不会重复发出 reset 事件。
TEST230 通过真实脚本 `HTMLElement.click()` 验证 native file input 的 click 目标、click
取消、disabled/no-op 和空文件状态；自动路径只停在 typed click contract，不打开系统文件选择器，
picker、文件系统权限和窗口生命周期继续由宿主 GUI 路径负责。TEST228 继续覆盖
programmatic-click adapter error、注销和 native function 资源关闭。
TEST231 通过宿主注入的同步 picker adapter 验证选择、取消、picker 错误、空选择提交错误、
`input` → `change` 顺序、再次取消保留既有文件状态，以及 callback 不重入且调用结束后无活动状态；
真实 WM6 picker 仍只通过 `GetOpenFileNameEx` 的 GUI 路径运行。
TEST232 是 manual-only 的真实 WM6 picker fixture：它保持一个显式开启的脚本 session，
让页面显示选中文件名和 `input|file;change|file;` trace；它不伪造系统对话框，也不会被
自动设备门选中。取消、窗口返回和文件状态由验收者在设备上观察；picker 错误/无效输入的
可注入边界继续由 TEST231 自动覆盖。
TEST262 是自动的 host queue/adapter 边界；TEST263 是 manual-only 的 programmatic picker
fixture，保持一个显式脚本 session，让 checkbox listener 调用 `file.click()`，由宿主消息循环
在脚本返回后打开 `GetOpenFileNameEx`。它不把 picker、窗口或文件系统权限迁入
`positron_browser.dll`；取消必须保留已有文件状态。该入口已由用户完成真实 WM6 GUI 验收。
TEST264 是自动的 `HTMLElement.disabled` 属性门，覆盖 getter/setter 的 attribute round-trip、
required validation 的禁用/启用切换、successful-control submission 的包含/排除以及动态值
恢复；它不需要人工页面观察，也不宣称完整 DOM IDL reflection。
TEST265 是自动的约束相关 reflected-property 门，覆盖 `required`、`readOnly`、`multiple`、
`noValidate`、`formNoValidate` 和 `min/max/step` 的 getter/setter round-trip；同时验证动态
range underflow/overflow/step mismatch、readonly 绕过/恢复、form/button no-validate 绕过/恢复、
successful-control submission 和 draft form 的恢复提交，不需要人工页面观察，也不宣称完整
DOM IDL reflection。
TEST266 是自动的 browser validation-query 门，覆盖控件 `checkValidity()`、`willValidate` 和
`validity` 对 required/disabled/readonly/select 控件的候选状态，以及 number 的
`rangeUnderflow`/`rangeOverflow`/`stepMismatch` flags 和动态恢复；它验证的是按 id 的查询型
bridge，不宣称 form-level 聚合、`validationMessage`、invalid 事件触发或 native invalid UI。
TEST267 是自动的 browser custom-validity 门，覆盖按 id 的 UTF-8 message set/get、脚本
`setCustomValidity()`、`validationMessage`、`validity.customError` 和 clear 后恢复；它验证
当前 form-control candidates 的控件级 message，不宣称 form-level 聚合、invalid 事件
触发、native invalid UI 或完整本地化错误消息。
TEST268 是自动的 form-level validation-query 门，覆盖按 form id 聚合当前 controls、忽略
`novalidate`、跳过 disabled/readonly/submit 控件、传播 custom validity 与 number range flags，
并验证动态禁用/恢复。该 API 可在布局前后查询，但只提供受限的 form `checkValidity()`，不触发
`invalid` 事件、不提交、不实现 `reportValidity()` 或 native invalid UI。
TEST269 是自动的 report-validity 门，覆盖 form/control `reportValidity()` 的 invalid 事件目标、
DOM 顺序、non-bubbling/cancelable/trusted 字段、`preventDefault()` 后仍返回 false、动态恢复、
`novalidate` 不绕过和 disabled/readonly 非候选边界。它不实现 native validation UI、焦点/滚动、
本地化错误消息或提交行为。

## 运行自动设备门

### 一键设备门

先在 WMDC/Device Emulator GUI 中建立当前设备连接。USB 真机和 DMA emulator 均可；
RAPI 1 只暴露 WMDC 当前会话，因此 gate 不枚举设备、不绑定 VMID，也不会连接、选择、
启动、Cradle、断开或重置设备：

```bat
scripts\device_gate.bat -Candidate nextNNN
```

脚本使用 WMDC 官方 32 位 RAPI 通道，并完成正式增量构建、隔离 staging、整包部署、
`test_host.exe` 启动、有限等待、日志回收和自动判门。每次运行使用唯一的设备端
`\Temp\Positron-device-gate\<candidate>-<timestamp>`，不会读取旧日志。下一次运行只回收
这个 gate 根目录中符合自身命名规则的旧候选目录；未知目录一律保留。完整证据保存在本地
`tmp/device-runs/`。

默认测试选择来自 tracked `test_host/test_host.ini`。调试或批次定向门可只改本次隔离
staging 的 `tests=` 值，不修改 tracked ini：

```bat
scripts\device_gate.bat -Candidate nextNNN-debug ^
  -TestSelection "190-194,999"
```

`-PlatformName`/`-DeviceName` 不用于 RAPI gate；更换设备只需先在 GUI 中切换 WMDC 当前连接。
RAPI 1 没有安全的远端等待/终止接口，因此 gate 以完整 `TESTBENCH PASS/FAIL` 日志作为完成
标记；超时会保存可取得的部分日志并返回非零，但不会杀死设备进程。连接缺失、部署失败、
运行超时、日志缺失或任一判门条件失败也都会返回非零。自动门仍不替代下文列出的人工视觉
和输入检查。

如果 `CeRapiInit` 报 `0x8007007E`，先运行：

```bat
scripts\repair_wmdc_rapi.bat
```

该脚本自行请求 UAC，只把 5 个已知 WMDC/RAPI COM 类的旧 `%windir%` 注册改为现有
32/64 位 DLL 绝对路径；DLL、注册键或原值不符合预期时会拒绝修改。旧 CoreCon gate 为何
无需该修复、WMDC 已连接为何仍不能由 CoreCon 活动连接枚举发现，以及 `0x8007007E` 的完整
取证见[故障排查](TROUBLESHOOTING.md#wmdc-自动设备门不要混淆-corecon-与-rapi)。

### 手工设备门

人工包不是自动跑完即退出：每个 render 测试会停在屏幕上，必须由验收者看完并按
`Esc`（或点空白处）关闭。推荐按下面的顺序执行：

1. 先在 WMDC/Device Emulator GUI 中确认当前只有一个已连接设备；不要让脚本替你连接、
   选择或重置设备。
2. 关闭设备上已有的 `test_host.exe`，在仓库根目录执行：

   ```bat
   scripts\stage_manual_picker.bat Debug C:\WMShare\Positron-manual-next295
   ```

   该脚本先按正式 `stage.bat` 构建并复制整包，再只替换 staging 目录中的
   `test_host.ini` 为 `auto=0`、`javascript=1`、`tests=232,263,999`；tracked 的自动 INI 不会改变。

3. 在设备 File Explorer 打开 `Storage Card\Positron-manual-next295`（或共享目录映射的
   对应路径），确认 `test_host.exe` 与上表配置的 `test_host.ini` 在同一目录，然后运行
   `test_host.exe`。
4. 启动确认框必须显示选择 `232, 263, 999`，并显示
   `Browser JavaScript: experimental inline scripts enabled.`。点击 **Yes**；点 **No** 会退回普通分组选择，不是本次流程。
5. TEST232 开始时先读提示框，再点 OK 进入 render 窗口：在同一个 render 窗口内点击
   File 控件，在真实 WM6 对话框中选一个小文件并确认；回到页面后确认文件名出现、trace
   恰好为 `input|file;change|file;`。仍在同一个 render 窗口内再次点 File 并按
   **Cancel**，确认文件名和 trace 均保持不变。关闭 render 窗口后重新运行 TEST232
   会创建新的 fixture，不要求跨次运行保留文件状态。记录设备型号、viewport/DPI 和截图；
   最后用 `Esc` 或点空白处关闭窗口。
6. TEST232 关闭后应出现 `TEST 232 OK`，其中 value/path 非空且 trace 与上述字符串一致；
   若选择失败、页面崩溃、回到页面后状态丢失或 trace 重复，立即记录为失败，不要继续扩大范围。
   随后继续 TEST263：先读提示框并点 OK，在 render 窗口只点击
   **Script click file input** 复选框（不要直接点击 File 控件）。它必须通过脚本的
   `file.click()` 打开真实 WM6 picker；选择文件后页面应显示文件名，Events 应以
   `click|file;input|file;change|file;` 开头。再次点击同一复选框并在 picker 中按
   **Cancel**，文件名必须保持，Events 只增加最后一个 `click|file;`，不增加第二组
   `input|file;change|file;`。关闭该窗口后应出现 `TEST 263 OK`。最后 TEST999 应只触发一次
   系统提示音，随后出现 `Configured tests passed`。
7. 如果之后仍要运行原来的视觉/输入包，另行使用普通 `stage.bat`，不要把本次手工 INI
   复制回仓库；普通包仍按启动框中的 13 项和上表逐项操作。TEST13 先截取 example.com
   初始页，再截取 `Learn More` 后和 IANA 页面；TEST65/66/67 要实际操作控件并记录完整
   SIP 候选词，最后 TEST999 只应发出一次提示音。
8. `auto=0` 的交互路径不会创建 `test_host.log`；请保存截图、设备型号/viewport/DPI、
   操作步骤和异常描述到本地 `tmp/`。若还需要机器可判定的完整日志，应在人工记录完成后
   恢复自动三行配置，再单独运行自动 gate；自动日志不能替代人工截图。

若另行运行自动配置，日志至少应满足：

- 开头记录实际 screen 和 DPI；
- 所选测试都有预期的 `OK` 或明确的汇总记录；
- 没有非预期 `ERROR`；
- 没有 `FAIL`；
- 最终包含 `TESTBENCH PASS`；
- 网络 Browse 门完成了配置要求的页面序列，而不是只打开第一屏。

进程退出、提示音或看到部分 `OK` 都不能单独证明整批通过。

## 自动门分层

每个能力批次都要运行与风险直接相关的自动设备门。

低风险、局部变更可以运行：

- 本批新增测试；
- 直接共享的旧路径；
- TEST13 真实网页导航哨兵；
- TEST999 完成提示音。

满足以下任一情况时运行全量门：

- 累积了约五个低风险定向批次；
- 触及公共 DLL/ABI、布局/重绘、网络、输入基础设施；
- 准备交付一个里程碑；
- 出现超时、崩溃、数据错误、混包或无法解释的异常。

全量或定向选择不是稳定常量，始终以当前 handoff 和候选 ini 为准。

## 人工验收

以下风险不能由首帧自动测试替代：

- 字体 fallback、字形、抗锯齿和颜色；
- 左右边距、居中容器、换行、表格和列表观感；
- 真实触摸、链接命中、滚动和返回；
- SIP 候选词、IME composition、Unicode 输入；
- 旋转、不同 screen/DPI 和滚动位置保持；
- 失败网络、旧页保留和 loading 反馈；
- 真实页面的深层导航。

低风险视觉或输入变化可以累计若干批次后集中验收。遇到崩溃、数据损坏、严重布局破坏或
核心交互阻塞时必须立即人工复核，不能等待累计窗口。

人工截图和设备日志放在仓库本地 `tmp/`；该目录不得加入 Git。比较截图时必须记录页面、
viewport、DPI、方向、操作步骤和对应候选包，避免把偶然加载、旧 DLL 或不同滚动位置误认为
代码变化。

## 网络测试注意事项

- 运行证书测试前，把模拟器系统时钟调整到当前日期；默认旧时钟会让现役证书显示为尚未生效。
- 分清 DNS、TCP、TLS、证书、HTTP status、redirect 和页面提交阶段。
- 失败导航应保留旧页时，不能只看最终窗口是否仍有内容。
- TEST13 是回归哨兵，不等于互联网任意网站兼容性。

## 候选成为基线的条件

一个候选只有在以下条件都满足后才能写成设备基线：

1. 源码和配置范围清楚；
2. C89 回归与仓库审计通过；
3. ARMV4I 正式构建通过；
4. stage 包没有混用旧二进制；
5. 所需设备日志完整通过；
6. 本批要求的人工门已经完成，或明确进入允许累计的清单；
7. handoff、限制和路线图只更新各自职责内的事实。

如果日志不在当前工作区，仅有“已经跑完”不能替代对完整日志的读取和判定。

# 目录应用的空资源提交合同

## 消费者缺口

Runtime 的 `PRuntime_DirectoryReadResource` 明确允许成功读取零字节文件；Browser 的
`PBrowser_NavigationResourceSetData` 则在 `data_len <= 0` 返回参数错误。
Core fetch/缓存消费者也须区分“已经取得空内容”与“不可用/未取得”。
真实目录应用可以包含空 CSS、空 `@import` 或空 classic JS；它们不应成为必需资源失败，
也不应因始终 pending 而卡住候选。

EXE 当前沿既有非空资源路径：必需空 CSS 会拒绝候选并保留旧页，可选空脚本回退。
不填入空格、注释、NUL 或假 HTTP 正文绕过，不在宿主另造资源 ready/commit 语义。
此缺口由“主线与DLL”会话拥有；EXE 只在公共合同完成后接入。

## 所有权与兼容

- Browser 拥有资源状态、已取字节、预算/统计及 required/optional commit gate；Core 拥有
  stylesheet/script 解析、资源缓存和空内容语义。Runtime 的目录读取及物理 I/O 不需改写。
- 保持稳定 C ABI、显式长度、原内存所有权和旧消费者；先核对旧文档是否把正长度作为合同，
  必要时追加版本化 Ex，不悄悄放宽旧错误语义。
- READY 且长度为 0 必须可与 pending、失败和取消区分；不能用 URL、MIME、文件名或
  单独的宿主布尔值替代事务状态。零字节仍占一个资源槽，数量/重试/总字节预算不扩大。
- 规定零内容的输入指针、复制容量、`written`、重复 SetData、失败不改输出及释放行为；
  可以分配最小缓冲存放零字节结果，但实际长度必须仍为 0，不依赖 `malloc(0)` 的平台结果。
- 图片空内容仍可在解码层失败；“已取得 bytes”不是“有效图像”。不把整个空资源类别统一
  冒充渲染成功，不改变 HTTP/TLS、取消、旧页保留或脚本异常策略。

## 最小验收

1. 直接公开 API 验证空内容 READY/Copy/GetStats/commit gate，非法 handle/NULL/容量、
   重复终态、取消/stale、失败输出 canary 和释放；原非空资源 corpus 不变。
2. Core 经真实 callback 取得空 CSS 与空嵌套 import，完成 style/layout；仍正确应用其他
   作者样式。空 classic JS 为无副作用的有序脚本，不冒充未取得外部脚本。
3. 对照缺失必需 CSS、可选失败和已取得但无效的空图片；必需 I/O 失败继续阻止提交，
   老 DOM/history/session 保留，不留下 pending 或无限重试。
4. Runtime→Browser→Core 的 `app://` 目录组合使用一次明确读取，证明实际长度仍为零、
   不路由到 HTTP，成功提交只产生一次 history。HTML 空主文档若另有既有 parser 限制，
   明确列为独立边界，不借此扩张通用 HTML 解析或伪造文档。
5. C89、审计、正式 Debug/Release、相邻非空资源回归和 ARMV4I 自动设备门；test_host
   只提供 fixture/平台接线/断言，不编译 DLL 产品源文件。更新公共头文件/组件文档，
   给 EXE 明确的 SetData/Copy/fetch 调用及失败/所有权说明。

## EXE 后续接入点

公共合同完成后修改 `app_navigation_fetch_resource` 的 Runtime 分支，以及
`AppResources_Fetch` 对 READY/长度/分配的处理；仍使用同一 candidate/resource transaction，
不额外构建空资源状态机。将空 CSS/JS 加入现有目录页面设备门，并重新验证旧页回滚、可选
失败、取消/退休、owner 关闭和普通 HTTP(S) 路径。SQL 队列/SDK 是另一个未完成纵切，
不与本资源合同混为同一产品能力。

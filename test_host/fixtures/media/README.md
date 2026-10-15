# 压缩媒体断言夹具

本目录只用于 `test_host`。纯红画面与程序生成的纯音由
[`scripts/media_fixtures.py`](../../../scripts/media_fixtures.py) 离线生成，无下载的第三方媒体内容。
生成内容按 CC0-1.0 提供，不授予 AVC/AAC 专利许可；FFmpeg/x264 工具许可证不被重新标记。
工具版本、可执行文件哈希、每条生成参数、文件长度与 SHA-256 固定在 `manifest.json`。
仓库构建只部署固定文件，不调用桌面编码器或联网。

- `baseline-aac.mp4`：320×240、5 fps、三帧 Constrained Baseline/AVCC，AAC-LC 双声道 48 kHz。
- `main-vga.mp4`：640×480、5 fps、三帧 Main，包含 B 帧和 decoder drain。
- `mixed-main-aac.mp4`：复制 `main-vga.mp4` 的视频和 `baseline-aac.mp4` 的音频到同一
  MP4，不重编码；视频源 PTS 0/200000/400000 µs，AAC 实际输出仍为 29×1024 stereo samples。
  `mixed-packet-limit.mp4` 在固定 Baseline MP4 的首个 AVC sample 前插入合法 filler NAL，
  同步调整 stsz/stco/mdat，不改 slice/AAC；首包 263027 bytes，超过 256 KiB。
  桌面可正常解出三帧与 29 块音频，负样本不是靠损坏解码触发拒绝。
- `mixed-clock-{video-late,audio-late,shifted}.mp4` 只修改固定 Baseline MP4 的 v0
  edit-list：各轨开头插入 empty edit，音视频起点分别为 400/0、0/400、600/600 ms。
  保留原 media_time（包括 AAC priming 裁剪）、全部压缩 mdat 内容和除 stco 外的 sample
  tables，更新 box 大小、movie/track duration 与文件偏移；不重编码、不 remux。
  各仍输出三帧和 29×1024 stereo samples，分别提供音频先结束、视频先结束和共同非零起点控制。
- `aac-lc.aac`：ADTS AAC-LC 单声道 48 kHz；`aac-main.aac` 用于拒绝非 LC profile。
  MP4 的 AAC priming 包 PTS 为 -1024 sample；设备输出从 PTS 零起为 29 块、每声道
  29696 sample。ADTS 为 30 块、每声道 30720 sample。TEST1332 各完整解码三次，
  在 EOF 后暂停并 seek 到零两次，后两次全部 S16LE 字节与首次输出一致；同时保护
  PTS/时长、样本数和 EOF，不以相同幅度或校验值代替逐字节断言。打开后 source 被设为
  I/O 错误，重播不得再次读取宿主。非零 AAC seek 不在此合同内。
- `aac-lc-44100.aac` 是单声道 44.1 kHz LC、27 块；`aac-lc-stereo.aac` 是 48 kHz LC、
  30 块，左右声道分别为 440/660 Hz、幅度 0.08/0.06。各块 1024 samples/声道；
  与原 `aac-lc.aac` 一起作为格式变化的完整片段参考，不裁剪 ADTS 编码 padding。
  `aac-clock-{repeat,rate-down,rate-up,stereo,mono,rate-return}.aac` 只按完整片段拼接：
  分别是 48k mono 重复、48k→44.1k、44.1k→48k、mono→stereo、stereo→mono 和
  48k→44.1k→48k；不篡改 ADTS header 或 AAC payload。独立桌面校验检查每帧 LC/
  sample rate/声道/1024 samples，帧数分别为 60/57/57/60/60/87，证明不是损坏输入。
  TEST1354 以三个单独片段的设备 PCM 为首段逐字节参考：默认模式输出所有片段并缓存重播；
  时钟模式对变化组保留完整首段后 FORMAT，变化块不进入 callback，后续恢复原参数也不继续。
  参数不变的重复控制正常 EOF；暂停中丢弃待块/seek 零、两种 seekability、禁止重读源、
  一次性错误/STATE、stop/close 与停止后查询也有断言。不证明任意 AAC 动态重配置、
  MP3 变化或超出转换预算的布局拒绝；变化后 PCM 只检查块形状，不与独立新 decoder 作字节等同。
- `high.mp4`、`high422.mp4`、`interlaced.mp4`、`oversize.mp4`：拒绝 profile、像素布局、隔行和 656×480 超限。
- `mjpeg-mp3.avi`：320×240、5 fps、三帧全范围 MJPEG 4:2:0，MP3 双声道 48 kHz。
  固定 AVI 含一个空视频槽，实际帧 PTS 为 0/400000/600000 µs，每帧 duration 为 200000 µs；
  不能按输出帧号重新生成等间隔时间戳。MP3 解码为每声道 29952 个 sample，包含容器中的编码 padding。
- `mjpeg422.avi`：全范围 MJPEG 4:2:2 拒绝夹具，不应产生输出回调或 session。
- `mp3-mono.mp3`：44.1 kHz 单声道 MP3，保留 Xing gapless 信息；去除编码 delay/padding 后为
  26460 个 sample，不以 AVI 的 padding 规则解释这个裸流。
- `mpeg2-mp2.ts`、`mpeg1-mp2.mpg`：分别为 MPEG-TS/MPEG-2 与 MPEG-PS/MPEG-1，
  320×240、25 fps、三帧渐进式 I/B/P 视频，MP2 单声道 48 kHz。视频 PTS 为
  2000000/2040000/2080000 µs，duration 为 40000 µs，保留非零起点，不归零；
  MP2 为五块各 1152 个 sample，PTS 从 1989978 µs 开始每块增加 24000 µs。
  MPEG-1 的末帧在 decoder drain 时缺少 PTS，DLL 从前帧已知 PTS/时长推导并设置
  `PMEDIA_FRAME_PTS_INFERRED`，seek 重播仍须得到同一结果。
- `mpeg2-interlaced.ts`、`mpeg2-oversize.ts`：隔行与 656×480 MPEG-2 拒绝夹具。
- `mpeg4-simple.mp4`：320×240、5 fps、三帧 MPEG-4 Part 2 Simple Profile I/P/P；
  `mpeg4-asp-vga.mp4`：640×480、5 fps、三帧 Advanced Simple Profile I/B/P。
  两者为渐进式有限范围 I420，PTS 为 0/200000/400000 µs，每帧 duration 为 200000 µs。
  `mpeg4-mp3.avi` 为 320×240 Simple Profile 与 MP3 双声道 48 kHz；AVI 的空视频槽使
  PTS 为 0/400000/600000 µs，MP3 为 26 块各 1152 sample，每声道共 29952 sample，
  音频 PTS 从零起每块增加 24000 µs。`mpeg4-interlaced.mp4` 与 `mpeg4-oversize.mp4`
  分别用于隔行和 656×480 拒绝边界。TEST1344 已通过 WM6 Debug/Release 设备门：
  实际 I420 像素/flags、PCM、PTS/时长、三 session 各两次完整解码与 seek 零重播校验值一致；
  7-byte 短读、不可 seek AUTO、显式暂停、音视频 STOP/负 callback、EOF 一次性、停止态、
  截断头失败不改 probe 和应用尺寸上限均有断言。隔行/超限输入在 probe/open 拒绝且无输出。
  ASP 的短红色视频仍不覆盖 Qpel、GMC、全部复杂编码工具或实时播放性能。
- `h263-cif.avi`：H.263 CIF 352×288；`h263p-vga.avi`：H.263+ 自定义 VGA 640×480。
  两者无音频、5 fps、三帧渐进式 I/P/P、有限范围 I420，PTS 为 0/200000/400000 µs，
  每帧 duration 为 200000 µs。TEST1347 已通过 WM6 Debug/Release：plane/stride/像素取样、
  flags、时间戳、三 session 各两次解码与 seek 零校验值一致；7-byte 短读、不可 seek AUTO、
  SOFT、暂停/视频 STOP/恢复、负 callback、EOF 一次性、停止态和关闭。
  打开后 source 设为 I/O 错误，重播不得再读取宿主。`h263-oversize.avi` 为标准 4CIF
  704×576，扩大 options 也须拒绝；较小应用尺寸和截断 AVI 头亦在输出前拒绝。
  不覆盖 H.263 裸流、3GP、复杂编码工具、音视频组合或非零 seek。
- `flv-h264-aac.flv`、`flv-main-vga.flv`：分别从固定 `baseline-aac.mp4`、`main-vga.mp4`
  直接 remux，不重编码。三帧有限范围 I420 的 PTS 分别为 21000/221000/421000 µs
  和 400000/600000/800000 µs，每帧时长 200000 µs。前者 AAC-LC stereo/48 kHz
  保留 priming，共 30×1024 sample；PTS 保留 FLV 毫秒量化（0/21/42/64…618 ms），
  不按 MP4 的负 PTS 裁剪规则去掉首包。TEST1348 双配置 WM6 门覆盖像素取样/flags/stride、
  PCM/时间戳、各三 session 三遍解码与 seek 零重播，后两遍全部 PCM 字节与首次相同。
  7-byte 短读、不可 seek AUTO/SOFT、暂停、音频或视频 STOP/负 callback、EOF/停止态/关闭
  均有断言；打开后 source 设为 I/O 错误，重播不得再次读取宿主。
- `flv-oversize.flv`：从固定 `oversize.mp4` remux，656×480 必须返回 LIMIT。
  `flv1-unsupported.flv`：独立生成的 Sorenson H.263（FLV1），桌面可解码但不在产品
  decoder 集合，必须返回 UNSUPPORTED。两者及截断 FLV 头在输出前拒绝，失败 probe
  不改输出，open 不留下 session。FLV 内 MP3、损坏 tag/payload、非零 seek 和 RTMP/同步
  不在本合同中。
- `amr-nb.amr`：AMR-NB 12.2 kbit/s、单声道 8 kHz，七块各 160 个 sample。
- `amr-wb.amr`：AMR-WB 23.85 kbit/s、单声道 16 kHz，六块各 320 个 sample。
  两者关闭 DTX，PTS 从零起每块增加 20000 µs，duration 为 20000 µs；NB 保留编码器
  的尾部 padding，不能按原始 0.12 秒纯音裁成六块。PCM 断言检查样本数、范围、幅度总和
  及跳过前两块后的过零次数，不要求不同版本浮点 decoder 逐字节相同；同一 session
  EOF 后 seek 到零的 PCM 校验值须与首次解码一致，不能保留旧预测/合成历史。
- `ima-mono.wav`、`ima-stereo.wav`：4-bit WAV IMA ADPCM、8 kHz、512-byte 块。
  mono 每块 1017 sample、三块；stereo 每块每声道 505 sample、五块，左声道为 440 Hz/0.125
  幅度，右声道为 880 Hz/0.0625 幅度，避免相同声道掩盖四字节组交错错误。
  两者 `fact` 均声明每声道 2400 sample，因此 DLL 裁剪尾部 padding，duration 为 300000 µs。
  `ima-mono.pcm`、`ima-stereo.pcm` 由同一 pinned 桌面 FFmpeg 独立解码为 S16LE，保留完整
  编码块的 3051/2525 sample；设备断言逐字节比较前 2400 sample。移除 `fact` 时比较整个参考，
  块内/块边界 seek 则比较对应 sample 开始的后缀，不在宿主实现参考 decoder。
  另以 metadata mutation 验证非法步进索引、保留字节、fmt/fact/RIFF 边界、后置 fact、部分块
  和超大块拒绝；合法 2041-sample 静音块验证固定输出容量不截断。

Annex-B 文件组 `annexb-{baseline-aac,main-vga,high,high422,interlaced,oversize}.h264`
由对应固定 MP4 仅复制视频并使用 h264_mp4toannexb 转换，不重新编码、不含音频；
有起始码、SPS/PPS/IDR。TEST1349 接受前两条：三帧 I420、关键帧/范围/stride/像素取样、
未知 PTS=-1 且不设推导标记、SPS 名义帧间隔 200000 µs；三 session 各三遍取样校验值一致。
缓存零点重播不得重读 source，正值 seek 拒绝且不改变输出；短读、不可 seek AUTO/SOFT、
暂停/STOP/负 callback、EOF/停止态/关闭均保护。其余四条验证 High/4:2:2/隔行拒绝及
超 VGA LIMIT，截断头验证 FORMAT 与 probe 输出不变。
`annexb-no-timing.h264` 对 pinned Baseline SPS 移除 timing 字段（trace_headers 的
NAL/RBSP bit 75，删除其后 65 bit 并重建尾部对齐/防竞争字节），不改 slice 数据；
不是通用 SPS 编辑器。设备三帧 duration 必须为零，PTS 仍为 -1。
`annexb-repeat-sps.h264` 复制拼接同一裸流，六帧、两个关键帧、不变 SPS 可解码；
`annexb-change-{no-timing,main-vga,high,high422,interlaced,oversize}.h264` 则先放有效
Baseline，再拼接另一 SPS 的三帧片段。桌面六帧可解码，但产品必须在 probe/open 拒绝，
不能先输出好片段再失败；oversize 返回 LIMIT，其余 UNSUPPORTED，probe 输出保持不变。
无 timing/重复 SPS 的三 session 各三遍输出与缓存重播、暂停/STOP/负 callback 也有断言。
损坏组从固定 Baseline 离线变换：`annexb-bad-sps.h264` 只留 SPS NAL 前四字节；
`annexb-bad-pps.h264` 将 PPS 替为 `68 80`；`missing-pps`/`orphan-pps` 分别清空
PPS/SPS NAL 内容（保留起始码）。`late-bad-sps`/`late-bad-pps` 在有效三帧后追加
上述损坏参数集。六条均须在 probe/open 返回 FORMAT、probe 不变、session 为 NULL、无输出。
`annexb-cut-idr.h264` 在首个 IDR NAL 的第 12 字节结束；`annexb-cut-p.h264` 在末尾
P NAL 的第 4 字节结束。两者参数集仍有效，probe/open 成功；各三 session 验证 pump
FORMAT，分别零帧/前两帧、无正常 EOF、错误回调/事件各一次，错误后的 pump/resume/seek
STATE 且不再回调、close 成功。打开后 source 禁止重读。`annexb-two-frames.h264`
则在最后一个 P NAL 起始码之前结束，是合法两帧控制：正常 EOF，三 session 各三遍
缓存重播及暂停/STOP/负 callback/停止态断言保持。没有外部预期长度时，合法边界结束
无法被当作截断错误。

参数/引用组仍只变换 pinned Baseline：`repeat-pps` 在原 PPS 后重复同一 NAL；
`change-pps` 拼接第二段三帧，只翻转 PPS 的 constrained_intra_pred_flag（NAL bit 36）。
桌面六帧可解码，但 DLL 对不同有效 PPS 返回 UNSUPPORTED、无输出，不承诺动态重配置。
`pps-id1` 将 PPS ID 和各 slice 引用从 0 改为 1（ue 字段由 1 改为 010，重建尾部对齐与
防竞争字节）；三个帧仍有效，和 `repeat-pps` 一样保护三 session 各三遍缓存重播及生命周期。
`dangling-pps` 只改 slice 引用，`late-dangling-pps` 在有效三帧后拼接该片段；
`pps-missing-sps` 只把 PPS 的 SPS 引用改为 1；`slice-before-pps` 将 PPS 移到首个 IDR
之后。这四条返回 FORMAT、probe 不变、open 无 session/输出。
`sps-4096/4097`、`pps-4096/4097` 在有效三帧后拼接带越界 SPS ID=32/PPS ID=256
的容量 NAL；`late-pps-4097` 则只追加越界 PPS。NAL 头仍正常，PPS 引用已有 SPS 0，
用于进入固定 raw probe 后的预算检查。4096 字节返回 FORMAT，4097 字节返回 LIMIT，
均在输出前拒绝；这些是容量优先级负控制，不证明最大尺寸合法参数集可以解码。
VFR、SEI、其他编码工具与损坏模式仍未验收。

TEST1352 复用 `mpeg4-simple.mp4`、`main-vga.mp4`、`h263-cif.avi` 和
`flv-main-vga.flv`。SOFT + SCHEDULE_VIDEO 的合成宿主时钟断言检查
首帧对齐、截止前 1 µs 保留帧/到期输出、源 PTS/像素/stride、负/回退/同值时钟与非法预算，
显式暂停及 STOP 后恢复、幂等 resume、暂停中 seek 零丢弃待输出帧、EOF 一次性、极大
时钟不溢出/不丢帧和停止/负 callback 终态/关闭。四条各使用可 seek/不可 seek source；
打开后禁止读源，缓存重播必须仍成功。非法 flags/backend/缺视频 callback、WAV、
`baseline-aac.mp4` 混合流、Annex-B、ADTS 和 PS 均验证拒绝。
这是公共 DLL 的到期回调合同，不是宿主自建调度器；无实际睡眠，不证明实时帧率、
音视频同步、总内存或重建分配失败。

TEST1355 用 Baseline/AAC 与 Main/B 帧/AAC 比较默认解码和混合源时钟模式：每帧所有有效
I420 字节参与校验值、全部 PCM 字节逐个比较，源 PTS/时长/flags、两路到期合并顺序与
同值音频优先均保护。两种 source seekability、7-byte 短读、打开后禁读源、每 pump
最多一次回调、提前 1 µs、负/回退/同值/极大时钟、背压同块重试/阻止另一流、两路 STOP、
暂停间隔、待帧/PCM 的缓存 seek 零与停止、视频先 EOF 的音频尾部、一次性 EOF/错误和
关闭查询均有断言。有效超大首包要求 LIMIT、零输出及失败终态；组合 flags、缺 callback、
AUTO、WAV/纯流/AVI 拒绝也覆盖；设备 PCM 参考不取自桌面浮点 decoder。

TEST1356 使用三条 mixed-clock 控制，先验证默认解码的精确源 PTS、视频内容与原 Baseline
PCM 的全部字节相同，再比较混合调度两遍输出。两种 source seekability、7-byte 短读、
禁读源的待输出/EOF 后缓存重播、提前 1 µs/到期/极大时钟、暂停排除间隔、首回调与同值
音频优先、两种 EOF 尾部顺序及 EOF 一次性均有断言。异常时间轴/格式变化、其他 edit-list
形状、内存故障与真机同步仍待独立门，不把这三个短输入当成任意 MP4 时间轴的一致性证明。

时间戳控制组 `flv-clock-{base,unknown,late-unknown,backward,negative,gap,duplicate}.flv`
从固定 `flv-h264-aac.flv` 删除音频/script tag 并设置 video-only 标志，保留所有 AVC
配置和三个 I/P/P payload，只变换视频包的 FLV DTS/有符号 CTS。每条 1052 字节，旧 pin 不变。
base 的 DTS/PTS 为 21/221/421 ms、CTS 为零；unknown 对三个包设置 CTS=900001 ms，
固定 demux 会把 PTS/DTS 标为未知；late-unknown 只改后两个包。此阈值来自
[FFmpeg 3.4 FLV 解析器](https://ffmpeg.org/doxygen/3.4/libavformat_2flvdec_8c_source.html)，
不改 AVCC/slice，不把它当作完整坏 FLV 验收。negative 首包 CTS=-22 ms，PTS=-1 ms，
这是已知负值而非未知 sentinel；backward 将第二包 DTS/PTS 改为零；gap 为 21/621/821 ms，
duplicate 为 21/21/421 ms。

TEST1352 各使用可 seek/不可 seek source、7-byte 短读：先验证默认解码的三帧/像素/PTS，
再各创建三个调度 session。unknown/negative 零帧 FORMAT，backward 保留首帧后 FORMAT；
必须匹配具体时钟守卫错误文字、错误回调/事件一次、无正常 EOF、后续状态守卫及 stop/close。
调度禁用 demux 隐式补零；默认 unknown 控制仍从零起，后两帧为 200/400 ms 并带推导标记。
late-unknown 的后两帧保持 221/421 ms、各设 PTS_INFERRED；名义时长为 200 ms。
base/gap/duplicate 的显式 PTS 不重写，截止前 1 µs 不交付、到期交付，重复 PTS 每次 pump
一帧；EOF 和暂停中缓存 seek 零重置也有断言，打开后禁止再读 source。
这不是完整 VFR/SEI、无 timing/零时长/推导溢出或其他容器异常的一致性门。

验证固定输入：`python scripts/media_fixtures.py`。
只有有意更新整个夹具 pin 时运行 `python scripts/media_fixtures.py --generate --ffmpeg PATH`；
生成依赖 libx264、libmp3lame、libopencore_amrnb、libvo_amrwbenc 和原生 AAC/MJPEG/MPEG-1/MPEG-2/MPEG-4 Part 2/H.263/H.263+/MP2/IMA ADPCM 编码器，但它们不进入 WM6 产品。`--extend`
只用于首次用同一 pinned 生成器补齐 MJPEG/MP3 文件，先验证已有 pin，不重生成旧文件；已经
补齐后再次调用会拒绝。`--extend-mpeg`、`--extend-amr`、`--extend-ima`、`--extend-mpeg4` 与 `--extend-h263` 分别对 MPEG、AMR、IMA WAV/PCM、MPEG-4 Part 2、H.263 文件组采用相同规则；
`--extend-flv` 对 FLV 文件组采用相同 pin 规则：三条 remux、一条 FLV1 负夹具编码；
FLV1 编码器只用于桌面生成，不进入产品。`--extend-annexb` 对六条裸流采用相同 pin
规则，只转换已有固定 MP4，不改旧记录。`--extend-annexb-sequence` 对八条 timing/拼接
夹具采用相同 pin 规则，离线按固定字节变换生成，不调用编码器、不改原文件。
`--extend-annexb-damage` 对九条损坏/完整边界控制输入采用同一规则；派生不重新编码。
`--extend-annexb-parameters` 对 PPS/引用/容量组采用同一规则，不改已有 pin。
`--extend-flv-clock` 对七条时间戳控制采用相同规则，只做固定字节变换，不重编码。
`--extend-audio-change` 保留旧 pin，生成两条 LC 纯音并拼接六条完整片段控制，不修改旧媒体。
`--extend-mixed` 只增加 Main/AAC remux；`--extend-mixed-limit` 只增加首包 filler 负控制，
`--extend-mixed-clock` 只增加上述三条固定 empty-edit 控制；均保留旧 pin，不在构建时执行。
扩展操作不能同时选择。AMR 编码器的采样率/单声道边界见
[FFmpeg codec 文档](https://ffmpeg.org/ffmpeg-codecs.html#libopencore_002damrnb-1)。
WAV IMA 块/声道布局与 `fact` 定义见微软原始
[Multimedia Data Standards Update（RIFF/WAVE）](https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/Docs/RIFFNEW.pdf)。
其他工具版本可能生成不同字节，
必须重新审查 manifest、profile 和设备断言，不能只替换哈希让失败通过。

MPEG-4 Part 2 的独立桌面校验：
`python scripts/test_media_mpeg4_fixtures.py --ffmpeg PATH`。
PATH 必须是 manifest 固定的生成器；脚本只读，单线程、Windows 下以低优先级运行工具，
检查三帧全部有效像素、profile、B 帧顺序、PTS/duration、隔行属性及 AVI MP3 样本数。
不传 PATH 时仅验证固定输入哈希，不运行解码；两种模式都不能替代 WM6 设备门。

H.263 的独立桌面校验：`python scripts/test_media_h263_fixtures.py --ffmpeg PATH`。
必须显式指定 manifest 固定生成器；只读、单线程、Windows 低优先级，检查三个文件的
全部有效像素、I/P/P、尺寸、PTS/时长和渐进属性。桌面可解码超限文件只证明夹具有效，
WM6 必须拒绝它；设备断言采用像素取样，桌面全像素检查不能替代设备门。

时钟控制的独立校验：`python scripts/test_media_clock_fixtures.py --ffmpeg PATH`。
只读、同一 pinned 工具、单线程低优先级：验证精确 DTS/CTS、tag 边界、除时间戳外的全部
AVC tag/payload 与源相同，独立解码三帧全部有效 I420 像素。桌面 CLI 可以自动生成 PTS，
因此其帧 PTS 不作为 WM6 未知/推导时间戳或调度参考；必须另过公共 DLL 设备门。

FLV 的独立桌面校验：`python scripts/test_media_flv_fixtures.py --ffmpeg PATH`。
必须显式指定固定生成器；只读、单线程、Windows 低优先级，检查四条输入的全部有效像素、
帧序/尺寸/源时间轴、AAC 样本数及 tag 边界/毫秒 PTS。FLV1 桌面帧时长 metadata 为零，
只证明负夹具有效，不提供 WM6 输出承诺；桌面 PCM 不作为 ARMV4I 逐字节参考。

Annex-B 的独立桌面校验：`python scripts/test_media_annexb_fixtures.py --ffmpeg PATH`。
必须显式指定固定生成器；只读、单线程、Windows 低优先级，检查原六条和无 timing/重复
SPS 输入的全部有效像素、尺寸/帧序/隔行属性、NAL 类型及 demux packet PTS/DTS 均未知；
trace_headers 独立确认 timing 字段已移除，六条 SPS 变化输入各可解出前后三帧并含不同 SPS。
变化输入只检查帧属性，不把桌面动态重配置当作产品支持。损坏组由桌面严格 decoder
（bitstream/buffer/explode、xerror）独立确认八条损坏输入非零退出，两帧控制正常解码；
桌面非零退出不规定 WM6 的 probe/pump 分类或错误前已输出帧数，也不是穷尽损坏检测。
参数组检查重复 PPS/ID1 的三帧与有效 PPS 变化的六帧全部像素，trace_headers 核对 ID/flag；
缺失参数引用由严格 decoder 拒绝，容量负控制独立检查精确 NAL 字节数与越界 ID/SPS 引用，
前向 PPS 控制独立检查 NAL 顺序。桌面能力不替代 DLL 的保守拒绝与生命周期门。
CLI 自动生成的呈现时间戳
不能替代 DLL 的未知 PTS 合同，桌面全像素检查也不能替代 WM6 取样/重播/拒绝设备门。

AAC 格式变化的独立校验：`python scripts/test_media_audio_change_fixtures.py --ffmpeg PATH`。
只读、同一 pinned 工具、单线程低优先级；逐个验证 ADTS 帧边界/LC/采样率/声道和独立解码
后的帧形状、精确拼接关系。它不把桌面 PCM 当作 ARMV4I 字节参考，也不证明 DLL 应支持
动态时钟切换；FORMAT 分类、错误前 PCM 和生命周期必须通过设备门。

混合流的独立校验：`python scripts/test_media_av_fixtures.py --ffmpeg PATH`。
同一 pinned 工具、只读、单线程低优先级，检查三条输入的所有有效 I420 像素、I/P/B 顺序、
视频 PTS、实际 AAC sample PTS/裁剪/块数与不变 PCM，以及首包容量正负控制。
它不证明 WM6 调度、输出设备同步或性能。

混合起点的独立校验：`python scripts/test_media_av_clock_fixtures.py --ffmpeg PATH`。
同一 pinned 工具、只读、单线程低优先级，独立检查完整 mdat/sample tables 与原 edit
保持关系，以 `-copyts` 保留源时间戳，验证三帧全部有效 I420 字节、29 块全部 PCM 字节
与原输入相同及精确音视频起点。桌面 PCM 只在同一工具内比较；WM6 仍须通过 TEST1356，
不能用桌面结果替代 DLL 调度、AAC 裁剪或生命周期断言。

AAC 的独立桌面对照：`python scripts/test_media_aac_seek_evidence.py --ffmpeg PATH`。
PATH 必须匹配 manifest 的工具 SHA-256；脚本只读、单线程，Windows 下使用低优先级，
比较新开解码与 CLI seek 零的包 PTS、PCM 和逐块差异。CLI 创建新 decoder，不能替代
WM6 同一 session 的 seek 断言；桌面浮点 PCM 不作为 ARMV4I 的逐字节参考。

断言检查实际 I420 plane/stride/像素、视频时间戳与持续时间、S16LE 内容、音频时间戳、EOF
和 seek 重播。红色 MJPEG 的全范围 Y/U/V 期望为 76/85/255，H.264 夹具的有限范围为
81/90/240，MPEG-1/2、MPEG-4 Part 2 与 H.263 也为该有限范围值；同时核对公开 FULL_RANGE 与 PTS_INFERRED 标记，
不通过改像素或忽略 flags 掩盖范围或时间戳错误。
这些是短媒体解码合同，不是实时播放、复杂画面质量、underrun 或 CPU 性能门。

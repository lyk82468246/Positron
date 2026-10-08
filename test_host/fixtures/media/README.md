# 压缩媒体断言夹具

本目录只用于 `test_host`。纯红画面与 440/880 Hz 纯音由
[`scripts/media_fixtures.py`](../../../scripts/media_fixtures.py) 离线生成，无下载的第三方媒体内容。
生成内容按 CC0-1.0 提供，不授予 AVC/AAC 专利许可；FFmpeg/x264 工具许可证不被重新标记。
工具版本、可执行文件哈希、每条生成参数、文件长度与 SHA-256 固定在 `manifest.json`。
仓库构建只部署固定文件，不调用桌面编码器或联网。

- `baseline-aac.mp4`：320×240、5 fps、三帧 Constrained Baseline/AVCC，AAC-LC 双声道 48 kHz。
- `main-vga.mp4`：640×480、5 fps、三帧 Main，包含 B 帧和 decoder drain。
- `aac-lc.aac`：ADTS AAC-LC 单声道 48 kHz；`aac-main.aac` 用于拒绝非 LC profile。
  MP4 的 AAC priming 包 PTS 为 -1024 sample；设备输出从 PTS 零起为 29 块、每声道
  29696 sample。ADTS 为 30 块、每声道 30720 sample。TEST1332 各完整解码三次，
  在 EOF 后暂停并 seek 到零两次，后两次全部 S16LE 字节与首次输出一致；同时保护
  PTS/时长、样本数和 EOF，不以相同幅度或校验值代替逐字节断言。打开后 source 被设为
  I/O 错误，重播不得再次读取宿主。非零 AAC seek 不在此合同内。
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
  不覆盖 H.263 裸流、3GP、FLV/Sorenson H.263、复杂编码工具、音视频组合或非零 seek。
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

验证固定输入：`python scripts/media_fixtures.py`。
只有有意更新整个夹具 pin 时运行 `python scripts/media_fixtures.py --generate --ffmpeg PATH`；
生成依赖 libx264、libmp3lame、libopencore_amrnb、libvo_amrwbenc 和原生 AAC/MJPEG/MPEG-1/MPEG-2/MPEG-4 Part 2/H.263/H.263+/MP2/IMA ADPCM 编码器，但它们不进入 WM6 产品。`--extend`
只用于首次用同一 pinned 生成器补齐 MJPEG/MP3 文件，先验证已有 pin，不重生成旧文件；已经
补齐后再次调用会拒绝。`--extend-mpeg`、`--extend-amr`、`--extend-ima`、`--extend-mpeg4` 与 `--extend-h263` 分别对 MPEG、AMR、IMA WAV/PCM、MPEG-4 Part 2、H.263 文件组采用相同规则；
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

AAC 的独立桌面对照：`python scripts/test_media_aac_seek_evidence.py --ffmpeg PATH`。
PATH 必须匹配 manifest 的工具 SHA-256；脚本只读、单线程，Windows 下使用低优先级，
比较新开解码与 CLI seek 零的包 PTS、PCM 和逐块差异。CLI 创建新 decoder，不能替代
WM6 同一 session 的 seek 断言；桌面浮点 PCM 不作为 ARMV4I 的逐字节参考。

断言检查实际 I420 plane/stride/像素、视频时间戳与持续时间、S16LE 内容、音频时间戳、EOF
和 seek 重播。红色 MJPEG 的全范围 Y/U/V 期望为 76/85/255，H.264 夹具的有限范围为
81/90/240，MPEG-1/2、MPEG-4 Part 2 与 H.263 也为该有限范围值；同时核对公开 FULL_RANGE 与 PTS_INFERRED 标记，
不通过改像素或忽略 flags 掩盖范围或时间戳错误。
这些是短媒体解码合同，不是实时播放、复杂画面质量、underrun 或 CPU 性能门。

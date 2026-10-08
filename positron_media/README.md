# positron_media.dll 使用说明

positron_media.dll 是面向 Windows Mobile 6 / Windows CE 5.2 ARMV4I 的解码与播放
边界。第三方应用只应包含 positron_media.h，通过稳定的 C ABI 使用 opaque session；
DirectShow、ACM、WaveOut、FFmpeg 的类型和生命周期都不暴露到公共头文件。

本文是第三方应用的调用合同。能力矩阵见
[docs/CAPABILITIES.md](../docs/CAPABILITIES.md)，总体所有权规则见
[docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)。

## 当前首版能做什么

- 宿主通过同步 read/seek/tell/size 回调提供媒体字节；DLL 不访问 URL、HTTP、文件系统，
  也不创建长期工作线程。
- AUTO 对设备可接受的 WAV PCM 先尝试 WM6 WaveOut；失败后回退到软解。
- FFmpeg 3.4.14 的固定 ARMV4I 归档用于有界的容器和 codec 软解，输出统一为 I420 视频
  帧和交错 S16LE 音频块。
- pm_pump(clock_us, budget_us) 由宿主反复调用，驱动解码、输出回调和 WaveOut 队列。
- 输入会在打开期间复制到 DLL 自己的内存，当前总上限为 16 MiB。软解因此不要求源可 seek，
  但打开期间遇到 PMEDIA_WOULD_BLOCK 会直接失败，不会在 DLL 内等待或稍后重试。

当前只承诺 decoder/playback，不承诺编码、DRM、字幕、直播协议、AV1、HEVC/H.265、VP9，
或高于 640×480 的软件视频实时播放。DirectShow 目前只做 WM6 graph 创建性探测；公共
PMEDIA_BACKEND_NATIVE 路径现在只落实 WAV PCM WaveOut，不能把桌面 DirectShow 格式
列表当成设备能力保证。

## 给第三方应用的集成方式

### 需要部署的文件

1. 包含仓库中的 positron_media/positron_media.h。
2. 在应用工程中链接与目标配置匹配的 positron_media.lib。
3. 将同一 Debug/Release、同一 ARMV4I 配置生成的 positron_media.dll 部署到设备，
   使系统能够按正常 DLL 搜索规则加载它。
4. 不要让第三方应用直接链接 FFmpeg 的头文件或库；FFmpeg 已由媒体 DLL 按固定版本静态
   集成。FFmpeg 源码、移植边界和许可证见
   [third_party/ffmpeg-3.4.14/POSITRON_PORT.md](../third_party/ffmpeg-3.4.14/POSITRON_PORT.md)、
   [THIRD_PARTY.md](../THIRD_PARTY.md)。

应用必须使用与目标设备相同的 Windows Mobile SDK、ARMV4I 和 C ABI 配置。桌面 x86 DLL、
当前桌面 FFmpeg 或其他编译器生成的 import library 不能混用。test_host.exe 只是回归宿主
和调用示例，不是第三方应用必须依赖的运行时。

### 所有权、线程和回调规则

- pm_source_callbacks.context、pm_output_callbacks.context 以及它们指向的内存由应用
  持有；应用必须保持它们有效，直到对应 session 已经 pm_close。
- 成功的 pm_open 返回一个由应用拥有的 session；每个成功 session 只能调用一次
  pm_close。pm_close 返回后不能再使用该 handle。
- pm_video_frame、pm_audio_block 以及它们指向的 plane/data 都是借用数据，只在当前
  回调返回前有效。应用若要异步绘制或播放，必须在回调中复制到自己的缓冲区。
- 回调是同步调用。不要在回调中调用同一 session 的 pm_pump、pm_close 或其他会重入
  该 session 的入口；不要从另一个线程同时驱动同一 session。
- DLL 不替应用保存 source/output context 的所有权，也不会在 pm_close 返回后继续回调。
  关闭前应先停止应用自己的泵循环，并确保没有并发回调。
- 音频和视频回调都应尽快返回。需要排队时使用应用自己的有界 ring buffer；回调中不要
  执行网络、磁盘等待或无界分配。
- pm_open_options.flags 和 native_window 在当前首版保留，必须置零或传 NULL；
  当前没有可由第三方接线的 native 视频窗口生命周期。

## 标准调用顺序

典型生命周期如下：

1. 调用 pm_abi_version()，确认 ABI 主版本与应用编译时使用的头文件一致。
2. 初始化 pm_source_callbacks。如果需要先探测再打开，最好提供可恢复的 seek/tell；
   对不可 seek 的源，使用一次新的 source context 做探测和打开，不能让 pm_probe 消耗
   同一个只能向前读的 context 后再直接 pm_open。
3. 可选地调用 pm_probe()，读取容器、音视频流、codec、分辨率和能力 mask。
4. 初始化 pm_output_callbacks 和 pm_open_options，调用 pm_open()。
5. 成功打开后用 pm_get_stream_info()、pm_get_capabilities() 和 pm_get_backend()
   记录实际选择的路径。
6. 在应用自己的消息循环或定时器中反复调用 pm_pump()。clock_us 应是单调时钟的
   微秒值；当前实现尚未使用该值做播放调度。budget_us 只控制每次处理量，不是严格墙钟
   时间上限，不能据此认为 DLL 已实现音视频同步或实时播放。
7. 用户暂停/恢复/停止/跳转时调用相应的 pm_pause()、pm_resume()、pm_stop()、
   pm_seek()。
8. 播放结束或应用退出时调用 pm_close()；发生错误时也必须释放已经成功打开的 session。

## 最小 C89 调用示例

下面的例子使用内存中的文件作为 source，展示一个第三方应用需要实现的最小接线。
app_monotonic_us() 和 app_sleep_ms() 是应用自己的 WM6 时钟/消息泵函数，不属于媒体 DLL。
真实应用在 SOFT backend 下把 media_audio() 的数据复制到 WaveOut 或自己的音频队列；
NATIVE 已由 DLL 播放，不应再次输出音频。media_video() 的 I420 plane 由应用转换或绘制
到自己的窗口。下面的 callback 只观察数据，不包含音视频同步或窗口/音频队列实现。

~~~c
#include <string.h>
#include "positron_media.h"

typedef struct media_memory_source {
    const unsigned char *data;
    int bytes;
    int position;
} media_memory_source;

typedef struct media_sink {
    int audio_blocks;
    int video_frames;
} media_sink;

static int media_read(void *context, unsigned char *destination,
                      int capacity, int *out_read)
{
    media_memory_source *source;
    int remaining;
    int count;

    if (out_read == NULL || capacity < 0) return PMEDIA_ERROR_ARGUMENT;
    *out_read = 0;
    source = (media_memory_source *)context;
    if (source == NULL || source->data == NULL || source->bytes < 0) {
        return PMEDIA_ERROR_IO;
    }
    if (source->position >= source->bytes) return PMEDIA_EOF;
    remaining = source->bytes - source->position;
    count = remaining < capacity ? remaining : capacity;
    if (count > 0) {
        memcpy(destination, source->data + source->position, (size_t)count);
        source->position += count;
    }
    *out_read = count;
    return source->position >= source->bytes ? PMEDIA_EOF : PMEDIA_OK;
}

static int media_seek(void *context, pm_position offset, int origin,
                      pm_position *out_position)
{
    media_memory_source *source;
    pm_position base;
    pm_position target;

    source = (media_memory_source *)context;
    if (source == NULL || out_position == NULL) return PMEDIA_ERROR_ARGUMENT;
    if (origin == PMEDIA_SEEK_SET) base = 0;
    else if (origin == PMEDIA_SEEK_CUR) base = source->position;
    else if (origin == PMEDIA_SEEK_END) base = source->bytes;
    else return PMEDIA_ERROR_ARGUMENT;
    target = base + offset;
    if (target < 0 || target > source->bytes) return PMEDIA_ERROR_IO;
    source->position = (int)target;
    *out_position = target;
    return PMEDIA_OK;
}

static int media_tell(void *context, pm_position *out_position)
{
    media_memory_source *source;
    source = (media_memory_source *)context;
    if (source == NULL || out_position == NULL) return PMEDIA_ERROR_ARGUMENT;
    *out_position = source->position;
    return PMEDIA_OK;
}

static int media_size(void *context, pm_position *out_size)
{
    media_memory_source *source;
    source = (media_memory_source *)context;
    if (source == NULL || out_size == NULL) return PMEDIA_ERROR_ARGUMENT;
    *out_size = source->bytes;
    return PMEDIA_OK;
}

static int media_audio(void *context, const pm_audio_block *block)
{
    media_sink *sink;
    sink = (media_sink *)context;
    if (sink == NULL || block == NULL || block->data == NULL ||
        block->bytes <= 0) {
        return -1;
    }
    /* 在这里复制 block->data；回调返回后不能继续使用它。 */
    sink->audio_blocks++;
    return PMEDIA_OK;
}

static int media_video(void *context, const pm_video_frame *frame)
{
    media_sink *sink;
    sink = (media_sink *)context;
    if (sink == NULL || frame == NULL || frame->plane[0] == NULL) {
        return -1;
    }
    /* I420 的三个 plane 和 stride 只在本回调中有效。 */
    sink->video_frames++;
    return PMEDIA_OK;
}

static void media_event(void *context, int event, int value)
{
    (void)context;
    (void)event;
    (void)value;
}

static void media_error(void *context, int error, const char *message)
{
    (void)context;
    (void)error;
    (void)message;
    /* 应用应记录 error/message，但不要在此回调中重入 session。 */
}

/* 由应用实现；这里仅表示调用点。 */
extern pm_position app_monotonic_us(void);
extern void app_sleep_ms(int milliseconds);

int play_memory_media(const unsigned char *data, int bytes)
{
    media_memory_source source_state;
    media_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_session session;
    pm_position clock_us;
    int result;

    if (data == NULL || bytes <= 0) return PMEDIA_ERROR_ARGUMENT;
    memset(&source_state, 0, sizeof(source_state));
    source_state.data = data;
    source_state.bytes = bytes;
    memset(&sink, 0, sizeof(sink));

    memset(&source, 0, sizeof(source));
    source.size = sizeof(source);
    source.context = &source_state;
    source.read = media_read;
    source.seek = media_seek;
    source.tell = media_tell;
    source.size_callback = media_size;

    memset(&probe, 0, sizeof(probe));
    probe.size = sizeof(probe);
    result = pm_probe(&source, &probe);
    if (result != PMEDIA_OK) return result;

    memset(&output, 0, sizeof(output));
    output.size = sizeof(output);
    output.context = &sink;
    output.video = media_video;
    output.audio = media_audio;
    output.event = media_event;
    output.error = media_error;

    memset(&options, 0, sizeof(options));
    options.size = sizeof(options);
    options.backend = PMEDIA_BACKEND_AUTO;
    options.max_video_width = 640;
    options.max_video_height = 480;

    session = NULL;
    result = pm_open(&source, &options, &output, &session);
    if (result != PMEDIA_OK) return result;

    /* 这里可读取实际 stream/backend；不要根据 probe 猜测 backend。 */
    pm_get_stream_info(session, &probe.stream);
    pm_get_capabilities(session, &probe.capabilities);
    pm_get_backend(session);

    for (;;) {
        clock_us = app_monotonic_us();
        result = pm_pump(session, clock_us, 2000);
        if (result == PMEDIA_EOF) {
            result = PMEDIA_OK;
            break;
        }
        if (result < 0) break;
        app_sleep_ms(1);
    }

    if (result != PMEDIA_OK) {
        /* pm_last_error(session) 返回 DLL 内部借用字符串，不要释放。 */
        (void)pm_last_error(session);
    }
    pm_close(session);
    return result;
}
~~~

示例中的 pm_probe() 与 pm_open() 共用一个可 seek 的内存 source，因此探测后会回到
原位置。网络、文件或管道 source 应由应用自己实现读缓存、错误和取消策略；DLL 不会把
PMEDIA_WOULD_BLOCK 转换成异步任务。

## Source 回调合同

### read

read 每次都必须设置 out_read。返回 PMEDIA_OK 表示还可以继续读取，返回
PMEDIA_EOF 表示本次数据已经是最后一块；最后一块可以同时返回正的 out_read 和
PMEDIA_EOF。返回 PMEDIA_WOULD_BLOCK 只适合表达当前不可立即读取，但当前实现是在
pm_probe()/pm_open() 的同步装载阶段直接返回该错误，不会保存请求等待下次 pm_pump()。
其他失败应返回负错误码或让 DLL 将其归类为 PMEDIA_ERROR_IO。
返回 PMEDIA_OK 且 out_read 为零不等于 EOF：DLL 将其视为 PMEDIA_WOULD_BLOCK，避免把
临时无进展误判为完整媒体。打开失败后不会留下 session；应用重试前须自己恢复或重建 source。

### seek、tell、size

当前软解会先把输入复制到 DLL 内存，因此打开和软解本身不要求 source 可 seek。提供
seek/tell 仍然很重要：pm_probe() 可以恢复原位置，应用也可以复用同一个 source
进行 pm_open()。不可 seek source 应直接从起点调用 pm_open()，不要先用同一 context
调用 pm_probe()。
如果提供 seek，DLL 要求回调确实回到请求的位置；提供了失效的 seek 不能当成未提供 seek
处理。pm_probe() 恢复原位置失败时返回 PMEDIA_ERROR_NOT_SEEKABLE，不改写 out_info；
应用不能假定这时 source 已恢复。

size_callback 在当前实现中不是必需的预读入口；如果应用已经知道长度，仍建议提供，
以便未来 ABI 扩展和诊断使用。所有 source 回调都在应用驱动的同步调用中执行，DLL 不会
把 callback 交给后台线程。

### 输入和错误边界

- DLL 会在打开期间连续读取整个输入，超过 16 MiB 返回 PMEDIA_ERROR_LIMIT。
- 空输入、截断头、损坏 chunk、无法识别的容器或没有可用 decoder 会安全失败。
- source 回调返回非法读取长度或读错误时返回 PMEDIA_ERROR_IO；回到起点失败返回
  PMEDIA_ERROR_NOT_SEEKABLE。这些输入错误不会被后续 codec 尝试覆盖。pm_open() 失败时
  没有可供 pm_last_error() 查询的 session，应在 error callback 中复制错误信息。
- DLL 不保证 pm_probe() 成功就一定能按应用指定的 backend 打开；打开时仍会再次验证
  decoder、WaveOut 格式和视频尺寸。

## Output 回调和播放控制

### 音频

pm_audio_block.data 是交错 S16LE。bytes 是字节数，samples 是每个声道的 sample
数，channels 和 sample_rate 描述当前块，pts_us/duration_us 是微秒时间戳。
应用必须在回调中复制或消费它；不能把这个指针交给稍后运行的线程。

WAV PCM 的 AUTO/NATIVE 可能使用 WaveOut。即使设备实际通过 WaveOut 播放，DLL 仍会
同步调用应用的 audio callback；WaveOut 接受或拒绝的实际格式由设备音频驱动决定。
即使设备播放的是 8-bit unsigned PCM，audio callback 仍收到转换后的 S16LE，而不是设备
原始缓冲格式。NATIVE 播放时该回调用于观察或复制；应用不要再把同一块送往自己的音频设备，
否则会重复播放。需要完全接管音频输出时选择 SOFT。
WAV IMA ADPCM 和 FFmpeg 音频走软解回调。

WAV IMA ADPCM 使用 DLL 内的便携 decoder：只接受 4-bit、mono/stereo、完整编码块和
按每声道四字节组交替排列的 payload；`samples_per_block` 必须与块长度一致，每声道最多
2048 sample（该布局可达到的最大合法值为 2041）。超过容量返回 PMEDIA_ERROR_LIMIT，
部分块、非法步进索引/保留字节、损坏 RIFF/fmt/fact 返回 PMEDIA_ERROR_FORMAT，不通过
FFmpeg 回退绕过守卫。有效 `fact` sample 数裁剪尾部 padding，必须为正且不超过编码容量；
没有 `fact` 时输出所有编码 sample。输出采样率保持源值，不重采样；8 kHz 单/双声道已有
设备断言，其他采样率仍需独立夹具。seek 到块内时先完整解码再丢弃前缀；最后一块或 seek
后的第一块可短于 `samples_per_block`，应用必须使用 callback 的 `samples`/`bytes`/时间戳。

AMR-NB/WB 的已验收裸流子集为单声道 8 kHz/16 kHz，分别每块 160/320 个 sample、
duration 为 20000 µs，输出仍为 S16LE，不在 DLL 内重采样。所有实际编码帧都会输出，
包括编码 padding；没有容器裁剪信息时，不按应用估计的原始录音长度截断尾块。

### 视频

pm_video_frame.format 当前为 PMEDIA_PIXEL_I420。三个 plane 分别是 Y、U、V，具体
行距必须使用 stride[0..2]，不能假设每行紧密排列。视频帧为 8-bit、4:2:0、渐进式；
pts_us 是微秒时间戳，PMEDIA_FRAME_KEY 表示关键帧。帧数据仅在 video callback
返回前有效。duration_us 优先采用 packet duration；缺失时可由容器平均帧率换算，
两者均未知时为零，应用不能把未知时长解释为固定帧率。

裸 H.264 Annex-B 例外：duration_us 取已验证 SPS 的名义两 tick 帧间隔
（2×num_units_in_tick/time_scale，换算为微秒）；缺失有效 timing 时为零，不采用裸流
demux 的默认 25 fps，也不沿用 decoder 的旧帧率。这不是 VFR 或 SEI picture timing 的真实时间轴。
没有 PTS 起点的裸流保持 pts_us=-1，不设置 PTS_INFERRED，不能仅凭时长虚构起点。
当前裸流只接受一个不变的 SPS NAL；忽略起始码长度和尾随零后，字节相同的重复 SPS
可以接受。DLL 在 stream-info 解码前扫描已缓存输入，单 SPS NAL 最多 4096 字节；
缺少可解析 SPS 返回 FORMAT，超过尺寸/预算返回 LIMIT，profile/布局不支持或不同 SPS
返回 UNSUPPORTED。即使新 SPS 单独可解码，改变 timing、尺寸或 SPS ID 也不支持。
失败 probe 不改输出，open 不留下 session 或视频回调；此策略只针对 Annex-B，不宣称
MP4/TS/FLV 的参数变化也已提前检查。PPS/SEI 变化、损坏 payload 仍未充分验收。

FFmpeg 路径保留输入的呈现时间轴，不把 TS/PS 的非零 PTS 起点归零。decoder 没有提供
PTS 时，只有前一输出帧具有已知非负 PTS、正的 duration 且相加不溢出，DLL 才用两者之和
估计当前 PTS，并设置 `PMEDIA_FRAME_PTS_INFERRED`；没有可靠起点时仍返回未知值 `-1`。
显式 PTS 不会被该估计覆盖；成功 seek 会清除推导状态，避免沿用跳转前的时间轴。应用可按位
区分估计时间戳与 decoder 时间戳，这不等于 DLL 已按宿主时钟调度或同步音视频。

绘制前检查 `frame->flags & PMEDIA_FRAME_FULL_RANGE`：置位表示 JPEG/全范围 YUV，
sample 范围为 0..255；例如 MJPEG 的 YUVJ420P 仍使用相同 I420 plane 布局，但不能套用
有限范围的黑白电平转换。未置位表示未报告全范围；有限范围视频的名义 Y 范围为 16..235、
U/V 为 16..240。DLL 不把全范围像素重写成有限范围，也尚未公开色彩矩阵、primaries 或
transfer metadata，不能把该标记当成完整色彩管理。应用应按位读取 flags 并忽略未知位，
不要用 flags 的整体数值判断关键帧。该位是现有字段的追加，不改变结构布局和借用所有权。

软件视频的公开保证上限是 640×480。max_video_width/height 可以用来选择更小的应用
上限；请求更大尺寸仍被 clamp 到 640×480，不会放开产品边界。超限视频返回
PMEDIA_ERROR_LIMIT；H.264 只接受 Baseline（含 Constrained Baseline）/Main，所有 High
profile、非 8-bit/4:2:0 和隔行视频均不支持。可读取的 SPS 在打开前检查，实际帧输出前
再次检查，避免只相信容器 metadata。

MPEG-4 Part 2 在可读取的 extradata 中检查每个 VOL 的有界头前缀，拒绝非矩形、非 4:2:0、
隔行或超过应用/VGA 尺寸上限的声明；不把 FFmpeg 的未知 field_order 当作渐进式证明。
没有 extradata 或码流内参数变化仍由实际帧输出前的守卫约束，不承诺全部输入都能在 probe
时提前识别。此检查不是另一套 decoder；熵解码和后续编码工具仍由固定 FFmpeg 完成。

### callback 返回值

- 返回 PMEDIA_OK：继续解码。
- 返回 PMEDIA_CALLBACK_STOP：请求停止当前输出并暂停 session；应用稍后可调用
  pm_resume()。
- 返回负值：当前 pump 返回 PMEDIA_ERROR_CALLBACK，并通过 error/event callback 报告。

### pump 和状态

pm_pump() 的常见结果如下：

| 返回值 | 应用处理 |
| --- | --- |
| PMEDIA_OK | 已完成一小段工作，或 session 当前暂停；稍后继续调用 |
| PMEDIA_WOULD_BLOCK | 当前 WaveOut 块尚未播放完；交还消息循环，稍后继续调用 |
| PMEDIA_EOF | 所有可输出流已经耗尽；通常关闭 session |
| PMEDIA_ERROR_* | 停止泵循环，读取 pm_last_error()，然后释放 session |
| PMEDIA_CALLBACK_STOP | 兼容路径可能直接返回；按暂停处理并在需要时 resume |

pm_pause() 不会释放 decoder 或输出资源；pm_resume() 继续同一位置。pm_stop() 使
session 进入停止态，之后的 pm_pump()、pm_pause()、pm_resume()、pm_seek() 会
返回 PMEDIA_ERROR_STATE（重复 pm_stop() 是安全的）。pm_seek() 的参数是微秒，
负值非法；FFmpeg 使用可解码的后向关键点，WAV 使用对应 sample 位置，seek 成功后应用
应重新按时间戳显示/播放后续回调。
AMR seek 会重建解码器，清除预测/合成历史，避免重播沿用跳转前的音频状态；pause/resume
则保留历史并继续同一位置。当前设备合同验证 EOF 后 seek 到零，不承诺非零压缩 seek 的精确定位。
含 AAC 的 FFmpeg session 在 `pm_seek(session, 0)` 时，从 DLL 已缓存输入创建新的
demux 和音视频 decoder，保留初始 priming 并清除音频历史，不再次读取宿主 source。
候选创建成功后才替换旧状态，暂停状态保持；失败时旧解码状态保留。
此同步操作短暂同时持有两套 demux/decoder，可能返回内存错误，不能按 pump 预算估算耗时。
非零 AAC seek 仍使用后向关键点与 flush，尚未验证相同的 PCM 保真性或精确定位；
候选分配失败的回滚尚未经过设备故障注入，峰值内存也未测量。
裸 H.264 Annex-B 的零点 seek 同样从缓存事务式重建 demux/decoder，不读宿主 source，
保留暂停状态；正值 seek 返回 PMEDIA_ERROR_NOT_SEEKABLE，拒绝前不改变解码状态。
裸流没有可用于时间定位的容器索引，不能套用 MP4 的后向关键点语义。重建的瞬时内存、
耗时及分配失败注入仍待测量。
便携 WAV IMA 路径例外：已验证按 sample 定位的块内/块边界非零 seek，首个输出 PTS 对应
定位后的 sample；到达或超过 duration（包括极大正值）直接定位 EOF，不从块头重播。
budget_us 必须非负；零使用内部默认处理量。clock_us 目前不驱动按时输出、迟到丢帧、
音视频同步或暂停时间基准，这些仍是本阶段待实现的播放器能力。EOF 事件对当前播放区间只发
一次；成功 seek 后开始新的区间。原生 callback STOP 的块重放语义尚未有设备断言，不应据此
承诺与软解完全相同的消费位置。

## Probe、能力和 backend

所有公开结构都使用 size 字段做版本兼容。调用前应清零结构并设置 size = sizeof(...)。
pm_probe_info.stream 描述输入，pm_probe_info.capabilities 描述该 DLL 和该输入可以
使用的路径；pm_get_*() 则返回已经打开的 session 的实际信息。

不要只根据文件扩展名选择 backend：

| backend | 当前行为 |
| --- | --- |
| PMEDIA_BACKEND_AUTO | WAV PCM 先尝试 WaveOut；不接受时尝试 FFmpeg/便携软解；其他输入按已链接的软解集合打开 |
| PMEDIA_BACKEND_NATIVE | 当前只对设备接受的 WAV PCM WaveOut 有效；其他输入 fail closed，DirectShow graph 探测不等于已实现的 callback source |
| PMEDIA_BACKEND_SOFT | 禁止 WaveOut，使用便携路径或 FFmpeg 软解；超出编译集合、profile、尺寸或声道边界时失败 |

native_graph_available 只表示 WM6 graph 能否创建性探测，不能解释为某个 H.264/AAC/WMV
codec 已经可播放。native_callback_source_available 当前为 false；第三方应用不能把
自己的 source callback 直接交给 DirectShow。

## 首版格式覆盖边界

### WM6 原生路径

设备运行时可能注册 ASF/WMV/WMA、AVI、WAV、MP3、MPEG、MJPEG、H.264、AAC 等 filter 或
ACM codec，但不同固件差异很大。本 DLL 当前只把设备接受的 WAV PCM WaveOut 作为可用
native 播放合同；其他原生格式必须等待未来 callback source filter/native renderer
生命周期完成，不能由桌面 DirectShow 文档推断。

### FFmpeg 软解路径

| 类别 | 首版边界 |
| --- | --- |
| 容器/输入 | AVI、MP4/MOV、MPEG-PS、MPEG-TS、WAV、FLV、AMR/ADTS/MP3/H.264 Annex-B 等选定裸流 |
| 视频 | H.264/AVC、MPEG-4 Part 2、MPEG-1/2 Video、MJPEG、H.263；8-bit、4:2:0、渐进式，公开保证最多 640×480 |
| 音频 | AAC-LC、MP2/MP3、AMR-NB/WB、PCM、IMA ADPCM；输出最多双声道交错 S16LE |
| 明确排除 | AV1、HEVC/H.265、VP9、H.264 High profile、10-bit/4:2:2/4:4:4、隔行视频、编码、DRM、字幕、RTSP/HLS/直播协议 |

实际打开结果以 pm_probe()/pm_get_capabilities() 和 pm_get_backend() 为准。容器支持不代表
其中所有 codec 都支持：FLV 的 H.264/AAC-LC 短媒体已有设备合同，Sorenson H.263
（FLV1）不在当前 decoder 集合，返回 PMEDIA_ERROR_UNSUPPORTED；它不等于 AVI 中的
普通 H.263。FLV 内 MP3、其他 codec 组合及损坏 tag/payload 仍待独立验收。

FFmpeg 源码快照、配置、ARMV4I 构建方式和原始许可证保留在仓库；AVC/H.264 的源码许可证不等于
专利许可，发布产品前仍需独立评估地区和发行方式的 AVC 义务。

## 错误处理和关闭

公共错误码定义在头文件中，包括参数、状态、内存、I/O、不可 seek、格式、不支持、资源
上限、native 和 callback 错误。pm_last_error() 返回 session 内部的只读 UTF-8/ASCII
错误字符串，不需要释放；它只在 session 存活期间有效，后续错误可能覆盖内容。

pm_error_callback 的 message 同样只在当前回调期间有效。错误回调不应抛出异常、调用
媒体入口或直接释放 session。应用应在自己的控制流中退出泵循环，调用 pm_close()，
再释放 source/output context 和应用拥有的复制缓冲。

## 相关文件

- 公共 ABI：positron_media/positron_media.h
- 当前能力矩阵：[docs/CAPABILITIES.md](../docs/CAPABILITIES.md)
- 总体架构、线程和所有权：[docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)
- 回归宿主边界：[test_host/README.md](../test_host/README.md)
- FFmpeg 版本、移植和许可证：[third_party/ffmpeg-3.4.14/POSITRON_PORT.md](../third_party/ffmpeg-3.4.14/POSITRON_PORT.md)、[THIRD_PARTY.md](../THIRD_PARTY.md)

当前 Debug/Release 设备证据覆盖 WM6 Emulator 上的输入错误传播、probe 失败不改输出、
非 seek WAV PCM8 与双声道 PCM16 的样本/时间戳、EOF/seek 重播、软解回调暂停与失败、
停止态守卫及独立 session 重复释放。AUTO PCM8 已实际选中 NATIVE，并验证 WaveOut 完成
和统一 S16LE callback。压缩路径另以固定离线夹具验证 MP4/AVCC Constrained Baseline +
AAC-LC stereo、VGA Main/B 帧和 ADTS AAC-LC mono 的实际 I420/S16LE、时间戳、EOF 后
seek 重播与 callback 暂停/恢复；High/4:2:2/隔行/超 VGA/非 LC AAC 和截断 MP4 头拒绝。
TEST1332 对上述 MP4 stereo 与 ADTS mono 各执行三次完整播放、两次暂停中 seek 到零，
后两次 S16LE 全部字节与首次输出比较一致，样本数、PTS/时长和每区间单次 EOF 保持。
打开后让不可 seek source 的 read 返回 I/O 错误，重播仍成功，验证使用 DLL 缓存。
此比较针对同一 WM6 decoder，不要求桌面不同版本的浮点 AAC 输出逐字节相同。
AVI/MJPEG 4:2:0 + MP3 stereo 与 44.1 kHz mono MP3 裸流也已验证：全范围视频像素与 flags、
逐帧 PTS/时长、PCM 样本数（含裸流 gapless trimming）、不可 seek 打开、音频 callback
暂停/恢复、EOF/seek 重播、独立关闭，以及 MJPEG 4:2:2、截断头和应用尺寸上限拒绝。
MPEG-TS/MPEG-2 + MP2 与 MPEG-PS/MPEG-1 + MP2 的三帧 I/B/P、有限范围像素、非零
源 PTS、五块 PCM 和尾帧推导标记也已验证，包括 13-byte 短读、不可 seek AUTO 打开、
视频 callback 暂停/恢复、EOF/seek 重播、独立关闭及隔行/超限 MPEG-2 与截断头拒绝。
AMR-NB 12.2 kbit/s 与 AMR-WB 23.85 kbit/s 单声道裸流也已验证：逐块 PCM/PTS/时长、
幅度与过零检查、重播 PCM 校验值一致、1-byte 短读、不可 seek AUTO 打开、暂停/恢复、
负音频 callback、EOF/seek、独立关闭及截断头失败不改 probe。DTX、丢失帧、其他码率和
3GP 内 AMR 仍未验收，不能把这两个夹具解释为完整 AMR 一致性测试。
WAV IMA 的 8 kHz 单/不同内容双声道已有逐 sample S16LE 参考比对、块大小与 fact 裁剪、
块内/边界/EOF seek、5-byte 短读、不可 seek AUTO、暂停/STOP/负 callback、重播和独立关闭
断言；有效后置 fact、无 fact padding、损坏头和 2041-sample 容量/超限拒绝也已覆盖。
MPEG-4 Part 2 的 MP4 Simple 320×240、Advanced Simple VGA/B 帧及 AVI Simple + MP3
已通过 TEST1344：有限范围 I420/flags、逐帧 PTS/时长、26 块 PCM、EOF 后 seek 零的
视频取样/PCM 校验值一致、7-byte 短读、不可 seek AUTO、音视频 STOP/恢复和负 callback、
停止态及独立关闭。隔行/超 VGA、较小应用尺寸上限与截断头均拒绝且不输出。
这不等于 Qpel、GMC 或所有 Advanced Simple 编码工具已经验收。

H.263 的 AVI CIF 352×288 与 H.263+ 自定义 VGA 640×480 已通过 TEST1347 双配置设备门：
三帧 I/P/P、有限范围 I420/stride/flags、PTS 0/200000/400000 µs、每帧时长 200000 µs；
各三 session、每 session 初次解码和 EOF 后 seek 零重播的视频取样校验值一致。
7-byte 短读、不可 seek AUTO、SOFT、暂停/视频 STOP/恢复、负 callback、EOF 一次性和
停止态守卫均有断言；打开后禁止继续读取 source，重播使用 DLL 缓存。
704×576 超限、较小应用尺寸上限和截断 AVI 头在输出前拒绝，失败 probe 不改输出。
这只覆盖无音频的短 AVI，不代表 H.263 裸流、3GP、复杂编码工具、
音视频组合或非零 seek 已验收。

FLV 的 Constrained Baseline 320×240 + AAC-LC stereo 和 Main VGA/B 帧无音频输入
已通过 TEST1348 双配置设备门。保留视频源 PTS：前者从 21000 µs 起，后者从 400000 µs 起，
各三帧、时长 200000 µs；有限范围 I420/stride/flags 与重复播放取样校验值一致。
FLV AAC 保留 priming 包，共 30 块各 1024 sample，输出 PTS 保留容器毫秒量化；
不能套用 MP4 的 29 块裁剪规则。各三 session、每 session 三遍播放，后两遍 seek 零的
全部 S16LE 字节与首次相同，source 禁止再读取。短读、不可 seek AUTO、SOFT、暂停、
音频或视频 STOP/负 callback、EOF/停止态/关闭均有断言；超 VGA、FLV1 和截断头
在输出前拒绝，失败 probe 不改输出。此合同不涉及 RTMP/直播、非零 seek 或实时同步。

H.264 Annex-B 的 Constrained Baseline 320×240 与 Main VGA/B 帧无音频输入通过
TEST1349：三帧有限范围 I420、plane/stride/像素取样/关键帧标记，PTS 全部为 -1，
不设推导标记，SPS 5 fps 对应时长 200000 µs。各三 session 三遍播放的取样校验值一致，
零点重播禁止重读 source；1-byte 短读、不可 seek AUTO/SOFT、暂停/视频 STOP/负 callback、
EOF/停止态/关闭和正值 seek 拒绝后继续解码均有断言。High、4:2:2、隔行返回
UNSUPPORTED，超 VGA 返回 LIMIT；截断头返回 FORMAT，失败 probe 不改输出且不产生帧。
同一 TEST1349 还验证无 timing 的三帧输出 duration=0，以及相同 SPS 重复的六帧输出与
零点重播；生命周期断言保持。先播放有效 Baseline 再拼接移除 timing、Main VGA、High、
4:2:2、隔行或超 VGA 的新 SPS，均在 probe/open 阶段拒绝且无输出。这不是动态重配置
支持：只允许不变 SPS。VFR/SEI timing、PPS 变化、损坏 payload 和重建分配失败仍待门。

夹具来源与哈希见 [媒体夹具](../test_host/fixtures/media/README.md)。这些是短小媒体的
解码合同，不是实时播放、复杂画面质量、帧率、underrun、内存泄漏证明或真实 ARMV4I
设备验收；其他已编译容器/codec、截断压缩 payload、非零 FFmpeg 压缩 seek 与原生完整生命周期
仍待专用 fixture。

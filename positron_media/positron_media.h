#ifndef POSITRON_MEDIA_H
#define POSITRON_MEDIA_H

/*
 * Positron Media public ABI.
 *
 * The header deliberately exposes no DirectShow, ACM, WaveOut or FFmpeg
 * types.  Callbacks are synchronous and their buffers are borrowed only for
 * the duration of the callback.  A session is owned by the caller and must
 * be released with pm_close().
 */

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef POSITRON_MEDIA_EXPORTS
#define PMEDIA_API __declspec(dllexport)
#else
#define PMEDIA_API __declspec(dllimport)
#endif

#define PMEDIA_ABI_VERSION 0x00010000UL
#define PMEDIA_STRUCT_VERSION 1UL
#define PMEDIA_MAX_PLANES 3

typedef __int64 pm_position;
typedef void *pm_session;

enum {
    PMEDIA_OK = 0,
    PMEDIA_EOF = 1,
    PMEDIA_WOULD_BLOCK = 2,
    PMEDIA_CALLBACK_STOP = 3,
    PMEDIA_ERROR_ARGUMENT = -1,
    PMEDIA_ERROR_STATE = -2,
    PMEDIA_ERROR_MEMORY = -3,
    PMEDIA_ERROR_IO = -4,
    PMEDIA_ERROR_NOT_SEEKABLE = -5,
    PMEDIA_ERROR_FORMAT = -6,
    PMEDIA_ERROR_UNSUPPORTED = -7,
    PMEDIA_ERROR_LIMIT = -8,
    PMEDIA_ERROR_NATIVE = -9,
    PMEDIA_ERROR_CALLBACK = -10
};

enum {
    PMEDIA_SEEK_SET = 0,
    PMEDIA_SEEK_CUR = 1,
    PMEDIA_SEEK_END = 2
};

enum {
    PMEDIA_BACKEND_NONE = 0,
    PMEDIA_BACKEND_AUTO = 1,
    PMEDIA_BACKEND_NATIVE = 2,
    PMEDIA_BACKEND_SOFT = 3
};

enum {
    PMEDIA_CONTAINER_UNKNOWN = 0,
    PMEDIA_CONTAINER_WAV = 1,
    PMEDIA_CONTAINER_AVI = 2,
    PMEDIA_CONTAINER_MP4 = 3,
    PMEDIA_CONTAINER_MPEG_PS = 4,
    PMEDIA_CONTAINER_MPEG_TS = 5,
    PMEDIA_CONTAINER_FLV = 6,
    PMEDIA_CONTAINER_RAW = 7,
    PMEDIA_CONTAINER_ASF = 8,
    PMEDIA_CONTAINER_AMR = 9
};

enum {
    PMEDIA_CODEC_UNKNOWN = 0,
    PMEDIA_CODEC_PCM = 1,
    PMEDIA_CODEC_IMA_ADPCM = 2,
    PMEDIA_CODEC_MP3 = 3,
    PMEDIA_CODEC_AAC_LC = 4,
    PMEDIA_CODEC_AMR_NB = 5,
    PMEDIA_CODEC_AMR_WB = 6,
    PMEDIA_CODEC_H264 = 7,
    PMEDIA_CODEC_MPEG4_PART2 = 8,
    PMEDIA_CODEC_MPEG1_VIDEO = 9,
    PMEDIA_CODEC_MPEG2_VIDEO = 10,
    PMEDIA_CODEC_MJPEG = 11,
    PMEDIA_CODEC_H263 = 12,
    PMEDIA_CODEC_WMV = 13,
    PMEDIA_CODEC_WMA = 14,
    /* Appended so the first ABI's existing codec values remain stable. */
    PMEDIA_CODEC_MP2 = 15
};

enum {
    PMEDIA_PIXEL_I420 = 1
};

enum {
    PMEDIA_EVENT_OPENED = 1,
    PMEDIA_EVENT_BACKEND_FALLBACK = 2,
    PMEDIA_EVENT_FORMAT = 3,
    PMEDIA_EVENT_EOF = 4,
    PMEDIA_EVENT_STOPPED = 5,
    PMEDIA_EVENT_ERROR = 6
};

enum {
    PMEDIA_FRAME_KEY = 0x00000001UL,
    PMEDIA_FRAME_INTERLACED = 0x00000002UL
};

/* read returns an I/O status and writes the number of bytes produced. */
typedef int (*pm_read_callback)(void *context,
                                unsigned char *destination,
                                int capacity,
                                int *out_read);

/* seek/tell/size return an I/O status and write the requested value. */
typedef int (*pm_seek_callback)(void *context,
                                pm_position offset,
                                int origin,
                                pm_position *out_position);
typedef int (*pm_tell_callback)(void *context, pm_position *out_position);
typedef int (*pm_size_callback)(void *context, pm_position *out_size);

typedef struct pm_source_callbacks {
    unsigned long size;
    void *context;
    pm_read_callback read;
    pm_seek_callback seek;
    pm_tell_callback tell;
    pm_size_callback size_callback;
} pm_source_callbacks;

typedef struct pm_video_frame {
    unsigned long size;
    int format;
    int width;
    int height;
    int stride[PMEDIA_MAX_PLANES];
    const unsigned char *plane[PMEDIA_MAX_PLANES];
    pm_position pts_us;
    pm_position duration_us;
    unsigned long flags;
} pm_video_frame;

typedef struct pm_audio_block {
    unsigned long size;
    const unsigned char *data;
    int bytes;
    int samples;
    int sample_rate;
    int channels;
    pm_position pts_us;
    pm_position duration_us;
} pm_audio_block;

typedef int (*pm_video_callback)(void *context, const pm_video_frame *frame);
typedef int (*pm_audio_callback)(void *context, const pm_audio_block *block);
typedef void (*pm_event_callback)(void *context, int event, int value);
typedef void (*pm_error_callback)(void *context, int error, const char *message);

typedef struct pm_output_callbacks {
    unsigned long size;
    void *context;
    pm_video_callback video;
    pm_audio_callback audio;
    pm_event_callback event;
    pm_error_callback error;
} pm_output_callbacks;

typedef struct pm_open_options {
    unsigned long size;
    int backend;
    unsigned long flags;
    void *native_window;
    int max_video_width;
    int max_video_height;
} pm_open_options;

typedef struct pm_stream_info {
    unsigned long size;
    int container;
    int video_codec;
    int audio_codec;
    int has_video;
    int has_audio;
    int width;
    int height;
    int frame_rate_num;
    int frame_rate_den;
    int sample_rate;
    int channels;
    int bits_per_sample;
    pm_position duration_us;
    pm_position file_size;
} pm_stream_info;

typedef struct pm_capabilities {
    unsigned long size;
    unsigned long abi_version;
    int native_graph_available;
    int native_callback_source_available;
    int native_waveout_available;
    int soft_audio_available;
    int soft_video_available;
    int requires_seek;
    int max_video_width;
    int max_video_height;
    unsigned long container_mask;
    unsigned long codec_mask;
} pm_capabilities;

typedef struct pm_probe_info {
    unsigned long size;
    pm_stream_info stream;
    pm_capabilities capabilities;
} pm_probe_info;

PMEDIA_API unsigned long pm_abi_version(void);
PMEDIA_API int pm_probe(const pm_source_callbacks *source, pm_probe_info *out_info);
PMEDIA_API int pm_open(const pm_source_callbacks *source,
                       const pm_open_options *options,
                       const pm_output_callbacks *output,
                       pm_session *out_session);
PMEDIA_API int pm_close(pm_session session);
PMEDIA_API int pm_pump(pm_session session, pm_position clock_us, int budget_us);
PMEDIA_API int pm_pause(pm_session session);
PMEDIA_API int pm_resume(pm_session session);
PMEDIA_API int pm_stop(pm_session session);
PMEDIA_API int pm_seek(pm_session session, pm_position position_us);
PMEDIA_API int pm_get_stream_info(pm_session session, pm_stream_info *out_info);
PMEDIA_API int pm_get_capabilities(pm_session session, pm_capabilities *out_capabilities);
PMEDIA_API int pm_get_backend(pm_session session);
PMEDIA_API const char *pm_last_error(pm_session session);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_MEDIA_H */

/*
 * FFmpeg 3.4.14 software demux/decode backend.
 *
 * The public DLL owns the input buffer and calls this backend synchronously.
 * FFmpeg sees only a memory AVIOContext; it never opens a URL or starts a
 * worker thread.  Output buffers are borrowed by the Positron callbacks.
 */

#include "positron_media_ffmpeg.h"

#ifndef inline
#define inline __inline
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/pixfmt.h"
#include "libavutil/samplefmt.h"

#define PMEDIA_FFMPEG_IO_BYTES 4096
#define PMEDIA_FFMPEG_MAX_AUDIO_CHANNELS 2
#define PMEDIA_FFMPEG_MAX_AUDIO_SAMPLES 8192

struct pmedia_ffmpeg {
    const unsigned char *input;
    int input_bytes;
    int input_position;
    int max_video_width;
    int max_video_height;
    pm_output_callbacks output;
    AVFormatContext *format;
    AVIOContext *io;
    unsigned char *io_buffer;
    AVCodecContext *video_codec;
    AVCodecContext *audio_codec;
    AVFrame *frame;
    int video_stream;
    int audio_stream;
    int input_eof;
    int video_flush_sent;
    int audio_flush_sent;
    int video_drained;
    int audio_drained;
    unsigned char *audio_buffer;
    int audio_buffer_bytes;
    int opened_format;
};

static void pm_ff_error(char *error_text, int error_text_bytes,
                        const char *message)
{
    int length;

    if (error_text == NULL || error_text_bytes <= 0) return;
    if (message == NULL) message = "FFmpeg software backend failed";
    length = (int)strlen(message);
    if (length >= error_text_bytes) length = error_text_bytes - 1;
    memcpy(error_text, message, (size_t)length);
    error_text[length] = '\0';
}

static void pm_ff_error_code(char *error_text, int error_text_bytes,
                             const char *prefix, int error_code)
{
    char buffer[64];
    if (prefix == NULL) prefix = "FFmpeg error";
    av_strerror(error_code, buffer, sizeof(buffer));
    if (error_text != NULL && error_text_bytes > 0) {
        _snprintf(error_text, error_text_bytes, "%s: %s", prefix, buffer);
        error_text[error_text_bytes - 1] = '\0';
    }
}

static int pm_ff_read(void *opaque, unsigned char *buffer, int buffer_bytes)
{
    pmedia_ffmpeg *context;
    int remaining;

    context = (pmedia_ffmpeg *)opaque;
    if (context == NULL || buffer == NULL || buffer_bytes < 0) {
        return AVERROR(EINVAL);
    }
    remaining = context->input_bytes - context->input_position;
    if (remaining <= 0) return AVERROR_EOF;
    if (buffer_bytes > remaining) buffer_bytes = remaining;
    memcpy(buffer, context->input + context->input_position,
           (size_t)buffer_bytes);
    context->input_position += buffer_bytes;
    return buffer_bytes;
}

static int64_t pm_ff_seek(void *opaque, int64_t offset, int whence)
{
    pmedia_ffmpeg *context;
    int64_t position;

    context = (pmedia_ffmpeg *)opaque;
    if (context == NULL) return AVERROR(EINVAL);
    if ((whence & 0xFFFF) == AVSEEK_SIZE) return context->input_bytes;
    if ((whence & 0xFFFF) == SEEK_SET) position = offset;
    else if ((whence & 0xFFFF) == SEEK_CUR) {
        position = (int64_t)context->input_position + offset;
    } else if ((whence & 0xFFFF) == SEEK_END) {
        position = (int64_t)context->input_bytes + offset;
    } else {
        return AVERROR(EINVAL);
    }
    if (position < 0 || position > context->input_bytes) {
        return AVERROR(EINVAL);
    }
    context->input_position = (int)position;
    return position;
}

static int pm_ff_container(const char *name)
{
    if (name == NULL) return PMEDIA_CONTAINER_UNKNOWN;
    if (strstr(name, "avi") != NULL) return PMEDIA_CONTAINER_AVI;
    if (strstr(name, "mov") != NULL || strstr(name, "mp4") != NULL ||
        strstr(name, "3gp") != NULL || strstr(name, "mj2") != NULL) {
        return PMEDIA_CONTAINER_MP4;
    }
    if (strstr(name, "mpegts") != NULL || strstr(name, "mpegtsraw") != NULL) {
        return PMEDIA_CONTAINER_MPEG_TS;
    }
    if (strstr(name, "mpegvideo") != NULL ||
        strstr(name, "h264") != NULL || strstr(name, "m4v") != NULL ||
        strstr(name, "mjpeg") != NULL || strstr(name, "aac") != NULL ||
        strstr(name, "mp3") != NULL) {
        return PMEDIA_CONTAINER_RAW;
    }
    if (strstr(name, "mpeg") != NULL) return PMEDIA_CONTAINER_MPEG_PS;
    if (strstr(name, "wav") != NULL) return PMEDIA_CONTAINER_WAV;
    if (strstr(name, "flv") != NULL) return PMEDIA_CONTAINER_FLV;
    if (strstr(name, "amr") != NULL) return PMEDIA_CONTAINER_AMR;
    return PMEDIA_CONTAINER_UNKNOWN;
}

static int pm_ff_codec(enum AVCodecID codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_U8:
    case AV_CODEC_ID_PCM_U16LE:
        return PMEDIA_CODEC_PCM;
    case AV_CODEC_ID_ADPCM_IMA_WAV:
        return PMEDIA_CODEC_IMA_ADPCM;
    case AV_CODEC_ID_MP2:
        return PMEDIA_CODEC_MP2;
    case AV_CODEC_ID_MP3:
        return PMEDIA_CODEC_MP3;
    case AV_CODEC_ID_AAC:
        return PMEDIA_CODEC_AAC_LC;
    case AV_CODEC_ID_AMR_NB:
        return PMEDIA_CODEC_AMR_NB;
    case AV_CODEC_ID_AMR_WB:
        return PMEDIA_CODEC_AMR_WB;
    case AV_CODEC_ID_H264:
        return PMEDIA_CODEC_H264;
    case AV_CODEC_ID_MPEG4:
        return PMEDIA_CODEC_MPEG4_PART2;
    case AV_CODEC_ID_MPEG1VIDEO:
        return PMEDIA_CODEC_MPEG1_VIDEO;
    case AV_CODEC_ID_MPEG2VIDEO:
        return PMEDIA_CODEC_MPEG2_VIDEO;
    case AV_CODEC_ID_MJPEG:
        return PMEDIA_CODEC_MJPEG;
    case AV_CODEC_ID_H263:
        return PMEDIA_CODEC_H263;
    case AV_CODEC_ID_WMAV1:
    case AV_CODEC_ID_WMAV2:
        return PMEDIA_CODEC_WMA;
    case AV_CODEC_ID_WMV1:
    case AV_CODEC_ID_WMV2:
    case AV_CODEC_ID_WMV3:
        return PMEDIA_CODEC_WMV;
    default:
        return PMEDIA_CODEC_UNKNOWN;
    }
}

static int pm_ff_is_video_supported(enum AVCodecID codec_id)
{
    return codec_id == AV_CODEC_ID_H264 || codec_id == AV_CODEC_ID_MPEG4 ||
           codec_id == AV_CODEC_ID_MPEG1VIDEO ||
           codec_id == AV_CODEC_ID_MPEG2VIDEO ||
           codec_id == AV_CODEC_ID_MJPEG || codec_id == AV_CODEC_ID_H263;
}

static int pm_ff_is_audio_supported(enum AVCodecID codec_id)
{
    return codec_id == AV_CODEC_ID_PCM_S16LE ||
           codec_id == AV_CODEC_ID_PCM_S8 || codec_id == AV_CODEC_ID_PCM_U8 ||
           codec_id == AV_CODEC_ID_PCM_U16LE ||
           codec_id == AV_CODEC_ID_ADPCM_IMA_WAV ||
           codec_id == AV_CODEC_ID_MP2 || codec_id == AV_CODEC_ID_MP3 ||
           codec_id == AV_CODEC_ID_AAC || codec_id == AV_CODEC_ID_AMR_NB ||
           codec_id == AV_CODEC_ID_AMR_WB;
}

static int pm_ff_mask_value(int value)
{
    if (value <= 0 || value >= 31) return 0;
    return (int)(1UL << (value - 1));
}

static pm_position pm_ff_timestamp(int64_t timestamp, AVRational time_base)
{
    AVRational microseconds;
    if (timestamp == AV_NOPTS_VALUE) return (pm_position)-1;
    microseconds.num = 1;
    microseconds.den = 1000000;
    return (pm_position)av_rescale_q(timestamp, time_base, microseconds);
}

static void pm_ff_fill_base_info(pmedia_ffmpeg *context,
                                 pm_stream_info *info,
                                 pm_capabilities *capabilities)
{
    int i;
    AVStream *stream;
    AVCodecParameters *parameters;
    pm_position duration;
    AVRational microseconds;
    AVRational output_microseconds;

    memset(info, 0, sizeof(*info));
    memset(capabilities, 0, sizeof(*capabilities));
    info->size = sizeof(*info);
    capabilities->size = sizeof(*capabilities);
    capabilities->abi_version = PMEDIA_ABI_VERSION;
    capabilities->max_video_width = context->max_video_width;
    capabilities->max_video_height = context->max_video_height;
    info->file_size = context->input_bytes;
    info->container = pm_ff_container(context->format->iformat == NULL ?
                                      NULL : context->format->iformat->name);
    for (i = 0; i < (int)context->format->nb_streams; i++) {
        stream = context->format->streams[i];
        parameters = stream->codecpar;
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO &&
            !info->has_video) {
            info->has_video = 1;
            info->video_codec = pm_ff_codec(parameters->codec_id);
            info->width = parameters->width;
            info->height = parameters->height;
            info->frame_rate_num = stream->avg_frame_rate.num;
            info->frame_rate_den = stream->avg_frame_rate.den;
            if (info->frame_rate_num <= 0 || info->frame_rate_den <= 0) {
                info->frame_rate_num = stream->r_frame_rate.num;
                info->frame_rate_den = stream->r_frame_rate.den;
            }
        } else if (parameters->codec_type == AVMEDIA_TYPE_AUDIO &&
                   !info->has_audio) {
            info->has_audio = 1;
            info->audio_codec = pm_ff_codec(parameters->codec_id);
            info->sample_rate = parameters->sample_rate;
            info->channels = parameters->channels;
            info->bits_per_sample = parameters->bits_per_raw_sample;
            if (info->bits_per_sample <= 0) info->bits_per_sample = 16;
        }
    }
    if (context->format->duration != AV_NOPTS_VALUE) {
        microseconds.num = 1;
        microseconds.den = AV_TIME_BASE;
        output_microseconds.num = 1;
        output_microseconds.den = 1000000;
        duration = (pm_position)av_rescale_q(context->format->duration,
                                             microseconds,
                                             output_microseconds);
        info->duration_us = duration;
    }
    capabilities->soft_audio_available = info->has_audio;
    capabilities->soft_video_available = info->has_video;
    if (info->has_video &&
        (info->width <= 0 || info->height <= 0 ||
         info->width > context->max_video_width ||
         info->height > context->max_video_height)) {
        capabilities->soft_video_available = 0;
    }
    capabilities->container_mask = (unsigned long)
        pm_ff_mask_value(info->container);
    capabilities->codec_mask = (unsigned long)
        (pm_ff_mask_value(info->video_codec) |
         pm_ff_mask_value(info->audio_codec));
}

static int pm_ff_validate_streams(pmedia_ffmpeg *context,
                                  char *error_text,
                                  int error_text_bytes)
{
    int i;
    AVStream *stream;
    AVCodecParameters *parameters;

    context->video_stream = -1;
    context->audio_stream = -1;
    for (i = 0; i < (int)context->format->nb_streams; i++) {
        stream = context->format->streams[i];
        parameters = stream->codecpar;
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO &&
            context->video_stream < 0) {
            if (!pm_ff_is_video_supported(parameters->codec_id)) {
                pm_ff_error(error_text, error_text_bytes,
                            "FFmpeg video codec is outside the ARMV4I subset");
                return PMEDIA_ERROR_UNSUPPORTED;
            }
            if (parameters->width <= 0 || parameters->height <= 0 ||
                parameters->width > context->max_video_width ||
                parameters->height > context->max_video_height) {
                pm_ff_error(error_text, error_text_bytes,
                            "software video exceeds the 640x480 limit");
                return PMEDIA_ERROR_LIMIT;
            }
            if (parameters->codec_id == AV_CODEC_ID_H264 &&
                parameters->profile != FF_PROFILE_UNKNOWN &&
                parameters->profile != FF_PROFILE_H264_BASELINE &&
                parameters->profile != FF_PROFILE_H264_MAIN) {
                pm_ff_error(error_text, error_text_bytes,
                            "H.264 profile is outside the ARMV4I subset");
                return PMEDIA_ERROR_UNSUPPORTED;
            }
            context->video_stream = i;
        } else if (parameters->codec_type == AVMEDIA_TYPE_AUDIO &&
                   context->audio_stream < 0) {
            if (!pm_ff_is_audio_supported(parameters->codec_id)) {
                pm_ff_error(error_text, error_text_bytes,
                            "FFmpeg audio codec is outside the ARMV4I subset");
                return PMEDIA_ERROR_UNSUPPORTED;
            }
            if (parameters->channels < 1 ||
                parameters->channels > PMEDIA_FFMPEG_MAX_AUDIO_CHANNELS ||
                parameters->sample_rate <= 0) {
                pm_ff_error(error_text, error_text_bytes,
                            "audio channel layout is outside the ABI subset");
                return PMEDIA_ERROR_UNSUPPORTED;
            }
            if (parameters->codec_id == AV_CODEC_ID_AAC &&
                parameters->profile != FF_PROFILE_UNKNOWN &&
                parameters->profile != FF_PROFILE_AAC_LOW) {
                pm_ff_error(error_text, error_text_bytes,
                            "AAC profile is outside the AAC-LC subset");
                return PMEDIA_ERROR_UNSUPPORTED;
            }
            context->audio_stream = i;
        }
    }
    if (context->video_stream < 0 && context->audio_stream < 0) {
        pm_ff_error(error_text, error_text_bytes, "FFmpeg found no playable stream");
        return PMEDIA_ERROR_UNSUPPORTED;
    }
    return PMEDIA_OK;
}

static int pm_ff_open_decoder(AVFormatContext *format,
                              int stream_index,
                              AVCodecContext **out_codec,
                              char *error_text,
                              int error_text_bytes)
{
    AVStream *stream;
    const AVCodec *decoder;
    AVCodecContext *codec;
    int result;

    if (stream_index < 0) return PMEDIA_OK;
    stream = format->streams[stream_index];
    decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (decoder == NULL) {
        pm_ff_error(error_text, error_text_bytes,
                    "FFmpeg decoder is not compiled in");
        return PMEDIA_ERROR_UNSUPPORTED;
    }
    codec = avcodec_alloc_context3(decoder);
    if (codec == NULL) {
        pm_ff_error(error_text, error_text_bytes, "FFmpeg codec allocation failed");
        return PMEDIA_ERROR_MEMORY;
    }
    result = avcodec_parameters_to_context(codec, stream->codecpar);
    if (result < 0) {
        avcodec_free_context(&codec);
        pm_ff_error_code(error_text, error_text_bytes,
                         "FFmpeg codec parameters failed", result);
        return PMEDIA_ERROR_FORMAT;
    }
    codec->thread_count = 1;
    result = avcodec_open2(codec, decoder, NULL);
    if (result < 0) {
        avcodec_free_context(&codec);
        pm_ff_error_code(error_text, error_text_bytes,
                         "FFmpeg decoder open failed", result);
        return PMEDIA_ERROR_UNSUPPORTED;
    }
    *out_codec = codec;
    return PMEDIA_OK;
}

static void pm_ff_cleanup(pmedia_ffmpeg *context)
{
    if (context == NULL) return;
    if (context->video_codec != NULL) avcodec_free_context(&context->video_codec);
    if (context->audio_codec != NULL) avcodec_free_context(&context->audio_codec);
    if (context->frame != NULL) av_frame_free(&context->frame);
    if (context->format != NULL) {
        if (context->opened_format) avformat_close_input(&context->format);
        else avformat_free_context(context->format);
    }
    if (context->io != NULL) {
        /* avio_context_free owns the current AVIO buffer.  libavformat may
         * replace it while probing, so context->io_buffer is not safe to
         * free separately after the AVIO context exists. */
        avio_context_free(&context->io);
        context->io_buffer = NULL;
    }
    if (context->io_buffer != NULL) av_free(context->io_buffer);
    if (context->audio_buffer != NULL) free(context->audio_buffer);
    free(context);
}

static int pm_ff_open_internal(const unsigned char *input,
                               int input_bytes,
                               const pm_output_callbacks *output,
                               int max_video_width,
                               int max_video_height,
                               pmedia_ffmpeg **out_context,
                               pm_stream_info *out_info,
                               pm_capabilities *out_capabilities,
                               char *error_text,
                               int error_text_bytes)
{
    pmedia_ffmpeg *context;
    int result;

    if (input == NULL || input_bytes <= 0 || out_context == NULL ||
        out_info == NULL || out_capabilities == NULL) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    if (max_video_width <= 0) max_video_width = 640;
    if (max_video_height <= 0) max_video_height = 480;
    context = (pmedia_ffmpeg *)calloc(1, sizeof(*context));
    if (context == NULL) return PMEDIA_ERROR_MEMORY;
    context->input = input;
    context->input_bytes = input_bytes;
    context->max_video_width = max_video_width;
    context->max_video_height = max_video_height;
    context->video_stream = -1;
    context->audio_stream = -1;
    if (output != NULL) {
        memcpy(&context->output, output, sizeof(*output));
    }
    av_register_all();
    context->io_buffer = (unsigned char *)av_malloc(PMEDIA_FFMPEG_IO_BYTES);
    if (context->io_buffer == NULL) {
        pm_ff_error(error_text, error_text_bytes, "FFmpeg AVIO allocation failed");
        pm_ff_cleanup(context);
        return PMEDIA_ERROR_MEMORY;
    }
    context->io = avio_alloc_context(context->io_buffer,
                                     PMEDIA_FFMPEG_IO_BYTES,
                                     0,
                                     context,
                                     pm_ff_read,
                                     NULL,
                                     pm_ff_seek);
    if (context->io == NULL) {
        pm_ff_error(error_text, error_text_bytes, "FFmpeg AVIO creation failed");
        pm_ff_cleanup(context);
        return PMEDIA_ERROR_MEMORY;
    }
    context->format = avformat_alloc_context();
    if (context->format == NULL) {
        pm_ff_error(error_text, error_text_bytes, "FFmpeg format allocation failed");
        pm_ff_cleanup(context);
        return PMEDIA_ERROR_MEMORY;
    }
    context->format->pb = context->io;
    context->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    result = avformat_open_input(&context->format, NULL, NULL, NULL);
    if (result < 0 || context->format == NULL) {
        pm_ff_error_code(error_text, error_text_bytes,
                         "FFmpeg input format is not recognized", result);
        pm_ff_cleanup(context);
        return PMEDIA_ERROR_FORMAT;
    }
    context->opened_format = 1;
    result = avformat_find_stream_info(context->format, NULL);
    if (result < 0) {
        pm_ff_error_code(error_text, error_text_bytes,
                         "FFmpeg stream information failed", result);
        pm_ff_cleanup(context);
        return PMEDIA_ERROR_FORMAT;
    }
    pm_ff_fill_base_info(context, out_info, out_capabilities);
    result = pm_ff_validate_streams(context, error_text, error_text_bytes);
    if (result != PMEDIA_OK) {
        pm_ff_cleanup(context);
        return result;
    }
    result = pm_ff_open_decoder(context->format, context->video_stream,
                                &context->video_codec,
                                error_text, error_text_bytes);
    if (result != PMEDIA_OK) {
        pm_ff_cleanup(context);
        return result;
    }
    result = pm_ff_open_decoder(context->format, context->audio_stream,
                                &context->audio_codec,
                                error_text, error_text_bytes);
    if (result != PMEDIA_OK) {
        pm_ff_cleanup(context);
        return result;
    }
    context->frame = av_frame_alloc();
    if (context->frame == NULL) {
        pm_ff_error(error_text, error_text_bytes, "FFmpeg frame allocation failed");
        pm_ff_cleanup(context);
        return PMEDIA_ERROR_MEMORY;
    }
    out_capabilities->soft_audio_available = context->audio_stream >= 0;
    out_capabilities->soft_video_available = context->video_stream >= 0;
    out_capabilities->requires_seek = 0;
    *out_context = context;
    return PMEDIA_OK;
}

int pmedia_ffmpeg_open(const unsigned char *input,
                       int input_bytes,
                       const pm_output_callbacks *output,
                       int max_video_width,
                       int max_video_height,
                       pmedia_ffmpeg **out_context,
                       pm_stream_info *out_info,
                       pm_capabilities *out_capabilities,
                       char *error_text,
                       int error_text_bytes)
{
    if (out_context != NULL) *out_context = NULL;
    return pm_ff_open_internal(input, input_bytes, output,
                               max_video_width, max_video_height,
                               out_context, out_info, out_capabilities,
                               error_text, error_text_bytes);
}

int pmedia_ffmpeg_probe(const unsigned char *input,
                        int input_bytes,
                        pm_stream_info *out_info,
                        pm_capabilities *out_capabilities,
                        char *error_text,
                        int error_text_bytes)
{
    pmedia_ffmpeg *context;
    int result;

    context = NULL;
    result = pm_ff_open_internal(input, input_bytes, NULL, 640, 480,
                                 &context, out_info, out_capabilities,
                                 error_text, error_text_bytes);
    if (context != NULL) pm_ff_cleanup(context);
    return result;
}

void pmedia_ffmpeg_close(pmedia_ffmpeg *context)
{
    pm_ff_cleanup(context);
}

static int pm_ff_clip_sample(int sample)
{
    if (sample < -32768) return -32768;
    if (sample > 32767) return 32767;
    return sample;
}

static int pm_ff_float_sample(float value)
{
    if (value <= -1.0f) return -32768;
    if (value >= 1.0f) return 32767;
    return pm_ff_clip_sample((int)(value * 32767.0f));
}

static int pm_ff_convert_audio(pmedia_ffmpeg *context, int *out_bytes)
{
    enum AVSampleFormat format;
    int samples;
    int channels;
    int i;
    int channel;
    int sample;
    int bytes;
    const unsigned char *packed;
    const unsigned char *plane;
    short *destination;
    float *float_plane;
    int32_t *long_plane;

    format = (enum AVSampleFormat)context->frame->format;
    samples = context->frame->nb_samples;
    channels = context->frame->channels;
    if (channels < 1 || channels > PMEDIA_FFMPEG_MAX_AUDIO_CHANNELS ||
        samples < 1 || samples > PMEDIA_FFMPEG_MAX_AUDIO_SAMPLES) {
        return PMEDIA_ERROR_UNSUPPORTED;
    }
    bytes = samples * channels * (int)sizeof(short);
    if (bytes > context->audio_buffer_bytes) {
        unsigned char *grown;
        grown = (unsigned char *)realloc(context->audio_buffer, (size_t)bytes);
        if (grown == NULL) return PMEDIA_ERROR_MEMORY;
        context->audio_buffer = grown;
        context->audio_buffer_bytes = bytes;
    }
    destination = (short *)context->audio_buffer;
    for (i = 0; i < samples; i++) {
        for (channel = 0; channel < channels; channel++) {
            sample = 0;
            if (format == AV_SAMPLE_FMT_S16) {
                packed = context->frame->data[0];
                sample = ((const short *)packed)[i * channels + channel];
            } else if (format == AV_SAMPLE_FMT_S16P) {
                plane = context->frame->extended_data[channel];
                sample = ((const short *)plane)[i];
            } else if (format == AV_SAMPLE_FMT_FLT) {
                float_plane = (float *)context->frame->data[0];
                sample = pm_ff_float_sample(float_plane[i * channels + channel]);
            } else if (format == AV_SAMPLE_FMT_FLTP) {
                float_plane = (float *)context->frame->extended_data[channel];
                sample = pm_ff_float_sample(float_plane[i]);
            } else if (format == AV_SAMPLE_FMT_U8) {
                packed = context->frame->data[0];
                sample = ((int)packed[i * channels + channel] - 128) << 8;
            } else if (format == AV_SAMPLE_FMT_U8P) {
                plane = context->frame->extended_data[channel];
                sample = ((int)plane[i] - 128) << 8;
            } else if (format == AV_SAMPLE_FMT_S32) {
                long_plane = (int32_t *)context->frame->data[0];
                sample = (int)(long_plane[i * channels + channel] >> 16);
            } else if (format == AV_SAMPLE_FMT_S32P) {
                long_plane = (int32_t *)context->frame->extended_data[channel];
                sample = (int)(long_plane[i] >> 16);
            } else {
                return PMEDIA_ERROR_UNSUPPORTED;
            }
            destination[i * channels + channel] = (short)pm_ff_clip_sample(sample);
        }
    }
    *out_bytes = bytes;
    return PMEDIA_OK;
}

static int pm_ff_emit_video(pmedia_ffmpeg *context)
{
    AVStream *stream;
    pm_video_frame frame;
    int callback_result;

    if (context->frame->format != AV_PIX_FMT_YUV420P &&
        context->frame->format != AV_PIX_FMT_YUVJ420P) {
        return PMEDIA_ERROR_UNSUPPORTED;
    }
    if (context->frame->width > context->max_video_width ||
        context->frame->height > context->max_video_height) {
        return PMEDIA_ERROR_LIMIT;
    }
    memset(&frame, 0, sizeof(frame));
    frame.size = sizeof(frame);
    frame.format = PMEDIA_PIXEL_I420;
    frame.width = context->frame->width;
    frame.height = context->frame->height;
    frame.stride[0] = context->frame->linesize[0];
    frame.stride[1] = context->frame->linesize[1];
    frame.stride[2] = context->frame->linesize[2];
    frame.plane[0] = context->frame->data[0];
    frame.plane[1] = context->frame->data[1];
    frame.plane[2] = context->frame->data[2];
    stream = context->format->streams[context->video_stream];
    frame.pts_us = pm_ff_timestamp(av_frame_get_best_effort_timestamp(context->frame),
                                   stream->time_base);
    frame.duration_us = 0;
    if (context->frame->key_frame) frame.flags |= PMEDIA_FRAME_KEY;
    if (context->output.video == NULL) return PMEDIA_OK;
    callback_result = context->output.video(context->output.context, &frame);
    if (callback_result < 0) return PMEDIA_ERROR_CALLBACK;
    if (callback_result == PMEDIA_CALLBACK_STOP) return PMEDIA_CALLBACK_STOP;
    return PMEDIA_OK;
}

static int pm_ff_emit_audio(pmedia_ffmpeg *context)
{
    AVStream *stream;
    pm_audio_block block;
    int bytes;
    int callback_result;
    int result;

    result = pm_ff_convert_audio(context, &bytes);
    if (result != PMEDIA_OK) return result;
    memset(&block, 0, sizeof(block));
    block.size = sizeof(block);
    block.data = context->audio_buffer;
    block.bytes = bytes;
    block.samples = context->frame->nb_samples;
    block.sample_rate = context->frame->sample_rate > 0 ?
                        context->frame->sample_rate : context->audio_codec->sample_rate;
    block.channels = context->frame->channels;
    stream = context->format->streams[context->audio_stream];
    block.pts_us = pm_ff_timestamp(av_frame_get_best_effort_timestamp(context->frame),
                                   stream->time_base);
    if (block.pts_us < 0) block.pts_us = 0;
    block.duration_us = block.sample_rate > 0 ?
        ((pm_position)block.samples * 1000000) / block.sample_rate : 0;
    if (context->output.audio == NULL) return PMEDIA_OK;
    callback_result = context->output.audio(context->output.context, &block);
    if (callback_result < 0) return PMEDIA_ERROR_CALLBACK;
    if (callback_result == PMEDIA_CALLBACK_STOP) return PMEDIA_CALLBACK_STOP;
    return PMEDIA_OK;
}

static int pm_ff_receive(pmedia_ffmpeg *context,
                         AVCodecContext *codec,
                         int is_video,
                         int *out_emitted,
                         int *out_drained)
{
    int result;

    *out_emitted = 0;
    *out_drained = 0;
    if (codec == NULL) return PMEDIA_OK;
    av_frame_unref(context->frame);
    result = avcodec_receive_frame(codec, context->frame);
    if (result == 0) {
        *out_emitted = 1;
        return is_video ? pm_ff_emit_video(context) : pm_ff_emit_audio(context);
    }
    if (result == AVERROR_EOF) {
        *out_drained = 1;
        return PMEDIA_OK;
    }
    if (result == AVERROR(EAGAIN)) return PMEDIA_OK;
    return PMEDIA_ERROR_FORMAT;
}

static int pm_ff_send_flush(pmedia_ffmpeg *context,
                            AVCodecContext *codec,
                            int *sent)
{
    int result;
    if (codec == NULL || *sent) return PMEDIA_OK;
    result = avcodec_send_packet(codec, NULL);
    if (result < 0 && result != AVERROR_EOF && result != AVERROR(EAGAIN)) {
        return PMEDIA_ERROR_FORMAT;
    }
    *sent = 1;
    return PMEDIA_OK;
}

int pmedia_ffmpeg_pump(pmedia_ffmpeg *context,
                       pm_position clock_us,
                       int budget_us)
{
    int loop;
    int result;
    int emitted;
    int drained;
    int work_limit;
    AVPacket packet;
    AVCodecContext *codec;

    (void)clock_us;
    if (context == NULL) return PMEDIA_ERROR_ARGUMENT;
    work_limit = budget_us <= 0 ? 1 : budget_us / 1000;
    if (work_limit < 1) work_limit = 1;
    if (work_limit > 64) work_limit = 64;
    for (loop = 0; loop < work_limit; loop++) {
        if (!context->video_drained) {
            result = pm_ff_receive(context, context->video_codec, 1,
                                   &emitted, &drained);
            if (result != PMEDIA_OK) return result;
            if (emitted) return PMEDIA_OK;
            if (drained) context->video_drained = 1;
        }
        if (!context->audio_drained) {
            result = pm_ff_receive(context, context->audio_codec, 0,
                                   &emitted, &drained);
            if (result != PMEDIA_OK) return result;
            if (emitted) return PMEDIA_OK;
            if (drained) context->audio_drained = 1;
        }
        if (context->input_eof) {
            result = pm_ff_send_flush(context, context->video_codec,
                                      &context->video_flush_sent);
            if (result != PMEDIA_OK) return result;
            result = pm_ff_send_flush(context, context->audio_codec,
                                      &context->audio_flush_sent);
            if (result != PMEDIA_OK) return result;
            if ((context->video_codec == NULL || context->video_drained) &&
                (context->audio_codec == NULL || context->audio_drained)) {
                return PMEDIA_EOF;
            }
            continue;
        }
        av_init_packet(&packet);
        packet.data = NULL;
        packet.size = 0;
        result = av_read_frame(context->format, &packet);
        if (result < 0) {
            context->input_eof = 1;
            av_packet_unref(&packet);
            continue;
        }
        codec = NULL;
        if (packet.stream_index == context->video_stream) {
            codec = context->video_codec;
        } else if (packet.stream_index == context->audio_stream) {
            codec = context->audio_codec;
        }
        if (codec != NULL) {
            result = avcodec_send_packet(codec, &packet);
            if (result < 0 && result != AVERROR(EAGAIN)) {
                av_packet_unref(&packet);
                return PMEDIA_ERROR_FORMAT;
            }
        }
        av_packet_unref(&packet);
    }
    return PMEDIA_OK;
}

int pmedia_ffmpeg_seek(pmedia_ffmpeg *context, pm_position position_us)
{
    int result;
    if (context == NULL || position_us < 0) return PMEDIA_ERROR_ARGUMENT;
    result = av_seek_frame(context->format, -1, (int64_t)position_us,
                           AVSEEK_FLAG_BACKWARD);
    if (result < 0) return PMEDIA_ERROR_NOT_SEEKABLE;
    avformat_flush(context->format);
    if (context->video_codec != NULL) avcodec_flush_buffers(context->video_codec);
    if (context->audio_codec != NULL) avcodec_flush_buffers(context->audio_codec);
    context->input_eof = 0;
    context->video_flush_sent = 0;
    context->audio_flush_sent = 0;
    context->video_drained = 0;
    context->audio_drained = 0;
    return PMEDIA_OK;
}

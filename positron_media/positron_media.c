#include "positron_media.h"

#include <stdlib.h>
#include <string.h>

#ifdef PMEDIA_HAS_FFMPEG
#include "positron_media_ffmpeg.h"
#endif
#include "pmedia_waveout.h"

#define PMEDIA_INPUT_LIMIT (16 * 1024 * 1024)
#define PMEDIA_READ_CHUNK (32 * 1024)
#define PMEDIA_AUDIO_FRAMES 2048
#define PMEDIA_ERROR_TEXT 256

extern int pmedia_system_probe(void);

typedef struct pm_session_impl {
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_stream_info info;
    pm_capabilities capabilities;
    int backend;
    int paused;
    int stopped;
    int eof_sent;
    int source_seekable;
    unsigned char *input;
    int input_bytes;
    int audio_data_offset;
    int audio_data_bytes;
    int audio_cursor;
    int audio_total_frames;
    int format_tag;
    int block_align;
    int samples_per_block;
    unsigned char *audio_buffer;
    int audio_buffer_bytes;
    pmedia_waveout *waveout;
    int waveout_active;
    void *ffmpeg;
    int ffmpeg_active;
    char error_text[PMEDIA_ERROR_TEXT];
} pm_session_impl;

static void pm_zero(void *memory, int bytes)
{
    memset(memory, 0, (size_t)bytes);
}

static unsigned int pm_le16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned long pm_le32(const unsigned char *p)
{
    return (unsigned long)p[0] |
           ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

static int pm_fourcc(const unsigned char *p, char a, char b, char c, char d)
{
    return p[0] == (unsigned char)a && p[1] == (unsigned char)b &&
           p[2] == (unsigned char)c && p[3] == (unsigned char)d;
}

static void pm_set_error(pm_session_impl *session, int error, const char *message)
{
    int length;

    if (session == NULL) {
        return;
    }
    if (message == NULL) {
        message = "media error";
    }
    length = (int)strlen(message);
    if (length >= PMEDIA_ERROR_TEXT) {
        length = PMEDIA_ERROR_TEXT - 1;
    }
    memcpy(session->error_text, message, (size_t)length);
    session->error_text[length] = '\0';
    if (session->output.error != NULL) {
        session->output.error(session->output.context, error, session->error_text);
    }
    if (session->output.event != NULL) {
        session->output.event(session->output.context, PMEDIA_EVENT_ERROR, error);
    }
}

static void pm_emit(pm_session_impl *session, int event, int value)
{
    if (session != NULL && session->output.event != NULL) {
        session->output.event(session->output.context, event, value);
    }
}

static int pm_copy_source(pm_source_callbacks *destination,
                          const pm_source_callbacks *source)
{
    if (destination == NULL || source == NULL || source->read == NULL ||
        (source->size != 0 && source->size < sizeof(*source))) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    pm_zero(destination, (int)sizeof(*destination));
    memcpy(destination, source, sizeof(*destination));
    if (destination->size == 0) {
        destination->size = sizeof(*destination);
    }
    return PMEDIA_OK;
}

static void pm_copy_output(pm_output_callbacks *destination,
                           const pm_output_callbacks *output)
{
    pm_zero(destination, (int)sizeof(*destination));
    if (output != NULL) {
        memcpy(destination, output, sizeof(*destination));
        if (destination->size == 0) {
            destination->size = sizeof(*destination);
        }
    }
}

static int pm_source_seek_start(pm_session_impl *session)
{
    pm_position position;
    int result;

    if (session->source.seek == NULL) {
        session->source_seekable = 0;
        return PMEDIA_OK;
    }
    position = 0;
    result = session->source.seek(session->source.context,
                                  0,
                                  PMEDIA_SEEK_SET,
                                  &position);
    if (result != PMEDIA_OK) {
        session->source_seekable = 0;
        return PMEDIA_ERROR_NOT_SEEKABLE;
    }
    session->source_seekable = 1;
    return PMEDIA_OK;
}

static int pm_load_input(pm_session_impl *session)
{
    unsigned char *buffer;
    int capacity;
    int total;
    int result;
    int got;

    result = pm_source_seek_start(session);
    if (result != PMEDIA_OK && session->source.tell != NULL) {
        pm_position current;
        current = 0;
        if (session->source.tell(session->source.context, &current) != PMEDIA_OK ||
            current != 0) {
            pm_set_error(session, PMEDIA_ERROR_NOT_SEEKABLE,
                         "source cannot be rewound for media open");
            return PMEDIA_ERROR_NOT_SEEKABLE;
        }
    }

    capacity = PMEDIA_READ_CHUNK;
    buffer = (unsigned char *)malloc((size_t)capacity);
    if (buffer == NULL) {
        pm_set_error(session, PMEDIA_ERROR_MEMORY, "media input allocation failed");
        return PMEDIA_ERROR_MEMORY;
    }
    total = 0;
    for (;;) {
        if (total == capacity) {
            unsigned char *grown;
            int next_capacity;

            if (capacity >= PMEDIA_INPUT_LIMIT) {
                free(buffer);
                pm_set_error(session, PMEDIA_ERROR_LIMIT,
                             "media input exceeds the fixed memory limit");
                return PMEDIA_ERROR_LIMIT;
            }
            next_capacity = capacity * 2;
            if (next_capacity > PMEDIA_INPUT_LIMIT) {
                next_capacity = PMEDIA_INPUT_LIMIT;
            }
            grown = (unsigned char *)realloc(buffer, (size_t)next_capacity);
            if (grown == NULL) {
                free(buffer);
                pm_set_error(session, PMEDIA_ERROR_MEMORY, "media input growth failed");
                return PMEDIA_ERROR_MEMORY;
            }
            buffer = grown;
            capacity = next_capacity;
        }
        got = 0;
        result = session->source.read(session->source.context,
                                      buffer + total,
                                      capacity - total,
                                      &got);
        if (got < 0 || got > capacity - total) {
            free(buffer);
            pm_set_error(session, PMEDIA_ERROR_IO, "source returned an invalid read length");
            return PMEDIA_ERROR_IO;
        }
        total += got;
        if (result == PMEDIA_WOULD_BLOCK) {
            free(buffer);
            pm_set_error(session, PMEDIA_WOULD_BLOCK, "source would block during open");
            return PMEDIA_WOULD_BLOCK;
        }
        if (result != PMEDIA_OK && result != PMEDIA_EOF) {
            free(buffer);
            pm_set_error(session, PMEDIA_ERROR_IO, "source read failed during open");
            return PMEDIA_ERROR_IO;
        }
        if (result == PMEDIA_EOF || got == 0) {
            break;
        }
    }
    session->input = buffer;
    session->input_bytes = total;
    return PMEDIA_OK;
}

static int pm_find_riff_chunk(const unsigned char *data,
                              int bytes,
                              const char *fourcc,
                              int *out_offset,
                              int *out_size)
{
    int offset;
    unsigned long chunk_size;

    if (bytes < 12 || !pm_fourcc(data, 'R', 'I', 'F', 'F')) {
        return PMEDIA_ERROR_FORMAT;
    }
    offset = 12;
    while (offset + 8 <= bytes) {
        if (data[offset] == (unsigned char)fourcc[0] &&
            data[offset + 1] == (unsigned char)fourcc[1] &&
            data[offset + 2] == (unsigned char)fourcc[2] &&
            data[offset + 3] == (unsigned char)fourcc[3]) {
            chunk_size = pm_le32(data + offset + 4);
            if (chunk_size > (unsigned long)(bytes - offset - 8)) {
                return PMEDIA_ERROR_FORMAT;
            }
            *out_offset = offset + 8;
            *out_size = (int)chunk_size;
            return PMEDIA_OK;
        }
        chunk_size = pm_le32(data + offset + 4);
        if (chunk_size > (unsigned long)(bytes - offset - 8)) {
            return PMEDIA_ERROR_FORMAT;
        }
        offset += 8 + (int)chunk_size + ((chunk_size & 1UL) != 0 ? 1 : 0);
    }
    return PMEDIA_ERROR_FORMAT;
}

static int pm_parse_wav(pm_session_impl *session)
{
    int fmt_offset;
    int fmt_bytes;
    int data_offset;
    int data_bytes;
    unsigned int channels;
    unsigned int rate;
    unsigned int bits;
    unsigned int block_align;
    unsigned int samples_per_block;
    unsigned int tag;

    if (session->input_bytes < 12 ||
        !pm_fourcc(session->input, 'R', 'I', 'F', 'F') ||
        !pm_fourcc(session->input + 8, 'W', 'A', 'V', 'E')) {
        return PMEDIA_ERROR_FORMAT;
    }
    if (pm_find_riff_chunk(session->input, session->input_bytes, "fmt ",
                           &fmt_offset, &fmt_bytes) != PMEDIA_OK ||
        fmt_bytes < 16 ||
        pm_find_riff_chunk(session->input, session->input_bytes, "data",
                           &data_offset, &data_bytes) != PMEDIA_OK) {
        return PMEDIA_ERROR_FORMAT;
    }
    tag = pm_le16(session->input + fmt_offset);
    channels = pm_le16(session->input + fmt_offset + 2);
    rate = pm_le32(session->input + fmt_offset + 4);
    block_align = pm_le16(session->input + fmt_offset + 12);
    bits = pm_le16(session->input + fmt_offset + 14);
    samples_per_block = 0;
    if (tag == 0x0011U && fmt_bytes >= 20) {
        samples_per_block = pm_le16(session->input + fmt_offset + 18);
    }
    if ((tag != 1U && tag != 0x0011U) || channels == 0 || channels > 2 ||
        rate == 0 || block_align == 0 || data_bytes <= 0) {
        return PMEDIA_ERROR_UNSUPPORTED;
    }
    if (tag == 1U && (bits != 8U && bits != 16U)) {
        return PMEDIA_ERROR_UNSUPPORTED;
    }
    if (tag == 0x0011U && (samples_per_block == 0 || block_align < channels * 4U)) {
        return PMEDIA_ERROR_FORMAT;
    }

    session->format_tag = (int)tag;
    session->block_align = (int)block_align;
    session->samples_per_block = (int)samples_per_block;
    session->audio_data_offset = data_offset;
    session->audio_data_bytes = data_bytes;
    session->info.container = PMEDIA_CONTAINER_WAV;
    session->info.audio_codec = tag == 1U ? PMEDIA_CODEC_PCM : PMEDIA_CODEC_IMA_ADPCM;
    session->info.has_audio = 1;
    session->info.sample_rate = (int)rate;
    session->info.channels = (int)channels;
    session->info.bits_per_sample = (int)bits;
    session->info.file_size = session->input_bytes;
    if (tag == 1U) {
        session->audio_total_frames = data_bytes / (int)(channels * (bits / 8U));
    } else {
        session->audio_total_frames =
            ((data_bytes + (int)block_align - 1) / (int)block_align) * (int)samples_per_block;
    }
    session->info.duration_us =
        ((__int64)session->audio_total_frames * (__int64)1000000) / (__int64)rate;
    return PMEDIA_OK;
}

static int pm_is_adts(const unsigned char *data, int bytes)
{
    return bytes >= 2 && data[0] == 0xff && (data[1] & 0xf6) == 0xf0;
}

static void pm_set_masks(pm_session_impl *session)
{
    unsigned int container_bit;
    unsigned int codec_bit;

    container_bit = session->info.container > 0 && session->info.container < 31 ?
                    1U << (session->info.container - 1) : 0;
    codec_bit = session->info.audio_codec > 0 && session->info.audio_codec < 31 ?
                1U << (session->info.audio_codec - 1) : 0;
    if (session->info.video_codec > 0 && session->info.video_codec < 31) {
        codec_bit |= 1U << (session->info.video_codec - 1);
    }
    session->capabilities.container_mask = container_bit;
    session->capabilities.codec_mask = codec_bit;
}

static int pm_parse_input(pm_session_impl *session)
{
    int result;

    pm_zero(&session->info, (int)sizeof(session->info));
    session->info.size = sizeof(session->info);
    if (session->input_bytes >= 12 &&
        pm_fourcc(session->input, 'R', 'I', 'F', 'F') &&
        pm_fourcc(session->input + 8, 'W', 'A', 'V', 'E')) {
        result = pm_parse_wav(session);
        if (result != PMEDIA_OK) {
            return result;
        }
    } else if (session->input_bytes >= 12 &&
               pm_fourcc(session->input, 'R', 'I', 'F', 'F') &&
               pm_fourcc(session->input + 8, 'A', 'V', 'I', ' ')) {
        session->info.container = PMEDIA_CONTAINER_AVI;
        session->info.file_size = session->input_bytes;
    } else if (session->input_bytes >= 8 &&
               (pm_fourcc(session->input + 4, 'f', 't', 'y', 'p') ||
                pm_fourcc(session->input + 4, 's', 't', 'y', 'p'))) {
        session->info.container = PMEDIA_CONTAINER_MP4;
        session->info.file_size = session->input_bytes;
    } else if (session->input_bytes >= 4 &&
               pm_fourcc(session->input, 'F', 'L', 'V', 1)) {
        session->info.container = PMEDIA_CONTAINER_FLV;
        session->info.file_size = session->input_bytes;
    } else if (session->input_bytes >= 5 &&
               pm_fourcc(session->input, '#', '!', 'A', 'M') &&
               (session->input[4] == '\n' || session->input[4] == 'W')) {
        session->info.container = PMEDIA_CONTAINER_AMR;
        session->info.audio_codec = PMEDIA_CODEC_AMR_NB;
        session->info.has_audio = 1;
        session->info.file_size = session->input_bytes;
    } else if (pm_is_adts(session->input, session->input_bytes)) {
        session->info.container = PMEDIA_CONTAINER_RAW;
        session->info.audio_codec = PMEDIA_CODEC_AAC_LC;
        session->info.has_audio = 1;
        session->info.file_size = session->input_bytes;
    } else if (session->input_bytes >= 4 &&
               session->input[0] == 0 && session->input[1] == 0 &&
               session->input[2] == 1 && session->input[3] == 0x67) {
        session->info.container = PMEDIA_CONTAINER_RAW;
        session->info.video_codec = PMEDIA_CODEC_H264;
        session->info.has_video = 1;
        session->info.file_size = session->input_bytes;
    } else if (session->input_bytes >= 189 &&
               ((session->input[0] == 0 && session->input[1] == 0 &&
                 session->input[2] == 1 && session->input[3] == 0xba) ||
                (session->input[0] == 0x47 && session->input[188] == 0x47))) {
        session->info.container = session->input[0] == 0x47 ?
                                  PMEDIA_CONTAINER_MPEG_TS : PMEDIA_CONTAINER_MPEG_PS;
        session->info.file_size = session->input_bytes;
    } else if (session->input_bytes >= 3 &&
               pm_fourcc(session->input, 'I', 'D', '3', 0)) {
        session->info.container = PMEDIA_CONTAINER_RAW;
        session->info.audio_codec = PMEDIA_CODEC_MP3;
        session->info.has_audio = 1;
        session->info.file_size = session->input_bytes;
    } else {
        return PMEDIA_ERROR_FORMAT;
    }

    pm_zero(&session->capabilities, (int)sizeof(session->capabilities));
    session->capabilities.size = sizeof(session->capabilities);
    session->capabilities.abi_version = PMEDIA_ABI_VERSION;
    session->capabilities.native_graph_available = pmedia_system_probe();
    session->capabilities.native_callback_source_available = 0;
    session->capabilities.soft_audio_available =
        session->info.container == PMEDIA_CONTAINER_WAV && session->info.has_audio;
    session->capabilities.soft_video_available = 0;
    /* The current soft paths retain a bounded memory copy, so they do not
     * require a seekable host source.  Native graph playback will advertise
     * its own requirement when a callback-backed source filter is added. */
    session->capabilities.requires_seek = 0;
    session->capabilities.max_video_width = 640;
    session->capabilities.max_video_height = 480;
    pm_set_masks(session);
    return PMEDIA_OK;
}

static int pm_probe_source(const pm_source_callbacks *source,
                           pm_probe_info *out_info)
{
    pm_session_impl probe;
    pm_position original_position;
    pm_position restored_position;
    int restore_position;
    int result;

    pm_zero(&probe, (int)sizeof(probe));
    result = pm_copy_source(&probe.source, source);
    if (result != PMEDIA_OK) {
        return result;
    }
    original_position = 0;
    restore_position = source->tell != NULL &&
                      source->tell(source->context, &original_position) == PMEDIA_OK;
    pm_copy_output(&probe.output, NULL);
    result = pm_load_input(&probe);
    if (result == PMEDIA_OK) {
        result = pm_parse_input(&probe);
    }
#ifdef PMEDIA_HAS_FFMPEG
    if (probe.input != NULL &&
        (result != PMEDIA_OK || probe.info.container != PMEDIA_CONTAINER_WAV)) {
        pm_stream_info ff_info;
        pm_capabilities ff_capabilities;
        char ff_error[PMEDIA_ERROR_TEXT];
        int ff_result;

        pm_zero(&ff_info, (int)sizeof(ff_info));
        pm_zero(&ff_capabilities, (int)sizeof(ff_capabilities));
        pm_zero(ff_error, (int)sizeof(ff_error));
        ff_result = pmedia_ffmpeg_probe(probe.input, probe.input_bytes,
                                        &ff_info, &ff_capabilities,
                                        ff_error, sizeof(ff_error));
        if (ff_result == PMEDIA_OK) {
            memcpy(&probe.info, &ff_info, sizeof(ff_info));
            memcpy(&probe.capabilities, &ff_capabilities,
                   sizeof(ff_capabilities));
            result = PMEDIA_OK;
        }
    }
#endif
    if (result == PMEDIA_OK && out_info != NULL) {
        pm_zero(out_info, (int)sizeof(*out_info));
        out_info->size = sizeof(*out_info);
        memcpy(&out_info->stream, &probe.info, sizeof(probe.info));
        memcpy(&out_info->capabilities, &probe.capabilities, sizeof(probe.capabilities));
    }
    if (probe.input != NULL) {
        free(probe.input);
    }
    if (restore_position && source->seek != NULL) {
        restored_position = original_position;
        source->seek(source->context, original_position, PMEDIA_SEEK_SET,
                     &restored_position);
    }
    return result;
}

static int pm_ima_clip(int value)
{
    if (value < -32768) {
        return -32768;
    }
    if (value > 32767) {
        return 32767;
    }
    return value;
}

static int pm_decode_ima_block(pm_session_impl *session,
                               const unsigned char *block,
                               int block_bytes,
                               short *output,
                               int output_capacity_frames)
{
    static const int index_table[16] = {
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    };
    static const int step_table[89] = {
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28,
        31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
        118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337,
        371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
        1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
        3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
        7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
        18500, 20350, 22385, 24623, 27086, 29794, 32767
    };
    int channels;
    int header_bytes;
    int bytes_per_channel;
    int channel;
    int i;
    int nibble;
    int step;
    int difference;
    int predictor[2];
    int index[2];
    int sample;
    int frames;

    channels = session->info.channels;
    header_bytes = channels * 4;
    if (channels < 1 || channels > 2 || block_bytes < header_bytes ||
        session->block_align < header_bytes || output_capacity_frames < 1) {
        return PMEDIA_ERROR_FORMAT;
    }
    bytes_per_channel = (block_bytes - header_bytes) / channels;
    frames = 1 + bytes_per_channel * 2;
    if (frames > session->samples_per_block) {
        frames = session->samples_per_block;
    }
    if (frames > output_capacity_frames) {
        frames = output_capacity_frames;
    }
    for (channel = 0; channel < channels; channel++) {
        predictor[channel] = (short)pm_le16(block + channel * 4);
        index[channel] = block[channel * 4 + 2];
        if (index[channel] > 88) {
            index[channel] = 88;
        }
        output[channel] = (short)predictor[channel];
    }
    for (i = 1; i < frames; i++) {
        for (channel = 0; channel < channels; channel++) {
            int byte_index;
            int channel_nibble;

            byte_index = (i - 1) / 2;
            channel_nibble = (i - 1) & 1;
            nibble = block[header_bytes + channel * bytes_per_channel + byte_index];
            if (channel_nibble == 0) {
                nibble &= 0x0f;
            } else {
                nibble >>= 4;
            }
            sample = predictor[channel];
            step = step_table[index[channel]];
            difference = step >> 3;
            if (nibble & 1) difference += step >> 2;
            if (nibble & 2) difference += step >> 1;
            if (nibble & 4) difference += step;
            if (nibble & 8) sample -= difference;
            else sample += difference;
            sample = pm_ima_clip(sample);
            predictor[channel] = sample;
            output[i * channels + channel] = (short)sample;
            index[channel] += index_table[nibble];
            if (index[channel] < 0) index[channel] = 0;
            if (index[channel] > 88) index[channel] = 88;
        }
    }
    return frames;
}

static int pm_output_pcm(pm_session_impl *session, int frames)
{
    const unsigned char *input;
    int channels;
    int bits;
    int frame_bytes;
    int i;
    int channel;
    short *samples;
    pm_audio_block block;
    int callback_result;

    channels = session->info.channels;
    bits = session->info.bits_per_sample;
    samples = (short *)session->audio_buffer;
    input = session->input + session->audio_data_offset +
            session->audio_cursor * channels * (bits / 8);
    frame_bytes = channels * (bits / 8);
    if (session->format_tag == 1) {
        if (bits == 16) {
            for (i = 0; i < frames; i++) {
                for (channel = 0; channel < channels; channel++) {
                    const unsigned char *source;
                    source = input + i * frame_bytes + channel * 2;
                    samples[i * channels + channel] = (short)pm_le16(source);
                }
            }
        } else {
            for (i = 0; i < frames; i++) {
                for (channel = 0; channel < channels; channel++) {
                    samples[i * channels + channel] =
                        (short)(((int)input[i * frame_bytes + channel] - 128) << 8);
                }
            }
        }
    } else {
        int block_offset;
        int block_bytes;
        block_offset = session->audio_data_offset +
                       (session->audio_cursor / session->samples_per_block) * session->block_align;
        block_bytes = session->block_align;
        if (block_offset + block_bytes > session->audio_data_offset + session->audio_data_bytes) {
            block_bytes = session->audio_data_offset + session->audio_data_bytes - block_offset;
        }
        frames = pm_decode_ima_block(session, session->input + block_offset,
                                     block_bytes, samples, PMEDIA_AUDIO_FRAMES);
        if (frames < 1) {
            return PMEDIA_ERROR_FORMAT;
        }
    }
    pm_zero(&block, (int)sizeof(block));
    block.size = sizeof(block);
    block.data = session->audio_buffer;
    block.bytes = frames * channels * (int)sizeof(short);
    block.samples = frames;
    block.sample_rate = session->info.sample_rate;
    block.channels = channels;
    block.pts_us = ((__int64)session->audio_cursor * (__int64)1000000) /
                   (__int64)session->info.sample_rate;
    block.duration_us = ((__int64)frames * (__int64)1000000) /
                        (__int64)session->info.sample_rate;
    if (session->output.audio != NULL) {
        callback_result = session->output.audio(session->output.context, &block);
        if (callback_result < 0) {
            pm_set_error(session, PMEDIA_ERROR_CALLBACK, "audio callback failed");
            return PMEDIA_ERROR_CALLBACK;
        }
        if (callback_result == PMEDIA_CALLBACK_STOP) {
            session->paused = 1;
        }
    }
    session->audio_cursor += frames;
    if (session->audio_cursor > session->audio_total_frames) {
        session->audio_cursor = session->audio_total_frames;
    }
    return PMEDIA_OK;
}

PMEDIA_API unsigned long pm_abi_version(void)
{
    return PMEDIA_ABI_VERSION;
}

PMEDIA_API int pm_probe(const pm_source_callbacks *source, pm_probe_info *out_info)
{
    if (out_info == NULL || out_info->size < sizeof(*out_info)) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    return pm_probe_source(source, out_info);
}

PMEDIA_API int pm_open(const pm_source_callbacks *source,
            const pm_open_options *options,
            const pm_output_callbacks *output,
            pm_session *out_session)
{
    pm_session_impl *session;
    int requested_backend;
    int result;
    int max_video_width;
    int max_video_height;

    if (out_session == NULL || *out_session != NULL || source == NULL) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    if (options != NULL && options->size < sizeof(*options)) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    if (output != NULL && output->size != 0 &&
        output->size < sizeof(*output)) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    session = (pm_session_impl *)calloc(1, sizeof(*session));
    if (session == NULL) {
        return PMEDIA_ERROR_MEMORY;
    }
    result = pm_copy_source(&session->source, source);
    if (result != PMEDIA_OK) {
        free(session);
        return result;
    }
    pm_copy_output(&session->output, output);
    requested_backend = options == NULL ? PMEDIA_BACKEND_AUTO : options->backend;
    if (requested_backend == PMEDIA_BACKEND_NONE) {
        requested_backend = PMEDIA_BACKEND_AUTO;
    }
    if (requested_backend < PMEDIA_BACKEND_AUTO || requested_backend > PMEDIA_BACKEND_SOFT) {
        free(session);
        return PMEDIA_ERROR_ARGUMENT;
    }
    max_video_width = options == NULL ? 640 : options->max_video_width;
    max_video_height = options == NULL ? 480 : options->max_video_height;
    if (max_video_width <= 0) max_video_width = 640;
    if (max_video_height <= 0) max_video_height = 480;
    result = pm_load_input(session);
    if (result == PMEDIA_OK) {
        result = pm_parse_input(session);
    }
    if (requested_backend != PMEDIA_BACKEND_SOFT &&
        result == PMEDIA_OK &&
        session->info.container == PMEDIA_CONTAINER_WAV &&
        session->format_tag == 1) {
        pmedia_waveout *waveout;
        char native_error[PMEDIA_ERROR_TEXT];
        int native_result;

        waveout = NULL;
        pm_zero(native_error, (int)sizeof(native_error));
        native_result = pmedia_waveout_open(
            session->input,
            session->input_bytes,
            session->audio_data_offset,
            session->audio_data_bytes,
            session->info.sample_rate,
            session->info.channels,
            session->info.bits_per_sample,
            session->audio_total_frames,
            &session->output,
            &waveout,
            native_error,
            sizeof(native_error));
        if (native_result == PMEDIA_OK) {
            session->waveout = waveout;
            session->waveout_active = 1;
            session->capabilities.native_waveout_available = 1;
            session->backend = PMEDIA_BACKEND_NATIVE;
            pm_emit(session, PMEDIA_EVENT_FORMAT, session->info.container);
            pm_emit(session, PMEDIA_EVENT_OPENED, session->backend);
            *out_session = (pm_session)session;
            return PMEDIA_OK;
        }
        if (requested_backend == PMEDIA_BACKEND_NATIVE) {
            pm_set_error(session, native_result,
                         native_error[0] == '\0' ?
                         "WaveOut rejected the PCM stream" : native_error);
            free(session->input);
            free(session);
            return native_result;
        }
    }
    if (requested_backend == PMEDIA_BACKEND_NATIVE) {
        if (result != PMEDIA_OK) {
            if (session->input != NULL) free(session->input);
            free(session);
            return result;
        }
        pm_set_error(session, PMEDIA_ERROR_NATIVE,
                     "DirectShow callback source adapter is not available in this build");
        free(session->input);
        free(session);
        return PMEDIA_ERROR_NATIVE;
    }
#ifdef PMEDIA_HAS_FFMPEG
    if (!(result == PMEDIA_OK && session->info.container == PMEDIA_CONTAINER_WAV)) {
        pmedia_ffmpeg *ffmpeg;
        pm_stream_info ff_info;
        pm_capabilities ff_capabilities;
        char ff_error[PMEDIA_ERROR_TEXT];
        int ff_result;

        ffmpeg = NULL;
        pm_zero(&ff_info, (int)sizeof(ff_info));
        pm_zero(&ff_capabilities, (int)sizeof(ff_capabilities));
        pm_zero(ff_error, (int)sizeof(ff_error));
        ff_result = pmedia_ffmpeg_open(session->input, session->input_bytes,
                                       &session->output,
                                       max_video_width, max_video_height,
                                       &ffmpeg, &ff_info, &ff_capabilities,
                                       ff_error, sizeof(ff_error));
        if (ff_result == PMEDIA_OK) {
            ff_capabilities.native_graph_available =
                session->capabilities.native_graph_available;
            ff_capabilities.native_callback_source_available = 0;
            session->info = ff_info;
            session->capabilities = ff_capabilities;
            session->ffmpeg = ffmpeg;
            session->ffmpeg_active = 1;
            session->backend = PMEDIA_BACKEND_SOFT;
            if (requested_backend == PMEDIA_BACKEND_AUTO &&
                session->capabilities.native_graph_available) {
                pm_emit(session, PMEDIA_EVENT_BACKEND_FALLBACK,
                        PMEDIA_BACKEND_SOFT);
            }
            pm_emit(session, PMEDIA_EVENT_FORMAT, session->info.container);
            pm_emit(session, PMEDIA_EVENT_OPENED, session->backend);
            *out_session = (pm_session)session;
            return PMEDIA_OK;
        }
        if (requested_backend == PMEDIA_BACKEND_SOFT || result != PMEDIA_OK) {
            pm_set_error(session, ff_result,
                         ff_error[0] == '\0' ?
                         "FFmpeg software backend rejected this stream" : ff_error);
            if (session->input != NULL) free(session->input);
            free(session);
            return ff_result;
        }
    }
#endif
    if (result != PMEDIA_OK) {
        if (session->input != NULL) free(session->input);
        free(session);
        return result;
    }
    if (requested_backend == PMEDIA_BACKEND_SOFT &&
        !session->capabilities.soft_audio_available) {
        pm_set_error(session, PMEDIA_ERROR_UNSUPPORTED,
                     "selected software decoder is not compiled for this stream");
        free(session->input);
        free(session);
        return PMEDIA_ERROR_UNSUPPORTED;
    }
    if (requested_backend == PMEDIA_BACKEND_AUTO &&
        session->capabilities.native_graph_available &&
        session->capabilities.native_callback_source_available == 0) {
        pm_emit(session, PMEDIA_EVENT_BACKEND_FALLBACK, PMEDIA_BACKEND_SOFT);
    }
    if (!session->capabilities.soft_audio_available) {
        pm_set_error(session, PMEDIA_ERROR_UNSUPPORTED,
                     "no compiled backend can consume this stream");
        free(session->input);
        free(session);
        return PMEDIA_ERROR_UNSUPPORTED;
    }
    session->backend = PMEDIA_BACKEND_SOFT;
    session->audio_buffer_bytes = PMEDIA_AUDIO_FRAMES * 2 * (int)sizeof(short);
    session->audio_buffer = (unsigned char *)malloc((size_t)session->audio_buffer_bytes);
    if (session->audio_buffer == NULL) {
        free(session->input);
        free(session);
        return PMEDIA_ERROR_MEMORY;
    }
    pm_emit(session, PMEDIA_EVENT_FORMAT, session->info.container);
    pm_emit(session, PMEDIA_EVENT_OPENED, session->backend);
    *out_session = (pm_session)session;
    return PMEDIA_OK;
}

PMEDIA_API int pm_close(pm_session opaque_session)
{
    pm_session_impl *session;

    session = (pm_session_impl *)opaque_session;
    if (session == NULL) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    session->output.audio = NULL;
    session->output.video = NULL;
    session->output.event = NULL;
    session->output.error = NULL;
#ifdef PMEDIA_HAS_FFMPEG
    if (session->ffmpeg_active && session->ffmpeg != NULL) {
        pmedia_ffmpeg_close((pmedia_ffmpeg *)session->ffmpeg);
        session->ffmpeg = NULL;
        session->ffmpeg_active = 0;
    }
#endif
    if (session->waveout_active && session->waveout != NULL) {
        pmedia_waveout_close(session->waveout);
        session->waveout = NULL;
        session->waveout_active = 0;
    }
    if (session->audio_buffer != NULL) free(session->audio_buffer);
    if (session->input != NULL) free(session->input);
    free(session);
    return PMEDIA_OK;
}

PMEDIA_API int pm_pump(pm_session opaque_session, pm_position clock_us, int budget_us)
{
    pm_session_impl *session;
    int remaining;
    int frames;
    int native_frames;
    int result;

    (void)clock_us;
    session = (pm_session_impl *)opaque_session;
    if (session == NULL) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    if (session->stopped) {
        return PMEDIA_ERROR_STATE;
    }
    if (session->paused) {
        return PMEDIA_OK;
    }
    if (session->waveout_active && session->waveout != NULL) {
        native_frames = 0;
        if (budget_us > 0 && session->info.sample_rate > 0) {
            native_frames = (budget_us * session->info.sample_rate) / 1000000;
            if (native_frames < 1) native_frames = 1;
        }
        result = pmedia_waveout_pump(session->waveout, native_frames);
        if (result == PMEDIA_CALLBACK_STOP) {
            session->paused = 1;
            return PMEDIA_OK;
        }
        if (result == PMEDIA_EOF) {
            if (!session->eof_sent) {
                session->eof_sent = 1;
                pm_emit(session, PMEDIA_EVENT_EOF, 0);
            }
        } else if (result < 0) {
            pm_set_error(session, result, "WaveOut pump failed");
        }
        return result;
    }
#ifdef PMEDIA_HAS_FFMPEG
    if (session->ffmpeg_active && session->ffmpeg != NULL) {
        int ff_result;
        ff_result = pmedia_ffmpeg_pump((pmedia_ffmpeg *)session->ffmpeg,
                                       clock_us, budget_us);
        if (ff_result == PMEDIA_CALLBACK_STOP) {
            session->paused = 1;
            return PMEDIA_OK;
        }
        if (ff_result == PMEDIA_EOF) {
            if (!session->eof_sent) {
                session->eof_sent = 1;
                pm_emit(session, PMEDIA_EVENT_EOF, 0);
            }
        } else if (ff_result < 0) {
            pm_set_error(session, ff_result, "FFmpeg software pump failed");
        }
        return ff_result;
    }
#endif
    if (session->backend != PMEDIA_BACKEND_SOFT || !session->info.has_audio) {
        return PMEDIA_ERROR_UNSUPPORTED;
    }
    remaining = session->audio_total_frames - session->audio_cursor;
    if (remaining <= 0) {
        if (!session->eof_sent) {
            session->eof_sent = 1;
            pm_emit(session, PMEDIA_EVENT_EOF, 0);
        }
        return PMEDIA_EOF;
    }
    frames = remaining;
    if (frames > PMEDIA_AUDIO_FRAMES) {
        frames = PMEDIA_AUDIO_FRAMES;
    }
    if (session->format_tag == 0x0011) {
        frames = session->samples_per_block;
        if (frames > remaining) frames = remaining;
    }
    return pm_output_pcm(session, frames);
}

PMEDIA_API int pm_pause(pm_session opaque_session)
{
    pm_session_impl *session;
    int result;
    session = (pm_session_impl *)opaque_session;
    if (session == NULL) return PMEDIA_ERROR_ARGUMENT;
    if (session->stopped) return PMEDIA_ERROR_STATE;
    if (session->waveout_active && session->waveout != NULL) {
        result = pmedia_waveout_pause(session->waveout);
        if (result != PMEDIA_OK) return result;
    }
    session->paused = 1;
    return PMEDIA_OK;
}

PMEDIA_API int pm_resume(pm_session opaque_session)
{
    pm_session_impl *session;
    int result;
    session = (pm_session_impl *)opaque_session;
    if (session == NULL) return PMEDIA_ERROR_ARGUMENT;
    if (session->stopped) return PMEDIA_ERROR_STATE;
    if (session->waveout_active && session->waveout != NULL) {
        result = pmedia_waveout_resume(session->waveout);
        if (result != PMEDIA_OK) return result;
    }
    session->paused = 0;
    return PMEDIA_OK;
}

PMEDIA_API int pm_stop(pm_session opaque_session)
{
    pm_session_impl *session;
    int result;
    session = (pm_session_impl *)opaque_session;
    if (session == NULL) return PMEDIA_ERROR_ARGUMENT;
    if (session->stopped) return PMEDIA_OK;
    if (session->waveout_active && session->waveout != NULL) {
        result = pmedia_waveout_stop(session->waveout);
        if (result != PMEDIA_OK) return result;
    }
    session->stopped = 1;
    pm_emit(session, PMEDIA_EVENT_STOPPED, 0);
    return PMEDIA_OK;
}

PMEDIA_API int pm_seek(pm_session opaque_session, pm_position position_us)
{
    pm_session_impl *session;
    pm_position frame;
    int result;

    session = (pm_session_impl *)opaque_session;
    if (session == NULL || position_us < 0) return PMEDIA_ERROR_ARGUMENT;
    if (session->stopped) return PMEDIA_ERROR_STATE;
#ifdef PMEDIA_HAS_FFMPEG
    if (session->ffmpeg_active && session->ffmpeg != NULL) {
        return pmedia_ffmpeg_seek((pmedia_ffmpeg *)session->ffmpeg,
                                  position_us);
    }
#endif
    if (session->waveout_active && session->waveout != NULL) {
        result = pmedia_waveout_seek(session->waveout, position_us);
        if (result == PMEDIA_OK) session->eof_sent = 0;
        return result;
    }
    if (session->backend != PMEDIA_BACKEND_SOFT || !session->info.has_audio) {
        return PMEDIA_ERROR_NOT_SEEKABLE;
    }
    frame = (position_us * (__int64)session->info.sample_rate) / (__int64)1000000;
    if (frame > session->audio_total_frames) frame = session->audio_total_frames;
    session->audio_cursor = (int)frame;
    session->eof_sent = 0;
    return PMEDIA_OK;
}

PMEDIA_API int pm_get_stream_info(pm_session opaque_session, pm_stream_info *out_info)
{
    pm_session_impl *session;
    if (opaque_session == NULL || out_info == NULL || out_info->size < sizeof(*out_info)) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    session = (pm_session_impl *)opaque_session;
    memcpy(out_info, &session->info, sizeof(*out_info));
    return PMEDIA_OK;
}

PMEDIA_API int pm_get_capabilities(pm_session opaque_session, pm_capabilities *out_capabilities)
{
    pm_session_impl *session;
    if (opaque_session == NULL || out_capabilities == NULL ||
        out_capabilities->size < sizeof(*out_capabilities)) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    session = (pm_session_impl *)opaque_session;
    memcpy(out_capabilities, &session->capabilities, sizeof(*out_capabilities));
    return PMEDIA_OK;
}

PMEDIA_API int pm_get_backend(pm_session opaque_session)
{
    pm_session_impl *session;
    session = (pm_session_impl *)opaque_session;
    if (session == NULL) return PMEDIA_BACKEND_NONE;
    return session->backend;
}

PMEDIA_API const char *pm_last_error(pm_session opaque_session)
{
    pm_session_impl *session;
    session = (pm_session_impl *)opaque_session;
    if (session == NULL) return "invalid media session";
    return session->error_text;
}

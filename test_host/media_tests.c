#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdio.h>

#include "positron_media.h"

typedef struct media_fixture_source {
    const unsigned char *data;
    int bytes;
    int position;
    int read_mode;
    int seek_mode;
    int read_chunk;
} media_fixture_source;

typedef struct media_fixture_output {
    int blocks;
    int samples;
    int errors;
    int channels;
    int callback_result;
    int eof_events;
    int error_events;
    int error_callbacks;
    int last_error;
    int stopped_events;
    short pcm[16];
    int pcm_count;
    pm_position pts_us;
    pm_position duration_us;
} media_fixture_output;

static const unsigned char g_media_fixture_wav[] = {
    'R','I','F','F', 44,0,0,0, 'W','A','V','E',
    'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x40,0x1f,0,0,
    0x80,0x3e,0,0, 2,0, 16,0,
    'd','a','t','a', 8,0,0,0, 0,0, 0,0, 0,0, 0,0
};

static int media_fixture_read(void *context,
                              unsigned char *destination,
                              int capacity,
                              int *out_read)
{
    media_fixture_source *source;
    int amount;

    source = (media_fixture_source *)context;
    if (source == NULL || destination == NULL || out_read == NULL ||
        capacity < 0) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    amount = source->bytes - source->position;
    *out_read = 0;
    if (source->read_mode == 1) return PMEDIA_WOULD_BLOCK;
    if (source->read_mode == 2) return PMEDIA_ERROR_IO;
    if (source->read_mode == 3) {
        *out_read = capacity + 1;
        return PMEDIA_OK;
    }
    if (source->read_mode == 4) return PMEDIA_OK;
    if (amount > capacity) amount = capacity;
    if (amount > (source->read_chunk > 0 ? source->read_chunk : 3)) {
        amount = source->read_chunk > 0 ? source->read_chunk : 3;
    }
    if (amount > 0) {
        memcpy(destination, source->data + source->position, (size_t)amount);
        source->position += amount;
    }
    *out_read = amount;
    return source->position == source->bytes ? PMEDIA_EOF : PMEDIA_OK;
}

static int media_fixture_seek(void *context,
                              pm_position offset,
                              int origin,
                              pm_position *out_position)
{
    media_fixture_source *source;
    pm_position position;

    source = (media_fixture_source *)context;
    if (source == NULL || out_position == NULL) return PMEDIA_ERROR_ARGUMENT;
    if (source->seek_mode == 1 || (source->seek_mode == 2 && offset != 0)) {
        return PMEDIA_ERROR_IO;
    }
    if (origin == PMEDIA_SEEK_SET) position = offset;
    else if (origin == PMEDIA_SEEK_CUR) position = source->position + offset;
    else if (origin == PMEDIA_SEEK_END) position = source->bytes + offset;
    else return PMEDIA_ERROR_ARGUMENT;
    if (position < 0 || position > source->bytes) return PMEDIA_ERROR_IO;
    source->position = (int)position;
    *out_position = position;
    return PMEDIA_OK;
}

static int media_fixture_tell(void *context, pm_position *out_position)
{
    media_fixture_source *source;
    if (context == NULL || out_position == NULL) return PMEDIA_ERROR_ARGUMENT;
    source = (media_fixture_source *)context;
    *out_position = source->position;
    return PMEDIA_OK;
}

static int media_fixture_size(void *context, pm_position *out_size)
{
    media_fixture_source *source;
    if (context == NULL || out_size == NULL) return PMEDIA_ERROR_ARGUMENT;
    source = (media_fixture_source *)context;
    *out_size = source->bytes;
    return PMEDIA_OK;
}

static int media_fixture_audio(void *context, const pm_audio_block *block)
{
    media_fixture_output *output;
    int i;
    int channels;

    output = (media_fixture_output *)context;
    channels = output != NULL && output->channels != 0 ? output->channels : 1;
    if (output == NULL || block == NULL || block->sample_rate != 8000 ||
        block->channels != channels || block->bytes != block->samples * channels * 2 ||
        block->samples <= 0 || block->data == NULL) {
        if (output != NULL) output->errors++;
        return PMEDIA_ERROR_CALLBACK;
    }
    output->blocks++;
    output->samples += block->samples;
    output->pts_us = block->pts_us;
    output->duration_us = block->duration_us;
    for (i = 0; i < block->samples * channels && output->pcm_count < 16; i++) {
        output->pcm[output->pcm_count++] = (short)
            ((unsigned int)block->data[i * 2] |
             ((unsigned int)block->data[i * 2 + 1] << 8));
    }
    return output->callback_result;
}

static void media_fixture_event(void *context, int event, int value)
{
    media_fixture_output *output;
    output = (media_fixture_output *)context;
    if (event == PMEDIA_EVENT_EOF) output->eof_events++;
    if (event == PMEDIA_EVENT_STOPPED) output->stopped_events++;
    if (event == PMEDIA_EVENT_ERROR) {
        output->error_events++;
        output->last_error = value;
    }
}

static void media_fixture_error(void *context, int error, const char *message)
{
    media_fixture_output *output;
    output = (media_fixture_output *)context;
    output->error_callbacks++;
    output->last_error = error;
    if (message == NULL || message[0] == '\0') output->errors++;
}

static void media_fixture_source_init(media_fixture_source *source)
{
    memset(source, 0, sizeof(*source));
    source->data = g_media_fixture_wav;
    source->bytes = sizeof(g_media_fixture_wav);
}

BOOL test1312_media_wav_callback_contract(void)
{
    media_fixture_source source_data;
    media_fixture_output output_data;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_stream_info info;
    pm_capabilities capabilities;
    pm_session session;
    pm_session auto_session;
    int result;
    BOOL ok;

    media_fixture_source_init(&source_data);
    memset(&source, 0, sizeof(source));
    source.size = sizeof(source);
    source.context = &source_data;
    source.read = media_fixture_read;
    source.seek = media_fixture_seek;
    source.tell = media_fixture_tell;
    source.size_callback = media_fixture_size;
    source_data.position = 1;
    memset(&probe, 0, sizeof(probe));
    probe.size = sizeof(probe);
    result = pm_probe(&source, &probe);
    ok = result == PMEDIA_OK && source_data.position == 1 &&
         probe.stream.container == PMEDIA_CONTAINER_WAV &&
         probe.stream.audio_codec == PMEDIA_CODEC_PCM &&
         probe.capabilities.soft_audio_available != 0;
    if (!ok) return FALSE;

    source_data.position = 0;
    memset(&output_data, 0, sizeof(output_data));
    memset(&output, 0, sizeof(output));
    output.size = sizeof(output);
    output.context = &output_data;
    output.audio = media_fixture_audio;
    memset(&options, 0, sizeof(options));
    options.size = sizeof(options);
    options.backend = PMEDIA_BACKEND_SOFT;
    session = NULL;
    result = pm_open(&source, &options, &output, &session);
    if (result != PMEDIA_OK || session == NULL) return FALSE;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    memset(&capabilities, 0, sizeof(capabilities));
    capabilities.size = sizeof(capabilities);
    ok = pm_get_stream_info(session, &info) == PMEDIA_OK &&
         pm_get_capabilities(session, &capabilities) == PMEDIA_OK &&
         pm_get_backend(session) == PMEDIA_BACKEND_SOFT &&
         info.sample_rate == 8000 && capabilities.soft_audio_available != 0;
    if (ok) ok = pm_pause(session) == PMEDIA_OK &&
        pm_pump(session, 0, 1000) == PMEDIA_OK && output_data.blocks == 0;
    if (ok) ok = pm_resume(session) == PMEDIA_OK &&
        pm_pump(session, 0, 1000) == PMEDIA_OK &&
        output_data.samples == 4 && output_data.errors == 0;
    if (ok) ok = pm_seek(session, 0) == PMEDIA_OK &&
        pm_pump(session, 0, 1000) == PMEDIA_OK &&
        output_data.samples == 8 && pm_stop(session) == PMEDIA_OK &&
        pm_pump(session, 0, 1000) == PMEDIA_ERROR_STATE;
    result = pm_close(session);
    if (!ok || result != PMEDIA_OK) return FALSE;

    media_fixture_source_init(&source_data);
    auto_session = NULL;
    memset(&options, 0, sizeof(options));
    options.size = sizeof(options);
    options.backend = PMEDIA_BACKEND_AUTO;
    result = pm_open(&source, &options, &output, &auto_session);
    if (result != PMEDIA_OK || auto_session == NULL) return FALSE;
    ok = pm_get_backend(auto_session) == PMEDIA_BACKEND_NATIVE ||
         pm_get_backend(auto_session) == PMEDIA_BACKEND_SOFT;
    if (pm_close(auto_session) != PMEDIA_OK) ok = FALSE;
    return ok;
}

static const unsigned char g_media_fixture_u8[] = {
    'R','I','F','F', 40,0,0,0, 'W','A','V','E',
    'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x40,0x1f,0,0,
    0x40,0x1f,0,0, 1,0, 8,0,
    'd','a','t','a', 4,0,0,0, 0,128,255,64
};

static const unsigned char g_media_fixture_stereo[] = {
    'R','I','F','F', 44,0,0,0, 'W','A','V','E',
    'f','m','t',' ', 16,0,0,0, 1,0, 2,0, 0x40,0x1f,0,0,
    0x00,0x7d,0,0, 4,0, 16,0,
    'd','a','t','a', 8,0,0,0, 0,128,255,127, 0,0,1,0
};

static const char *g_media_contract_error = "media contract not run";

const char *test1331_media_last_error(void)
{
    return g_media_contract_error;
}

static void media_contract_init(media_fixture_source *input,
                                media_fixture_output *sink,
                                pm_source_callbacks *source,
                                pm_output_callbacks *output,
                                pm_open_options *options)
{
    media_fixture_source_init(input);
    memset(sink, 0, sizeof(*sink));
    memset(source, 0, sizeof(*source));
    source->size = sizeof(*source);
    source->context = input;
    source->read = media_fixture_read;
    source->seek = media_fixture_seek;
    source->tell = media_fixture_tell;
    source->size_callback = media_fixture_size;
    memset(output, 0, sizeof(*output));
    output->size = sizeof(*output);
    output->context = sink;
    output->audio = media_fixture_audio;
    output->event = media_fixture_event;
    output->error = media_fixture_error;
    memset(options, 0, sizeof(*options));
    options->size = sizeof(*options);
    options->backend = PMEDIA_BACKEND_SOFT;
}

static BOOL media_contract_drain(pm_session session)
{
    DWORD start;
    int result;
    start = GetTickCount();
    do {
        result = pm_pump(session, (pm_position)GetTickCount() * 1000, 2000);
        if (result == PMEDIA_EOF) return TRUE;
        if (result != PMEDIA_OK && result != PMEDIA_WOULD_BLOCK) return FALSE;
        Sleep(1);
    } while (GetTickCount() - start < 5000);
    return FALSE;
}

BOOL test1331_media_io_pcm_contract(void (*progress)(const char *))
{
    media_fixture_source input;
    media_fixture_output sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_probe_info unchanged;
    pm_session session;
    unsigned char damaged[sizeof(g_media_fixture_stereo)];
    int mode;
    int backend;
    int expected;
    int result;
    int iteration;
    BOOL ok;

    session = NULL;
    g_media_contract_error = "ABI/null guards";
    if (progress != NULL) progress(g_media_contract_error);
    if (pm_abi_version() != PMEDIA_ABI_VERSION ||
        pm_close(NULL) != PMEDIA_ERROR_ARGUMENT ||
        pm_pump(NULL, 0, 1000) != PMEDIA_ERROR_ARGUMENT ||
        pm_pause(NULL) != PMEDIA_ERROR_ARGUMENT ||
        pm_resume(NULL) != PMEDIA_ERROR_ARGUMENT ||
        pm_stop(NULL) != PMEDIA_ERROR_ARGUMENT ||
        pm_seek(NULL, 0) != PMEDIA_ERROR_ARGUMENT ||
        pm_get_backend(NULL) != PMEDIA_BACKEND_NONE) return FALSE;

    g_media_contract_error = "source failure codes and unchanged probe output";
    if (progress != NULL) progress(g_media_contract_error);
    for (mode = 1; mode <= 4; mode++) {
        expected = mode == 1 || mode == 4 ? PMEDIA_WOULD_BLOCK : PMEDIA_ERROR_IO;
        for (backend = PMEDIA_BACKEND_AUTO; backend <= PMEDIA_BACKEND_SOFT; backend++) {
            media_contract_init(&input, &sink, &source, &output, &options);
            input.read_mode = mode;
            options.backend = backend;
            result = pm_open(&source, &options, &output, &session);
            if (session != NULL) { pm_close(session); return FALSE; }
            if (result != expected || sink.error_callbacks != 1 ||
                sink.error_events != 1 || sink.last_error != expected) return FALSE;
            memset(&probe, 0xa5, sizeof(probe));
            probe.size = sizeof(probe);
            unchanged = probe;
            if (pm_probe(&source, &probe) != expected ||
                memcmp(&probe, &unchanged, sizeof(probe)) != 0) return FALSE;
        }
    }

    g_media_contract_error = "seek failure and probe restore failure";
    if (progress != NULL) progress(g_media_contract_error);
    media_contract_init(&input, &sink, &source, &output, &options);
    input.seek_mode = 1;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_NOT_SEEKABLE ||
        session != NULL) goto fail;
    input.seek_mode = 2;
    input.position = 1;
    memset(&probe, 0xa5, sizeof(probe));
    probe.size = sizeof(probe);
    unchanged = probe;
    if (pm_probe(&source, &probe) != PMEDIA_ERROR_NOT_SEEKABLE ||
        memcmp(&probe, &unchanged, sizeof(probe)) != 0) goto fail;

    g_media_contract_error = "non-seek PCM8 contents, PTS, EOF and seek replay";
    if (progress != NULL) progress(g_media_contract_error);
    media_contract_init(&input, &sink, &source, &output, &options);
    input.data = g_media_fixture_u8;
    input.bytes = sizeof(g_media_fixture_u8);
    source.seek = NULL;
    source.tell = NULL;
    source.size_callback = NULL;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
    if (pm_pump(session, 0, -1) != PMEDIA_ERROR_ARGUMENT || sink.blocks != 0 ||
        !media_contract_drain(session) || sink.errors != 0 || sink.pcm_count != 4 ||
        sink.pcm[0] != -32768 || sink.pcm[1] != 0 || sink.pcm[2] != 32512 ||
        sink.pcm[3] != -16384 || sink.pts_us != 0 || sink.duration_us != 500 ||
        sink.eof_events != 1 || pm_pump(session, 0, 1000) != PMEDIA_EOF ||
        sink.eof_events != 1 || pm_seek(session, -1) != PMEDIA_ERROR_ARGUMENT ||
        pm_seek(session, 0) != PMEDIA_OK || !media_contract_drain(session) ||
        sink.eof_events != 2 || sink.samples != 8 ||
        pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK ||
        sink.stopped_events != 1 || pm_pause(session) != PMEDIA_ERROR_STATE ||
        pm_resume(session) != PMEDIA_ERROR_STATE ||
        pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
        pm_pump(session, 0, 1000) != PMEDIA_ERROR_STATE) goto fail;
    pm_close(session);
    session = NULL;

    g_media_contract_error = "stereo sample order and partial seek timestamp";
    if (progress != NULL) progress(g_media_contract_error);
    media_contract_init(&input, &sink, &source, &output, &options);
    input.data = g_media_fixture_stereo;
    input.bytes = sizeof(g_media_fixture_stereo);
    sink.channels = 2;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
        !media_contract_drain(session) || sink.pcm_count != 4 ||
        sink.pcm[0] != -32768 || sink.pcm[1] != 32767 ||
        sink.pcm[2] != 0 || sink.pcm[3] != 1 || sink.duration_us != 250 ||
        pm_seek(session, 125) != PMEDIA_OK ||
        !media_contract_drain(session) || sink.pts_us != 125 ||
        sink.duration_us != 125 || sink.samples != 3) goto fail;
    pm_close(session);
    session = NULL;

    g_media_contract_error = "truncated WAV and invalid PCM block alignment";
    if (progress != NULL) progress(g_media_contract_error);
    for (mode = 0; mode < 3; mode++) {
        media_contract_init(&input, &sink, &source, &output, &options);
        memcpy(damaged, g_media_fixture_stereo, sizeof(damaged));
        input.data = damaged;
        input.bytes = sizeof(damaged);
        if (mode == 0) input.bytes--;
        if (mode == 1) damaged[32] = 1;
        if (mode == 2) damaged[40] = 7;
        result = pm_open(&source, &options, &output, &session);
        if (result != PMEDIA_ERROR_FORMAT || session != NULL) goto fail;
        memset(&probe, 0, sizeof(probe));
        probe.size = sizeof(probe);
        if (pm_probe(&source, &probe) != PMEDIA_ERROR_FORMAT) goto fail;
    }

    g_media_contract_error = "callback pause/resume and error reporting";
    if (progress != NULL) progress(g_media_contract_error);
    for (mode = 0; mode < 2; mode++) {
        media_contract_init(&input, &sink, &source, &output, &options);
        sink.callback_result = mode == 0 ? PMEDIA_CALLBACK_STOP : -1;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
        result = pm_pump(session, 0, 1000);
        if (mode == 0) {
            if (result != PMEDIA_OK || sink.blocks != 1 ||
                pm_pump(session, 0, 1000) != PMEDIA_OK || sink.blocks != 1 ||
                pm_resume(session) != PMEDIA_OK ||
                pm_pump(session, 0, 1000) != PMEDIA_EOF) goto fail;
        } else if (result != PMEDIA_ERROR_CALLBACK || sink.error_callbacks != 1 ||
                   sink.error_events != 1 || sink.last_error != PMEDIA_ERROR_CALLBACK ||
                   pm_last_error(session)[0] == '\0') goto fail;
        pm_close(session);
        session = NULL;
    }

    g_media_contract_error = "AUTO PCM8 actual output and WaveOut completion";
    if (progress != NULL) progress(g_media_contract_error);
    media_contract_init(&input, &sink, &source, &output, &options);
    input.data = g_media_fixture_u8;
    input.bytes = sizeof(g_media_fixture_u8);
    options.backend = PMEDIA_BACKEND_AUTO;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
    backend = pm_get_backend(session);
    if (progress != NULL) progress(backend == PMEDIA_BACKEND_NATIVE ?
        "AUTO selected NATIVE: exercising WaveOut" : "AUTO selected SOFT: native unavailable");
    if ((backend != PMEDIA_BACKEND_NATIVE && backend != PMEDIA_BACKEND_SOFT) ||
        !media_contract_drain(session) || sink.eof_events != 1 || sink.errors != 0 ||
        sink.pcm_count != 4 || sink.pcm[0] != -32768 || sink.pcm[1] != 0 ||
        sink.pcm[2] != 32512 || sink.pcm[3] != -16384) goto fail;
    pm_close(session);
    session = NULL;

    g_media_contract_error = "repeated independent session cleanup";
    if (progress != NULL) progress(g_media_contract_error);
    for (iteration = 0; iteration < 24; iteration++) {
        media_contract_init(&input, &sink, &source, &output, &options);
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
            !media_contract_drain(session)) goto fail;
        ok = pm_close(session) == PMEDIA_OK;
        session = NULL;
        if (!ok) return FALSE;
    }
    g_media_contract_error = "media IO/PCM contract passed";
    return TRUE;

fail:
    if (session != NULL) pm_close(session);
    return FALSE;
}

typedef struct media_compressed_sink {
    media_fixture_output events;
    int width;
    int height;
    int channels;
    int frames;
    int callback_result;
    int audio_callback_result;
    int full_range;
    int sample_rate;
    unsigned long audio_magnitude;
    pm_position audio_pts;
    pm_position video_gap_us;
    pm_position video_start_us;
    pm_position video_duration_us;
    int check_audio_timeline;
    pm_position audio_start_us;
    int inferred_last_frame;
    int amr_frame_samples;
    int amr_last_sample;
    int amr_have_sample;
    int amr_zero_crossings;
    unsigned long amr_pcm_hash;
    unsigned long replay_video_hash;
    unsigned long replay_audio_hash;
    unsigned long aac_block_hash[32];
    void (*aac_progress)(const char *);
    unsigned char *aac_reference;
    int aac_reference_bytes;
    int aac_reference_position;
    int aac_compare;
    char assertion[256];
} media_compressed_sink;

static const char *g_media_compressed_error = "compressed media contract not run";
static char g_media_compressed_detail[256];

static void media_compressed_error(void *context, int error, const char *message)
{
    media_fixture_error(context, error, message);
    _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
              "compressed error=%d: %s", error, message == NULL ? "" : message);
    g_media_compressed_detail[sizeof(g_media_compressed_detail) - 1] = '\0';
    g_media_compressed_error = g_media_compressed_detail;
}

const char *test1332_media_last_error(void)
{
    return g_media_compressed_error;
}

static unsigned char *media_compressed_load(const WCHAR *name, int *out_bytes)
{
    WCHAR path[MAX_PATH];
    DWORD length;
    DWORD bytes;
    DWORD got;
    HANDLE file;
    unsigned char *data;

    *out_bytes = 0;
    length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return NULL;
    while (length > 0 && path[length - 1] != L'\\') length--;
    if (length == 0 || length + 15 + wcslen(name) >= MAX_PATH) return NULL;
    wcscpy(path + length, L"fixtures\\media\\");
    wcscat(path, name);
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    bytes = GetFileSize(file, NULL);
    if (bytes == 0 || bytes > 32768) { CloseHandle(file); return NULL; }
    data = (unsigned char *)malloc((size_t)bytes);
    got = 0;
    if (data == NULL || !ReadFile(file, data, bytes, &got, NULL) || got != bytes) {
        if (data != NULL) free(data);
        CloseHandle(file);
        return NULL;
    }
    CloseHandle(file);
    *out_bytes = (int)bytes;
    return data;
}

static int media_compressed_video(void *context, const pm_video_frame *frame)
{
    media_compressed_sink *sink;
    int plane;
    int width;
    int height;
    int x;
    int y;
    int value;
    int target;
    pm_position duration;

    sink = (media_compressed_sink *)context;
    duration = sink->video_duration_us > 0 ? sink->video_duration_us : 200000;
    if (frame == NULL || frame->size != sizeof(*frame) ||
        frame->format != PMEDIA_PIXEL_I420 || frame->width != sink->width ||
        frame->height != sink->height || frame->flags & PMEDIA_FRAME_INTERLACED ||
        !!(frame->flags & PMEDIA_FRAME_FULL_RANGE) != sink->full_range ||
        !!(frame->flags & PMEDIA_FRAME_PTS_INFERRED) !=
            (sink->inferred_last_frame && sink->frames == 2) ||
        frame->pts_us != sink->video_start_us + (pm_position)sink->frames * duration +
                         (sink->frames > 0 ? sink->video_gap_us : 0) ||
        frame->duration_us != duration ||
        (sink->frames == 0 && !(frame->flags & PMEDIA_FRAME_KEY))) goto invalid;
    for (plane = 0; plane < 3; plane++) {
        width = plane == 0 ? frame->width : (frame->width + 1) / 2;
        height = plane == 0 ? frame->height : (frame->height + 1) / 2;
        target = sink->full_range ? (plane == 0 ? 76 : (plane == 1 ? 85 : 255)) :
                                   (plane == 0 ? 81 : (plane == 1 ? 90 : 240));
        if (frame->plane[plane] == NULL || frame->stride[plane] < width) goto invalid;
        for (y = 0; y < height; y += 17) {
            for (x = 0; x < width; x += 17) {
                value = frame->plane[plane][y * frame->stride[plane] + x];
                if (value < target - 1 || value > target + 1) goto invalid;
            }
        }
    }
    sink->frames++;
    return sink->callback_result;

invalid:
    if (frame != NULL) {
        _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                  "video assertion: frames=%d size=%dx%d pts=%I64d duration=%I64d flags=%lu",
                  sink->frames, frame->width, frame->height, frame->pts_us,
                  frame->duration_us, frame->flags);
        g_media_compressed_detail[sizeof(g_media_compressed_detail) - 1] = '\0';
        g_media_compressed_error = g_media_compressed_detail;
        strcpy(sink->assertion, g_media_compressed_detail);
    }
    sink->events.errors++;
    return -1;
}

static int media_compressed_audio(void *context, const pm_audio_block *block)
{
    media_compressed_sink *sink;
    int i;
    int sample;
    int rate;

    sink = (media_compressed_sink *)context;
    rate = sink->sample_rate > 0 ? sink->sample_rate : 48000;
    if (block == NULL || block->size != sizeof(*block) || block->data == NULL ||
        block->sample_rate != rate || block->channels != sink->channels ||
        block->samples <= 0 || block->samples > 8192 ||
        block->bytes != block->samples * block->channels * 2 ||
        block->pts_us < sink->audio_pts ||
        (sink->check_audio_timeline && (block->samples != 1152 ||
         block->pts_us != sink->audio_start_us + (pm_position)sink->events.blocks * 24000)) ||
        (sink->amr_frame_samples && (block->samples != sink->amr_frame_samples ||
         block->pts_us != (pm_position)sink->events.blocks * 20000)) ||
        block->duration_us != (pm_position)block->samples * 1000000 / rate) {
        if (block != NULL) {
            _snprintf(sink->assertion, sizeof(sink->assertion),
                      "audio assertion: rate=%d channels=%d samples=%d pts=%I64d previous=%I64d duration=%I64d",
                      block->sample_rate, block->channels, block->samples,
                      block->pts_us, sink->audio_pts, block->duration_us);
            sink->assertion[sizeof(sink->assertion) - 1] = '\0';
        }
        sink->events.errors++;
        return -1;
    }
    sink->audio_pts = block->pts_us;
    if (sink->amr_frame_samples) {
        for (i = 0; i < block->bytes; i++)
            sink->amr_pcm_hash = sink->amr_pcm_hash * 33UL + block->data[i];
    }
    for (i = 0; i < block->samples * block->channels; i++) {
        sample = (short)((unsigned int)block->data[i * 2] |
                         ((unsigned int)block->data[i * 2 + 1] << 8));
        sink->audio_magnitude += (unsigned long)(sample < 0 ? -sample : sample);
        if (sink->amr_frame_samples && sink->events.blocks >= 2) {
            if (sink->amr_have_sample && (sample < 0) != (sink->amr_last_sample < 0))
                sink->amr_zero_crossings++;
            sink->amr_last_sample = sample;
            sink->amr_have_sample = 1;
        }
    }
    sink->events.samples += block->samples;
    sink->events.blocks++;
    return sink->audio_callback_result;
}

static void media_compressed_init(media_fixture_source *input,
                                 media_compressed_sink *sink,
                                 pm_source_callbacks *source,
                                 pm_output_callbacks *output,
                                 pm_open_options *options)
{
    media_contract_init(input, &sink->events, source, output, options);
    memset(sink, 0, sizeof(*sink));
    input->read_chunk = 1024;
    output->context = sink;
    output->video = media_compressed_video;
    output->audio = media_compressed_audio;
    output->error = media_compressed_error;
}

static int media_aac_replay_audio(void *context, const pm_audio_block *block)
{
    media_compressed_sink *sink;
    unsigned long hash;
    char detail[192];
    int result;
    int index;
    int i;

    sink = (media_compressed_sink *)context;
    index = sink->events.blocks;
    result = media_compressed_audio(context, block);
    if (result < 0) return result;
    if (sink->aac_reference != NULL) {
        if (block->bytes > 122880 - sink->aac_reference_position ||
            (sink->aac_compare && block->bytes >
             sink->aac_reference_bytes - sink->aac_reference_position)) {
            strcpy(sink->assertion, "AAC replay reference capacity/count mismatch");
            sink->events.errors++;
            return -1;
        }
        if (sink->aac_compare) {
            if (memcmp(sink->aac_reference + sink->aac_reference_position,
                       block->data, (size_t)block->bytes) != 0) {
                _snprintf(sink->assertion, sizeof(sink->assertion),
                          "AAC seek PCM differs at block %d byte offset %d",
                          index, sink->aac_reference_position);
                sink->assertion[sizeof(sink->assertion) - 1] = '\0';
                sink->events.errors++;
                return -1;
            }
        } else {
            memcpy(sink->aac_reference + sink->aac_reference_position,
                   block->data, (size_t)block->bytes);
        }
        sink->aac_reference_position += block->bytes;
    }
    hash = 0;
    for (i = 0; i < block->bytes; i++) {
        hash = hash * 33UL + block->data[i];
        sink->replay_audio_hash = sink->replay_audio_hash * 33UL + block->data[i];
    }
    if (index < 32) sink->aac_block_hash[index] = hash;
    if (sink->aac_progress != NULL) {
        _snprintf(detail, sizeof(detail),
                  "AAC PCM block=%d pts=%I64d samples=%d channels=%d hash=%lu",
                  index, block->pts_us, block->samples, block->channels, hash);
        detail[sizeof(detail) - 1] = '\0';
        sink->aac_progress(detail);
    }
    return result;
}

BOOL test1332_media_compressed_contract(void (*progress)(const char *))
{
    static const WCHAR *accepted[] = {
        L"baseline-aac.mp4", L"main-vga.mp4", L"aac-lc.aac"
    };
    static const WCHAR *rejected[] = {
        L"high.mp4", L"high422.mp4", L"interlaced.mp4",
        L"oversize.mp4", L"aac-main.aac"
    };
    static const char *phases[] = {
        "Constrained Baseline/AVCC + AAC-LC stereo AUTO decode and EOF seek",
        "VGA Main B-frame drain, callback pause/resume and EOF seek",
        "ADTS AAC-LC mono decode and EOF seek"
    };
    static const char *reject_phases[] = {
        "reject High profile", "reject High 4:2:2", "reject interlaced video",
        "reject oversize despite enlarged options", "reject non-LC AAC profile"
    };
    media_fixture_source input;
    media_compressed_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_probe_info unchanged;
    pm_capabilities capabilities;
    pm_session session;
    unsigned char *data;
    int bytes;
    int test;
    int pass;
    int iteration;
    int expected;
    int result;
    DWORD started;
    unsigned long aac_first_hash;
    unsigned long aac_first_blocks[32];
    int aac_first_count;
    int aac_changed_blocks;
    int aac_first_changed;
    int aac_index;
    unsigned char *aac_reference;
    int aac_reference_bytes;

    session = NULL;
    data = NULL;
    aac_reference = NULL;
    for (test = 0; test < 3; test++) {
        g_media_compressed_error = phases[test];
        if (progress != NULL) progress(g_media_compressed_error);
        data = media_compressed_load(accepted[test], &bytes);
        if (data == NULL) goto fail;
        media_compressed_init(&input, &sink, &source, &output, &options);
        input.data = data;
        input.bytes = bytes;
        sink.width = test == 1 ? 640 : 320;
        sink.height = test == 1 ? 480 : 240;
        sink.channels = test == 0 ? 2 : 1;
        output.audio = media_aac_replay_audio;
        sink.aac_progress = progress;
        aac_first_hash = 0;
        aac_first_count = 0;
        memset(aac_first_blocks, 0, sizeof(aac_first_blocks));
        if (test != 1) {
            aac_reference = (unsigned char *)malloc(122880);
            if (aac_reference == NULL) goto fail;
            sink.aac_reference = aac_reference;
        }
        memset(&probe, 0, sizeof(probe));
        probe.size = sizeof(probe);
        input.position = 1;
        result = pm_probe(&source, &probe);
        _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                  "probe: result=%d pos=%d container=%d video=%d codec=%d %dx%d audio=%d codec=%d rate=%d channels=%d",
                  result, input.position, probe.stream.container, probe.stream.has_video,
                  probe.stream.video_codec, probe.stream.width, probe.stream.height,
                  probe.stream.has_audio, probe.stream.audio_codec, probe.stream.sample_rate,
                  probe.stream.channels);
        g_media_compressed_error = g_media_compressed_detail;
        if (progress != NULL) progress(g_media_compressed_error);
        if (result != PMEDIA_OK) {
            pm_open(&source, &options, &output, &session);
            goto fail;
        }
        if (result != PMEDIA_OK || input.position != 1 ||
            probe.stream.container != (test == 2 ? PMEDIA_CONTAINER_RAW : PMEDIA_CONTAINER_MP4) ||
            probe.stream.has_video != (test != 2) ||
            probe.stream.has_audio != (test != 1) ||
            (test != 2 && (probe.stream.video_codec != PMEDIA_CODEC_H264 ||
             probe.stream.width != sink.width || probe.stream.height != sink.height)) ||
            (test != 1 && (probe.stream.audio_codec != PMEDIA_CODEC_AAC_LC ||
             probe.stream.sample_rate != 48000 || probe.stream.channels != sink.channels))) goto fail;
        input.position = 0;
        source.seek = NULL;
        source.tell = NULL;
        options.backend = test == 0 ? PMEDIA_BACKEND_AUTO : PMEDIA_BACKEND_SOFT;
        result = pm_open(&source, &options, &output, &session);
        _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                  "open: result=%d backend=%d callback-errors=%d",
                  result, pm_get_backend(session), sink.events.error_callbacks);
        g_media_compressed_error = g_media_compressed_detail;
        if (progress != NULL) progress(g_media_compressed_error);
        if (result != PMEDIA_OK ||
            pm_get_backend(session) != PMEDIA_BACKEND_SOFT) goto fail;
        memset(&capabilities, 0, sizeof(capabilities));
        capabilities.size = sizeof(capabilities);
        if (pm_get_capabilities(session, &capabilities) != PMEDIA_OK ||
            capabilities.requires_seek || capabilities.max_video_width != 640 ||
            capabilities.max_video_height != 480 ||
            pm_pause(session) != PMEDIA_OK || pm_pump(session, 0, 2000) != PMEDIA_OK ||
            sink.frames != 0 || sink.events.blocks != 0 ||
            pm_resume(session) != PMEDIA_OK) goto fail;
        for (pass = 0; pass < 3; pass++) {
            g_media_compressed_error = "compressed drain or output assertion";
            if (test == 1 && pass == 0) {
                sink.callback_result = PMEDIA_CALLBACK_STOP;
                started = GetTickCount();
                while (sink.frames == 0 && GetTickCount() - started < 5000) {
                    if (pm_pump(session, 0, 2000) != PMEDIA_OK) goto fail;
                }
                if (sink.frames != 1 || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                    sink.frames != 1) goto fail;
                sink.callback_result = PMEDIA_OK;
                if (pm_resume(session) != PMEDIA_OK) goto fail;
            }
            result = media_contract_drain(session);
            if (sink.events.errors == 0) {
                _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                          "drain: case=%d pass=%d ok=%d frames=%d blocks=%d samples=%d magnitude=%lu eof=%d errors=%d last=%d",
                          test, pass, result, sink.frames, sink.events.blocks, sink.events.samples,
                          sink.audio_magnitude, sink.events.eof_events, sink.events.error_callbacks,
                          sink.events.last_error);
                g_media_compressed_error = g_media_compressed_detail;
            }
            if (progress != NULL) progress(g_media_compressed_error);
            if (!result || sink.events.errors != 0 ||
                sink.events.error_callbacks != 0 || sink.frames != (test == 2 ? 0 : 3) ||
                sink.events.eof_events != 1 || pm_pump(session, 0, 2000) != PMEDIA_EOF ||
                sink.events.eof_events != 1 ||
                (test != 1 && (sink.events.samples < 28000 || sink.events.samples > 32000 ||
                 sink.events.blocks < 20 || sink.audio_magnitude < 1000000))) goto fail;
            if (pass == 0) {
                aac_first_hash = sink.replay_audio_hash;
                aac_first_count = sink.events.blocks;
                memcpy(aac_first_blocks, sink.aac_block_hash, sizeof(aac_first_blocks));
                aac_reference_bytes = sink.aac_reference_position;
            } else if (test != 1) {
                if (sink.aac_reference_position != aac_reference_bytes ||
                    sink.replay_audio_hash != aac_first_hash ||
                    sink.events.blocks != aac_first_count) goto fail;
                aac_changed_blocks = 0;
                aac_first_changed = -1;
                for (aac_index = 0; aac_index < sink.events.blocks &&
                     aac_index < aac_first_count && aac_index < 32; aac_index++) {
                    if (aac_first_blocks[aac_index] != sink.aac_block_hash[aac_index]) {
                        if (aac_first_changed < 0) aac_first_changed = aac_index;
                        aac_changed_blocks++;
                    }
                }
                _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                          "AAC replay BYTE-EXACT case=%d first_hash=%lu replay_hash=%lu first_blocks=%d replay_blocks=%d compared_max=32 changed=%d first_changed=%d",
                          test, aac_first_hash, sink.replay_audio_hash, aac_first_count,
                          sink.events.blocks, aac_changed_blocks, aac_first_changed);
                g_media_compressed_detail[sizeof(g_media_compressed_detail) - 1] = '\0';
                if (progress != NULL) progress(g_media_compressed_detail);
            }
            if (pass < 2) {
                if (pm_seek(session, -1) != PMEDIA_ERROR_ARGUMENT ||
                    pm_pause(session) != PMEDIA_OK) goto fail;
                /* Source storage is no longer usable; replay must use the DLL's
                 * copy and must preserve pause without invoking callbacks. */
                input.read_mode = 2;
                if (pm_seek(session, 0) != PMEDIA_OK || input.position != bytes) goto fail;
                memset(&sink, 0, sizeof(sink));
                sink.width = test == 1 ? 640 : 320;
                sink.height = test == 1 ? 480 : 240;
                sink.channels = test == 0 ? 2 : 1;
                sink.aac_progress = progress;
                sink.aac_reference = aac_reference;
                sink.aac_reference_bytes = aac_reference_bytes;
                sink.aac_compare = 1;
                if (pm_pump(session, 0, 2000) != PMEDIA_OK || sink.frames ||
                    sink.events.blocks || sink.events.eof_events ||
                    pm_resume(session) != PMEDIA_OK) goto fail;
            }
        }
        if (pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        if (aac_reference != NULL) free(aac_reference);
        aac_reference = NULL;
        free(data);
        data = NULL;
    }
    for (test = 0; test < 5; test++) {
        g_media_compressed_error = reject_phases[test];
        if (progress != NULL) progress(g_media_compressed_error);
        data = media_compressed_load(rejected[test], &bytes);
        if (data == NULL) goto fail;
        media_compressed_init(&input, &sink, &source, &output, &options);
        input.data = data;
        input.bytes = bytes;
        options.max_video_width = 1920;
        options.max_video_height = 1080;
        expected = test == 3 ? PMEDIA_ERROR_LIMIT : PMEDIA_ERROR_UNSUPPORTED;
        memset(&probe, 0xa5, sizeof(probe));
        probe.size = sizeof(probe);
        unchanged = probe;
        if (pm_probe(&source, &probe) != expected ||
            memcmp(&probe, &unchanged, sizeof(probe)) != 0 ||
            pm_open(&source, &options, &output, &session) != expected || session != NULL ||
            sink.frames != 0 || sink.events.blocks != 0 || sink.events.error_callbacks != 1 ||
            sink.events.last_error != expected) goto fail;
        free(data);
        data = NULL;
    }
    g_media_compressed_error = "truncated MP4 header and repeated compressed session cleanup";
    if (progress != NULL) progress(g_media_compressed_error);
    data = media_compressed_load(accepted[0], &bytes);
    if (data == NULL) goto fail;
    for (iteration = 0; iteration < 12; iteration++) {
        media_compressed_init(&input, &sink, &source, &output, &options);
        input.data = data;
        input.bytes = 16;
        result = pm_open(&source, &options, &output, &session);
        if (result != PMEDIA_ERROR_FORMAT || session != NULL ||
            sink.events.error_callbacks != 1 ||
            sink.events.last_error != PMEDIA_ERROR_FORMAT ||
            sink.frames != 0 || sink.events.blocks != 0) goto fail;
        input.bytes = bytes;
        memset(&sink, 0, sizeof(sink));
        sink.width = 320;
        sink.height = 240;
        sink.channels = 2;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
            !media_contract_drain(session) || sink.frames != 3 ||
            sink.events.errors != 0 || sink.events.error_callbacks != 0 ||
            sink.events.eof_events != 1 || sink.events.samples < 28000 ||
            sink.events.samples > 32000 || sink.audio_magnitude < 1000000) goto fail;
        if (pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
    }
    free(data);
    g_media_compressed_error = "compressed media contract passed";
    return TRUE;

fail:
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    if (aac_reference != NULL) free(aac_reference);
    return FALSE;
}

static int media_mpeg4_video(void *context, const pm_video_frame *frame)
{
    media_compressed_sink *sink;
    int result;
    int plane;
    int width;
    int height;
    int x;
    int y;

    result = media_compressed_video(context, frame);
    if (result < 0) return result;
    sink = (media_compressed_sink *)context;
    for (plane = 0; plane < 3; plane++) {
        width = plane == 0 ? frame->width : frame->width / 2;
        height = plane == 0 ? frame->height : frame->height / 2;
        for (y = 0; y < height; y += 17) {
            for (x = 0; x < width; x += 17) {
                sink->replay_video_hash = sink->replay_video_hash * 33UL +
                    frame->plane[plane][y * frame->stride[plane] + x];
            }
        }
    }
    return result;
}

static int media_mpeg4_audio(void *context, const pm_audio_block *block)
{
    media_compressed_sink *sink;
    int result;
    int i;

    result = media_compressed_audio(context, block);
    if (result < 0) return result;
    sink = (media_compressed_sink *)context;
    for (i = 0; i < block->bytes; i++)
        sink->replay_audio_hash = sink->replay_audio_hash * 33UL + block->data[i];
    return result;
}

static void media_mpeg4_reset_sink(media_compressed_sink *sink, int test)
{
    memset(sink, 0, sizeof(*sink));
    sink->width = test == 1 ? 640 : 320;
    sink->height = test == 1 ? 480 : 240;
    sink->channels = test == 2 ? 2 : 0;
    sink->video_gap_us = test == 2 ? 200000 : 0;
    sink->check_audio_timeline = 1;
}

BOOL test1344_media_mpeg4_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = {
        L"mpeg4-simple.mp4", L"mpeg4-asp-vga.mp4", L"mpeg4-mp3.avi"
    };
    static const WCHAR *rejected[] = {
        L"mpeg4-interlaced.mp4", L"mpeg4-oversize.mp4"
    };
    media_fixture_source input;
    media_compressed_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_probe_info unchanged;
    pm_stream_info info;
    pm_session session;
    unsigned char *data;
    unsigned long video_hash;
    unsigned long audio_hash;
    DWORD start;
    int bytes;
    int test;
    int iteration;
    int pass;
    int result;
    int expected;
    int blocks;

    session = NULL;
    data = NULL;
    for (test = 0; test < 3; test++) {
        g_media_compressed_error = test == 0 ? "MPEG4 Simple MP4" :
            (test == 1 ? "MPEG4 ASP VGA B-frame/drain" : "MPEG4 AVI MP3/empty video slot");
        if (progress != NULL) progress(g_media_compressed_error);
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        media_compressed_init(&input, &sink, &source, &output, &options);
        output.video = media_mpeg4_video;
        output.audio = media_mpeg4_audio;
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 7;
        input.position = 1;
        memset(&probe, 0, sizeof(probe));
        probe.size = sizeof(probe);
        result = pm_probe(&source, &probe);
        _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                  "MPEG4 probe: case=%d rc=%d container=%d video=%d %dx%d audio=%d rate=%d channels=%d",
                  test, result, probe.stream.container, probe.stream.video_codec,
                  probe.stream.width, probe.stream.height, probe.stream.audio_codec,
                  probe.stream.sample_rate, probe.stream.channels);
        g_media_compressed_detail[sizeof(g_media_compressed_detail) - 1] = '\0';
        g_media_compressed_error = g_media_compressed_detail;
        if (progress != NULL) progress(g_media_compressed_error);
        if (result != PMEDIA_OK || input.position != 1 ||
            probe.stream.container != (test == 2 ? PMEDIA_CONTAINER_AVI : PMEDIA_CONTAINER_MP4) ||
            !probe.stream.has_video || probe.stream.has_audio != (test == 2) ||
            probe.stream.video_codec != PMEDIA_CODEC_MPEG4_PART2 ||
            probe.stream.width != (test == 1 ? 640 : 320) ||
            probe.stream.height != (test == 1 ? 480 : 240) ||
            (test == 2 && (probe.stream.audio_codec != PMEDIA_CODEC_MP3 ||
             probe.stream.sample_rate != 48000 || probe.stream.channels != 2))) goto fail;
        for (iteration = 0; iteration < 3; iteration++) {
            input.position = 0;
            source.seek = iteration == 0 ? NULL : media_fixture_seek;
            source.tell = iteration == 0 ? NULL : media_fixture_tell;
            options.backend = iteration == 0 ? PMEDIA_BACKEND_AUTO : PMEDIA_BACKEND_SOFT;
            media_mpeg4_reset_sink(&sink, test);
            g_media_compressed_error = "MPEG4 open/backend/explicit pause";
            result = pm_open(&source, &options, &output, &session);
            if (result != PMEDIA_OK || pm_get_backend(session) != PMEDIA_BACKEND_SOFT ||
                input.position != bytes) goto fail;
            memset(&info, 0, sizeof(info));
            info.size = sizeof(info);
            if (pm_get_stream_info(session, &info) != PMEDIA_OK ||
                info.video_codec != PMEDIA_CODEC_MPEG4_PART2 || info.width != sink.width ||
                info.height != sink.height || info.container != probe.stream.container ||
                info.has_audio != (test == 2) ||
                pm_pause(session) != PMEDIA_OK || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                sink.frames || sink.events.blocks || pm_resume(session) != PMEDIA_OK) goto fail;
            video_hash = 0;
            audio_hash = 0;
            for (pass = 0; pass < 2; pass++) {
                if (pass == 0) {
                    sink.callback_result = PMEDIA_CALLBACK_STOP;
                    start = GetTickCount();
                    while (sink.frames == 0 && GetTickCount() - start < 5000) {
                        if (pm_pump(session, 0, 2000) != PMEDIA_OK) goto fail;
                    }
                    blocks = sink.events.blocks;
                    if (sink.frames != 1 || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                        sink.frames != 1 || sink.events.blocks != blocks) goto fail;
                    sink.callback_result = PMEDIA_OK;
                    if (pm_resume(session) != PMEDIA_OK) goto fail;
                }
                result = media_contract_drain(session);
                if (sink.assertion[0] != '\0' && progress != NULL) progress(sink.assertion);
                _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                          "MPEG4 drain: case=%d session=%d pass=%d ok=%d frames=%d blocks=%d samples=%d energy=%lu errors=%d eof=%d vh=%lu ah=%lu",
                          test, iteration, pass, result, sink.frames, sink.events.blocks,
                          sink.events.samples, sink.audio_magnitude, sink.events.errors,
                          sink.events.eof_events, sink.replay_video_hash, sink.replay_audio_hash);
                g_media_compressed_detail[sizeof(g_media_compressed_detail) - 1] = '\0';
                g_media_compressed_error = g_media_compressed_detail;
                if (progress != NULL) progress(g_media_compressed_error);
                if (!result || sink.frames != 3 || sink.events.blocks != (test == 2 ? 26 : 0) ||
                    sink.events.samples != (test == 2 ? 29952 : 0) ||
                    (test == 2 && sink.audio_magnitude < 1000000) || sink.events.errors ||
                    sink.events.error_callbacks || sink.events.eof_events != 1 ||
                    pm_pump(session, 0, 2000) != PMEDIA_EOF || sink.events.eof_events != 1) goto fail;
                if (pass == 0) {
                    video_hash = sink.replay_video_hash;
                    audio_hash = sink.replay_audio_hash;
                    g_media_compressed_error = "MPEG4 seek zero after EOF";
                    if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
                    media_mpeg4_reset_sink(&sink, test);
                } else if (sink.replay_video_hash != video_hash || sink.replay_audio_hash != audio_hash) {
                    g_media_compressed_error = "MPEG4 seek replay pixel/PCM differs";
                    goto fail;
                }
            }
            g_media_compressed_error = "MPEG4 idempotent stop and stopped-state guards";
            if (pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK ||
                sink.events.stopped_events != 1 || pm_pump(session, 0, 2000) != PMEDIA_ERROR_STATE ||
                pm_pause(session) != PMEDIA_ERROR_STATE || pm_resume(session) != PMEDIA_ERROR_STATE ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
        }
        g_media_compressed_error = "MPEG4 application dimension limit before output";
        if (progress != NULL) progress(g_media_compressed_error);
        input.position = 0;
        media_mpeg4_reset_sink(&sink, test);
        options.max_video_width = 160;
        options.max_video_height = 120;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_LIMIT ||
            session != NULL || sink.events.last_error != PMEDIA_ERROR_LIMIT ||
            sink.events.error_callbacks != 1 || sink.frames || sink.events.blocks) goto fail;
        options.max_video_width = 0;
        options.max_video_height = 0;
        if (test == 2) {
            g_media_compressed_error = "MPEG4 AVI audio STOP/resume and negative callback";
            if (progress != NULL) progress(g_media_compressed_error);
            input.position = 0;
            media_mpeg4_reset_sink(&sink, test);
            sink.audio_callback_result = PMEDIA_CALLBACK_STOP;
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
            start = GetTickCount();
            while (sink.events.blocks == 0 && GetTickCount() - start < 5000) {
                if (pm_pump(session, 0, 2000) != PMEDIA_OK) goto fail;
            }
            blocks = sink.frames;
            if (sink.events.blocks != 1 || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                sink.events.blocks != 1 || sink.frames != blocks) goto fail;
            sink.audio_callback_result = PMEDIA_OK;
            if (pm_resume(session) != PMEDIA_OK || !media_contract_drain(session) ||
                sink.frames != 3 || sink.events.blocks != 26 || sink.events.samples != 29952 ||
                sink.replay_video_hash != video_hash || sink.replay_audio_hash != audio_hash ||
                sink.events.eof_events != 1 || sink.events.error_callbacks || sink.events.errors ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
            input.position = 0;
            media_mpeg4_reset_sink(&sink, test);
            sink.audio_callback_result = -1;
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
                media_contract_drain(session) || sink.events.blocks != 1 ||
                sink.events.last_error != PMEDIA_ERROR_CALLBACK || sink.events.error_callbacks != 1 ||
                sink.events.error_events != 1 || sink.events.eof_events ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
        }
        g_media_compressed_error = "MPEG4 negative video callback propagation";
        if (progress != NULL) progress(g_media_compressed_error);
        input.position = 0;
        media_mpeg4_reset_sink(&sink, test);
        sink.callback_result = -1;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
            media_contract_drain(session) || sink.frames != 1 ||
            sink.events.last_error != PMEDIA_ERROR_CALLBACK || sink.events.error_callbacks != 1 ||
            sink.events.error_events != 1 || sink.events.eof_events != 0 ||
            pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        g_media_compressed_error = "MPEG4 truncated MP4/AVI header preserves probe output";
        if (progress != NULL) progress(g_media_compressed_error);
        input.position = 0;
        input.bytes = 8;
        media_mpeg4_reset_sink(&sink, test);
        memset(&probe, 0xa5, sizeof(probe));
        probe.size = sizeof(probe);
        unchanged = probe;
        if (pm_probe(&source, &probe) != PMEDIA_ERROR_FORMAT || input.position != 0 ||
            memcmp(&probe, &unchanged, sizeof(probe)) != 0 ||
            pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_FORMAT ||
            session != NULL || sink.events.last_error != PMEDIA_ERROR_FORMAT ||
            sink.events.error_callbacks != 1 || sink.frames || sink.events.blocks) goto fail;
        free(data);
        data = NULL;
    }
    for (test = 0; test < 2; test++) {
        g_media_compressed_error = test == 0 ? "reject interlaced MPEG4" : "reject over-VGA MPEG4";
        if (progress != NULL) progress(g_media_compressed_error);
        data = media_compressed_load(rejected[test], &bytes);
        if (data == NULL) goto fail;
        media_compressed_init(&input, &sink, &source, &output, &options);
        input.data = data;
        input.bytes = bytes;
        options.max_video_width = 4096;
        options.max_video_height = 4096;
        expected = test == 0 ? PMEDIA_ERROR_UNSUPPORTED : PMEDIA_ERROR_LIMIT;
        memset(&probe, 0xa5, sizeof(probe));
        probe.size = sizeof(probe);
        unchanged = probe;
        if (pm_probe(&source, &probe) != expected || input.position != 0 ||
            memcmp(&probe, &unchanged, sizeof(probe)) != 0 ||
            pm_open(&source, &options, &output, &session) != expected || session != NULL ||
            sink.events.last_error != expected || sink.events.error_callbacks != 1 ||
            sink.frames || sink.events.blocks) goto fail;
        free(data);
        data = NULL;
    }
    g_media_compressed_error = "MPEG4 Part 2 contract passed";
    return TRUE;

fail:
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    return FALSE;
}

static void media_h263_reset_sink(media_compressed_sink *sink, int test)
{
    memset(sink, 0, sizeof(*sink));
    sink->width = test == 0 ? 352 : 640;
    sink->height = test == 0 ? 288 : 480;
}

BOOL test1347_media_h263_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = { L"h263-cif.avi", L"h263p-vga.avi" };
    media_fixture_source input;
    media_compressed_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_probe_info unchanged;
    pm_stream_info info;
    pm_session session;
    unsigned char *data;
    unsigned long video_hash;
    DWORD start;
    int bytes;
    int test;
    int iteration;
    int pass;
    int result;

    session = NULL;
    data = NULL;
    for (test = 0; test < 2; test++) {
        g_media_compressed_error = test == 0 ? "H263 CIF AVI" : "H263+ VGA AVI";
        if (progress != NULL) progress(g_media_compressed_error);
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        media_compressed_init(&input, &sink, &source, &output, &options);
        output.video = media_mpeg4_video;
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 7;
        input.position = 1;
        memset(&probe, 0, sizeof(probe));
        probe.size = sizeof(probe);
        if (pm_probe(&source, &probe) != PMEDIA_OK || input.position != 1 ||
            probe.stream.container != PMEDIA_CONTAINER_AVI || !probe.stream.has_video ||
            probe.stream.has_audio || probe.stream.video_codec != PMEDIA_CODEC_H263 ||
            probe.stream.width != (test == 0 ? 352 : 640) ||
            probe.stream.height != (test == 0 ? 288 : 480)) goto fail;
        for (iteration = 0; iteration < 3; iteration++) {
            input.position = 0;
            input.read_mode = 0;
            source.seek = iteration == 0 ? NULL : media_fixture_seek;
            source.tell = iteration == 0 ? NULL : media_fixture_tell;
            options.backend = iteration == 0 ? PMEDIA_BACKEND_AUTO : PMEDIA_BACKEND_SOFT;
            media_h263_reset_sink(&sink, test);
            g_media_compressed_error = "H263 open/info/pause";
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
                input.position != bytes || pm_get_backend(session) != PMEDIA_BACKEND_SOFT) goto fail;
            memset(&info, 0, sizeof(info));
            info.size = sizeof(info);
            if (pm_get_stream_info(session, &info) != PMEDIA_OK ||
                info.container != PMEDIA_CONTAINER_AVI || info.video_codec != PMEDIA_CODEC_H263 ||
                info.width != sink.width || info.height != sink.height || info.has_audio ||
                pm_pause(session) != PMEDIA_OK || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                sink.frames || sink.events.blocks || pm_resume(session) != PMEDIA_OK) goto fail;
            video_hash = 0;
            for (pass = 0; pass < 2; pass++) {
                g_media_compressed_error = "H263 STOP/resume/pixel/timeline/drain";
                if (pass == 0) {
                    sink.callback_result = PMEDIA_CALLBACK_STOP;
                    start = GetTickCount();
                    while (sink.frames == 0 && GetTickCount() - start < 5000) {
                        if (pm_pump(session, 0, 2000) != PMEDIA_OK) goto fail;
                    }
                    if (sink.frames != 1 || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                        sink.frames != 1 || sink.events.blocks) goto fail;
                    sink.callback_result = PMEDIA_OK;
                    if (pm_resume(session) != PMEDIA_OK) goto fail;
                }
                result = media_contract_drain(session);
                if (sink.assertion[0] && progress != NULL) progress(sink.assertion);
                _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                          "H263 drain: case=%d session=%d pass=%d ok=%d frames=%d errors=%d eof=%d vh=%lu",
                          test, iteration, pass, result, sink.frames, sink.events.errors,
                          sink.events.eof_events, sink.replay_video_hash);
                g_media_compressed_detail[sizeof(g_media_compressed_detail) - 1] = '\0';
                g_media_compressed_error = g_media_compressed_detail;
                if (progress != NULL) progress(g_media_compressed_error);
                if (!result || sink.frames != 3 || sink.events.blocks || sink.events.errors ||
                    sink.events.error_callbacks || sink.events.eof_events != 1 ||
                    pm_pump(session, 0, 2000) != PMEDIA_EOF || sink.events.eof_events != 1) goto fail;
                if (pass == 0) {
                    video_hash = sink.replay_video_hash;
                    input.read_mode = 2;
                    if (pm_seek(session, -1) != PMEDIA_ERROR_ARGUMENT ||
                        pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK) goto fail;
                    media_h263_reset_sink(&sink, test);
                    if (pm_pump(session, 0, 2000) != PMEDIA_OK || sink.frames ||
                        sink.events.eof_events || pm_resume(session) != PMEDIA_OK) goto fail;
                } else if (sink.replay_video_hash != video_hash) goto fail;
            }
            if (pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK ||
                sink.events.stopped_events != 1 || pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
                pm_pump(session, 0, 2000) != PMEDIA_ERROR_STATE ||
                pm_pause(session) != PMEDIA_ERROR_STATE || pm_resume(session) != PMEDIA_ERROR_STATE ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
        }
        g_media_compressed_error = "H263 smaller application limit";
        input.position = 0;
        input.read_mode = 0;
        media_h263_reset_sink(&sink, test);
        options.max_video_width = 160;
        options.max_video_height = 120;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_LIMIT || session != NULL ||
            sink.events.last_error != PMEDIA_ERROR_LIMIT || sink.events.error_callbacks != 1 ||
            sink.frames || sink.events.blocks) goto fail;
        options.max_video_width = 0;
        options.max_video_height = 0;
        input.position = 0;
        media_h263_reset_sink(&sink, test);
        sink.callback_result = -1;
        g_media_compressed_error = "H263 negative video callback";
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
            media_contract_drain(session) || sink.frames != 1 ||
            sink.events.last_error != PMEDIA_ERROR_CALLBACK || sink.events.error_callbacks != 1 ||
            sink.events.error_events != 1 || sink.events.eof_events ||
            pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        input.position = 0;
        input.bytes = 8;
        media_h263_reset_sink(&sink, test);
        memset(&probe, 0xa5, sizeof(probe));
        probe.size = sizeof(probe);
        unchanged = probe;
        g_media_compressed_error = "H263 truncated header preserves probe";
        if (pm_probe(&source, &probe) != PMEDIA_ERROR_FORMAT || input.position != 0 ||
            memcmp(&probe, &unchanged, sizeof(probe)) != 0 ||
            pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_FORMAT || session != NULL ||
            sink.events.last_error != PMEDIA_ERROR_FORMAT || sink.events.error_callbacks != 1 ||
            sink.frames || sink.events.blocks) goto fail;
        free(data);
        data = NULL;
    }
    g_media_compressed_error = "H263 over-VGA probe/open rejects despite enlarged options";
    data = media_compressed_load(L"h263-oversize.avi", &bytes);
    if (data == NULL) goto fail;
    media_compressed_init(&input, &sink, &source, &output, &options);
    input.data = data;
    input.bytes = bytes;
    options.max_video_width = 4096;
    options.max_video_height = 4096;
    memset(&probe, 0xa5, sizeof(probe));
    probe.size = sizeof(probe);
    unchanged = probe;
    if (pm_probe(&source, &probe) != PMEDIA_ERROR_LIMIT || input.position != 0 ||
        memcmp(&probe, &unchanged, sizeof(probe)) != 0 ||
        pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_LIMIT || session != NULL ||
        sink.events.last_error != PMEDIA_ERROR_LIMIT || sink.events.error_callbacks != 1 ||
        sink.frames || sink.events.blocks) goto fail;
    free(data);
    g_media_compressed_error = "H263 contract passed";
    return TRUE;
fail:
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    return FALSE;
}

static void media_flv_reset_sink(media_compressed_sink *sink, int test)
{
    memset(sink, 0, sizeof(*sink));
    sink->width = test == 0 ? 320 : 640;
    sink->height = test == 0 ? 240 : 480;
    sink->channels = test == 0 ? 2 : 0;
    sink->video_start_us = test == 0 ? 21000 : 400000;
}

static int media_flv_audio(void *context, const pm_audio_block *block)
{
    media_compressed_sink *sink;
    pm_position expected;

    sink = (media_compressed_sink *)context;
    expected = sink->events.blocks == 0 ? 0 :
        (pm_position)(21 + ((sink->events.blocks - 1) * 1024 + 24) / 48) * 1000;
    if (block == NULL || block->samples != 1024 || block->pts_us != expected) {
        strcpy(sink->assertion, "FLV AAC millisecond timestamp/sample mismatch");
        sink->events.errors++;
        return -1;
    }
    return media_aac_replay_audio(context, block);
}

BOOL test1348_media_flv_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = { L"flv-h264-aac.flv", L"flv-main-vga.flv" };
    static const WCHAR *rejects[] = { L"flv-oversize.flv", L"flv1-unsupported.flv" };
    media_fixture_source input;
    media_compressed_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_probe_info unchanged;
    pm_stream_info info;
    pm_session session;
    unsigned char *data;
    unsigned char *reference;
    unsigned long video_hash;
    DWORD start;
    int bytes;
    int test;
    int iteration;
    int pass;
    int result;
    int count;
    int expected;

    session = NULL;
    data = NULL;
    reference = (unsigned char *)malloc(122880);
    if (reference == NULL) return FALSE;
    for (test = 0; test < 2; test++) {
        g_media_compressed_error = test == 0 ? "FLV Baseline/AAC" : "FLV Main VGA B-frame";
        if (progress != NULL) progress(g_media_compressed_error);
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        media_compressed_init(&input, &sink, &source, &output, &options);
        output.video = media_mpeg4_video;
        output.audio = media_flv_audio;
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 7;
        input.position = 1;
        memset(&probe, 0, sizeof(probe));
        probe.size = sizeof(probe);
        if (pm_probe(&source, &probe) != PMEDIA_OK || input.position != 1 ||
            probe.stream.container != PMEDIA_CONTAINER_FLV || !probe.stream.has_video ||
            probe.stream.video_codec != PMEDIA_CODEC_H264 ||
            probe.stream.width != (test == 0 ? 320 : 640) ||
            probe.stream.height != (test == 0 ? 240 : 480) ||
            probe.stream.has_audio != (test == 0) || (test == 0 &&
            (probe.stream.audio_codec != PMEDIA_CODEC_AAC_LC ||
             probe.stream.sample_rate != 48000 || probe.stream.channels != 2))) goto fail;
        for (iteration = 0; iteration < 3; iteration++) {
            input.position = 0;
            input.read_mode = 0;
            source.seek = iteration == 0 ? NULL : media_fixture_seek;
            source.tell = iteration == 0 ? NULL : media_fixture_tell;
            options.backend = iteration == 0 ? PMEDIA_BACKEND_AUTO : PMEDIA_BACKEND_SOFT;
            media_flv_reset_sink(&sink, test);
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
                input.position != bytes || pm_get_backend(session) != PMEDIA_BACKEND_SOFT) goto fail;
            memset(&info, 0, sizeof(info));
            info.size = sizeof(info);
            if (pm_get_stream_info(session, &info) != PMEDIA_OK ||
                info.container != PMEDIA_CONTAINER_FLV || info.width != sink.width ||
                info.height != sink.height || info.has_audio != (test == 0) ||
                pm_pause(session) != PMEDIA_OK || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                sink.frames || sink.events.blocks || pm_resume(session) != PMEDIA_OK) goto fail;
            video_hash = 0;
            for (pass = 0; pass < 3; pass++) {
                sink.aac_reference = test == 0 ? reference : NULL;
                sink.aac_reference_bytes = test == 0 ? 122880 : 0;
                sink.aac_compare = pass != 0;
                if (pass == 0) {
                    if (test == 0) sink.audio_callback_result = PMEDIA_CALLBACK_STOP;
                    else sink.callback_result = PMEDIA_CALLBACK_STOP;
                    start = GetTickCount();
                    while ((test == 0 ? sink.events.blocks : sink.frames) == 0 &&
                           GetTickCount() - start < 5000) {
                        if (pm_pump(session, 0, 2000) != PMEDIA_OK) goto fail;
                    }
                    count = test == 0 ? sink.frames : sink.events.blocks;
                    if ((test == 0 ? sink.events.blocks : sink.frames) != 1 ||
                        pm_pump(session, 0, 2000) != PMEDIA_OK ||
                        (test == 0 ? sink.events.blocks : sink.frames) != 1 ||
                        (test == 0 ? sink.frames : sink.events.blocks) != count) goto fail;
                    sink.audio_callback_result = PMEDIA_OK;
                    sink.callback_result = PMEDIA_OK;
                    if (pm_resume(session) != PMEDIA_OK) goto fail;
                }
                result = media_contract_drain(session);
                if (sink.assertion[0] && progress != NULL) progress(sink.assertion);
                _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                          "FLV drain case=%d session=%d pass=%d ok=%d frames=%d blocks=%d samples=%d errors=%d eof=%d vh=%lu pcm_bytes=%d compare=%d",
                          test, iteration, pass, result, sink.frames, sink.events.blocks,
                          sink.events.samples, sink.events.errors, sink.events.eof_events,
                          sink.replay_video_hash, sink.aac_reference_position, sink.aac_compare);
                g_media_compressed_detail[sizeof(g_media_compressed_detail) - 1] = '\0';
                g_media_compressed_error = g_media_compressed_detail;
                if (progress != NULL) progress(g_media_compressed_error);
                if (!result || sink.frames != 3 || sink.events.blocks != (test == 0 ? 30 : 0) ||
                    sink.events.samples != (test == 0 ? 30720 : 0) || sink.events.errors ||
                    sink.events.error_callbacks || sink.events.eof_events != 1 ||
                    (test == 0 && (sink.audio_magnitude < 1000000 ||
                     sink.aac_reference_position != 122880)) ||
                    pm_pump(session, 0, 2000) != PMEDIA_EOF || sink.events.eof_events != 1) goto fail;
                if (pass == 0) video_hash = sink.replay_video_hash;
                else if (sink.replay_video_hash != video_hash) goto fail;
                if (pass < 2) {
                    input.read_mode = 2;
                    if (pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK) goto fail;
                    media_flv_reset_sink(&sink, test);
                    if (pm_pump(session, 0, 2000) != PMEDIA_OK || sink.frames ||
                        sink.events.blocks || sink.events.eof_events || pm_resume(session) != PMEDIA_OK) goto fail;
                }
            }
            if (pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK ||
                sink.events.stopped_events != 1 || pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
                pm_pump(session, 0, 2000) != PMEDIA_ERROR_STATE ||
                pm_pause(session) != PMEDIA_ERROR_STATE || pm_resume(session) != PMEDIA_ERROR_STATE ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
        }
        input.position = 0;
        input.read_mode = 0;
        media_flv_reset_sink(&sink, test);
        if (test == 0) sink.audio_callback_result = -1;
        else sink.callback_result = -1;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
            media_contract_drain(session) || sink.events.last_error != PMEDIA_ERROR_CALLBACK ||
            sink.events.error_callbacks != 1 || sink.events.error_events != 1 || sink.events.eof_events ||
            (test == 0 ? sink.events.blocks : sink.frames) != 1 || pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        input.position = 0;
        input.bytes = 8;
        media_flv_reset_sink(&sink, test);
        memset(&probe, 0xa5, sizeof(probe));
        probe.size = sizeof(probe);
        unchanged = probe;
        if (pm_probe(&source, &probe) != PMEDIA_ERROR_FORMAT || input.position != 0 ||
            memcmp(&probe, &unchanged, sizeof(probe)) != 0 ||
            pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_FORMAT || session != NULL ||
            sink.events.error_callbacks != 1 || sink.frames || sink.events.blocks) goto fail;
        free(data);
        data = NULL;
    }
    for (test = 0; test < 2; test++) {
        g_media_compressed_error = test == 0 ? "FLV over-VGA reject" : "FLV1 unsupported reject";
        if (progress != NULL) progress(g_media_compressed_error);
        data = media_compressed_load(rejects[test], &bytes);
        if (data == NULL) goto fail;
        media_compressed_init(&input, &sink, &source, &output, &options);
        input.data = data;
        input.bytes = bytes;
        options.max_video_width = 4096;
        options.max_video_height = 4096;
        expected = test == 0 ? PMEDIA_ERROR_LIMIT : PMEDIA_ERROR_UNSUPPORTED;
        memset(&probe, 0xa5, sizeof(probe));
        probe.size = sizeof(probe);
        unchanged = probe;
        if (pm_probe(&source, &probe) != expected || input.position != 0 ||
            memcmp(&probe, &unchanged, sizeof(probe)) != 0 ||
            pm_open(&source, &options, &output, &session) != expected || session != NULL ||
            sink.events.last_error != expected || sink.events.error_callbacks != 1 ||
            sink.frames || sink.events.blocks) goto fail;
        free(data);
        data = NULL;
    }
    free(reference);
    return TRUE;
fail:
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    free(reference);
    return FALSE;
}

BOOL test1334_media_mpeg_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = { L"mpeg2-mp2.ts", L"mpeg1-mp2.mpg" };
    static const WCHAR *rejected[] = { L"mpeg2-interlaced.ts", L"mpeg2-oversize.ts" };
    media_fixture_source input;
    media_compressed_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_probe_info unchanged;
    pm_stream_info info;
    pm_session session;
    unsigned char *data;
    int bytes;
    int test;
    int iteration;
    int pass;
    int result;
    int expected;
    DWORD start;

    session = NULL;
    data = NULL;
    for (test = 0; test < 2; test++) {
        g_media_compressed_error = test == 0 ? "TS MPEG2 B-frame + MP2 nonzero timestamps" :
                                              "PS MPEG1 B-frame + MP2 nonzero timestamps";
        if (progress != NULL) progress(g_media_compressed_error);
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        media_compressed_init(&input, &sink, &source, &output, &options);
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 13;
        input.position = 1;
        memset(&probe, 0, sizeof(probe));
        probe.size = sizeof(probe);
        result = pm_probe(&source, &probe);
        _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                  "MPEG probe: result=%d container=%d video=%d %dx%d audio=%d rate=%d channels=%d",
                  result, probe.stream.container, probe.stream.video_codec,
                  probe.stream.width, probe.stream.height, probe.stream.audio_codec,
                  probe.stream.sample_rate, probe.stream.channels);
        g_media_compressed_error = g_media_compressed_detail;
        if (progress != NULL) progress(g_media_compressed_error);
        if (result != PMEDIA_OK || input.position != 1 ||
            probe.stream.container != (test == 0 ? PMEDIA_CONTAINER_MPEG_TS : PMEDIA_CONTAINER_MPEG_PS) ||
            !probe.stream.has_video || !probe.stream.has_audio ||
            probe.stream.video_codec != (test == 0 ? PMEDIA_CODEC_MPEG2_VIDEO : PMEDIA_CODEC_MPEG1_VIDEO) ||
            probe.stream.audio_codec != PMEDIA_CODEC_MP2 || probe.stream.width != 320 ||
            probe.stream.height != 240 || probe.stream.sample_rate != 48000 || probe.stream.channels != 1) goto fail;
        for (iteration = 0; iteration < 3; iteration++) {
            input.position = 0;
            source.seek = iteration == 0 ? NULL : media_fixture_seek;
            source.tell = iteration == 0 ? NULL : media_fixture_tell;
            options.backend = iteration == 0 ? PMEDIA_BACKEND_AUTO : PMEDIA_BACKEND_SOFT;
            memset(&sink, 0, sizeof(sink));
            sink.width = 320;
            sink.height = 240;
            sink.channels = 1;
            sink.video_start_us = 2000000;
            sink.video_duration_us = 40000;
            sink.check_audio_timeline = 1;
            sink.audio_start_us = 1989978;
            sink.inferred_last_frame = test == 1;
            result = pm_open(&source, &options, &output, &session);
            if (result != PMEDIA_OK || pm_get_backend(session) != PMEDIA_BACKEND_SOFT ||
                input.position != bytes) goto fail;
            memset(&info, 0, sizeof(info));
            info.size = sizeof(info);
            if (pm_get_stream_info(session, &info) != PMEDIA_OK ||
                info.container != probe.stream.container || info.video_codec != probe.stream.video_codec ||
                info.audio_codec != PMEDIA_CODEC_MP2 || info.sample_rate != 48000 || info.channels != 1 ||
                pm_pause(session) != PMEDIA_OK || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                sink.frames || sink.events.blocks || pm_resume(session) != PMEDIA_OK) goto fail;
            for (pass = 0; pass < 2; pass++) {
                if (pass == 0) {
                    sink.callback_result = PMEDIA_CALLBACK_STOP;
                    start = GetTickCount();
                    while (sink.frames == 0 && GetTickCount() - start < 5000) {
                        if (pm_pump(session, 0, 2000) != PMEDIA_OK) goto fail;
                    }
                    expected = sink.events.blocks;
                    if (sink.frames != 1 || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                        sink.frames != 1 || sink.events.blocks != expected) goto fail;
                    sink.callback_result = PMEDIA_OK;
                    if (pm_resume(session) != PMEDIA_OK) goto fail;
                }
                result = media_contract_drain(session);
                if (sink.assertion[0] != '\0' && progress != NULL) progress(sink.assertion);
                _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                          "MPEG drain: case=%d iteration=%d pass=%d ok=%d frames=%d blocks=%d samples=%d energy=%lu errors=%d eof=%d",
                          test, iteration, pass, result, sink.frames, sink.events.blocks,
                          sink.events.samples, sink.audio_magnitude, sink.events.errors, sink.events.eof_events);
                g_media_compressed_error = g_media_compressed_detail;
                if (progress != NULL) progress(g_media_compressed_error);
                if (!result || sink.frames != 3 || sink.events.samples != 5760 || sink.events.blocks != 5 ||
                    sink.audio_magnitude < 1000000 || sink.events.errors || sink.events.error_callbacks ||
                    sink.events.eof_events != 1 || pm_pump(session, 0, 2000) != PMEDIA_EOF ||
                    sink.events.eof_events != 1) goto fail;
                if (pass == 0) {
                    g_media_compressed_error = "MPEG seek zero after EOF";
                    if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
                    memset(&sink, 0, sizeof(sink));
                    sink.width = 320;
                    sink.height = 240;
                    sink.channels = 1;
                    sink.video_start_us = 2000000;
                    sink.video_duration_us = 40000;
                    sink.check_audio_timeline = 1;
                    sink.audio_start_us = 1989978;
                    sink.inferred_last_frame = test == 1;
                }
            }
            if (pm_stop(session) != PMEDIA_OK || pm_pump(session, 0, 2000) != PMEDIA_ERROR_STATE ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
        }
        g_media_compressed_error = "truncated MPEG header without output";
        if (progress != NULL) progress(g_media_compressed_error);
        input.position = 0;
        input.bytes = 8;
        memset(&sink, 0, sizeof(sink));
        if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_FORMAT ||
            session != NULL || sink.events.last_error != PMEDIA_ERROR_FORMAT ||
            sink.events.error_callbacks != 1 || sink.frames || sink.events.blocks) goto fail;
        free(data);
        data = NULL;
    }
    for (test = 0; test < 2; test++) {
        g_media_compressed_error = test == 0 ? "reject interlaced MPEG2" : "reject over-VGA MPEG2";
        if (progress != NULL) progress(g_media_compressed_error);
        data = media_compressed_load(rejected[test], &bytes);
        if (data == NULL) goto fail;
        media_compressed_init(&input, &sink, &source, &output, &options);
        input.data = data;
        input.bytes = bytes;
        options.max_video_width = 4096;
        options.max_video_height = 4096;
        memset(&probe, 0xa5, sizeof(probe));
        probe.size = sizeof(probe);
        unchanged = probe;
        expected = test == 0 ? PMEDIA_ERROR_UNSUPPORTED : PMEDIA_ERROR_LIMIT;
        if (pm_probe(&source, &probe) != expected || memcmp(&probe, &unchanged, sizeof(probe)) != 0 ||
            input.position != 0 || pm_open(&source, &options, &output, &session) != expected ||
            session != NULL || sink.events.last_error != expected || sink.events.error_callbacks != 1 ||
            sink.frames || sink.events.blocks) goto fail;
        free(data);
        data = NULL;
    }
    g_media_compressed_error = "MPEG contract passed";
    return TRUE;

fail:
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    return FALSE;
}

typedef struct media_ima_sink {
    media_fixture_output events;
    const unsigned char *reference;
    int channels;
    int samples_per_block;
    int total_frames;
    int cursor;
    int callback_result;
} media_ima_sink;

static int media_ima_audio(void *context, const pm_audio_block *block)
{
    media_ima_sink *sink;
    int frames;
    int i;
    sink = (media_ima_sink *)context;
    frames = sink->samples_per_block - sink->cursor % sink->samples_per_block;
    if (frames > sink->total_frames - sink->cursor) frames = sink->total_frames - sink->cursor;
    if (block == NULL || block->size != sizeof(*block) || block->data == NULL ||
        block->sample_rate != 8000 || block->channels != sink->channels ||
        block->samples != frames || frames <= 0 || block->bytes != frames * sink->channels * 2 ||
        block->pts_us != (pm_position)sink->cursor * 125 ||
        block->duration_us != (pm_position)frames * 125) goto invalid;
    if (sink->reference != NULL) {
        if (memcmp(block->data, sink->reference + sink->cursor * sink->channels * 2,
                   (size_t)block->bytes) != 0) goto invalid;
    } else {
        for (i = 0; i < block->bytes; i++) if (block->data[i] != 0) goto invalid;
    }
    sink->cursor += frames;
    sink->events.blocks++;
    sink->events.samples += frames;
    return sink->callback_result;
invalid:
    sink->events.errors++;
    g_media_contract_error = "IMA PCM bytes/block/PTS assertion";
    return -1;
}

static void media_ima_reset(media_ima_sink *sink, const unsigned char *reference,
                            int channels, int samples_per_block, int total, int cursor)
{
    memset(sink, 0, sizeof(*sink));
    sink->reference = reference;
    sink->channels = channels;
    sink->samples_per_block = samples_per_block;
    sink->total_frames = total;
    sink->cursor = cursor;
}

static void media_ima_put16(unsigned char *data, unsigned int value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
}

static void media_ima_put32(unsigned char *data, unsigned long value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static BOOL media_ima_reject(const unsigned char *data, int bytes, int expected)
{
    media_fixture_source input;
    media_ima_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_probe_info unchanged;
    pm_session session;
    int backend;
    media_contract_init(&input, &sink.events, &source, &output, &options);
    input.data = data;
    input.bytes = bytes;
    input.position = 1;
    memset(&probe, 0xa5, sizeof(probe));
    probe.size = sizeof(probe);
    unchanged = probe;
    if (pm_probe(&source, &probe) != expected || input.position != 1 ||
        memcmp(&probe, &unchanged, sizeof(probe)) != 0) return FALSE;
    output.audio = media_ima_audio;
    for (backend = 0; backend < 2; backend++) {
        input.position = 0;
        memset(&sink, 0, sizeof(sink));
        options.backend = backend == 0 ? PMEDIA_BACKEND_AUTO : PMEDIA_BACKEND_SOFT;
        session = NULL;
        if (pm_open(&source, &options, &output, &session) != expected || session != NULL ||
            sink.events.blocks || sink.events.samples || sink.events.errors ||
            sink.events.error_callbacks != 1 || sink.events.last_error != expected) {
            if (session != NULL) pm_close(session);
            return FALSE;
        }
    }
    return TRUE;
}

BOOL test1337_media_ima_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = { L"ima-mono.wav", L"ima-stereo.wav" };
    static const WCHAR *pcm_names[] = { L"ima-mono.pcm", L"ima-stereo.pcm" };
    media_fixture_source input;
    media_ima_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_stream_info info;
    pm_session session;
    unsigned char *data;
    unsigned char *pcm;
    unsigned char *damaged;
    unsigned char *large;
    int bytes;
    int pcm_bytes;
    int test;
    int iteration;
    int pass;
    int channels;
    int samples_per_block;
    int target;
    int mode;

    session = NULL;
    data = NULL;
    pcm = NULL;
    damaged = NULL;
    large = NULL;
    for (test = 0; test < 2; test++) {
        g_media_contract_error = test == 0 ? "IMA mono exact PCM and fact trim" :
                                           "IMA distinct stereo channel groups";
        if (progress != NULL) progress(g_media_contract_error);
        data = media_compressed_load(names[test], &bytes);
        pcm = media_compressed_load(pcm_names[test], &pcm_bytes);
        if (data == NULL || pcm == NULL || bytes != (test == 0 ? 1596 : 2620) ||
            pcm_bytes != (test == 0 ? 6102 : 10100)) goto fail;
        channels = test + 1;
        samples_per_block = test == 0 ? 1017 : 505;
        media_contract_init(&input, &sink.events, &source, &output, &options);
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 5;
        input.position = 1;
        output.audio = media_ima_audio;
        memset(&probe, 0, sizeof(probe));
        probe.size = sizeof(probe);
        if (pm_probe(&source, &probe) != PMEDIA_OK || input.position != 1 ||
            probe.stream.container != PMEDIA_CONTAINER_WAV || probe.stream.has_video ||
            !probe.stream.has_audio || probe.stream.audio_codec != PMEDIA_CODEC_IMA_ADPCM ||
            probe.stream.channels != channels || probe.stream.sample_rate != 8000 ||
            probe.stream.bits_per_sample != 4 || probe.stream.duration_us != 300000 ||
            !probe.capabilities.soft_audio_available) goto fail;
        for (iteration = 0; iteration < 3; iteration++) {
            input.position = 0;
            source.seek = iteration == 0 ? NULL : media_fixture_seek;
            source.tell = iteration == 0 ? NULL : media_fixture_tell;
            options.backend = iteration == 0 ? PMEDIA_BACKEND_AUTO : PMEDIA_BACKEND_SOFT;
            media_ima_reset(&sink, pcm, channels, samples_per_block, 2400, 0);
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
                pm_get_backend(session) != PMEDIA_BACKEND_SOFT || input.position != bytes) goto fail;
            memset(&info, 0, sizeof(info));
            info.size = sizeof(info);
            if (pm_get_stream_info(session, &info) != PMEDIA_OK || info.duration_us != 300000 ||
                info.audio_codec != PMEDIA_CODEC_IMA_ADPCM || info.channels != channels ||
                pm_pause(session) != PMEDIA_OK || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                sink.events.blocks || pm_resume(session) != PMEDIA_OK) goto fail;
            for (pass = 0; pass < 2; pass++) {
                sink.callback_result = PMEDIA_CALLBACK_STOP;
                if (pm_pump(session, 0, 2000) != PMEDIA_OK || sink.events.blocks != 1 ||
                    pm_pump(session, 0, 2000) != PMEDIA_OK || sink.events.blocks != 1) goto fail;
                sink.callback_result = PMEDIA_OK;
                if (pm_resume(session) != PMEDIA_OK || !media_contract_drain(session) ||
                    sink.cursor != 2400 || sink.events.samples != 2400 ||
                    sink.events.blocks != (test == 0 ? 3 : 5) || sink.events.errors ||
                    sink.events.error_callbacks || sink.events.eof_events != 1 ||
                    pm_pump(session, 0, 2000) != PMEDIA_EOF || sink.events.eof_events != 1) goto fail;
                if (pass == 0) {
                    if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
                    media_ima_reset(&sink, pcm, channels, samples_per_block, 2400, 0);
                }
            }
            g_media_contract_error = "IMA intra-block/boundary/end seek exact PCM";
            for (mode = 0; mode < 6; mode++) {
                target = mode == 0 ? 1 : mode == 1 ? samples_per_block - 1 :
                         mode == 2 ? samples_per_block : mode == 3 ? samples_per_block + 1 : 2400;
                if (pm_seek(session, mode == 5 ? (pm_position)9223372036854775807 :
                            (pm_position)target * 125) != PMEDIA_OK) goto fail;
                media_ima_reset(&sink, pcm, channels, samples_per_block, 2400, target);
                if (!media_contract_drain(session) || sink.cursor != 2400 ||
                    sink.events.samples != 2400 - target || sink.events.errors ||
                    sink.events.error_callbacks || sink.events.eof_events != 1) goto fail;
            }
            if (pm_stop(session) != PMEDIA_OK || pm_pump(session, 0, 2000) != PMEDIA_ERROR_STATE ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
        }
        source.seek = media_fixture_seek;
        source.tell = media_fixture_tell;
        input.position = 0;
        media_ima_reset(&sink, pcm, channels, samples_per_block, 2400, 0);
        sink.callback_result = -1;
        g_media_contract_error = "IMA negative callback";
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
            pm_pump(session, 0, 2000) != PMEDIA_ERROR_CALLBACK || sink.events.blocks != 1 ||
            sink.events.last_error != PMEDIA_ERROR_CALLBACK || sink.events.error_callbacks != 1 ||
            pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        damaged = (unsigned char *)malloc((size_t)bytes + 12);
        if (damaged == NULL) goto fail;
        g_media_contract_error = "IMA malformed block/index/fmt/fact rejects before output";
        if (progress != NULL) progress(g_media_contract_error);
        for (mode = 0; mode < 10; mode++) {
            memcpy(damaged, data, (size_t)bytes);
            if (mode == 0) damaged[62] = 89;
            if (mode == 1) damaged[60 + 512 + channels * 4 - 1] = 1;
            if (mode == 2) media_ima_put16(damaged + 38, samples_per_block - 1);
            if (mode == 3) media_ima_put16(damaged + 32, 511);
            if (mode == 4) media_ima_put16(damaged + 34, 3);
            if (mode == 5) media_ima_put16(damaged + 36, 1);
            if (mode == 6) media_ima_put32(damaged + 48, 0);
            if (mode == 7) media_ima_put32(damaged + 48, 0xffffffffUL);
            if (mode == 8) media_ima_put32(damaged + 56, bytes - 61);
            if (mode == 9) media_ima_put32(damaged + 44, 2);
            if (!media_ima_reject(damaged, bytes, PMEDIA_ERROR_FORMAT)) goto fail;
        }
        if (!media_ima_reject(data, bytes - 1, PMEDIA_ERROR_FORMAT)) goto fail;
        memcpy(damaged, data, (size_t)bytes);
        memcpy(damaged + 40, "JUNK", 4);
        memcpy(damaged + bytes, "fact", 4);
        media_ima_put32(damaged + 4, bytes + 4);
        media_ima_put32(damaged + bytes + 4, 0xffffffffUL);
        media_ima_put32(damaged + bytes + 8, 2400);
        if (!media_ima_reject(damaged, bytes + 12, PMEDIA_ERROR_FORMAT)) goto fail;
        media_ima_put32(damaged + bytes + 4, 4);
        input.data = damaged;
        input.bytes = bytes + 12;
        input.position = 0;
        media_ima_reset(&sink, pcm, channels, samples_per_block, 2400, 0);
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
            !media_contract_drain(session) || sink.cursor != 2400 || sink.events.errors ||
            sink.events.eof_events != 1 || pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        input.bytes = bytes;
        g_media_contract_error = "IMA without fact emits all coded samples";
        memcpy(damaged, data, (size_t)bytes);
        memcpy(damaged + 40, "JUNK", 4);
        input.data = damaged;
        input.position = 0;
        media_ima_reset(&sink, pcm, channels, samples_per_block, pcm_bytes / (channels * 2), 0);
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
            !media_contract_drain(session) || sink.cursor != pcm_bytes / (channels * 2) ||
            sink.events.errors || sink.events.eof_events != 1 || pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        if (test == 0) {
            g_media_contract_error = "IMA bounded full-block capacity and oversized reject";
            if (progress != NULL) progress(g_media_contract_error);
            large = (unsigned char *)calloc(1, 2108);
            if (large == NULL) goto fail;
            memcpy(large, data, 60);
            media_ima_put32(large + 4, 2100);
            media_ima_put16(large + 32, 2048);
            media_ima_put16(large + 38, 4089);
            media_ima_put32(large + 56, 2048);
            if (!media_ima_reject(large, 2108, PMEDIA_ERROR_LIMIT)) goto fail;
            media_ima_put32(large + 4, 1076);
            media_ima_put16(large + 32, 1024);
            media_ima_put16(large + 38, 2041);
            media_ima_put32(large + 48, 2041);
            media_ima_put32(large + 56, 1024);
            input.data = large;
            input.bytes = 1084;
            input.position = 0;
            media_ima_reset(&sink, NULL, 1, 2041, 2041, 0);
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
                !media_contract_drain(session) || sink.events.samples != 2041 ||
                sink.events.blocks != 1 || sink.events.errors || pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
            free(large);
            large = NULL;
        }
        free(damaged);
        damaged = NULL;
        free(pcm);
        pcm = NULL;
        free(data);
        data = NULL;
    }
    g_media_contract_error = "IMA PCM/block/seek contract passed";
    return TRUE;
fail:
    if (session != NULL) pm_close(session);
    if (large != NULL) free(large);
    if (damaged != NULL) free(damaged);
    if (pcm != NULL) free(pcm);
    if (data != NULL) free(data);
    return FALSE;
}

BOOL test1335_media_amr_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = { L"amr-nb.amr", L"amr-wb.amr" };
    media_fixture_source input;
    media_compressed_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_probe_info unchanged;
    pm_stream_info info;
    pm_session session;
    unsigned char *data;
    int bytes;
    int test;
    int iteration;
    int pass;
    int result;
    int expected_blocks;
    int frame_samples;
    int rate;
    int codec;
    unsigned long first_pcm_hash;
    DWORD start;

    session = NULL;
    data = NULL;
    for (test = 0; test < 2; test++) {
        g_media_compressed_error = test == 0 ? "AMR-NB mono 8k" : "AMR-WB mono 16k";
        if (progress != NULL) progress(g_media_compressed_error);
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        frame_samples = test == 0 ? 160 : 320;
        expected_blocks = test == 0 ? 7 : 6;
        rate = test == 0 ? 8000 : 16000;
        codec = test == 0 ? PMEDIA_CODEC_AMR_NB : PMEDIA_CODEC_AMR_WB;
        media_compressed_init(&input, &sink, &source, &output, &options);
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 1;
        input.position = 1;
        memset(&probe, 0, sizeof(probe));
        probe.size = sizeof(probe);
        result = pm_probe(&source, &probe);
        if (result != PMEDIA_OK || input.position != 1 || probe.stream.container != PMEDIA_CONTAINER_AMR ||
            probe.stream.has_video || !probe.stream.has_audio || probe.stream.audio_codec != codec ||
            probe.stream.channels != 1 || probe.stream.sample_rate != rate) goto fail;
        for (iteration = 0; iteration < 3; iteration++) {
            first_pcm_hash = 0;
            input.position = 0;
            source.seek = iteration == 0 ? NULL : media_fixture_seek;
            source.tell = iteration == 0 ? NULL : media_fixture_tell;
            options.backend = iteration == 0 ? PMEDIA_BACKEND_AUTO : PMEDIA_BACKEND_SOFT;
            memset(&sink, 0, sizeof(sink));
            sink.channels = 1;
            sink.sample_rate = rate;
            sink.amr_frame_samples = frame_samples;
            result = pm_open(&source, &options, &output, &session);
            if (result != PMEDIA_OK || pm_get_backend(session) != PMEDIA_BACKEND_SOFT ||
                input.position != bytes) goto fail;
            memset(&info, 0, sizeof(info));
            info.size = sizeof(info);
            if (pm_get_stream_info(session, &info) != PMEDIA_OK || info.has_video || !info.has_audio ||
                info.container != PMEDIA_CONTAINER_AMR || info.audio_codec != codec ||
                info.channels != 1 || info.sample_rate != rate || pm_pause(session) != PMEDIA_OK ||
                pm_pump(session, 0, 2000) != PMEDIA_OK || sink.events.blocks || sink.frames ||
                pm_resume(session) != PMEDIA_OK) goto fail;
            for (pass = 0; pass < 2; pass++) {
                if (pass == 0) {
                    sink.audio_callback_result = PMEDIA_CALLBACK_STOP;
                    start = GetTickCount();
                    while (sink.events.blocks == 0 && GetTickCount() - start < 5000) {
                        if (pm_pump(session, 0, 2000) != PMEDIA_OK) goto fail;
                    }
                    if (sink.events.blocks != 1 || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                        sink.events.blocks != 1) goto fail;
                    sink.audio_callback_result = PMEDIA_OK;
                    if (pm_resume(session) != PMEDIA_OK) goto fail;
                }
                result = media_contract_drain(session);
                if (sink.assertion[0] && progress != NULL) progress(sink.assertion);
                _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                          "AMR drain: case=%d iteration=%d pass=%d ok=%d blocks=%d samples=%d energy=%lu crossings=%d errors=%d eof=%d",
                          test, iteration, pass, result, sink.events.blocks, sink.events.samples,
                          sink.audio_magnitude, sink.amr_zero_crossings, sink.events.errors, sink.events.eof_events);
                g_media_compressed_error = g_media_compressed_detail;
                if (progress != NULL) progress(g_media_compressed_error);
                /* Independent desktop decode: energy ~1.97M/3.23M, tail crossings 87/70.
                 * Allow fixed float-to-S16/decoder rounding, not silent or wrong-rate PCM. */
                if (!result || sink.frames || sink.events.blocks != expected_blocks ||
                    sink.events.samples != expected_blocks * frame_samples ||
                    sink.audio_magnitude < (test == 0 ? 1750000UL : 2900000UL) ||
                    sink.audio_magnitude > (test == 0 ? 2160000UL : 3550000UL) ||
                    sink.amr_zero_crossings < (test == 0 ? 70 : 60) ||
                    sink.amr_zero_crossings > (test == 0 ? 100 : 80) ||
                    sink.events.errors || sink.events.error_callbacks || sink.events.eof_events != 1 ||
                    pm_pump(session, 0, 2000) != PMEDIA_EOF || sink.events.eof_events != 1) goto fail;
                if (pass == 0) {
                    first_pcm_hash = sink.amr_pcm_hash;
                    g_media_compressed_error = "AMR seek zero after EOF";
                    if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
                    memset(&sink, 0, sizeof(sink));
                    sink.channels = 1;
                    sink.sample_rate = rate;
                    sink.amr_frame_samples = frame_samples;
                } else if (sink.amr_pcm_hash != first_pcm_hash) {
                    g_media_compressed_error = "AMR seek replay PCM differs from fresh decoder";
                    goto fail;
                }
            }
            if (pm_stop(session) != PMEDIA_OK || pm_pump(session, 0, 2000) != PMEDIA_ERROR_STATE ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
        }
        g_media_compressed_error = "AMR negative audio callback propagates";
        if (progress != NULL) progress(g_media_compressed_error);
        input.position = 0;
        memset(&sink, 0, sizeof(sink));
        sink.channels = 1;
        sink.sample_rate = rate;
        sink.amr_frame_samples = frame_samples;
        sink.audio_callback_result = -1;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
        start = GetTickCount();
        result = PMEDIA_OK;
        while (result == PMEDIA_OK && GetTickCount() - start < 5000)
            result = pm_pump(session, 0, 2000);
        if (result != PMEDIA_ERROR_CALLBACK || sink.events.blocks != 1 || sink.frames ||
            sink.events.last_error != PMEDIA_ERROR_CALLBACK || sink.events.error_callbacks != 1 ||
            pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        g_media_compressed_error = "truncated AMR header: unchanged probe and no output";
        if (progress != NULL) progress(g_media_compressed_error);
        input.position = 0;
        input.bytes = 4;
        memset(&sink, 0, sizeof(sink));
        memset(&probe, 0xa5, sizeof(probe));
        probe.size = sizeof(probe);
        unchanged = probe;
        if (pm_probe(&source, &probe) != PMEDIA_ERROR_FORMAT || input.position != 0 ||
            memcmp(&probe, &unchanged, sizeof(probe)) ||
            pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_FORMAT || session != NULL ||
            sink.events.last_error != PMEDIA_ERROR_FORMAT || sink.events.error_callbacks != 1 ||
            sink.frames || sink.events.blocks) goto fail;
        free(data);
        data = NULL;
    }
    g_media_compressed_error = "AMR contract passed";
    return TRUE;

fail:
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    return FALSE;
}

BOOL test1333_media_mjpeg_mp3_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = { L"mjpeg-mp3.avi", L"mp3-mono.mp3" };
    media_fixture_source input;
    media_compressed_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_probe_info unchanged;
    pm_stream_info info;
    pm_session session;
    unsigned char *data;
    int bytes;
    int test;
    int pass;
    int iteration;
    int result;
    int expected_samples;
    DWORD start;

    session = NULL;
    data = NULL;
    for (test = 0; test < 2; test++) {
        g_media_compressed_error = test == 0 ? "AVI MJPEG full-range + MP3 stereo" :
                                              "raw MP3 44.1k mono and gapless sample count";
        if (progress != NULL) progress(g_media_compressed_error);
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        media_compressed_init(&input, &sink, &source, &output, &options);
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 7;
        input.position = 1;
        memset(&probe, 0, sizeof(probe));
        probe.size = sizeof(probe);
        result = pm_probe(&source, &probe);
        if (progress != NULL) progress(result == PMEDIA_OK ? "probe OK" : "probe failed");
        if (result != PMEDIA_OK || input.position != 1 ||
            probe.stream.container != (test == 0 ? PMEDIA_CONTAINER_AVI : PMEDIA_CONTAINER_RAW) ||
            probe.stream.has_video != (test == 0) || !probe.stream.has_audio ||
            probe.stream.audio_codec != PMEDIA_CODEC_MP3 ||
            probe.stream.channels != (test == 0 ? 2 : 1) ||
            probe.stream.sample_rate != (test == 0 ? 48000 : 44100) ||
            (test == 0 && (probe.stream.video_codec != PMEDIA_CODEC_MJPEG ||
             probe.stream.width != 320 || probe.stream.height != 240))) goto fail;
        for (iteration = 0; iteration < 3; iteration++) {
            input.position = 0;
            source.seek = iteration == 0 ? NULL : media_fixture_seek;
            source.tell = iteration == 0 ? NULL : media_fixture_tell;
            options.backend = test == 0 ? PMEDIA_BACKEND_AUTO : PMEDIA_BACKEND_SOFT;
            memset(&sink, 0, sizeof(sink));
            sink.width = 320;
            sink.height = 240;
            sink.channels = test == 0 ? 2 : 1;
            sink.full_range = test == 0;
            /* Pinned AVI mux has a zero-length slot after its first frame. */
            sink.video_gap_us = test == 0 ? 200000 : 0;
            sink.sample_rate = test == 0 ? 48000 : 44100;
            result = pm_open(&source, &options, &output, &session);
            if (result != PMEDIA_OK || pm_get_backend(session) != PMEDIA_BACKEND_SOFT ||
                input.position != bytes) goto fail;
            memset(&info, 0, sizeof(info));
            info.size = sizeof(info);
            if (pm_get_stream_info(session, &info) != PMEDIA_OK ||
                info.container != probe.stream.container ||
                info.audio_codec != PMEDIA_CODEC_MP3 || info.sample_rate != sink.sample_rate ||
                info.channels != sink.channels || pm_pause(session) != PMEDIA_OK ||
                pm_pump(session, 0, 2000) != PMEDIA_OK || sink.events.blocks != 0 ||
                sink.frames != 0 || pm_resume(session) != PMEDIA_OK) goto fail;
            for (pass = 0; pass < 2; pass++) {
                if (test == 1 && pass == 0) {
                    sink.audio_callback_result = PMEDIA_CALLBACK_STOP;
                    start = GetTickCount();
                    while (sink.events.blocks == 0 && GetTickCount() - start < 5000) {
                        if (pm_pump(session, 0, 2000) != PMEDIA_OK) goto fail;
                    }
                    if (sink.events.blocks != 1 || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                        sink.events.blocks != 1) goto fail;
                    sink.audio_callback_result = PMEDIA_OK;
                    if (pm_resume(session) != PMEDIA_OK) goto fail;
                }
                result = media_contract_drain(session);
                if (sink.assertion[0] != '\0' && progress != NULL) progress(sink.assertion);
                _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                          "MJPEG/MP3 drain: case=%d iteration=%d pass=%d ok=%d frames=%d samples=%d energy=%lu errors=%d eof=%d",
                          test, iteration, pass, result, sink.frames, sink.events.samples,
                          sink.audio_magnitude, sink.events.errors, sink.events.eof_events);
                g_media_compressed_error = g_media_compressed_detail;
                if (progress != NULL) progress(g_media_compressed_error);
                expected_samples = test == 0 ? 29952 : 26460;
                if (!result || sink.events.errors || sink.events.error_callbacks ||
                    sink.frames != (test == 0 ? 3 : 0) || sink.events.samples != expected_samples ||
                    sink.audio_magnitude < 1000000 || sink.events.eof_events != 1 ||
                    pm_pump(session, 0, 2000) != PMEDIA_EOF || sink.events.eof_events != 1) goto fail;
                if (pass == 0) {
                    if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
                    memset(&sink, 0, sizeof(sink));
                    sink.width = 320;
                    sink.height = 240;
                    sink.channels = test == 0 ? 2 : 1;
                    sink.full_range = test == 0;
                    sink.video_gap_us = test == 0 ? 200000 : 0;
                    sink.sample_rate = test == 0 ? 48000 : 44100;
                }
            }
            if (pm_stop(session) != PMEDIA_OK ||
                pm_pump(session, 0, 2000) != PMEDIA_ERROR_STATE ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
        }
        g_media_compressed_error = "reject truncated AVI/MP3 header without output";
        if (progress != NULL) progress(g_media_compressed_error);
        input.position = 0;
        input.bytes = 8;
        memset(&sink, 0, sizeof(sink));
        if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_FORMAT ||
            session != NULL || sink.events.error_callbacks != 1 ||
            sink.events.last_error != PMEDIA_ERROR_FORMAT || sink.frames || sink.events.blocks) goto fail;
        free(data);
        data = NULL;
    }
    g_media_compressed_error = "reject MJPEG 4:2:2 and application dimension limit";
    if (progress != NULL) progress(g_media_compressed_error);
    data = media_compressed_load(L"mjpeg422.avi", &bytes);
    if (data == NULL) goto fail;
    media_compressed_init(&input, &sink, &source, &output, &options);
    input.data = data;
    input.bytes = bytes;
    memset(&probe, 0xa5, sizeof(probe));
    probe.size = sizeof(probe);
    unchanged = probe;
    if (pm_probe(&source, &probe) != PMEDIA_ERROR_UNSUPPORTED ||
        memcmp(&probe, &unchanged, sizeof(probe)) != 0 ||
        pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_UNSUPPORTED ||
        session != NULL || sink.events.error_callbacks != 1 || sink.frames || sink.events.blocks) goto fail;
    free(data);
    data = media_compressed_load(names[0], &bytes);
    if (data == NULL) goto fail;
    media_compressed_init(&input, &sink, &source, &output, &options);
    input.data = data;
    input.bytes = bytes;
    options.max_video_width = 160;
    options.max_video_height = 120;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_LIMIT ||
        session != NULL || sink.events.last_error != PMEDIA_ERROR_LIMIT ||
        sink.events.error_callbacks != 1 || sink.frames || sink.events.blocks) goto fail;
    free(data);
    g_media_compressed_error = "MJPEG/MP3 contract passed";
    return TRUE;

fail:
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    return FALSE;
}

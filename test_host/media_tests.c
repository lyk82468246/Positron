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
    int explicit_video_pts;
    pm_position expected_video_pts[3];
    unsigned int inferred_video_mask;
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

static unsigned char *media_compressed_load_bounded(const WCHAR *name, int *out_bytes, DWORD limit)
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
    if (bytes == 0 || bytes > limit) { CloseHandle(file); return NULL; }
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

static unsigned char *media_compressed_load(const WCHAR *name, int *out_bytes)
{
    return media_compressed_load_bounded(name, out_bytes, 32768);
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
    pm_position expected_pts;
    int inferred;

    sink = (media_compressed_sink *)context;
    duration = sink->video_duration_us > 0 ? sink->video_duration_us : 200000;
    if (sink->explicit_video_pts && sink->frames >= 3) goto invalid;
    expected_pts = sink->explicit_video_pts ? sink->expected_video_pts[sink->frames] :
        sink->video_start_us + (pm_position)sink->frames * duration +
        (sink->frames > 0 ? sink->video_gap_us : 0);
    inferred = sink->explicit_video_pts ?
        !!(sink->inferred_video_mask & (1U << sink->frames)) :
        (sink->inferred_last_frame && sink->frames == 2);
    if (frame == NULL || frame->size != sizeof(*frame) ||
        frame->format != PMEDIA_PIXEL_I420 || frame->width != sink->width ||
        frame->height != sink->height || frame->flags & PMEDIA_FRAME_INTERLACED ||
        !!(frame->flags & PMEDIA_FRAME_FULL_RANGE) != sink->full_range ||
        !!(frame->flags & PMEDIA_FRAME_PTS_INFERRED) !=
            inferred ||
        frame->pts_us != expected_pts ||
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

static void media_clock_reset_sink(media_compressed_sink *sink, int test)
{
    memset(sink, 0, sizeof(*sink));
    sink->width = test == 0 ? 320 : (test == 2 ? 352 : 640);
    sink->height = test == 0 ? 240 : (test == 2 ? 288 : 480);
    sink->video_start_us = test == 3 ? 400000 : 0;
}

/* Synthetic host clocks: no real-time sleeps and no scheduling policy here. */
static BOOL media_clock_expect(pm_session session, media_compressed_sink *sink,
                              pm_position clock, int frames, int expected)
{
    int i;
    int result;
    for (i = 0; i < 128; i++) {
        result = pm_pump(session, clock, 2000);
        if (result < 0 || result == PMEDIA_EOF || result == PMEDIA_WOULD_BLOCK ||
            (expected == PMEDIA_OK && sink->frames == frames)) break;
    }
    if (result == expected && sink->frames == frames && !sink->assertion[0]) return TRUE;
    _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
              "clock assertion: clock=%I64d rc=%d expected=%d frames=%d expected=%d %s",
              clock, result, expected, sink->frames, frames, sink->assertion);
    g_media_compressed_detail[sizeof(g_media_compressed_detail) - 1] = '\0';
    g_media_compressed_error = g_media_compressed_detail;
    return FALSE;
}

static int media_clock_pts_video(void *context, const pm_video_frame *frame)
{
    media_compressed_sink *sink;
    char detail[160];
    int result;
    sink = (media_compressed_sink *)context;
    if (frame != NULL && sink->aac_progress != NULL) {
        _snprintf(detail, sizeof(detail), "clock PTS frame=%d pts=%I64d duration=%I64d flags=%lu",
                  sink->frames, frame->pts_us, frame->duration_us, frame->flags);
        detail[sizeof(detail) - 1] = '\0';
        sink->aac_progress(detail);
    }
    result = media_compressed_video(context, frame);
    return result;
}

static void media_clock_pts_reset(media_compressed_sink *sink, int test, int scheduled,
                                  void (*progress)(const char *))
{
    memset(sink, 0, sizeof(*sink));
    sink->width = 320;
    sink->height = 240;
    sink->explicit_video_pts = 1;
    sink->expected_video_pts[0] = test == 1 ? -1 : (test == 4 ? -1000 : 21000);
    sink->expected_video_pts[1] = test == 1 ? -1 :
        (test == 3 ? 0 : (test == 5 ? 621000 : (test == 6 ? 21000 : 221000)));
    sink->expected_video_pts[2] = test == 1 ? -1 : (test == 5 ? 821000 : 421000);
    sink->inferred_video_mask = test == 2 ? 6U : 0;
    /* Explicit compatibility control: legacy demux may fill unknown packets.
     * Clock mode must not turn this manufactured zero origin into a clock. */
    if (!scheduled && test == 1) {
        sink->expected_video_pts[0] = 0;
        sink->expected_video_pts[1] = 200000;
        sink->expected_video_pts[2] = 400000;
        sink->inferred_video_mask = 6U;
    }
    sink->aac_progress = progress;
}

static BOOL media_clock_pts_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = {
        L"flv-clock-base.flv", L"flv-clock-unknown.flv", L"flv-clock-late-unknown.flv",
        L"flv-clock-backward.flv", L"flv-clock-negative.flv", L"flv-clock-gap.flv",
        L"flv-clock-duplicate.flv"
    };
    media_fixture_source input;
    media_compressed_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_session session;
    unsigned char *data;
    pm_position deadline;
    int bytes;
    int test;
    int iteration;
    int pass;
    int expected_frames;

    session = NULL;
    data = NULL;
    for (test = 0; test < 7; test++) {
        data = media_compressed_load(names[test], &bytes);
        g_media_compressed_error = "clock PTS fixture load";
        if (data == NULL) goto fail;
        for (iteration = 0; iteration < 2; iteration++) {
            media_compressed_init(&input, &sink, &source, &output, &options);
            media_clock_pts_reset(&sink, test, 0, progress);
            input.data = data;
            input.bytes = bytes;
            input.read_chunk = 7;
            memset(&probe, 0, sizeof(probe));
            probe.size = sizeof(probe);
            input.position = 1;
            if (pm_probe(&source, &probe) != PMEDIA_OK || input.position != 1 ||
                probe.stream.container != PMEDIA_CONTAINER_FLV || !probe.stream.has_video ||
                probe.stream.has_audio || probe.stream.video_codec != PMEDIA_CODEC_H264 ||
                probe.stream.width != 320 || probe.stream.height != 240) goto fail;
            source.seek = iteration ? media_fixture_seek : NULL;
            source.tell = iteration ? media_fixture_tell : NULL;
            input.position = 0;
            output.video = media_clock_pts_video;
            _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                      "clock PTS case=%d seekable=%d decode-only control", test, iteration);
            g_media_compressed_error = g_media_compressed_detail;
            if (progress != NULL) progress(g_media_compressed_error);
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
            input.read_mode = 2;
            if (!media_clock_expect(session, &sink, 0, 3, PMEDIA_EOF) ||
                sink.events.errors || sink.events.error_callbacks || sink.events.eof_events != 1 ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
            for (pass = 0; pass < 3; pass++) {
                input.read_mode = 0;
                input.position = 0;
                media_clock_pts_reset(&sink, test, 1, progress);
                options.flags = PMEDIA_OPEN_SCHEDULE_VIDEO;
                _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                          "clock PTS case=%d seekable=%d scheduled pass=%d", test, iteration, pass);
                g_media_compressed_error = g_media_compressed_detail;
                if (progress != NULL) progress(g_media_compressed_error);
                if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
                input.read_mode = 2;
                if (test == 1 || test == 3 || test == 4) {
                    expected_frames = test == 3 ? 1 : 0;
                    if (expected_frames && !media_clock_expect(session, &sink, 1000000, 1, PMEDIA_OK)) goto fail;
                    if (!media_clock_expect(session, &sink, 1000000, expected_frames, PMEDIA_ERROR_FORMAT) ||
                        sink.events.error_callbacks != 1 || sink.events.error_events != 1 ||
                        sink.events.last_error != PMEDIA_ERROR_FORMAT || sink.events.errors ||
                        sink.events.eof_events || strcmp(pm_last_error(session), test == 3 ?
                        "scheduled video presentation timestamp moved backward" :
                        "scheduled video requires a nonnegative presentation timestamp") != 0 ||
                        pm_pump(session, 2000000, 0) != PMEDIA_ERROR_STATE ||
                        pm_pause(session) != PMEDIA_ERROR_STATE || pm_resume(session) != PMEDIA_ERROR_STATE ||
                        pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
                        sink.events.error_callbacks != 1 || sink.events.error_events != 1 ||
                        sink.frames != expected_frames || pm_stop(session) != PMEDIA_OK ||
                        pm_stop(session) != PMEDIA_OK || sink.events.stopped_events != 1 ||
                        pm_close(session) != PMEDIA_OK) goto fail;
                    session = NULL;
                    if (sink.events.eof_events || sink.events.error_callbacks != 1 ||
                        sink.frames != expected_frames) goto fail;
                } else {
                    if (!media_clock_expect(session, &sink, 1000000, 1, PMEDIA_OK)) goto fail;
                    deadline = 1000000 + sink.expected_video_pts[1] - sink.expected_video_pts[0];
                    if (deadline > 1000000 &&
                        !media_clock_expect(session, &sink, deadline - 1, 1, PMEDIA_WOULD_BLOCK)) goto fail;
                    if (!media_clock_expect(session, &sink, deadline, 2, PMEDIA_OK)) goto fail;
                    deadline = 1000000 + sink.expected_video_pts[2] - sink.expected_video_pts[0];
                    if (!media_clock_expect(session, &sink, deadline - 1, 2, PMEDIA_WOULD_BLOCK) ||
                        !media_clock_expect(session, &sink, deadline, 3, PMEDIA_OK) ||
                        !media_clock_expect(session, &sink, deadline, 3, PMEDIA_EOF) ||
                        sink.events.eof_events != 1 || sink.events.errors || sink.events.error_callbacks ||
                        pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK) goto fail;
                    media_clock_pts_reset(&sink, test, 1, progress);
                    if (pm_pump(session, 0, 0) != PMEDIA_OK || sink.frames ||
                        pm_resume(session) != PMEDIA_OK ||
                        !media_clock_expect(session, &sink, 0, 1, PMEDIA_OK) ||
                        pm_stop(session) != PMEDIA_OK || pm_close(session) != PMEDIA_OK) goto fail;
                    session = NULL;
                    if (sink.events.errors || sink.events.error_callbacks || sink.frames != 1 ||
                        sink.events.eof_events) goto fail;
                }
            }
        }
        free(data);
        data = NULL;
    }
    return TRUE;
fail:
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    return FALSE;
}

BOOL test1352_media_clock_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = {
        L"mpeg4-simple.mp4", L"main-vga.mp4", L"h263-cif.avi", L"flv-main-vga.flv"
    };
    static const WCHAR *rejected[] = {
        L"baseline-aac.mp4", L"annexb-main-vga.h264", L"aac-lc.aac", L"mpeg1-mp2.mpg"
    };
    media_fixture_source input;
    media_compressed_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_session session;
    unsigned char *data;
    pm_position maximum;
    int bytes;
    int test;
    int iteration;
    int attempt;
    int result;

    session = NULL;
    data = NULL;
    maximum = (((pm_position)0x7fffffff) << 32) | (pm_position)0xffffffffUL;
    for (test = 0; test < 4; test++) {
        data = media_compressed_load(names[test], &bytes);
        g_media_compressed_error = "scheduled video fixture load";
        if (data == NULL) goto fail;
        for (iteration = 0; iteration < 2; iteration++) {
            media_compressed_init(&input, &sink, &source, &output, &options);
            media_clock_reset_sink(&sink, test);
            input.data = data;
            input.bytes = bytes;
            source.seek = iteration ? media_fixture_seek : NULL;
            source.tell = iteration ? media_fixture_tell : NULL;
            options.flags = PMEDIA_OPEN_SCHEDULE_VIDEO;
            _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                      "scheduled video case=%d seekable=%d open/deadline", test, iteration);
            g_media_compressed_error = g_media_compressed_detail;
            if (progress != NULL) progress(g_media_compressed_error);
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
                pm_get_backend(session) != PMEDIA_BACKEND_SOFT) goto fail;
            input.read_mode = 2;
            if (!media_clock_expect(session, &sink, -1, 0, PMEDIA_ERROR_ARGUMENT) ||
                !media_clock_expect(session, &sink, 1000000, 1, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, 1000000, 1, PMEDIA_WOULD_BLOCK) ||
                pm_pump(session, 1200000, -1) != PMEDIA_ERROR_ARGUMENT ||
                !media_clock_expect(session, &sink, 1199999, 1, PMEDIA_WOULD_BLOCK) ||
                !media_clock_expect(session, &sink, 999999, 1, PMEDIA_ERROR_ARGUMENT) ||
                pm_seek(session, 100000) != PMEDIA_ERROR_NOT_SEEKABLE ||
                pm_resume(session) != PMEDIA_OK ||
                !media_clock_expect(session, &sink, 1200000, 2, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, 1399999, 2, PMEDIA_WOULD_BLOCK) ||
                !media_clock_expect(session, &sink, 1400000, 3, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, 1400000, 3, PMEDIA_EOF) ||
                pm_pump(session, 1400000, 0) != PMEDIA_EOF ||
                sink.events.eof_events != 1 || sink.events.errors) goto fail;
            if (sink.events.error_callbacks || sink.events.error_events) goto fail;

            g_media_compressed_error = "scheduled video explicit/callback pause and pending rewind";
            if (progress != NULL) progress(g_media_compressed_error);
            if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
            media_clock_reset_sink(&sink, test);
            if (!media_clock_expect(session, &sink, 0, 1, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, 0, 1, PMEDIA_WOULD_BLOCK) ||
                pm_pause(session) != PMEDIA_OK || pm_pause(session) != PMEDIA_OK ||
                pm_pump(session, -1, 0) != PMEDIA_OK ||
                pm_pump(session, 9999999, 0) != PMEDIA_OK || sink.frames != 1 ||
                pm_resume(session) != PMEDIA_OK ||
                !media_clock_expect(session, &sink, 9000000, 1, PMEDIA_WOULD_BLOCK) ||
                !media_clock_expect(session, &sink, 9199999, 1, PMEDIA_WOULD_BLOCK)) goto fail;
            sink.callback_result = PMEDIA_CALLBACK_STOP;
            if (!media_clock_expect(session, &sink, 9200000, 2, PMEDIA_OK) ||
                pm_pump(session, 99999999, 0) != PMEDIA_OK || sink.frames != 2 ||
                pm_resume(session) != PMEDIA_OK) goto fail;
            sink.callback_result = 0;
            if (!media_clock_expect(session, &sink, 10000000, 2, PMEDIA_WOULD_BLOCK) ||
                !media_clock_expect(session, &sink, 10200000, 3, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, 10200000, 3, PMEDIA_EOF) ||
                sink.events.eof_events != 1 || sink.events.errors) goto fail;
            if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
            media_clock_reset_sink(&sink, test);
            if (!media_clock_expect(session, &sink, 0, 1, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, 0, 1, PMEDIA_WOULD_BLOCK) ||
                pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK) goto fail;
            media_clock_reset_sink(&sink, test);
            if (pm_pump(session, 100, 0) != PMEDIA_OK || sink.frames ||
                pm_resume(session) != PMEDIA_OK ||
                !media_clock_expect(session, &sink, 0, 1, PMEDIA_OK)) goto fail;

            g_media_compressed_error = "scheduled video INT64 boundary and late frames without dropping";
            if (progress != NULL) progress(g_media_compressed_error);
            if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
            media_clock_reset_sink(&sink, test);
            if (!media_clock_expect(session, &sink, maximum - 400000, 1, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, maximum - 200001, 1, PMEDIA_WOULD_BLOCK) ||
                !media_clock_expect(session, &sink, maximum - 200000, 2, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, maximum, 3, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, maximum, 3, PMEDIA_EOF)) goto fail;
            if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
            media_clock_reset_sink(&sink, test);
            if (!media_clock_expect(session, &sink, 0, 1, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, maximum, 2, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, maximum, 3, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, maximum, 3, PMEDIA_EOF)) goto fail;

            g_media_compressed_error = "scheduled video stop releases pending frame and close stays quiet";
            if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
            media_clock_reset_sink(&sink, test);
            if (!media_clock_expect(session, &sink, 0, 1, PMEDIA_OK) ||
                !media_clock_expect(session, &sink, 0, 1, PMEDIA_WOULD_BLOCK) ||
                pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK ||
                pm_pump(session, maximum, 0) != PMEDIA_ERROR_STATE ||
                pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
                pm_pause(session) != PMEDIA_ERROR_STATE || pm_resume(session) != PMEDIA_ERROR_STATE ||
                sink.frames != 1 || sink.events.stopped_events != 1 || sink.events.errors ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
            if (sink.frames != 1 || sink.events.eof_events || sink.events.errors) goto fail;

            input.read_mode = 0;
            input.position = 0;
            media_clock_reset_sink(&sink, test);
            g_media_compressed_error = "scheduled video negative callback enters one-shot failed state";
            if (progress != NULL) progress(g_media_compressed_error);
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
            sink.callback_result = -1;
            result = PMEDIA_OK;
            for (attempt = 0; attempt < 128 && result == PMEDIA_OK; attempt++)
                result = pm_pump(session, 0, 2000);
            if (result != PMEDIA_ERROR_CALLBACK || sink.frames != 1 ||
                sink.events.errors || sink.events.error_callbacks != 1 ||
                sink.events.error_events != 1 || sink.events.last_error != PMEDIA_ERROR_CALLBACK ||
                pm_pump(session, 0, 0) != PMEDIA_ERROR_STATE ||
                pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
                pm_stop(session) != PMEDIA_OK || pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
            if (sink.events.error_callbacks != 1 || sink.events.error_events != 1 ||
                sink.frames != 1 || sink.events.eof_events) goto fail;
        }
        free(data);
        data = NULL;
    }
    g_media_compressed_error = "scheduled video unsupported input and option guards";
    if (progress != NULL) progress(g_media_compressed_error);
    media_compressed_init(&input, &sink, &source, &output, &options);
    options.flags = 8;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_ARGUMENT ||
        input.position || session != NULL) goto fail;
    options.flags = PMEDIA_OPEN_SCHEDULE_VIDEO;
    options.backend = PMEDIA_BACKEND_AUTO;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_ARGUMENT || input.position) goto fail;
    options.backend = PMEDIA_BACKEND_NATIVE;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_ARGUMENT || input.position) goto fail;
    options.backend = PMEDIA_BACKEND_SOFT;
    output.video = NULL;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_ARGUMENT || input.position) goto fail;
    output.video = media_compressed_video;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_UNSUPPORTED || session != NULL) goto fail;
    for (test = 0; test < 4; test++) {
        data = media_compressed_load(rejected[test], &bytes);
        if (data == NULL) goto fail;
        media_compressed_init(&input, &sink, &source, &output, &options);
        input.data = data;
        input.bytes = bytes;
        options.flags = PMEDIA_OPEN_SCHEDULE_VIDEO;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_UNSUPPORTED ||
            session != NULL || sink.frames || sink.events.blocks || sink.events.eof_events) goto fail;
        free(data);
        data = NULL;
    }
    if (!media_clock_pts_contract(progress)) return FALSE;
    g_media_compressed_error = "scheduled video contract passed";
    if (progress != NULL) progress(g_media_compressed_error);
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

static int media_annexb_video(void *context, const pm_video_frame *frame)
{
    media_compressed_sink *sink;
    int plane;
    int width;
    int height;
    int x;
    int y;
    int target;
    int value;

    sink = (media_compressed_sink *)context;
    if (frame == NULL || frame->size != sizeof(*frame) ||
        frame->format != PMEDIA_PIXEL_I420 || frame->width != sink->width ||
        frame->height != sink->height || frame->pts_us != -1 ||
        frame->duration_us != sink->video_duration_us ||
        (frame->flags & (PMEDIA_FRAME_INTERLACED | PMEDIA_FRAME_FULL_RANGE | PMEDIA_FRAME_PTS_INFERRED)) ||
        !!(frame->flags & PMEDIA_FRAME_KEY) != (sink->frames % 3 == 0)) goto invalid;
    for (plane = 0; plane < 3; plane++) {
        width = plane == 0 ? frame->width : frame->width / 2;
        height = plane == 0 ? frame->height : frame->height / 2;
        target = plane == 0 ? 81 : (plane == 1 ? 90 : 240);
        if (frame->plane[plane] == NULL || frame->stride[plane] < width) goto invalid;
        for (y = 0; y < height; y += 17) {
            for (x = 0; x < width; x += 17) {
                value = frame->plane[plane][y * frame->stride[plane] + x];
                if (value < target - 1 || value > target + 1) goto invalid;
                sink->replay_video_hash = sink->replay_video_hash * 33UL + value;
            }
        }
    }
    sink->frames++;
    return sink->callback_result;
invalid:
    if (frame != NULL) {
        _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                  "AnnexB frame=%d pts=%I64d duration=%I64d flags=%lu size=%dx%d",
                  sink->frames, frame->pts_us, frame->duration_us, frame->flags,
                  frame->width, frame->height);
        g_media_compressed_detail[sizeof(g_media_compressed_detail) - 1] = '\0';
        g_media_compressed_error = g_media_compressed_detail;
        strcpy(sink->assertion, g_media_compressed_detail);
    }
    sink->events.errors++;
    return -1;
}

BOOL test1349_media_annexb_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = { L"annexb-baseline-aac.h264", L"annexb-main-vga.h264",
        L"annexb-no-timing.h264", L"annexb-repeat-sps.h264",
        L"annexb-high.h264", L"annexb-high422.h264", L"annexb-interlaced.h264", L"annexb-oversize.h264",
        L"annexb-change-no-timing.h264", L"annexb-change-main-vga.h264", L"annexb-change-high.h264",
        L"annexb-change-high422.h264", L"annexb-change-interlaced.h264", L"annexb-change-oversize.h264",
        L"annexb-bad-sps.h264", L"annexb-bad-pps.h264", L"annexb-missing-pps.h264",
        L"annexb-orphan-pps.h264", L"annexb-late-bad-sps.h264", L"annexb-late-bad-pps.h264",
        L"annexb-cut-idr.h264", L"annexb-cut-p.h264", L"annexb-two-frames.h264",
        L"annexb-repeat-pps.h264", L"annexb-change-pps.h264", L"annexb-pps-id1.h264",
        L"annexb-dangling-pps.h264", L"annexb-late-dangling-pps.h264", L"annexb-pps-missing-sps.h264",
        L"annexb-sps-4096.h264", L"annexb-sps-4097.h264", L"annexb-pps-4096.h264",
        L"annexb-pps-4097.h264", L"annexb-late-pps-4097.h264", L"annexb-slice-before-pps.h264" };
    media_fixture_source input;
    media_compressed_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_probe_info probe;
    pm_probe_info unchanged;
    pm_session session;
    unsigned char *data;
    unsigned long hash;
    int bytes;
    int test;
    int iteration;
    int pass;
    int expected;
    int actual;
    int frame_count;
    DWORD start;

    data = NULL;
    session = NULL;
    for (test = 0; test < 35; test++) {
        memset(&sink, 0, sizeof(sink));
        _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail), "AnnexB case=%d", test);
        if (progress != NULL) progress(g_media_compressed_detail);
        g_media_compressed_error = "AnnexB probe/open";
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        media_compressed_init(&input, &sink, &source, &output, &options);
        output.video = media_annexb_video;
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 1;
        memset(&probe, 0xa5, sizeof(probe));
        probe.size = sizeof(probe);
        unchanged = probe;
        if ((test >= 4 && test < 20) || test == 24 || test >= 26) {
            expected = test == 24 ? PMEDIA_ERROR_UNSUPPORTED :
                test == 30 || test == 32 || test == 33 ? PMEDIA_ERROR_LIMIT :
                test >= 14 ? PMEDIA_ERROR_FORMAT :
                test == 7 || test == 13 ? PMEDIA_ERROR_LIMIT : PMEDIA_ERROR_UNSUPPORTED;
            options.max_video_width = options.max_video_height = 4096;
            actual = pm_probe(&source, &probe);
            _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                      "AnnexB reject case=%d expected=%d actual=%d", test, expected, actual);
            g_media_compressed_error = g_media_compressed_detail;
            if (actual != expected || input.position != 0 ||
                memcmp(&probe, &unchanged, sizeof(probe)) ||
                pm_open(&source, &options, &output, &session) != expected || session != NULL ||
                sink.events.error_callbacks != 1 || sink.events.last_error != expected ||
                sink.frames || sink.events.blocks) goto fail;
        } else if (test == 20 || test == 21) {
            input.position = 1;
            if (pm_probe(&source, &probe) != PMEDIA_OK || input.position != 1) goto fail;
            for (iteration = 0; iteration < 3; iteration++) {
                memset(&sink, 0, sizeof(sink));
                sink.width = 320;
                sink.height = 240;
                sink.video_duration_us = 200000;
                input.position = input.read_mode = 0;
                source.seek = iteration == 0 ? NULL : media_fixture_seek;
                source.tell = iteration == 0 ? NULL : media_fixture_tell;
                options.backend = iteration == 0 ? PMEDIA_BACKEND_AUTO : PMEDIA_BACKEND_SOFT;
                if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
                input.read_mode = 2;
                actual = PMEDIA_OK;
                start = GetTickCount();
                while (actual == PMEDIA_OK && GetTickCount() - start < 5000)
                    actual = pm_pump(session, 0, 2000);
                _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
                          "AnnexB payload case=%d rc=%d frames=%d", test, actual, sink.frames);
                g_media_compressed_error = g_media_compressed_detail;
                if (progress != NULL) progress(g_media_compressed_detail);
                if (actual != PMEDIA_ERROR_FORMAT || sink.frames != (test == 20 ? 0 : 2) ||
                    sink.events.blocks || sink.events.errors || sink.events.eof_events ||
                    sink.events.error_callbacks != 1 || sink.events.error_events != 1 ||
                    sink.events.last_error != PMEDIA_ERROR_FORMAT || !pm_last_error(session)[0] ||
                    pm_pump(session, 0, 2000) != PMEDIA_ERROR_STATE ||
                    pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
                    pm_resume(session) != PMEDIA_ERROR_STATE ||
                    sink.events.error_callbacks != 1 || sink.events.eof_events ||
                    pm_close(session) != PMEDIA_OK) goto fail;
                session = NULL;
            }
        } else {
            input.position = 1;
            if (pm_probe(&source, &probe) != PMEDIA_OK || input.position != 1 ||
                probe.stream.container != PMEDIA_CONTAINER_RAW || !probe.stream.has_video ||
                probe.stream.has_audio || probe.stream.video_codec != PMEDIA_CODEC_H264 ||
                probe.stream.width != (test == 1 ? 640 : 320) ||
                probe.stream.height != (test == 1 ? 480 : 240)) goto fail;
            frame_count = test == 22 ? 2 : test == 3 ? 6 : 3;
            for (iteration = 0; iteration < 3; iteration++) {
                input.position = input.read_mode = 0;
                source.seek = iteration == 0 ? NULL : media_fixture_seek;
                source.tell = iteration == 0 ? NULL : media_fixture_tell;
                options.backend = iteration == 0 ? PMEDIA_BACKEND_AUTO : PMEDIA_BACKEND_SOFT;
                memset(&sink, 0, sizeof(sink));
                sink.width = test == 1 ? 640 : 320;
                sink.height = test == 1 ? 480 : 240;
                sink.video_duration_us = test == 2 ? 0 : 200000;
                if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
                    pm_get_backend(session) != PMEDIA_BACKEND_SOFT || input.position != bytes) goto fail;
                if (pm_seek(session, -1) != PMEDIA_ERROR_ARGUMENT ||
                    pm_seek(session, 200000) != PMEDIA_ERROR_NOT_SEEKABLE ||
                    sink.frames || sink.events.blocks || sink.events.error_callbacks) goto fail;
                hash = 0;
                for (pass = 0; pass < 3; pass++) {
                    if (pm_pause(session) != PMEDIA_OK || pm_pump(session, 0, 2000) != PMEDIA_OK ||
                        sink.frames || pm_resume(session) != PMEDIA_OK) goto fail;
                    sink.callback_result = PMEDIA_CALLBACK_STOP;
                    start = GetTickCount();
                    while (sink.frames == 0 && GetTickCount() - start < 5000)
                        if (pm_pump(session, 0, 2000) != PMEDIA_OK) goto fail;
                    if (sink.frames != 1 || pm_pump(session, 0, 2000) != PMEDIA_OK || sink.frames != 1) goto fail;
                    sink.callback_result = PMEDIA_OK;
                    if (pm_resume(session) != PMEDIA_OK || !media_contract_drain(session) ||
                        sink.frames != frame_count || sink.events.blocks || sink.events.errors ||
                        sink.events.error_callbacks || sink.events.eof_events != 1 ||
                        pm_pump(session, 0, 2000) != PMEDIA_EOF || sink.events.eof_events != 1) goto fail;
                    if (pass == 0) hash = sink.replay_video_hash;
                    else if (hash != sink.replay_video_hash) goto fail;
                    if (progress != NULL) progress("AnnexB expected frames/duration, unknown PTS, replay hash OK");
                    if (pass < 2) {
                        g_media_compressed_error = "AnnexB seek zero";
                        input.read_mode = 2;
                        if (pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK) goto fail;
                        memset(&sink, 0, sizeof(sink));
                        sink.width = test == 1 ? 640 : 320;
                        sink.height = test == 1 ? 480 : 240;
                        sink.video_duration_us = test == 2 ? 0 : 200000;
                    }
                }
                if (pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK ||
                    sink.events.stopped_events != 1 || pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
                    pm_pump(session, 0, 2000) != PMEDIA_ERROR_STATE ||
                    pm_pause(session) != PMEDIA_ERROR_STATE || pm_resume(session) != PMEDIA_ERROR_STATE ||
                    pm_close(session) != PMEDIA_OK) goto fail;
                session = NULL;
            }
            input.position = input.read_mode = 0;
            memset(&sink, 0, sizeof(sink));
            sink.width = test == 1 ? 640 : 320;
            sink.height = test == 1 ? 480 : 240;
            sink.video_duration_us = test == 2 ? 0 : 200000;
            sink.callback_result = -1;
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK || media_contract_drain(session) ||
                sink.events.last_error != PMEDIA_ERROR_CALLBACK || sink.events.error_callbacks != 1 ||
                sink.events.error_events != 1 || sink.frames != 1 || sink.events.eof_events ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
            input.position = 0;
            input.bytes = 3;
            memset(&sink, 0, sizeof(sink));
            memset(&probe, 0xa5, sizeof(probe));
            probe.size = sizeof(probe);
            unchanged = probe;
            if (pm_probe(&source, &probe) != PMEDIA_ERROR_FORMAT || input.position != 0 ||
                memcmp(&probe, &unchanged, sizeof(probe)) ||
                pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_FORMAT || session != NULL ||
                sink.events.error_callbacks != 1 || sink.frames || sink.events.blocks) goto fail;
        }
        free(data);
        data = NULL;
    }
    return TRUE;
fail:
    if (sink.assertion[0] && progress != NULL) progress(sink.assertion);
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

typedef struct media_audio_clock_sink {
    media_fixture_output events;
    unsigned char *reference;
    int reference_bytes;
    int reference_samples[64];
    int reference_blocks;
    int offset;
    int compare;
    int scheduled;
    int rate;
    int channels;
    int response;
    int offers;
} media_audio_clock_sink;

static int media_audio_clock_output(void *context, const pm_audio_block *block)
{
    media_audio_clock_sink *sink;
    int index;
    sink = (media_audio_clock_sink *)context;
    index = sink->events.blocks;
    sink->offers++;
    if (block == NULL || block->data == NULL || block->size != sizeof(*block) ||
        block->sample_rate != sink->rate || block->channels != sink->channels ||
        block->samples < 1 || block->samples > 8192 || block->bytes > 32768 ||
        block->bytes != block->samples * sink->channels * 2 || index >= 64 ||
        block->duration_us != (pm_position)block->samples * 1000000 / sink->rate ||
        (sink->scheduled && block->pts_us != (pm_position)sink->events.samples * 1000000 / sink->rate) ||
        block->bytes > 122880 - sink->offset) goto invalid;
    if (sink->compare) {
        if (index >= sink->reference_blocks || sink->reference_samples[index] != block->samples ||
            block->bytes > sink->reference_bytes - sink->offset ||
            memcmp(block->data, sink->reference + sink->offset, (size_t)block->bytes)) goto invalid;
    } else {
        memcpy(sink->reference + sink->offset, block->data, (size_t)block->bytes);
        sink->reference_samples[index] = block->samples;
    }
    if (sink->scheduled && sink->response == PMEDIA_WOULD_BLOCK) return PMEDIA_WOULD_BLOCK;
    if (sink->response < 0) return sink->response;
    sink->offset += block->bytes;
    sink->events.samples += block->samples;
    sink->events.blocks++;
    return sink->response;
invalid:
    sink->events.errors++;
    return PMEDIA_ERROR_CALLBACK;
}

static BOOL media_audio_clock_expect(pm_session session, media_audio_clock_sink *sink,
                                    pm_position clock, int blocks, int expected)
{
    int attempt;
    int result;
    int previous_offers;
    int previous_blocks;
    result = PMEDIA_OK;
    for (attempt = 0; attempt < 128; attempt++) {
        previous_offers = sink->offers;
        previous_blocks = sink->events.blocks;
        result = pm_pump(session, clock, 2000);
        if (sink->offers - previous_offers > 1 ||
            sink->events.blocks - previous_blocks > 1) return FALSE;
        if (result != PMEDIA_OK ||
            (expected == PMEDIA_OK && sink->events.blocks >= blocks)) break;
    }
    if (result == expected && sink->events.blocks == blocks && !sink->events.errors) return TRUE;
    _snprintf(g_media_compressed_detail, sizeof(g_media_compressed_detail),
              "audio clock=%I64d rc=%d expected=%d blocks=%d expected=%d errors=%d",
              clock, result, expected, sink->events.blocks, blocks, sink->events.errors);
    g_media_compressed_detail[sizeof(g_media_compressed_detail) - 1] = '\0';
    g_media_compressed_error = g_media_compressed_detail;
    return FALSE;
}

BOOL test1353_media_audio_clock_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = { NULL, L"aac-lc.aac", L"mp3-mono.mp3", NULL };
    static const WCHAR *rejected[] = {
        L"baseline-aac.mp4", L"mjpeg-mp3.avi", L"amr-nb.amr", L"ima-mono.wav", L"aac-main.aac"
    };
    media_fixture_source input;
    media_audio_clock_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_session session;
    unsigned char *data;
    unsigned char *reference;
    pm_position maximum;
    pm_position deadline;
    int bytes;
    int test;
    int iteration;
    int pass;
    int i;
    int blocks;
    int samples;
    int offers;
    int result;
    char phase[160];

    session = NULL;
    data = NULL;
    reference = (unsigned char *)malloc(122880);
    if (reference == NULL) return FALSE;
    maximum = (((pm_position)0x7fffffff) << 32) | (pm_position)0xffffffffUL;
    for (test = 0; test < 4; test++) {
        if (names[test] != NULL) data = media_compressed_load(names[test], &bytes);
        else {
            bytes = 44 + 5000 * (test == 0 ? 4 : 1);
            data = (unsigned char *)malloc((size_t)bytes);
            if (data != NULL) {
                memcpy(data, g_media_fixture_wav, 44);
                media_ima_put32(data + 4, bytes - 8);
                media_ima_put16(data + 22, test == 0 ? 2 : 1);
                media_ima_put32(data + 24, test == 0 ? 44100 : 8000);
                media_ima_put32(data + 28, test == 0 ? 176400 : 8000);
                media_ima_put16(data + 32, test == 0 ? 4 : 1);
                media_ima_put16(data + 34, test == 0 ? 16 : 8);
                media_ima_put32(data + 40, bytes - 44);
                for (i = 44; i < bytes; i++) data[i] = (unsigned char)(i * 37 + i / 7);
            }
        }
        g_media_compressed_error = "audio clock fixture load";
        if (data == NULL) goto fail;
        media_contract_init(&input, &sink.events, &source, &output, &options);
        memset(&sink, 0, sizeof(sink));
        sink.reference = reference;
        sink.rate = test == 1 ? 48000 : (test == 3 ? 8000 : 44100);
        sink.channels = test == 0 ? 2 : 1;
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 7;
        output.context = &sink;
        output.audio = media_audio_clock_output;
        _snprintf(phase, sizeof(phase), "audio case=%d decode-only PCM reference", test);
        g_media_compressed_error = "audio decode-only reference";
        if (progress != NULL) progress(phase);
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
        result = PMEDIA_OK;
        for (i = 0; i < 512 && result == PMEDIA_OK; i++) result = pm_pump(session, -1, 2000);
        if (result != PMEDIA_EOF || sink.events.errors || sink.events.error_callbacks ||
            sink.events.samples != (test == 1 ? 30720 : (test == 2 ? 26460 : 5000)) ||
            pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        blocks = sink.events.blocks;
        samples = sink.events.samples;
        sink.reference_blocks = blocks;
        sink.reference_bytes = sink.offset;
        sink.compare = 1;
        sink.scheduled = 1;
        options.flags = PMEDIA_OPEN_SCHEDULE_AUDIO;
        for (iteration = 0; iteration < 2; iteration++) {
            input.position = 0;
            input.read_mode = 0;
            source.seek = iteration ? media_fixture_seek : NULL;
            source.tell = iteration ? media_fixture_tell : NULL;
            memset(&sink.events, 0, sizeof(sink.events));
            sink.offset = 0;
            sink.response = 0;
            _snprintf(phase, sizeof(phase), "audio case=%d seekable=%d exact deadlines, full PCM and rewind", test, iteration);
            if (progress != NULL) progress(phase);
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
            /* After open every replay must consume the DLL-owned cache. */
            input.read_mode = 2;
            for (pass = 0; pass < 2; pass++) {
                if (pass && pm_seek(session, 0) != PMEDIA_OK) goto fail;
                memset(&sink.events, 0, sizeof(sink.events));
                sink.offset = 0;
                if (pm_pump(session, 0, -1) != PMEDIA_ERROR_ARGUMENT) goto fail;
                if (!media_audio_clock_expect(session, &sink, -1, 0, PMEDIA_ERROR_ARGUMENT) ||
                    !media_audio_clock_expect(session, &sink, 1000000, 1, PMEDIA_OK)) goto fail;
                for (i = 1; i < blocks; i++) {
                    deadline = 1000000 + (pm_position)sink.events.samples * 1000000 / sink.rate;
                    if (!media_audio_clock_expect(session, &sink, deadline - 1, i, PMEDIA_WOULD_BLOCK) ||
                        !media_audio_clock_expect(session, &sink, deadline - 1, i, PMEDIA_WOULD_BLOCK) ||
                        !media_audio_clock_expect(session, &sink, 999999, i, PMEDIA_ERROR_ARGUMENT) ||
                        pm_seek(session, 1) != PMEDIA_ERROR_NOT_SEEKABLE ||
                        !media_audio_clock_expect(session, &sink, deadline, i + 1, PMEDIA_OK)) goto fail;
                }
                result = PMEDIA_OK;
                for (i = 0; i < 128 && result == PMEDIA_OK; i++) result = pm_pump(session, maximum, 2000);
                if (result != PMEDIA_EOF || pm_pump(session, maximum, 0) != PMEDIA_EOF ||
                    sink.events.eof_events != 1 || sink.events.samples != samples ||
                    sink.offset != sink.reference_bytes || sink.events.error_callbacks) goto fail;
            }
            if (progress != NULL) progress("audio pause, identical backpressure retry, STOP consumes, cached rewind");
            if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
            memset(&sink.events, 0, sizeof(sink.events));
            sink.offset = 0;
            sink.response = PMEDIA_WOULD_BLOCK;
            if (!media_audio_clock_expect(session, &sink, 0, 0, PMEDIA_WOULD_BLOCK)) goto fail;
            offers = sink.offers;
            if (!media_audio_clock_expect(session, &sink, 0, 0, PMEDIA_WOULD_BLOCK) || sink.offers != offers + 1 ||
                pm_pause(session) != PMEDIA_OK || pm_pump(session, -1, 0) != PMEDIA_OK ||
                sink.offers != offers + 1 || pm_seek(session, 0) != PMEDIA_OK ||
                pm_pump(session, 0, 0) != PMEDIA_OK || sink.offers != offers + 1 ||
                pm_resume(session) != PMEDIA_OK) goto fail;
            sink.response = PMEDIA_CALLBACK_STOP;
            if (!media_audio_clock_expect(session, &sink, 9000000, 1, PMEDIA_OK) ||
                pm_pump(session, maximum, 0) != PMEDIA_OK || sink.events.blocks != 1 ||
                pm_resume(session) != PMEDIA_OK) goto fail;
            sink.response = PMEDIA_OK;
            deadline = (pm_position)sink.events.samples * 1000000 / sink.rate;
            if (!media_audio_clock_expect(session, &sink, 19000000, 1, PMEDIA_WOULD_BLOCK) ||
                pm_resume(session) != PMEDIA_OK ||
                !media_audio_clock_expect(session, &sink, 19000000 + deadline - 1, 1, PMEDIA_WOULD_BLOCK) ||
                !media_audio_clock_expect(session, &sink, 19000000 + deadline, 2, PMEDIA_OK)) goto fail;
            if (progress != NULL) progress("audio retained nonzero block pause and identical backpressure");
            if (!media_audio_clock_expect(session, &sink, 19000000 + deadline, 2, PMEDIA_WOULD_BLOCK) ||
                pm_pause(session) != PMEDIA_OK) goto fail;
            offers = sink.offers;
            if (pm_pump(session, maximum, -1) != PMEDIA_ERROR_ARGUMENT ||
                pm_pump(session, maximum, 0) != PMEDIA_OK || sink.offers != offers ||
                pm_resume(session) != PMEDIA_OK ||
                !media_audio_clock_expect(session, &sink, 29000000 + deadline, 2, PMEDIA_WOULD_BLOCK)) goto fail;
            deadline = 29000000 + (pm_position)sink.events.samples * 1000000 / sink.rate;
            sink.response = PMEDIA_WOULD_BLOCK;
            if (!media_audio_clock_expect(session, &sink, deadline - 1, 2, PMEDIA_WOULD_BLOCK) ||
                sink.offers != offers ||
                !media_audio_clock_expect(session, &sink, deadline, 2, PMEDIA_WOULD_BLOCK) ||
                !media_audio_clock_expect(session, &sink, deadline, 2, PMEDIA_WOULD_BLOCK) ||
                sink.offers != offers + 2) goto fail;
            sink.response = PMEDIA_OK;
            if (!media_audio_clock_expect(session, &sink, deadline, 3, PMEDIA_OK)) goto fail;
            /* Pending second/third block is discarded on zero rewind; near
             * INT64_MAX clocks must use differences, never deadline addition. */
            if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
            memset(&sink.events, 0, sizeof(sink.events));
            sink.offset = 0;
            if (!media_audio_clock_expect(session, &sink, maximum - 1000000, 1, PMEDIA_OK)) goto fail;
            for (i = 1; i < blocks; i++)
                if (!media_audio_clock_expect(session, &sink, maximum, i + 1, PMEDIA_OK)) goto fail;
            if (sink.offset != sink.reference_bytes || pm_seek(session, 0) != PMEDIA_OK) goto fail;
            memset(&sink.events, 0, sizeof(sink.events));
            sink.offset = 0;
            sink.response = -1;
            if (!media_audio_clock_expect(session, &sink, 0, 0, PMEDIA_ERROR_CALLBACK) ||
                sink.events.error_callbacks != 1 || sink.events.error_events != 1 ||
                sink.events.last_error != PMEDIA_ERROR_CALLBACK ||
                pm_pump(session, 0, 0) != PMEDIA_ERROR_STATE || pm_pause(session) != PMEDIA_ERROR_STATE ||
                pm_resume(session) != PMEDIA_ERROR_STATE || pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
                pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK ||
                pm_close(session) != PMEDIA_OK || sink.events.error_callbacks != 1 ||
                sink.events.stopped_events != 1) goto fail;
            session = NULL;
        }
        free(data);
        data = NULL;
    }
    if (progress != NULL) progress("audio option/source guards and unsupported stream boundary");
    media_contract_init(&input, &sink.events, &source, &output, &options);
    options.flags = PMEDIA_OPEN_SCHEDULE_AUDIO;
    output.audio = NULL;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_ARGUMENT || input.position) goto fail;
    output.audio = media_audio_clock_output;
    options.flags = PMEDIA_OPEN_SCHEDULE_AUDIO | PMEDIA_OPEN_SCHEDULE_VIDEO;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_ARGUMENT || input.position) goto fail;
    options.flags = PMEDIA_OPEN_SCHEDULE_AUDIO;
    for (i = 0; i < 3; i++) {
        options.backend = i;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_ARGUMENT || input.position) goto fail;
    }
    options.backend = PMEDIA_BACKEND_SOFT;
    input.read_mode = 1;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_WOULD_BLOCK || session != NULL) goto fail;
    input.read_mode = 2;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_IO || session != NULL) goto fail;
    for (test = 0; test < 5; test++) {
        data = media_compressed_load(rejected[test], &bytes);
        if (data == NULL) goto fail;
        input.data = data;
        input.bytes = bytes;
        input.position = 0;
        input.read_mode = 0;
        memset(&sink.events, 0, sizeof(sink.events));
        if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_UNSUPPORTED || session != NULL ||
            sink.events.blocks || sink.events.error_callbacks != 1 || sink.events.error_events != 1) goto fail;
        free(data);
        data = NULL;
    }
    free(reference);
    if (progress != NULL) progress("pure-audio sample-clock PCM contract passed");
    return TRUE;
fail:
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    free(reference);
    return FALSE;
}

typedef struct media_audio_change_sink {
    media_fixture_output events;
    unsigned char *reference;
    int capture;
    int scheduled;
    int first;
    int second;
    int third;
    int reference_offset;
    int nonzero_samples;
    int distinct_samples;
} media_audio_change_sink;

static const int g_media_audio_change_rates[] = { 48000, 44100, 48000 };
static const int g_media_audio_change_channels[] = { 1, 1, 2 };
static const int g_media_audio_change_blocks[] = { 30, 27, 30 };

static int media_audio_change_output(void *context, const pm_audio_block *block)
{
    media_audio_change_sink *sink;
    int index;
    int format;
    int first_blocks;
    int i;
    sink = (media_audio_change_sink *)context;
    index = sink->events.blocks;
    first_blocks = g_media_audio_change_blocks[sink->first];
    format = sink->first;
    if (index >= first_blocks) {
        if (sink->scheduled && sink->second != sink->first) goto invalid;
        format = sink->second;
        if (format < 0) goto invalid;
        if (index >= first_blocks + g_media_audio_change_blocks[format]) format = sink->third;
        if (format < 0) goto invalid;
    }
    if (block == NULL || block->size != sizeof(*block) || block->data == NULL ||
        block->samples != 1024 || block->sample_rate != g_media_audio_change_rates[format] ||
        block->channels != g_media_audio_change_channels[format] ||
        block->bytes != 2048 * block->channels ||
        block->duration_us != (pm_position)1024 * 1000000 / block->sample_rate ||
        (sink->scheduled && block->pts_us !=
         (pm_position)sink->events.samples * 1000000 / g_media_audio_change_rates[sink->first])) goto invalid;
    if (index < first_blocks) {
        if (sink->reference_offset + block->bytes > first_blocks * 2048 * block->channels) goto invalid;
        if (sink->capture) memcpy(sink->reference + sink->reference_offset, block->data, (size_t)block->bytes);
        else if (memcmp(sink->reference + sink->reference_offset, block->data, (size_t)block->bytes)) goto invalid;
        sink->reference_offset += block->bytes;
    }
    for (i = 0; i < block->samples; i++) {
        int offset;
        offset = i * block->channels * 2;
        if (block->data[offset] || block->data[offset + 1]) sink->nonzero_samples++;
        if (block->channels == 2 &&
            (block->data[offset] != block->data[offset + 2] ||
             block->data[offset + 1] != block->data[offset + 3])) sink->distinct_samples++;
    }
    sink->events.blocks++;
    sink->events.samples += block->samples;
    return PMEDIA_OK;
invalid:
    sink->events.errors++;
    return PMEDIA_ERROR_CALLBACK;
}

static int media_audio_change_pump(pm_session session, media_audio_change_sink *sink,
                                   pm_position clock, int target)
{
    int i;
    int result;
    for (i = 0; i < 1024; i++) {
        result = pm_pump(session, clock, 2000);
        if (result != PMEDIA_OK || (target > 0 && sink->events.blocks >= target)) return result;
    }
    return PMEDIA_ERROR_STATE;
}

static void media_audio_change_reset(media_audio_change_sink *sink)
{
    memset(&sink->events, 0, sizeof(sink->events));
    sink->reference_offset = 0;
    sink->nonzero_samples = 0;
    sink->distinct_samples = 0;
}

BOOL test1354_media_audio_change_contract(void (*progress)(const char *))
{
    static const WCHAR *controls[] = { L"aac-lc.aac", L"aac-lc-44100.aac", L"aac-lc-stereo.aac" };
    static const WCHAR *names[] = {
        L"aac-clock-repeat.aac", L"aac-clock-rate-down.aac", L"aac-clock-rate-up.aac",
        L"aac-clock-stereo.aac", L"aac-clock-mono.aac", L"aac-clock-rate-return.aac"
    };
    static const int segments[6][3] = { {0,0,-1}, {0,1,-1}, {1,0,-1}, {0,2,-1}, {2,0,-1}, {0,1,0} };
    media_fixture_source input;
    media_audio_change_sink sink;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_stream_info info;
    pm_session session;
    unsigned char *data;
    unsigned char *reference[3];
    pm_position maximum;
    int bytes;
    int test;
    int iteration;
    int pass;
    int i;
    int blocks;
    int result;
    char phase[192];

    session = NULL;
    data = NULL;
    memset(reference, 0, sizeof(reference));
    maximum = (((pm_position)0x7fffffff) << 32) | (pm_position)0xffffffffUL;
    for (test = 0; test < 3; test++) {
        _snprintf(phase, sizeof(phase), "audio change standalone PCM reference=%d", test);
        g_media_compressed_error = "audio change standalone reference";
        if (progress != NULL) progress(phase);
        reference[test] = (unsigned char *)malloc((size_t)(g_media_audio_change_blocks[test] *
                          g_media_audio_change_channels[test] * 2048));
        data = media_compressed_load(controls[test], &bytes);
        if (reference[test] == NULL || data == NULL) goto fail;
        media_contract_init(&input, &sink.events, &source, &output, &options);
        memset(&sink, 0, sizeof(sink));
        sink.reference = reference[test];
        sink.capture = 1;
        sink.first = test;
        sink.second = -1;
        sink.third = -1;
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 7;
        output.audio = media_audio_change_output;
        output.error = media_compressed_error;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
        result = media_audio_change_pump(session, &sink, -1, 0);
        if (result != PMEDIA_EOF || sink.events.errors || sink.events.error_callbacks ||
            sink.events.blocks != g_media_audio_change_blocks[test] || sink.events.eof_events != 1 ||
            sink.nonzero_samples < 20000 || (test == 2 && sink.distinct_samples < 20000) ||
            pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        free(data);
        data = NULL;
    }
    for (test = 0; test < 6; test++) {
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        media_contract_init(&input, &sink.events, &source, &output, &options);
        memset(&sink, 0, sizeof(sink));
        sink.first = segments[test][0];
        sink.second = segments[test][1];
        sink.third = segments[test][2];
        sink.reference = reference[sink.first];
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 7;
        output.audio = media_audio_change_output;
        output.error = media_compressed_error;
        blocks = g_media_audio_change_blocks[sink.first] + g_media_audio_change_blocks[sink.second];
        if (sink.third >= 0) blocks += g_media_audio_change_blocks[sink.third];
        for (iteration = 0; iteration < 2; iteration++) {
            input.position = 0;
            input.read_mode = 0;
            source.seek = iteration ? media_fixture_seek : NULL;
            source.tell = iteration ? media_fixture_tell : NULL;
            options.flags = 0;
            sink.scheduled = 0;
            media_audio_change_reset(&sink);
            _snprintf(phase, sizeof(phase), "audio change case=%d seekable=%d decode-only keeps all %d blocks", test, iteration, blocks);
            g_media_compressed_error = "audio format change decode-only control";
            if (progress != NULL) progress(phase);
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
            input.read_mode = 2;
            for (pass = 0; pass < 2; pass++) {
                if (pass) {
                    if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
                    media_audio_change_reset(&sink);
                }
                result = media_audio_change_pump(session, &sink, -1, 0);
                if (result != PMEDIA_EOF || sink.events.blocks != blocks ||
                    sink.events.samples != blocks * 1024 || sink.events.errors ||
                    sink.events.error_callbacks || sink.events.eof_events != 1) goto fail;
            }
            if (pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
            input.position = 0;
            input.read_mode = 0;
            options.flags = PMEDIA_OPEN_SCHEDULE_AUDIO;
            sink.scheduled = 1;
            media_audio_change_reset(&sink);
            _snprintf(phase, sizeof(phase), "audio change case=%d seekable=%d scheduled boundary, prefix PCM and terminal state", test, iteration);
            g_media_compressed_error = "scheduled audio format boundary";
            if (progress != NULL) progress(phase);
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
            input.read_mode = 2;
            if (media_audio_change_pump(session, &sink, 0, 1) != PMEDIA_OK || sink.events.blocks != 1 ||
                media_audio_change_pump(session, &sink, 0, 0) != PMEDIA_WOULD_BLOCK || sink.events.blocks != 1 ||
                pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK ||
                pm_pump(session, -1, 0) != PMEDIA_OK || sink.events.blocks != 1 ||
                pm_resume(session) != PMEDIA_OK) goto fail;
            media_audio_change_reset(&sink);
            if (media_audio_change_pump(session, &sink, 0, 1) != PMEDIA_OK) goto fail;
            result = media_audio_change_pump(session, &sink, maximum, 0);
            _snprintf(phase, sizeof(phase), "audio change observed case=%d rc=%d blocks=%d samples=%d errors=%d error_callbacks=%d",
                      test, result, sink.events.blocks, sink.events.samples, sink.events.errors, sink.events.error_callbacks);
            if (progress != NULL) progress(phase);
            if (test == 0) {
                if (result != PMEDIA_EOF || sink.events.blocks != blocks || sink.events.errors ||
                    sink.events.error_callbacks || sink.events.eof_events != 1 || pm_seek(session, 0) != PMEDIA_OK) goto fail;
                media_audio_change_reset(&sink);
                if (media_audio_change_pump(session, &sink, 0, 1) != PMEDIA_OK ||
                    media_audio_change_pump(session, &sink, maximum, 0) != PMEDIA_EOF ||
                    sink.events.blocks != blocks || sink.events.eof_events != 1 || sink.events.errors) goto fail;
            } else {
                if (result != PMEDIA_ERROR_FORMAT || sink.events.blocks != g_media_audio_change_blocks[sink.first] ||
                    sink.events.samples != g_media_audio_change_blocks[sink.first] * 1024 ||
                    sink.events.errors || sink.events.eof_events || sink.events.error_callbacks != 1 ||
                    sink.events.error_events != 1 || sink.events.last_error != PMEDIA_ERROR_FORMAT ||
                    strcmp(pm_last_error(session), "scheduled audio sample format changed") != 0 ||
                    pm_pump(session, maximum, 0) != PMEDIA_ERROR_STATE ||
                    pm_pause(session) != PMEDIA_ERROR_STATE || pm_resume(session) != PMEDIA_ERROR_STATE ||
                    pm_seek(session, 0) != PMEDIA_ERROR_STATE || sink.events.error_callbacks != 1) goto fail;
            }
            if (pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK ||
                sink.events.stopped_events != 1) goto fail;
            memset(&info, 0, sizeof(info));
            info.size = sizeof(info);
            if (pm_get_stream_info(session, &info) != PMEDIA_OK || !info.has_audio || info.has_video ||
                pm_get_backend(session) != PMEDIA_BACKEND_SOFT || pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
        }
        free(data);
        data = NULL;
    }
    for (i = 0; i < 3; i++) free(reference[i]);
    if (progress != NULL) progress("scheduled audio format-change boundary passed; decode-only unchanged");
    return TRUE;
fail:
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    for (i = 0; i < 3; i++) if (reference[i] != NULL) free(reference[i]);
    return FALSE;
}

/* Consumer-only reference capture. The DLL owns scheduling; these fixtures
 * compare every visible video byte and every PCM byte with decode-only. */
typedef struct media_av_reference {
    pm_video_frame video[8];
    unsigned long video_hash[8];
    pm_audio_block audio[64];
    unsigned char pcm[131072];
    int videos;
    int audios;
    int bytes;
} media_av_reference;

typedef struct media_av_sink {
    media_fixture_output events;
    media_av_reference *reference;
    int capture;
    int videos;
    int audios;
    int bytes;
    int offers;
    int audio_result;
    int video_result;
    int last_audio;
} media_av_sink;

static unsigned long media_av_video_hash(const pm_video_frame *frame)
{
    unsigned long hash;
    int plane;
    int x;
    int y;
    int width;
    int height;
    hash = 2166136261UL;
    for (plane = 0; plane < 3; plane++) {
        width = plane ? (frame->width + 1) / 2 : frame->width;
        height = plane ? (frame->height + 1) / 2 : frame->height;
        for (y = 0; y < height; y++)
            for (x = 0; x < width; x++)
                hash = (hash ^ frame->plane[plane][y * frame->stride[plane] + x]) * 16777619UL;
    }
    return hash;
}

static int media_av_video(void *opaque, const pm_video_frame *frame)
{
    media_av_sink *sink;
    media_av_reference *ref;
    pm_video_frame *expected;
    unsigned long hash;
    sink = (media_av_sink *)opaque;
    ref = sink->reference;
    sink->offers++;
    sink->last_audio = 0;
    if (frame == NULL || frame->size != sizeof(*frame) || frame->format != PMEDIA_PIXEL_I420 ||
        frame->width <= 0 || frame->height <= 0 || frame->width > 640 || frame->height > 480 ||
        frame->plane[0] == NULL || frame->plane[1] == NULL || frame->plane[2] == NULL ||
        frame->stride[0] < frame->width || frame->stride[1] < (frame->width + 1) / 2 ||
        frame->stride[2] < (frame->width + 1) / 2 || sink->videos >= 8) goto invalid;
    hash = media_av_video_hash(frame);
    if (sink->capture) {
        ref->video[sink->videos] = *frame;
        ref->video_hash[sink->videos] = hash;
    } else {
        if (sink->videos >= ref->videos) goto invalid;
        expected = &ref->video[sink->videos];
        if (frame->width != expected->width || frame->height != expected->height ||
            frame->pts_us != expected->pts_us || frame->duration_us != expected->duration_us ||
            frame->flags != expected->flags || hash != ref->video_hash[sink->videos] ||
            (sink->audios < ref->audios && ref->audio[sink->audios].pts_us <= frame->pts_us)) goto invalid;
    }
    if (sink->video_result == PMEDIA_WOULD_BLOCK || sink->video_result < 0) return sink->video_result;
    sink->videos++;
    if (sink->capture) ref->videos = sink->videos;
    return sink->video_result;
invalid:
    sink->events.errors++;
    return -1;
}

static int media_av_audio(void *opaque, const pm_audio_block *block)
{
    media_av_sink *sink;
    media_av_reference *ref;
    pm_audio_block *expected;
    sink = (media_av_sink *)opaque;
    ref = sink->reference;
    sink->offers++;
    sink->last_audio = 1;
    if (block == NULL || block->size != sizeof(*block) || block->data == NULL ||
        block->sample_rate != 48000 || block->channels != 2 || block->samples != 1024 ||
        block->bytes != 4096 || sink->audios >= 64 || sink->bytes + block->bytes > sizeof(ref->pcm)) goto invalid;
    if (sink->capture) {
        ref->audio[sink->audios] = *block;
        memcpy(ref->pcm + sink->bytes, block->data, (size_t)block->bytes);
    } else {
        if (sink->audios >= ref->audios) goto invalid;
        expected = &ref->audio[sink->audios];
        if (block->pts_us != expected->pts_us || block->duration_us != expected->duration_us ||
            memcmp(ref->pcm + sink->bytes, block->data, (size_t)block->bytes) ||
            (sink->videos < ref->videos && ref->video[sink->videos].pts_us < block->pts_us)) goto invalid;
    }
    if (sink->audio_result == PMEDIA_WOULD_BLOCK || sink->audio_result < 0) return sink->audio_result;
    sink->audios++;
    sink->bytes += block->bytes;
    if (sink->capture) {
        ref->audios = sink->audios;
        ref->bytes = sink->bytes;
    }
    return sink->audio_result;
invalid:
    sink->events.errors++;
    return -1;
}

static int media_av_pump(pm_session session, media_av_sink *sink, pm_position clock)
{
    int result;
    int offers;
    int i;
    offers = sink->offers;
    for (i = 0; i < 2000; i++) {
        result = pm_pump(session, clock, 2000);
        if (sink->offers > offers + 1) return PMEDIA_ERROR_CALLBACK;
        if (result != PMEDIA_OK || sink->offers != offers) return result;
    }
    return PMEDIA_ERROR_LIMIT;
}

BOOL test1355_media_av_clock_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = { L"baseline-aac.mp4", L"mixed-main-aac.mp4" };
    static const WCHAR *rejects[] = { L"main-vga.mp4", L"mjpeg-mp3.avi", L"aac-lc.aac" };
    media_fixture_source input;
    media_av_sink sink;
    media_av_reference *ref;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_stream_info info;
    pm_session session;
    unsigned char *data;
    pm_position pts;
    pm_position previous;
    pm_position maximum;
    int test;
    int seekable;
    int bytes;
    int result;
    int offers;
    int i;
    char phase[160];

    ref = (media_av_reference *)calloc(1, sizeof(*ref));
    data = NULL;
    session = NULL;
    if (ref == NULL) return FALSE;
    maximum = (((pm_position)0x7fffffff) << 32) | (pm_position)0xffffffffUL;
    media_contract_init(&input, &sink.events, &source, &output, &options);
    memset(&sink, 0, sizeof(sink));
    sink.reference = ref;
    output.video = media_av_video;
    output.audio = media_av_audio;
    output.error = media_compressed_error;
    g_media_compressed_error = "mixed flags/callback/backend validation before source read";
    for (i = 3; i < 9; i++) {
        if (i == 4) continue;
        options.flags = i;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_ARGUMENT ||
            session != NULL || input.position || sink.offers) goto fail;
    }
    options.flags = PMEDIA_OPEN_SCHEDULE_AV;
    options.backend = PMEDIA_BACKEND_AUTO;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_ARGUMENT || input.position) goto fail;
    options.backend = PMEDIA_BACKEND_SOFT;
    output.video = NULL;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_ARGUMENT || input.position) goto fail;
    output.video = media_av_video;
    output.audio = NULL;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_ARGUMENT || input.position) goto fail;
    output.audio = media_av_audio;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_UNSUPPORTED || session != NULL) goto fail;
    for (test = 0; test < 3; test++) {
        data = media_compressed_load(rejects[test], &bytes);
        if (data == NULL) goto fail;
        input.data = data;
        input.bytes = bytes;
        input.position = 0;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_ERROR_UNSUPPORTED || session != NULL || sink.offers) goto fail;
        free(data);
        data = NULL;
    }
    g_media_compressed_error = "mixed valid filler packet exceeds budget before output";
    data = media_compressed_load_bounded(L"mixed-packet-limit.mp4", &bytes, 524288);
    if (data == NULL) goto fail;
    input.data = data;
    input.bytes = bytes;
    input.position = 0;
    memset(&sink, 0, sizeof(sink));
    sink.reference = ref;
    if (pm_open(&source, &options, &output, &session) != PMEDIA_OK ||
        media_av_pump(session, &sink, 0) != PMEDIA_ERROR_LIMIT || sink.offers ||
        sink.events.error_callbacks != 1 || sink.events.error_events != 1 || sink.events.eof_events ||
        strcmp(pm_last_error(session), "mixed compressed packet exceeds 256 KiB") ||
        pm_pump(session, 0, 0) != PMEDIA_ERROR_STATE || pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
        pm_stop(session) != PMEDIA_OK || pm_close(session) != PMEDIA_OK) goto fail;
    session = NULL;
    free(data);
    data = NULL;
    for (test = 0; test < 2; test++) {
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 7;
        input.read_mode = input.position = 0;
        memset(ref, 0, sizeof(*ref));
        memset(&sink, 0, sizeof(sink));
        sink.reference = ref;
        sink.capture = 1;
        options.flags = 0;
        _snprintf(phase, sizeof(phase), "mixed case=%d full decode-only I420/PCM reference", test);
        g_media_compressed_error = "mixed decode-only reference";
        if (progress != NULL) progress(phase);
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
        result = PMEDIA_OK;
        for (i = 0; i < 3000 && result == PMEDIA_OK; i++) result = pm_pump(session, 0, 2000);
        if (result != PMEDIA_EOF || ref->videos != 3 || ref->audios != 29 ||
            ref->bytes != 118784 || sink.events.errors || sink.events.error_callbacks ||
            sink.events.eof_events != 1 || pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        for (i = 0; i < 3; i++)
            if (ref->video[i].pts_us != (pm_position)i * 200000) goto fail;
        for (i = 0; i < 29; i++)
            if (ref->audio[i].pts_us != ((pm_position)i * 1024 * 1000000 + 24000) / 48000) goto fail;
        for (seekable = 0; seekable < 2; seekable++) {
            _snprintf(phase, sizeof(phase), "mixed case=%d seekable=%d deadlines, merge, full bytes, tail EOF", test, seekable);
            if (progress != NULL) progress(phase);
            g_media_compressed_error = "mixed deadline/content contract";
            input.position = input.read_mode = 0;
            source.seek = seekable ? media_fixture_seek : NULL;
            source.tell = seekable ? media_fixture_tell : NULL;
            memset(&sink, 0, sizeof(sink));
            sink.reference = ref;
            options.flags = PMEDIA_OPEN_SCHEDULE_AV;
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK || input.position != bytes ||
                pm_pump(session, -1, 2000) != PMEDIA_ERROR_ARGUMENT ||
                pm_pump(session, 1000000, -1) != PMEDIA_ERROR_ARGUMENT) goto fail;
            input.read_mode = 2;
            previous = 0;
            for (i = 0; i < 32; i++) {
                pts = sink.audios < ref->audios && (sink.videos == ref->videos ||
                       ref->audio[sink.audios].pts_us <= ref->video[sink.videos].pts_us) ?
                       ref->audio[sink.audios].pts_us : ref->video[sink.videos].pts_us;
                offers = sink.offers;
                if (pts > previous && (media_av_pump(session, &sink, 1000000 + pts - 1) != PMEDIA_WOULD_BLOCK ||
                    sink.offers != offers)) goto fail;
                if (media_av_pump(session, &sink, 1000000 + pts) != PMEDIA_OK ||
                    sink.offers != offers + 1 || sink.events.errors ||
                    pm_seek(session, 1) != PMEDIA_ERROR_NOT_SEEKABLE ||
                    pm_resume(session) != PMEDIA_OK ||
                    pm_pump(session, 999999, 0) != PMEDIA_ERROR_ARGUMENT) goto fail;
                previous = pts;
            }
            if (sink.videos != 3 || sink.audios != 29 || sink.bytes != ref->bytes ||
                media_av_pump(session, &sink, maximum) != PMEDIA_EOF || sink.events.eof_events != 1 ||
                pm_pump(session, maximum, 0) != PMEDIA_EOF || sink.events.eof_events != 1) goto fail;
            g_media_compressed_error = "mixed cached rewind, both backpressure heads, STOP/pause and near-max clock";
            if (progress != NULL) progress(g_media_compressed_error);
            if (pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK ||
                pm_pump(session, -1, 0) != PMEDIA_OK || pm_resume(session) != PMEDIA_OK) goto fail;
            memset(&sink, 0, sizeof(sink));
            sink.reference = ref;
            sink.audio_result = PMEDIA_WOULD_BLOCK;
            if (media_av_pump(session, &sink, 0) != PMEDIA_WOULD_BLOCK || sink.offers != 1 || sink.audios || sink.videos ||
                media_av_pump(session, &sink, 0) != PMEDIA_WOULD_BLOCK || sink.offers != 2 || !sink.last_audio) goto fail;
            sink.audio_result = PMEDIA_CALLBACK_STOP;
            if (media_av_pump(session, &sink, 0) != PMEDIA_OK || sink.audios != 1 ||
                pm_pump(session, 9000000, 0) != PMEDIA_OK || sink.offers != 3 || pm_resume(session) != PMEDIA_OK) goto fail;
            sink.audio_result = PMEDIA_OK;
            sink.video_result = PMEDIA_WOULD_BLOCK;
            if (media_av_pump(session, &sink, 9000000) != PMEDIA_WOULD_BLOCK || sink.offers != 4 || sink.last_audio ||
                media_av_pump(session, &sink, 9000000) != PMEDIA_WOULD_BLOCK || sink.offers != 5 || sink.videos) goto fail;
            sink.video_result = PMEDIA_CALLBACK_STOP;
            if (media_av_pump(session, &sink, 9000000) != PMEDIA_OK || sink.videos != 1 ||
                pm_pump(session, 19000000, 0) != PMEDIA_OK || sink.offers != 6 || pm_resume(session) != PMEDIA_OK) goto fail;
            sink.video_result = PMEDIA_OK;
            if (media_av_pump(session, &sink, 19000000) != PMEDIA_WOULD_BLOCK ||
                media_av_pump(session, &sink, 19000000 + ref->audio[1].pts_us - 1) != PMEDIA_WOULD_BLOCK ||
                media_av_pump(session, &sink, 19000000 + ref->audio[1].pts_us) != PMEDIA_OK || sink.audios != 2) goto fail;
            if (pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK || pm_resume(session) != PMEDIA_OK) goto fail;
            memset(&sink, 0, sizeof(sink));
            sink.reference = ref;
            if (media_av_pump(session, &sink, maximum - 1000000) != PMEDIA_OK) goto fail;
            for (i = 1; i < 32; i++) if (media_av_pump(session, &sink, maximum) != PMEDIA_OK) goto fail;
            if (media_av_pump(session, &sink, maximum) != PMEDIA_EOF || sink.videos != 3 || sink.audios != 29 ||
                sink.events.errors || sink.events.error_callbacks || pm_seek(session, 0) != PMEDIA_OK) goto fail;
            memset(&sink, 0, sizeof(sink));
            sink.reference = ref;
            sink.audio_result = PMEDIA_WOULD_BLOCK;
            if (media_av_pump(session, &sink, 0) != PMEDIA_WOULD_BLOCK ||
                pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK ||
                pm_pump(session, -1, 0) != PMEDIA_OK || sink.offers != 1 || pm_resume(session) != PMEDIA_OK) goto fail;
            memset(&sink, 0, sizeof(sink));
            sink.reference = ref;
            sink.video_result = PMEDIA_WOULD_BLOCK;
            if (media_av_pump(session, &sink, 0) != PMEDIA_OK ||
                media_av_pump(session, &sink, 0) != PMEDIA_WOULD_BLOCK || sink.videos || sink.audios != 1 ||
                pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK || sink.events.stopped_events != 1 ||
                pm_pump(session, maximum, 0) != PMEDIA_ERROR_STATE) goto fail;
            memset(&info, 0, sizeof(info));
            info.size = sizeof(info);
            if (pm_get_stream_info(session, &info) != PMEDIA_OK || !info.has_video || !info.has_audio ||
                pm_get_backend(session) != PMEDIA_BACKEND_SOFT || pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
            for (i = 0; i < 2; i++) {
                g_media_compressed_error = "mixed negative callback fatal once, no recovery until close";
                input.read_mode = input.position = 0;
                memset(&sink, 0, sizeof(sink));
                sink.reference = ref;
                if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
                input.read_mode = 2;
                if (i) {
                    if (media_av_pump(session, &sink, 0) != PMEDIA_OK) goto fail;
                    sink.video_result = -1;
                } else sink.audio_result = -1;
                if (media_av_pump(session, &sink, 0) != PMEDIA_ERROR_CALLBACK || sink.events.errors ||
                    sink.events.error_callbacks != 1 || sink.events.error_events != 1 || sink.events.eof_events ||
                    pm_pump(session, maximum, 0) != PMEDIA_ERROR_STATE || pm_pause(session) != PMEDIA_ERROR_STATE ||
                    pm_resume(session) != PMEDIA_ERROR_STATE || pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
                    sink.events.error_callbacks != 1 || pm_stop(session) != PMEDIA_OK || pm_close(session) != PMEDIA_OK) goto fail;
                session = NULL;
            }
        }
        free(data);
        data = NULL;
    }
    free(ref);
    if (progress != NULL) progress("mixed common-PTS schedule and decoder/PCM fidelity passed");
    return TRUE;
fail:
    _snprintf(phase, sizeof(phase), "mixed failure rc/state videos=%d audio=%d offers=%d errors=%d callbacks=%d: %s",
              sink.videos, sink.audios, sink.offers, sink.events.errors, sink.events.error_callbacks,
              session == NULL ? "no session" : pm_last_error(session));
    if (progress != NULL) progress(phase);
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    free(ref);
    return FALSE;
}

BOOL test1356_media_av_timeline_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = { L"baseline-aac.mp4", L"mixed-clock-video-late.mp4",
        L"mixed-clock-audio-late.mp4", L"mixed-clock-shifted.mp4" };
    static const int video_start[] = { 0, 400000, 0, 600000 };
    static const int audio_start[] = { 0, 0, 400000, 600000 };
    media_fixture_source input;
    media_av_sink sink;
    media_av_reference *reference;
    media_av_reference *control;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_session session;
    unsigned char *data;
    pm_position origin;
    pm_position pts;
    pm_position previous;
    pm_position base;
    pm_position maximum;
    int bytes;
    int test;
    int seekable;
    int pass;
    int i;
    int result;
    int offers;
    int first_audio;
    int paused;
    int audio_tail;
    int video_tail;
    char phase[192];

    reference = (media_av_reference *)calloc(1, sizeof(*reference));
    control = (media_av_reference *)calloc(1, sizeof(*control));
    session = NULL;
    data = NULL;
    memset(&sink, 0, sizeof(sink));
    if (reference == NULL || control == NULL) goto fail;
    maximum = (((pm_position)0x7fffffff) << 32) | (pm_position)0xffffffffUL;
    media_contract_init(&input, &sink.events, &source, &output, &options);
    output.video = media_av_video;
    output.audio = media_av_audio;
    output.error = media_compressed_error;
    for (test = 0; test < 4; test++) {
        _snprintf(phase, sizeof(phase), "mixed timeline case=%d decode-only source PTS, priming and full bytes", test);
        g_media_compressed_error = "mixed timeline default source/PCM reference";
        if (progress != NULL) progress(phase);
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        input.data = data;
        input.bytes = bytes;
        input.position = input.read_mode = 0;
        input.read_chunk = 7;
        source.seek = media_fixture_seek;
        source.tell = media_fixture_tell;
        options.flags = 0;
        memset(&sink, 0, sizeof(sink));
        memset(reference, 0, sizeof(*reference));
        sink.reference = test ? reference : control;
        sink.capture = 1;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
        input.read_mode = 2;
        result = PMEDIA_OK;
        for (i = 0; i < 3000 && result == PMEDIA_OK; i++) result = pm_pump(session, 0, 2000);
        if (result != PMEDIA_EOF || sink.videos != 3 || sink.audios != 29 ||
            sink.bytes != 118784 || sink.events.errors || sink.events.error_callbacks ||
            sink.events.eof_events != 1 || pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        if (!test) {
            free(data);
            data = NULL;
            continue;
        }
        _snprintf(phase, sizeof(phase), "mixed timeline observed source case=%d video=%d..%d audio=%d..%d samples=%d",
                  test, (int)reference->video[0].pts_us, (int)reference->video[2].pts_us,
                  (int)reference->audio[0].pts_us, (int)reference->audio[28].pts_us, sink.audios * 1024);
        if (progress != NULL) progress(phase);
        if (memcmp(reference->pcm, control->pcm, (size_t)control->bytes)) goto fail;
        for (i = 0; i < 3; i++) {
            if (reference->video[i].pts_us != video_start[test] + (pm_position)i * 200000 ||
                reference->video[i].duration_us != control->video[i].duration_us ||
                reference->video[i].flags != control->video[i].flags ||
                reference->video_hash[i] != control->video_hash[i] ||
                reference->video[i].width != 320 || reference->video[i].height != 240) goto fail;
        }
        for (i = 0; i < 29; i++) {
            if (reference->audio[i].pts_us != audio_start[test] +
                ((pm_position)i * 1024 * 1000000 + 24000) / 48000 ||
                reference->audio[i].duration_us != control->audio[i].duration_us) goto fail;
        }
        origin = video_start[test] < audio_start[test] ? video_start[test] : audio_start[test];
        first_audio = audio_start[test] <= video_start[test];
        for (seekable = 0; seekable < 2; seekable++) {
            _snprintf(phase, sizeof(phase), "mixed timeline case=%d seekable=%d pending-head rewind then both EOF directions", test, seekable);
            g_media_compressed_error = "mixed timeline source-clock/pending rewind";
            if (progress != NULL) progress(phase);
            input.position = input.read_mode = 0;
            source.seek = seekable ? media_fixture_seek : NULL;
            source.tell = seekable ? media_fixture_tell : NULL;
            options.flags = PMEDIA_OPEN_SCHEDULE_AV;
            memset(&sink, 0, sizeof(sink));
            sink.reference = reference;
            if (first_audio) sink.audio_result = PMEDIA_WOULD_BLOCK;
            else sink.video_result = PMEDIA_WOULD_BLOCK;
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK || input.position != bytes) goto fail;
            input.read_mode = 2;
            if (media_av_pump(session, &sink, 7000000) != PMEDIA_WOULD_BLOCK ||
                sink.offers != 1 || sink.audios || sink.videos || sink.last_audio != first_audio ||
                pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK ||
                pm_pump(session, -1, 0) != PMEDIA_OK || sink.offers != 1 || pm_resume(session) != PMEDIA_OK) goto fail;
            for (pass = 0; pass < 2; pass++) {
                if (pass && (pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK ||
                    pm_pump(session, -1, 0) != PMEDIA_OK || pm_resume(session) != PMEDIA_OK)) goto fail;
                memset(&sink, 0, sizeof(sink));
                sink.reference = reference;
                base = pass ? maximum - 2000000 : 5000000;
                previous = origin;
                paused = audio_tail = video_tail = 0;
                g_media_compressed_error = "mixed timeline deadlines/content/pause/tail";
                for (i = 0; i < 32; i++) {
                    pts = sink.audios < reference->audios && (sink.videos == reference->videos ||
                          reference->audio[sink.audios].pts_us <= reference->video[sink.videos].pts_us) ?
                          reference->audio[sink.audios].pts_us : reference->video[sink.videos].pts_us;
                    offers = sink.offers;
                    if (!pass && !paused && i > 0 && pts > previous) {
                        if (pm_pause(session) != PMEDIA_OK || pm_pump(session, maximum, 0) != PMEDIA_OK ||
                            sink.offers != offers || pm_resume(session) != PMEDIA_OK) goto fail;
                        base += 9000000;
                        if (media_av_pump(session, &sink, base + previous - origin) != PMEDIA_WOULD_BLOCK ||
                            sink.offers != offers) goto fail;
                        paused = 1;
                    }
                    if (pts > previous && (media_av_pump(session, &sink, base + pts - origin - 1) != PMEDIA_WOULD_BLOCK ||
                        sink.offers != offers)) goto fail;
                    if (media_av_pump(session, &sink, base + pts - origin) != PMEDIA_OK ||
                        sink.offers != offers + 1 || sink.events.errors || sink.events.eof_events ||
                        (!i && sink.last_audio != first_audio) || pm_seek(session, 1) != PMEDIA_ERROR_NOT_SEEKABLE ||
                        pm_resume(session) != PMEDIA_OK) goto fail;
                    if (sink.audios == 29 && sink.videos < 3) video_tail = 1;
                    if (sink.videos == 3 && sink.audios < 29) audio_tail = 1;
                    previous = pts;
                }
                if (sink.audios != 29 || sink.videos != 3 || sink.bytes != reference->bytes ||
                    video_tail != (test == 1) || audio_tail != (test != 1) ||
                    sink.last_audio != (test != 1) || media_av_pump(session, &sink, maximum) != PMEDIA_EOF ||
                    sink.events.eof_events != 1 || sink.events.errors || sink.events.error_callbacks ||
                    pm_pump(session, maximum, 0) != PMEDIA_EOF || sink.events.eof_events != 1) goto fail;
                _snprintf(phase, sizeof(phase), "mixed timeline case=%d seekable=%d pass=%d 3/29 bytes identical; first=%s tail=%s EOF once",
                          test, seekable, pass, first_audio ? "audio" : "video", video_tail ? "video" : "audio");
                if (progress != NULL) progress(phase);
            }
            if (pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK || sink.events.stopped_events != 1 ||
                pm_pump(session, maximum, 0) != PMEDIA_ERROR_STATE || pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
        }
        free(data);
        data = NULL;
    }
    free(control);
    free(reference);
    if (progress != NULL) progress("mixed relative/common source origins and both EOF tails passed");
    return TRUE;
fail:
    _snprintf(phase, sizeof(phase), "mixed timeline failure: video=%d audio=%d offers=%d errors=%d callbacks=%d: %s",
              sink.videos, sink.audios, sink.offers, sink.events.errors, sink.events.error_callbacks,
              session == NULL ? "no session" : pm_last_error(session));
    phase[sizeof(phase) - 1] = '\0';
    if (progress != NULL) progress(phase);
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    if (control != NULL) free(control);
    if (reference != NULL) free(reference);
    return FALSE;
}

BOOL test1357_media_av_pts_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = { L"baseline-aac.mp4", L"mixed-pts-negative.mp4",
        L"mixed-pts-backward.mp4", L"mixed-pts-gap.mp4", L"mixed-pts-duplicate.mp4",
        L"mixed-pts-fallback.mp4" };
    static const int expected_pts[6][3] = { { 0, 200000, 400000 }, { -100000, 100000, 300000 },
        { 400000, 200000, 400000 }, { 0, 600000, 800000 }, { 200000, 200000, 400000 },
        { 0, 200000, 400000 } };
    media_fixture_source input;
    media_av_sink sink;
    media_av_reference *reference;
    media_av_reference *control;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_session session;
    unsigned char *data;
    pm_position maximum;
    pm_position pts;
    pm_position previous;
    pm_position base;
    int bytes;
    int test;
    int seekable;
    int pass;
    int i;
    int result;
    int offers;
    char phase[192];

    reference = (media_av_reference *)calloc(1, sizeof(*reference));
    control = (media_av_reference *)calloc(1, sizeof(*control));
    session = NULL;
    data = NULL;
    memset(&sink, 0, sizeof(sink));
    if (reference == NULL || control == NULL) goto fail;
    maximum = (((pm_position)0x7fffffff) << 32) | (pm_position)0xffffffffUL;
    media_contract_init(&input, &sink.events, &source, &output, &options);
    output.video = media_av_video;
    output.audio = media_av_audio;
    output.error = media_compressed_error;
    for (test = 0; test < 6; test++) {
        _snprintf(phase, sizeof(phase), "mixed PTS case=%d decode-only best-effort/reference", test);
        g_media_compressed_error = "mixed PTS default reference";
        if (progress != NULL) progress(phase);
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        input.data = data;
        input.bytes = bytes;
        input.position = input.read_mode = 0;
        input.read_chunk = 7;
        source.seek = media_fixture_seek;
        source.tell = media_fixture_tell;
        options.flags = 0;
        memset(&sink, 0, sizeof(sink));
        memset(reference, 0, sizeof(*reference));
        sink.reference = test ? reference : control;
        sink.capture = 1;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
        input.read_mode = 2;
        result = PMEDIA_OK;
        for (i = 0; i < 3000 && result == PMEDIA_OK; i++) result = pm_pump(session, 0, 2000);
        if (result != PMEDIA_EOF || sink.videos != 3 || sink.audios != 29 || sink.bytes != 118784 ||
            sink.events.errors || sink.events.error_callbacks || sink.events.eof_events != 1) goto fail;
        _snprintf(phase, sizeof(phase), "mixed PTS observed case=%d video=%d/%d/%d audio=%d..%d",
                  test, (int)sink.reference->video[0].pts_us, (int)sink.reference->video[1].pts_us,
                  (int)sink.reference->video[2].pts_us, (int)sink.reference->audio[0].pts_us,
                  (int)sink.reference->audio[28].pts_us);
        if (progress != NULL) progress(phase);
        if (pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        if (!test) {
            free(data);
            data = NULL;
            continue;
        }
        if (memcmp(reference->pcm, control->pcm, (size_t)control->bytes)) goto fail;
        for (i = 0; i < 3; i++) {
            if (reference->video[i].pts_us != expected_pts[test][i] ||
                reference->video[i].duration_us != control->video[i].duration_us ||
                reference->video[i].flags != control->video[i].flags ||
                reference->video_hash[i] != control->video_hash[i] ||
                reference->video[i].width != 320 || reference->video[i].height != 240) goto fail;
        }
        for (i = 0; i < 29; i++) {
            if (reference->audio[i].pts_us != control->audio[i].pts_us ||
                reference->audio[i].duration_us != control->audio[i].duration_us) goto fail;
        }
        for (seekable = 0; seekable < 2; seekable++) {
            for (pass = 0; pass < 2; pass++) {
                _snprintf(phase, sizeof(phase), "mixed PTS case=%d seekable=%d pass=%d exact prefix/deadlines/terminal", test, seekable, pass);
                g_media_compressed_error = "mixed PTS scheduling/guard lifecycle";
                if (progress != NULL) progress(phase);
                memset(&sink, 0, sizeof(sink));
                sink.reference = reference;
                if (test < 3 || !pass) {
                    input.position = input.read_mode = 0;
                    source.seek = seekable ? media_fixture_seek : NULL;
                    source.tell = seekable ? media_fixture_tell : NULL;
                    options.flags = PMEDIA_OPEN_SCHEDULE_AV;
                    if (pm_open(&source, &options, &output, &session) != PMEDIA_OK || input.position != bytes) goto fail;
                    input.read_mode = 2;
                }
                base = pass ? maximum - 2000000 : 3000000;
                if (test < 3) {
                    result = media_av_pump(session, &sink, 0);
                    for (i = 0; i < 64 && result == PMEDIA_OK; i++) result = media_av_pump(session, &sink, maximum);
                    _snprintf(phase, sizeof(phase), "mixed PTS rejected case=%d rc=%d video=%d audio=%d error=%s",
                              test, result, sink.videos, sink.audios, pm_last_error(session));
                    phase[sizeof(phase) - 1] = '\0';
                    if (progress != NULL) progress(phase);
                    if (result != PMEDIA_ERROR_FORMAT || sink.videos != (test == 2 ? 1 : 0) ||
                        sink.audios != (test == 2 ? 19 : 0) || sink.events.errors || sink.events.eof_events ||
                        sink.events.error_callbacks != 1 || sink.events.error_events != 1 ||
                        sink.events.last_error != PMEDIA_ERROR_FORMAT ||
                        strcmp(pm_last_error(session), "mixed video requires nonnegative nondecreasing presentation timestamps") ||
                        pm_pump(session, maximum, 0) != PMEDIA_ERROR_STATE || pm_pause(session) != PMEDIA_ERROR_STATE ||
                        pm_resume(session) != PMEDIA_ERROR_STATE || pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
                        sink.events.error_callbacks != 1 || sink.events.error_events != 1) goto fail;
                } else {
                    previous = 0;
                    for (i = 0; i < 32; i++) {
                        pts = sink.audios < reference->audios && (sink.videos == reference->videos ||
                              reference->audio[sink.audios].pts_us <= reference->video[sink.videos].pts_us) ?
                              reference->audio[sink.audios].pts_us : reference->video[sink.videos].pts_us;
                        offers = sink.offers;
                        if (pts > previous && (media_av_pump(session, &sink, base + pts - 1) != PMEDIA_WOULD_BLOCK ||
                            sink.offers != offers)) goto fail;
                        if (media_av_pump(session, &sink, base + pts) != PMEDIA_OK || sink.offers != offers + 1 ||
                            sink.events.errors || sink.events.eof_events || pm_seek(session, 1) != PMEDIA_ERROR_NOT_SEEKABLE) goto fail;
                        previous = pts;
                    }
                    if (sink.videos != 3 || sink.audios != 29 || sink.bytes != reference->bytes ||
                        media_av_pump(session, &sink, maximum) != PMEDIA_EOF || sink.events.eof_events != 1 ||
                        sink.events.errors || sink.events.error_callbacks) goto fail;
                    if (!pass) {
                        if (pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK ||
                            pm_pump(session, -1, 0) != PMEDIA_OK || pm_resume(session) != PMEDIA_OK) goto fail;
                        continue;
                    }
                }
                offers = sink.offers;
                if (pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK || sink.events.stopped_events != 1 ||
                    pm_pump(session, maximum, 0) != PMEDIA_ERROR_STATE || pm_close(session) != PMEDIA_OK ||
                    sink.offers != offers) goto fail;
                session = NULL;
            }
        }
        free(data);
        data = NULL;
    }
    free(control);
    free(reference);
    if (progress != NULL) progress("mixed video negative/backward guard, gap/duplicate and best-effort fallback passed");
    return TRUE;
fail:
    _snprintf(phase, sizeof(phase), "mixed PTS failure: video=%d audio=%d offers=%d errors=%d: %s",
              sink.videos, sink.audios, sink.offers, sink.events.errors,
              session == NULL ? "no session" : pm_last_error(session));
    phase[sizeof(phase) - 1] = '\0';
    if (progress != NULL) progress(phase);
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    if (control != NULL) free(control);
    if (reference != NULL) free(reference);
    return FALSE;
}

typedef struct media_config_sink {
    media_av_sink av;
    int channels;
    int limit;
} media_config_sink;

static int media_config_audio(void *opaque, const pm_audio_block *block)
{
    media_config_sink *config;
    media_av_sink *sink;
    media_av_reference *ref;
    config = (media_config_sink *)opaque;
    sink = &config->av;
    ref = sink->reference;
    sink->offers++;
    sink->last_audio = 1;
    if (block == NULL || block->size != sizeof(*block) || block->data == NULL ||
        block->sample_rate != 48000 || block->channels != config->channels || block->samples != 1024 ||
        block->bytes != config->channels * 2048 || sink->audios >= config->limit ||
        sink->bytes + block->bytes > sizeof(ref->pcm)) goto invalid;
    if (sink->capture) {
        ref->audio[sink->audios] = *block;
        memcpy(ref->pcm + sink->bytes, block->data, (size_t)block->bytes);
    } else if (sink->audios >= ref->audios ||
               block->pts_us != ref->audio[sink->audios].pts_us ||
               block->duration_us != ref->audio[sink->audios].duration_us ||
               memcmp(ref->pcm + sink->bytes, block->data, (size_t)block->bytes) ||
               (sink->videos < ref->videos && ref->video[sink->videos].pts_us < block->pts_us)) goto invalid;
    if (sink->audio_result == PMEDIA_WOULD_BLOCK) return PMEDIA_WOULD_BLOCK;
    sink->audios++;
    sink->bytes += block->bytes;
    if (sink->capture) { ref->audios = sink->audios; ref->bytes = sink->bytes; }
    return PMEDIA_OK;
invalid:
    sink->events.errors++;
    return -1;
}

BOOL test1358_media_av_config_contract(void (*progress)(const char *))
{
    static const WCHAR *names[] = { L"aac-lc.aac", L"aac-lc-stereo.aac", L"mixed-config-repeat.mp4",
        L"mixed-config-rate-down.mp4", L"mixed-config-mono.mp4", L"mixed-config-return.mp4", L"mixed-config-stereo.mp4" };
    media_fixture_source input;
    media_config_sink sink;
    media_av_reference *refs[3];
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_session session;
    unsigned char *data;
    pm_position maximum;
    pm_position base;
    pm_position pts;
    pm_position previous;
    int bytes;
    int test;
    int seekable;
    int pass;
    int i;
    int result;
    int offers;
    char phase[192];

    for (i = 0; i < 3; i++) refs[i] = (media_av_reference *)calloc(1, sizeof(*refs[i]));
    memset(&sink, 0, sizeof(sink));
    session = NULL;
    data = NULL;
    if (refs[0] == NULL || refs[1] == NULL || refs[2] == NULL) goto fail;
    maximum = (((pm_position)0x7fffffff) << 32) | (pm_position)0xffffffffUL;
    media_contract_init(&input, &sink.av.events, &source, &output, &options);
    output.video = media_av_video;
    output.audio = media_config_audio;
    output.error = media_compressed_error;
    for (test = 0; test < 7; test++) {
        _snprintf(phase, sizeof(phase), "mixed config case=%d references and sample-description transitions", test);
        g_media_compressed_error = "mixed config reference/transition";
        if (progress != NULL) progress(phase);
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        input.data = data;
        input.bytes = bytes;
        input.read_chunk = 7;
        if (test < 3) {
            input.position = input.read_mode = 0;
            source.seek = media_fixture_seek;
            source.tell = media_fixture_tell;
            options.flags = 0;
            memset(&sink, 0, sizeof(sink));
            sink.av.reference = refs[test];
            sink.av.capture = 1;
            sink.channels = test == 1 ? 2 : 1;
            sink.limit = test == 2 ? 60 : 30;
            if (pm_open(&source, &options, &output, &session) != PMEDIA_OK) goto fail;
            input.read_mode = 2;
            result = PMEDIA_OK;
            for (i = 0; i < 3000 && result == PMEDIA_OK; i++) result = pm_pump(session, 0, 2000);
            if (result != PMEDIA_EOF || sink.av.audios != sink.limit || sink.av.videos != (test == 2 ? 3 : 0) ||
                sink.av.events.eof_events != 1 || sink.av.events.errors || sink.av.events.error_callbacks ||
                pm_close(session) != PMEDIA_OK) goto fail;
            session = NULL;
            if (test < 2) { free(data); data = NULL; continue; }
            if (memcmp(refs[2]->pcm, refs[0]->pcm, (size_t)refs[0]->bytes)) goto fail;
            for (i = 0; i < 3; i++) {
                if (refs[2]->video[i].pts_us != 400000 + (pm_position)i * 200000) goto fail;
            }
            for (i = 0; i < 2; i++) {
                memcpy(refs[i]->video, refs[2]->video, sizeof(refs[i]->video));
                memcpy(refs[i]->video_hash, refs[2]->video_hash, sizeof(refs[i]->video_hash));
                refs[i]->videos = 3;
            }
        }
        for (seekable = 0; seekable < 2; seekable++) {
            for (pass = 0; pass < 2; pass++) {
                memset(&sink, 0, sizeof(sink));
                sink.av.reference = refs[test == 2 ? 2 : test == 4 ? 1 : 0];
                sink.channels = test == 4 ? 2 : 1;
                sink.limit = test == 2 ? 60 : 30;
                if (test != 2 || !pass) {
                    input.position = input.read_mode = 0;
                    source.seek = seekable ? media_fixture_seek : NULL;
                    source.tell = seekable ? media_fixture_tell : NULL;
                    options.flags = PMEDIA_OPEN_SCHEDULE_AV;
                    if (pm_open(&source, &options, &output, &session) != PMEDIA_OK || input.position != bytes) goto fail;
                    input.read_mode = 2;
                }
                base = pass ? maximum - 2000000 : 5000000;
                if (test == 2) {
                    previous = 0;
                    for (i = 0; i < 63; i++) {
                        pts = sink.av.audios < 60 && (sink.av.videos == 3 ||
                              refs[2]->audio[sink.av.audios].pts_us <= refs[2]->video[sink.av.videos].pts_us) ?
                              refs[2]->audio[sink.av.audios].pts_us : refs[2]->video[sink.av.videos].pts_us;
                        offers = sink.av.offers;
                        if (pts > previous && (media_av_pump(session, &sink.av, base + pts - 1) != PMEDIA_WOULD_BLOCK ||
                            sink.av.offers != offers)) goto fail;
                        if (media_av_pump(session, &sink.av, base + pts) != PMEDIA_OK ||
                            sink.av.offers != offers + 1 || sink.av.events.errors || sink.av.events.eof_events) goto fail;
                        previous = pts;
                    }
                    if (media_av_pump(session, &sink.av, maximum) != PMEDIA_EOF || sink.av.events.eof_events != 1 ||
                        sink.av.events.error_callbacks || sink.av.bytes != refs[2]->bytes) goto fail;
                    if (!pass) {
                        if (pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK ||
                            pm_pump(session, -1, 0) != PMEDIA_OK || pm_resume(session) != PMEDIA_OK) goto fail;
                        continue;
                    }
                } else {
                    result = media_av_pump(session, &sink.av, 0);
                    for (i = 0; i < 100 && result == PMEDIA_OK; i++) result = media_av_pump(session, &sink.av, maximum);
                    _snprintf(phase, sizeof(phase), "mixed config observed case=%d seekable=%d pass=%d rc=%d video=%d audio=%d errors=%d detail=%s",
                              test, seekable, pass, result, sink.av.videos, sink.av.audios, sink.av.events.errors, pm_last_error(session));
                    phase[sizeof(phase) - 1] = '\0';
                    if (progress != NULL) progress(phase);
                    if (result != PMEDIA_ERROR_FORMAT || sink.av.videos != 2 || sink.av.audios != 30 || sink.av.events.errors ||
                        sink.av.bytes != refs[test == 4 ? 1 : 0]->bytes || sink.av.events.eof_events ||
                        sink.av.events.error_callbacks != 1 || sink.av.events.error_events != 1 ||
                        strcmp(pm_last_error(session), "mixed audio codec configuration changed") ||
                        pm_pump(session, maximum, 0) != PMEDIA_ERROR_STATE || pm_pause(session) != PMEDIA_ERROR_STATE ||
                        pm_resume(session) != PMEDIA_ERROR_STATE || pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
                        sink.av.events.error_callbacks != 1 || sink.av.events.error_events != 1) goto fail;
                }
                if (pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK || sink.av.events.stopped_events != 1 ||
                    pm_close(session) != PMEDIA_OK) goto fail;
                session = NULL;
            }
        }
        free(data);
        data = NULL;
    }
    for (i = 0; i < 3; i++) free(refs[i]);
    if (progress != NULL) progress("mixed identical descriptions accepted; changed AAC config rejected before decode/callback with exact prefix");
    return TRUE;
fail:
    _snprintf(phase, sizeof(phase), "mixed config failure: video=%d audio=%d bytes=%d errors=%d: %s",
              sink.av.videos, sink.av.audios, sink.av.bytes, sink.av.events.errors,
              session == NULL ? "no session" : pm_last_error(session));
    phase[sizeof(phase) - 1] = '\0';
    if (progress != NULL) progress(phase);
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    for (i = 0; i < 3; i++) if (refs[i] != NULL) free(refs[i]);
    return FALSE;
}

static BOOL media_video_change_contract(int external, void (*progress)(const char *))
{
    static const WCHAR *inband[] = { L"mixed-video-repeat.mp4", L"mixed-video-vga.mp4",
        L"mixed-video-oversize.mp4", L"mixed-video-high.mp4", L"mixed-video-high422.mp4",
        L"mixed-video-interlaced.mp4", L"mixed-video-vga.mp4" };
    static const WCHAR *outband[] = { L"mixed-avcc-repeat.mp4", L"mixed-avcc-vga.mp4",
        L"mixed-avcc-oversize.mp4", L"mixed-avcc-high.mp4", L"mixed-avcc-high422.mp4",
        L"mixed-avcc-interlaced.mp4", L"mixed-avcc-vga.mp4" };
    const WCHAR *const *names;
    const char *guard_message;
    media_fixture_source input;
    media_av_sink sink;
    media_av_reference *refs[3];
    media_av_reference *reference;
    pm_source_callbacks source;
    pm_output_callbacks output;
    pm_open_options options;
    pm_session session;
    unsigned char *data;
    pm_position maximum;
    pm_position base;
    pm_position pts;
    pm_position previous;
    int test;
    int seekable;
    int pass;
    int i;
    int bytes;
    int result;
    int expected;
    int offers;
    char phase[192];

    names = external ? outband : inband;
    guard_message = external ? "MP4 H.264 avcC change validation failed" : "MP4 in-band H.264 SPS validation failed";
    for (i = 0; i < 3; i++) refs[i] = (media_av_reference *)calloc(1, sizeof(*refs[i]));
    memset(&sink, 0, sizeof(sink));
    session = NULL;
    data = NULL;
    if (refs[0] == NULL || refs[1] == NULL || refs[2] == NULL) goto fail;
    maximum = (((pm_position)0x7fffffff) << 32) | (pm_position)0xffffffffUL;
    media_contract_init(&input, &sink.events, &source, &output, &options);
    output.video = media_av_video;
    output.audio = media_av_audio;
    output.error = media_compressed_error;
    for (test = 0; test < 7; test++) {
        _snprintf(phase, sizeof(phase), "video change case=%d default reference/output guard", test);
        g_media_compressed_error = "mixed video change reference/guard";
        if (progress != NULL) progress(phase);
        data = media_compressed_load(names[test], &bytes);
        if (data == NULL) goto fail;
        input.data = data;
        input.bytes = bytes;
        input.position = input.read_mode = 0;
        input.read_chunk = 7;
        source.seek = media_fixture_seek;
        source.tell = media_fixture_tell;
        options.flags = 0;
        options.max_video_width = test == 6 ? 320 : 640;
        reference = refs[test < 2 ? test : 2];
        memset(reference, 0, sizeof(*reference));
        memset(&sink, 0, sizeof(sink));
        sink.reference = reference;
        sink.capture = 1;
        if (pm_open(&source, &options, &output, &session) != PMEDIA_OK || input.position != bytes) goto fail;
        input.read_mode = 2;
        result = PMEDIA_OK;
        for (i = 0; i < 3000 && result == PMEDIA_OK; i++) result = pm_pump(session, 0, 2000);
        expected = test < 2 ? PMEDIA_EOF : (test == 2 || test == 6 ? PMEDIA_ERROR_LIMIT : PMEDIA_ERROR_UNSUPPORTED);
        _snprintf(phase, sizeof(phase), "video change default case=%d rc=%d video=%d audio=%d errors=%d",
                  test, result, sink.videos, sink.audios, sink.events.errors);
        if (progress != NULL) progress(phase);
        if (result != expected || sink.videos != (test < 2 ? 6 : 3) || sink.audios != 29 || sink.events.errors ||
            sink.events.eof_events != (test < 2 ? 1 : 0) || sink.events.error_callbacks != (test < 2 ? 0 : 1) ||
            sink.events.error_events != (test < 2 ? 0 : 1)) goto fail;
        if (test >= 2 && strcmp(pm_last_error(session), guard_message)) goto fail;
        for (i = 0; i < reference->videos; i++) {
            if (reference->video[i].pts_us != 400000 + (pm_position)i * 200000 ||
                reference->video[i].duration_us != 200000 || reference->video[i].height != (test == 1 && i >= 3 ? 480 : 240) ||
                reference->video[i].width != (test == 1 && i >= 3 ? 640 : 320) ||
                reference->video[i].flags != (i == 0 || i == 3 ? PMEDIA_FRAME_KEY : 0)) goto fail;
            if (test >= 2 && reference->video_hash[i] != refs[0]->video_hash[i]) goto fail;
        }
        if (test && (reference->bytes != refs[0]->bytes || memcmp(reference->pcm, refs[0]->pcm, (size_t)reference->bytes))) goto fail;
        if (test) for (i = 0; i < 29; i++) {
            if (reference->audio[i].pts_us != refs[0]->audio[i].pts_us ||
                reference->audio[i].duration_us != refs[0]->audio[i].duration_us) goto fail;
        }
        /* This explicit parameter validation failure is terminal in default mode
         * too; that does not generalize the policy of other legacy errors. */
        if (test >= 2 && (pm_pump(session, 0, 0) != PMEDIA_ERROR_STATE || pm_pause(session) != PMEDIA_ERROR_STATE ||
            pm_resume(session) != PMEDIA_ERROR_STATE || pm_seek(session, 0) != PMEDIA_ERROR_STATE ||
            sink.events.error_callbacks != 1 || sink.events.error_events != 1 || pm_stop(session) != PMEDIA_OK ||
            pm_stop(session) != PMEDIA_OK || sink.events.stopped_events != 1)) goto fail;
        if (pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
        for (seekable = 0; seekable < 2; seekable++) {
            for (pass = 0; pass < 2; pass++) {
                memset(&sink, 0, sizeof(sink));
                sink.reference = reference;
                if (test >= 2 || !pass) {
                    input.position = input.read_mode = 0;
                    source.seek = seekable ? media_fixture_seek : NULL;
                    source.tell = seekable ? media_fixture_tell : NULL;
                    options.flags = PMEDIA_OPEN_SCHEDULE_AV;
                    result = pm_open(&source, &options, &output, &session);
                    if (test >= 2 && test != 5) {
                        /* NOFILLIN stream analysis reaches the changed header
                         * in these short files: require exact open rejection,
                         * not an open-or-pump alternative that could hide drift. */
                        _snprintf(phase, sizeof(phase), "video change scheduled open case=%d seekable=%d pass=%d rc=%d video=%d audio=%d",
                                  test, seekable, pass, result, sink.videos, sink.audios);
                        if (progress != NULL) progress(phase);
                        if (result != expected || session != NULL || input.position != bytes || sink.videos || sink.audios ||
                            sink.events.errors || sink.events.eof_events || sink.events.error_callbacks != 1 ||
                            sink.events.error_events != 1) goto fail;
                        continue;
                    }
                    if (result != PMEDIA_OK || input.position != bytes) goto fail;
                    input.read_mode = 2;
                }
                base = pass ? maximum - 2000000 : 5000000;
                if (test < 2) {
                    previous = 0;
                    for (i = 0; i < 35; i++) {
                        pts = sink.audios < 29 && (sink.videos == 6 ||
                              reference->audio[sink.audios].pts_us <= reference->video[sink.videos].pts_us) ?
                              reference->audio[sink.audios].pts_us : reference->video[sink.videos].pts_us;
                        offers = sink.offers;
                        if (pts > previous && (media_av_pump(session, &sink, base + pts - 1) != PMEDIA_WOULD_BLOCK ||
                            sink.offers != offers)) goto fail;
                        if (sink.videos == 3 && pts == reference->video[3].pts_us) {
                            sink.video_result = PMEDIA_WOULD_BLOCK;
                            if (media_av_pump(session, &sink, base + pts) != PMEDIA_WOULD_BLOCK ||
                                sink.offers != offers + 1 || sink.videos != 3 || pm_pause(session) != PMEDIA_OK ||
                                pm_pump(session, -1, 0) != PMEDIA_OK || pm_resume(session) != PMEDIA_OK) goto fail;
                            offers++;
                            sink.video_result = PMEDIA_OK;
                        }
                        if (media_av_pump(session, &sink, base + pts) != PMEDIA_OK || sink.offers != offers + 1 ||
                            sink.events.errors || sink.events.eof_events) goto fail;
                        previous = pts;
                    }
                    if (media_av_pump(session, &sink, maximum) != PMEDIA_EOF ||
                        media_av_pump(session, &sink, maximum) != PMEDIA_EOF || sink.events.eof_events != 1 ||
                        sink.events.error_callbacks || sink.bytes != reference->bytes) goto fail;
                    if (!pass) {
                        if (pm_pause(session) != PMEDIA_OK || pm_seek(session, 0) != PMEDIA_OK ||
                            pm_pump(session, -1, 0) != PMEDIA_OK || pm_resume(session) != PMEDIA_OK) goto fail;
                        continue;
                    }
                } else {
                    /* The fixed interlaced input opens successfully: require
                     * the new parameters to fail before any changed frame. */
                    result = media_av_pump(session, &sink, 0);
                    for (i = 0; i < 100 && result == PMEDIA_OK; i++) result = media_av_pump(session, &sink, maximum);
                    _snprintf(phase, sizeof(phase), "video change scheduled %s case=%d seekable=%d pass=%d rc=%d video=%d audio=%d errors=%d",
                              external ? "avcC" : "SPS", test, seekable, pass, result, sink.videos, sink.audios, sink.events.errors);
                    if (progress != NULL) progress(phase);
                    /* Stream analysis exposes B-frame delay. Decoding ahead
                     * reaches the bad SPS before the two queued prefix frames
                     * can be received; no attempt to flush after fatal error. */
                    if (result != PMEDIA_ERROR_UNSUPPORTED || sink.videos != 1 || sink.audios != 19 || sink.events.errors ||
                        sink.bytes != 19 * 4096 || sink.events.eof_events || sink.events.error_callbacks != 1 ||
                        sink.events.error_events != 1 || pm_pump(session, maximum, 0) != PMEDIA_ERROR_STATE ||
                        pm_pause(session) != PMEDIA_ERROR_STATE || pm_resume(session) != PMEDIA_ERROR_STATE ||
                        pm_seek(session, 0) != PMEDIA_ERROR_STATE || sink.events.error_callbacks != 1 ||
                        sink.events.error_events != 1 ||
                        strcmp(pm_last_error(session), guard_message)) goto fail;
                }
                if (pm_stop(session) != PMEDIA_OK || pm_stop(session) != PMEDIA_OK || sink.events.stopped_events != 1 ||
                    pm_close(session) != PMEDIA_OK) goto fail;
                session = NULL;
            }
        }
        free(data);
        data = NULL;
    }
    for (i = 0; i < 3; i++) free(refs[i]);
    if (progress != NULL) progress(external ? "mixed external avcC repeat/VGA accepted, out-of-bound descriptions rejected with exact prefix and lifecycle" :
        "mixed in-band repeat/VGA accepted, out-of-bound parameters rejected with exact prefix and lifecycle");
    return TRUE;
fail:
    _snprintf(phase, sizeof(phase), "video change failure: video=%d audio=%d bytes=%d errors=%d: %s",
              sink.videos, sink.audios, sink.bytes, sink.events.errors, session == NULL ? "no session" : pm_last_error(session));
    phase[sizeof(phase) - 1] = '\0';
    if (progress != NULL) progress(phase);
    if (session != NULL) pm_close(session);
    if (data != NULL) free(data);
    for (i = 0; i < 3; i++) if (refs[i] != NULL) free(refs[i]);
    return FALSE;
}

BOOL test1359_media_video_change_contract(void (*progress)(const char *))
{
    return media_video_change_contract(0, progress);
}

BOOL test1360_media_video_config_contract(void (*progress)(const char *))
{
    return media_video_change_contract(1, progress);
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

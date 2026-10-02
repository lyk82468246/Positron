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
    unsigned long audio_magnitude;
    pm_position audio_pts;
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

    sink = (media_compressed_sink *)context;
    if (frame == NULL || frame->size != sizeof(*frame) ||
        frame->format != PMEDIA_PIXEL_I420 || frame->width != sink->width ||
        frame->height != sink->height || frame->flags & PMEDIA_FRAME_INTERLACED ||
        frame->pts_us != (pm_position)sink->frames * 200000 ||
        frame->duration_us != 200000 ||
        (sink->frames == 0 && !(frame->flags & PMEDIA_FRAME_KEY))) goto invalid;
    for (plane = 0; plane < 3; plane++) {
        width = plane == 0 ? frame->width : (frame->width + 1) / 2;
        height = plane == 0 ? frame->height : (frame->height + 1) / 2;
        target = plane == 0 ? 81 : (plane == 1 ? 90 : 240);
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
    }
    sink->events.errors++;
    return -1;
}

static int media_compressed_audio(void *context, const pm_audio_block *block)
{
    media_compressed_sink *sink;
    int i;
    int sample;

    sink = (media_compressed_sink *)context;
    if (block == NULL || block->size != sizeof(*block) || block->data == NULL ||
        block->sample_rate != 48000 || block->channels != sink->channels ||
        block->samples <= 0 || block->samples > 8192 ||
        block->bytes != block->samples * block->channels * 2 ||
        block->pts_us < sink->audio_pts ||
        block->duration_us != (pm_position)block->samples * 1000000 / 48000) {
        sink->events.errors++;
        return -1;
    }
    sink->audio_pts = block->pts_us;
    for (i = 0; i < block->samples * block->channels; i++) {
        sample = (short)((unsigned int)block->data[i * 2] |
                         ((unsigned int)block->data[i * 2 + 1] << 8));
        sink->audio_magnitude += (unsigned long)(sample < 0 ? -sample : sample);
    }
    sink->events.samples += block->samples;
    sink->events.blocks++;
    return PMEDIA_OK;
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

    session = NULL;
    data = NULL;
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
        for (pass = 0; pass < 2; pass++) {
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
                if (pm_seek(session, 0) != PMEDIA_OK) goto fail;
                memset(&sink, 0, sizeof(sink));
                sink.width = test == 1 ? 640 : 320;
                sink.height = test == 1 ? 480 : 240;
                sink.channels = test == 0 ? 2 : 1;
            }
        }
        if (pm_close(session) != PMEDIA_OK) goto fail;
        session = NULL;
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
    return FALSE;
}

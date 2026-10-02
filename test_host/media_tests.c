#include <windows.h>
#include <string.h>

#include "positron_media.h"

typedef struct media_fixture_source {
    const unsigned char *data;
    int bytes;
    int position;
    int read_mode;
    int seek_mode;
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
    if (amount > 3) amount = 3;
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

#include <windows.h>
#include <string.h>

#include "positron_media.h"

typedef struct media_fixture_source {
    const unsigned char *data;
    int bytes;
    int position;
} media_fixture_source;

typedef struct media_fixture_output {
    int blocks;
    int samples;
    int errors;
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

    output = (media_fixture_output *)context;
    if (output == NULL || block == NULL || block->sample_rate != 8000 ||
        block->channels != 1 || block->bytes != block->samples * 2 ||
        block->samples <= 0) {
        if (output != NULL) output->errors++;
        return PMEDIA_ERROR_CALLBACK;
    }
    output->blocks++;
    output->samples += block->samples;
    return PMEDIA_OK;
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

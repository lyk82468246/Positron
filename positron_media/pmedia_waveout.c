#include "pmedia_waveout.h"

#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>

#define PMEDIA_WAVEOUT_FRAMES 2048

struct pmedia_waveout {
    const unsigned char *input;
    int input_bytes;
    int data_offset;
    int data_bytes;
    int sample_rate;
    int channels;
    int bits_per_sample;
    int block_align;
    int total_frames;
    int cursor;
    int paused;
    int stopped;
    int prepared;
    int pending;
    HWAVEOUT device;
    WAVEHDR header;
    unsigned char *buffer;
    int buffer_bytes;
    pm_output_callbacks output;
};

static void pm_waveout_error(char *error_text,
                             int error_text_bytes,
                             const char *message)
{
    int length;

    if (error_text == NULL || error_text_bytes <= 0) return;
    if (message == NULL) message = "WaveOut backend failed";
    length = (int)strlen(message);
    if (length >= error_text_bytes) length = error_text_bytes - 1;
    memcpy(error_text, message, (size_t)length);
    error_text[length] = '\0';
}

static int pm_waveout_reset(pmedia_waveout *context)
{
    MMRESULT result;

    if (context == NULL || context->device == NULL) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    result = waveOutReset(context->device);
    if (result != MMSYSERR_NOERROR) return PMEDIA_ERROR_NATIVE;
    if (context->prepared) {
        result = waveOutUnprepareHeader(context->device,
                                        &context->header,
                                        sizeof(context->header));
        if (result != MMSYSERR_NOERROR) return PMEDIA_ERROR_NATIVE;
        context->prepared = 0;
    }
    context->pending = 0;
    memset(&context->header, 0, sizeof(context->header));
    return PMEDIA_OK;
}

static int pm_waveout_collect(pmedia_waveout *context)
{
    MMRESULT result;

    if (context == NULL || context->device == NULL) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    if (!context->pending) return PMEDIA_OK;
    if ((context->header.dwFlags & WHDR_DONE) == 0) {
        return PMEDIA_WOULD_BLOCK;
    }
    result = waveOutUnprepareHeader(context->device,
                                    &context->header,
                                    sizeof(context->header));
    if (result != MMSYSERR_NOERROR) return PMEDIA_ERROR_NATIVE;
    context->prepared = 0;
    context->pending = 0;
    memset(&context->header, 0, sizeof(context->header));
    return PMEDIA_OK;
}

int pmedia_waveout_open(const unsigned char *input,
                        int input_bytes,
                        int data_offset,
                        int data_bytes,
                        int sample_rate,
                        int channels,
                        int bits_per_sample,
                        int total_frames,
                        const pm_output_callbacks *output,
                        pmedia_waveout **out_context,
                        char *error_text,
                        int error_text_bytes)
{
    pmedia_waveout *context;
    WAVEFORMATEX format;
    MMRESULT result;

    if (out_context != NULL) *out_context = NULL;
    if (input == NULL || input_bytes <= 0 || out_context == NULL ||
        data_offset < 0 || data_bytes <= 0 ||
        data_offset > input_bytes || data_bytes > input_bytes - data_offset ||
        sample_rate <= 0 || (channels != 1 && channels != 2) ||
        (bits_per_sample != 8 && bits_per_sample != 16) ||
        total_frames <= 0) {
        return PMEDIA_ERROR_ARGUMENT;
    }
    context = (pmedia_waveout *)calloc(1, sizeof(*context));
    if (context == NULL) {
        pm_waveout_error(error_text, error_text_bytes,
                         "WaveOut context allocation failed");
        return PMEDIA_ERROR_MEMORY;
    }
    context->input = input;
    context->input_bytes = input_bytes;
    context->data_offset = data_offset;
    context->data_bytes = data_bytes;
    context->sample_rate = sample_rate;
    context->channels = channels;
    context->bits_per_sample = bits_per_sample;
    context->block_align = channels * (bits_per_sample / 8);
    context->total_frames = total_frames;
    context->buffer_bytes = PMEDIA_WAVEOUT_FRAMES * context->block_align;
    context->buffer = (unsigned char *)malloc((size_t)context->buffer_bytes);
    if (context->buffer == NULL) {
        pm_waveout_error(error_text, error_text_bytes,
                         "WaveOut buffer allocation failed");
        free(context);
        return PMEDIA_ERROR_MEMORY;
    }
    if (output != NULL) memcpy(&context->output, output, sizeof(*output));
    memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = (WORD)channels;
    format.nSamplesPerSec = (DWORD)sample_rate;
    format.wBitsPerSample = (WORD)bits_per_sample;
    format.nBlockAlign = (WORD)context->block_align;
    format.nAvgBytesPerSec = (DWORD)(sample_rate * context->block_align);
    result = waveOutOpen(&context->device,
                         WAVE_MAPPER,
                         &format,
                         0,
                         0,
                         CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        pm_waveout_error(error_text, error_text_bytes,
                         "WaveOut device rejected the PCM format");
        free(context->buffer);
        free(context);
        return PMEDIA_ERROR_NATIVE;
    }
    *out_context = context;
    return PMEDIA_OK;
}

void pmedia_waveout_close(pmedia_waveout *context)
{
    if (context == NULL) return;
    if (context->device != NULL) {
        waveOutReset(context->device);
        if (context->prepared) {
            waveOutUnprepareHeader(context->device,
                                   &context->header,
                                   sizeof(context->header));
            context->prepared = 0;
        }
        waveOutClose(context->device);
        context->device = NULL;
    }
    if (context->buffer != NULL) free(context->buffer);
    free(context);
}

int pmedia_waveout_pump(pmedia_waveout *context, int max_frames)
{
    int result;
    int frames;
    int bytes;
    pm_audio_block block;
    int callback_result;
    MMRESULT wave_result;

    if (context == NULL) return PMEDIA_ERROR_ARGUMENT;
    if (context->stopped) return PMEDIA_ERROR_STATE;
    if (context->paused) return PMEDIA_OK;
    result = pm_waveout_collect(context);
    if (result != PMEDIA_OK) return result;
    if (context->cursor >= context->total_frames) return PMEDIA_EOF;
    frames = context->total_frames - context->cursor;
    if (frames > PMEDIA_WAVEOUT_FRAMES) frames = PMEDIA_WAVEOUT_FRAMES;
    if (max_frames > 0 && frames > max_frames) frames = max_frames;
    bytes = frames * context->block_align;
    if (bytes <= 0 || bytes > context->buffer_bytes ||
        context->data_offset + context->cursor * context->block_align <
            context->data_offset ||
        context->cursor * context->block_align > context->data_bytes - bytes) {
        return PMEDIA_ERROR_FORMAT;
    }
    memcpy(context->buffer,
           context->input + context->data_offset +
               context->cursor * context->block_align,
           (size_t)bytes);
    memset(&block, 0, sizeof(block));
    block.size = sizeof(block);
    block.data = context->buffer;
    block.bytes = bytes;
    block.samples = frames;
    block.sample_rate = context->sample_rate;
    block.channels = context->channels;
    block.pts_us = ((pm_position)context->cursor * 1000000) /
                   context->sample_rate;
    block.duration_us = ((pm_position)frames * 1000000) /
                        context->sample_rate;
    if (context->output.audio != NULL) {
        callback_result = context->output.audio(context->output.context, &block);
        if (callback_result < 0) return PMEDIA_ERROR_CALLBACK;
        if (callback_result == PMEDIA_CALLBACK_STOP) {
            context->paused = 1;
            return PMEDIA_CALLBACK_STOP;
        }
    }
    memset(&context->header, 0, sizeof(context->header));
    context->header.lpData = (LPSTR)context->buffer;
    context->header.dwBufferLength = (DWORD)bytes;
    wave_result = waveOutPrepareHeader(context->device,
                                       &context->header,
                                       sizeof(context->header));
    if (wave_result != MMSYSERR_NOERROR) return PMEDIA_ERROR_NATIVE;
    context->prepared = 1;
    wave_result = waveOutWrite(context->device,
                               &context->header,
                               sizeof(context->header));
    if (wave_result != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(context->device,
                               &context->header,
                               sizeof(context->header));
        context->prepared = 0;
        memset(&context->header, 0, sizeof(context->header));
        return PMEDIA_ERROR_NATIVE;
    }
    context->pending = 1;
    context->cursor += frames;
    return PMEDIA_OK;
}

int pmedia_waveout_pause(pmedia_waveout *context)
{
    if (context == NULL) return PMEDIA_ERROR_ARGUMENT;
    if (waveOutPause(context->device) != MMSYSERR_NOERROR) {
        return PMEDIA_ERROR_NATIVE;
    }
    context->paused = 1;
    return PMEDIA_OK;
}

int pmedia_waveout_resume(pmedia_waveout *context)
{
    if (context == NULL) return PMEDIA_ERROR_ARGUMENT;
    if (waveOutRestart(context->device) != MMSYSERR_NOERROR) {
        return PMEDIA_ERROR_NATIVE;
    }
    context->paused = 0;
    return PMEDIA_OK;
}

int pmedia_waveout_stop(pmedia_waveout *context)
{
    int result;

    if (context == NULL) return PMEDIA_ERROR_ARGUMENT;
    result = pm_waveout_reset(context);
    if (result != PMEDIA_OK) return result;
    context->stopped = 1;
    return PMEDIA_OK;
}

int pmedia_waveout_seek(pmedia_waveout *context, pm_position position_us)
{
    pm_position frame;
    int result;

    if (context == NULL || position_us < 0) return PMEDIA_ERROR_ARGUMENT;
    result = pm_waveout_reset(context);
    if (result != PMEDIA_OK) return result;
    frame = (position_us * (pm_position)context->sample_rate) / 1000000;
    if (frame > context->total_frames) frame = context->total_frames;
    context->cursor = (int)frame;
    return PMEDIA_OK;
}

#ifndef POSITRON_MEDIA_WAVEOUT_H
#define POSITRON_MEDIA_WAVEOUT_H

#include "positron_media.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pmedia_waveout pmedia_waveout;

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
                        int error_text_bytes);
void pmedia_waveout_close(pmedia_waveout *context);
int pmedia_waveout_pump(pmedia_waveout *context, int max_frames);
int pmedia_waveout_pause(pmedia_waveout *context);
int pmedia_waveout_resume(pmedia_waveout *context);
int pmedia_waveout_stop(pmedia_waveout *context);
int pmedia_waveout_seek(pmedia_waveout *context, pm_position position_us);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_MEDIA_WAVEOUT_H */

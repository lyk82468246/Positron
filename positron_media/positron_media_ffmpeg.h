#ifndef POSITRON_MEDIA_FFMPEG_H
#define POSITRON_MEDIA_FFMPEG_H

#include "positron_media.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pmedia_ffmpeg pmedia_ffmpeg;

int pmedia_ffmpeg_open(const unsigned char *input,
                       int input_bytes,
                       const pm_output_callbacks *output,
                       int max_video_width,
                       int max_video_height,
                       pmedia_ffmpeg **out_context,
                       pm_stream_info *out_info,
                       pm_capabilities *out_capabilities,
                       char *error_text,
                       int error_text_bytes);

int pmedia_ffmpeg_probe(const unsigned char *input,
                        int input_bytes,
                        pm_stream_info *out_info,
                        pm_capabilities *out_capabilities,
                        char *error_text,
                        int error_text_bytes);

void pmedia_ffmpeg_close(pmedia_ffmpeg *context);
int pmedia_ffmpeg_pump(pmedia_ffmpeg *context,
                       pm_position clock_us,
                       int budget_us);
int pmedia_ffmpeg_seek(pmedia_ffmpeg *context, pm_position position_us);

#ifdef __cplusplus
}
#endif

#endif /* POSITRON_MEDIA_FFMPEG_H */

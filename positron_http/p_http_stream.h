#ifndef P_HTTP_STREAM_INTERNAL_H
#define P_HTTP_STREAM_INTERNAL_H
#include <stddef.h>
/* Internal reuse of the existing HTTP URL parser; not a public export. */
int phttp_text_utf8(const char* text, size_t bytes);
int phttp_stream_url_parts(const char* url, char* host, char* path,
        int* port, int* scheme);
int phttp_stream_initialized(void);
#endif

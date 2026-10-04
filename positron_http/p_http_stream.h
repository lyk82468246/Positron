#ifndef P_HTTP_STREAM_INTERNAL_H
#define P_HTTP_STREAM_INTERNAL_H
/* Internal reuse of the existing HTTP URL parser; not a public export. */
int phttp_stream_url_parts(const char* url, char* host, char* path,
        int* port, int* scheme);
int phttp_stream_initialized(void);
#endif

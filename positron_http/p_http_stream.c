/* Streaming protocol state belongs to HTTP, never the file-saving host. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "positron_http.h"
#include "positron_tls.h"
#include "p_http_stream.h"

typedef struct stream_request {
    CRITICAL_SECTION lock;
    HANDLE cancel;
    int state; /* 0 created, 1 executing, 2 terminal; guarded by lock */
    int probe_only; /* immutable request family; no pretend file sink */
    int sensitive_headers;
    PHttpStreamOptions options;
    PHttpStreamResult result;
    char request_headers[8192];
    char response_headers[PHTTP_STREAM_HEADERS_MAX_BYTES + 1];
    unsigned long response_header_bytes;
} stream_request;
typedef struct stream_reader {
    stream_request* request;
    HANDLE transport;
    char block[PHTTP_STREAM_BLOCK_BYTES];
    int position;
    int length;
} stream_reader;
static int stream_ci(const char* a, const char* b, size_t bytes)
{
    size_t i;
    int x;
    int y;
    for (i = 0; i < bytes; i++) {
        x = (unsigned char)a[i]; y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') { x += 32; }
        if (y >= 'A' && y <= 'Z') { y += 32; }
        if (x != y) { return 0; }
    }
    return 1;
}
static int stream_equal(const char* a, const char* b)
{ return strlen(a) == strlen(b) && stream_ci(a, b, strlen(a)); }
int phttp_text_utf8(const char* text, size_t bytes)
{
    size_t i;
    unsigned long code;
    unsigned long minimum;
    unsigned int c;
    int more;
    i = 0;
    while (i < bytes) {
        c = (unsigned char)text[i++];
        if (c < 128) { continue; }
        if (c >= 194 && c <= 223) { more = 1; code = c & 31; minimum = 128; }
        else if (c >= 224 && c <= 239) { more = 2; code = c & 15; minimum = 2048; }
        else if (c >= 240 && c <= 244) { more = 3; code = c & 7; minimum = 65536; }
        else { return 0; }
        while (more-- > 0) {
            if (i == bytes || ((unsigned char)text[i] & 192) != 128) { return 0; }
            code = (code << 6) | ((unsigned char)text[i++] & 63);
        }
        if (code < minimum || code > 1114111 || (code >= 55296 && code <= 57343)) { return 0; }
    }
    return 1;
}
static int stream_cancelled(stream_request* request)
{ return WaitForSingleObject(request->cancel, 0) == WAIT_OBJECT_0; }
static int stream_transport_error(stream_request* request, int error)
{
    request->result.transport_error = error;
    if (error == PTLS_TRANSPORT_CANCELLED) { return PHTTP_STREAM_CANCELLED; }
    if (error == PTLS_TRANSPORT_TIMEOUT) { return PHTTP_STREAM_TIMEOUT; }
    if (error == PTLS_TRANSPORT_LIMIT) { return PHTTP_STREAM_LIMIT; }
    if (error == PTLS_TRANSPORT_MEMORY) { return PHTTP_STREAM_MEMORY; }
    return PHTTP_STREAM_TRANSPORT;
}
static int stream_fill(stream_reader* reader)
{
    int bytes;
    if (stream_cancelled(reader->request)) { return PHTTP_STREAM_CANCELLED; }
    bytes = PTls_TransportRead(reader->transport, reader->block, sizeof(reader->block));
    if (bytes < 0) { return stream_transport_error(reader->request, bytes); }
    reader->position = 0; reader->length = bytes;
    return bytes == 0 ? 0 : 1;
}
/* Return 1 line, 0 EOF before any byte, or a negative STREAM error. */
static int stream_line(stream_reader* reader, char* output, unsigned long capacity)
{
    unsigned long used;
    unsigned char c;
    int rc;
    int cr;
    used = 0; cr = 0;
    for (;;) {
        if (reader->position == reader->length) {
            rc = stream_fill(reader);
            if (rc <= 0) { return rc == 0 && used != 0 ? PHTTP_STREAM_PROTOCOL : rc; }
        }
        c = (unsigned char)reader->block[reader->position++];
        if (cr) {
            if (c != '\n') { return PHTTP_STREAM_PROTOCOL; }
            output[used] = '\0';
            return phttp_text_utf8(output, used) ? 1 : PHTTP_STREAM_UNSUPPORTED;
        }
        if (c == '\r') { cr = 1; continue; }
        if (c == '\n' || c == 0 || c == 127 || (c < 32 && c != '\t')) { return PHTTP_STREAM_PROTOCOL; }
        if (used + 1 >= capacity) { return PHTTP_STREAM_LIMIT; }
        output[used++] = (char)c;
    }
}
static int stream_header_name(const char* line, size_t* colon)
{
    size_t i;
    unsigned char c;
    for (i = 0; line[i] != '\0' && line[i] != ':'; i++) {
        c = (unsigned char)line[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || strchr("!#$%&'*+-.^_`|~", c) != NULL)) { return 0; }
    }
    if (i == 0 || line[i] != ':') { return 0; }
    *colon = i; return 1;
}
static int stream_field(const char* block, const char* name,
        unsigned long occurrence, char* out, unsigned long capacity)
{
    const char* start;
    const char* end;
    const char* value;
    size_t name_bytes;
    size_t bytes;
    start = strstr(block, "\r\n");
    if (start == NULL) { return PHTTP_STREAM_STATE; }
    start += 2; name_bytes = strlen(name);
    while (*start != '\0' && *start != '\r') {
        end = strstr(start, "\r\n");
        if (end == NULL) { return PHTTP_STREAM_PROTOCOL; }
        if ((size_t)(end - start) > name_bytes && start[name_bytes] == ':' &&
                stream_ci(start, name, name_bytes)) {
            if (occurrence == 0) {
                value = start + name_bytes + 1;
                while (value < end && (*value == ' ' || *value == '\t')) { value++; }
                while (end > value && (end[-1] == ' ' || end[-1] == '\t')) { end--; }
                bytes = (size_t)(end - value);
                if (bytes >= capacity) { return PHTTP_STREAM_LIMIT; }
                memcpy(out, value, bytes); out[bytes] = '\0'; return PHTTP_STREAM_OK;
            }
            occurrence--;
        }
        start = end + 2;
    }
    return PHTTP_STREAM_STATE; /* missing occurrence, output unchanged */
}
static int stream_decimal(const char* text, __int64* result)
{
    __int64 value;
    size_t i;
    value = 0;
    if (text[0] == '\0') { return 0; }
    for (i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9' || value > (9223372036854775807i64 - 9) / 10) { return 0; }
        value = value * 10 + text[i] - '0';
    }
    *result = value; return 1;
}
static int stream_headers(stream_reader* reader, int* chunked, char* location)
{
    static const char* SINGLETON[] = { "Content-Length", "Transfer-Encoding", "Content-Type",
        "Content-Disposition", "Content-Encoding", "Location" };
    stream_request* request;
    char line[1024];
    char duplicate[1024];
    char length_text[64];
    char transfer[64];
    unsigned long used;
    unsigned long fields;
    unsigned long i;
    unsigned long interim;
    size_t bytes;
    size_t colon;
    int rc;
    int status;
    request = reader->request;
    interim = 0;
again:
    used = fields = 0;
    rc = stream_line(reader, line, sizeof(line));
    if (rc <= 0) { return rc == 0 ? PHTTP_STREAM_PROTOCOL : rc; }
    if (strlen(line) < 12 || (!stream_ci(line, "HTTP/1.1 ", 9) && !stream_ci(line, "HTTP/1.0 ", 9)) ||
            line[9] < '1' || line[9] > '5' || line[10] < '0' || line[10] > '9' ||
            line[11] < '0' || line[11] > '9' || (line[12] != '\0' && line[12] != ' ')) { return PHTTP_STREAM_PROTOCOL; }
    status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';
    for (;;) {
        bytes = strlen(line);
        if (used + bytes + 2 > PHTTP_STREAM_HEADERS_MAX_BYTES) { return PHTTP_STREAM_LIMIT; }
        memcpy(request->response_headers + used, line, bytes);
        used += (unsigned long)bytes;
        memcpy(request->response_headers + used, "\r\n", 2); used += 2;
        request->response_headers[used] = '\0';
        rc = stream_line(reader, line, sizeof(line));
        if (rc <= 0) { return rc == 0 ? PHTTP_STREAM_PROTOCOL : rc; }
        if (line[0] == '\0') {
            if (used + 2 > PHTTP_STREAM_HEADERS_MAX_BYTES) { return PHTTP_STREAM_LIMIT; }
            memcpy(request->response_headers + used, "\r\n", 3); used += 2; break;
        }
        if (++fields > PHTTP_STREAM_MAX_HEADER_FIELDS) { return PHTTP_STREAM_LIMIT; }
        if (!stream_header_name(line, &colon)) { return PHTTP_STREAM_PROTOCOL; }
    }
    if (status < 200) {
        if (status == 101 || ++interim > 4) { return PHTTP_STREAM_UNSUPPORTED; }
        goto again;
    }
    request->response_header_bytes = used;
    for (i = 0; i < sizeof(SINGLETON) / sizeof(SINGLETON[0]); i++) {
        rc = stream_field(request->response_headers, SINGLETON[i], 1, duplicate, sizeof(duplicate));
        if (rc != PHTTP_STREAM_STATE) { return PHTTP_STREAM_PROTOCOL; }
    }
    request->result.status_code = status; request->result.total = -1;
    length_text[0] = transfer[0] = location[0] = '\0';
    request->result.content_type[0] = request->result.content_disposition[0] = request->result.content_encoding[0] = '\0';
    rc = stream_field(request->response_headers, "Content-Length", 0, length_text, sizeof(length_text));
    if (rc == PHTTP_STREAM_LIMIT || (rc == 0 && !stream_decimal(length_text, &request->result.total))) { return PHTTP_STREAM_PROTOCOL; }
    rc = stream_field(request->response_headers, "Transfer-Encoding", 0, transfer, sizeof(transfer));
    if (rc == PHTTP_STREAM_LIMIT) { return PHTTP_STREAM_LIMIT; }
    if (rc == PHTTP_STREAM_OK && transfer[0] == '\0') { return PHTTP_STREAM_PROTOCOL; }
    *chunked = transfer[0] != '\0';
    if (*chunked && length_text[0] != '\0') { return PHTTP_STREAM_PROTOCOL; }
    if (*chunked && !stream_equal(transfer, "chunked")) { return PHTTP_STREAM_UNSUPPORTED; }
    if (stream_field(request->response_headers, "Content-Type", 0, request->result.content_type, sizeof(request->result.content_type)) == PHTTP_STREAM_LIMIT ||
            stream_field(request->response_headers, "Content-Disposition", 0, request->result.content_disposition, sizeof(request->result.content_disposition)) == PHTTP_STREAM_LIMIT ||
            stream_field(request->response_headers, "Content-Encoding", 0, request->result.content_encoding, sizeof(request->result.content_encoding)) == PHTTP_STREAM_LIMIT ||
            stream_field(request->response_headers, "Location", 0, location, PHTTP_URL_MAX) == PHTTP_STREAM_LIMIT) { return PHTTP_STREAM_LIMIT; }
    return PHTTP_STREAM_OK;
}
static int stream_body(stream_reader* reader, __int64 wanted, int until_eof)
{
    stream_request* request;
    int rc;
    int bytes;
    __int64 available;
    request = reader->request;
    while (until_eof || wanted > 0) {
        if (stream_cancelled(request)) { return PHTTP_STREAM_CANCELLED; }
        if (reader->position == reader->length) {
            rc = stream_fill(reader);
            if (rc <= 0) { return rc == 0 ? (until_eof ? PHTTP_STREAM_OK : PHTTP_STREAM_PROTOCOL) : rc; }
        }
        bytes = reader->length - reader->position;
        if (!until_eof && wanted < bytes) { bytes = (int)wanted; }
        available = (__int64)request->options.body_limit - request->result.received;
        if (available < bytes) { return PHTTP_STREAM_LIMIT; }
        rc = request->options.sink(request->options.user_data,
                (unsigned char*)reader->block + reader->position, (unsigned long)bytes,
                request->result.received + bytes, request->result.total);
        if (rc != 0) { return PHTTP_STREAM_SINK; }
        request->result.received += bytes; reader->position += bytes;
        wanted -= bytes;
    }
    return PHTTP_STREAM_OK;
}
static int stream_chunked(stream_reader* reader)
{
    char line[1024];
    __int64 bytes;
    unsigned long i;
    unsigned long trailers;
    unsigned long trailer_bytes;
    size_t colon;
    int digit;
    int rc;
    for (;;) {
        rc = stream_line(reader, line, sizeof(line));
        if (rc <= 0) { return rc == 0 ? PHTTP_STREAM_PROTOCOL : rc; }
        bytes = 0;
        for (i = 0; line[i] != '\0' && line[i] != ';'; i++) {
            digit = line[i] >= '0' && line[i] <= '9' ? line[i] - '0' :
                    line[i] >= 'a' && line[i] <= 'f' ? line[i] - 'a' + 10 :
                    line[i] >= 'A' && line[i] <= 'F' ? line[i] - 'A' + 10 : -1;
            if (digit < 0 || bytes > (9223372036854775807i64 - 15) / 16) { return PHTTP_STREAM_PROTOCOL; }
            bytes = bytes * 16 + digit;
        }
        if (i == 0) { return PHTTP_STREAM_PROTOCOL; }
        if (bytes > (__int64)reader->request->options.body_limit - reader->request->result.received) { return PHTTP_STREAM_LIMIT; }
        if (bytes == 0) { break; }
        rc = stream_body(reader, bytes, 0);
        if (rc != 0) { return rc; }
        rc = stream_line(reader, line, sizeof(line));
        if (rc <= 0 || line[0] != '\0') { return rc < 0 ? rc : PHTTP_STREAM_PROTOCOL; }
    }
    trailers = trailer_bytes = 0;
    for (;;) {
        rc = stream_line(reader, line, sizeof(line));
        if (rc <= 0) { return rc < 0 ? rc : PHTTP_STREAM_PROTOCOL; }
        trailer_bytes += (unsigned long)strlen(line) + 2;
        if (trailer_bytes > PHTTP_STREAM_HEADERS_MAX_BYTES) { return PHTTP_STREAM_LIMIT; }
        if (line[0] == '\0') { return PHTTP_STREAM_OK; }
        if (++trailers > PHTTP_STREAM_MAX_HEADER_FIELDS || !stream_header_name(line, &colon)) { return PHTTP_STREAM_PROTOCOL; }
        if ((colon == 14 && stream_ci(line, "Content-Length", colon)) ||
                (colon == 17 && stream_ci(line, "Transfer-Encoding", colon)) ||
                (colon == 16 && stream_ci(line, "Content-Encoding", colon))) { return PHTTP_STREAM_PROTOCOL; }
    }
}
static int stream_create_get(const char* url, const char** headers,
        const PHttpStreamOptions* options, HANDLE* out_request, int probe_only)
{
    stream_request* request;
    char canonical[PHTTP_URL_MAX];
    unsigned long i;
    unsigned long used;
    size_t bytes;
    size_t colon;
    size_t j;
    if (!phttp_stream_initialized()) { return PHTTP_STREAM_STATE; }
    if (options == NULL || options->size != sizeof(*options) || options->version != PHTTP_STREAM_VERSION ||
            options->timeout_ms == 0 || options->timeout_ms > 120000 || options->body_limit == 0 ||
            options->body_limit > PHTTP_STREAM_BODY_MAX_BYTES ||
            (!probe_only && options->sink == NULL) || out_request == NULL ||
            PHttp_ResolveReferenceUrl(NULL, url, canonical, sizeof(canonical)) != 0) { return PHTTP_STREAM_ARGUMENT; }
    request = (stream_request*)calloc(1, sizeof(*request));
    if (request == NULL) { return PHTTP_STREAM_MEMORY; }
    used = 0;
    if (headers != NULL) {
        for (i = 0; headers[i] != NULL; i++) {
            if (i == 32) { free(request); return PHTTP_STREAM_LIMIT; }
            bytes = strlen(headers[i]);
            if (bytes > 1023 || used + bytes + 2 >= sizeof(request->request_headers)) { free(request); return PHTTP_STREAM_LIMIT; }
            if (!phttp_text_utf8(headers[i], bytes) || !stream_header_name(headers[i], &colon) ||
                    (colon == 4 && stream_ci(headers[i], "Host", colon)) ||
                    (colon == 14 && stream_ci(headers[i], "Content-Length", colon)) ||
                    (colon == 10 && stream_ci(headers[i], "Connection", colon)) ||
                    (colon == 17 && stream_ci(headers[i], "Transfer-Encoding", colon)) ||
                    (colon == 15 && stream_ci(headers[i], "Accept-Encoding", colon)) ||
                    (colon == 19 && stream_ci(headers[i], "Proxy-Authorization", colon))) {
                free(request); return PHTTP_STREAM_ARGUMENT;
            }
            if ((colon == 13 && stream_ci(headers[i], "Authorization", colon)) ||
                    (colon == 6 && stream_ci(headers[i], "Cookie", colon))) { request->sensitive_headers = 1; }
            for (j = colon + 1; j < bytes; j++) {
                if ((unsigned char)headers[i][j] < 32 || (unsigned char)headers[i][j] == 127) { free(request); return PHTTP_STREAM_ARGUMENT; }
            }
            memcpy(request->request_headers + used, headers[i], bytes); used += (unsigned long)bytes;
            memcpy(request->request_headers + used, "\r\n", 2); used += 2;
        }
    }
    request->cancel = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (request->cancel == NULL) { free(request); return PHTTP_STREAM_MEMORY; }
    InitializeCriticalSection(&request->lock);
    request->options = *options; request->result.size = sizeof(request->result);
    request->probe_only = probe_only;
    request->result.version = PHTTP_STREAM_VERSION; request->result.total = -1;
    strcpy(request->result.final_url, canonical);
    *out_request = request;
    return PHTTP_STREAM_OK;
}
static int stream_identity(const char* headers, char* etag,
        char* modified, unsigned long* flags)
{
    char duplicate[256];
    const char* p;
    size_t bytes;
    int rc;
    etag[0] = modified[0] = '\0'; *flags = 0;
    if (stream_field(headers, "ETag", 1, duplicate, sizeof(duplicate)) != PHTTP_STREAM_STATE ||
            stream_field(headers, "Last-Modified", 1, duplicate, sizeof(duplicate)) != PHTTP_STREAM_STATE) {
        return PHTTP_STREAM_PROTOCOL;
    }
    rc = stream_field(headers, "ETag", 0, etag, 256);
    if (rc != PHTTP_STREAM_STATE && rc != PHTTP_STREAM_OK) return rc;
    if (rc == PHTTP_STREAM_OK) {
        p = etag;
        if (p[0] == 'W' && p[1] == '/') p += 2;
        bytes = strlen(p);
        if (bytes < 2 || p[0] != '"' || p[bytes - 1] != '"') return PHTTP_STREAM_PROTOCOL;
        if (p == etag) *flags |= PHTTP_PROBE_FLAG_STRONG_ETAG;
        p++;
        while (p[1] != '\0') {
            if ((unsigned char)*p < 33 || *p == '"' || (unsigned char)*p == 127) return PHTTP_STREAM_PROTOCOL;
            p++;
        }
    }
    rc = stream_field(headers, "Last-Modified", 0, modified, 128);
    return rc == PHTTP_STREAM_STATE ? PHTTP_STREAM_OK : rc;
}
static int stream_execute(HANDLE handle, int probe_only)
{
    stream_request* request;
    stream_reader* reader;
    PTlsTransportOptions transport_options;
    DWORD started;
    DWORD elapsed;
    char host[256];
    char path[1024];
    char location[PHTTP_URL_MAX];
    char next[PHTTP_URL_MAX];
    char next_host[256];
    char next_path[1024];
    char etag[256];
    char modified[128];
    unsigned long identity_flags;
    char* wire;
    int port;
    int scheme;
    int next_scheme;
    int next_port;
    int chunked;
    int bytes;
    int written;
    int rc;
    int error;
    request = (stream_request*)handle;
    if (request == NULL) { return PHTTP_STREAM_ARGUMENT; }
    EnterCriticalSection(&request->lock);
    if (request->state != 0 || request->probe_only != probe_only) {
        LeaveCriticalSection(&request->lock); return PHTTP_STREAM_STATE;
    }
    request->state = 1; LeaveCriticalSection(&request->lock);
    started = GetTickCount(); wire = NULL;
    reader = (stream_reader*)calloc(1, sizeof(*reader));
    rc = PHTTP_STREAM_MEMORY;
    if (reader == NULL) { goto done; }
    reader->request = request;
    for (;;) {
        rc = PHTTP_STREAM_CANCELLED;
        if (stream_cancelled(request)) { break; }
        rc = PHTTP_STREAM_TIMEOUT;
        elapsed = (DWORD)(GetTickCount() - started);
        if (elapsed >= request->options.timeout_ms) { break; }
        rc = PHTTP_STREAM_ARGUMENT;
        if (phttp_stream_url_parts(request->result.final_url, host, path, &port, &scheme) != 0) { break; }
        memset(&transport_options, 0, sizeof(transport_options));
        transport_options.size = sizeof(transport_options); transport_options.version = PTLS_TRANSPORT_VERSION;
        transport_options.use_tls = scheme == PHTTP_SCHEME_HTTPS; transport_options.verify_peer = 1;
        transport_options.timeout_ms = request->options.timeout_ms - elapsed;
        transport_options.cancel_event = request->cancel;
        reader->transport = PTls_TransportOpenEx(host, port, &transport_options, NULL, &error);
        if (reader->transport == NULL) { rc = stream_transport_error(request, error); break; }
        wire = (char*)malloc(12288);
        rc = PHTTP_STREAM_MEMORY;
        if (wire == NULL) { break; }
        bytes = _snprintf(wire, 12288, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n"
                "Accept-Encoding: identity\r\nUser-Agent: Positron/stream\r\n%s\r\n",
                path, host, port, request->request_headers);
        rc = PHTTP_STREAM_LIMIT;
        if (bytes < 0 || bytes >= 12288) { break; }
        written = 0;
        while (written < bytes) {
            error = PTls_TransportWrite(reader->transport, wire + written, bytes - written);
            if (error <= 0) { rc = stream_transport_error(request, error); break; }
            written += error;
        }
        free(wire); wire = NULL;
        if (written != bytes) { break; }
        reader->position = reader->length = 0;
        rc = stream_headers(reader, &chunked, location);
        if (rc != PHTTP_STREAM_OK) { break; }
        if ((request->result.status_code == 301 || request->result.status_code == 302 ||
                request->result.status_code == 303 || request->result.status_code == 307 ||
                request->result.status_code == 308) && location[0] != '\0') {
            rc = PHTTP_STREAM_PROTOCOL;
            if (request->result.redirect_hop == 5 ||
                    PHttp_ResolveReferenceUrl(request->result.final_url, location, next, sizeof(next)) != 0 ||
                    phttp_stream_url_parts(next, next_host, next_path, &next_port, &next_scheme) != 0 ||
                    (scheme == PHTTP_SCHEME_HTTPS && next_scheme != scheme)) { break; }
            if (request->sensitive_headers && (scheme != next_scheme || port != next_port ||
                    !stream_equal(host, next_host))) { rc = PHTTP_STREAM_UNSUPPORTED; break; }
            PTls_TransportClose(reader->transport); reader->transport = NULL;
            request->result.redirect_hop++;
            strcpy(request->result.final_url, next);
            request->result.status_code = 0; request->result.total = -1;
            request->response_header_bytes = 0;
            continue;
        }
        if (probe_only && (request->result.status_code < 200 || request->result.status_code >= 300)) {
            rc = stream_identity(request->response_headers, etag, modified, &identity_flags);
            if (rc == PHTTP_STREAM_OK) rc = PHTTP_PROBE_HTTP_STATUS;
            break; /* error-page encoding is not a local file/sink failure */
        }
        rc = PHTTP_STREAM_UNSUPPORTED;
        if (request->result.content_encoding[0] != '\0' &&
                !stream_equal(request->result.content_encoding, "identity")) { break; }
        rc = PHTTP_STREAM_LIMIT;
        if (!probe_only && request->result.status_code != 204 && request->result.status_code != 304 &&
                request->result.total > request->options.body_limit) { break; }
        rc = PHTTP_STREAM_CANCELLED;
        if (stream_cancelled(request)) { break; }
        if (probe_only) {
            rc = stream_identity(request->response_headers, etag, modified, &identity_flags);
            if (rc == PHTTP_STREAM_OK && (request->result.status_code < 200 ||
                    request->result.status_code >= 300)) rc = PHTTP_PROBE_HTTP_STATUS;
            break; /* discard bounded read-ahead, never parse/deliver body */
        }
        if (request->options.headers != NULL && request->options.headers(request->options.user_data,
                &request->result, request->response_headers, request->response_header_bytes) != 0) { rc = PHTTP_STREAM_SINK; break; }
        if (request->result.status_code == 204 || request->result.status_code == 304) { rc = PHTTP_STREAM_OK; }
        else if (chunked) { rc = stream_chunked(reader); }
        else { rc = stream_body(reader, request->result.total, request->result.total < 0); }
        break;
    }
done:
    free(wire);
    if (reader != NULL) { PTls_TransportClose(reader->transport); free(reader); }
    EnterCriticalSection(&request->lock);
    if (stream_cancelled(request)) { rc = PHTTP_STREAM_CANCELLED; }
    else if ((rc == PHTTP_STREAM_OK || (probe_only && rc == PHTTP_PROBE_HTTP_STATUS)) &&
            (DWORD)(GetTickCount() - started) >= request->options.timeout_ms) { rc = PHTTP_STREAM_TIMEOUT; }
    request->result.result = rc; request->state = 2;
    LeaveCriticalSection(&request->lock);
    return rc;
}
PHTTP_API int PHttp_StreamCreateGet(const char* url, const char** headers,
        const PHttpStreamOptions* options, HANDLE* out_request)
{ return stream_create_get(url, headers, options, out_request, 0); }
PHTTP_API int PHttp_StreamGet(HANDLE handle)
{ return stream_execute(handle, 0); }
static int stream_cancel(HANDLE handle, int probe_only)
{
    stream_request* request;
    int rc;
    request = (stream_request*)handle;
    if (request == NULL) { return PHTTP_STREAM_ARGUMENT; }
    EnterCriticalSection(&request->lock);
    if (request->probe_only != probe_only) {
        LeaveCriticalSection(&request->lock); return PHTTP_STREAM_STATE;
    }
    rc = request->state == 2 ? PHTTP_STREAM_STATE : PHTTP_STREAM_OK;
    if (rc == 0) { SetEvent(request->cancel); }
    LeaveCriticalSection(&request->lock);
    return rc;
}
PHTTP_API int PHttp_StreamCancel(HANDLE handle)
{ return stream_cancel(handle, 0); }
PHTTP_API int PHttp_StreamGetResult(HANDLE handle, PHttpStreamResult* result)
{
    stream_request* request;
    int rc;
    request = (stream_request*)handle;
    if (request == NULL || result == NULL || result->size != sizeof(*result) || result->version != PHTTP_STREAM_VERSION) { return PHTTP_STREAM_ARGUMENT; }
    EnterCriticalSection(&request->lock);
    rc = request->state == 2 && !request->probe_only ? PHTTP_STREAM_OK : PHTTP_STREAM_STATE;
    if (rc == 0) { *result = request->result; }
    LeaveCriticalSection(&request->lock);
    return rc;
}
PHTTP_API int PHttp_StreamGetHeader(HANDLE handle, const char* name,
        unsigned long occurrence, char* output, unsigned long capacity)
{
    stream_request* request;
    int rc;
    request = (stream_request*)handle;
    if (request == NULL || name == NULL || name[0] == '\0' || output == NULL || capacity == 0) { return PHTTP_STREAM_ARGUMENT; }
    EnterCriticalSection(&request->lock);
    rc = request->state == 2 && !request->probe_only && request->response_header_bytes != 0 ?
            stream_field(request->response_headers, name, occurrence, output, capacity) : PHTTP_STREAM_STATE;
    LeaveCriticalSection(&request->lock);
    return rc;
}
static int stream_close(HANDLE handle, int probe_only)
{
    stream_request* request;
    request = (stream_request*)handle;
    if (request == NULL) { return PHTTP_STREAM_OK; }
    EnterCriticalSection(&request->lock);
    if (request->state == 1 || request->probe_only != probe_only) {
        LeaveCriticalSection(&request->lock); return PHTTP_STREAM_STATE;
    }
    LeaveCriticalSection(&request->lock);
    DeleteCriticalSection(&request->lock); CloseHandle(request->cancel); free(request);
    return PHTTP_STREAM_OK;
}
PHTTP_API int PHttp_StreamClose(HANDLE handle)
{ return stream_close(handle, 0); }
PHTTP_API int PHttp_ProbeCreateGet(const char* url, const char** headers,
        const PHttpProbeOptions* options, HANDLE* out_request)
{
    PHttpStreamOptions stream;
    if (options == NULL || options->size != sizeof(*options) ||
            options->version != PHTTP_PROBE_VERSION) return PHTTP_STREAM_ARGUMENT;
    memset(&stream, 0, sizeof(stream)); stream.size = sizeof(stream);
    stream.version = PHTTP_STREAM_VERSION; stream.timeout_ms = options->timeout_ms;
    stream.body_limit = 1; /* unused by header-only execution */
    return stream_create_get(url, headers, &stream, out_request, 1);
}
PHTTP_API int PHttp_ProbeGet(HANDLE handle)
{ return stream_execute(handle, 1); }
PHTTP_API int PHttp_ProbeCancel(HANDLE handle)
{ return stream_cancel(handle, 1); }
PHTTP_API int PHttp_ProbeClose(HANDLE* handle)
{
    int rc;
    if (handle == NULL) return PHTTP_STREAM_ARGUMENT;
    rc = stream_close(*handle, 1);
    if (rc == PHTTP_STREAM_OK) *handle = NULL;
    return rc;
}
PHTTP_API int PHttp_ProbeGetResult(HANDLE handle, PHttpProbeResult* result)
{
    stream_request* request;
    PHttpProbeResult next;
    int rc;
    request = (stream_request*)handle;
    if (request == NULL || result == NULL || result->size != sizeof(*result) ||
            result->version != PHTTP_PROBE_VERSION) return PHTTP_STREAM_ARGUMENT;
    memset(&next, 0, sizeof(next)); next.size = sizeof(next); next.version = PHTTP_PROBE_VERSION;
    next.name.size = sizeof(next.name); next.name.version = PHTTP_DOWNLOAD_NAME_VERSION;
    next.name.status = PHTTP_NAME_MISSING;
    EnterCriticalSection(&request->lock);
    rc = request->state == 2 && request->probe_only ? PHTTP_STREAM_OK : PHTTP_STREAM_STATE;
    if (rc == PHTTP_STREAM_OK) {
        next.metadata = request->result;
        if (next.metadata.result == PHTTP_STREAM_OK) {
            rc = stream_identity(request->response_headers, next.etag, next.last_modified, &next.flags);
            if (rc == PHTTP_STREAM_OK) {
                rc = PHttp_SuggestDownloadName(next.metadata.content_disposition,
                        next.metadata.final_url, &next.name);
            }
        }
        if (rc == PHTTP_STREAM_OK) *result = next;
    }
    LeaveCriticalSection(&request->lock);
    return rc;
}
/* Validate the bounded borrowed block without relying on a caller-supplied
 * NUL terminator. Used only by the additive matching query, not old GET. */
static int probe_header_block(const char* block, unsigned long bytes, int status)
{
    const char* p;
    const char* end;
    unsigned long fields;
    size_t colon;
    size_t length;
    size_t i;
    char line[1024];
    p = block; fields = 0;
    end = strstr(p, "\r\n");
    if (end == NULL || end - p < 12 || end - p > 1023 ||
            (!stream_ci(p, "HTTP/1.1 ", 9) && !stream_ci(p, "HTTP/1.0 ", 9)) ||
            p[9] != '0' + status / 100 || p[10] != '0' + (status / 10) % 10 ||
            p[11] != '0' + status % 10 || (p[12] != '\r' && p[12] != ' ')) {
        return PHTTP_STREAM_PROTOCOL;
    }
    while ((end = strstr(p, "\r\n")) != NULL) {
        length = (size_t)(end - p);
        if (length > 1023) return PHTTP_STREAM_LIMIT;
        for (i = 0; i < length; i++) {
            if ((unsigned char)p[i] == 127 || ((unsigned char)p[i] < 32 && p[i] != '\t')) {
                return PHTTP_STREAM_PROTOCOL;
            }
        }
        if (!phttp_text_utf8(p, length)) return PHTTP_STREAM_UNSUPPORTED;
        if (length == 0) return end + 2 == block + bytes ? PHTTP_STREAM_OK : PHTTP_STREAM_PROTOCOL;
        if (p != block) {
            if (++fields > PHTTP_STREAM_MAX_HEADER_FIELDS) return PHTTP_STREAM_LIMIT;
            memcpy(line, p, length); line[length] = '\0';
            if (!stream_header_name(line, &colon)) return PHTTP_STREAM_PROTOCOL;
        }
        p = end + 2;
    }
    return PHTTP_STREAM_PROTOCOL;
}
static int probe_metadata_valid(const PHttpStreamResult* metadata)
{
    return metadata->size == sizeof(*metadata) && metadata->version == PHTTP_STREAM_VERSION &&
            metadata->received == 0 && metadata->total >= -1 &&
            metadata->status_code >= 100 && metadata->status_code <= 599 &&
            metadata->final_url[0] != '\0' &&
            memchr(metadata->final_url, '\0', sizeof(metadata->final_url)) != NULL &&
            memchr(metadata->content_type, '\0', sizeof(metadata->content_type)) != NULL &&
            memchr(metadata->content_disposition, '\0', sizeof(metadata->content_disposition)) != NULL &&
            memchr(metadata->content_encoding, '\0', sizeof(metadata->content_encoding)) != NULL;
}
PHTTP_API int PHttp_ProbeMatchFinalHeaders(const PHttpProbeResult* probe,
        const PHttpStreamResult* metadata, const char* headers, unsigned long header_bytes)
{
    char* copy;
    char etag[256];
    char modified[128];
    unsigned long flags;
    int rc;
    if (probe == NULL || metadata == NULL || headers == NULL || header_bytes < 16 ||
            probe->size != sizeof(*probe) || probe->version != PHTTP_PROBE_VERSION ||
            !probe_metadata_valid(&probe->metadata) || probe->metadata.result != PHTTP_STREAM_OK ||
            !probe_metadata_valid(metadata) || memchr(probe->etag, '\0', sizeof(probe->etag)) == NULL ||
            memchr(probe->last_modified, '\0', sizeof(probe->last_modified)) == NULL) {
        return PHTTP_STREAM_ARGUMENT;
    }
    if (header_bytes > PHTTP_STREAM_HEADERS_MAX_BYTES) return PHTTP_STREAM_LIMIT;
    if (memchr(headers, '\0', header_bytes) != NULL) return PHTTP_STREAM_PROTOCOL;
    copy = (char*)malloc(header_bytes + 1);
    if (copy == NULL) return PHTTP_STREAM_MEMORY;
    memcpy(copy, headers, header_bytes); copy[header_bytes] = '\0';
    rc = probe_header_block(copy, header_bytes, metadata->status_code);
    if (rc == PHTTP_STREAM_OK) rc = stream_identity(copy, etag, modified, &flags);
    free(copy);
    if (rc != PHTTP_STREAM_OK) return rc;
    if (metadata->status_code < 200 || metadata->status_code >= 300) return PHTTP_PROBE_HTTP_STATUS;
    if (strcmp(probe->metadata.final_url, metadata->final_url) ||
            probe->metadata.status_code != metadata->status_code ||
            probe->metadata.total != metadata->total ||
            strcmp(probe->metadata.content_type, metadata->content_type) ||
            strcmp(probe->metadata.content_disposition, metadata->content_disposition) ||
            strcmp(probe->metadata.content_encoding, metadata->content_encoding) ||
            strcmp(probe->etag, etag) || strcmp(probe->last_modified, modified) || probe->flags != flags) {
        return PHTTP_PROBE_CHANGED;
    }
    return PHTTP_STREAM_OK;
}

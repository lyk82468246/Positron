/* Public HTTP consumer only: deterministic loopback, threads and assertions. */
#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <string.h>
#include "positron_http.h"

static char probe_error[256];
typedef struct probe_fixture {
    SOCKET listener;
    HANDLE server;
    HANDLE accepted;
    HANDLE release;
    HANDLE request;
    int mode;
    int execute_result;
    int server_error;
    int headers_seen;
    int match_result;
    int real_stream;
    unsigned long bytes;
    unsigned long expected;
    unsigned short port;
    const PHttpProbeResult* snapshot;
} probe_fixture;
#define PROBE_CHECK(test, label) do { if (!(test)) { \
    _snprintf(probe_error, sizeof(probe_error)-1, "%s (line %d, mode %d)", \
            label, __LINE__, fixture.mode); goto done; } } while (0)

static int probe_send(SOCKET peer, const char* text, int bytes)
{
    int n;
    while (bytes > 0) {
        n = send(peer, text, bytes, 0);
        if (n <= 0) return 0;
        bytes -= n; text += n;
    }
    return 1;
}
static int probe_send_text(SOCKET peer, const char* text)
{ return probe_send(peer, text, (int)strlen(text)); }
static int probe_read_request(SOCKET peer, char* request, int capacity)
{
    fd_set read_set;
    struct timeval wait;
    DWORD started;
    int used;
    int n;
    started = GetTickCount(); used = 0; request[0] = '\0';
    while ((DWORD)(GetTickCount() - started) < 5000 && used < capacity - 1) {
        FD_ZERO(&read_set); FD_SET(peer, &read_set);
        wait.tv_sec = 0; wait.tv_usec = 100000;
        n = select(0, &read_set, NULL, NULL, &wait);
        if (n < 0) return 0;
        if (n == 0) continue;
        n = recv(peer, request + used, capacity - used - 1, 0);
        if (n <= 0) return 0;
        used += n; request[used] = '\0';
        if (strstr(request, "\r\n\r\n") != NULL) return 1;
    }
    return 0;
}
static DWORD WINAPI probe_server(LPVOID data)
{
    probe_fixture* fixture;
    SOCKET peer;
    char request[12288];
    char header[1536];
    char block[4096];
    const char* disposition;
    const char* etag;
    const char* length;
    const char* encoding;
    int status;
    int hop;
    int hops;
    int n;
    unsigned long sent;
    unsigned long i;
    fixture = (probe_fixture*)data;
    hops = fixture->mode == 3 ? 2 : fixture->mode == 22 ? 6 :
            fixture->mode >= 31 && fixture->mode <= 33 ? 2 : 1;
    for (hop = 0; hop < hops; hop++) {
        peer = accept(fixture->listener, NULL, NULL);
        if (peer == INVALID_SOCKET) return 0;
        SetEvent(fixture->accepted);
        if (fixture->mode == 12 || fixture->mode == 13) {
            WaitForSingleObject(fixture->release, 10000); closesocket(peer); return 0;
        }
        if (!probe_read_request(peer, request, sizeof(request)) ||
                strncmp(request, "GET /", 5) != 0 ||
                strstr(request, "Accept-Encoding: identity") == NULL) {
            fixture->server_error = 1; closesocket(peer); return 0;
        }
        if ((fixture->mode == 3 && hop == 0) || fixture->mode == 22) {
            if (!probe_send_text(peer, "HTTP/1.1 302 Found\r\nLocation: /final.bin?x=1#gone\r\n"
                    "Content-Disposition: attachment; filename=intermediate.bin\r\nContent-Length: 3\r\n\r\nbad")) {
                fixture->server_error = 2;
            }
            closesocket(peer); continue;
        }
        if (fixture->mode == 23) {
            _snprintf(header, sizeof(header), "HTTP/1.1 302 Found\r\n"
                    "Location: http://localhost:%u/final.bin\r\nContent-Length: 0\r\n\r\n", fixture->port);
            probe_send_text(peer, header); closesocket(peer); return 0;
        }
        disposition = "Content-Disposition: attachment; filename*=UTF-8''%E4%B8%AD%E6%96%87.bin\r\n";
        etag = "ETag: \"fixture-v1\"\r\n";
        length = "Content-Length: 5\r\n"; encoding = ""; status = 200;
        if (fixture->mode == 2) { status = 404; encoding = "Content-Encoding: gzip\r\n"; }
        if (fixture->mode == 4) length = "Transfer-Encoding: chunked\r\n";
        if (fixture->mode == 5) status = 404;
        if (fixture->mode == 6) disposition = "";
        if (fixture->mode == 7) disposition = "Content-Disposition: attachment; filename=\"a;b.txt\"\r\n";
        if (fixture->mode == 8) disposition = "Content-Disposition: attachment; filename*=UTF-8''..%2Fbad.bin\r\n";
        if (fixture->mode == 9) etag = "ETag: \"a\"\r\nETag: \"b\"\r\n";
        if (fixture->mode == 10) etag = "ETag: \"a b\"\r\n";
        if (fixture->mode == 11) encoding = "Content-Encoding: gzip\r\n";
        if (fixture->mode == 14) length = "Content-Length: 10000000000\r\n";
        if (fixture->mode == 15) length = "Content-Length: 5\r\nTransfer-Encoding: chunked\r\n";
        if (fixture->mode == 16) disposition = "Content-Disposition: attachment; filename=a; FILENAME=b\r\n";
        if (fixture->mode == 17) { etag = "ETag: W/\"fixture-v1\"\r\n"; length = ""; }
        if (fixture->mode == 18) {
            memset(block, 'x', 256); block[256] = '\0';
            _snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nETag: \"%s\"\r\n\r\n", block);
            probe_send_text(peer, header); closesocket(peer); return 0;
        }
        if (fixture->mode == 19) disposition = "Content-Disposition: attachment; filename=\"\xff\"\r\n";
        if (fixture->mode == 20) status = 204;
        if (fixture->mode == 21) status = 206;
        if (fixture->mode >= 31 && fixture->mode <= 33) {
            length = "Content-Length: 2097152\r\n";
            if (fixture->mode == 32 && hop == 1) etag = "ETag: \"fixture-v2\"\r\n";
            if (fixture->mode == 33 && hop == 1) status = 404;
        }
        n = _snprintf(header, sizeof(header), "HTTP/1.1 %d Fixture\r\n%s%s%s%s"
                "Content-Type: application/octet-stream\r\n"
                "Last-Modified: Wed, 01 Jan 2020 00:00:00 GMT\r\n\r\n",
                status, length, disposition, etag, encoding);
        if (n <= 0 || n >= sizeof(header) || !probe_send(peer, header, n)) {
            fixture->server_error = 3; closesocket(peer); return 0;
        }
        /* Withhold all body bytes until preflight has returned and the test
         * explicitly releases the server. Closing before EOF is normal. */
        if (hop == 0 || fixture->mode == 3) WaitForSingleObject(fixture->release, 10000);
        if (fixture->mode >= 31 && fixture->mode <= 33 && hop == 1) {
            sent = 0;
            while (sent < fixture->expected) {
                n = fixture->expected - sent > sizeof(block) ? sizeof(block) : (int)(fixture->expected - sent);
                for (i = 0; i < (unsigned long)n; i++) block[i] = (char)((sent + i) % 251);
                if (!probe_send(peer, block, n)) break;
                sent += n;
            }
        }
        closesocket(peer);
    }
    return 0;
}
static int probe_start(probe_fixture* fixture, int mode)
{
    struct sockaddr_in address;
    int bytes;
    memset(fixture, 0, sizeof(*fixture)); fixture->listener = INVALID_SOCKET;
    fixture->mode = mode; fixture->expected = 2UL * 1024 * 1024;
    fixture->accepted = CreateEvent(NULL, TRUE, FALSE, NULL);
    fixture->release = CreateEvent(NULL, TRUE, FALSE, NULL);
    fixture->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fixture->accepted == NULL || fixture->release == NULL || fixture->listener == INVALID_SOCKET) return 0;
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1"); bytes = sizeof(address);
    if (bind(fixture->listener, (struct sockaddr*)&address, sizeof(address)) != 0 ||
            getsockname(fixture->listener, (struct sockaddr*)&address, &bytes) != 0 ||
            listen(fixture->listener, 2) != 0) return 0;
    fixture->port = ntohs(address.sin_port);
    fixture->server = CreateThread(NULL, 0, probe_server, fixture, 0, NULL);
    return fixture->server != NULL;
}
static void probe_stop(probe_fixture* fixture)
{
    if (fixture->release != NULL) SetEvent(fixture->release);
    if (fixture->listener != INVALID_SOCKET) {
        closesocket(fixture->listener); fixture->listener = INVALID_SOCKET;
    }
    if (fixture->server != NULL) {
        WaitForSingleObject(fixture->server, INFINITE); CloseHandle(fixture->server);
    }
    if (fixture->real_stream && fixture->request != NULL) {
        PHttp_StreamClose(fixture->request); fixture->request = NULL;
    } else PHttp_ProbeClose(&fixture->request);
    if (fixture->accepted != NULL) CloseHandle(fixture->accepted);
    if (fixture->release != NULL) CloseHandle(fixture->release);
}
static DWORD WINAPI probe_execute(LPVOID data)
{
    probe_fixture* fixture;
    fixture = (probe_fixture*)data;
    fixture->execute_result = PHttp_ProbeGet(fixture->request);
    return 0;
}
static int probe_match_guards(const PHttpProbeResult* snapshot,
        const PHttpStreamResult* metadata, const char* headers, unsigned long bytes)
{
    PHttpProbeResult changed;
    PHttpStreamResult bad;
    int i;
    int rc;
    for (i = 0; i < 8; i++) {
        changed = *snapshot;
        if (i == 0) strcpy(changed.metadata.final_url, "http://127.0.0.1/different");
        if (i == 1) changed.metadata.status_code = 206;
        if (i == 2) changed.metadata.total++;
        if (i == 3) strcpy(changed.metadata.content_type, "text/plain");
        if (i == 4) strcpy(changed.metadata.content_disposition, "attachment; filename=other.bin");
        if (i == 5) strcpy(changed.metadata.content_encoding, "identity");
        if (i == 6) strcpy(changed.etag, "\"another\"");
        if (i == 7) strcpy(changed.last_modified, "Thu, 02 Jan 2020 00:00:00 GMT");
        rc = PHttp_ProbeMatchFinalHeaders(&changed, metadata, headers, bytes);
        if (rc != PHTTP_PROBE_CHANGED) return 0;
    }
    bad = *metadata; bad.received = 1;
    if (PHttp_ProbeMatchFinalHeaders(snapshot, &bad, headers, bytes) != PHTTP_STREAM_ARGUMENT ||
            PHttp_ProbeMatchFinalHeaders(snapshot, metadata, headers, 15) != PHTTP_STREAM_ARGUMENT ||
            PHttp_ProbeMatchFinalHeaders(snapshot, metadata, headers,
            PHTTP_STREAM_HEADERS_MAX_BYTES + 1) != PHTTP_STREAM_LIMIT) return 0;
    return 1;
}
static int probe_checked_headers(void* data, const PHttpStreamResult* metadata,
        const char* headers, unsigned long bytes)
{
    probe_fixture* fixture;
    fixture = (probe_fixture*)data; fixture->headers_seen++;
    fixture->match_result = PHttp_ProbeMatchFinalHeaders(fixture->snapshot, metadata, headers, bytes);
    if (fixture->mode == 31 && !probe_match_guards(fixture->snapshot, metadata, headers, bytes)) {
        fixture->server_error = 4; return 1;
    }
    return fixture->match_result != PHTTP_STREAM_OK;
}
static int probe_sink(void* data, const unsigned char* block, unsigned long bytes,
        __int64 received, __int64 total)
{
    probe_fixture* fixture;
    unsigned long i;
    fixture = (probe_fixture*)data;
    if (bytes == 0 || bytes > 4096 || total != fixture->expected ||
            received != (__int64)fixture->bytes + bytes) return 1;
    for (i = 0; i < bytes; i++) if (block[i] != (fixture->bytes + i) % 251) return 1;
    fixture->bytes += bytes;
    return 0;
}
typedef struct name_case {
    const char* header;
    const char* url;
    int status;
    int source;
    int fallback;
    const char* name;
} name_case;
static int probe_names(void)
{
    static const name_case cases[] = {
        {NULL, "https://host/?name=fake#ignored", PHTTP_NAME_MISSING, 0, 0, ""},
        {NULL, "https://host/dir/file.bin?name=fake#ignored", 0, 3, 0, "file.bin"},
        {NULL, "https://host/dir/%E4%B8%AD%E6%96%87.bin?x=1", 0, 3, 0, "\xe4\xb8\xad\xe6\x96\x87.bin"},
        {"attachment", "https://host/file.bin", 0, 3, 0, "file.bin"},
        {"attachment; filename=a.bin", NULL, 0, 2, 0, "a.bin"},
        {"inline; filename=\"a;b.txt\"", NULL, 0, 2, 0, "a;b.txt"},
        {"attachment; filename=\"a\\\"b.txt\"", NULL, 0, 2, 0, "a\"b.txt"},
        {"attachment; filename=\"literal%2F+.bin\"", NULL, 0, 2, 0, "literal%2F+.bin"},
        {"attachment; filename=old.bin; filename*=UTF-8'zh'%E4%B8%AD%E6%96%87.bin", NULL, 0, 1, 0, "\xe4\xb8\xad\xe6\x96\x87.bin"},
        {"attachment; filename*=utf-8''a+b.bin", NULL, 0, 1, 0, "a+b.bin"},
        {"attachment; filename*=ISO-8859-1''%A3.bin; filename=safe.bin", NULL, 0, 2, 3, "safe.bin"},
        {"attachment; filename*=UTF-8''bad%GG; filename=safe.bin", NULL, 0, 2, 2, "safe.bin"},
        {"attachment; filename*=UTF-8''bad%GG", "https://host/safe.bin", 2, 0, 2, ""},
        {"attachment; filename*=ISO-8859-1''%A3.bin", "https://host/safe.bin", 3, 0, 3, ""},
        {"attachment; filename=a; FILENAME=b", NULL, 2, 0, 0, ""},
        {"attachment; foo=a; FOO=b", NULL, 2, 0, 0, ""},
        {"attachment; filename=\"unterminated", NULL, 2, 0, 0, ""},
        {"attachment; filename=\"\"", "https://host/safe.bin", 1, 0, 0, ""},
        {"attachment; filename*=UTF-8''..%2Fbad", NULL, 2, 0, 2, ""},
        {"attachment; filename*=UTF-8''a%5Cb", NULL, 2, 0, 2, ""},
        {"attachment; filename*=UTF-8''C%3Afile", NULL, 2, 0, 2, ""},
        {"attachment; filename*=UTF-8''%00.bin", NULL, 2, 0, 2, ""},
        {"attachment; filename*=UTF-8''%0A.bin", NULL, 2, 0, 2, ""},
        {"attachment; filename*=UTF-8''%C2%85.bin", NULL, 2, 0, 2, ""},
        {"attachment; filename*=UTF-8''%C0%AF.bin", NULL, 2, 0, 2, ""},
        {"attachment; filename*=UTF-8''%ED%A0%80.bin", NULL, 2, 0, 2, ""},
        {"attachment; filename*=UTF-8''%2E%2E", NULL, 2, 0, 2, ""},
        {"attachment; filename*=\"UTF-8''a.bin\"", NULL, 2, 0, 2, ""},
        {"attachment; filename=\" a.bin\"", NULL, 2, 0, 0, ""},
        {"attachment; FILENAME*0*=UTF-8''a", NULL, 3, 0, 0, ""},
        {"attachment; filename=\"=?UTF-8?B?YQ==?=\"", NULL, 3, 0, 0, ""},
        {"attachment; filename=\"\xff\"", NULL, 2, 0, 0, ""},
        {NULL, "https://host/a%2fb.bin", 2, 0, 0, ""},
        {NULL, "https://host/%2e%2e", 2, 0, 0, ""},
        {NULL, "https://host/bad%", 2, 0, 0, ""},
        {NULL, "ftp://host/a.bin", 2, 0, 0, ""}
    };
    PHttpDownloadNameInfo info;
    PHttpDownloadNameInfo unchanged;
    char large[1024];
    unsigned int i;
    size_t prefix;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        memset(&info, 0, sizeof(info)); info.size = sizeof(info); info.version = PHTTP_DOWNLOAD_NAME_VERSION;
        if (PHttp_SuggestDownloadName(cases[i].header, cases[i].url, &info) != 0 ||
                info.status != cases[i].status || info.source != cases[i].source ||
                info.disposition_status != cases[i].fallback || strcmp(info.basename, cases[i].name)) {
            _snprintf(probe_error, sizeof(probe_error)-1, "name case=%u status=%d source=%d fallback=%d name=%s",
                    i, info.status, info.source, info.disposition_status, info.basename);
            return 0;
        }
    }
    memset(&info, 123, sizeof(info)); info.size = sizeof(info)-1; info.version = PHTTP_DOWNLOAD_NAME_VERSION;
    unchanged = info;
    if (PHttp_SuggestDownloadName(NULL, NULL, &info) != PHTTP_STREAM_ARGUMENT || memcmp(&info, &unchanged, sizeof(info))) return 0;
    memset(&info, 0, sizeof(info)); info.size = sizeof(info); info.version = PHTTP_DOWNLOAD_NAME_VERSION;
    memset(large, 'a', 512); large[512] = '\0';
    if (PHttp_SuggestDownloadName(large, NULL, &info) != 0 || info.status != PHTTP_NAME_LIMIT || info.basename[0]) return 0;
    strcpy(large, "attachment; filename=\""); prefix = strlen(large);
    memset(large + prefix, 'a', 255); large[prefix + 255] = '"'; large[prefix + 256] = '\0';
    if (PHttp_SuggestDownloadName(large, NULL, &info) != 0 || info.status != 0 || strlen(info.basename) != 255) return 0;
    large[prefix + 255] = 'a'; large[prefix + 256] = '"'; large[prefix + 257] = '\0';
    if (PHttp_SuggestDownloadName(large, NULL, &info) != 0 || info.status != PHTTP_NAME_LIMIT || info.basename[0]) return 0;
    strcpy(large, "attachment");
    for (i = 0; i < 33; i++) sprintf(large + strlen(large), "; p%u=x", i);
    if (PHttp_SuggestDownloadName(large, NULL, &info) != 0 || info.status != PHTTP_NAME_LIMIT || info.basename[0]) return 0;
    strcpy(large, "attachment; "); prefix = strlen(large);
    memset(large + prefix, 'p', 64); strcpy(large + prefix + 64, "=x");
    if (PHttp_SuggestDownloadName(large, NULL, &info) != 0 || info.status != PHTTP_NAME_LIMIT || info.basename[0]) return 0;
    return 1;
}
BOOL test1350_http_probe_contract(void)
{
    static const char* bad_urls[] = {"ftp://host/file", "file:///file", "http://u@host/",
        "http://[::1]/", "http://host:0/", "http://host:65536/", "http://host/\r\ninject"};
    const char* authorization[] = {"Authorization: Bearer fixture-public", NULL};
    probe_fixture fixture;
    PHttpProbeOptions options;
    PHttpProbeResult result;
    PHttpProbeResult unchanged_result;
    PHttpStreamOptions stream;
    HANDLE worker;
    HANDLE unchanged_handle;
    char url[256];
    int rc;
    int expected;
    int ok;
    int mode;
    unsigned int i;
    DWORD started;
    memset(&fixture, 0, sizeof(fixture)); fixture.listener = INVALID_SOCKET;
    worker = NULL; ok = 0; probe_error[0] = '\0';
    if (!probe_names()) {
        if (!probe_error[0]) strcpy(probe_error, "naming budget/output guard failed");
        goto done;
    }
    PROBE_CHECK(PHttp_Init(), "HTTP init");
    memset(&options, 0, sizeof(options)); options.size = sizeof(options);
    options.version = PHTTP_PROBE_VERSION; options.timeout_ms = 5000;
    unchanged_handle = (HANDLE)123;
    for (i = 0; i < sizeof(bad_urls)/sizeof(bad_urls[0]); i++) {
        PROBE_CHECK(PHttp_ProbeCreateGet(bad_urls[i], NULL, &options, &unchanged_handle) == PHTTP_STREAM_ARGUMENT &&
                unchanged_handle == (HANDLE)123, "invalid URL preserves handle");
    }
    for (mode = 1; mode <= 23; mode++) {
        PROBE_CHECK(probe_start(&fixture, mode), "loopback start");
        options.timeout_ms = mode == 12 ? 100 : 5000;
        _snprintf(url, sizeof(url), "%s://127.0.0.1:%u/%s", mode == 13 ? "https" : "http",
                fixture.port, mode == 6 ? "?name=fake" : "file.bin");
        PROBE_CHECK(PHttp_ProbeCreateGet(url, mode == 23 ? authorization : NULL, &options, &fixture.request) == 0,
                "create probe");
        memset(&result, 0, sizeof(result)); result.size = sizeof(result); result.version = PHTTP_PROBE_VERSION;
        result.metadata.received = 99; unchanged_result = result;
        PROBE_CHECK(PHttp_ProbeGetResult(fixture.request, &result) == PHTTP_STREAM_STATE &&
                memcmp(&result, &unchanged_result, sizeof(result)) == 0 &&
                PHttp_StreamGet(fixture.request) == PHTTP_STREAM_STATE, "created query/wrong family fail closed");
        started = GetTickCount();
        if (mode == 13) {
            worker = CreateThread(NULL, 0, probe_execute, &fixture, 0, NULL);
            PROBE_CHECK(worker != NULL && WaitForSingleObject(fixture.accepted, 5000) == WAIT_OBJECT_0,
                    "HTTPS reaches controlled handshake");
            PROBE_CHECK(PHttp_ProbeClose(&fixture.request) == PHTTP_STREAM_STATE && fixture.request != NULL &&
                    PHttp_ProbeGetResult(fixture.request, &result) == PHTTP_STREAM_STATE &&
                    memcmp(&result, &unchanged_result, sizeof(result)) == 0, "running query/close refuse without freeing");
            PROBE_CHECK(PHttp_ProbeCancel(fixture.request) == 0, "cancel during TLS wait");
            rc = PHttp_ProbeCancel(fixture.request);
            PROBE_CHECK((rc == 0 || rc == PHTTP_STREAM_STATE) && WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0,
                    "repeat cancellation and bounded return");
            CloseHandle(worker); worker = NULL; rc = fixture.execute_result;
        } else rc = PHttp_ProbeGet(fixture.request);
        expected = mode == 2 || mode == 5 ? PHTTP_PROBE_HTTP_STATUS : mode == 9 || mode == 10 || mode == 15 || mode == 22 ?
                PHTTP_STREAM_PROTOCOL : mode == 11 || mode == 19 || mode == 23 ? PHTTP_STREAM_UNSUPPORTED :
                mode == 12 ? PHTTP_STREAM_TIMEOUT : mode == 13 ? PHTTP_STREAM_CANCELLED :
                mode == 18 ? PHTTP_STREAM_LIMIT : PHTTP_STREAM_OK;
        if (rc != expected) {
            _snprintf(probe_error, sizeof(probe_error)-1, "mode=%d rc=%d expected=%d", mode, rc, expected); goto done;
        }
        PROBE_CHECK((DWORD)(GetTickCount() - started) < 5000 && PHttp_ProbeGetResult(fixture.request, &result) == 0 &&
                result.metadata.result == rc && result.metadata.received == 0 && fixture.bytes == 0 &&
                fixture.headers_seen == 0 && PHttp_ProbeGet(fixture.request) == PHTTP_STREAM_STATE &&
                PHttp_ProbeCancel(fixture.request) == PHTTP_STREAM_STATE, "preflight completes without EOF/body/sink and is one-shot");
        if (rc == 0) {
            PROBE_CHECK(fixture.server_error == 0 && result.metadata.status_code == (mode == 20 ? 204 : mode == 21 ? 206 : 200),
                    "HTTP status is metadata, not download authorization");
            if (mode == 4 || mode == 17) PROBE_CHECK(result.metadata.total == -1, "unknown chunked/close-delimited size");
            if (mode == 14) PROBE_CHECK(result.metadata.total == 10000000000i64, "64-bit length does not consume body quota");
            if (mode == 6) PROBE_CHECK(result.name.status == PHTTP_NAME_MISSING && result.name.basename[0] == '\0', "no invented filename");
            if (mode == 8 || mode == 16) PROBE_CHECK(result.name.status == PHTTP_NAME_INVALID && !result.name.basename[0], "unsafe/malformed names have no output");
            if (mode == 3) PROBE_CHECK(result.metadata.redirect_hop == 1 && strstr(result.metadata.final_url, "/final.bin?x=1") &&
                    strchr(result.metadata.final_url, '#') == NULL && result.name.source == PHTTP_NAME_SOURCE_FILENAME_STAR,
                    "final redirect metadata and naming only");
            if (mode == 17) PROBE_CHECK(result.flags == 0, "weak ETag is not strong identity");
        } else PROBE_CHECK(result.name.status != PHTTP_NAME_OK && !result.name.basename[0], "failed probe has no usable name");
        SetEvent(fixture.release);
        PROBE_CHECK(PHttp_ProbeClose(&fixture.request) == 0 && fixture.request == NULL &&
                PHttp_ProbeClose(&fixture.request) == 0, "repeat slot release safe");
        probe_stop(&fixture); memset(&fixture, 0, sizeof(fixture)); fixture.listener = INVALID_SOCKET;
    }
    for (mode = 31; mode <= 33; mode++) {
        PROBE_CHECK(probe_start(&fixture, mode), "two GET matching fixture");
        _snprintf(url, sizeof(url), "http://127.0.0.1:%u/file.bin", fixture.port); options.timeout_ms = 5000;
        PROBE_CHECK(PHttp_ProbeCreateGet(url, NULL, &options, &fixture.request) == 0 && PHttp_ProbeGet(fixture.request) == 0,
                "preflight before user confirmation");
        memset(&result, 0, sizeof(result)); result.size = sizeof(result); result.version = PHTTP_PROBE_VERSION;
        PROBE_CHECK(PHttp_ProbeGetResult(fixture.request, &result) == 0 && result.flags == PHTTP_PROBE_FLAG_STRONG_ETAG &&
                result.metadata.total == 2097152 && result.metadata.received == 0 &&
                PHttp_ProbeClose(&fixture.request) == 0, "copied snapshot survives close");
        SetEvent(fixture.release); fixture.snapshot = &result;
        memset(&stream, 0, sizeof(stream)); stream.size = sizeof(stream); stream.version = PHTTP_STREAM_VERSION;
        stream.timeout_ms = 20000; stream.body_limit = fixture.expected; stream.headers = probe_checked_headers;
        stream.sink = probe_sink; stream.user_data = &fixture;
        PROBE_CHECK(PHttp_StreamCreateGet(url, NULL, &stream, &fixture.request) == 0, "new real GET");
        fixture.real_stream = 1;
        rc = PHttp_StreamGet(fixture.request);
        PROBE_CHECK(fixture.server_error == 0 && fixture.headers_seen == 1 && (mode == 31 ?
                rc == 0 && fixture.match_result == 0 && fixture.bytes == fixture.expected :
                rc == PHTTP_STREAM_SINK && fixture.match_result ==
                (mode == 32 ? PHTTP_PROBE_CHANGED : PHTTP_PROBE_HTTP_STATUS) && fixture.bytes == 0),
                "exact 2MiB match or changed response rejected before any body");
        PHttp_StreamClose(fixture.request); fixture.request = NULL;
        probe_stop(&fixture); memset(&fixture, 0, sizeof(fixture)); fixture.listener = INVALID_SOCKET;
    }
    PROBE_CHECK(PHttp_ProbeCreateGet("http://127.0.0.1:1/", NULL, &options, &fixture.request) == 0 &&
            PHttp_ProbeCancel(fixture.request) == 0 && PHttp_ProbeGet(fixture.request) == PHTTP_STREAM_CANCELLED,
            "pre-start cancellation no network");
    ok = 1;
done:
    if (worker != NULL) {
        PHttp_ProbeCancel(fixture.request); SetEvent(fixture.release);
        WaitForSingleObject(worker, INFINITE); CloseHandle(worker);
    }
    if (fixture.request != NULL && fixture.real_stream) {
        PHttp_StreamClose(fixture.request); fixture.request = NULL;
    }
    probe_stop(&fixture); probe_error[sizeof(probe_error)-1] = '\0';
    return ok;
}
BOOL test1351_http_probe_verified_https(void)
{
    PHttpProbeOptions options;
    PHttpProbeResult result;
    HANDLE request;
    int rc;
    int ok;
    request = NULL; ok = 0; probe_error[0] = '\0';
    memset(&options, 0, sizeof(options)); options.size = sizeof(options);
    options.version = PHTTP_PROBE_VERSION; options.timeout_ms = 60000;
    if (!PHttp_Init() || PHttp_ProbeCreateGet("https://www.iana.org/help/example-domains", NULL,
            &options, &request) != 0) goto done;
    rc = PHttp_ProbeGet(request);
    memset(&result, 0, sizeof(result)); result.size = sizeof(result); result.version = PHTTP_PROBE_VERSION;
    if (PHttp_ProbeGetResult(request, &result) != 0 || rc != 0 || result.metadata.status_code != 200 ||
            result.metadata.received != 0 || strncmp(result.metadata.final_url, "https://www.iana.org/", 21) ||
            strncmp(result.metadata.content_type, "text/html", 9) || result.name.status != PHTTP_NAME_OK) {
        _snprintf(probe_error, sizeof(probe_error)-1, "verified probe rc=%d transport=%d status=%d name=%d",
                rc, result.metadata.transport_error, result.metadata.status_code, result.name.status);
        goto done;
    }
    ok = 1;
done:
    PHttp_ProbeClose(&request); return ok;
}
const char* test1350_http_probe_last_error(void) { return probe_error; }

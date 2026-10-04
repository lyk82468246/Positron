/* Public DLL consumer: bounded loopback server, sink, threads and assertions. */
#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#include <string.h>
#include "positron_http.h"
#include "positron_tls.h"

typedef struct stream_fixture {
    SOCKET listener;
    HANDLE server;
    HANDLE accepted;
    HANDLE release;
    HANDLE body_seen;
    HANDLE request;
    int mode;
    int error;
    int sink_error;
    int headers_seen;
    int guards;
    int execute_result;
    unsigned long bytes;
    unsigned long expected;
    unsigned short port;
} stream_fixture;
static char stream_error[256];
static int fixture_send(SOCKET socket_handle, const char* data, int bytes)
{
    int rc;
    while (bytes > 0) {
        rc = send(socket_handle, data, bytes, 0);
        if (rc <= 0) { return 0; }
        data += rc; bytes -= rc;
    }
    return 1;
}
static int fixture_text(SOCKET peer, const char* text)
{ return fixture_send(peer, text, (int)strlen(text)); }
static DWORD WINAPI fixture_server(LPVOID data)
{
    stream_fixture* fixture;
    SOCKET peer;
    char request[12288];
    char header[512];
    char block[4096];
    unsigned long sent;
    unsigned long i;
    int used;
    int bytes;
    int hop;
    fixture = (stream_fixture*)data;
    for (hop = 0; hop < (fixture->mode == 3 ? 2 : 1); hop++) {
        peer = accept(fixture->listener, NULL, NULL);
        if (peer == INVALID_SOCKET) { fixture->error = 1; return 0; }
        SetEvent(fixture->accepted);
        if (fixture->mode == 9 || fixture->mode == 17) {
            WaitForSingleObject(fixture->release, 5000); closesocket(peer); return 0;
        }
        used = 0;
        while (used < (int)sizeof(request) - 1) {
            bytes = recv(peer, request + used, (int)sizeof(request) - 1 - used, 0);
            if (bytes <= 0) { break; }
            used += bytes; request[used] = '\0';
            if (strstr(request, "\r\n\r\n") != NULL) { break; }
        }
        if (used == 0 || strstr(request, "GET /") == NULL) { fixture->error = 2; closesocket(peer); return 0; }
        if (fixture->mode == 3 && hop == 0) {
            fixture_text(peer, "HTTP/1.1 302 Found\r\nLocation: /final?x=1#gone\r\nContent-Length: 3\r\n\r\nbad");
        } else if (fixture->mode == 2) {
            fixture_send(peer, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3;key=value\r\nabc\r\n2\r\nde\r\n0\r\nX-End: yes\r\n\r\n",
                    (int)strlen("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3;key=value\r\nabc\r\n2\r\nde\r\n0\r\nX-End: yes\r\n\r\n"));
        } else if (fixture->mode == 4) {
            fixture_text(peer, "HTTP/1.1 404 Missing\r\n\r\nabcde");
        } else if (fixture->mode == 5) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nabcde");
        } else if (fixture->mode == 6) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 5\r\n\r\nabcde");
        } else if (fixture->mode == 7) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nabc");
        } else if (fixture->mode == 11) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\nX-Large: ");
            memset(block, 'x', 1024); fixture_send(peer, block, 1024); fixture_text(peer, "\r\n\r\n");
        } else if (fixture->mode == 12) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\n");
            for (i = 0; i < 65; i++) { fixture_text(peer, "X-Many: yes\r\n"); }
            fixture_text(peer, "\r\n");
        } else if (fixture->mode == 13) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n");
        } else if (fixture->mode == 14) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\nContent-Type: ");
            memset(block, 'x', 300); fixture_send(peer, block, 300); fixture_text(peer, "\r\n\r\n");
        } else if (fixture->mode == 15) {
            fixture_text(peer, "HTTP/1.1 20x Bad\r\n\r\n");
        } else if (fixture->mode == 16) {
            fixture_text(peer, "HTTP/1.1 204 No Content\r\n\r\n");
        } else if (fixture->mode == 18) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n");
        } else if (fixture->mode == 19) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\nContent-Length: 5junk\r\n\r\n");
        } else if (fixture->mode == 21) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 9\r\n\r\n");
        } else if (fixture->mode == 22) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n200001\r\n");
        } else if (fixture->mode == 23) {
            _snprintf(header, sizeof(header), "HTTP/1.1 302 Found\r\nLocation: http://localhost:%u/final\r\nContent-Length: 0\r\n\r\n", fixture->port);
            fixture_text(peer, header);
        } else if (fixture->mode == 24) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\nContent-Type: \xff\r\n\r\n");
        } else if (fixture->mode == 26) {
            fixture_text(peer, "HTTP/1.1 200 OK\r\n");
            for (i = 0; i < 32; i++) {
                fixture_text(peer, "X-Fill: "); memset(block, 'x', 600);
                fixture_send(peer, block, 600); fixture_text(peer, "\r\n");
            }
            fixture_text(peer, "\r\n");
        } else if (fixture->mode == 30) {
            fixture_text(peer, "HTTP/1.1 304 Not Modified\r\nContent-Length: 3000000\r\n\r\n");
        } else {
            if (fixture->mode == 20) { fixture_text(peer, "HTTP/1.1 100 Continue\r\n\r\n"); }
            _snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\nContent-Type: application/octet-stream\r\n"
                    "Content-Disposition: attachment; filename=fixture.bin\r\nX-Repeat: one\r\nX-Repeat: two\r\n\r\n", fixture->expected);
            fixture_send(peer, header, (int)strlen(header));
            sent = 0;
            while (sent < fixture->expected) {
                bytes = fixture->expected - sent > sizeof(block) ? sizeof(block) : (int)(fixture->expected - sent);
                for (i = 0; i < (unsigned long)bytes; i++) { block[i] = (char)((sent + i) % 251); }
                if (!fixture_send(peer, block, bytes)) { break; }
                sent += bytes;
                if (fixture->mode == 8) { WaitForSingleObject(fixture->release, 5000); break; }
            }
        }
        closesocket(peer);
    }
    return 0;
}
static int fixture_headers(void* data, const PHttpStreamResult* result,
        const char* headers, unsigned long bytes)
{
    stream_fixture* fixture;
    fixture = (stream_fixture*)data;
    fixture->headers_seen++;
    if (result->size != sizeof(*result) || result->version != PHTTP_STREAM_VERSION ||
            result->received != 0 || headers == NULL || bytes == 0) { fixture->error = 3; }
    return fixture->mode == 27 ? 1 : 0;
}
static int fixture_sink(void* data, const unsigned char* block,
        unsigned long bytes, __int64 received, __int64 total)
{
    stream_fixture* fixture;
    PHttpStreamResult unchanged;
    unsigned long i;
    fixture = (stream_fixture*)data;
    if (bytes == 0 || bytes > PHTTP_STREAM_BLOCK_BYTES || received != (__int64)fixture->bytes + bytes) { return 1; }
    if (fixture->mode == 2 || fixture->mode == 4 || fixture->mode == 7) {
        if (total != -1 || fixture->bytes + bytes > 5 ||
                memcmp(block, "abcde" + fixture->bytes, bytes) != 0) { return 1; }
    } else if (fixture->mode != 25) {
        if (total != fixture->expected) { return 1; }
        for (i = 0; i < bytes; i++) { if (block[i] != (fixture->bytes + i) % 251) { return 1; } }
    }
    memset(&unchanged, 0, sizeof(unchanged)); unchanged.size = sizeof(unchanged);
    unchanged.version = PHTTP_STREAM_VERSION; unchanged.received = 991;
    fixture->guards = PHttp_StreamClose(fixture->request) == PHTTP_STREAM_STATE &&
            PHttp_StreamGet(fixture->request) == PHTTP_STREAM_STATE &&
            PHttp_StreamGetResult(fixture->request, &unchanged) == PHTTP_STREAM_STATE && unchanged.received == 991;
    if (fixture->sink_error) { return 1; }
    fixture->bytes += bytes;
    if (fixture->mode == 29 && fixture->bytes == fixture->expected &&
            PHttp_StreamCancel(fixture->request) != PHTTP_STREAM_OK) { return 1; }
    if (fixture->body_seen != NULL) { SetEvent(fixture->body_seen); }
    return 0;
}
static DWORD WINAPI fixture_execute(LPVOID data)
{
    stream_fixture* fixture;
    fixture = (stream_fixture*)data;
    fixture->execute_result = PHttp_StreamGet(fixture->request);
    return 0;
}
static void fixture_options(PHttpStreamOptions* options, stream_fixture* fixture)
{
    memset(options, 0, sizeof(*options)); options->size = sizeof(*options);
    options->version = PHTTP_STREAM_VERSION; options->timeout_ms = 20000;
    options->body_limit = 2 * 1024 * 1024; options->headers = fixture_headers;
    options->sink = fixture_sink; options->user_data = fixture;
}
static void fixture_stop(stream_fixture* fixture)
{
    if (fixture->release != NULL) { SetEvent(fixture->release); }
    if (fixture->server != NULL) {
        /* Closing the fixture listener only interrupts its own pending accept. */
        if (fixture->listener != INVALID_SOCKET) { closesocket(fixture->listener); fixture->listener = INVALID_SOCKET; }
        WaitForSingleObject(fixture->server, INFINITE); CloseHandle(fixture->server);
    }
    if (fixture->listener != INVALID_SOCKET) { closesocket(fixture->listener); }
    if (fixture->request != NULL) { PHttp_StreamClose(fixture->request); }
    if (fixture->accepted != NULL) { CloseHandle(fixture->accepted); }
    if (fixture->release != NULL) { CloseHandle(fixture->release); }
    if (fixture->body_seen != NULL) { CloseHandle(fixture->body_seen); }
}
static int fixture_start(stream_fixture* fixture, int mode, unsigned long expected)
{
    struct sockaddr_in address;
    int size;
    memset(fixture, 0, sizeof(*fixture)); fixture->listener = INVALID_SOCKET;
    fixture->mode = mode; fixture->expected = expected;
    fixture->accepted = CreateEvent(NULL, TRUE, FALSE, NULL);
    fixture->release = CreateEvent(NULL, TRUE, FALSE, NULL);
    fixture->body_seen = CreateEvent(NULL, TRUE, FALSE, NULL);
    fixture->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fixture->listener == INVALID_SOCKET || fixture->accepted == NULL || fixture->release == NULL || fixture->body_seen == NULL) { return 0; }
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    size = sizeof(address);
    if (bind(fixture->listener, (struct sockaddr*)&address, sizeof(address)) != 0 ||
            getsockname(fixture->listener, (struct sockaddr*)&address, &size) != 0 || listen(fixture->listener, 2) != 0) { return 0; }
    fixture->port = ntohs(address.sin_port);
    fixture->server = CreateThread(NULL, 0, fixture_server, fixture, 0, NULL);
    return fixture->server != NULL;
}
static int fixture_isolation(void)
{
    stream_fixture a;
    stream_fixture b;
    PHttpStreamOptions options;
    PHttpStreamResult result;
    HANDLE wa;
    HANDLE wb;
    char url[256];
    int ok;
    memset(&a, 0, sizeof(a)); a.listener = INVALID_SOCKET;
    memset(&b, 0, sizeof(b)); b.listener = INVALID_SOCKET;
    wa = wb = NULL; ok = 0;
    if (!fixture_start(&a, 8, 65536) || !fixture_start(&b, 1, 1081344UL)) { goto done; }
    fixture_options(&options, &a);
    _snprintf(url, sizeof(url), "http://127.0.0.1:%u/", a.port);
    if (PHttp_StreamCreateGet(url, NULL, &options, &a.request) != 0) { goto done; }
    fixture_options(&options, &b);
    _snprintf(url, sizeof(url), "http://127.0.0.1:%u/", b.port);
    if (PHttp_StreamCreateGet(url, NULL, &options, &b.request) != 0) { goto done; }
    wa = CreateThread(NULL, 0, fixture_execute, &a, 0, NULL);
    if (wa == NULL || WaitForSingleObject(a.body_seen, 5000) != WAIT_OBJECT_0) { goto done; }
    wb = CreateThread(NULL, 0, fixture_execute, &b, 0, NULL);
    if (wb == NULL || PHttp_StreamCancel(a.request) != 0 ||
            WaitForSingleObject(wa, 5000) != WAIT_OBJECT_0 ||
            WaitForSingleObject(wb, 20000) != WAIT_OBJECT_0) { goto done; }
    memset(&result, 0, sizeof(result)); result.size = sizeof(result); result.version = PHTTP_STREAM_VERSION;
    if (a.execute_result != PHTTP_STREAM_CANCELLED || b.execute_result != PHTTP_STREAM_OK ||
            b.bytes != 1081344UL || !b.guards || a.headers_seen != 1 || b.headers_seen != 1 ||
            PHttp_StreamGetResult(b.request, &result) != 0 || result.received != 1081344UL) { goto done; }
    ok = 1;
done:
    if (wa != NULL) { PHttp_StreamCancel(a.request); SetEvent(a.release); WaitForSingleObject(wa, INFINITE); CloseHandle(wa); }
    if (wb != NULL) { PHttp_StreamCancel(b.request); SetEvent(b.release); WaitForSingleObject(wb, INFINITE); CloseHandle(wb); }
    fixture_stop(&a); fixture_stop(&b);
    return ok;
}
typedef struct fixture_dns_cancel {
    HANDLE cancel;
    int notifications;
} fixture_dns_cancel;
static void fixture_cancel_dns(const PTlsConnectObserverEvent* event, void* data)
{
    fixture_dns_cancel* fixture;
    fixture = (fixture_dns_cancel*)data;
    fixture->notifications++;
    if (event->phase == PTLS_CONNECT_PHASE_RESOLVING_NAME) { SetEvent(fixture->cancel); }
}
static int fixture_dns_cancellation(void)
{
    fixture_dns_cancel fixture;
    PTlsConnectObserver observer;
    PTlsTransportOptions options;
    HANDLE connection;
    DWORD started;
    int error;
    int ok;
    memset(&fixture, 0, sizeof(fixture));
    fixture.cancel = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (fixture.cancel == NULL) { return 0; }
    memset(&observer, 0, sizeof(observer)); observer.size = sizeof(observer);
    observer.version = PTLS_CONNECT_OBSERVER_VERSION; observer.callback = fixture_cancel_dns; observer.user_data = &fixture;
    memset(&options, 0, sizeof(options)); options.size = sizeof(options); options.version = PTLS_TRANSPORT_VERSION;
    options.use_tls = options.verify_peer = 1; options.timeout_ms = 2000; options.cancel_event = fixture.cancel;
    started = GetTickCount();
    connection = PTls_TransportOpenEx("positron-cancel-fixture.invalid", 443, &options, &observer, &error);
    ok = connection == NULL && error == PTLS_TRANSPORT_CANCELLED && fixture.notifications == 1 &&
            (DWORD)(GetTickCount() - started) < 2000;
    PTls_TransportClose(connection); CloseHandle(fixture.cancel);
    /* The resolver owns copied data, never this fixture/event after Open returns. */
    return ok;
}
#define STREAM_CHECK(condition, message) do { if (!(condition)) { \
    _snprintf(stream_error, sizeof(stream_error) - 1, "%s (line %d)", message, __LINE__); goto done; } } while (0)
BOOL test1342_http_stream_contract(void)
{
    stream_fixture fixture;
    PHttpStreamOptions options;
    PHttpStreamResult result;
    PHttpResponse* legacy;
    HANDLE worker;
    HANDLE unchanged;
    char url[256];
    char field[512];
    const char* bad_headers[2];
    const char* authorized[2];
    int mode;
    int expected_result;
    int ok;
    int rc;
    DWORD started;
    memset(&fixture, 0, sizeof(fixture)); fixture.listener = INVALID_SOCKET;
    worker = NULL; legacy = NULL; ok = 0; stream_error[0] = '\0';
    STREAM_CHECK(PHttp_Init(), "HTTP initialize");
    fixture_options(&options, &fixture);
    unchanged = (HANDLE)123;
    options.version++;
    STREAM_CHECK(PHttp_StreamCreateGet("http://127.0.0.1/", NULL, &options, &unchanged) == PHTTP_STREAM_ARGUMENT &&
            unchanged == (HANDLE)123, "invalid options preserve handle");
    options.version--;
    bad_headers[0] = "X-Test: ok\r\nInjected: no"; bad_headers[1] = NULL;
    STREAM_CHECK(PHttp_StreamCreateGet("http://127.0.0.1/", bad_headers, &options, &unchanged) == PHTTP_STREAM_ARGUMENT,
            "request header injection rejects");
    bad_headers[0] = "X-Test: \xff";
    STREAM_CHECK(PHttp_StreamCreateGet("http://127.0.0.1/", bad_headers, &options, &unchanged) == PHTTP_STREAM_ARGUMENT,
            "invalid UTF-8 header rejects");
    authorized[0] = "Authorization: Bearer fixture-not-a-secret"; authorized[1] = NULL;
    for (mode = 1; mode <= 30; mode++) {
        if (mode == 25) { continue; } /* real HTTPS is an independent test */
        STREAM_CHECK(fixture_start(&fixture, mode == 10 ? 9 : mode,
                mode == 1 ? 1081344UL : mode == 8 ? 65536 : 5), "fixture start");
        fixture_options(&options, &fixture);
        if (mode == 10 || mode == 17) { options.timeout_ms = 100; }
        if (mode == 28) { options.headers = NULL; }
        _snprintf(url, sizeof(url), "%s://%s:%u/start", mode == 9 || mode == 10 ? "https" : "http",
                mode == 3 ? "localhost" : "127.0.0.1", fixture.port);
        STREAM_CHECK(PHttp_StreamCreateGet(url, mode == 23 ? authorized : NULL, &options, &fixture.request) == PHTTP_STREAM_OK, "create stream");
        if (mode == 8 || mode == 9) {
            worker = CreateThread(NULL, 0, fixture_execute, &fixture, 0, NULL);
            STREAM_CHECK(worker != NULL && WaitForSingleObject(mode == 8 ? fixture.body_seen : fixture.accepted, 5000) == WAIT_OBJECT_0,
                    "worker reaches controlled body/handshake boundary");
            started = GetTickCount();
            STREAM_CHECK(PHttp_StreamCancel(fixture.request) == PHTTP_STREAM_OK,
                    "first cross-thread cancellation accepted");
            rc = PHttp_StreamCancel(fixture.request);
            STREAM_CHECK((rc == PHTTP_STREAM_OK || rc == PHTTP_STREAM_STATE) &&
                    WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0 && fixture.execute_result == PHTTP_STREAM_CANCELLED &&
                    (DWORD)(GetTickCount() - started) < 5000, "cross-thread cancellation returns without server release");
            CloseHandle(worker); worker = NULL; rc = fixture.execute_result;
        } else { rc = PHttp_StreamGet(fixture.request); }
        expected_result = mode == 5 || mode == 7 || mode == 13 || mode == 15 ||
                mode == 19 || mode == 21 ? PHTTP_STREAM_PROTOCOL :
                mode == 11 || mode == 12 || mode == 14 || mode == 22 || mode == 26 ? PHTTP_STREAM_LIMIT :
                mode == 6 || mode == 18 || mode == 23 || mode == 24 ? PHTTP_STREAM_UNSUPPORTED :
                mode == 27 ? PHTTP_STREAM_SINK : mode == 8 || mode == 9 || mode == 29 ? PHTTP_STREAM_CANCELLED :
                mode == 10 || mode == 17 ? PHTTP_STREAM_TIMEOUT : PHTTP_STREAM_OK;
        if (rc != expected_result) {
            memset(&result, 0, sizeof(result)); result.size = sizeof(result); result.version = PHTTP_STREAM_VERSION;
            PHttp_StreamGetResult(fixture.request, &result);
            _snprintf(stream_error, sizeof(stream_error) - 1,
                    "mode=%d rc=%d expected=%d transport=%d status=%d bytes=%lu headers=%d server=%d",
                    mode, rc, expected_result, result.transport_error, result.status_code,
                    fixture.bytes, fixture.headers_seen, fixture.error);
            goto done;
        }
        memset(&result, 0, sizeof(result)); result.size = sizeof(result); result.version = PHTTP_STREAM_VERSION;
        STREAM_CHECK(PHttp_StreamGetResult(fixture.request, &result) == 0 && result.result == rc &&
                result.received == fixture.bytes && PHttp_StreamCancel(fixture.request) == PHTTP_STREAM_STATE &&
                PHttp_StreamGet(fixture.request) == PHTTP_STREAM_STATE, "stable result and one-shot terminal");
        if (mode <= 4) {
            STREAM_CHECK(fixture.error == 0 && fixture.headers_seen == 1 && fixture.guards &&
                    fixture.bytes == (mode == 1 ? 1081344UL : 5), "exact sink bytes and callback guards");
        }
        if (mode == 1) {
            STREAM_CHECK(PHttp_StreamGetHeader(fixture.request, "X-Repeat", 1, field, sizeof(field)) == 0 &&
                    strcmp(field, "two") == 0 && strcmp(result.content_type, "application/octet-stream") == 0,
                    "metadata and duplicate header order");
            strcpy(field, "unchanged");
            STREAM_CHECK(PHttp_StreamGetHeader(fixture.request, "Content-Disposition", 0, field, 4) == PHTTP_STREAM_LIMIT &&
                    strcmp(field, "unchanged") == 0, "header capacity preserves output");
        }
        if (mode == 3) { STREAM_CHECK(result.redirect_hop == 1 && strstr(result.final_url, "/final?x=1") != NULL &&
                strchr(result.final_url, '#') == NULL, "final URL and intermediate body not delivered"); }
        if (mode == 4) { STREAM_CHECK(result.status_code == 404, "non-2xx transport completes"); }
        if (mode == 16) { STREAM_CHECK(result.status_code == 204 && fixture.bytes == 0 && fixture.headers_seen == 1,
                "no-body status completes without sink"); }
        if (mode == 23) { STREAM_CHECK(result.redirect_hop == 0 && fixture.bytes == 0 && fixture.headers_seen == 0,
                "sensitive cross-origin redirect rejected before second connection"); }
        if (mode == 27) { STREAM_CHECK(fixture.bytes == 0 && fixture.headers_seen == 1,
                "header refusal delivers no body"); }
        if (mode == 28) { STREAM_CHECK(fixture.bytes == 5 && fixture.headers_seen == 0 && fixture.guards,
                "NULL headers callback preserves body delivery"); }
        if (mode == 29) { STREAM_CHECK(fixture.bytes == 5 && result.received == 5,
                "cancel wins before terminal even after final sink accepts"); }
        if (mode == 30) { STREAM_CHECK(result.status_code == 304 && result.total == 3000000 &&
                fixture.bytes == 0 && fixture.headers_seen == 1, "304 representation length does not consume body quota"); }
        fixture_stop(&fixture); memset(&fixture, 0, sizeof(fixture)); fixture.listener = INVALID_SOCKET;
    }
    STREAM_CHECK(fixture_start(&fixture, 1, 65536), "sink error fixture");
    fixture_options(&options, &fixture); fixture.sink_error = 1;
    _snprintf(url, sizeof(url), "http://127.0.0.1:%u/", fixture.port);
    STREAM_CHECK(PHttp_StreamCreateGet(url, NULL, &options, &fixture.request) == 0 &&
            PHttp_StreamGet(fixture.request) == PHTTP_STREAM_SINK && fixture.bytes == 0, "sink refusal distinct from success");
    fixture_stop(&fixture); memset(&fixture, 0, sizeof(fixture)); fixture.listener = INVALID_SOCKET;
    STREAM_CHECK(fixture_start(&fixture, 1, 65536), "body limit fixture");
    fixture_options(&options, &fixture); options.body_limit = 1024;
    _snprintf(url, sizeof(url), "http://127.0.0.1:%u/", fixture.port);
    STREAM_CHECK(PHttp_StreamCreateGet(url, NULL, &options, &fixture.request) == 0 &&
            PHttp_StreamGet(fixture.request) == PHTTP_STREAM_LIMIT && fixture.bytes == 0 && fixture.headers_seen == 0,
            "known body limit before sink");
    fixture_stop(&fixture); memset(&fixture, 0, sizeof(fixture)); fixture.listener = INVALID_SOCKET;
    fixture_options(&options, &fixture);
    STREAM_CHECK(PHttp_StreamCreateGet("https://127.0.0.1:1/", NULL, &options, &fixture.request) == 0 &&
            PHttp_StreamCancel(fixture.request) == 0 && PHttp_StreamGet(fixture.request) == PHTTP_STREAM_CANCELLED &&
            fixture.headers_seen == 0, "pre-execute cancellation no network callback");
    STREAM_CHECK(fixture_isolation(), "concurrent request cancellation isolation");
    STREAM_CHECK(fixture_dns_cancellation(), "DNS cancellation returns with isolated resolver lifetime");
    fixture_stop(&fixture); memset(&fixture, 0, sizeof(fixture)); fixture.listener = INVALID_SOCKET;
    STREAM_CHECK(fixture_start(&fixture, 1, 1081344UL), "legacy body ceiling fixture");
    _snprintf(url, sizeof(url), "http://127.0.0.1:%u/", fixture.port);
    legacy = PHttp_GetUrl(url, NULL);
    STREAM_CHECK(legacy != NULL && legacy->status_code == 0 && legacy->body_len == 0 &&
            strcmp(legacy->error_msg, "response body too large") == 0, "legacy complete-body 1 MiB ceiling unchanged");
    ok = 1;
done:
    if (worker != NULL) { PHttp_StreamCancel(fixture.request); SetEvent(fixture.release); WaitForSingleObject(worker, INFINITE); CloseHandle(worker); }
    fixture_stop(&fixture);
    PHttp_FreeResponse(legacy);
    stream_error[sizeof(stream_error) - 1] = '\0';
    return ok;
}
const char* test1342_http_stream_last_error(void) { return stream_error; }

BOOL test1343_http_stream_verified_https(void)
{
    stream_fixture fixture;
    PHttpStreamOptions options;
    PHttpStreamResult result;
    int rc;
    int ok;
    memset(&fixture, 0, sizeof(fixture)); fixture.listener = INVALID_SOCKET;
    fixture.mode = 25; stream_error[0] = '\0'; ok = 0;
    fixture_options(&options, &fixture); options.timeout_ms = 60000;
    if (!PHttp_Init() || PHttp_StreamCreateGet("https://www.iana.org/help/example-domains",
            NULL, &options, &fixture.request) != 0) { strcpy(stream_error, "verified HTTPS create failed"); goto done; }
    rc = PHttp_StreamGet(fixture.request);
    memset(&result, 0, sizeof(result)); result.size = sizeof(result); result.version = PHTTP_STREAM_VERSION;
    if (PHttp_StreamGetResult(fixture.request, &result) != 0 || rc != PHTTP_STREAM_OK ||
            result.status_code != 200 || fixture.bytes < 1000 || fixture.headers_seen != 1 ||
            !fixture.guards || strncmp(result.final_url, "https://www.iana.org/", 21) != 0) {
        _snprintf(stream_error, sizeof(stream_error) - 1, "verified HTTPS rc=%d transport=%d status=%d bytes=%lu headers=%d",
                rc, result.transport_error, result.status_code, fixture.bytes, fixture.headers_seen);
        goto done;
    }
    ok = 1;
done:
    fixture_stop(&fixture);
    return ok;
}

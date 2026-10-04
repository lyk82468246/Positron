/* Browser-owned bounded async application services. No HWND, I/O or worker. */
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "positron_browser.h"
#include "positron_script.h"
#include "positron_json.h"
#include "p_browser_services.h"

#define SERVICE_SUBMIT "__positronServiceSubmit"
#define SERVICE_NEXT "__positronServiceNext"
#define SERVICE_PUMP "__positronServicePump"
typedef struct p_service_pending {
    int state; /* 0 empty, 1 awaiting host, 2 queued result */
    unsigned long id;
    unsigned long order;
    int succeeded;
    char local_id[16];
    char *result;
} p_service_pending;
struct p_browser_services {
    HANDLE runtime;
    int enabled;
    int busy;
    int pumping;
    int clearing;
    unsigned long bridge_id;
    unsigned long next_id;
    unsigned long next_order;
    unsigned long remaining;
    unsigned long delivered;
    PBrowserServiceOptions options;
    char methods[PBROWSER_SERVICE_MAX_METHODS][PBROWSER_SERVICE_METHOD_MAX_BYTES + 1];
    p_service_pending pending[PBROWSER_SERVICE_MAX_PENDING];
};

/* Counter only, never a process-wide observer. Refuse exhaustion instead of
 * recycling tokens after a destroyed page or same-address session reuse. */
/* All accesses use Interlocked; WM6's prototype takes LONG*, not volatile. */
static LONG service_serial;
static unsigned long p_service_new_serial(void)
{
    LONG old;
    for (;;) {
        old = InterlockedCompareExchange(&service_serial, 0, 0);
        if (old == 2147483647L) { return 0; }
        if (InterlockedCompareExchange(&service_serial, old + 1, old) == old) {
            return (unsigned long) old + 1;
        }
    }
}

/* Strict, bounded lexical validation before using the JSON DLL's legacy
 * prefix-accepting parser. This owns no JSON values or schema: it only checks
 * that the whole wire value fits depth/UTF-8/syntax budgets. */
typedef struct p_service_json {
    const unsigned char *p;
    const unsigned char *end;
} p_service_json;
static void p_service_space(p_service_json *r)
{
    while (r->p < r->end && (*r->p == ' ' || *r->p == '\t' ||
            *r->p == '\r' || *r->p == '\n')) { r->p++; }
}
static int p_service_string(p_service_json *r)
{
    unsigned int code;
    unsigned int minimum;
    unsigned int c;
    int more;
    int i;
    if (r->p == r->end || *r->p++ != '"') { return 0; }
    while (r->p < r->end) {
        c = *r->p++;
        if (c == '"') { return 1; }
        if (c < 32) { return 0; }
        if (c == '\\') {
            if (r->p == r->end) { return 0; }
            c = *r->p++;
            if (c == 'u') {
                for (i = 0; i < 4; i++) {
                    if (r->p == r->end) { return 0; }
                    c = *r->p++;
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                            (c >= 'A' && c <= 'F'))) { return 0; }
                }
            } else if (c != '"' && c != '\\' && c != '/' && c != 'b' &&
                    c != 'f' && c != 'n' && c != 'r' && c != 't') { return 0; }
        } else if (c >= 128) {
            if (c >= 194 && c <= 223) { more = 1; code = c & 31; minimum = 128; }
            else if (c >= 224 && c <= 239) { more = 2; code = c & 15; minimum = 2048; }
            else if (c >= 240 && c <= 244) { more = 3; code = c & 7; minimum = 65536; }
            else { return 0; }
            for (i = 0; i < more; i++) {
                if (r->p == r->end || (*r->p & 192) != 128) { return 0; }
                code = (code << 6) | (*r->p++ & 63);
            }
            if (code < minimum || code > 1114111 ||
                    (code >= 55296 && code <= 57343)) { return 0; }
        }
    }
    return 0;
}
static int p_service_value(p_service_json *r, int depth)
{
    unsigned char end;
    const char *literal;
    size_t length;
    p_service_space(r);
    if (r->p == r->end || depth > 16) { return 0; }
    if (*r->p == '"') { return p_service_string(r); }
    if (*r->p == '[' || *r->p == '{') {
        end = *r->p++ == '[' ? ']' : '}';
        p_service_space(r);
        if (r->p < r->end && *r->p == end) { r->p++; return 1; }
        for (;;) {
            if (end == '}') {
                p_service_space(r);
                if (!p_service_string(r)) { return 0; }
                p_service_space(r);
                if (r->p == r->end || *r->p++ != ':') { return 0; }
            }
            if (!p_service_value(r, depth + 1)) { return 0; }
            p_service_space(r);
            if (r->p == r->end) { return 0; }
            if (*r->p == end) { r->p++; return 1; }
            if (*r->p++ != ',') { return 0; }
        }
    }
    literal = *r->p == 't' ? "true" : *r->p == 'f' ? "false" :
            *r->p == 'n' ? "null" : NULL;
    if (literal != NULL) {
        length = strlen(literal);
        if ((size_t) (r->end - r->p) < length ||
                memcmp(r->p, literal, length) != 0) { return 0; }
        r->p += length;
        return 1;
    }
    if (*r->p == '-') { r->p++; }
    if (r->p == r->end) { return 0; }
    if (*r->p == '0') { r->p++; }
    else {
        if (*r->p < '1' || *r->p > '9') { return 0; }
        while (r->p < r->end && *r->p >= '0' && *r->p <= '9') { r->p++; }
    }
    if (r->p < r->end && *r->p == '.') {
        r->p++;
        if (r->p == r->end || *r->p < '0' || *r->p > '9') { return 0; }
        while (r->p < r->end && *r->p >= '0' && *r->p <= '9') { r->p++; }
    }
    if (r->p < r->end && (*r->p == 'e' || *r->p == 'E')) {
        r->p++;
        if (r->p < r->end && (*r->p == '+' || *r->p == '-')) { r->p++; }
        if (r->p == r->end || *r->p < '0' || *r->p > '9') { return 0; }
        while (r->p < r->end && *r->p >= '0' && *r->p <= '9') { r->p++; }
    }
    return 1;
}
static int p_service_json_valid(const char *json, unsigned long bytes)
{
    p_service_json reader;
    if (json == NULL || bytes == 0 || bytes > PBROWSER_SERVICE_JSON_MAX_BYTES) { return 0; }
    reader.p = (const unsigned char *) json;
    reader.end = reader.p + bytes;
    if (!p_service_value(&reader, 0)) { return 0; }
    p_service_space(&reader);
    return reader.p == reader.end;
}
static int p_service_method_valid(const char *name)
{
    unsigned long i;
    unsigned char c;
    if (name == NULL || name[0] == '\0') { return 0; }
    for (i = 0; name[i] != '\0'; i++) {
        if (i >= PBROWSER_SERVICE_METHOD_MAX_BYTES) { return 0; }
        c = (unsigned char) name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) { return 0; }
    }
    return 1;
}
static int p_service_write(char *out, int capacity, int *length, const char *text)
{
    size_t bytes;
    bytes = strlen(text);
    if (capacity <= 0 || bytes >= (size_t) capacity) { return 1; }
    memcpy(out, text, bytes + 1);
    *length = (int) bytes;
    return 0;
}
static int p_service_submit(void *pw, const char *args, int bytes,
        char *out, int capacity, int *length)
{
    p_browser_services *s;
    HANDLE parsed;
    const char *method;
    const char *params;
    const char *local_id;
    PBrowserServiceRequest request;
    p_service_pending *pending;
    unsigned long i;
    unsigned long j;
    int accepted;
    char token[80];
    s = (p_browser_services *) pw;
    if (!s->enabled || bytes <= 0 || bytes > 6 * (int) PBROWSER_SERVICE_JSON_MAX_BYTES + 256 ||
            capacity < (int) sizeof(token)) { return 1; }
    parsed = PJson_Parse(args);
    if (parsed == NULL) { return 1; }
    method = PJson_GetString(parsed, "method");
    params = PJson_GetString(parsed, "params");
    local_id = PJson_GetString(parsed, "local");
    /* Native arguments are an array; the sole envelope is its first item. */
    if (PJson_GetType(parsed) == PJSON_TYPE_ARRAY && PJson_GetArraySize(parsed) == 1) {
        HANDLE envelope;
        envelope = PJson_GetArrayItem(parsed, 0);
        method = PJson_GetString(envelope, "method");
        params = PJson_GetString(envelope, "params");
        local_id = PJson_GetString(envelope, "local");
    }
    if (!p_service_method_valid(method) || params == NULL || local_id == NULL ||
            !p_service_json_valid(params, (unsigned long) strlen(params))) { PJson_Free(parsed); return 1; }
    for (i = 0; i < s->options.method_count; i++) {
        if (strcmp(method, s->methods[i]) == 0) { break; }
    }
    if (i == s->options.method_count || strlen(local_id) == 0 || strlen(local_id) > 10) {
        PJson_Free(parsed); return 1;
    }
    for (j = 0; local_id[j] != '\0'; j++) {
        if (local_id[j] < '0' || local_id[j] > '9') { PJson_Free(parsed); return 1; }
    }
    pending = NULL;
    for (i = 0; i < PBROWSER_SERVICE_MAX_PENDING; i++) {
        if (s->pending[i].state != 0 && strcmp(s->pending[i].local_id, local_id) == 0) {
            PJson_Free(parsed); return 1;
        }
        if (s->pending[i].state == 0 && pending == NULL) { pending = &s->pending[i]; }
    }
    if (pending == NULL || s->next_id == 0xffffffffUL) { PJson_Free(parsed); return 1; }
    memset(&request, 0, sizeof(request));
    request.size = sizeof(request);
    request.version = PBROWSER_SERVICE_VERSION;
    request.token.bridge_id = s->bridge_id;
    request.token.request_id = ++s->next_id;
    request.tab_id = s->options.tab_id;
    request.page_generation = s->options.page_generation;
    request.method = method;
    request.params_json = params;
    request.params_bytes = (unsigned long) strlen(params);
    pending->state = 1;
    pending->id = request.token.request_id;
    strcpy(pending->local_id, local_id);
    s->busy++;
    accepted = s->options.submit(s->options.pw, &request);
    s->busy--;
    PJson_Free(parsed);
    if (accepted != 0) { memset(pending, 0, sizeof(*pending)); return 1; }
    _snprintf(token, sizeof(token), "\"%lu/%lu\"", s->bridge_id, pending->id);
    return p_service_write(out, capacity, length, token);
}
static int p_service_next(void *pw, const char *args, int bytes,
        char *out, int capacity, int *length)
{
    p_browser_services *s;
    p_service_pending *pending;
    unsigned long i;
    int used;
    size_t result_bytes;
    (void) args; (void) bytes;
    s = (p_browser_services *) pw;
    if (s->clearing) {
        s->clearing = 0;
        return p_service_write(out, capacity, length, "{\"clear\":true}");
    }
    if (!s->pumping || !s->enabled || s->remaining == 0) {
        return p_service_write(out, capacity, length, "null");
    }
    pending = NULL;
    for (i = 0; i < PBROWSER_SERVICE_MAX_PENDING; i++) {
        if (s->pending[i].state == 2 && (pending == NULL || s->pending[i].order < pending->order)) {
            pending = &s->pending[i];
        }
    }
    if (pending == NULL) { return p_service_write(out, capacity, length, "null"); }
    used = _snprintf(out, capacity, "{\"local\":\"%s\",\"ok\":%s,\"value\":",
            pending->local_id, pending->succeeded ? "true" : "false");
    result_bytes = strlen(pending->result);
    if (used < 0 || result_bytes + (size_t) used + 2 >= (size_t) capacity) { return 1; }
    memcpy(out + used, pending->result, result_bytes);
    out[used + result_bytes] = '}';
    out[used + result_bytes + 1] = '\0';
    *length = used + (int) result_bytes + 1;
    free(pending->result);
    memset(pending, 0, sizeof(*pending));
    s->remaining--;
    s->delivered++;
    return 0;
}

static const char SERVICE_INSTALL[] =
    "(function(g){'use strict';var submit=g.__positronServiceSubmit,next=g.__positronServiceNext;"
    "var create=Object.create,callbacks=create(null),id=0,count=0,stringify=JSON.stringify;"
    "var service=Object.create(null);"
    "service.request=function(method,params,cb){if(typeof method!=='string'||"
    "!/^[A-Za-z0-9_.-]{1,63}$/.test(method)||typeof cb!=='function')throw new TypeError('service arguments');"
    "if(count>=16||id>=2147483647)throw new Error('service request limit');"
    "var json=stringify(params);if(typeof json!=='string')throw new TypeError('service JSON');"
    "var local=String(++id);callbacks[local]=cb;count++;try{return submit({method:method,params:json,local:local});}"
    "catch(e){delete callbacks[local];count--;throw e;}};Object.freeze(service);"
    "Object.defineProperty(g,'PositronServices',{value:service});"
    "Object.defineProperty(g,'__positronServicePump',{value:function(){"
    "var item=next();if(item&&item.clear){callbacks=create(null);count=0;return 0;}"
    "while(item!==null){var cb=callbacks[item.local];delete callbacks[item.local];"
    "if(cb){count--;cb(item.ok,item.value);}item=next();}return 0;}});"
    "g.__positronServiceSubmit=null;g.__positronServiceNext=null;return 0;})(this);";

int p_services_busy(const p_browser_services *s) { return s != NULL && s->busy != 0; }
int p_services_register(HANDLE runtime, const PBrowserServiceOptions *options,
        p_browser_services **out_services)
{
    p_browser_services *s;
    PScriptJsonFunctionOptions native_options;
    unsigned long i;
    unsigned long j;
    int rc;
    if (options == NULL || options->size != sizeof(*options) ||
            options->version != PBROWSER_SERVICE_VERSION || options->tab_id == 0 ||
            options->page_generation == 0 || options->methods == NULL || options->submit == NULL ||
            options->cancel == NULL || options->method_count == 0 ||
            options->method_count > PBROWSER_SERVICE_MAX_METHODS) { return PBROWSER_ERROR_ARGUMENT; }
    for (i = 0; i < options->method_count; i++) {
        if (!p_service_method_valid(options->methods[i])) { return PBROWSER_ERROR_ARGUMENT; }
        for (j = 0; j < i; j++) {
            if (strcmp(options->methods[i], options->methods[j]) == 0) { return PBROWSER_ERROR_ARGUMENT; }
        }
    }
    s = (p_browser_services *) calloc(1, sizeof(*s));
    if (s == NULL) { return PBROWSER_ERROR_MEMORY; }
    s->bridge_id = p_service_new_serial();
    if (s->bridge_id == 0) { free(s); return PBROWSER_ERROR_LIMIT; }
    s->runtime = runtime;
    s->options = *options;
    s->options.methods = NULL;
    for (i = 0; i < options->method_count; i++) { strcpy(s->methods[i], options->methods[i]); }
    native_options.size = sizeof(native_options);
    native_options.version = PSCRIPT_JSON_FUNCTION_VERSION;
    native_options.result_capacity = PSCRIPT_JSON_FUNCTION_MAX_CAPACITY;
    rc = PScript_RegisterGlobalJsonFunction(runtime, SERVICE_SUBMIT, -1, p_service_submit, s);
    if (rc == PSCRIPT_OK) {
        rc = PScript_RegisterGlobalJsonFunctionEx(runtime, SERVICE_NEXT, -1,
                p_service_next, s, &native_options);
    }
    if (rc == PSCRIPT_OK) { rc = PScript_Evaluate(runtime, SERVICE_INSTALL, sizeof(SERVICE_INSTALL) - 1); }
    if (rc != PSCRIPT_OK) {
        /* A partial JS installer may retain native closures. Keep their
         * disabled binding alive until the owning runtime is destroyed. */
        s->enabled = 0;
        *out_services = s;
        return PBROWSER_ERROR_STATE;
    }
    s->enabled = 1;
    *out_services = s;
    return PBROWSER_OK;
}
int p_services_revoke(p_browser_services *s)
{
    PBrowserServiceToken token;
    unsigned long i;
    int rc;
    if (s == NULL) { return PBROWSER_ERROR_STATE; }
    if (!s->enabled) { return PBROWSER_OK; }
    s->enabled = 0;
    s->busy++;
    for (i = 0; i < PBROWSER_SERVICE_MAX_PENDING; i++) {
        if (s->pending[i].state == 1) {
            token.bridge_id = s->bridge_id;
            token.request_id = s->pending[i].id;
            s->options.cancel(s->options.pw, &token);
        }
        free(s->pending[i].result);
        memset(&s->pending[i], 0, sizeof(s->pending[i]));
    }
    s->clearing = 1;
    rc = PScript_CallGlobalJson(s->runtime, SERVICE_PUMP, -1, "[]", -1);
    s->clearing = 0;
    s->busy--;
    return rc == PSCRIPT_OK ? PBROWSER_OK : PBROWSER_ERROR_STATE;
}
void p_services_destroy(p_browser_services *s)
{
    if (s == NULL) { return; }
    p_services_revoke(s);
    /* Caller revoked before destroying runtime, and frees this binding only
     * AFTER engine finalizers have unwound. Disabled install failures also
     * retain their native binding until that point. */
    free(s);
}
int p_services_complete(p_browser_services *s, const PBrowserServiceToken *token,
        int succeeded, const char *json, unsigned long bytes)
{
    unsigned long i;
    char *copy;
    if (s == NULL || !s->enabled) { return PBROWSER_ERROR_STATE; }
    if (token == NULL || (succeeded != 0 && succeeded != 1) || json == NULL) { return PBROWSER_ERROR_ARGUMENT; }
    if (bytes > PBROWSER_SERVICE_JSON_MAX_BYTES) { return PBROWSER_ERROR_LIMIT; }
    if (!p_service_json_valid(json, bytes)) { return PBROWSER_ERROR_ARGUMENT; }
    if (token->bridge_id != s->bridge_id) { return PBROWSER_ERROR_NOT_FOUND; }
    for (i = 0; i < PBROWSER_SERVICE_MAX_PENDING; i++) {
        if (s->pending[i].state != 0 && s->pending[i].id == token->request_id) { break; }
    }
    if (i == PBROWSER_SERVICE_MAX_PENDING || s->pending[i].state != 1) { return PBROWSER_ERROR_NOT_FOUND; }
    if (s->next_order == 0xffffffffUL) { return PBROWSER_ERROR_LIMIT; }
    copy = (char *) malloc((size_t) bytes + 1);
    if (copy == NULL) { return PBROWSER_ERROR_MEMORY; }
    memcpy(copy, json, bytes);
    copy[bytes] = '\0';
    s->pending[i].result = copy;
    s->pending[i].succeeded = succeeded;
    s->pending[i].order = ++s->next_order;
    s->pending[i].state = 2;
    return PBROWSER_OK;
}
int p_services_pump(p_browser_services *s, unsigned long limit, unsigned long *out_delivered)
{
    int rc;
    if (s == NULL || !s->enabled) { return PBROWSER_ERROR_STATE; }
    if (limit == 0 || limit > PBROWSER_SERVICE_MAX_PENDING || out_delivered == NULL) { return PBROWSER_ERROR_ARGUMENT; }
    s->remaining = limit;
    s->delivered = 0;
    s->pumping = 1;
    s->busy++;
    rc = PScript_CallGlobalJson(s->runtime, SERVICE_PUMP, -1, "[]", -1);
    s->busy--;
    s->pumping = 0;
    *out_delivered = s->delivered;
    return rc == PSCRIPT_OK ? PBROWSER_OK : PBROWSER_ERROR_STATE;
}

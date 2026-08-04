/*
 * positron_script.c - Duktape-backed standalone JavaScript service.
 *
 * Duktape's NetSurf custom configuration supplies interrupt checks. The
 * wrapper supplies the WM-friendly deadline callback and keeps all result
 * storage inside the DLL-owned context.
 */

#include <windows.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "positron_script.h"
#define DUK_COMPILING_DUKTAPE
#include "duktape.h"
#undef DUK_COMPILING_DUKTAPE

typedef struct pscript_alloc_header {
    size_t bytes;
} pscript_alloc_header;

typedef struct pscript_context {
    duk_context *duk;
    unsigned long budget_ms;
    DWORD deadline;
    unsigned long memory_used;
    unsigned long evaluations;
    int timed_out;
    int poisoned;
    int fatal_jmp_active;
    jmp_buf fatal_jmp;
    char result[256];
    char error[256];
} pscript_context;

static void pscript_copy(char *out, int capacity, const char *value)
{
    size_t length;

    if (out == NULL || capacity <= 0) {
        return;
    }
    if (value == NULL) {
        out[0] = '\0';
        return;
    }
    length = strlen(value);
    if (length >= (size_t) capacity) {
        length = (size_t) capacity - 1;
    }
    memcpy(out, value, length);
    out[length] = '\0';
}

static void pscript_add_memory(pscript_context *ctx, size_t amount)
{
    unsigned long add;

    if (ctx == NULL) {
        return;
    }
    add = (amount > 0xffffffffUL) ? 0xffffffffUL : (unsigned long) amount;
    if (ctx->memory_used > 0xffffffffUL - add) {
        ctx->memory_used = 0xffffffffUL;
    } else {
        ctx->memory_used += add;
    }
}

static void pscript_sub_memory(pscript_context *ctx, size_t amount)
{
    unsigned long sub;

    if (ctx == NULL) {
        return;
    }
    sub = (amount > 0xffffffffUL) ? 0xffffffffUL : (unsigned long) amount;
    if (sub >= ctx->memory_used) {
        ctx->memory_used = 0;
    } else {
        ctx->memory_used -= sub;
    }
}

static void *pscript_alloc(void *udata, duk_size_t size)
{
    pscript_context *ctx;
    pscript_alloc_header *header;
    size_t bytes;

    ctx = (pscript_context *) udata;
    bytes = (size_t) size;
    if (bytes > (size_t) -1 - sizeof(*header)) {
        return NULL;
    }
    header = (pscript_alloc_header *) malloc(sizeof(*header) + bytes);
    if (header == NULL) {
        return NULL;
    }
    header->bytes = bytes;
    pscript_add_memory(ctx, bytes);
    return (void *) (header + 1);
}

static void *pscript_realloc(void *udata, void *ptr, duk_size_t size)
{
    pscript_context *ctx;
    pscript_alloc_header *old_header;
    pscript_alloc_header *new_header;
    size_t old_bytes;
    size_t new_bytes;

    ctx = (pscript_context *) udata;
    if (ptr == NULL) {
        return pscript_alloc(udata, size);
    }
    old_header = ((pscript_alloc_header *) ptr) - 1;
    old_bytes = old_header->bytes;
    new_bytes = (size_t) size;
    if (new_bytes > (size_t) -1 - sizeof(*new_header)) {
        return NULL;
    }
    if (new_bytes == 0) {
        free(old_header);
        pscript_sub_memory(ctx, old_bytes);
        return NULL;
    }
    new_header = (pscript_alloc_header *) realloc(old_header,
            sizeof(*new_header) + new_bytes);
    if (new_header == NULL) {
        return NULL;
    }
    new_header->bytes = new_bytes;
    if (new_bytes > old_bytes) {
        pscript_add_memory(ctx, new_bytes - old_bytes);
    } else {
        pscript_sub_memory(ctx, old_bytes - new_bytes);
    }
    return (void *) (new_header + 1);
}

static void pscript_free(void *udata, void *ptr)
{
    pscript_context *ctx;
    pscript_alloc_header *header;

    ctx = (pscript_context *) udata;
    if (ptr == NULL) {
        return;
    }
    header = ((pscript_alloc_header *) ptr) - 1;
    pscript_sub_memory(ctx, header->bytes);
    free(header);
}

/* Called by Duktape's NetSurf custom configuration at interrupt points. */
duk_bool_t dukky_check_timeout(void *udata)
{
    pscript_context *ctx;

    ctx = (pscript_context *) udata;
    if (ctx == NULL || ctx->budget_ms == 0 || ctx->deadline == 0) {
        return 0;
    }
    if ((LONG) (GetTickCount() - ctx->deadline) >= 0) {
        ctx->timed_out = 1;
        return 1;
    }
    return 0;
}

static void pscript_fatal(void *udata, const char *message)
{
    pscript_context *ctx;

    ctx = (pscript_context *) udata;
    if (ctx != NULL) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                (message != NULL) ? message : "Duktape fatal error");
        if (ctx->fatal_jmp_active) {
            longjmp(ctx->fatal_jmp, 1);
        }
    }
    /* Duktape requires a fatal handler not to return. This path means the
     * wrapper was called outside its protected create/evaluate window. */
    ExitThread(1);
}

PSCRIPT_API unsigned long PScript_AbiVersion(void)
{
    return PSCRIPT_ABI_VERSION;
}

PSCRIPT_API HANDLE PScript_Create(unsigned long budget_ms)
{
    pscript_context *ctx;

    ctx = (pscript_context *) malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->budget_ms = (budget_ms == 0) ? PSCRIPT_DEFAULT_BUDGET_MS : budget_ms;
    ctx->fatal_jmp_active = 1;
    if (setjmp(ctx->fatal_jmp) != 0) {
        ctx->fatal_jmp_active = 0;
        free(ctx);
        return NULL;
    }
    ctx->duk = duk_create_heap(pscript_alloc, pscript_realloc, pscript_free,
            ctx, pscript_fatal);
    ctx->fatal_jmp_active = 0;
    if (ctx->duk == NULL) {
        free(ctx);
        return NULL;
    }
    return (HANDLE) ctx;
}

PSCRIPT_API void PScript_Destroy(HANDLE hScript)
{
    pscript_context *ctx;

    ctx = (pscript_context *) hScript;
    if (ctx == NULL) {
        return;
    }
    if (ctx->duk != NULL && !ctx->poisoned) {
        duk_destroy_heap(ctx->duk);
    }
    free(ctx);
}

PSCRIPT_API int PScript_Evaluate(HANDLE hScript, const char *source,
        int source_len)
{
    pscript_context *ctx;
    size_t length;
    int rc;
    const char *text;

    ctx = (pscript_context *) hScript;
    if (ctx == NULL || ctx->duk == NULL || ctx->poisoned || source == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (source_len < 0) {
        length = strlen(source);
    } else {
        length = (size_t) source_len;
    }
    if (length > PSCRIPT_MAX_SOURCE_BYTES) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "source exceeds PSCRIPT_MAX_SOURCE_BYTES");
        ctx->result[0] = '\0';
        return PSCRIPT_ERROR_SOURCE_TOO_LARGE;
    }

    ctx->result[0] = '\0';
    ctx->error[0] = '\0';
    ctx->timed_out = 0;
    ctx->evaluations++;
    ctx->deadline = (ctx->budget_ms == 0) ? 0 :
            GetTickCount() + ctx->budget_ms;
    duk_set_top(ctx->duk, 0);
    ctx->fatal_jmp_active = 1;
    if (setjmp(ctx->fatal_jmp) != 0) {
        ctx->fatal_jmp_active = 0;
        ctx->poisoned = 1;
        ctx->duk = NULL;
        return PSCRIPT_ERROR_FATAL;
    }
    rc = duk_peval_lstring(ctx->duk, source, (duk_size_t) length);
    ctx->fatal_jmp_active = 0;
    ctx->deadline = 0;
    if (rc != 0) {
        text = duk_safe_to_string(ctx->duk, -1);
        if (ctx->timed_out) {
            pscript_copy(ctx->error, sizeof(ctx->error),
                    "JavaScript execution timeout");
            rc = PSCRIPT_ERROR_TIMEOUT;
        } else {
            pscript_copy(ctx->error, sizeof(ctx->error), text);
            rc = PSCRIPT_ERROR_EVALUATION;
        }
        duk_set_top(ctx->duk, 0);
        return rc;
    }
    text = duk_safe_to_string(ctx->duk, -1);
    pscript_copy(ctx->result, sizeof(ctx->result), text);
    duk_set_top(ctx->duk, 0);
    return PSCRIPT_OK;
}

PSCRIPT_API const char *PScript_GetResult(HANDLE hScript)
{
    pscript_context *ctx;

    ctx = (pscript_context *) hScript;
    return (ctx != NULL) ? ctx->result : "";
}

PSCRIPT_API const char *PScript_GetError(HANDLE hScript)
{
    pscript_context *ctx;

    ctx = (pscript_context *) hScript;
    return (ctx != NULL) ? ctx->error : "invalid script context";
}

PSCRIPT_API unsigned long PScript_GetMemoryUsed(HANDLE hScript)
{
    pscript_context *ctx;

    ctx = (pscript_context *) hScript;
    return (ctx != NULL) ? ctx->memory_used : 0;
}

PSCRIPT_API unsigned long PScript_GetEvaluationCount(HANDLE hScript)
{
    pscript_context *ctx;

    ctx = (pscript_context *) hScript;
    return (ctx != NULL) ? ctx->evaluations : 0;
}

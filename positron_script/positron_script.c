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

#define PSCRIPT_MODULES_GLOBAL "__positron_internal_modules"
#define PSCRIPT_REQUIRE_GLOBAL "__positron_internal_require"
#define PSCRIPT_CONTEXT_STASH "__positron_internal_context"

typedef struct pscript_alloc_header {
    size_t bytes;
} pscript_alloc_header;

typedef struct pscript_context pscript_context;

struct pscript_context {
    duk_context *duk;
    unsigned long budget_ms;
    unsigned long memory_limit;
    DWORD deadline;
    unsigned long memory_used;
    unsigned long peak_memory_used;
    unsigned long evaluations;
    int timed_out;
    int memory_limited;
    int poisoned;
    unsigned long module_count;
    PScriptModuleSourceFn source_fn;
    PScriptModuleSourceFreeFn source_free_fn;
    void *source_pw;
    int fatal_jmp_active;
    jmp_buf fatal_jmp;
    char result[256];
    char error[256];
};

static duk_ret_t pscript_require(duk_context *duk);
static int pscript_module_evaluate(pscript_context *ctx, const char *name,
        const char *source, size_t source_length);

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

static int pscript_module_name_copy(const char *source, int source_len,
        char *destination, size_t destination_size, size_t *out_length)
{
    size_t length;

    if (source == NULL || destination == NULL || destination_size < 2) {
        return 0;
    }
    if (source_len < 0) {
        length = strlen(source);
    } else {
        length = (size_t) source_len;
    }
    if (length == 0 || length >= destination_size ||
            length > PSCRIPT_MAX_MODULE_NAME_BYTES) {
        return 0;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
    if (out_length != NULL) {
        *out_length = length;
    }
    return 1;
}

static int pscript_module_wrapper(const char *source, size_t source_len,
        char **out_source, size_t *out_length)
{
    static const char prefix[] =
            "(function(module, exports, require) {\n";
    static const char suffix[] = "\n})";
    char *wrapper;
    size_t prefix_len;
    size_t suffix_len;
    size_t total;

    if (source == NULL || out_source == NULL || out_length == NULL) {
        return 0;
    }
    prefix_len = sizeof(prefix) - 1;
    suffix_len = sizeof(suffix) - 1;
    if (source_len > (size_t) -1 - prefix_len ||
            source_len + prefix_len > (size_t) -1 - suffix_len ||
            source_len + prefix_len + suffix_len == (size_t) -1) {
        return 0;
    }
    total = prefix_len + source_len + suffix_len;
    wrapper = (char *) malloc(total + 1);
    if (wrapper == NULL) {
        return 0;
    }
    memcpy(wrapper, prefix, prefix_len);
    memcpy(wrapper + prefix_len, source, source_len);
    memcpy(wrapper + prefix_len + source_len, suffix, suffix_len);
    wrapper[total] = '\0';
    *out_source = wrapper;
    *out_length = total;
    return 1;
}

static int pscript_modules_ensure(pscript_context *ctx)
{
    duk_context *duk;

    if (ctx == NULL || ctx->duk == NULL) {
        return 0;
    }
    duk = ctx->duk;
    duk_set_top(duk, 0);
    duk_get_global_string(duk, PSCRIPT_MODULES_GLOBAL);
    if (duk_is_object(duk, -1)) {
        duk_pop(duk);
        duk_get_global_string(duk, PSCRIPT_REQUIRE_GLOBAL);
        if (!duk_is_callable(duk, -1)) {
            duk_pop(duk);
            duk_push_c_function(duk, pscript_require, 1);
            duk_put_global_string(duk, PSCRIPT_REQUIRE_GLOBAL);
        } else {
            duk_pop(duk);
        }
        duk_push_heap_stash(duk);
        duk_push_pointer(duk, ctx);
        duk_put_prop_string(duk, -2, PSCRIPT_CONTEXT_STASH);
        duk_pop(duk);
        return 1;
    }
    duk_pop(duk);
    duk_push_bare_object(duk);
    duk_put_global_string(duk, PSCRIPT_MODULES_GLOBAL);
    duk_push_c_function(duk, pscript_require, 1);
    duk_put_global_string(duk, PSCRIPT_REQUIRE_GLOBAL);
    duk_push_heap_stash(duk);
    duk_push_pointer(duk, ctx);
    duk_put_prop_string(duk, -2, PSCRIPT_CONTEXT_STASH);
    duk_pop(duk);
    return 1;
}

static void pscript_module_remove_at(pscript_context *ctx, const char *name,
        duk_idx_t base)
{
    duk_context *duk;

    if (ctx == NULL || ctx->duk == NULL || name == NULL) {
        return;
    }
    duk = ctx->duk;
    duk_set_top(duk, base);
    duk_get_global_string(duk, PSCRIPT_MODULES_GLOBAL);
    if (duk_is_object(duk, -1)) {
        duk_del_prop_string(duk, -1, name);
    }
    duk_set_top(duk, base);
    if (ctx->module_count > 0) {
        ctx->module_count--;
    }
}

static int pscript_module_cached_export(pscript_context *ctx,
        const char *name, duk_idx_t base)
{
    duk_context *duk;

    if (ctx == NULL || ctx->duk == NULL || name == NULL) {
        return 0;
    }
    duk = ctx->duk;
    duk_get_global_string(duk, PSCRIPT_MODULES_GLOBAL);
    if (!duk_is_object(duk, -1) ||
            !duk_get_prop_string(duk, -1, name) ||
            !duk_is_object(duk, -1)) {
        duk_set_top(duk, base);
        return 0;
    }
    duk_get_prop_string(duk, -1, "exports");
    duk_remove(duk, base);
    duk_remove(duk, base);
    return 1;
}

static duk_ret_t pscript_require(duk_context *duk)
{
    pscript_context *ctx;
    const char *name;
    char *source;
    int source_len;
    int rc;
    duk_idx_t base;

    if (duk == NULL || !duk_is_string(duk, 0)) {
        return duk_error(duk, DUK_ERR_TYPE_ERROR,
                "module name must be a string");
    }
    name = duk_get_string(duk, 0);
    if (name == NULL || strlen(name) == 0 ||
            strlen(name) > PSCRIPT_MAX_MODULE_NAME_BYTES) {
        return duk_error(duk, DUK_ERR_ERROR, "invalid module name");
    }
    base = duk_get_top(duk);
    duk_get_global_string(duk, PSCRIPT_MODULES_GLOBAL);
    if (duk_is_object(duk, -1) &&
            duk_get_prop_string(duk, -1, name) &&
            duk_is_object(duk, -1)) {
        duk_get_prop_string(duk, -1, "exports");
        duk_remove(duk, base);
        duk_remove(duk, base);
        return 1;
    }
    duk_set_top(duk, base);

    duk_push_heap_stash(duk);
    duk_get_prop_string(duk, -1, PSCRIPT_CONTEXT_STASH);
    ctx = (pscript_context *) duk_get_pointer(duk, -1);
    duk_set_top(duk, base);
    if (ctx == NULL || ctx->source_fn == NULL) {
        return duk_error(duk, DUK_ERR_ERROR,
                "module is not loaded");
    }

    source = NULL;
    source_len = 0;
    rc = ctx->source_fn(ctx->source_pw, name, &source, &source_len);
    if (rc != 0 || source == NULL || source_len < 0) {
        if (source != NULL && ctx->source_free_fn != NULL) {
            ctx->source_free_fn(ctx->source_pw, source);
        }
        pscript_copy(ctx->error, sizeof(ctx->error),
                "module source provider failed");
        return duk_error(duk, DUK_ERR_ERROR,
                "module source unavailable");
    }
    rc = pscript_module_evaluate(ctx, name, source,
            (size_t) source_len);
    if (ctx->source_free_fn != NULL) {
        ctx->source_free_fn(ctx->source_pw, source);
    }
    if (rc != PSCRIPT_OK) {
        return duk_error(duk, DUK_ERR_ERROR, "%s", ctx->error);
    }
    return 1;
}

static int pscript_memory_reserve(pscript_context *ctx, size_t amount)
{
    unsigned long add;

    if (ctx == NULL) {
        return 0;
    }
    add = (amount > 0xffffffffUL) ? 0xffffffffUL : (unsigned long) amount;
    if (amount > 0xffffffffUL ||
            ctx->memory_used > ctx->memory_limit ||
            add > ctx->memory_limit - ctx->memory_used) {
        ctx->memory_limited = 1;
        return 0;
    }
    return 1;
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
    if (ctx->memory_used > ctx->peak_memory_used) {
        ctx->peak_memory_used = ctx->memory_used;
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
    size_t total;

    ctx = (pscript_context *) udata;
    bytes = (size_t) size;
    if (bytes > (size_t) -1 - sizeof(*header)) {
        return NULL;
    }
    total = sizeof(*header) + bytes;
    if (!pscript_memory_reserve(ctx, total)) {
        return NULL;
    }
    header = (pscript_alloc_header *) malloc(total);
    if (header == NULL) {
        return NULL;
    }
    header->bytes = bytes;
    pscript_add_memory(ctx, total);
    return (void *) (header + 1);
}

static void *pscript_realloc(void *udata, void *ptr, duk_size_t size)
{
    pscript_context *ctx;
    pscript_alloc_header *old_header;
    pscript_alloc_header *new_header;
    size_t old_bytes;
    size_t new_bytes;
    size_t old_total;
    size_t new_total;

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
    old_total = sizeof(*old_header) + old_bytes;
    new_total = sizeof(*new_header) + new_bytes;
    if (new_bytes == 0) {
        free(old_header);
        pscript_sub_memory(ctx, old_total);
        return NULL;
    }
    if (new_total > old_total &&
            !pscript_memory_reserve(ctx, new_total - old_total)) {
        return NULL;
    }
    new_header = (pscript_alloc_header *) realloc(old_header,
            new_total);
    if (new_header == NULL) {
        return NULL;
    }
    new_header->bytes = new_bytes;
    if (new_total > old_total) {
        pscript_add_memory(ctx, new_total - old_total);
    } else if (old_total > new_total) {
        pscript_sub_memory(ctx, old_total - new_total);
    }
    return (void *) (new_header + 1);
}

static void pscript_free(void *udata, void *ptr)
{
    pscript_context *ctx;
    pscript_alloc_header *header;
    size_t total;

    ctx = (pscript_context *) udata;
    if (ptr == NULL) {
        return;
    }
    header = ((pscript_alloc_header *) ptr) - 1;
    total = sizeof(*header) + header->bytes;
    pscript_sub_memory(ctx, total);
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
        if (ctx->memory_limited) {
            pscript_copy(ctx->error, sizeof(ctx->error),
                    "JavaScript memory limit exceeded");
        } else {
            pscript_copy(ctx->error, sizeof(ctx->error),
                    (message != NULL) ? message : "Duktape fatal error");
        }
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
    return PScript_CreateEx(budget_ms, 0);
}

PSCRIPT_API HANDLE PScript_CreateEx(unsigned long budget_ms,
        unsigned long memory_limit_bytes)
{
    pscript_context *ctx;

    ctx = (pscript_context *) malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->budget_ms = (budget_ms == 0) ? PSCRIPT_DEFAULT_BUDGET_MS : budget_ms;
    ctx->memory_limit = (memory_limit_bytes == 0) ?
            PSCRIPT_DEFAULT_MEMORY_LIMIT_BYTES : memory_limit_bytes;
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
    ctx->memory_limited = 0;
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
        if (ctx->memory_limited) {
            pscript_copy(ctx->error, sizeof(ctx->error),
                    "JavaScript memory limit exceeded");
            rc = PSCRIPT_ERROR_MEMORY_LIMIT;
        } else {
            text = duk_safe_to_string(ctx->duk, -1);
            if (ctx->timed_out) {
                pscript_copy(ctx->error, sizeof(ctx->error),
                        "JavaScript execution timeout");
                rc = PSCRIPT_ERROR_TIMEOUT;
            } else {
                pscript_copy(ctx->error, sizeof(ctx->error), text);
                rc = PSCRIPT_ERROR_EVALUATION;
            }
        }
        duk_set_top(ctx->duk, 0);
        return rc;
    }
    text = duk_safe_to_string(ctx->duk, -1);
    pscript_copy(ctx->result, sizeof(ctx->result), text);
    duk_set_top(ctx->duk, 0);
    return PSCRIPT_OK;
}

static int pscript_module_eval_error(pscript_context *ctx)
{
    const char *text;

    if (ctx->memory_limited) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "JavaScript memory limit exceeded");
        return PSCRIPT_ERROR_MEMORY_LIMIT;
    }
    text = duk_safe_to_string(ctx->duk, -1);
    if (ctx->timed_out) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "JavaScript execution timeout");
        return PSCRIPT_ERROR_TIMEOUT;
    }
    pscript_copy(ctx->error, sizeof(ctx->error), text);
    return PSCRIPT_ERROR_EVALUATION;
}

/* Evaluate while preserving the caller's Duktape value stack. The public
 * entry starts at stack base zero; require() re-enters here with its own
 * argument frame still below the base. On success exactly one exports value
 * remains above that base. */
static int pscript_module_evaluate(pscript_context *ctx, const char *name,
        const char *source, size_t source_length)
{
    duk_context *duk;
    char *wrapper;
    size_t wrapper_length;
    duk_idx_t base;
    int rc;

    if (ctx == NULL || ctx->duk == NULL || ctx->poisoned ||
            name == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    duk = ctx->duk;
    base = duk_get_top(duk);
    if (pscript_module_cached_export(ctx, name, base)) {
        return PSCRIPT_OK;
    }
    if (source == NULL) {
        duk_set_top(duk, base);
        pscript_copy(ctx->error, sizeof(ctx->error),
                "module source unavailable");
        return PSCRIPT_ERROR_MODULE_SOURCE;
    }
    if (source_length > PSCRIPT_MAX_SOURCE_BYTES) {
        duk_set_top(duk, base);
        pscript_copy(ctx->error, sizeof(ctx->error),
                "source exceeds PSCRIPT_MAX_SOURCE_BYTES");
        return PSCRIPT_ERROR_SOURCE_TOO_LARGE;
    }
    if (!pscript_module_wrapper(source, source_length, &wrapper,
            &wrapper_length)) {
        duk_set_top(duk, base);
        pscript_copy(ctx->error, sizeof(ctx->error),
                "module wrapper allocation failed");
        return PSCRIPT_ERROR_EVALUATION;
    }

    if (ctx->module_count >= PSCRIPT_MAX_MODULES) {
        duk_set_top(duk, base);
        free(wrapper);
        pscript_copy(ctx->error, sizeof(ctx->error),
                "module cache limit exceeded");
        return PSCRIPT_ERROR_MODULE_LIMIT;
    }

    duk_get_global_string(duk, PSCRIPT_MODULES_GLOBAL);
    if (!duk_is_object(duk, -1)) {
        duk_set_top(duk, base);
        free(wrapper);
        pscript_copy(ctx->error, sizeof(ctx->error),
                "module registry unavailable");
        return PSCRIPT_ERROR_FATAL;
    }
    duk_push_object(duk);
    duk_push_object(duk);
    duk_put_prop_string(duk, -2, "exports");
    duk_dup(duk, -1);
    duk_put_prop_string(duk, -3, name);
    ctx->module_count++;
    ctx->evaluations++;
    ctx->deadline = (ctx->budget_ms == 0) ? 0 :
            GetTickCount() + ctx->budget_ms;

    rc = duk_peval_lstring(duk, wrapper, (duk_size_t) wrapper_length);
    free(wrapper);
    wrapper = NULL;
    if (rc != 0) {
        rc = pscript_module_eval_error(ctx);
        pscript_module_remove_at(ctx, name, base);
        return rc;
    }

    duk_dup(duk, -2);
    duk_get_prop_string(duk, -1, "exports");
    duk_get_global_string(duk, PSCRIPT_REQUIRE_GLOBAL);
    rc = duk_pcall(duk, 3);
    if (rc != 0) {
        rc = pscript_module_eval_error(ctx);
        pscript_module_remove_at(ctx, name, base);
        return rc;
    }

    duk_get_prop_string(duk, -2, "exports");
    duk_remove(duk, base);
    duk_remove(duk, base);
    duk_remove(duk, base);
    return PSCRIPT_OK;
}

PSCRIPT_API int PScript_EvaluateModule(HANDLE hScript,
        const char *module_name, int module_name_len,
        const char *source, int source_len)
{
    pscript_context *ctx;
    duk_context *duk;
    char name[PSCRIPT_MAX_MODULE_NAME_BYTES + 1];
    size_t source_length;
    int rc;
    const char *text;

    ctx = (pscript_context *) hScript;
    if (ctx == NULL || ctx->duk == NULL || ctx->poisoned ||
            module_name == NULL || source == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (!pscript_module_name_copy(module_name, module_name_len,
            name, sizeof(name), NULL)) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "invalid module name");
        return PSCRIPT_ERROR_MODULE_NAME;
    }
    if (source_len < 0) {
        source_length = strlen(source);
    } else {
        source_length = (size_t) source_len;
    }
    if (source_length > PSCRIPT_MAX_SOURCE_BYTES) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "source exceeds PSCRIPT_MAX_SOURCE_BYTES");
        ctx->result[0] = '\0';
        return PSCRIPT_ERROR_SOURCE_TOO_LARGE;
    }

    ctx->result[0] = '\0';
    ctx->error[0] = '\0';
    ctx->timed_out = 0;
    ctx->memory_limited = 0;
    duk = ctx->duk;
    ctx->fatal_jmp_active = 1;
    if (setjmp(ctx->fatal_jmp) != 0) {
        ctx->fatal_jmp_active = 0;
        ctx->deadline = 0;
        ctx->poisoned = 1;
        ctx->duk = NULL;
        if (ctx->memory_limited) {
            pscript_copy(ctx->error, sizeof(ctx->error),
                    "JavaScript memory limit exceeded");
            return PSCRIPT_ERROR_MEMORY_LIMIT;
        }
        pscript_copy(ctx->error, sizeof(ctx->error),
                "Duktape fatal error");
        return PSCRIPT_ERROR_FATAL;
    }
    if (!pscript_modules_ensure(ctx)) {
        ctx->fatal_jmp_active = 0;
        pscript_copy(ctx->error, sizeof(ctx->error),
                "module registry unavailable");
        return PSCRIPT_ERROR_FATAL;
    }
    ctx->deadline = (ctx->budget_ms == 0) ? 0 :
            GetTickCount() + ctx->budget_ms;
    rc = pscript_module_evaluate(ctx, name, source, source_length);
    ctx->deadline = 0;
    ctx->fatal_jmp_active = 0;
    if (rc != PSCRIPT_OK) {
        duk_set_top(duk, 0);
        return rc;
    }
    text = duk_safe_to_string(duk, -1);
    pscript_copy(ctx->result, sizeof(ctx->result), text);
    duk_set_top(duk, 0);
    return PSCRIPT_OK;
}

PSCRIPT_API int PScript_SetModuleSourceProvider(HANDLE hScript,
        PScriptModuleSourceFn source_fn,
        PScriptModuleSourceFreeFn free_fn, void *pw)
{
    pscript_context *ctx;

    ctx = (pscript_context *) hScript;
    if (ctx == NULL || ctx->duk == NULL || ctx->poisoned) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if ((source_fn == NULL) != (free_fn == NULL)) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    ctx->source_fn = source_fn;
    ctx->source_free_fn = free_fn;
    ctx->source_pw = pw;
    return PSCRIPT_OK;
}

PSCRIPT_API int PScript_LoadModule(HANDLE hScript,
        const char *module_name, int module_name_len)
{
    pscript_context *ctx;
    duk_context *duk;
    char name[PSCRIPT_MAX_MODULE_NAME_BYTES + 1];
    char *source;
    int source_len;
    int rc;
    const char *text;

    ctx = (pscript_context *) hScript;
    if (ctx == NULL || ctx->duk == NULL || ctx->poisoned ||
            module_name == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (!pscript_module_name_copy(module_name, module_name_len,
            name, sizeof(name), NULL)) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "invalid module name");
        return PSCRIPT_ERROR_MODULE_NAME;
    }

    ctx->result[0] = '\0';
    ctx->error[0] = '\0';
    ctx->timed_out = 0;
    ctx->memory_limited = 0;
    duk = ctx->duk;
    ctx->fatal_jmp_active = 1;
    if (setjmp(ctx->fatal_jmp) != 0) {
        ctx->fatal_jmp_active = 0;
        ctx->deadline = 0;
        ctx->poisoned = 1;
        ctx->duk = NULL;
        if (ctx->memory_limited) {
            pscript_copy(ctx->error, sizeof(ctx->error),
                    "JavaScript memory limit exceeded");
            return PSCRIPT_ERROR_MEMORY_LIMIT;
        }
        pscript_copy(ctx->error, sizeof(ctx->error),
                "Duktape fatal error");
        return PSCRIPT_ERROR_FATAL;
    }
    if (!pscript_modules_ensure(ctx)) {
        ctx->fatal_jmp_active = 0;
        pscript_copy(ctx->error, sizeof(ctx->error),
                "module registry unavailable");
        return PSCRIPT_ERROR_FATAL;
    }
    if (pscript_module_cached_export(ctx, name, 0)) {
        text = duk_safe_to_string(duk, -1);
        pscript_copy(ctx->result, sizeof(ctx->result), text);
        duk_set_top(duk, 0);
        ctx->fatal_jmp_active = 0;
        return PSCRIPT_OK;
    }
    if (ctx->source_fn == NULL) {
        ctx->fatal_jmp_active = 0;
        pscript_copy(ctx->error, sizeof(ctx->error),
                "module source provider unavailable");
        return PSCRIPT_ERROR_MODULE_SOURCE;
    }

    source = NULL;
    source_len = 0;
    ctx->deadline = (ctx->budget_ms == 0) ? 0 :
            GetTickCount() + ctx->budget_ms;
    rc = ctx->source_fn(ctx->source_pw, name, &source, &source_len);
    if (rc != 0 || source == NULL || source_len < 0) {
        if (source != NULL && ctx->source_free_fn != NULL) {
            ctx->source_free_fn(ctx->source_pw, source);
        }
        ctx->deadline = 0;
        ctx->fatal_jmp_active = 0;
        pscript_copy(ctx->error, sizeof(ctx->error),
                "module source provider failed");
        return PSCRIPT_ERROR_MODULE_SOURCE;
    }
    rc = pscript_module_evaluate(ctx, name, source,
            (size_t) source_len);
    if (ctx->source_free_fn != NULL) {
        ctx->source_free_fn(ctx->source_pw, source);
    }
    ctx->deadline = 0;
    ctx->fatal_jmp_active = 0;
    if (rc != PSCRIPT_OK) {
        duk_set_top(duk, 0);
        return rc;
    }
    text = duk_safe_to_string(duk, -1);
    pscript_copy(ctx->result, sizeof(ctx->result), text);
    duk_set_top(duk, 0);
    return PSCRIPT_OK;
}

PSCRIPT_API int PScript_ClearModules(HANDLE hScript)
{
    pscript_context *ctx;
    duk_context *duk;

    ctx = (pscript_context *) hScript;
    if (ctx == NULL || ctx->duk == NULL || ctx->poisoned) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    duk = ctx->duk;
    ctx->fatal_jmp_active = 1;
    if (setjmp(ctx->fatal_jmp) != 0) {
        ctx->fatal_jmp_active = 0;
        ctx->poisoned = 1;
        ctx->duk = NULL;
        pscript_copy(ctx->error, sizeof(ctx->error),
                "Duktape fatal error while clearing modules");
        return PSCRIPT_ERROR_FATAL;
    }
    duk_set_top(duk, 0);
    duk_push_bare_object(duk);
    duk_put_global_string(duk, PSCRIPT_MODULES_GLOBAL);
    if (!pscript_modules_ensure(ctx)) {
        ctx->fatal_jmp_active = 0;
        pscript_copy(ctx->error, sizeof(ctx->error),
                "module registry unavailable");
        return PSCRIPT_ERROR_FATAL;
    }
    ctx->module_count = 0;
    ctx->result[0] = '\0';
    ctx->error[0] = '\0';
    ctx->fatal_jmp_active = 0;
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

PSCRIPT_API unsigned long PScript_GetPeakMemoryUsed(HANDLE hScript)
{
    pscript_context *ctx;

    ctx = (pscript_context *) hScript;
    return (ctx != NULL) ? ctx->peak_memory_used : 0;
}

PSCRIPT_API unsigned long PScript_GetMemoryLimit(HANDLE hScript)
{
    pscript_context *ctx;

    ctx = (pscript_context *) hScript;
    return (ctx != NULL) ? ctx->memory_limit : 0;
}

PSCRIPT_API unsigned long PScript_GetEvaluationCount(HANDLE hScript)
{
    pscript_context *ctx;

    ctx = (pscript_context *) hScript;
    return (ctx != NULL) ? ctx->evaluations : 0;
}

PSCRIPT_API unsigned long PScript_GetModuleCount(HANDLE hScript)
{
    pscript_context *ctx;

    ctx = (pscript_context *) hScript;
    return (ctx != NULL) ? ctx->module_count : 0;
}

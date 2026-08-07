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
#define PSCRIPT_NATIVE_NAME_PROP "__positron_native_name"

typedef struct pscript_alloc_header {
    size_t bytes;
} pscript_alloc_header;

typedef struct pscript_context pscript_context;

typedef struct pscript_native_function {
    int active;
    char name[PSCRIPT_MAX_GLOBAL_NAME_BYTES + 1];
    PScriptJsonFunctionFn fn;
    void *pw;
} pscript_native_function;

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
    pscript_native_function native_functions[PSCRIPT_MAX_NATIVE_FUNCTIONS];
    unsigned long native_count;
    int fatal_jmp_active;
    jmp_buf fatal_jmp;
    char result[256];
    char error[256];
};

static duk_ret_t pscript_require(duk_context *duk);
static duk_ret_t pscript_native_dispatch(duk_context *duk);
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

static int pscript_name_copy(const char *source, int source_len,
        char *destination, size_t destination_size, size_t max_length,
        size_t *out_length)
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
            length > max_length) {
        return 0;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
    if (out_length != NULL) {
        *out_length = length;
    }
    return 1;
}

static int pscript_module_name_copy(const char *source, int source_len,
        char *destination, size_t destination_size, size_t *out_length)
{
    return pscript_name_copy(source, source_len, destination,
            destination_size, PSCRIPT_MAX_MODULE_NAME_BYTES, out_length);
}

static int pscript_global_name_copy(const char *source, int source_len,
        char *destination, size_t destination_size, size_t *out_length)
{
    return pscript_name_copy(source, source_len, destination,
            destination_size, PSCRIPT_MAX_GLOBAL_NAME_BYTES, out_length);
}

static int pscript_native_find(pscript_context *ctx, const char *name)
{
    int i;

    if (ctx == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < (int) PSCRIPT_MAX_NATIVE_FUNCTIONS; i++) {
        if (ctx->native_functions[i].active &&
                strcmp(ctx->native_functions[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int pscript_native_free_slot(pscript_context *ctx)
{
    int i;

    if (ctx == NULL) {
        return -1;
    }
    for (i = 0; i < (int) PSCRIPT_MAX_NATIVE_FUNCTIONS; i++) {
        if (!ctx->native_functions[i].active) {
            return i;
        }
    }
    return -1;
}

static int pscript_context_stash_set(pscript_context *ctx)
{
    duk_context *duk;

    if (ctx == NULL || ctx->duk == NULL) {
        return 0;
    }
    duk = ctx->duk;
    duk_push_heap_stash(duk);
    duk_push_pointer(duk, ctx);
    duk_put_prop_string(duk, -2, PSCRIPT_CONTEXT_STASH);
    duk_pop(duk);
    return 1;
}

static duk_ret_t pscript_native_dispatch(duk_context *duk)
{
    pscript_context *ctx;
    pscript_native_function *native;
    duk_idx_t base;
    const char *name;
    const char *args_json;
    char native_name[PSCRIPT_MAX_GLOBAL_NAME_BYTES + 1];
    char output[256];
    int native_index;
    int args_len;
    int out_len;
    int callback_rc;
    int i;

    if (duk == NULL) {
        return 0;
    }
    base = duk_get_top(duk);
    native_name[0] = '\0';
    duk_push_current_function(duk);
    if (!duk_get_prop_string(duk, -1, PSCRIPT_NATIVE_NAME_PROP) ||
            !duk_is_string(duk, -1)) {
        duk_set_top(duk, base);
        return duk_error(duk, DUK_ERR_ERROR,
                "native callback name is missing");
    }
    name = duk_get_string(duk, -1);
    if (!pscript_global_name_copy(name, -1, native_name,
            sizeof(native_name), NULL)) {
        duk_set_top(duk, base);
        return duk_error(duk, DUK_ERR_ERROR,
                "native callback name is invalid");
    }
    duk_set_top(duk, base);

    duk_push_heap_stash(duk);
    if (!duk_get_prop_string(duk, -1, PSCRIPT_CONTEXT_STASH)) {
        duk_set_top(duk, base);
        return duk_error(duk, DUK_ERR_ERROR,
                "native callback context is missing");
    }
    ctx = (pscript_context *) duk_get_pointer(duk, -1);
    duk_set_top(duk, base);
    if (ctx == NULL || ctx->duk != duk || ctx->poisoned) {
        return duk_error(duk, DUK_ERR_ERROR,
                "native callback context is invalid");
    }
    native_index = pscript_native_find(ctx, native_name);
    if (native_index < 0) {
        return duk_error(duk, DUK_ERR_ERROR,
                "native callback is not registered");
    }
    native = &ctx->native_functions[native_index];
    if (native->fn == NULL) {
        return duk_error(duk, DUK_ERR_ERROR,
                "native callback is unavailable");
    }

    duk_push_array(duk);
    for (i = 0; i < (int) base; i++) {
        duk_dup(duk, i);
        duk_put_prop_index(duk, base, (duk_uarridx_t) i);
    }
    duk_json_encode(duk, base);
    if (!duk_is_string(duk, base)) {
        return duk_error(duk, DUK_ERR_ERROR,
                "native callback arguments are not JSON");
    }
    args_json = duk_get_string(duk, base);
    if (args_json == NULL) {
        return duk_error(duk, DUK_ERR_ERROR,
                "native callback arguments are empty");
    }
    args_len = (int) strlen(args_json);
    if (args_len < 0 || args_len > (int) PSCRIPT_MAX_SOURCE_BYTES) {
        return duk_error(duk, DUK_ERR_ERROR,
                "native callback arguments are too large");
    }

    output[0] = '\0';
    out_len = -1;
    callback_rc = native->fn(native->pw, args_json, args_len, output,
            (int) sizeof(output), &out_len);
    if (callback_rc != 0) {
        return duk_error(duk, DUK_ERR_ERROR,
                "native callback failed");
    }
    if (out_len < 0 || out_len >= (int) sizeof(output)) {
        return duk_error(duk, DUK_ERR_ERROR,
                "native callback JSON result is invalid");
    }
    output[out_len] = '\0';
    duk_push_lstring(duk, output, (duk_size_t) out_len);
    duk_json_decode(duk, -1);
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

static int pscript_protected_error(pscript_context *ctx, int error_code)
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
    return error_code;
}

static int pscript_module_eval_error(pscript_context *ctx)
{
    return pscript_protected_error(ctx, PSCRIPT_ERROR_EVALUATION);
}

static duk_ret_t pscript_json_decode(duk_context *duk)
{
    duk_json_decode(duk, 0);
    return 1;
}

static duk_ret_t pscript_json_encode(duk_context *duk)
{
    duk_json_encode(duk, 0);
    return 1;
}

/* Encode through a protected Duktape call. JSON encoding can throw for a
 * cyclic object, so calling duk_json_encode directly from an exported ABI
 * function would turn a recoverable host error into a fatal context. */
static int pscript_capture_json(pscript_context *ctx, duk_idx_t index)
{
    duk_context *duk;
    duk_idx_t value_index;
    duk_idx_t top;
    int rc;
    const char *text;
    size_t length;

    if (ctx == NULL || ctx->duk == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    duk = ctx->duk;
    top = duk_get_top(duk);
    value_index = index;
    if (value_index < 0) {
        value_index = top + value_index;
    }
    if (value_index < 0 || value_index >= top) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "JSON value stack index is invalid");
        return PSCRIPT_ERROR_JSON;
    }
    duk_push_c_function(duk, pscript_json_encode, 1);
    duk_dup(duk, value_index);
    rc = duk_pcall(duk, 1);
    if (rc != 0) {
        return pscript_protected_error(ctx, PSCRIPT_ERROR_JSON);
    }
    if (!duk_is_string(duk, -1)) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "value has no JSON representation");
        return PSCRIPT_ERROR_JSON;
    }
    text = duk_get_string(duk, -1);
    if (text == NULL) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "JSON encoder returned no string");
        return PSCRIPT_ERROR_JSON;
    }
    length = strlen(text);
    if (length >= sizeof(ctx->result)) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "JSON result exceeds DLL result buffer");
        return PSCRIPT_ERROR_RESULT_TOO_LARGE;
    }
    memcpy(ctx->result, text, length + 1);
    return PSCRIPT_OK;
}

static int pscript_global_prepare(pscript_context *ctx, const char *name,
        int name_len, char *name_copy, size_t name_copy_size)
{
    if (ctx == NULL || ctx->duk == NULL || ctx->poisoned ||
            name == NULL || name_copy == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (!pscript_global_name_copy(name, name_len, name_copy,
            name_copy_size, NULL)) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "invalid global name");
        return PSCRIPT_ERROR_GLOBAL;
    }
    ctx->result[0] = '\0';
    ctx->error[0] = '\0';
    duk_set_top(ctx->duk, 0);
    return PSCRIPT_OK;
}

static int pscript_fatal_error(pscript_context *ctx, const char *fallback)
{
    ctx->fatal_jmp_active = 0;
    ctx->poisoned = 1;
    ctx->duk = NULL;
    if (ctx->memory_limited) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "JavaScript memory limit exceeded");
        return PSCRIPT_ERROR_MEMORY_LIMIT;
    }
    pscript_copy(ctx->error, sizeof(ctx->error), fallback);
    return PSCRIPT_ERROR_FATAL;
}

PSCRIPT_API int PScript_SetGlobalString(HANDLE hScript, const char *name,
        int name_len, const char *value, int value_len)
{
    pscript_context *ctx;
    duk_context *duk;
    char global_name[PSCRIPT_MAX_GLOBAL_NAME_BYTES + 1];
    size_t length;
    int rc;

    ctx = (pscript_context *) hScript;
    if (value == NULL) {
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (value_len < 0) {
        length = strlen(value);
    } else {
        length = (size_t) value_len;
    }
    rc = pscript_global_prepare(ctx, name, name_len, global_name,
            sizeof(global_name));
    if (rc != PSCRIPT_OK) {
        return rc;
    }
    duk = ctx->duk;
    ctx->fatal_jmp_active = 1;
    if (setjmp(ctx->fatal_jmp) != 0) {
        return pscript_fatal_error(ctx,
                "Duktape fatal error while setting global string");
    }
    duk_push_lstring(duk, value, (duk_size_t) length);
    duk_put_global_string(duk, global_name);
    duk_set_top(duk, 0);
    ctx->fatal_jmp_active = 0;
    return PSCRIPT_OK;
}

PSCRIPT_API int PScript_SetGlobalNumber(HANDLE hScript, const char *name,
        int name_len, double value)
{
    pscript_context *ctx;
    duk_context *duk;
    char global_name[PSCRIPT_MAX_GLOBAL_NAME_BYTES + 1];
    int rc;

    ctx = (pscript_context *) hScript;
    rc = pscript_global_prepare(ctx, name, name_len, global_name,
            sizeof(global_name));
    if (rc != PSCRIPT_OK) {
        return rc;
    }
    duk = ctx->duk;
    ctx->fatal_jmp_active = 1;
    if (setjmp(ctx->fatal_jmp) != 0) {
        return pscript_fatal_error(ctx,
                "Duktape fatal error while setting global number");
    }
    duk_push_number(duk, (duk_double_t) value);
    duk_put_global_string(duk, global_name);
    duk_set_top(duk, 0);
    ctx->fatal_jmp_active = 0;
    return PSCRIPT_OK;
}

PSCRIPT_API int PScript_SetGlobalBoolean(HANDLE hScript, const char *name,
        int name_len, int value)
{
    pscript_context *ctx;
    duk_context *duk;
    char global_name[PSCRIPT_MAX_GLOBAL_NAME_BYTES + 1];
    int rc;

    ctx = (pscript_context *) hScript;
    rc = pscript_global_prepare(ctx, name, name_len, global_name,
            sizeof(global_name));
    if (rc != PSCRIPT_OK) {
        return rc;
    }
    duk = ctx->duk;
    ctx->fatal_jmp_active = 1;
    if (setjmp(ctx->fatal_jmp) != 0) {
        return pscript_fatal_error(ctx,
                "Duktape fatal error while setting global boolean");
    }
    duk_push_boolean(duk, (value != 0) ? 1 : 0);
    duk_put_global_string(duk, global_name);
    duk_set_top(duk, 0);
    ctx->fatal_jmp_active = 0;
    return PSCRIPT_OK;
}

PSCRIPT_API int PScript_GetGlobalJson(HANDLE hScript, const char *name,
        int name_len)
{
    pscript_context *ctx;
    duk_context *duk;
    char global_name[PSCRIPT_MAX_GLOBAL_NAME_BYTES + 1];
    int rc;

    ctx = (pscript_context *) hScript;
    rc = pscript_global_prepare(ctx, name, name_len, global_name,
            sizeof(global_name));
    if (rc != PSCRIPT_OK) {
        return rc;
    }
    duk = ctx->duk;
    ctx->fatal_jmp_active = 1;
    if (setjmp(ctx->fatal_jmp) != 0) {
        return pscript_fatal_error(ctx,
                "Duktape fatal error while reading global");
    }
    duk_get_global_string(duk, global_name);
    if (duk_is_undefined(duk, -1)) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "global is undefined");
        duk_set_top(duk, 0);
        ctx->fatal_jmp_active = 0;
        return PSCRIPT_ERROR_GLOBAL;
    }
    rc = pscript_capture_json(ctx, -1);
    duk_set_top(duk, 0);
    ctx->fatal_jmp_active = 0;
    return rc;
}

PSCRIPT_API int PScript_CallGlobalJson(HANDLE hScript, const char *name,
        int name_len, const char *args_json, int args_len)
{
    pscript_context *ctx;
    duk_context *duk;
    char global_name[PSCRIPT_MAX_GLOBAL_NAME_BYTES + 1];
    size_t args_length;
    duk_size_t array_length;
    duk_uarridx_t i;
    duk_idx_t nargs;
    int rc;

    ctx = (pscript_context *) hScript;
    rc = pscript_global_prepare(ctx, name, name_len, global_name,
            sizeof(global_name));
    if (rc != PSCRIPT_OK) {
        return rc;
    }
    if (args_json == NULL) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "JSON argument array is NULL");
        return PSCRIPT_ERROR_ARGUMENT;
    }
    if (args_len < 0) {
        args_length = strlen(args_json);
    } else {
        args_length = (size_t) args_len;
    }
    if (args_length > PSCRIPT_MAX_SOURCE_BYTES) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "JSON arguments exceed PSCRIPT_MAX_SOURCE_BYTES");
        return PSCRIPT_ERROR_SOURCE_TOO_LARGE;
    }

    duk = ctx->duk;
    ctx->timed_out = 0;
    ctx->memory_limited = 0;
    ctx->deadline = (ctx->budget_ms == 0) ? 0 :
            GetTickCount() + ctx->budget_ms;
    ctx->fatal_jmp_active = 1;
    if (setjmp(ctx->fatal_jmp) != 0) {
        return pscript_fatal_error(ctx,
                "Duktape fatal error while calling global");
    }

    duk_get_global_string(duk, global_name);
    if (duk_is_undefined(duk, -1)) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "global function is undefined");
        duk_set_top(duk, 0);
        ctx->deadline = 0;
        ctx->fatal_jmp_active = 0;
        return PSCRIPT_ERROR_GLOBAL;
    }
    if (!duk_is_callable(duk, -1)) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "global is not callable");
        duk_set_top(duk, 0);
        ctx->deadline = 0;
        ctx->fatal_jmp_active = 0;
        return PSCRIPT_ERROR_GLOBAL;
    }

    /* The decoder is itself protected because invalid JSON is a normal
     * caller error, not a reason to poison the Duktape context. */
    duk_push_c_function(duk, pscript_json_decode, 1);
    duk_push_lstring(duk, args_json, (duk_size_t) args_length);
    rc = duk_pcall(duk, 1);
    if (rc != 0) {
        rc = pscript_protected_error(ctx, PSCRIPT_ERROR_JSON);
        duk_set_top(duk, 0);
        ctx->deadline = 0;
        ctx->fatal_jmp_active = 0;
        return rc;
    }
    if (!duk_is_array(duk, 1)) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "JSON arguments must be an array");
        duk_set_top(duk, 0);
        ctx->deadline = 0;
        ctx->fatal_jmp_active = 0;
        return PSCRIPT_ERROR_JSON;
    }
    array_length = duk_get_length(duk, 1);
    if (array_length > (duk_size_t) 0x7fffffffUL) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "JSON argument array is too large");
        duk_set_top(duk, 0);
        ctx->deadline = 0;
        ctx->fatal_jmp_active = 0;
        return PSCRIPT_ERROR_JSON;
    }
    nargs = (duk_idx_t) array_length;
    for (i = 0; i < (duk_uarridx_t) array_length; i++) {
        duk_get_prop_index(duk, 1, i);
    }
    duk_remove(duk, 1);
    ctx->evaluations++;
    rc = duk_pcall(duk, nargs);
    if (rc != 0) {
        rc = pscript_protected_error(ctx, PSCRIPT_ERROR_CALL);
        duk_set_top(duk, 0);
        ctx->deadline = 0;
        ctx->fatal_jmp_active = 0;
        return rc;
    }
    rc = pscript_capture_json(ctx, -1);
    duk_set_top(duk, 0);
    ctx->deadline = 0;
    ctx->fatal_jmp_active = 0;
    return rc;
}

PSCRIPT_API int PScript_RegisterGlobalJsonFunction(HANDLE hScript,
        const char *name, int name_len, PScriptJsonFunctionFn fn, void *pw)
{
    pscript_context *ctx;
    duk_context *duk;
    char global_name[PSCRIPT_MAX_GLOBAL_NAME_BYTES + 1];
    int native_index;
    int is_new;
    int rc;

    ctx = (pscript_context *) hScript;
    if (fn == NULL) {
        if (ctx != NULL) {
            pscript_copy(ctx->error, sizeof(ctx->error),
                    "native callback is NULL");
        }
        return PSCRIPT_ERROR_NATIVE;
    }
    rc = pscript_global_prepare(ctx, name, name_len, global_name,
            sizeof(global_name));
    if (rc != PSCRIPT_OK) {
        return rc;
    }
    native_index = pscript_native_find(ctx, global_name);
    is_new = (native_index < 0) ? 1 : 0;
    if (is_new) {
        if (ctx->native_count >= PSCRIPT_MAX_NATIVE_FUNCTIONS) {
            pscript_copy(ctx->error, sizeof(ctx->error),
                    "native function limit exceeded");
            return PSCRIPT_ERROR_NATIVE_LIMIT;
        }
        native_index = pscript_native_free_slot(ctx);
        if (native_index < 0) {
            pscript_copy(ctx->error, sizeof(ctx->error),
                    "native function table is full");
            return PSCRIPT_ERROR_NATIVE_LIMIT;
        }
    }

    duk = ctx->duk;
    ctx->fatal_jmp_active = 1;
    if (setjmp(ctx->fatal_jmp) != 0) {
        return pscript_fatal_error(ctx,
                "Duktape fatal error while registering native function");
    }
    if (!pscript_context_stash_set(ctx)) {
        ctx->fatal_jmp_active = 0;
        pscript_copy(ctx->error, sizeof(ctx->error),
                "native callback context unavailable");
        return PSCRIPT_ERROR_NATIVE;
    }
    duk_push_c_function(duk, pscript_native_dispatch, DUK_VARARGS);
    duk_push_string(duk, global_name);
    duk_put_prop_string(duk, -2, PSCRIPT_NATIVE_NAME_PROP);
    duk_put_global_string(duk, global_name);
    duk_set_top(duk, 0);
    ctx->fatal_jmp_active = 0;

    ctx->native_functions[native_index].active = 1;
    pscript_copy(ctx->native_functions[native_index].name,
            sizeof(ctx->native_functions[native_index].name), global_name);
    ctx->native_functions[native_index].fn = fn;
    ctx->native_functions[native_index].pw = pw;
    if (is_new) {
        ctx->native_count++;
    }
    return PSCRIPT_OK;
}

PSCRIPT_API int PScript_UnregisterGlobalJsonFunction(HANDLE hScript,
        const char *name, int name_len)
{
    pscript_context *ctx;
    duk_context *duk;
    char global_name[PSCRIPT_MAX_GLOBAL_NAME_BYTES + 1];
    int native_index;
    int rc;

    ctx = (pscript_context *) hScript;
    rc = pscript_global_prepare(ctx, name, name_len, global_name,
            sizeof(global_name));
    if (rc != PSCRIPT_OK) {
        return rc;
    }
    native_index = pscript_native_find(ctx, global_name);
    if (native_index < 0) {
        pscript_copy(ctx->error, sizeof(ctx->error),
                "native function is not registered");
        return PSCRIPT_ERROR_GLOBAL;
    }

    duk = ctx->duk;
    ctx->fatal_jmp_active = 1;
    if (setjmp(ctx->fatal_jmp) != 0) {
        return pscript_fatal_error(ctx,
                "Duktape fatal error while unregistering native function");
    }
    duk_push_undefined(duk);
    duk_put_global_string(duk, global_name);
    duk_set_top(duk, 0);
    ctx->fatal_jmp_active = 0;

    memset(&ctx->native_functions[native_index], 0,
            sizeof(ctx->native_functions[native_index]));
    if (ctx->native_count > 0) {
        ctx->native_count--;
    }
    return PSCRIPT_OK;
}

PSCRIPT_API unsigned long PScript_GetNativeFunctionCount(HANDLE hScript)
{
    pscript_context *ctx;

    ctx = (pscript_context *) hScript;
    return (ctx != NULL) ? ctx->native_count : 0;
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

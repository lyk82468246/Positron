/* Advisory HTTP names, not local filesystem policy. Fixed budgets, no I/O. */
#include <windows.h>
#include <string.h>
#include "positron_http.h"
#include "p_http_stream.h"

static int name_length(const char* text, size_t capacity, size_t* bytes)
{
    size_t i;
    if (text == NULL) { *bytes = 0; return 1; }
    for (i = 0; i < capacity; i++) {
        if (text[i] == '\0') { *bytes = i; return 1; }
    }
    return 0;
}
static int name_token(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            (c != 0 && strchr("!#$%&'*+-.^_`|~", c) != NULL);
}
static int name_attr(unsigned char c)
{ return name_token(c) && c != '*' && c != '\'' && c != '%'; }
static int name_equal(const char* a, const char* b)
{
    unsigned char x;
    unsigned char y;
    while (*a != '\0' && *b != '\0') {
        x = (unsigned char)*a++; y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}
static void name_space(const char** cursor)
{ while (**cursor == ' ' || **cursor == '\t') (*cursor)++; }
static int name_hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int name_basename(const char* value, size_t bytes)
{
    size_t i;
    unsigned char c;
    if (bytes == 0) return PHTTP_NAME_MISSING;
    if (bytes > PHTTP_DOWNLOAD_NAME_MAX_BYTES) return PHTTP_NAME_LIMIT;
    if (!phttp_text_utf8(value, bytes)) return PHTTP_NAME_INVALID;
    if (value[0] == ' ' || value[bytes - 1] == ' ' ||
            (bytes == 1 && value[0] == '.') ||
            (bytes == 2 && value[0] == '.' && value[1] == '.')) return PHTTP_NAME_INVALID;
    for (i = 0; i < bytes; i++) {
        c = (unsigned char)value[i];
        if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':' ||
                (c == 194 && i + 1 < bytes &&
                 (unsigned char)value[i + 1] >= 128 &&
                 (unsigned char)value[i + 1] <= 159)) return PHTTP_NAME_INVALID;
    }
    return PHTTP_NAME_OK;
}
static int name_decode(const char* text, size_t bytes, int extended, char* out)
{
    size_t i;
    size_t used;
    int high;
    int low;
    unsigned char c;
    used = 0;
    for (i = 0; i < bytes; i++) {
        c = (unsigned char)text[i];
        if (c == '%') {
            if (i + 2 >= bytes || (high = name_hex((unsigned char)text[i + 1])) < 0 ||
                    (low = name_hex((unsigned char)text[i + 2])) < 0) return PHTTP_NAME_INVALID;
            c = (unsigned char)(high * 16 + low); i += 2;
        } else if (extended && !name_attr(c)) return PHTTP_NAME_INVALID;
        if (used == PHTTP_DOWNLOAD_NAME_MAX_BYTES) return PHTTP_NAME_LIMIT;
        if (c == 0) return PHTTP_NAME_INVALID;
        out[used++] = (char)c;
    }
    out[used] = '\0';
    return name_basename(out, used);
}
static int name_extended(const char* value, char* out)
{
    const char* language;
    const char* text;
    const char* p;
    size_t bytes;
    language = strchr(value, '\'');
    if (language == NULL || language == value) return PHTTP_NAME_INVALID;
    if (language - value != 5 ||
            !((value[0] == 'U' || value[0] == 'u') &&
              (value[1] == 'T' || value[1] == 't') &&
              (value[2] == 'F' || value[2] == 'f') && value[3] == '-' && value[4] == '8')) {
        return PHTTP_NAME_UNSUPPORTED;
    }
    language++;
    text = strchr(language, '\'');
    if (text == NULL) return PHTTP_NAME_INVALID;
    if (text - language > 32) return PHTTP_NAME_LIMIT;
    for (p = language; p < text; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                (*p >= '0' && *p <= '9') || *p == '-')) return PHTTP_NAME_INVALID;
    }
    text++;
    bytes = strlen(text);
    return name_decode(text, bytes, 1, out);
}
static int name_disposition(const char* header, PHttpDownloadNameInfo* info,
        int* has_name)
{
    char names[32][64];
    char value[512];
    char ordinary[512];
    char extended[512];
    char decoded[PHTTP_DOWNLOAD_NAME_MAX_BYTES + 1];
    const char* p;
    size_t used;
    unsigned int count;
    unsigned int i;
    int quoted;
    int extended_quoted;
    int has_ordinary;
    int has_extended;
    int star_status;
    int ordinary_status;
    unsigned char c;
    *has_name = 0;
    p = header; count = 0; has_ordinary = has_extended = extended_quoted = 0;
    name_space(&p);
    if (*p == '\0') return PHTTP_NAME_OK;
    used = 0;
    while (name_token((unsigned char)*p)) { used++; p++; }
    if (used == 0) return PHTTP_NAME_INVALID;
    name_space(&p);
    while (*p != '\0') {
        if (*p++ != ';') return PHTTP_NAME_INVALID;
        name_space(&p);
        if (count == 32) return PHTTP_NAME_LIMIT;
        used = 0;
        while (name_token((unsigned char)*p)) {
            if (used == 63) return PHTTP_NAME_LIMIT;
            c = (unsigned char)*p++;
            names[count][used++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        names[count][used] = '\0';
        if (used == 0) return PHTTP_NAME_INVALID;
        for (i = 0; i < count; i++) {
            if (name_equal(names[i], names[count])) return PHTTP_NAME_INVALID;
        }
        name_space(&p);
        if (*p++ != '=') return PHTTP_NAME_INVALID;
        name_space(&p);
        used = 0; quoted = *p == '"';
        if (quoted) {
            p++;
            while (*p != '\0' && *p != '"') {
                c = (unsigned char)*p++;
                if (c == '\\') {
                    if (*p == '\0') return PHTTP_NAME_INVALID;
                    c = (unsigned char)*p++;
                }
                if (c < 32 || c == 127) return PHTTP_NAME_INVALID;
                value[used++] = (char)c;
            }
            if (*p++ != '"') return PHTTP_NAME_INVALID;
        } else {
            while (name_token((unsigned char)*p)) value[used++] = *p++;
            if (used == 0) return PHTTP_NAME_INVALID;
        }
        value[used] = '\0';
        name_space(&p);
        if (*p != '\0' && *p != ';') return PHTTP_NAME_INVALID;
        if (name_equal(names[count], "filename")) {
            strcpy(ordinary, value); has_ordinary = 1;
        } else if (name_equal(names[count], "filename*")) {
            strcpy(extended, value); has_extended = 1; extended_quoted = quoted;
        } else if (strlen(names[count]) > 9 &&
                memcmp(names[count], "filename*", 9) == 0) {
            return PHTTP_NAME_UNSUPPORTED; /* no MIME continuation assembly */
        }
        count++;
    }
    *has_name = has_ordinary || has_extended;
    if (!*has_name) return PHTTP_NAME_OK;
    star_status = PHTTP_NAME_OK;
    if (has_extended) {
        star_status = extended_quoted ? PHTTP_NAME_INVALID : name_extended(extended, decoded);
        if (star_status == PHTTP_NAME_OK) {
            strcpy(info->basename, decoded); info->source = PHTTP_NAME_SOURCE_FILENAME_STAR;
            return PHTTP_NAME_OK;
        }
        info->disposition_status = star_status;
    }
    if (has_ordinary) {
        ordinary_status = strstr(ordinary, "=?") != NULL && strstr(ordinary, "?=") != NULL ?
                PHTTP_NAME_UNSUPPORTED : name_basename(ordinary, strlen(ordinary));
        if (ordinary_status == PHTTP_NAME_OK) {
            strcpy(info->basename, ordinary); info->source = PHTTP_NAME_SOURCE_FILENAME;
            return PHTTP_NAME_OK;
        }
        return ordinary_status;
    }
    return star_status;
}
PHTTP_API int PHttp_SuggestDownloadName(const char* disposition,
        const char* final_url, PHttpDownloadNameInfo* info)
{
    PHttpDownloadNameInfo next;
    char canonical[PHTTP_URL_MAX];
    const char* path;
    const char* end;
    const char* segment;
    const char* p;
    size_t bytes;
    int has_name;
    if (info == NULL || info->size != sizeof(*info) ||
            info->version != PHTTP_DOWNLOAD_NAME_VERSION) return PHTTP_STREAM_ARGUMENT;
    memset(&next, 0, sizeof(next)); next.size = sizeof(next);
    next.version = PHTTP_DOWNLOAD_NAME_VERSION; next.status = PHTTP_NAME_MISSING;
    if (!name_length(disposition, 512, &bytes)) { next.status = PHTTP_NAME_LIMIT; goto done; }
    if (bytes != 0) {
        if (!phttp_text_utf8(disposition, bytes)) { next.status = PHTTP_NAME_INVALID; goto done; }
        next.status = name_disposition(disposition, &next, &has_name);
        if (next.status != PHTTP_NAME_OK || has_name) goto done;
    }
    next.status = PHTTP_NAME_MISSING;
    if (!name_length(final_url, PHTTP_URL_MAX, &bytes)) { next.status = PHTTP_NAME_LIMIT; goto done; }
    if (bytes == 0) goto done;
    if (PHttp_ResolveReferenceUrl(NULL, final_url, canonical, sizeof(canonical)) != 0) {
        next.status = PHTTP_NAME_INVALID; goto done;
    }
    path = strchr(strstr(canonical, "://") + 3, '/');
    if (path == NULL) goto done;
    end = strchr(path, '?');
    if (end == NULL) end = path + strlen(path);
    segment = path + 1;
    for (p = segment; p < end; p++) if (*p == '/') segment = p + 1;
    next.status = name_decode(segment, (size_t)(end - segment), 0, next.basename);
    if (next.status == PHTTP_NAME_OK) next.source = PHTTP_NAME_SOURCE_URL;
done:
    if (next.status != PHTTP_NAME_OK) {
        next.basename[0] = '\0'; next.source = PHTTP_NAME_SOURCE_NONE;
    }
    *info = next;
    return PHTTP_STREAM_OK;
}

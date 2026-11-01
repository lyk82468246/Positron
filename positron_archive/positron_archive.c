#include "positron_archive.h"
#include "miniz.h"
#include <stdlib.h>
#include <string.h>

#define PA_MAGIC 0x50415a31U
#define PA_MIB 1048576U
typedef union PaAllocation {
    double alignment;
    struct { size_t bytes; } meta;
} PaAllocation;
typedef struct PaRange { mz_uint64 start, end; } PaRange;
struct PArchiveReaderTag {
    unsigned int magic, busy, io_error, memory_error;
    size_t used, budget;
    PArchiveSource source;
    PArchiveLimits limits;
    mz_zip_archive zip;
    PArchiveEntry* entries;
    PaRange* ranges;
    mz_zip_reader_extract_iter_state* active;
    unsigned int active_index, emitted;
};
static void* pa_alloc(void* opaque, size_t count, size_t width)
{
    PArchiveReader r = (PArchiveReader)opaque;
    PaAllocation* p;
    size_t n;
    if (width && count > (size_t)-1 / width) {
        r->memory_error = 1; return NULL;
    }
    n = count * width;
    if (n > r->budget || sizeof(*p) > r->budget - n ||
            r->used > r->budget - n - sizeof(*p)) {
        r->memory_error = 1; return NULL;
    }
    p = (PaAllocation*)malloc(sizeof(*p) + n);
    if (!p) { r->memory_error = 2; return NULL; }
    p->meta.bytes = n;
    r->used += n + sizeof(*p);
    return p + 1;
}
static void pa_free(void* opaque, void* address)
{
    PArchiveReader r = (PArchiveReader)opaque;
    PaAllocation* p;
    if (!address) return;
    p = (PaAllocation*)address - 1;
    r->used -= p->meta.bytes + sizeof(*p);
    free(p);
}
static void* pa_realloc(void* opaque, void* address, size_t count, size_t width)
{
    PArchiveReader r = (PArchiveReader)opaque;
    PaAllocation* old;
    void* next;
    size_t n, old_bytes;
    if (!address) return pa_alloc(opaque, count, width);
    if (width && count > (size_t)-1 / width) {
        r->memory_error = 1; return NULL;
    }
    n = count * width;
    old = (PaAllocation*)address - 1;
    old_bytes = old->meta.bytes;
    /* Count simultaneous old/new blocks: conservative peak accounting. */
    next = pa_alloc(r, count, width);
    if (!next) return NULL;
    memcpy(next, address, n < old_bytes ? n : old_bytes);
    pa_free(r, address);
    return next;
}
static size_t pa_read(void* opaque, mz_uint64 offset, void* buffer, size_t bytes)
{
    PArchiveReader r = (PArchiveReader)opaque;
    unsigned int got;
    if (offset > r->source.archive_bytes ||
            bytes > r->source.archive_bytes - offset) {
        r->io_error = 1; return 0;
    }
    if (!bytes) return 0;
    got = r->source.read_at(r->source.user_data, (unsigned int)offset,
            buffer, (unsigned int)bytes);
    if (got != bytes) { r->io_error = 1; return 0; }
    return got;
}
static int pa_error(PArchiveReader r)
{
    mz_zip_error e = mz_zip_get_last_error(&r->zip);
    if (r->io_error) return PARCHIVE_IO;
    if (r->memory_error) return r->memory_error == 1 ?
            PARCHIVE_LIMIT : PARCHIVE_MEMORY;
    if (e == MZ_ZIP_UNSUPPORTED_METHOD || e == MZ_ZIP_UNSUPPORTED_ENCRYPTION ||
            e == MZ_ZIP_UNSUPPORTED_FEATURE || e == MZ_ZIP_UNSUPPORTED_MULTIDISK)
        return PARCHIVE_UNSUPPORTED;
    return PARCHIVE_INVALID;
}
static unsigned int pa16(const unsigned char* p)
{ return p[0] | ((unsigned int)p[1] << 8); }
static unsigned int pa32(const unsigned char* p)
{ return pa16(p) | (pa16(p + 2) << 16); }
static unsigned char pa_lower(unsigned char c)
{ return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 32) : c; }
static int pa_utf8(const unsigned char* p, unsigned int n)
{
    unsigned int i = 0, c, more, value, minimum;
    while (i < n) {
        c = p[i++];
        if (c < 128) { if (c < 32 || c == 127) return 0; continue; }
        if (c >= 194 && c <= 223) { more = 1; value = c & 31; minimum = 128; }
        else if (c >= 224 && c <= 239) { more = 2; value = c & 15; minimum = 2048; }
        else if (c >= 240 && c <= 244) { more = 3; value = c & 7; minimum = 65536; }
        else return 0;
        if (more > n - i) return 0;
        while (more--) {
            c = p[i++]; if ((c & 192) != 128) return 0;
            value = (value << 6) | (c & 63);
        }
        if (value < minimum || value > 0x10ffff ||
                (value >= 0xd800 && value <= 0xdfff)) return 0;
    }
    return 1;
}
static int pa_path(const char* name, unsigned int n, unsigned int flags)
{
    unsigned int i, start = 0, depth = 0, length, base;
    char short_name[5];
    if (!n || n > PARCHIVE_PATH_BYTES || !pa_utf8((const unsigned char*)name, n)) return 0;
    for (i = 0; i <= n; ++i) {
        if (i < n) {
            unsigned char c = (unsigned char)name[i];
            if (c >= 128 && !(flags & 2048)) return 0;
            if (strchr("\\:*?\"<>|", c)) return 0;
            if (c != '/') continue;
        }
        length = i - start;
        if (!length) return i == n && i && name[i - 1] == '/';
        if (name[start] == ' ') return 0;
        if (++depth > 16 || name[i - 1] == '.' || name[i - 1] == ' ') return 0;
        base = 0;
        while (base < length && name[start + base] != '.') ++base;
        if (base == 3 || base == 4) {
            unsigned int j;
            for (j = 0; j < base; ++j) short_name[j] = (char)pa_lower((unsigned char)name[start + j]);
            short_name[base] = 0;
            if (!strcmp(short_name, "con") || !strcmp(short_name, "prn") ||
                    !strcmp(short_name, "aux") || !strcmp(short_name, "nul") ||
                    (base == 4 && (!memcmp(short_name, "com", 3) || !memcmp(short_name, "lpt", 3)) &&
                    short_name[3] >= '1' && short_name[3] <= '9')) return 0;
        }
        start = i + 1;
    }
    return 1;
}
static int pa_collision(const PArchiveEntry* a, const PArchiveEntry* b)
{
    unsigned int i = 0;
    WCHAR aw[PARCHIVE_PATH_BYTES + 1], bw[PARCHIVE_PATH_BYTES + 1];
    int an, bn, common, comparison;
    an = MultiByteToWideChar(CP_UTF8, 0, a->path, -1, aw, PARCHIVE_PATH_BYTES + 1) - 1;
    bn = MultiByteToWideChar(CP_UTF8, 0, b->path, -1, bw, PARCHIVE_PATH_BYTES + 1) - 1;
    if (an < 1 || bn < 1) return 1;
    if (a->is_directory) --an;
    if (b->is_directory) --bn;
    common = an < bn ? an : bn;
    comparison = CompareStringW(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE,
            aw, common, bw, common);
    if (!comparison) return 1;
    if (comparison != CSTR_EQUAL) return 0;
    i = (unsigned int)common;
    if (an == bn) return 1;
    if (common == an && bw[i] == '/' && !a->is_directory) return 1;
    if (common == bn && aw[i] == '/' && !b->is_directory) return 1;
    return 0;
}
static int pa_admit(PArchiveReader r)
{
    unsigned int i, j, n, total = 0, count = r->zip.m_total_files;
    unsigned char header[46], local[30];
    mz_zip_archive_file_stat stat;
    PArchiveEntry* e;
    mz_uint64 end;
    if (mz_zip_is_zip64(&r->zip)) return PARCHIVE_UNSUPPORTED;
    if (count > r->limits.max_entries) return PARCHIVE_LIMIT;
    r->entries = (PArchiveEntry*)pa_alloc(r, count, sizeof(*e));
    if (!r->entries && count) return pa_error(r);
    r->ranges = (PaRange*)pa_alloc(r, count, sizeof(PaRange));
    if (!r->ranges && count) return pa_error(r);
    for (i = 0; i < count; ++i) {
        if (!mz_zip_reader_file_stat(&r->zip, i, &stat)) return pa_error(r);
        if (stat.m_version_needed > 20 || stat.m_is_encrypted ||
                (stat.m_method != 0 && stat.m_method != 8) ||
                (stat.m_bit_flag & ~0x080eU) ||
                ((((stat.m_external_attr >> 16) & 0170000) != 0) &&
                 (((stat.m_external_attr >> 16) & 0170000) != 0100000) &&
                 (((stat.m_external_attr >> 16) & 0170000) != 0040000))) return PARCHIVE_UNSUPPORTED;
        if (stat.m_uncomp_size > r->limits.max_entry_bytes ||
                stat.m_uncomp_size > r->limits.max_total_bytes - total) return PARCHIVE_LIMIT;
        if (stat.m_comp_size > r->source.archive_bytes) return PARCHIVE_INVALID;
        total += (unsigned int)stat.m_uncomp_size;
        if (pa_read(r, r->zip.m_central_directory_file_ofs + stat.m_central_dir_ofs,
                header, sizeof(header)) != sizeof(header)) return pa_error(r);
        if (pa16(header + 34)) return PARCHIVE_UNSUPPORTED;
        n = pa16(header + 28);
        if (!n || n > PARCHIVE_PATH_BYTES) return PARCHIVE_PATH;
        e = &r->entries[i]; memset(e, 0, sizeof(*e));
        if (pa_read(r, r->zip.m_central_directory_file_ofs + stat.m_central_dir_ofs + 46,
                e->path, n) != n) return pa_error(r);
        if (!pa_path(e->path, n, stat.m_bit_flag)) return PARCHIVE_PATH;
        e->size = sizeof(*e); e->version = PARCHIVE_VERSION;
        e->compressed_bytes = (unsigned int)stat.m_comp_size;
        e->uncompressed_bytes = (unsigned int)stat.m_uncomp_size;
        e->crc32 = stat.m_crc32; e->method = stat.m_method;
        e->is_directory = e->path[n - 1] == '/';
        if (e->is_directory != (unsigned int)stat.m_is_directory) return PARCHIVE_INVALID;
        if (e->is_directory && (e->uncompressed_bytes || e->compressed_bytes)) return PARCHIVE_INVALID;
        if (!e->method && e->compressed_bytes != e->uncompressed_bytes) return PARCHIVE_INVALID;
        for (j = 0; j < i; ++j) if (pa_collision(e, &r->entries[j])) return PARCHIVE_PATH;
        if (pa_read(r, stat.m_local_header_ofs, local, sizeof(local)) != sizeof(local)) return pa_error(r);
        if (pa32(local) != 0x04034b50 || pa16(local + 6) != stat.m_bit_flag ||
                pa16(local + 8) != stat.m_method || pa16(local + 26) != n) return PARCHIVE_INVALID;
        end = stat.m_local_header_ofs + 30 + n + pa16(local + 28) + stat.m_comp_size;
        if (end > r->zip.m_central_directory_file_ofs) return PARCHIVE_INVALID;
        if (!mz_zip_validate_file(&r->zip, i, MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY)) return pa_error(r);
        if (stat.m_bit_flag & 8) {
            unsigned char descriptor[4];
            if (pa_read(r, end, descriptor, 4) != 4) return pa_error(r);
            end += pa32(descriptor) == 0x08074b50 ? 16 : 12;
            if (end > r->zip.m_central_directory_file_ofs) return PARCHIVE_INVALID;
        }
        for (j = 0; j < i; ++j) if (stat.m_local_header_ofs < r->ranges[j].end &&
                end > r->ranges[j].start) return PARCHIVE_INVALID;
        r->ranges[i].start = stat.m_local_header_ofs; r->ranges[i].end = end;
        {
            char name[PARCHIVE_PATH_BYTES];
            if (pa_read(r, stat.m_local_header_ofs + 30, name, n) != n) return pa_error(r);
            if (memcmp(name, e->path, n)) return PARCHIVE_INVALID;
        }
        if (!(stat.m_bit_flag & 8) && (pa32(local + 14) != stat.m_crc32 ||
                pa32(local + 18) != stat.m_comp_size || pa32(local + 22) != stat.m_uncomp_size)) return PARCHIVE_INVALID;
    }
    return PARCHIVE_OK;
}
int PArchive_Open(const PArchiveSource* source, const PArchiveLimits* limits, PArchiveReader* output)
{
    PArchiveReader r;
    int result;
    PArchiveLimits defaults = {sizeof(PArchiveLimits), PARCHIVE_VERSION, 1024, 16 * PA_MIB, 64 * PA_MIB, PA_MIB};
    if (!source || !output || source->size != sizeof(*source) ||
            source->version != PARCHIVE_VERSION || !source->read_at) return PARCHIVE_ARGUMENT;
    if (source->archive_bytes > 32 * PA_MIB) return PARCHIVE_LIMIT;
    if (!limits) limits = &defaults;
    if (limits->size != sizeof(*limits) || limits->version != PARCHIVE_VERSION ||
            !limits->max_entries || limits->max_entries > 1024 ||
            !limits->max_entry_bytes || limits->max_entry_bytes > 16 * PA_MIB ||
            !limits->max_total_bytes || limits->max_total_bytes > 64 * PA_MIB ||
            limits->max_memory_bytes < 65536 || limits->max_memory_bytes > PA_MIB) return PARCHIVE_ARGUMENT;
    r = (PArchiveReader)calloc(1, sizeof(*r));
    if (!r) return PARCHIVE_MEMORY;
    r->magic = PA_MAGIC; r->busy = 1; r->source = *source; r->limits = *limits;
    r->used = sizeof(*r); r->budget = limits->max_memory_bytes;
    r->zip.m_pAlloc = pa_alloc; r->zip.m_pFree = pa_free; r->zip.m_pRealloc = pa_realloc;
    r->zip.m_pAlloc_opaque = r; r->zip.m_pIO_opaque = r; r->zip.m_pRead = pa_read;
    if (!mz_zip_reader_init(&r->zip, source->archive_bytes, MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) result = pa_error(r);
    else result = pa_admit(r);
    r->busy = 0;
    if (result) { PArchive_Close(r); return result; }
    *output = r; return PARCHIVE_OK;
}
int PArchive_Count(PArchiveReader r, unsigned int* count)
{
    if (!r || r->magic != PA_MAGIC || !count) return PARCHIVE_ARGUMENT;
    if (r->busy) return PARCHIVE_STATE;
    *count = r->zip.m_total_files; return PARCHIVE_OK;
}
int PArchive_Entry(PArchiveReader r, unsigned int index, PArchiveEntry* entry)
{
    if (!r || r->magic != PA_MAGIC || !entry || entry->size != sizeof(*entry) ||
            entry->version != PARCHIVE_VERSION || index >= r->zip.m_total_files) return PARCHIVE_ARGUMENT;
    if (r->busy) return PARCHIVE_STATE;
    *entry = r->entries[index]; return PARCHIVE_OK;
}
int PArchive_Begin(PArchiveReader r, unsigned int index)
{
    if (!r || r->magic != PA_MAGIC || index >= r->zip.m_total_files) return PARCHIVE_ARGUMENT;
    if (r->busy || r->active) return PARCHIVE_STATE;
    r->busy = 1; r->io_error = r->memory_error = 0;
    r->active = mz_zip_reader_extract_iter_new(&r->zip, index, 0);
    r->busy = 0;
    if (!r->active) return pa_error(r);
    r->active_index = index; r->emitted = 0;
    return PARCHIVE_OK;
}
int PArchive_Read(PArchiveReader r, void* buffer, unsigned int capacity, unsigned int* written)
{
    size_t n;
    int result = PARCHIVE_OK;
    unsigned int expected;
    if (written) *written = 0;
    if (!r || r->magic != PA_MAGIC || !buffer || !written || !capacity ||
            capacity > PARCHIVE_CHUNK_BYTES) return PARCHIVE_ARGUMENT;
    if (r->busy || !r->active) return PARCHIVE_STATE;
    r->busy = 1;
    expected = r->entries[r->active_index].uncompressed_bytes;
    n = mz_zip_reader_extract_iter_read(r->active, buffer, capacity);
    if (n > expected - r->emitted || r->active->status < 0 || r->io_error) result = PARCHIVE_INTEGRITY;
    else {
        r->emitted += (unsigned int)n;
        if (r->active->file_stat.m_method && r->active->status == TINFL_STATUS_DONE &&
                (r->active->comp_remaining || r->active->read_buf_avail)) result = PARCHIVE_INTEGRITY;
        if (result == PARCHIVE_OK && (!n || (r->emitted == expected && !r->active->out_blk_remain &&
                (!r->active->file_stat.m_method || r->active->status == TINFL_STATUS_DONE)))) {
            if (r->emitted != expected || !mz_zip_reader_extract_iter_free(r->active)) result = PARCHIVE_INTEGRITY;
            else result = PARCHIVE_DONE;
            /* free must run even when advertised size was not reached. */
            if (r->emitted != expected) mz_zip_reader_extract_iter_free(r->active);
            r->active = NULL;
        }
    }
    if (result < 0) {
        if (r->active) { mz_zip_reader_extract_iter_free(r->active); r->active = NULL; }
        if (r->io_error) result = PARCHIVE_IO;
    } else *written = (unsigned int)n;
    r->busy = 0; return result;
}
int PArchive_Abort(PArchiveReader r)
{
    if (!r || r->magic != PA_MAGIC) return PARCHIVE_ARGUMENT;
    if (r->busy) return PARCHIVE_STATE;
    if (r->active) { mz_zip_reader_extract_iter_free(r->active); r->active = NULL; }
    return PARCHIVE_OK;
}
void PArchive_Close(PArchiveReader r)
{
    if (!r || r->magic != PA_MAGIC || r->busy) return;
    PArchive_Abort(r);
    if (r->zip.m_pState) mz_zip_reader_end(&r->zip);
    pa_free(r, r->entries);
    pa_free(r, r->ranges);
    r->magic = 0; free(r);
}

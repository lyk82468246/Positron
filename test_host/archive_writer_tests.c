#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../positron_archive/positron_archive.h"

typedef struct WriterFixture {
    unsigned char* output;
    unsigned int capacity, written, writes, reads, max_io;
    unsigned int fail_write, fail_source, cancel_reads, cancel_writes;
    unsigned int bad_io;
    PArchiveWriter writer;
    int reentry;
} WriterFixture;
static char writer_error[160];
static unsigned char writer_pattern(unsigned int offset)
{ return (unsigned char)(offset * 17 + (offset >> 8)); }
static unsigned int writer_sink(void* user, unsigned int offset,
        const void* data, unsigned int bytes)
{
    WriterFixture* f = (WriterFixture*)user;
    PArchiveWriterInfo info;
    info.size = sizeof(info); info.version = PARCHIVE_VERSION;
    if (f->writer) f->reentry = PArchive_WriterGetInfo(f->writer, &info);
    ++f->writes;
    if (!bytes || bytes > PARCHIVE_CHUNK_BYTES || offset != f->written ||
            bytes > f->capacity - f->written) { ++f->bad_io; return 0; }
    if (bytes > f->max_io) f->max_io = bytes;
    if (f->fail_write && f->writes == f->fail_write) return bytes - 1;
    memcpy(f->output + f->written, data, bytes); f->written += bytes;
    return bytes;
}
static unsigned int writer_source(void* user, unsigned int offset,
        void* data, unsigned int bytes)
{
    WriterFixture* f = (WriterFixture*)user;
    unsigned int i;
    ++f->reads;
    if (!bytes || bytes > PARCHIVE_CHUNK_BYTES) { ++f->bad_io; return 0; }
    if (f->fail_source) return 0;
    for (i = 0; i < bytes; ++i) ((unsigned char*)data)[i] = writer_pattern(offset + i);
    return bytes;
}
static int writer_cancel(void* user)
{
    WriterFixture* f = (WriterFixture*)user;
    return (f->cancel_reads && f->reads >= f->cancel_reads) ||
            (f->cancel_writes && f->writes >= f->cancel_writes);
}
static int writer_create(WriterFixture* f, unsigned char* buffer,
        unsigned int capacity, const PArchiveLimits* limits, PArchiveWriter* writer)
{
    PArchiveSink sink;
    memset(f, 0, sizeof(*f)); f->output = buffer; f->capacity = capacity;
    sink.size = sizeof(sink); sink.version = 1; sink.max_archive_bytes = capacity;
    sink.write = writer_sink; sink.cancelled = writer_cancel; sink.user_data = f;
    return PArchive_WriterCreate(&sink, limits, writer);
}
static int writer_add(PArchiveWriter w, WriterFixture* f, const char* path,
        unsigned int compression, unsigned int bytes)
{
    PArchiveSource source;
    source.size = sizeof(source); source.version = 1; source.archive_bytes = bytes;
    source.read_at = writer_source; source.user_data = f;
    return PArchive_WriterAdd(w, path, compression, &source);
}
static unsigned int writer_readback(void* user, unsigned int offset,
        void* buffer, unsigned int bytes)
{
    WriterFixture* f = (WriterFixture*)user;
    if (offset > f->written || bytes > f->written - offset) return 0;
    memcpy(buffer, f->output + offset, bytes); return bytes;
}
static int writer_verify(WriterFixture* f)
{
    PArchiveSource source;
    PArchiveReader reader = NULL;
    PArchiveEntry entry;
    unsigned char buffer[4096];
    unsigned int count, index, n, total, i;
    int result = 0, rc;
    source.size = sizeof(source); source.version = 1; source.archive_bytes = f->written;
    source.read_at = writer_readback; source.user_data = f;
    if (PArchive_Open(&source, NULL, &reader) != PARCHIVE_OK) return 0;
    if (PArchive_Count(reader, &count) != PARCHIVE_OK || count != 5) goto done;
    for (index = 0; index < count; ++index) {
        memset(&entry, 0, sizeof(entry)); entry.size = sizeof(entry); entry.version = 1;
        if (PArchive_Entry(reader, index, &entry) != PARCHIVE_OK) goto done;
        if (index == 0 && (entry.method != 0 || entry.uncompressed_bytes != 49157)) goto done;
        if (index == 2 && (entry.method != 8 || entry.uncompressed_bytes != 131089 ||
                entry.compressed_bytes >= entry.uncompressed_bytes)) goto done;
        if (PArchive_Begin(reader, index) != PARCHIVE_OK) goto done;
        total = 0;
        do {
            rc = PArchive_Read(reader, buffer, sizeof(buffer), &n);
            if (rc != PARCHIVE_OK && rc != PARCHIVE_DONE) goto done;
            for (i = 0; i < n; ++i) if (buffer[i] != writer_pattern(total + i)) goto done;
            total += n;
        } while (rc != PARCHIVE_DONE);
        if (total != entry.uncompressed_bytes) goto done;
    }
    result = 1;
done:
    PArchive_Close(reader); return result;
}
static int writer_save_artifact(WriterFixture* f)
{
    WCHAR path[MAX_PATH];
    WCHAR* separator;
    HANDLE file;
    DWORD written;
    int okay;
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) return 0;
    separator = wcsrchr(path, L'\\');
    if (!separator || separator - path > MAX_PATH - 32) return 0;
    wcscpy(separator + 1, L"archive-writer-check.zip");
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    okay = WriteFile(file, f->output, f->written, &written, NULL) && written == f->written;
    if (okay) okay = FlushFileBuffers(file) != 0;
    if (!CloseHandle(file)) okay = 0;
    return okay;
}
#define WCHECK(expr) do { if (!(expr)) { \
    sprintf(writer_error, "Archive writer contract failed at line %d", __LINE__); goto failed; \
} } while (0)
BOOL test1376_archive_writer_contract(void)
{
    unsigned char* output = (unsigned char*)malloc(262144);
    unsigned char tiny[1024];
    WriterFixture f, second;
    PArchiveWriter w = NULL, other = NULL;
    PArchiveWriterInfo info;
    PArchiveLimits limits = {sizeof(PArchiveLimits), 1, 1, 16777216, 67108864, 1048576};
    unsigned int before, rounds;
    writer_error[0] = 0;
    WCHECK(output != NULL);
    w = (PArchiveWriter)1;
    WCHECK(PArchive_WriterCreate(NULL, NULL, &w) == PARCHIVE_ARGUMENT && w == (PArchiveWriter)1);
    w = NULL;
    for (rounds = 0; rounds < 8; ++rounds) {
        WCHECK(writer_create(&f, output, 262144, NULL, &w) == PARCHIVE_OK);
        WCHECK(f.writes == 0 && f.reads == 0);
        f.writer = w;
        WCHECK(writer_add(w, &f, "stored.bin", 0, 49157) == PARCHIVE_OK);
        before = f.written;
        WCHECK(writer_add(w, &f, "../bad", 0, 1) == PARCHIVE_PATH && before == f.written);
        WCHECK(writer_add(w, &f, "STORED.BIN", 0, 1) == PARCHIVE_PATH && before == f.written);
        WCHECK(writer_add(w, &f, "stored.bin/child", 0, 1) == PARCHIVE_PATH && before == f.written);
        WCHECK(writer_add(w, &f, "bad-dir/", 0, 1) == PARCHIVE_ARGUMENT && before == f.written);
        WCHECK(PArchive_WriterAdd(w, "text/", 0, NULL) == PARCHIVE_OK);
        WCHECK(writer_add(w, &f, "text/\xe4\xb8\xad\xe6\x96\x87.bin", 1, 131089) == PARCHIVE_OK);
        WCHECK(PArchive_WriterAdd(w, "empty.txt", 1, NULL) == PARCHIVE_OK);
        WCHECK(writer_add(w, &f, "tiny.bin", 1, 3) == PARCHIVE_OK);
        WCHECK(PArchive_WriterFinish(w) == PARCHIVE_DONE);
        info.size = sizeof(info); info.version = 1;
        WCHECK(PArchive_WriterGetInfo(w, &info) == PARCHIVE_OK);
        WCHECK(info.state == PARCHIVE_WRITER_COMPLETE && info.entry_count == 5 &&
                info.input_bytes == 180249 && info.output_bytes == f.written && info.error == 0);
        WCHECK(f.reentry == PARCHIVE_STATE && f.bad_io == 0 && f.reads > 8);
        before = f.writes;
        WCHECK(PArchive_WriterFinish(w) == PARCHIVE_STATE && before == f.writes);
        WCHECK(PArchive_WriterAdd(w, "late", 0, NULL) == PARCHIVE_STATE);
        WCHECK(writer_verify(&f));
        if (!rounds) WCHECK(writer_save_artifact(&f));
        WCHECK(writer_create(&second, tiny, sizeof(tiny), NULL, &other) == PARCHIVE_OK);
        WCHECK(PArchive_WriterFinish(other) == PARCHIVE_DONE && second.written == 22);
        PArchive_WriterClose(other); other = NULL;
        PArchive_WriterClose(w); w = NULL;
    }
    WCHECK(writer_create(&f, output, 262144, &limits, &w) == PARCHIVE_OK);
    WCHECK(PArchive_WriterAdd(w, "first", 0, NULL) == PARCHIVE_OK);
    before = f.writes;
    WCHECK(PArchive_WriterAdd(w, "second", 0, NULL) == PARCHIVE_LIMIT && before == f.writes);
    WCHECK(PArchive_WriterFinish(w) == PARCHIVE_DONE);
    PArchive_WriterClose(w); w = NULL;
    limits.max_entries = 1024; limits.max_entry_bytes = 8; limits.max_total_bytes = 10;
    WCHECK(writer_create(&f, output, 262144, &limits, &w) == PARCHIVE_OK);
    WCHECK(writer_add(w, &f, "too-large", 0, 9) == PARCHIVE_LIMIT && f.writes == 0);
    WCHECK(writer_add(w, &f, "first", 0, 6) == PARCHIVE_OK);
    before = f.writes;
    WCHECK(writer_add(w, &f, "total", 0, 6) == PARCHIVE_LIMIT && before == f.writes);
    WCHECK(PArchive_WriterFinish(w) == PARCHIVE_DONE);
    PArchive_WriterClose(w); w = NULL;
    limits.max_entry_bytes = 16777216; limits.max_total_bytes = 67108864; limits.max_memory_bytes = 65536;
    WCHECK(writer_create(&f, output, 262144, &limits, &w) == PARCHIVE_OK);
    WCHECK(writer_add(w, &f, "memory", 1, 65537) == PARCHIVE_LIMIT);
    WCHECK(PArchive_WriterFinish(w) == PARCHIVE_STATE);
    PArchive_WriterClose(w); w = NULL;
    WCHECK(writer_create(&f, tiny, 22, NULL, &w) == PARCHIVE_OK);
    WCHECK(writer_add(w, &f, "out-limit", 0, 10) == PARCHIVE_LIMIT);
    WCHECK(PArchive_WriterFinish(w) == PARCHIVE_STATE);
    PArchive_WriterClose(w); w = NULL;
    WCHECK(writer_create(&f, output, 262144, NULL, &w) == PARCHIVE_OK);
    f.fail_source = 1;
    WCHECK(writer_add(w, &f, "source-io", 1, 65537) == PARCHIVE_IO);
    WCHECK(PArchive_WriterFinish(w) == PARCHIVE_STATE);
    PArchive_WriterClose(w); w = NULL;
    WCHECK(writer_create(&f, output, 262144, NULL, &w) == PARCHIVE_OK);
    f.fail_write = 3;
    WCHECK(writer_add(w, &f, "sink-io", 0, 65537) == PARCHIVE_IO);
    PArchive_WriterClose(w); w = NULL;
    WCHECK(writer_create(&f, output, 262144, NULL, &w) == PARCHIVE_OK);
    WCHECK(PArchive_WriterAdd(w, "finish-io", 0, NULL) == PARCHIVE_OK);
    f.fail_write = f.writes + 1;
    WCHECK(PArchive_WriterFinish(w) == PARCHIVE_IO);
    before = f.writes;
    WCHECK(PArchive_WriterFinish(w) == PARCHIVE_STATE && before == f.writes);
    PArchive_WriterClose(w); w = NULL;
    WCHECK(writer_create(&f, output, 262144, NULL, &w) == PARCHIVE_OK);
    WCHECK(PArchive_WriterAdd(w, "finish-cancel", 0, NULL) == PARCHIVE_OK);
    f.cancel_writes = f.writes;
    before = f.writes;
    WCHECK(PArchive_WriterFinish(w) == PARCHIVE_CANCELLED && before == f.writes);
    PArchive_WriterClose(w); w = NULL;
    WCHECK(writer_create(&f, output, 262144, NULL, &w) == PARCHIVE_OK);
    f.cancel_reads = 2;
    WCHECK(writer_add(w, &f, "cancel", 1, 131089) == PARCHIVE_CANCELLED);
    info.size = sizeof(info); info.version = 1;
    WCHECK(PArchive_WriterGetInfo(w, &info) == PARCHIVE_OK &&
            info.state == PARCHIVE_WRITER_CANCELLED && info.error == PARCHIVE_CANCELLED);
    WCHECK(PArchive_WriterFinish(w) == PARCHIVE_STATE);
    WCHECK(PArchive_WriterAbort(w) == PARCHIVE_OK);
    PArchive_WriterClose(w); w = NULL;
    WCHECK(writer_create(&f, output, 262144, NULL, &w) == PARCHIVE_OK);
    WCHECK(PArchive_WriterAbort(w) == PARCHIVE_OK && f.writes == 0);
    WCHECK(PArchive_WriterAdd(w, "cancelled", 0, NULL) == PARCHIVE_STATE);
    PArchive_WriterClose(w); w = NULL;
    WCHECK(writer_create(&f, output, 262144, NULL, &w) == PARCHIVE_OK);
    WCHECK(PArchive_WriterAdd(w, "unfinished", 0, NULL) == PARCHIVE_OK);
    before = f.writes; PArchive_WriterClose(w); w = NULL;
    WCHECK(f.writes == before);
    free(output); return TRUE;
failed:
    if (w && w != (PArchiveWriter)1) PArchive_WriterClose(w);
    if (other) PArchive_WriterClose(other);
    free(output); return FALSE;
}
const char* test1376_archive_writer_error(void) { return writer_error; }

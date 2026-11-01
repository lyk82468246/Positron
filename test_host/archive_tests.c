#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include "../positron_archive/positron_archive.h"
#include "archive_fixtures.h"
static char archive_error[160];
typedef struct ArchiveFixture {
    const unsigned char* data;
    unsigned int size, reads, fail;
    PArchiveReader reader;
    int reentry;
} ArchiveFixture;
static unsigned int archive_read(void* user, unsigned int offset,
        void* buffer, unsigned int bytes)
{
    ArchiveFixture* f = (ArchiveFixture*)user;
    unsigned int count;
    ++f->reads;
    if (f->reader) f->reentry = PArchive_Count(f->reader, &count);
    if (f->fail || offset > f->size || bytes > f->size - offset) return 0;
    memcpy(buffer, f->data + offset, bytes); return bytes;
}
static int archive_open_fixture(ArchiveFixture* f, const unsigned char* bytes,
        unsigned int size, const PArchiveLimits* limits, PArchiveReader* reader)
{
    PArchiveSource source;
    memset(f, 0, sizeof(*f)); f->data = bytes; f->size = size;
    source.size = sizeof(source); source.version = PARCHIVE_VERSION;
    source.archive_bytes = size; source.read_at = archive_read; source.user_data = f;
    return PArchive_Open(&source, limits, reader);
}
#define ACHECK(expression) do { if (!(expression)) { \
    sprintf(archive_error, "Archive contract failed at line %d", __LINE__); \
    if (r && r != (PArchiveReader)1) PArchive_Close(r); return FALSE; \
} } while (0)
#define REJECT(name, code) do { r = (PArchiveReader)1; \
    result = archive_open_fixture(&f, zip_##name, sizeof(zip_##name), NULL, &r); \
    if (result != code) { sprintf(archive_error, "Reject %s: expected %d actual %d", #name, code, result); \
        if (r && r != (PArchiveReader)1) PArchive_Close(r); return FALSE; } \
    ACHECK(r == (PArchiveReader)1); r = NULL; } while (0)
BOOL test1375_archive_contract(void)
{
    ArchiveFixture f, second;
    PArchiveReader r = NULL, other = NULL;
    PArchiveEntry entry;
    PArchiveLimits limits = {sizeof(PArchiveLimits), PARCHIVE_VERSION, 1, 16777216, 67108864, 1048576};
    unsigned char buffer[4096];
    unsigned int count, index, n, total, i, rounds;
    int result;
    WCHAR loaded[MAX_PATH], expected[MAX_PATH];
    WCHAR* separator;
    archive_error[0] = 0;
    ACHECK(GetModuleFileNameW(GetModuleHandleW(L"positron_archive.dll"), loaded, MAX_PATH) > 0);
    ACHECK(GetModuleFileNameW(NULL, expected, MAX_PATH) > 0);
    separator = wcsrchr(expected, L'\\');
    ACHECK(separator && separator - expected < MAX_PATH - 24);
    wcscpy(separator + 1, L"positron_archive.dll");
    ACHECK(lstrcmpiW(loaded, expected) == 0);
    r = (PArchiveReader)1;
    ACHECK(PArchive_Open(NULL, NULL, &r) == PARCHIVE_ARGUMENT && r == (PArchiveReader)1);
    r = NULL;
    REJECT(traversal, PARCHIVE_PATH); REJECT(absolute, PARCHIVE_PATH);
    REJECT(backslash, PARCHIVE_PATH); REJECT(reserved, PARCHIVE_PATH);
    REJECT(dot, PARCHIVE_PATH); REJECT(colon, PARCHIVE_PATH);
    REJECT(collision, PARCHIVE_PATH); REJECT(prefix, PARCHIVE_PATH);
    REJECT(unicode_collision, PARCHIVE_PATH);
    REJECT(method, PARCHIVE_UNSUPPORTED); REJECT(symlink, PARCHIVE_UNSUPPORTED);
    REJECT(mismatch, PARCHIVE_INVALID);
    REJECT(nul, PARCHIVE_PATH); REJECT(bad_utf8, PARCHIVE_PATH);
    REJECT(long_path, PARCHIVE_PATH); REJECT(overlap, PARCHIVE_INVALID);
    REJECT(encrypted, PARCHIVE_UNSUPPORTED); REJECT(zip64, PARCHIVE_UNSUPPORTED);
    REJECT(disk, PARCHIVE_UNSUPPORTED);
    ACHECK(archive_open_fixture(&f, zip_valid, sizeof(zip_valid), &limits, &r) == PARCHIVE_LIMIT);
    limits.max_entries = 1024; limits.max_entry_bytes = 20;
    ACHECK(archive_open_fixture(&f, zip_valid, sizeof(zip_valid), &limits, &r) == PARCHIVE_LIMIT);
    limits.max_entry_bytes = 16777216; limits.max_total_bytes = 100;
    ACHECK(archive_open_fixture(&f, zip_valid, sizeof(zip_valid), &limits, &r) == PARCHIVE_LIMIT);
    limits.max_total_bytes = 67108864; limits.max_memory_bytes = 65536;
    ACHECK(archive_open_fixture(&f, zip_many, sizeof(zip_many), &limits, &r) == PARCHIVE_LIMIT);
    ACHECK(archive_open_fixture(&f, zip_many, sizeof(zip_many), NULL, &r) == PARCHIVE_OK);
    ACHECK(PArchive_Count(r, &count) == PARCHIVE_OK && count == 200);
    PArchive_Close(r); r = NULL;
    for (rounds = 0; rounds < 16; ++rounds) {
        ACHECK(archive_open_fixture(&f, zip_valid, sizeof(zip_valid), NULL, &r) == PARCHIVE_OK);
        ACHECK(PArchive_Count(r, &count) == PARCHIVE_OK && count == 5);
        n = 99;
        ACHECK(PArchive_Read(r, buffer, 0, &n) == PARCHIVE_ARGUMENT && n == 0);
        ACHECK(PArchive_Read(r, buffer, sizeof(buffer), &n) == PARCHIVE_STATE);
        for (index = 0; index < count; ++index) {
            memset(&entry, 0, sizeof(entry)); entry.size = sizeof(entry); entry.version = 1;
            ACHECK(PArchive_Entry(r, index, &entry) == PARCHIVE_OK);
            ACHECK(PArchive_Begin(r, index) == PARCHIVE_OK);
            ACHECK(PArchive_Begin(r, index) == PARCHIVE_STATE);
            total = 0; f.reader = r;
            do {
                result = PArchive_Read(r, buffer, sizeof(buffer), &n);
                ACHECK(result == PARCHIVE_OK || result == PARCHIVE_DONE);
                if (index == 4) for (i = 0; i < n; ++i) ACHECK(buffer[i] == "abcdefgh"[(total + i) % 8]);
                if (index == 0) ACHECK(n == 7 && !memcmp(buffer, "hello\r\n", 7));
                total += n;
                ACHECK(total <= entry.uncompressed_bytes);
            } while (result != PARCHIVE_DONE);
            f.reader = NULL;
            ACHECK(total == entry.uncompressed_bytes);
        }
        ACHECK(f.reentry == PARCHIVE_STATE);
        ACHECK(PArchive_Begin(r, 4) == PARCHIVE_OK);
        ACHECK(PArchive_Read(r, buffer, 1, &n) == PARCHIVE_OK && n == 1);
        ACHECK(PArchive_Abort(r) == PARCHIVE_OK);
        ACHECK(PArchive_Begin(r, 0) == PARCHIVE_OK);
        ACHECK(PArchive_Abort(r) == PARCHIVE_OK);
        ACHECK(archive_open_fixture(&second, zip_empty, sizeof(zip_empty), NULL, &other) == PARCHIVE_OK);
        ACHECK(PArchive_Count(other, &count) == PARCHIVE_OK && count == 0);
        PArchive_Close(other); other = NULL;
        PArchive_Close(r); r = NULL;
    }
    ACHECK(archive_open_fixture(&f, zip_descriptor, sizeof(zip_descriptor), NULL, &r) == PARCHIVE_OK);
    ACHECK(PArchive_Begin(r, 0) == PARCHIVE_OK);
    ACHECK(PArchive_Read(r, buffer, sizeof(buffer), &n) == PARCHIVE_DONE && n == 1000);
    PArchive_Close(r); r = NULL;
    ACHECK(archive_open_fixture(&f, zip_crc, sizeof(zip_crc), NULL, &r) == PARCHIVE_OK);
    ACHECK(PArchive_Begin(r, 0) == PARCHIVE_OK);
    ACHECK(PArchive_Read(r, buffer, sizeof(buffer), &n) == PARCHIVE_INTEGRITY && n == 0);
    PArchive_Close(r); r = NULL;
    ACHECK(archive_open_fixture(&f, zip_valid, sizeof(zip_valid), NULL, &r) == PARCHIVE_OK);
    ACHECK(PArchive_Begin(r, 0) == PARCHIVE_OK);
    f.fail = 1;
    ACHECK(PArchive_Read(r, buffer, sizeof(buffer), &n) == PARCHIVE_IO && n == 0);
    PArchive_Close(r);
    return TRUE;
}
const char* test1375_archive_error(void) { return archive_error; }

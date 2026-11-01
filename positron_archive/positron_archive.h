/* Positron bounded ZIP reader/writer. Stable C ABI; no filesystem or Runtime dependency. */
#ifndef POSITRON_ARCHIVE_H
#define POSITRON_ARCHIVE_H
#include <windows.h>
#ifdef POSITRON_ARCHIVE_EXPORTS
#define PARCHIVE_API __declspec(dllexport)
#else
#define PARCHIVE_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define PARCHIVE_VERSION 1U
#define PARCHIVE_OK 0
#define PARCHIVE_DONE 1
#define PARCHIVE_ARGUMENT -1
#define PARCHIVE_INVALID -2
#define PARCHIVE_UNSUPPORTED -3
#define PARCHIVE_LIMIT -4
#define PARCHIVE_MEMORY -5
#define PARCHIVE_IO -6
#define PARCHIVE_STATE -7
#define PARCHIVE_PATH -8
#define PARCHIVE_INTEGRITY -9
#define PARCHIVE_PATH_BYTES 240U
#define PARCHIVE_CHUNK_BYTES 16384U
typedef struct PArchiveReaderTag* PArchiveReader;
/* Return exactly bytes on success, otherwise a short count (IO failure).
 * Input is immutable and seekable for the reader lifetime. Borrowed output
 * buffer is writable only during callback. No callback after Close. */
typedef unsigned int (*PArchiveReadAtFn)(void* user_data,
        unsigned int offset, void* buffer, unsigned int bytes);
typedef struct PArchiveSource {
    unsigned int size, version;
    unsigned int archive_bytes;
    PArchiveReadAtFn read_at;
    void* user_data;
} PArchiveSource;
typedef struct PArchiveLimits {
    unsigned int size, version;
    unsigned int max_entries;       /* 1..1024 */
    unsigned int max_entry_bytes;   /* 1..16 MiB */
    unsigned int max_total_bytes;   /* 1..64 MiB, advertised unique entries */
    unsigned int max_memory_bytes;  /* 64 KiB..1 MiB, includes reader/index */
} PArchiveLimits;
typedef struct PArchiveEntry {
    unsigned int size, version;
    unsigned int compressed_bytes, uncompressed_bytes, crc32;
    unsigned int method, is_directory;
    char path[PARCHIVE_PATH_BYTES + 1]; /* UTF-8 relative; directories end / */
} PArchiveEntry;
/* All calls synchronous, owner-thread only. No concurrent access, callback
 * reentry, or Close during a call. Handles valid until exactly one Close;
 * use-after-close/arbitrary pointers are caller errors. No global hooks.
 * Open validates ALL entries before publishing a handle; archive <=32 MiB.
 * Accept single-volume non-ZIP64 STORE/DEFLATE, ASCII or flag-11 UTF-8 names.
 * Reject encryption, links, unsafe/colliding WM paths and unknown flags.
 * Open/Entry leave output unchanged on failure. NULL limits use maxima.
 * Admission is metadata validation, not proof of payload CRC/integrity. */
PARCHIVE_API int PArchive_Open(const PArchiveSource* source,
        const PArchiveLimits* limits, PArchiveReader* out_reader);
PARCHIVE_API int PArchive_Count(PArchiveReader reader,
        unsigned int* out_count);
PARCHIVE_API int PArchive_Entry(PArchiveReader reader, unsigned int index,
        PArchiveEntry* out_entry);
/* One entry active at a time. Begin does not write output. Read advances
 * by <=16 KiB; OK means provisional bytes, DONE means size+CRC validated.
 * A final call may return DONE with bytes. Directory/empty entries supported.
 * On negative result discard ALL output for that entry; buffer may contain
 * provisional bytes but written is zero. I/O/native decompression synchronous;
 * chunking is not a hard CPU-time guarantee. Abort discards active state and
 * permits another Begin; it cannot interrupt an in-flight call. Caller stages
 * files and commits only after DONE; no filesystem rollback promised here.
 * Calls may repeat entries; total limit is not a lifetime read-byte quota. */
PARCHIVE_API int PArchive_Begin(PArchiveReader reader, unsigned int index);
PARCHIVE_API int PArchive_Read(PArchiveReader reader, void* buffer,
        unsigned int capacity, unsigned int* written);
PARCHIVE_API int PArchive_Abort(PArchiveReader reader);
PARCHIVE_API void PArchive_Close(PArchiveReader reader); /* NULL-safe */

/* Additive writer family; reader ABI and limits are unchanged. */
#define PARCHIVE_CANCELLED -10
#define PARCHIVE_COMPRESSION_STORE 0U
#define PARCHIVE_COMPRESSION_FAST 1U /* bounded miniz level 1; tiny entries may STORE */
#define PARCHIVE_WRITER_OPEN 1U
#define PARCHIVE_WRITER_COMPLETE 2U
#define PARCHIVE_WRITER_FAILED 3U
#define PARCHIVE_WRITER_CANCELLED 4U
typedef struct PArchiveWriterTag* PArchiveWriter;
/* Sequential sink: every call offset equals bytes previously acknowledged.
 * Return exactly bytes, otherwise the whole archive fails (no retry/seek).
 * Callback block <=16 KiB, borrowed only during callback. */
typedef unsigned int (*PArchiveWriteFn)(void* user_data, unsigned int offset,
        const void* buffer, unsigned int bytes);
/* Nonzero requests cancellation, checked between I/O blocks and at Finish.
 * May read a host-owned atomic flag; must not reenter or destroy the writer. */
typedef int (*PArchiveCancelFn)(void* user_data);
typedef struct PArchiveSink {
    unsigned int size, version;
    unsigned int max_archive_bytes; /* 1..32 MiB, includes headers/directory */
    PArchiveWriteFn write;
    PArchiveCancelFn cancelled;     /* optional */
    void* user_data;
} PArchiveSink;
typedef struct PArchiveWriterInfo {
    unsigned int size, version;
    unsigned int state;
    unsigned int entry_count, input_bytes; /* completed successful Add calls */
    unsigned int output_bytes; /* fully acknowledged sink blocks, including partial archive */
    int error; /* first terminal error; zero while open/complete */
} PArchiveWriterInfo;
/* Create copies sink/limits, invokes no callbacks, preserves output on failure.
 * Same limits as reader: entry/total are uncompressed input budgets. All heap,
 * compressor and directory/index memory share max_memory_bytes. */
PARCHIVE_API int PArchive_WriterCreate(const PArchiveSink* sink,
        const PArchiveLimits* limits, PArchiveWriter* out_writer);
/* Synchronously add ONE complete entry; no resumable yield or message pump.
 * UTF-8 path uses the reader's admission/collision rules. NULL source means
 * zero bytes; otherwise source size/version and archive_bytes denote this
 * entry, read_at is exact-read and borrowed ONLY until Add returns. Directory
 * paths end '/', require zero input and STORE. FAST may STORE inputs <=3 bytes.
 * Parameter/path/input-limit rejection invokes no callbacks and leaves writer
 * usable. Once upstream work begins ANY failure is terminal: discard whole
 * sink, including preceding entries. Source bytes must remain immutable.
 * Use a worker to avoid blocking UI; native I/O/compression CPU calls are not
 * preempted. Distinct handles may be independently owned, no same-handle
 * concurrency/reentry. A successful Add is NOT a finished archive. */
PARCHIVE_API int PArchive_WriterAdd(PArchiveWriter writer, const char* path,
        unsigned int compression, const PArchiveSource* source);
/* Finish writes central directory/EOCD, returns DONE only after all writes
 * acknowledged. Host still must flush/close/commit its temporary sink; DLL
 * does not own filesystem atomicity. Repeat Finish or Add after terminal state
 * returns STATE with no callbacks. Close NEVER implicitly finalizes. */
PARCHIVE_API int PArchive_WriterFinish(PArchiveWriter writer);
PARCHIVE_API int PArchive_WriterGetInfo(PArchiveWriter writer,
        PArchiveWriterInfo* out_info);
/* Idle cancellation; idempotent once cancelled, no callbacks or output cleanup.
 * Cross-thread cancellation uses sink.cancelled reading an atomic host flag. */
PARCHIVE_API int PArchive_WriterAbort(PArchiveWriter writer);
PARCHIVE_API void PArchive_WriterClose(PArchiveWriter writer); /* NULL-safe */
#ifdef __cplusplus
}
#endif
#endif

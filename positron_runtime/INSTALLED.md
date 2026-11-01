# Read-only installed revision verification

This is an additive native contract in `positron_runtime.h`. It prepares an immutable resource inventory, builds explicitly confirmed registration data, and verifies an already committed first-install record before read-only resource access. It does not install files, persist registration, open installed SQL storage, authorize Browser services or execute author code. Existing development-directory/storage APIs and layouts are unchanged.

## Ownership and trust

Runtime owns inventory admission, ordering, SHA-256 framing, manifest comparison, budgets and job state. The trusted native `PRuntimeRevisionProvider` owns canonicalization, complete regular-file enumeration, physical containment, immutable root retention, native coordination and reads. It must reject links, aliases, case collisions or containment it cannot prove. Resources and the complete file set must remain unchanged until Close, including after successful verification. A timestamp/size check or copying a path string is insufficient.

`PRuntimeInstalledProvider.open` reads committed native registration and holds an exclusive per-installation lease through Close; duplicate launch returns BUSY. Update/uninstall must honor that lease. Runtime checks the expected generation, active state, ID, revision, API, capabilities and canonical root, then refreshes registration before completing verification and before every installed read. A copied approval supplied by author code, a development handle or an `app://` URL cannot replace this provider.

Providers are trusted native code, not a sandbox boundary against a malicious host. This batch supplies an in-memory regression provider, not a general WM filesystem canonicalizer or an EXE registry adapter. SHA-256 comes from native digest callbacks; an unavailable backend returns UNSUPPORTED. Runtime has no new TLS dependency, private hash implementation or weaker digest fallback. CSP allocations are provider-owned.

## Content and identity

All regular files participate, including raw root `manifest.json`, empty files and unreferenced assets. Directories have no inventory record. Paths use the existing decoded package-key rules and are sorted by UTF-8 bytes, independent of enumeration order. The manifest must be nonempty and valid; its entry and every migration source must exist. This verifies presence and content, not HTML validity or executable SQL.

Each file receives SHA-256 over its exact bytes. The resource-set SHA-256 input is the following concatenation, without padding:

1. ASCII `Positron.ResourceSet.v1` **including its terminating NUL**;
2. file count as unsigned 32-bit little-endian;
3. for each sorted file: unsigned 32-bit little-endian path byte length, UTF-8 path without NUL, unsigned 32-bit little-endian file size, then its 32 digest bytes.

An EOF probe rejects undeclared suffix bytes; short data fails. Content identity excludes native root, installation ID and registration generation. Renames, additions, removals and same-size byte changes invalidate the recorded content. SHA-256 identifies resources; it does not authenticate a publisher.

The native installer supplies a nonzero opaque 128-bit installation ID encoded as exactly 32 lowercase hex characters. It is separate from the normalized logical app ID and from content identity. V1 only admits **first-install revision 1**; later revisions return UNSUPPORTED. App version remains the manifest's positive integer. Requested and approved capability sets must match exactly, with API/digest/approval-policy version 1 and no unknown bits. Even a zero-capability candidate needs explicit confirmation.

Approval snapshots are copied native registration data, not bearer authority. Persist fields in a deterministic schema, never raw C struct bytes/padding. `RevisionBuildApproval` performs no COMMIT and does not turn a prepare handle into an installed handle.

## Driving the job

`PrepareRevision` copies a native directory/provider/options into a prepare job. `OpenInstalled` instead takes an installation ID, expected committed generation and registry provider; the registry chooses the directory. Begin calls allocate only and invoke no callback. All new structures require exact size/version 1.

```c
PRuntimeRevisionHandle job = NULL;
PRuntimeRevisionInfo info;
int rc;

rc = PRuntime_OpenInstalled(installation_id, 32, generation,
    &registry_provider, &resource_provider, NULL, &job);
if (rc == PRUNTIME_OK) {
    do {
        rc = PRuntime_RevisionStep(job);
        /* A worker scheduler may return to its caller between steps.
         * Keep job, callback code and provider targets alive. */
    } while (rc == PRUNTIME_OK);
    if (rc == PRUNTIME_REVISION_DONE) {
        info.size = sizeof(info);
        info.version = PRUNTIME_VERSION;
        rc = PRuntime_RevisionGetInfo(job, &info);
        /* GetFile enumerates the verified catalog; InstalledReadFile copies
         * bounded chunks using that catalog's index and explicit offset. */
    }
    PRuntime_RevisionClose(&job);
}
```

States are INITIALIZING, PENDING, COMPLETE, FAILED and CANCELLED. Step returns OK while pending, DONE on completion, otherwise a terminal negative result. It cannot restart a terminal job. Pending/failed info exposes progress but zeroes verified identity/content metadata. `GetFile` requires COMPLETE. `InstalledReadFile` additionally requires installed-kind; prepare jobs return DENIED. Successful reads copy at most the remaining bytes (zero at EOF), never append NUL, and refresh registration first. Buffer/count stay unchanged on failure; I/O, changed registration or cancellation retires the job.

Create, Step, query, read, cancel and Close use one creating thread. Callbacks are synchronous and cannot reenter the active job, close it, or recurse into Begin. No hidden thread or message pump exists. Cancel is cooperative at an idle call boundary, can retire a complete job, and cannot preempt provider I/O. Callback targets survive until Close. Failure closes the active digest; root/registry tokens remain held until Close, including partial acquisitions. Close releases hash, manifest, root, then registry lease, clears the owned slot and accepts repeated NULL values. Active/wrong-thread Close returns STATE without clearing it; freed aliases are invalid.

## Budgets and failures

| Limit | V1 bound |
| --- | --- |
| Regular files / path depth | 1024 / 16 segments |
| Path / canonical root | 255 / 1023 UTF-8 bytes |
| File / complete resources | 8 MiB / 32 MiB |
| Manifest | Existing 8192-byte strict JSON contract |
| One Step | At most 64 KiB read, or 16 enumeration/framing items |
| One installed read | Capacity 1–65536 bytes |
| DLL workset | Job plus JSON/temporary reserve ≤512 KiB; provider/CSP memory excluded |

These are processing and allocation limits, not a native-I/O wall-clock deadline. Core/Browser may impose lower resource limits. ARGUMENT rejects malformed API headers/indices/options, LIMIT rejects capacity, INVALID rejects resource/record mismatch or invalid manifest/path, UNSUPPORTED rejects future revisions/API/capabilities/digest, DENIED rejects missing/revoked/unapproved registration, STATE rejects owner/reentry/lifecycle misuse, BUSY rejects a held lease, IO reports provider failures and MEMORY reports allocation failure. Cancellation returns CANCELLED. Begin/query/approval failures preserve caller outputs; no failure writes registration, starts services or falls back to development identity.

## Validation and remaining work

The regression uses independently generated Python hashlib digests and device CryptoAPI SHA-256, with an `abc` known-answer check. It covers empty/binary/Unicode resources, enumeration order and root/installation-ID independence, exact reads, approval, duplicate leases, tamper/rename/add/remove, revocation/generation/API/capability failures, path/size/count limits, cancellation, thread/reentry and partial-token teardown. Device evidence is recorded in [the acceptance record](../docs/history/RUNTIME_REVISION_ACCEPTANCE.md).

Actual PXI isolation/extraction, registration COMMIT/recovery, installed SQL identity/storage, update/uninstall and EXE permission/launch integration remain separate work. In particular, development per-file migrations cannot promise rollback of an installed update across two databases. No application should suppress its existing development warning merely because this verification API is available.

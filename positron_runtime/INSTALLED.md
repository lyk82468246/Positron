# Revision verification and native installation intents

These native contracts in `positron_runtime.h` cover immutable revision verification, inactive installation intents, identity-bound database preparation and READY storage with page SQL queues. Final READY registration is coordinated by a retained native commit job; file publication and EXE installation remain separate work; Browser registration and author execution require explicit host scheduling. Existing development-directory/storage APIs and layouts are unchanged.

## Ownership and trust

Runtime owns inventory admission, ordering, SHA-256 framing, manifest comparison, budgets and job state. The trusted native `PRuntimeRevisionProvider` owns canonicalization, complete regular-file enumeration, physical containment, immutable resource-view retention, native coordination and reads. It must reject links, aliases, case collisions or containment it cannot prove. The complete observable catalog and bytes must remain unchanged until Close, including after successful verification. This may be a protected physical view or a complete native readonly snapshot, not merely timestamp/size checks or a copied path.

A snapshot captures every regular file, including the raw manifest, migration sources, empty and unreferenced resources. Capture must retain files, verify exact bytes/identity and the complete inventory, and fail on observed change or incomplete enumeration. It does not claim an atomic filesystem snapshot against malicious uncoordinated writers. Once published, enumeration and every read use only the retained snapshot; unknown keys fail, with no physical-file fallback or catalog expansion. Canonical still identifies the proven physical source root, while content identity binds the verified view. Root and installation/location coordination remain required. Physical changes after publication cannot alter this instance's bytes; ordinary OpenInstalled must capture the current source anew and reject content inconsistent with registration. This does not promise that the OS prevents arbitrary external additions or that a later startup will succeed.

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

These are processing and allocation limits, not a native-I/O wall-clock deadline. Core/Browser may impose lower resource limits. ARGUMENT rejects malformed API headers/indices/options, LIMIT rejects capacity, INVALID rejects resource/record mismatch or invalid manifest/path, UNSUPPORTED rejects future revisions/API/capabilities/digest, DENIED rejects missing/revoked/unapproved registration, STATE rejects owner/reentry/lifecycle misuse, BUSY rejects a held lease, IO reports provider failures and MEMORY reports allocation failure. Cancellation returns CANCELLED. Revision and readback operations never write registration or start services. The intent coordinator below may have committed native registration before an error; its state must be inspected instead of assuming rollback.

## Reconciling native registration readback

`PRuntime_RevisionReconcileRegistration` compares one native transition with a fresh committed read, using a COMPLETE prepare-kind job. Its independent size/version 1 record includes state, generation, target schema, canonical root and approval. It does not alter `PRuntimeInstalledRecord` or reinterpret its `active` field. Pass native transition metadata and a `PRuntimeRegistrationProvider` that acquires nonblocking app/installation coordination, reads once, then closes; even partial leases are closed before return. Call it after the original native transaction has settled, without already holding the same exclusive lease.

The DLL validates manifest/content/root/schema equality and first-install transition rules before calling the provider. It permits absent→PREPARING at generation 1; PREPARING→READY/FAILED/CANCELLED; and explicitly confirmed FAILED→PREPARING. Later transitions increment generation by exactly one, without overflow or changing approval. New intent and FAILED retry require `confirmed=1`. READY/CANCELLED/REVOKED cannot be revived through this interface.

| Readback result | Meaning |
| --- | --- |
| `MATCH_PROPOSED` | Exact proposed record observed; compatible with confirmed or unknown commit outcome |
| `MATCH_PREVIOUS` | Exact previous record, or genuine absence for a new intent; compatible with not-committed or unknown outcome |
| `READY_WON_CANCEL` | An unknown PREPARING→CANCELLED attempt instead observes exact READY at the proposed generation; do not cancel or clean it as incomplete |

These are copied **metadata observations**, not permission to retry CAS, claim storage readiness or start an application. READY still requires ordinary installed resource/storage verification. An outcome inconsistent with readback, changed identity/generation/content or an unrelated record fails INVALID. Missing previously existing registration is DENIED. Inaccessible registration must be IO, never NOT_FOUND; an unreadable result remains unresolved and blocks progress. Provider errors and all other failures preserve output and the revision. Repeated reconciliation does not write, replay SQL, advance generation or retire the candidate.

```c
PRuntimeRegistrationDecision decision;
decision.size = sizeof(decision);
decision.version = PRUNTIME_VERSION;
rc = PRuntime_RevisionReconcileRegistration(prepared_revision,
    &native_transition, &committed_read_provider, &decision);
/* Only examine decision on OK. Install coordination must separately enforce
 * data preparation, explicit retry policy and the final startup gate. */
```

There is no allocation, filesystem/SQL operation, CAS or background callback in this call. Runtime uses fixed local record copies (under 8 KiB); native provider memory/I/O is outside that processing bound. The creating-thread and active-callback guards cover acquire/read/close. Native callback targets are borrowed only until return; the caller still owns the retained revision/root. Production registration persistence and the installation coordinator are not supplied by this observation primitive.

## Coordinating an inactive installation intent

`PRuntime_InstallIntentBegin` accepts a live COMPLETE prepare-kind revision, exact v1 options and a copied `PRuntimeInstallIntentProvider`. `approval` uses the existing explicit native confirmation contract. Begin allocates one fixed job (compile-time ≤8 KiB), performs no callback and retains its parent. At most one intent owns a revision. While retained, parent Cancel/Close, approval building, standalone reconciliation and a second intent return STATE. Idle parent info/catalog queries remain available. During callbacks both child and parent reject reentry.

| Mode | Admission |
| --- | --- |
| NEW | Registration must be genuinely absent under the namespace/installation lease; CAS proposes PREPARING generation 1 |
| RESUME | Exact approved resource/root/schema and PREPARING registration required; reads it without replaying CAS |
| RETRY_FAILED | Exact FAILED registration and renewed native confirmation; CAS proposes PREPARING at generation+1, overflow rejected |

The native provider supplies nonblocking coordination, fresh committed read, atomic exact compare-exchange and release. A NULL expected record means both installation ID and logical app ID must be unoccupied; uniqueness is checked atomically by the provider. CAS always reports CONFIRMED/NOT_COMMITTED/UNKNOWN, even on an error, and leaves no active transaction or background write. Runtime compares explicit fields; native persistence must not serialize C struct padding. The fixture provider is not an EXE SQL implementation.

Each Step performs at most one acquire, read or CAS callback. It returns OK while pending, DONE when a PREPARING record is verified, or a negative terminal result. Every attempted CAS is followed by fresh readback, including error/unknown outcomes. Exact committed readback can prove the write despite a lost response. An unchanged previous record fails without automatically repeating CAS. An inaccessible post-CAS read returns **UNRESOLVED (-14)** and preserves the underlying provider result; it is never treated as absence. Conflicting/malformed records fail closed. Leases remain held in every terminal state until Close.

Intent states are INITIALIZING, PENDING, PREPARED, FAILED, CANCELLED and UNRESOLVED. PREPARED means only that an **inactive** durable installation intent exists. GetInfo copies state/error/provider result, the most recent CAS outcome and known committed record; `record_known=0` explicitly means the record is unresolved. A known record is native metadata, not an installed handle, storage grant or safe-to-delete indication. No path in this coordinator writes READY.

Cancel marks a request without I/O. Before a NEW/retry CAS it cancels locally, preserving any old registration. After an attempted write it first resolves that write; if exact PREPARING exists, subsequent steps CAS CANCELLED at generation+1 and read back. RESUME cancellation also reads and validates its existing intent before changing it. A cancellation request on PREPARED schedules this same path; callers must continue Step until terminal. An observed READY winning cancellation is retained as known metadata and blocks with STATE, without rewriting or cleaning it. Cancellation cannot undo a COMMIT or preempt native I/O.

```c
PRuntimeInstallIntentHandle intent = NULL;
PRuntimeInstallIntentInfo info;
rc = PRuntime_InstallIntentBegin(prepared_revision, &intent_options,
    &native_registry_provider, &intent);
if (rc == PRUNTIME_OK) {
    do { rc = PRuntime_InstallIntentStep(intent); } while (rc == PRUNTIME_OK);
    info.size = sizeof(info);
    info.version = PRUNTIME_VERSION;
    PRuntime_InstallIntentGetInfo(intent, &info);
    /* PREPARED is NOT installation completion. UNRESOLVED requires later
     * explicit recovery; never replay the write based on a lost response. */
    PRuntime_InstallIntentClose(&intent);
}
```

All calls use the parent's creating thread; provider targets live through Close, and no callback survives return. Close may abandon a pending/unknown job but performs no implicit cancellation, deletion or CAS. It releases the coordination lease before detaching the parent; the caller may then close the parent/root. Reopen with explicit RESUME after revalidating the same resources to inspect a committed PREPARING intent. Close clears the owned slot and accepts repeated NULL values; active/wrong-thread Close preserves it. An attached data-preparation child blocks intent Cancel/Close until that child closes. Final READY registration remains separate.

## Preparing the installation database

`PRuntime_InstallDataBegin/Step/GetInfo/Cancel/Close` prepare SQL data from one live PREPARED intent. Begin copies exact v1 options/provider, checks the approved SQL bit and VM budget, and retains the intent without callbacks. A second child returns STATE. No-SQL applications return DENIED before any data-location call; a complete installer should skip data preparation for them. The handle exposes no raw DB, author SQL execution or service queue. PREPARED data still has an inactive PREPARING registration.

Runtime generates `v1/installed/<installation-id>/app.db` beneath the trusted native data provider. The exact DB TEXT stamp is `Positron.storage.v1:installed:<canonical-app-id>:<installation-id>`. Root/content/revision/generation do not select data identity, and no development bucket is reused. `PDb_BindLocalIdentity` admits only a pristine unclaimed DB or an exact existing stamp; wrong identity/nonempty unclaimed data fail without reassignment, fallback or deletion. Binding may already have committed before a later failure.

Step refreshes the exact PREPARING record before location/DB preparation, before each migration and before completion. It opens/binds the DB, reads strict stored schema, and copies **all remaining** migration files from the retained immutable root before executing any of them. Sources are nonempty UTF-8, without BOM/NUL/unsupported controls, at most 32768 bytes each and sixteen files/512 KiB total. Lexically valid but invalid SQL can still fail during DB execution; source admission is not an SQL dry-run. Unknown/future/corrupt schema fails, with no downgrade.

Each migration runs through `PDb_ApplyMigrationRestricted(expected, expected+1)` and commits independently. A later failure retains the committed prefix. After explicit FAILED→PREPARING retry, the strict schema determines the pending suffix; previously committed SQL is never replayed. A zero-migration SQL application binds its own DB at schema 0. Successful completion rechecks target schema and registration, retaining DB/location and parent leases until Close.

On failure/cancellation, subsequent steps attempt an exact FAILED/CANCELLED CAS and fresh readback. The primary `PRuntimeStorageError` remains intact; `registration_result` and `registration_outcome` separately report secondary error or UNRESOLVED. The intent is retired to failed/cancelled/unresolved rather than left usable in memory. No failed recording is automatically replayed, and no committed migration is rolled back to make a failed install look pristine. The provider must support these failure states as well as PREPARING. An unreadable result requires explicit recovery after close; it never authorizes startup.

States are INITIALIZING, PENDING, PREPARED, FAILED and CANCELLED. Step returns OK while work or failure recording remains pending, DONE only for prepared data, otherwise the primary terminal error. GetInfo includes previous/current/target schema, applied-file count, native-only key and independent DB/registry diagnostics. Native messages may contain SQL/paths: do not blindly forward them to author JS. Cancellation is a request; continue Step to terminal. It cannot preempt native I/O or undo an already committed file.

```c
PRuntimeInstallDataHandle data = NULL;
PRuntimeInstallDataInfo info;
rc = PRuntime_InstallDataBegin(prepared_intent, &data_options,
    &trusted_data_provider, &data);
if (rc == PRUNTIME_OK) {
    do { rc = PRuntime_InstallDataStep(data); } while (rc == PRUNTIME_OK);
    info.size = sizeof(info);
    info.version = PRUNTIME_VERSION;
    PRuntime_InstallDataGetInfo(data, &info);
    /* DONE means data prepared, not installation complete or service grant. */
    PRuntime_InstallDataClose(&data);
}
```

All operations use the creating worker; callbacks guard data, intent and revision against reentry/close. A Step performs one complete DB operation or native resource/registry operation, plus the optional cancellation predicate; one migration is not a resumable SQL continuation or wall-clock bound. The combined Runtime-owned parent/intent/data/source workset plus fixed reserve is compile-time limited to 1280 KiB, excluding DB/CSP/native provider allocations. Existing DB quota, authorizer and 100..1000000 VM work budget per file remain unchanged. Close frees DB, location and copied sources, then detaches from the intent; it does not register, cancel or delete durable data. Close a pending job only if prepared to recover its partial committed prefix explicitly. After data is PREPARED, its Cancel returns STATE; to cancel installation instead, use CommitCancel while a commit child is attached, or close the data child and drive the intent cancellation path. Both retain the prepared database.

## Committing the prepared installation

`PRuntime_InstallCommitBegin(intent, data, &commit)` accepts a live PREPARED intent and, only when SQL is approved, its live PREPARED data child. A no-SQL installation passes NULL and does not create a database. Copied metadata or another intent's data cannot substitute for these handles. Begin allocates without callbacks, retaining intent, resource root, native coordination, and DB/location. Intent Cancel/Close and DataClose refuse until CommitClose, including after failure or success.

Drive CommitStep to a terminal result. It freshly compares the complete PREPARING record, checks the retained database is idle and its strict schema still equals the manifest target, reads PREPARING again, attempts **one** exact generation+1 CAS, then reads back the committed record. The retained DB's identity was bound during data preparation; no raw DB or author SQL has been exposed in between. Immutable resources/location and exclusive native coordination must remain valid throughout. The job never creates, binds, migrates, repairs or deletes data, and does not claim cross-database atomicity.

```c
PRuntimeInstallCommitHandle commit = NULL;
PRuntimeInstallCommitInfo result;
rc = PRuntime_InstallCommitBegin(prepared_intent, prepared_data_or_null, &commit);
if (rc == PRUNTIME_OK) {
    do { rc = PRuntime_InstallCommitStep(commit); } while (rc == PRUNTIME_OK);
    result.size = sizeof(result);
    result.version = PRUNTIME_VERSION;
    PRuntime_InstallCommitGetInfo(commit, &result);
    /* DONE confirms exact READY registration, not a running application. */
    PRuntime_InstallCommitClose(&commit);
}
/* Then close data, intent, revision. Start separately via OpenInstalled. */
```

States are PENDING, COMPLETE, FAILED, CANCELLED and UNRESOLVED. DONE means exact READY, including a lost CAS response followed by matching committed readback. Definite outcomes inconsistent with readback, a competing generation or altered identity fail closed. An unchanged PREPARING after an unknown/not-committed attempt is a failed attempt, not permission to repeat it automatically. An unreadable post-CAS result becomes UNRESOLVED. Closing between CAS and readback also makes the intent UNRESOLVED; a new finalizer cannot replay that write. Failure preserves files and holds leases until Close; no implicit FAILED write is attempted. Explicit recovery must reacquire coordination and examine persistent facts.

CommitCancel requests cancellation without I/O. Before CAS it changes the proposed state to CANCELLED; after CAS the job only resolves readback. Confirmed READY wins cancellation, including an unknown CANCELLED attempt whose exact READY peer is observed. READY is never rewritten as CANCELLED. Parent/data cancellation predicates are checked before CAS; cancellation cannot preempt a callback or undo COMMIT. Once finalized the intent reports FINALIZED and rejects cancellation or data preparation. Close is still required.

Calls use the creating worker and reject callback reentry and other threads. Step has at most one registration-provider operation or a bounded retained-DB check, plus optional native cancellation predicates; no message pump, background worker or hard I/O deadline. Combined Runtime-owned parent/intent/data/commit/source workset and reserve remain compile-time limited to 1280 KiB, excluding DB/CSP/provider memory. GetInfo copies only an exact v1 result; committed record is populated only for COMPLETE/CANCELLED. Provider diagnostics remain native, not author-facing text. Normal startup still independently validates READY resources and existing storage before Browser service registration.

## Opening READY installed storage

`PRuntime_InstalledStorageOpen` accepts only a live COMPLETE installed-kind revision, approved `storage.sql`, v1 `PRuntimeStorageOptions` and a separate v1 `PRuntimeInstalledStorageProvider`. A prepare/development handle or copied approval is not a substitute. Granted bits are a native use mask within the committed approval, not a renewed installation confirmation. They cannot exceed the existing SQL option; no SQL grant means DENIED before any data-location callback. Only one storage child may attach to a revision.

The provider's `open_existing_location` receives the installed key and returns a borrowed-location token and explicit UTF-8 native path. It must retain an already existing contained physical file/volume with an exclusive location lease: **no creation, deletion, overwrite or browser/development fallback**. It is deliberately a different callback table from the development/preparation provider. Partial tokens are closed even when acquisition fails. The installed parent already retains the exclusive installation coordination, immutable root and registry lease; the native adapter must honor that coordination for update/revocation.

Runtime refreshes the exact READY record before location acquisition, before DB admission, after admission and before every valid SQL request. It uses `PDb_OpenExistingLocalUtf8` with the full installed identity and exact manifest migration count. There is no CREATE, identity binding, migration, repair or retry path. A missing/readonly DB, wrong/missing identity, unclaimed data or future/corrupt schema publishes no storage handle and leaves existing file bytes unchanged on the ordinary rejection corpus. Native/SQLite I/O failures retain their separate diagnostics; no physical fault/durability guarantee is implied.

```c
PRuntimeStorageHandle storage = NULL;
PRuntimeStorageOptions options;
PRuntimeStorageError error;
memset(&options, 0, sizeof(options));
options.size = sizeof(options);
options.version = PRUNTIME_VERSION;
options.granted_permissions = PRUNTIME_PERMISSION_SQL;
options.vm_budget = 1000000;
error.size = sizeof(error);
error.version = PRUNTIME_VERSION;
rc = PRuntime_InstalledStorageOpen(verified_installed_revision, &options,
    &existing_location_provider, &storage, &error);
if (rc == PRUNTIME_OK) {
    /* Same worker: existing StorageExecuteJson/GetInfo/GetError contracts. */
    PRuntime_StorageClose(&storage);
}
/* Only after storage closes may RevisionClose release root/registry leases. */
```

The returned opaque Storage uses the existing typed SQL/JSON encoder and budgets; it is not cast to a development directory and exposes no raw DB. Query/result limits, provisional rows, COMMIT-before-output and rollback behavior remain the [existing storage contract](README.md). A changed/inactive/unreadable registration retires storage and parent before executing SQL; output bytes/count are unchanged. Cancellation is cooperative and never preempts provider I/O or undoes a committed request. Instance cancellation retires storage and all its queues; a page/request cancellation remains local and does not revoke a healthy installation. The native lease makes registry refresh meaningful; it is not a cross-database atomic transaction.

Info/error remain queryable on the creating worker after retirement. Close releases DB, location, then the parent hold. Parent Cancel/Close and duplicate storage opening return STATE while a child remains attached; wrong-thread and callback reentry cannot close active objects. Callback code/user targets live through Close; invalid/failure handle output is preserved, and a valid optional error output receives the failure. Invalid API/error headers leave outputs untouched. The combined revision/storage/SQL-work, four queues and fixed reserve are compile-time bounded by 1280 KiB, excluding DB/CSP/provider allocations; existing SQL VM/JSON quotas are unchanged. The queue contract below applies without changing existing ABI or starting a worker inside the DLL. Final READY COMMIT uses the separate retained installation job below.

## Binding installed page queues to Browser

`PRuntime_ServiceQueueCreate` accepts verified READY installed storage as well as development storage. For installed storage it refreshes registration before publishing the queue, under the storage/parent callback guards. At most four queues share one storage, with eight waiting/running/unclaimed-result slots per queue. Each binds a nonzero tab/page generation and UI delivery thread; bridge/request tokens retain the existing monotonic replay rejection. A queue retains storage, which retains its installed revision and native leases. No Browser service is enabled just by creating a queue or navigating to an app URL.

The host keeps the same split as the [shared queue contract](README.md#bounded-page-sql-service-queue): create/process/close on the storage worker; submit/take/cancel and Browser registration/completion/pump on the specified UI thread. Submit copies the request, without SQL or I/O. ProcessOne calls the shared restricted executor, including the installed registration check, before any author SQL. Ordinary SQL errors return the existing sanitized numeric error JSON. Only committed results are eligible for UI generation/token validation and Browser Complete/Pump; a failed or stale result never reexecutes SQL.

Worker-observed registration loss/change, instance cancellation, or a DB failure that leaves the storage unusable retires the installation storage and revokes all sibling queues. Waiting jobs and unclaimed results are dropped, and a running job receives cooperative cancellation. No result is published from that retired processing call. UI GetInfo observes `revoked=1`; it is a copied queue state, **not a native-registry query**. The worker must notify UI to revoke Browser services on the session owner before teardown. Runtime never touches a live Browser/Core/Script handle from the worker. Browser revocation drops pending callback references without running JS callbacks; it cannot reverse an already committed write.

Page QueueRevoke and individual QueueCancel alone do not retire storage or sibling pages. A cancelled running request discards its result and leaves a healthy instance usable by another queue. An explicit instance cancellation flag remains a separate reason and permanently retires the instance. The existing native coordination lease must prevent uncoordinated registry mutation; queue snapshots cannot promise instantaneous external revocation detection or cross-database atomicity.

Shutdown is a handshake: revoke Browser services and its queue, stop all UI access, let running processing return, then close each revoked/idle queue on the worker, followed by StorageClose and RevisionClose. QueueClose refuses active storage/parent callbacks, including callbacks that try to close a non-running sibling queue. Keep all callback targets and queue pointers alive until UI has acknowledged that it will no longer submit/cancel/take. A processed committed write may have its result dropped by page revocation; that is an unknown-to-the-page outcome, never permission to retry it automatically.

## Validation and remaining work

Final-commit regression uses the exact native CAS fixture with real prepared DB files: no SQL, schema 0, a non-idempotent migration, schema alteration after preparation, contradictory outcomes, lost responses, inaccessible readback, competing generations, cancellation before/after CAS, READY winning cancellation, pending close, owner/reentry and parent/data retention. Persistent EXE registry transactions remain a separate integration gate. Evidence and unresolved adjacent failures are kept in the [final-commit acceptance record](../docs/history/RUNTIME_FINAL_COMMIT_ACCEPTANCE.md).

The regression uses independently generated Python hashlib digests and device CryptoAPI SHA-256, with an `abc` known-answer check. It covers empty/binary/Unicode resources, enumeration order and root/installation-ID independence, exact reads, approval, duplicate leases, tamper/rename/add/remove, revocation/generation/API/capability failures, path/size/count limits, cancellation, thread/reentry and partial-token teardown. Device evidence is recorded in [the acceptance record](../docs/history/RUNTIME_REVISION_ACCEPTANCE.md).

Registration regression covers lost responses, definite-outcome contradictions, previous/proposed readback, READY winning cancellation, failure retry confirmation, tombstone rejection, capacity/version/identity conflicts, unreadable/absent records, partial leases and repeated reconciliation. It models native committed state; it does not prove real registry COMMIT or database preparation.

Intent regression additionally checks one-callback steps, exactly-once CAS attempts, lost-response success, unknown readback/close/resume without replay, confirmation, duplicate children, real wrong-thread calls, parent/child callback guards, partial leases and cancellation before/after CAS or preparation. These in-memory registry fixtures do not prove EXE database persistence.

Installation-data regression uses real private PDb files for full identity binding, zero/two migrations, all-source admission, wrong/unclaimed/future DB rejection, partial location failure, cancellation and exact prefix/suffix recovery. An independent post-close query counts rows/schema; a registry recording failure cannot overwrite the original DB error. The registry fixture still models committed native records rather than EXE SQL persistence.

DB now supplies [read-only existing-file verification](../positron_db/README.md#verifying-an-existing-local-database-without-writes), returning an observation without a handle. It cannot authorize a later CREATE-capable open. DB also supplies [writable existing-handle admission](../positron_db/README.md#opening-a-verified-existing-local-database), with a retained read snapshot, no-CREATE writable recheck and caller-held native location/coordination leases. Runtime composes that handle through InstalledStorageOpen above. Installed queues compose with the existing Browser bridge as above. PXI isolation/extraction, update/uninstall and EXE permission/launch integration remain separate work. The [first-install recovery contract](../docs/RUNTIME_INSTALL_RECOVERY.md) requires inactive durable intent, independent data preparation, then READY registration. Development per-file migrations cannot promise rollback of an installed update across two databases. No application should suppress its existing development warning merely because these native APIs are available.

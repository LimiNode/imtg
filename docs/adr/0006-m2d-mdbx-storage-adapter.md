# ADR 0006: M2d-a MDBX storage adapter

**Status:** Draft implementation
**Scope:** Optional persistent implementation of the backend-neutral storage protocol

## Decision

M2d-a adds `imtg_mdbx` behind `IMTG_BUILD_MDBX`. The public header
`imtg/storage/mdbx_storage.hpp` contains no MDBX types. The implementation uses
the pinned `LimiNode/mdbx-containers` submodule at
`4ab32b5eab3cdfce95d1a6c0f05f566019793ec4` and its flat sibling
`erthink/libmdbx` at `5315e9905b525a0a183b76fc787317c51076f944` (libmdbx
v0.13.12, Apache-2.0). The dependency is provided as a CMake target; no
configure-time FetchContent download is allowed.

The pinned libmdbx defaults to C23 when the compiler advertises support.
The Windows CI compiler rejects its C23 `[[maybe_unused]]` declaration;
the source integration selects C11 for MSVC without modifying upstream sources.
It also applies `/Zc:__cplusplus` to the native target's own C++ compilation;
the pinned upstream exports that option to consumers but omits it on this path.

The adapter is opened with `MdbxStorageBackend::open()` and closed explicitly
with `close()`. It uses `mdbx-containers::Connection` and its thread-bound
transaction guard, while the record codec and CAS protocol remain owned by
imtg. A read-write transaction is the single MDBX writer for its owning thread;
the portable fixture therefore declares the single-writer capability rather
than pretending MDBX has the in-memory backend's multi-writer schedule.

## Physical profile

The runtime profile has six named DBIs and two reserved headroom slots by
default (`max_dbs = 8`):

| DBI | Contents |
| --- | --- |
| `meta` | schema profile and migration state |
| `sessions` | typed sessions and FSM records |
| `processed_updates` | update deduplication markers |
| `render_intents` | TX1 render intents, claim side state and generations |
| `outbox` | effects, dispatch-target metadata and applied-target state |
| `event_journal` | technical/audit events |

There is no DBI per bot, user, chat, or record category. Logical keys are opaque
non-empty byte strings. The fixed routing table is:

| Physical DBI | Logical namespace | Namespace byte (hex) |
| --- | --- | --- |
| `meta` | internal schema (not public) | `00` |
| `meta` | `meta` | `01` |
| `sessions` | `sessions` | `01` |
| `processed_updates` | `processed_updates` | `01` |
| `render_intents` | `render_intents` | `01` |
| `render_intents` | `render_intent_claims` | `02` |
| `render_intents` | `render_intent_generations` | `03` |
| `outbox` | `outbox_effects` | `01` |
| `outbox` | `outbox_dispatch_targets` | `02` |
| `outbox` | `applied_targets` | `03` |
| `outbox` | `outbox` (generic draft protocol namespace) | `04` |
| `event_journal` | `event_journal` | `01` |

### Physical key format version 1

Every physical key is `01 || namespace_byte || logical_key_bytes`. These are
two literal bytes followed by the unmodified binary key, with no text conversion,
terminator or escaping. DBIs use ordinary bytewise comparison, so ordering inside
one logical namespace is unchanged, including embedded zero and high bytes.
All reads, CAS writes, watches, erases and scans use this same encoding.
Unknown logical namespaces fail closed; schema declarations do not create routes.
The internal schema key is `01 00 || "imtg/schema/v1"` in `meta`, and public meta
keys always use `01 01`. Public `scan("meta")` therefore never decodes an `IMS1`
record, even if the public key is also `imtg/schema/v1`.

New environments use 16 KiB pages explicitly. The pinned wrapper passes a zero
page size to native geometry as the minimum 256 bytes, not the OS default;
that would reject ordinary outbox effect identities. The adapter queries the
actual native maximum key size and rejects logical keys exceeding that maximum
minus two prefix bytes with `StorageRecordTooLarge` before native access.
No existing environment is resized silently. Stores from the earlier unprefixed
unreleased draft are rejected with `StorageSchemaFailure`; there is no automatic
migration or claim of compatibility with that draft format.

### Scan limits

Scans seek to the requested namespace prefix plus the exclusive logical
continuation, stop at the namespace boundary and return unprefixed logical keys.
They return at most 4096 visible records per call. Tombstones retain their
namespace-scoped revisions and are skipped, but **physical cursor examinations
are not bounded** by the result limit. The sparse regression exercises 256
committed tombstones followed by one visible row. Returning an empty page early
would incorrectly signal exhaustion to current callers. M2d-b/M2e must first
extend the cursor contract with a separate physical-progress continuation and
exhaustion indicator before imposing a physical examination budget. No due-index
or compaction framework is added here.

## Record and transaction mapping

Each value is a versioned binary record: magic, visible/tombstone flag,
big-endian revision, and the application payload. Tombstones remain physical
records so delete/recreate cannot reuse a generation. `create_if_absent`,
expected-revision `write`, `erase`, and `watch` stage changes in the native
transaction and expose read-your-writes. Native MDBX snapshots provide
consistent reads. A staging/storage error rolls back the native transaction;
the caller cannot commit a partial write set.

Schema metadata is stored in the internal `meta` prefix under `imtg/schema/v1`. New environments
write the requested schema once. Existing metadata is decoded and never
silently replaced. `begin_transaction()` admits only `Stable` migration state.
Read-only environments open existing DBIs without `MDBX_CREATE` and reject all
writes.

Successful native commit maps to `Committed`. A deterministic CAS or watch
failure maps to `RejectedConflict`. Native commit exceptions consume the MDBX
transaction before publication can be inferred, so they map conservatively to
`Unknown` and are never retried blindly. Crash injection and post-reopen
Unknown classification remain M2e work.

## Consequences

Core builds and installs without MDBX. The optional adapter and its header are
source-build-only and neither is installed/exported by the imtg package: exporting a static
adapter also requires a reviewed package boundary for the sibling libmdbx
target and platform runtime. That boundary is M2d-b work. The optional
conformance suite runs against MDBX with its single-writer capability declaration,
alongside the adapter-specific persistence/reopen checks. Together they cover
snapshots, read-your-writes, atomic multi-record publication, CAS/ABA fencing,
result-bounded scans, schema gates, read-only rejection and thread ownership.
The actual M2b/M2c repositories also exercise TX1 rollback/deduplication, TX2
rollback/publication, generation and claim fencing, DT1 reservation, reopen and
success acknowledgement with applied-target persistence. These are normal
close/reopen tests, not process-crash proof. Installed consumers verify that
the source-only adapter target and header are absent.

### Lifecycle audit

The pinned `Connection::cleanup()` rejects close with outstanding native handles;
the adapter preserves that error and remains open. Read-only native commit
resets a handle and the wrapper destructor frees it; writable commit consumes
the native handle, including terminal native errors. The adapter drops the
wrapper and conservatively reports `Unknown` for a thrown commit acknowledgement.
The backend must outlive transactions. A live native transaction must be used and
destroyed on its creating worker; a foreign-thread destructor is fail-fast rather
than silently invoking native cleanup on the wrong thread. Close and backend
destruction require external worker quiescence and must not race new transactions.
Tests cover close rejection, owner-thread rollback after foreign-thread access
rejection, read-only admission, snapshot readers and a joined writer, then reopen.
Native I/O commit-error injection and process interruption remain unverified here.

## Rejected alternatives

- A DBI per logical record type, user, or conversation was rejected because it
  exhausts MDBX's named-database budget and complicates migrations.
- A new native MDBX abstraction in `imtg_core` was rejected; core remains
  backend-neutral and continues to use `IStorageBackend`/`ITransaction`.
- Configure-time dependency downloads were rejected; both dependencies are
  pinned direct submodules.
- Treating every native commit error as `Failed` was rejected because an error
  after MDBX publication cannot be distinguished from a pre-publication error.

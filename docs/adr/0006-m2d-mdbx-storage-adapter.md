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
| `render_intents` | TX1 render intents and generations |
| `outbox` | effects, dispatch-target metadata and applied-target state |
| `event_journal` | technical/audit events |

There is no DBI per bot, user, chat, or record category. Keys are opaque
non-empty byte strings and retain MDBX bytewise ordering. Bounded scans use an
exclusive continuation key and a maximum page size of 4096.

## Record and transaction mapping

Each value is a versioned binary record: magic, visible/tombstone flag,
big-endian revision, and the application payload. Tombstones remain physical
records so delete/recreate cannot reuse a generation. `create_if_absent`,
expected-revision `write`, `erase`, and `watch` stage changes in the native
transaction and expose read-your-writes. Native MDBX snapshots provide
consistent reads. A staging/storage error rolls back the native transaction;
the caller cannot commit a partial write set.

Schema metadata is stored in `meta` under `imtg/schema/v1`. New environments
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

Core builds and installs without MDBX. The optional adapter currently builds
from source but is not exported by the imtg install package: exporting a static
adapter also requires a reviewed package boundary for the sibling libmdbx
target and platform runtime. That boundary is M2d-b work. The optional
conformance suite against MDBX with its single-writer capability declaration,
alongside the adapter-specific persistence/reopen checks. Together they cover
snapshots, read-your-writes, atomic multi-record publication, CAS/ABA fencing,
bounded scans, schema gates, read-only rejection and thread ownership.

## Rejected alternatives

- A DBI per logical record type, user, or conversation was rejected because it
  exhausts MDBX's named-database budget and complicates migrations.
- A new native MDBX abstraction in `imtg_core` was rejected; core remains
  backend-neutral and continues to use `IStorageBackend`/`ITransaction`.
- Configure-time dependency downloads were rejected; both dependencies are
  pinned direct submodules.
- Treating every native commit error as `Failed` was rejected because an error
  after MDBX publication cannot be distinguished from a pre-publication error.

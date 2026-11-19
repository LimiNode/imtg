# imtg v0.1.0-alpha.1

This is an experimental alpha of the Telegram-free imtg foundation. It does
not imply stable API or production support.

## Included

- C++17 core UI, actions, deterministic runtime, sessions, and FSM contracts.
- Backend-neutral snapshot/optimistic storage and reusable conformance tests.
- M2c claims, fencing, reconciliation, outbox, delivery acknowledgement, and
  conservative Unknown-state contracts on the reference backend.
- Optional M2d-a MDBX source-build adapter using the pinned direct submodules.
- Provider-neutral commerce/payment contracts and optional `imtg_commerce` target.
- Hybrid public-header architecture: domain umbrella headers plus standalone
  public components and independently enabled optional modules.
- CMake install/export for core and optional commerce targets, public-header
  checks, installed consumers, and the documented flat dependency boundary.

## Build and package requirements

- C++17 compiler and CMake 3.16 or newer.
- Core and commerce builds require no third-party submodules.
- MDBX builds require a Git clone followed by non-recursive initialization of
  `external/libmdbx` and `external/mdbx-containers`.
- The pinned revisions and commands are maintained in
  [`external/README.md`](external/README.md).
- The installed package contains core and optional commerce headers and CMake
  targets. `imtg::mdbx` remains source-build-only until M2d-b defines its
  installed/export contract.

GitHub-generated source archives do not contain Git submodule payloads. A
consumer that needs MDBX must clone the repository and initialize only the two
direct submodules; recursive initialization is not part of the supported path.

## Public compatibility

Supported installed entry points are the explicit 29 core headers and seven
optional commerce headers listed in `cmake/ImtgPublicHeaders.cmake`. The hybrid
umbrella policy and compatibility paths are defined by
[ADR 0011](docs/adr/0011-hybrid-public-header-profile.md). Public functions and
methods use `snake_case`; public types use `CamelCase`.

## Versioning

The release tag is `v0.1.0-alpha.1`. The CMake package version remains numeric
`0.1.0`; the `alpha.1` qualifier belongs to the prerelease tag and does not
imply stable API or production support.

## Known limitations

- No Telegram Bot API gateway or production transport integration.
- No live payment provider adapters, Mini App/ImGuiX UI, or operator GUI.
- MDBX is not exported by the installed package.
- Crash/restart durability, migration recovery, and bounded physical tombstone
  recovery are not established by this alpha.
- Full cross-backend recovery conformance and TSan coverage remain open.
- The reference backend is not evidence of physical durability between process
  runs.

## License and attribution

The root package is MIT licensed and names `LimiNode` as the copyright holder.
The package installs that exact `LICENSE` file under `share/licenses/imtg/` and
does not alter third-party notices.

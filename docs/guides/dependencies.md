# Dependencies

Core must remain backend- and transport-neutral. Integrations are adapters behind imtg contracts: MDBX through `mdbx-containers`, HTTP through `kurlyk`, and event delivery through `event-hub-cpp` where its semantics fit. Dependency additions require an ownership/license/API review, a documented failure model, and a conformance or fake-based test plan.

The reviewed capability boundaries and exact source revisions for M2c/M2d/M3
planning are recorded in the [dependency capability audit](../DEPENDENCY_CAPABILITY_AUDIT.md).

Git-based dependencies are pinned direct submodules under `external/<name>` by default. Record the
reviewed source URL in `.gitmodules`, pin the gitlink SHA, and initialize non-recursively. Do not use
`FetchContent`, configure-time Git downloads, or recursive submodule initialization as an
unreviewed fallback. Prefer one shared root-level dependency target when multiple sibling libraries
can reuse it. Advancing a gitlink is a dedicated dependency update; do not patch upstream sources
silently.

System and platform libraries for which `find_package()` is the established integration are valid
exceptions. Unavoidable nested dependencies require a documented reason and explicit approval.
Do not add speculative submodules. M2d-a pins sibling `external/mdbx-containers`
and `external/libmdbx` submodules and provides them to CMake as one optional
dependency boundary. `IMTG_BUILD_MDBX=OFF` remains the default and performs no
MDBX lookup or download. With `IMTG_BUILD_MDBX=ON`, an existing
`mdbx_containers::mdbx_containers` target wins before any source-path checks.
A parent may explicitly obtain it through `find_package()`; imtg does not
perform implicit package discovery. Otherwise `IMTG_MDBX_CONTAINERS_SOURCE_DIR`
selects an explicit source provider or defaults to the pinned flat sibling.
That source provider reuses an existing compatible libmdbx target before
`IMTG_LIBMDBX_SOURCE_DIR`, which defaults to `external/libmdbx`.
Without parent targets or explicit sources, both flat submodules must be
initialized. The native target is resolved before adding containers, so no
nested-source or FetchContent fallback can run; missing providers fail closed.
Optional dependency builds default to disabled and `MDBXC_DEPS_MODE` defaults
to `SYSTEM` only when unset. Parent normal/cache options and modes are preserved.
The pinned libmdbx source fallback requires CMake 3.19 or newer for its version
metadata parser; this does not raise the core CMake 3.18 minimum or affect
existing-target reuse.
The optional adapter is tested with a local persistence/reopen consumer; its
installed-package export remains a later M2d-b boundary.

The optional `IMTG_BUILD_TELEGRAM_TRANSPORT` slice uses nlohmann/json privately
at `b1bb9fce0c9ae9db5cecf8e3bd78519ac70683ee`, the JSON gitlink selected by
`kurlyk@b62cd53f672c47b74bb14d762ea9dc59a9df19ce`. It is a flat
`external/json` submodule, initialized explicitly and non-recursively. Its MIT
notice is installed with the optional adapter, while its headers and package
lookup do not enter imtg's installed public contract. Core-only and the base
Telegram router require neither this parser nor kurlyk.

When a parent supplies `nlohmann_json::nlohmann_json`, the transport consumes
that existing target before inspecting its own JSON provider path and preserves
the parent's configuration. Otherwise `IMTG_JSON_SOURCE_DIR` selects the pinned
private source provider. JSON usage is build-only: the installed imtg package
does not require a JSON package lookup. A parent-owned provider also owns its
license distribution; imtg installs the MIT notice only for its own source
provider.

`IMTG_BUILD_TELEGRAM_KURLYK=ON` optionally supplies the concrete HTTP adapter.
It requires the generic transport option and a kurlyk target with the bounded
response contract from upstream #50. The pinned source is the flat-dependency
corrective #51 at `72373420175a8414f84307f3b1f59709ce234e5a`.
Existing parent kurlyk targets win before source-path checks. Source fallback
is offline and reuses parent/system CURL, OpenSSL, Asio and
Simple-WebSocket-Server targets before direct sibling providers. Its installed
static target resolves the kurlyk package only when the `telegram_kurlyk`
component is requested. See [the direct dependency inventory](../../external/README.md)
for exact pins, platform packages, installation ownership and initialization
commands. No core or transport-neutral header exposes a kurlyk type.

# Flat dependency boundary

Only direct, pinned Git submodules are managed by imtg here:

| Path | Upstream | Pin |
| --- | --- | --- |
| `libmdbx` | erthink/libmdbx | `5315e9905b525a0a183b76fc787317c51076f944` (v0.13.12) |
| `mdbx-containers` | LimiNode/mdbx-containers | `4ab32b5eab3cdfce95d1a6c0f05f566019793ec4` |
| `json` | nlohmann/json | `b1bb9fce0c9ae9db5cecf8e3bd78519ac70683ee` (same pin as reviewed kurlyk baseline) |
| `log-it-cpp` | LimiNode/log-it-cpp | `659c38eef86cb3cbab448fb76852641830a89430` (v1.0.2) |
| `time-shield-cpp` | LimiNode/time-shield-cpp | `d5f1203f24341e4146af35d5bb36e762056d181f` (v2.0.0) |
| `kurlyk` | LimiNode/kurlyk | `72373420175a8414f84307f3b1f59709ce234e5a` (corrective #51, based on response-limit #50) |
| `asio` | chriskohlhoff/asio | `03cf5f86a780dd102f1cdd3a59d1244d12143e46` |
| `Simple-WebSocket-Server` | LimiNode/Simple-WebSocket-Server | `de7cb7c39edd6b4178adcc3f5f17d219720b3cdb` |
| `Simple-Web-Server` | eidheim/Simple-Web-Server (GitLab) | `546895a93a29062bb178367b46c7afb72da9881e` (loopback tests only) |
| `curl-win64-mingw` | LimiNode/curl-8.22.0_1-win64-mingw | `5a639c3fbe1c322a9048f4d54f66ba48f48a2d7d` |
| `openssl-win64` | LimiNode/openssl-win64-v3.5.8 | `d7528b3ade4da0ab445c8bdb34b65907595d27f5` |

The gitlinks are authoritative. Update this audit when changing their pins.
This local integration additionally includes #38: LogIt++ and TimeShield are
required implementation dependencies of core. Existing parent targets are
reused before own source paths, and parent `LOGIT_WITH_*` choices are preserved.
This local validation is not a merge or release claim for `main`.

From the repository root, initialize only the direct siblings:

```sh
git submodule update --init -- external/libmdbx external/mdbx-containers external/json
cmake -DIMTG_SOURCE_DIR=. -P cmake/CheckFlatDependencies.cmake
cmake -S . -B build/mdbx -DIMTG_BUILD_MDBX=ON -DBUILD_TESTING=ON
cmake --build build/mdbx --config Debug
ctest --test-dir build/mdbx -C Debug --output-on-failure
```

Do not use recursive initialization. The upstream mdbx-containers checkout
contains its own libmdbx and documentation-theme gitlinks. They remain
uninitialized; imtg does not modify upstream metadata or maintain duplicate
copies. `external/CMakeLists.txt` first reuses an existing
`mdbx_containers::mdbx_containers` target. A parent may explicitly load a local
or system package before adding imtg; imtg performs no implicit package lookup.
Only when the target is absent does it inspect `IMTG_MDBX_CONTAINERS_SOURCE_DIR`,
which defaults to the pinned flat sibling. A containers target owns its native
dependency and bypasses both unused source paths. When adding containers from
source, existing compatible libmdbx targets win before
`IMTG_LIBMDBX_SOURCE_DIR` (the pinned `external/libmdbx` by default).
The native target is resolved before the containers source is added, so its
nested-source and FetchContent fallback cannot run. Missing providers fail
configuration; configuration never downloads or updates dependencies.
Explicit source overrides are for deliberate local providers, not the pinned
CI or release path; the Git audit still checks the direct checkout.
The pinned libmdbx source fallback requires CMake 3.19 or newer to read its
version metadata. Core retains CMake 3.18; reusing compatible existing targets
does not enter that native-source requirement.

Third-party tools, examples and tests default to disabled only when unset.
Parent normal/cache options and dependency modes are preserved. The default
containers mode is `SYSTEM`; an explicit parent `AUTO`/`BUNDLED` mode is also
safe because an existing native target resolves first. The MSVC C11 default
and `/Zc:__cplusplus` adjustment apply only to imtg-owned libmdbx source targets;
existing parent/imported targets are not modified. Adapter code
and its `imtg::mdbx` target stay owned by imtg, not by this dependency provider.
The adapter remains source-build-only until M2d-b defines installed dependency
resolution and exports.

`imtg_mdbx_Embedding` builds and runs a real adapter consumer with source,
imported and explicitly package-provided targets, deliberately unavailable unused paths,
native-only target reuse, and normal/cache option preservation. It also checks
that implicit package lookup cannot override explicit/pinned sources, and that
absent providers fail before a download-capable fallback is entered. These
source-build fixtures do not establish the M2d-b installed adapter contract.

`json` is a private MIT-licensed parser for `imtg::telegram_transport` only.
Without a parent-owned `nlohmann_json::nlohmann_json` target, that option requires
explicit non-recursive initialization of `external/json`;
normal configuration performs no download. Its headers are compiled privately
without invoking upstream CMake installation, tests or downloads. The optional
installed package includes its MIT notice but neither JSON headers nor a JSON
package dependency. The same pin can later be reused by a kurlyk adapter.

`IMTG_BUILD_TELEGRAM_KURLYK=ON` adds the optional concrete
`imtg::telegram_kurlyk` adapter. Its source provider requires CMake 3.21 or
newer. Existing `kurlyk::kurlyk` or `kurlyk` targets are reused before inspecting
provider paths; otherwise imtg adds the direct sibling with
`KURLYK_ALLOW_DOWNLOADS=OFF`. CURL and OpenSSL targets/packages are reused first.
On MinGW the pinned binary siblings are a fallback; other platforms require
system CURL/OpenSSL packages. Standalone Asio and Simple-WebSocket-Server use
the direct sibling providers when parent targets are absent. No nested kurlyk
gitlink is initialized or used. The Simple-Web-Server sibling is needed only
for deterministic loopback tests, not for the installed adapter.

Initialize this profile without recursion:

```sh
git submodule update --init -- external/json external/kurlyk external/asio external/Simple-WebSocket-Server external/Simple-Web-Server external/curl-win64-mingw external/openssl-win64
cmake -S . -B build/kurlyk -DIMTG_BUILD_TELEGRAM=ON -DIMTG_BUILD_TELEGRAM_TRANSPORT=ON -DIMTG_BUILD_TELEGRAM_KURLYK=ON -DBUILD_TESTING=ON
cmake --build build/kurlyk --config Debug
ctest --test-dir build/kurlyk -C Debug --output-on-failure
```

An installed static consumer requests `find_package(imtg CONFIG REQUIRED
COMPONENTS telegram_kurlyk)` and links only `imtg::telegram_kurlyk`. The package
resolves kurlyk and its system dependencies as a private link requirement;
vendor types and includes are absent from the standalone adapter header. When
imtg supplies source dependencies, it installs the kurlyk package, the required
Asio/Simple-WebSocket-Server headers and their notices. It does not redistribute
the CURL/OpenSSL binaries as part of the installed package; consumers provide
compatible system packages and runtime libraries. The application owns kurlyk's
global initialization and shutdown.

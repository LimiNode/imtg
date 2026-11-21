# M3 composition evidence — 2026-11-21

**Scope:** source provenance and previously recorded local checks. This is not
an independent review, a CI success claim, a merge or operational acceptance.
The integration branch preserves the original composition as an ancestor; CI
and planning follow-ups have their own HEAD shown in the Draft PR.

Original composition: `467f6f467dcc0b36b5006e6e390b749b9aaeaab9`.
Original tree: `3c184c82bca9526754c54e2425df092efa22c52e`.
Base main: `0417545dbdb306ad94e8937aa887e51ce175cd3a`.

## Source heads and resolutions

| Source | Exact included HEAD |
| --- | --- |
| #36 documentation | `9d03350ec92a0361ea43c39e81e287da54533f8d` |
| #37 typed Telegram gateway | `02a0c8ab135c40d457187e3d8e1eecd2601dc822` |
| #38 internal logging | `12f58253e65e9f7b83250638c6f945a8246656a3` |
| #39 bounded generic transport | `6ef533713380e7a79a6142572b835cbfc9fcce1a` |
| #40 payment research | `9b60df9dccac890a76873fe52b22e9bbb58e5683` |
| #41 outgoing kurlyk adapter | `e54be2c13d18cd814d79db0f9c1bc9084408f74f` |

The original local merges retain mandatory implementation-private LogIt++ in
core/router, the optional outgoing adapter target, parent-target precedence,
offline source fallback, package/export boundaries, and the union of dependency
inventory/audit entries. Documentation conflicts retain outgoing support and
explicitly label integration as distinct from main. The last two corrective
merges (a3fd4a9 and 467f6f4) were conflict-free. Publishing this history permits
inspection of the actual resolutions, not a reconstructed equivalent tree.

## Direct gitlinks (unchanged by the publication follow-up)

| Sibling under external/ | Pin |
| --- | --- |
| Simple-Web-Server | `546895a93a29062bb178367b46c7afb72da9881e` |
| Simple-WebSocket-Server | `de7cb7c39edd6b4178adcc3f5f17d219720b3cdb` |
| asio | `03cf5f86a780dd102f1cdd3a59d1244d12143e46` |
| curl-win64-mingw | `5a639c3fbe1c322a9048f4d54f66ba48f48a2d7d` |
| json | `b1bb9fce0c9ae9db5cecf8e3bd78519ac70683ee` |
| kurlyk | `72373420175a8414f84307f3b1f59709ce234e5a` |
| libmdbx | `5315e9905b525a0a183b76fc787317c51076f944` |
| log-it-cpp | `659c38eef86cb3cbab448fb76852641830a89430` |
| mdbx-containers | `4ab32b5eab3cdfce95d1a6c0f05f566019793ec4` |
| openssl-win64 | `d7528b3ade4da0ab445c8bdb34b65907595d27f5` |
| time-shield-cpp | `d5f1203f24341e4146af35d5bb36e762056d181f` |

Simple-Web-Server is only a loopback fixture here. kurlyk #52 incoming server
and its corrected backend pin are not part of this tree. These tests do not
validate an imtg incoming server path.

## Previously executed local checks of the original composition

Windows MinGW GCC 16.1, Ninja, CMake 4.3.3, C++17. All modules enabled together:
commerce, MDBX, Telegram, generic transport, kurlyk, mandatory internal logging.

- Full Debug CTest: 20/20 PASS, 160.44 seconds.
- Full Release CTest: 20/20 PASS, 170.52 seconds.
- Included LogIt++ installed consumer and four parent embedding modes;
  bounded real HTTP loopback; installed kurlyk consumer, all 41 installed public
  first-includes and kurlyk/JSON parent reuse; MDBX conformance/reopen.
- Flat audit before configure and in both suites: all 11 direct pins match;
  nested submodules remain uninitialized. Configuration performed no downloads.
- Strict Doxygen target, installed-license check and CRLF-aware diff check PASS.

These builds reused existing caches and were rebuilt after both corrective
merges; they are not fresh-build or cross-platform evidence. The Draft PR records
new runs separately rather than attributing this evidence to a newer HEAD.

## Reproducible combined CI

Initialize only direct submodules, then audit before configuring:

```sh
git submodule update --init
cmake -DIMTG_SOURCE_DIR=. -P cmake/CheckFlatDependencies.cmake
cmake -S . -B build/combined -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DBUILD_TESTING=ON -DIMTG_BUILD_COMMERCE=ON -DIMTG_BUILD_MDBX=ON \
  -DIMTG_BUILD_TELEGRAM=ON -DIMTG_BUILD_TELEGRAM_TRANSPORT=ON \
  -DIMTG_BUILD_TELEGRAM_KURLYK=ON -DKURLYK_ALLOW_DOWNLOADS=OFF \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build/combined --parallel 3
ctest --test-dir build/combined --no-tests=error --output-on-failure
```

Use platform-appropriate CURL/OpenSSL providers. Repeat with Release in a
separate build directory. The combined job in ci.yml performs this on Linux
Debug/Release, including installed headers/consumers and parent reuse, plus
standalone commerce/transport installed consumers. Existing normal and adapter
workflows remain. MDBX stays source-only; package checks intentionally do not
assert an exported imtg::mdbx target.

CI success must be established from the new exact HEAD's workflow results,
not inferred from earlier standalone PR jobs. No local sanitizer/TSan, live
HTTPS/Telegram, public webhook or process-crash durability result is claimed.

## Published-tree CI evidence

Exact publication HEAD `9c2db22532d42c5d1da441bdbc53ae8065efd5cd` completed:

- [Normal CI 44918585884](https://github.com/LimiNode/imtg/actions/runs/44918585884):
  **17/17 jobs success**, including combined Linux Debug/Release, ordinary and
  MDBX platform matrices, core-only, documentation and ASan/UBSan.
- [Adapter CI 44918585871](https://github.com/LimiNode/imtg/actions/runs/44918585871):
  **7/7 jobs success**, including platform builds and ASan/UBSan.

A subsequent follow-up explicitly pins C++17 in the combined command and adds
this evidence/plan progress. Its exact HEAD and CI are tracked separately in
the PR; the successful runs above are evidence for 9c2db22 only. Local sanitizer
unavailability is separate from these successful hosted sanitizer jobs.

The follow-up's unchanged production source was also rebuilt locally with an
explicit required C++17 standard: Debug **20/20**, Release **20/20** CTest PASS,
including installed headers/consumers, parent reuse and flat audit. Debug took
93.58 seconds; Release took 284.44 seconds. These are fresh build directories
for this publication, subsequently reconfigured/rebuilt with explicit C++17,
not a second clean build claim. MinGW needs the three pinned CURL/OpenSSL
runtime DLL paths in IMTG_KURLYK_RUNTIME_DLLS when explicit library paths are
supplied; the first local launch without those runtime paths failed, and the
reported passing runs followed corrected local configuration. No product
source workaround or disabled test was introduced.

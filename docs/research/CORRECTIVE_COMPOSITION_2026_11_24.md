# Corrective composition — 2026-11-24

**Status:** assembled for independent review; not merged or operationally accepted.
**Owner:** architecture remediation batch B1–B6, D1, E1.

This is a second composition, not a re-labeling of #42. Its base is
`a5cdf3259792eb600f6bfb50cf78853bf568dc62`; original M3 composition `467f6f46`
remains reachable. Exact corrective sources are listed in the
[remediation plan](../ARCHITECTURE_REMEDIATION_PLAN.md#current-corrective-batch--2026-11-24).
The first complete source composition is commit
`ef486ca991af5a6d54d14f1e5f926152ffb8ecfd`, tree
`656b53bfc9152ca53b4ac80dd155526bd91a0f53`. Subsequent documentation/CI results
belong to their own exact HEAD, recorded in the integration PR.

## Resolutions and dependency identity

Local merge commits retain individual source history. Conflicts occurred only
in `docs/IMPLEMENTATION_STATUS.md` (independent additive status paragraphs) and
`docs/contracts/UI_AND_ACTIONS.md` (callback lifetime versus checked expiry).
Both contract additions were retained. Source files and test/CMake changes
merged mechanically. This does not authorize a GitHub merge into main.

No gitlink changed relative to #42. All eleven exact pins remain the inventory
in [the original evidence](M3_COMPOSITION_EVIDENCE_2026_11_21.md#direct-gitlinks-unchanged-by-the-publication-follow-up).
The flat audit must run before each combined configure; nested dependencies
remain uninitialized. The outgoing adapter is included, not the incoming server.
kurlyk #53 and Telegram webhook/durable inbox are separate boundaries.

## Finding state and observed local evidence

`FIXED` below means implemented with the named local regression, not independent
acceptance or a claim that hosted CI of this new composition has passed.

| Finding | State | Distinguishing evidence |
| --- | --- | --- |
| B1 registration/lifetime | FIXED | New regression times out on pre-fix header; GNU Debug/Release callback/action/runtime focused suites 13/13 each; original in-flight cases retained |
| B2 ST-1/ST-2 | FIXED | Current GNU Debug/Release storage suites 2/2 each; old-source relink fails terminal rollback and net-no-op watch; local MSVC Debug MDBX 1/1 |
| B2 Windows timeout diagnosis | FIXED | Exact hosted MSVC 19.52 diagnostic exits 77; matching MAP confirms noexcept string proxy allocation, not native MDBX deadlock; corrected #43 normal 45299238171 succeeded 17/17, adapter 45299238321 succeeded 7/7 |
| B3 session persistence | FIXED | Old session implementation fails invalid metadata regression; corrected C++17 Debug 4/4 storage/session/claim tests, including MDBX; Release focused checks recorded in #46 |
| B4 UI exception safety | FIXED | Old-source text probe rejects the terminal-state expectation; corrected GNU Debug/Release 4/4 each; MSVC Debug allocation cases 3/3 |
| B5 time/order | FIXED | GNU C++17 Debug/Release identity/time/action/storage 4/4 each, signed extrema and wire fixtures, allocation-prohibited ordering |
| B6 provider identity | FIXED | GNU C++17 Debug/Release payment suite 1/1 each, 128-byte identity and lookup with allocation forbidden |
| D1 provider resolution | FIXED | Existing target/provider/offline cases in seven real consumers and two expected configure failures; #45 updated onto verified #43, with its own exact-head checks |
| E1 audit | FIXED | Real-tree audit plus 15 fixtures; original checker accepts spaced upward include, new checker rejects it; focused CTest 2/2 |
| Independent batch acceptance | STILL_OPEN | Individual and combined results are evidence for review, not independent acceptance; current exact-head checks are recorded in #51 |
| Need for new upstream MDBX binding/CAS API | SUPERSEDED | Existing pinned table/transaction API is sufficient; D2 remains deferred |
| TimeShield full migration | RESEARCH_ONLY | Checked B5 contract is implemented locally; full dependency integration remains F |

MSVC iterator-debug fault injection excludes sizes at or below its proxy size.
This includes unrelated tiny allocations and is not exhaustive global-new failure
coverage. Ordinary iterator diagnostics remain enabled; GNU profiles retain full
sweeps. No specific failure ordinal is skipped in ordinary tests. The unfiltered
STL diagnostic is an expected terminate experiment, not a rollback test.

## Combined verification gates

Run the same all-module offline configuration as the original evidence, using
fresh `build/combined-debug` and `build/combined-release` trees, explicit C++17,
mandatory logging, all current optional modules, and `FETCHCONTENT_FULLY_DISCONNECTED`.
The normal and adapter workflows must test this branch's own tree, including
Debug/Release, public headers/installed consumers, parent reuse, platform jobs
and hosted ASan/UBSan. Local combined results and exact hosted run links are
recorded separately in [#51](https://github.com/LimiNode/imtg/pull/51); only those
exact-head results close the automated gate, not results of sibling PRs.

The completed local all-module C++17 checks on composition `f3fb6e4` passed
Debug 37/37 (145.16 seconds) and Release 37/37 (179.54 seconds), including installed
headers/consumer, parent reuse and MDBX embedding. Updating #45's ancestry in
`2de0d4b52547a0969a292649efb13cd7ec73468f` leaves that tree unchanged:
`8c85d266d9701632433e0e30780849cd043c3302`. Final exact-head results, after this
documentation update, are reported separately in the PR rather than inferred.
The first full Release attempt failed the injection-coverage assertion; it is
superseded, not counted as a pass. Doxygen 1.17 strict validation and local
Markdown target checks (11 changed documents, no missing targets) also passed.

Reproduction commands, from the composition worktree (GNU 16.1 MinGW, Ninja):

```powershell
cmake -DIMTG_SOURCE_DIR="$PWD" -P cmake/CheckFlatDependencies.cmake
cmake -S . -B build/combined-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DBUILD_TESTING=ON -DIMTG_BUILD_COMMERCE=ON -DIMTG_BUILD_MDBX=ON -DIMTG_BUILD_TELEGRAM=ON -DIMTG_BUILD_TELEGRAM_TRANSPORT=ON -DIMTG_BUILD_TELEGRAM_KURLYK=ON -DKURLYK_ALLOW_DOWNLOADS=OFF -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build/combined-debug --parallel 4
ctest --test-dir build/combined-debug --no-tests=error --output-on-failure
```

Repeat the flat audit, configure and checks with `build/combined-release` and
`Release`. Local CTest logs are under each profile's `Testing/Temporary/`;
hosted logs/artifacts are retained by the linked exact-head workflows.

Persistent IMR1/IMS1 bytes, clock injection, storage rounding-to-future,
fencing/Unknown semantics and transaction ownership remain unchanged. There is
no process-crash durability, installed MDBX export or live transport claim.
The root checkout's unfinished rebase and unrelated staged changes are untouched.

## Additional Windows test boundaries

Explicit C++17 MinGW Release initially failed the MDBX sweep because the dynamic
STL bypassed the executable's replacement allocator on `write_pending`. Identical
objects relinked with static libstdc++/libgcc passed; the isolated #43 reproduced
both outcomes. The correction is restricted to that injection executable and
keeps the active requirement that every mutation path encounter an allocation.
The corrected C++17 Debug/Release isolated storage suites pass 2/2 each. Current
Debug objects relinked to the old implementation still fail ST-1 terminal rollback
and ST-2 net-no-op watches. See the [harness diagnosis](MSVC_ALLOCATION_HARNESS_2026_11_24.md).

The MinGW installed core and commerce smoke executables also triggered Windows
installer detection (`ERROR_ELEVATION_REQUIRED`, code 740) because their output
names contained `install` without a manifest. Their fixture output names now
omit that substring on MinGW only. CMake targets and test names are unchanged;
both run through ordinary CTest without elevation. All six extra installed
core/commerce/generic-transport consumers (Debug and Release) pass. This does not
change production executable manifests or introduce an elevation workaround.

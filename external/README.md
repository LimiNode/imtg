# Flat dependency boundary

Only direct, pinned Git submodules are managed by imtg here:

| Path | Upstream | Pin |
| --- | --- | --- |
| `libmdbx` | erthink/libmdbx | `5315e9905b525a0a183b76fc787317c51076f944` (v0.13.12) |
| `mdbx-containers` | LimiNode/mdbx-containers | `4ab32b5eab3cdfce95d1a6c0f05f566019793ec4` |
| `log-it-cpp` | LimiNode/log-it-cpp | `659c38eef86cb3cbab448fb76852641830a89430` (v1.0.2) |
| `time-shield-cpp` | LimiNode/time-shield-cpp | `d5f1203f24341e4146af35d5bb36e762056d181f` (v2.0.0) |

The gitlinks are authoritative. Update this audit when changing their pins.
LogIt++ and TimeShield are required implementation dependencies of core.
MDBX and mdbx-containers remain optional. An existing parent-provided
`log-it-cpp::log-it-cpp` is reused before checking any local provider path;
an existing `time_shield::time_shield` is also reused when adding LogIt++.

From the repository root, initialize only the direct siblings:

```sh
git submodule update --init -- external/libmdbx external/mdbx-containers external/log-it-cpp external/time-shield-cpp
cmake -DIMTG_SOURCE_DIR=. -P cmake/CheckFlatDependencies.cmake
cmake -S . -B build/mdbx -DIMTG_BUILD_MDBX=ON -DBUILD_TESTING=ON
cmake --build build/mdbx --config Debug
ctest --test-dir build/mdbx -C Debug --output-on-failure
```

Do not use recursive initialization. The upstream mdbx-containers checkout
contains its own libmdbx and documentation-theme gitlinks. They remain
uninitialized; imtg does not modify upstream metadata or maintain duplicate
copies. `external/CMakeLists.txt` adds the sibling libmdbx target first, then
mdbx-containers consumes that existing target. `MDBXC_DEPS_MODE=SYSTEM` prevents
its nested-submodule/FetchContent fallback; missing siblings fail configuration.
Configuration never downloads or updates dependencies. The existing
`IMTG_MDBX_CONTAINERS_SOURCE_DIR` override is intended for deliberate local
experiments, not the pinned CI or release path; the Git audit checks the checkout.

Third-party tools, examples and tests are disabled. The MSVC C11 selection is
owned here for the pinned libmdbx compiler compatibility issue. Adapter code
and its `imtg::mdbx` target stay owned by imtg, not by this dependency provider.
The adapter remains source-build-only until M2d-b defines installed dependency
resolution and exports. LogIt++ optional backends remain at upstream defaults
unless selected by the application. imtg never overwrites `LOGIT_WITH_*` cache
settings and never initializes nested gitlinks. Installed consumers use
`find_package(imtg CONFIG REQUIRED)`; the package resolves LogIt++ unless the
parent already provided its target. An embedding application owns a compatible
installed LogIt++/TimeShield package when it provides those targets itself.
No GUI or payment SDK is acquired here.

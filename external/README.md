# Flat dependency boundary

Only direct, pinned Git submodules are managed by imtg here:

| Path | Upstream | Pin |
| --- | --- | --- |
| `libmdbx` | erthink/libmdbx | `5315e9905b525a0a183b76fc787317c51076f944` (v0.13.12) |
| `mdbx-containers` | LimiNode/mdbx-containers | `4ab32b5eab3cdfce95d1a6c0f05f566019793ec4` |

The gitlinks are authoritative. Update this audit when changing their pins.
Neither dependency is required by `imtg::core` or a core-only build.

From the repository root, initialize only the direct siblings:

```sh
git submodule update --init -- external/libmdbx external/mdbx-containers
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
resolution and exports. No new transport, GUI or payment SDK is acquired here.

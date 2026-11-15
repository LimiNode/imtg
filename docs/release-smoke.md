# Release smoke procedure

Run this procedure from a clean checkout of the exact release candidate. Keep
all generated files under `build/` and record the compiler, CMake version,
commit SHA, and command output.

## Core and commerce

```sh
cmake -S . -B build/release-core -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/release-core --config Release
ctest --test-dir build/release-core -C Release --output-on-failure

cmake -S . -B build/release-commerce -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DIMTG_BUILD_COMMERCE=ON
cmake --build build/release-commerce --config Release
ctest --test-dir build/release-commerce -C Release --output-on-failure
```

Install each package to a fresh prefix and run the installed public-header and
consumer checks:

```sh
cmake --install build/release-core --config Release --prefix build/install-core
cmake --install build/release-commerce --config Release --prefix build/install-commerce
cmake -S tests/public_headers -B build/release-core-headers \
  -DCMAKE_PREFIX_PATH=$PWD/build/install-core
cmake --build build/release-core-headers --config Release
cmake -S tests/install_consumer -B build/release-core-consumer \
  -DCMAKE_PREFIX_PATH=$PWD/build/install-core
cmake --build build/release-core-consumer --config Release
ctest --test-dir build/release-core-consumer -C Release --output-on-failure
cmake -S tests/install_commerce_consumer -B build/release-commerce-consumer \
  -DCMAKE_PREFIX_PATH=$PWD/build/install-commerce
cmake --build build/release-commerce-consumer --config Release
ctest --test-dir build/release-commerce-consumer -C Release --output-on-failure
```

Confirm that the fresh prefixes contain the explicit public-header manifest,
the CMake package files, and `share/licenses/imtg/LICENSE`, and that a core-only
consumer does not require MDBX.

## MDBX source-build profile

```sh
git submodule update --init -- external/libmdbx external/mdbx-containers
cmake -S . -B build/release-mdbx -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DIMTG_BUILD_MDBX=ON
cmake --build build/release-mdbx --config Release
ctest --test-dir build/release-mdbx -C Release --output-on-failure
```

Run the flat-dependency audit and verify the exact gitlink revisions before
accepting the result. Do not expect `imtg::mdbx` in the installed/exported
package; that boundary is deferred to M2d-b.

## Additional acceptance

Repeat the core and commerce checks in Debug, run the repository's ASan/UBSan
job, validate Doxygen and Markdown relative links, and inspect the CI matrix on
Ubuntu, Windows, and macOS. These checks establish the exercised build and
consumer paths only; they do not establish process-crash durability or TSan
coverage.

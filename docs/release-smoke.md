# Release smoke procedure

Run this procedure from a clean checkout of the exact release candidate. Keep
all generated files under `build/` and record the compiler, CMake version,
commit SHA, and command output.

All source-build profiles require CMake 3.18 or newer and the direct LogIt++
and TimeShield providers, unless the embedding application supplies their
targets first:

```sh
git submodule update --init -- external/log-it-cpp external/time-shield-cpp
```

## Core, commerce and Telegram boundary

```sh
cmake -S . -B build/release-core -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/release-core --config Release
ctest --test-dir build/release-core -C Release --output-on-failure

cmake -S . -B build/release-commerce -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DIMTG_BUILD_COMMERCE=ON
cmake --build build/release-commerce --config Release
ctest --test-dir build/release-commerce -C Release --output-on-failure

cmake -S . -B build/release-telegram -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DIMTG_BUILD_TELEGRAM=ON
cmake --build build/release-telegram --config Release
ctest --test-dir build/release-telegram -C Release --output-on-failure
```

Install each package to a fresh prefix and run the installed public-header and
consumer checks:

```sh
cmake --install build/release-core --config Release --prefix build/install-core
cmake --install build/release-commerce --config Release --prefix build/install-commerce
cmake --install build/release-telegram --config Release --prefix build/install-telegram
cmake -S tests/public_headers -B build/release-core-headers \
  -DCMAKE_PREFIX_PATH=$PWD/build/install-core
cmake --build build/release-core-headers --config Release
cmake -S tests/public_headers -B build/release-commerce-headers \
  -DCMAKE_PREFIX_PATH=$PWD/build/install-commerce \
  -DIMTG_REQUIRE_COMMERCE=ON
cmake --build build/release-commerce-headers --config Release
cmake -S tests/public_headers -B build/release-telegram-headers \
  -DCMAKE_PREFIX_PATH=$PWD/build/install-telegram \
  -DIMTG_REQUIRE_TELEGRAM=ON
cmake --build build/release-telegram-headers --config Release
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
consumer does not require MDBX. The core header build covers 29 installed
headers; the commerce build covers the complete 36-header inventory (29 core
plus seven optional commerce headers). The Telegram build adds the two
optional gateway headers and `imtg::telegram` target. All profiles install
LogIt++ and TimeShield packages for core's implementation link dependency.
CTest also builds and runs an independent installed logger consumer and parent
target-reuse fixtures with context enabled. No vendor types or logger headers
enter core's public API, and no observer logging adapter is installed.

The transport profile also installs `imtg::telegram_transport` and two standalone
transport headers. Its private JSON source pin is needed only for the source
build, not for the installed consumer:

```sh
git submodule update --init -- external/json
cmake -S . -B build/release-transport -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DIMTG_BUILD_TELEGRAM=ON -DIMTG_BUILD_TELEGRAM_TRANSPORT=ON
cmake --build build/release-transport --config Release
ctest --test-dir build/release-transport -C Release --output-on-failure
cmake --install build/release-transport --config Release --prefix build/install-transport
cmake -S tests/public_headers -B build/release-transport-headers \
  -DCMAKE_PREFIX_PATH=$PWD/build/install-transport \
  -DIMTG_REQUIRE_TELEGRAM=ON -DIMTG_REQUIRE_TELEGRAM_TRANSPORT=ON
cmake --build build/release-transport-headers --config Release
cmake -S tests/install_telegram_transport_consumer -B build/release-transport-consumer \
  -DCMAKE_PREFIX_PATH=$PWD/build/install-transport
cmake --build build/release-transport-consumer --config Release
ctest --test-dir build/release-transport-consumer -C Release --output-on-failure
```

## MDBX source-build profile

```sh
git submodule update --init -- external/libmdbx external/mdbx-containers
cmake -S . -B build/release-mdbx -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DIMTG_BUILD_MDBX=ON
cmake --build build/release-mdbx --config Release
ctest --test-dir build/release-mdbx -C Release --output-on-failure
```

For the repository-wide flat-dependency audit, initialize `external/json` too,
then verify the exact gitlink revisions before accepting the result. Do not
expect `imtg::mdbx` in the installed/exported
package; that boundary is deferred to M2d-b.

The install jobs must also run the repository-owned license assertion:

```sh
cmake -DIMTG_SOURCE_DIR=$PWD -DIMTG_INSTALL_PREFIX=$PWD/build/install-core \
  -P cmake/CheckInstalledLicense.cmake
cmake -DIMTG_SOURCE_DIR=$PWD -DIMTG_INSTALL_PREFIX=$PWD/build/install-commerce \
  -P cmake/CheckInstalledLicense.cmake
```

The assertion checks both the installed path and the SHA-256 content against the
root `LICENSE` file.

## Additional acceptance

Repeat the core and commerce checks in Debug, run the repository's ASan/UBSan
job, validate Doxygen and Markdown relative links, and inspect the CI matrix on
Ubuntu, Windows, and macOS. These checks establish the exercised build and
consumer paths only; they do not establish process-crash durability or TSan
coverage.

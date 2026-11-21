# ADR 0012: required internal LogIt++ diagnostics

**Status:** Accepted; supersedes the optional-observer-only draft of this ADR.
**Scope:** implementation dependency, configuration ownership, sanitization and packaging.
**Owner:** core maintainers; application owns logger configuration and lifecycle.
**Review trigger:** new event, backend dependency, ABI-affecting logger option or lifecycle policy.

## Context

The original draft adapted runtime/router observers through an optional
`imtg::logit` module. That did not provide internal core diagnostics and
duplicated errors once internal logging was added. LogIt++ is the standard
logger for LimiNode projects; retaining a dependency-free core is no longer a
goal. The public imtg domain API must still remain independent of vendor types.

## Decision

LogIt++ is a required implementation dependency of `imtg::core` and the optional
Telegram router. Real LogIt++ calls in their `.cpp` files emit fixed event codes
and numeric enum codes, without payloads, credentials, error/exception messages
or identifiers. The event/lifetime contract owns the exact diagnostic boundary.

The unmerged `LogitAdapter`, `<imtg/logging.hpp>`, `imtg::logit` and
`IMTG_BUILD_LOGIT` surface is removed, not retained as a no-op compatibility
layer. `ReferenceRuntime::ErrorFunction` and `UpdateRouter::OutcomeSink` remain
unchanged application notifications. A router callback error is logged once
by the runtime; applications choose whether their own observers add records.

LogIt++ backends, levels, formatting, context, bounded queue/drop policy and
flush/shutdown belong to the application. imtg does not register a backend,
change global options or call `LOGIT_SHUTDOWN()`. Calls happen outside imtg
mutexes and catch logger exceptions. These guards do not bound a blocking
backend; worker-safe backends and non-waiting queue policies are application
requirements. Application-provided diagnostic context must also be sanitized.

## Dependency resolution and package boundary

An existing `log-it-cpp::log-it-cpp` target wins before checking imtg's provider
paths or defaulting any dependency setting. Otherwise imtg adds its pinned
flat provider, reusing an existing `time_shield::time_shield` target or adding
the pinned TimeShield sibling. Missing required targets fail configure clearly.
No configure-time downloads, recursive submodules, generalized resolver or
raw vendor include-directory fallback is introduced.

The direct submodule pins are:

- LogIt++ `659c38eef86cb3cbab448fb76852641830a89430` (v1.0.2);
- TimeShield `d5f1203f24341e4146af35d5bb36e762056d181f` (v2.0.0).

Dependency test/example and NTP defaults are lightweight and never forced.
The NTP/test/example defaults are set only when the option is undefined,
preserving parent normal variables as well as cache entries under older CMake
policies.
All `LOGIT_WITH_*` settings belong to the application; a single shared target
propagates compatible includes, definitions and transitives, including the
context-dependent `LogRecord` layout.

The project now requires CMake 3.18, matching the pinned provider. The local
verification tool version is reported separately; a newer CMake run is not
proof of a minimum-version run. imtg's logging implementation and logger
fixtures compile as C++17 because the pinned provider's filesystem
`u8string()` usage is not C++20-clean on current libstdc++.

The static implementation links LogIt++ privately; its installed link-only
dependency is resolved by `find_package(imtg CONFIG REQUIRED)` unless the parent
already supplied the target. Standalone installs include the pinned provider
packages and license files. Parents supplying targets own installation and
compatible configuration of those packages. Upstream optional-backend
packaging restrictions remain upstream restrictions; imtg does not override
user configuration to conceal them.

## Verification and consequences

MemoryLogger tests exercise actual internal events, sanitization, exactly-once
task failure, bounded retention, throwing backend isolation, reentrant status
inspection outside runtime locks, preserved observers and logger usability
after imtg shutdown. The independent installed core consumer executes a failing
task and observes its diagnostic. Parent fixtures provide both dependencies
with context enabled and unavailable imtg provider paths, or only TimeShield
with imtg adding LogIt++; each builds and executes the same consumer.
Additional embedding modes let imtg create the providers with parent-selected
TimeShield NTP and both providers' tests/examples enabled through normal
variables or cache entries under the old CMP0126 behavior. They check the
effective option values, actual NTP target definition and test/example targets,
then build and run only the imtg consumer; no provider tests, examples or NTP
service are started.

No second logger interface, formatter, queue or scheduler is introduced.
Actual output remains optional because the application may register no backend,
but the implementation dependency is always present.

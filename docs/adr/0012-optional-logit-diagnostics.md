# ADR 0012: optional LogIt++ diagnostics boundary

**Status:** Accepted for the optional M3 diagnostics slice.
**Scope:** LogIt++ dependency ownership, observer adapters, and installed package boundaries.
**Owner:** application/integration layer; `imtg_core` remains independent.
**Review trigger:** a new diagnostic event, logger backend, or lifecycle policy.

## Context

`ReferenceRuntime` and the optional `telegram::UpdateRouter` already expose
non-throwing observers. Applications need bounded, sanitized operational
events, but the core package must remain usable without a logging framework.
LogIt++ v1.0.2 is header-only and requires TimeShield 2.0.x; its CMake project
requires CMake 3.18 even though imtg's core baseline remains CMake 3.16.

## Decision

The optional `IMTG_BUILD_LOGIT` profile provides `imtg::logit` and the
self-contained `<imtg/logging.hpp>` entry point. It binds
`ReferenceRuntime::ErrorFunction` and `telegram::UpdateRouter::OutcomeSink`,
and provides an explicit `record_update_admission()` call for synchronous
`receive()` results. The adapter emits fixed event codes and only emits
numeric bot/chat/update identifiers when `LogitOptions::include_safe_identifiers`
is true.

The direct sibling submodules are pinned to:

- LogIt++ `659c38eef86cb3cbab448fb76852641830a89430` (v1.0.2);
- TimeShield `d5f1203f24341e4146af35d5bb36e762056d181f` (v2.0.0).

Normal configuration performs no downloads and does not initialize nested
LogIt++ submodules. Context/MDC, OTLP, Prometheus, MDBX and fmt backends remain
disabled. The optional target is not part of `imtg_core` or base
`imtg::telegram`; core-only builds need neither LogIt++ nor TimeShield.

LogIt++ queue and logger lifecycle belong to the owning application. imtg does
not call `LOGIT_SHUTDOWN()` from runtime or router shutdown, and logger
failures are swallowed at the observer boundary. Applications must configure
bounded queue/retention policy and flush or shut down LogIt++ after imtg has
quiesced.

## Sanitization and evidence

The adapter never logs bot tokens, callback data, text, payment credentials,
provider secrets, HTTP headers, prompts, response bodies, or `Error::message`.
Tests use the bounded `MemoryLogger` to prove stable event coverage, retention
limits, identifier opt-in, and absence of sensitive fixture strings. The
optional installed-consumer test verifies that exported `imtg::logit`,
LogIt++, and TimeShield packages resolve together from a clean prefix.

## Consequences

Applications may use LogIt++, another logger, or no logger at all. The adapter
does not provide a second queue, formatter, scheduler, or tracing abstraction.
The current event set is intentionally narrow; adding a new event requires a
stable code, a sanitization review, and a deterministic test.

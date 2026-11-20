# ADR 0014: isolated outgoing kurlyk implementation

**Status:** implementation proposed in a Draft PR; upstream dependencies pending review.
**Owner:** transport/build maintainers.
**Scope:** concrete outgoing HTTP, optional package and flat network providers.
**Review trigger:** upstream lifecycle/body-limit changes, new platform provider,
or incoming webhook implementation.

## Context

ADR 0013 established the injected synchronous HTTP boundary and conservative
Telegram outcomes. kurlyk #50 bounds bytes in the libcurl write callback;
#51 supports offline, flat provider resolution. A concrete implementation can
now use these contracts without another HTTP client or a temporary post-buffer
size check. Incoming HTTP is a different ownership and acknowledgement boundary.

## Decision

Add the standalone `KurlykHttpTransport` and separately enabled
`imtg::telegram_kurlyk` target. Do not aggregate it in the generic umbrella or
core. Public API exposes only existing imtg HTTP values. The application owns
the shared kurlyk worker, admission policy and final shutdown; the compiled
adapter disables automatic startup/shutdown. Each exchange owns its request,
completion state and distinct cancellation group. Cancellation queues cleanup
without waiting for global quiescence. No late callback retains borrowed input.

Always configure a finite receive-body limit and timeout; disable retries,
redirects and verbose/header logging. Only explicit local/admission rejection
proves non-transmission. Loss or cancellation after submission remains ambiguous.
Preserve TLS verification and document the Windows trust-bundle prerequisite.
Body bounds do not imply bounded backend headers or process-wide concurrency.

Use parent targets first, then pinned direct siblings/system packages. Keep
kurlyk nested submodules uninitialized and prohibit configure downloads.
The installed optional component finds kurlyk only when requested, keeping a
core-only consumer independent from networking packages. Mandatory LogIt++
implementation integration is independently owned by sibling PR #38; this
outgoing PR does not copy or supersede its changes.

## Consequences and alternatives

An application must keep asynchronous worker progress through pending cleanup
and provide a valid Windows PEM trust bundle. Cancellation does not retract a
remote operation. Tests need actual loopback exchanges plus installed and
parent-target consumers, not only fake HTTP responses. Independent review of
the pinned upstream stack remains a merge gate.

A per-adapter worker would compete with the shared application lifecycle. A
second HTTP implementation or post-buffer truncation would bypass the accepted
limit contract. A combined incoming webhook/server PR would obscure different
TLS, authentication and durable-ACK requirements. The optional upstream server
foundation (#52) and later imtg webhook therefore remain separate changes.

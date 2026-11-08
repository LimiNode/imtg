# Operator console contract

**Status:** proposed, documentation-only. **Semantic owner:** operator-console
maintainers. This contract does not add an application, a GUI dependency, a
transport, or an operator API implementation to `imtg`.

The future `imtg-admin` application is an optional operator console for
managing several bots, shops and runtime instances. It is deliberately outside
`imtg_core`: the core owns backend/runtime contracts, while the console owns
presentation, operator workflows and a client for an authenticated operator
API.

## Architectural references

The intended UI composition is informed by two external projects:

- [ImGuiX](https://github.com/LimiNode/ImGuiX) is a candidate UI framework;
- [mgc-platform](https://github.com/LimiNode/mgc-platform) is a reference for
  GUI-shell lifecycle, model/controller separation and an optional local
  engine process.

These repositories are references, not dependencies or copied application
code. Their current APIs are not an `imtg` compatibility promise.

## Boundary and ownership

```text
imtg-admin (optional GUI process)
  ImGuiX view layer -> controllers -> local models -> OperatorApiClient
                                             |
                                  authenticated HTTP/WebSocket API
                                             |
                         imtg runtime, commerce and storage services
```

The remote operator API is the primary integration path. A local
`ProcessEngineController` may start and supervise a bot runtime for
development or single-machine deployments, but the console must also work
against an already-running remote service. Closing the GUI must not stop remote
bot or shop runtimes.

The console is not an authority for business state:

- payment, order, fulfillment, inventory, moderation and session state remain
  authoritative in the backend;
- a controller submits an authenticated command and renders the resulting
  revisioned state;
- a local model is a cache/projection and may be stale or incomplete;
- GUI events are process-local notifications, not a network protocol.

## Components

| Component | Responsibility |
|---|---|
| `AdminApplication` | Composition root, configuration, lifecycle and dependency wiring. |
| `MainWindow` | Stable navigation, selected bot/shop context and window shell. |
| Controllers | Translate user intent into model commands and render view state; no direct provider calls. |
| Models | Pending state, request lifecycle, cached projections and reconnect state. |
| `OperatorApiClient` | Authenticated transport, request IDs, idempotency keys, revision headers, timeout and reconnect policy. |
| GUI DTO/data types | Versioned, bounded data used by views; never expose storage or provider SDK types. |
| GUI event bus | Typed in-process events between models, controllers and windows. |
| `ProcessEngineController` | Optional local child-process start/stop, health and log attachment. |

The source layout is intentionally application-local and may evolve with the
chosen GUI framework:

```text
imtg-admin/
  apps/gui/                 # entry point and composition root
  src/imtg_admin/ui/        # windows, controllers, models, events and DTOs
  src/imtg_admin/operator/  # API client and transport adapters
  resources/                # icons, fonts, localization and themes
  tests/                    # model, controller and API-contract tests
```

The `.hpp`/`.cpp` pairing and `snake_case` function naming follow the project
guides when this application is eventually implemented.

## Context isolation

Every request and every cached projection is scoped by an explicit operator
context:

- selected runtime instance;
- bot identity;
- shop/merchant account identity;
- operator identity and authorization scope;
- request and observed revision.

Changing the selected bot or shop invalidates incompatible drafts, pending
requests and stale responses. A late response from one context must never
overwrite another context's model. The API must reject a command whose context
or expected revision does not match the authoritative state.

## Commands, queries and events

The operator API separates:

- queries, which return bounded revisioned projections;
- commands, which carry an authenticated operator identity, explicit scope,
  idempotency key and expected revision;
- audit events, which record actor, target, command, outcome and correlation
  identity.

DTOs are typed, versioned and transport-neutral. The GUI must not send ImGuiX
event objects, pointers, storage records or provider SDK objects across the
wire. A reconnect may replay projections and notifications, but it must not
replay a non-idempotent command without its original idempotency key.

## Security requirements

Before an operator API is implemented, its contract must define:

1. authentication and secure transport for local and remote connections;
2. per-operator and per-bot/shop authorization scopes;
3. TLS/certificate policy for remote deployment;
4. bounded request and response sizes, timeouts, rate limits and reconnect
   backoff;
5. audit records for sensitive commands, including payment/refund, access,
   moderation and configuration changes;
6. secret storage and redaction rules; tokens and payment credentials never
   enter ordinary GUI logs;
7. explicit confirmation and idempotency rules for destructive or financial
   operations.

The GUI is a privileged client, not a security boundary. Backend services must
repeat authorization and validate the selected context.

## Initial screen groups

The first screen set is intentionally small and backend-driven:

- Overview: runtime health, selected bot/shop, queue and error summary;
- Bots: instances, connection state, lifecycle and recent errors;
- Commerce: shops, catalog/order projections, payment attempts and refunds;
- Users/moderation: scoped users, permissions, moderation actions and audit;
- Logs: bounded, redacted, correlation-aware operational events;
- Settings: operator profile, connection, localization and UI preferences.

Screens may be added only when the corresponding backend contract and
authorization scope exist. Screenshots and visual references are design input,
not acceptance evidence.

## Lifecycle and recovery

The model/controller layer must represent `Loading`, `Ready`, `Stale`,
`Reconnecting` and `Forbidden` states. A lost connection must not silently
turn a command into success. After reconnect, the client refreshes the
authoritative projection and reports unresolved command outcomes by request ID
and idempotency key.

## Delivery gates

Implementation of `imtg-admin` is gated on:

1. an accepted operator API contract and DTO/version policy;
2. context-isolation tests for two bots and two shops using one provider;
3. authorization and audit conformance tests for sensitive commands;
4. reconnect, stale-revision, duplicate-command and unknown-outcome tests;
5. a headless model/controller test target that does not require a GUI;
6. a separate GUI target with an explicitly approved ImGuiX integration;
7. confirmation that the console remains optional and does not change the
   core library's build or runtime dependencies.

Until these gates are met, `imtg-admin` remains a roadmap item and the
existing `imtg` runtime remains usable without a GUI.

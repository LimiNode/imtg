# UI and actions contract

**Semantic owner:** UI/runtime maintainers. **Status:** contract-only.

The UI layer builds a transport-neutral tree from current state. `ui.begin()`, `ui.text()`, `ui.button()`, `ui.input_text()`, and `ui.end()` are conceptual examples and use `snake_case`. IDs are stable application data, never translated labels. A button may carry a typed payload and a callback that is valid only during the current action scope.

Callbacks are for short-lived interaction work: validate input, dispatch a typed event, or request navigation. Durable actions are registered commands/events with serializable data and must be used for jobs, retries, or post-restart work. Neither callbacks nor captured pointers/references are serialized.

The UI builder owns nodes until the render completes; `ViewContext` borrows read-only state. `ActionContext` owns dispatch capabilities for one invocation and is invalid after return. Unknown, expired, malformed, or over-sized callback tokens fail closed. Rendering must be deterministic for equal state and input.

**Verification:** deterministic tree snapshots, stable-ID tests, payload validation tests, callback expiry tests, and compile-time/API review. **Dependencies:** sessions, event contracts, and later a renderer/reconciler.

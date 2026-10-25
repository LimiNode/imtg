# Telegram delivery contract

**Semantic owner:** transport/delivery maintainers. **Status:** contract-only.

Delivery consumes renderer output and desired/applied revisions; it does not execute UI callbacks or own application state. A reconciler chooses send/edit operations, validates callback tokens, and records uncertain outcomes. Retries use bounded exponential backoff, rate limits, and idempotency keys where available. Telegram's external API cannot provide exactly-once delivery without corresponding guarantees.

Polling and webhook adapters are replaceable. `kurlyk` is the recommended future HTTP integration, not a core dependency. API limits and error behavior must be checked against official Telegram documentation at implementation time. **Verification:** fake transport tests for send/edit, duplicate/uncertain results, 429/backoff, malformed callbacks, revision races, and shutdown with pending outbox records.

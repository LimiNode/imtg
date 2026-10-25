# Localization contract

**Semantic owner:** localization maintainers. **Status:** contract-only.

Catalogs are immutable snapshots loaded from JSON or another documented source. A render selects a user/session locale and deterministic fallback chain; there is no global mutable language context. Templates accept named/typed arguments, plural rules are extensible toward CLDR, and text/media resources may be localized.

Stable UI IDs, callback tokens, and FSM states never depend on translated strings. Missing keys, unsupported plural categories, and malformed arguments have explicit fallback/error behavior. **Verification:** concurrent locale tests, fallback and plural tests, snapshot immutability tests, and ID independence tests. **Dependencies:** UI and sessions contracts.

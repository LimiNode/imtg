# Users, chats, and history contract

**Semantic owner:** domain-service maintainers. **Status:** contract-only.

User identity, chat metadata, permissions/roles/bans, settings, and optional message history are separate logical services. Runtime session state is mandatory for the framework; content history is opt-in. Applications provide retention, deletion, and access policies for stored message content.

Services accept typed identifiers and transaction scopes, return explicit not-found/conflict/failure results, and do not perform Telegram delivery directly. Authorization checks are evaluated against a consistent revision. **Verification:** ownership/permission tests, retention and deletion tests, multi-user locale tests, and service-level transaction tests. **Dependencies:** storage, sessions, localization, and delivery contracts.

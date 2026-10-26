# Users, chats, and history contract

**Semantic owner:** domain-service maintainers. **Status:** contract-only.

## Identity and storage separation

`UserId` identifies a Telegram user; `ChatId` identifies a private, group, or channel chat; `ConversationKey` identifies a serialized interaction scope. They are separate typed values and may not be substituted. User profile, chat metadata, permissions/roles/bans, settings, runtime session, technical event journal, and message history are separate logical services/records. Runtime state is mandatory for recovery; profile and history services are optional application capabilities.

## Message records

A history record is identified by `(bot_id, chat_id, message_id, revision)` with optional `thread_id`, `sender_user_id`, and update identity. The Telegram message ID is not globally unique without bot/chat context. Ordering uses a stored event timestamp plus a monotonic ingest sequence to break equal timestamps. Pagination is bounded and uses a continuation key rather than unbounded offsets; queries must state whether edits/deletions are represented as new revisions or tombstones.

Content capture is opt-in per application/chat policy. Metadata needed for runtime diagnostics may be retained separately from message text/media. Retention policy includes maximum age/count, legal deletion scope, and a deletion operation that removes content, indexes, derived search data, and associated media references. Deletion is idempotent and observable; expired content is not silently resurrected by replay.

## Authorization and journals

Permissions are evaluated against a consistent storage revision and are separate from transport delivery. A service returns an explicit authorization/not-found/conflict result; it does not send Telegram messages directly. The technical event journal records processing, errors, transitions, and delivery uncertainty without becoming a user-visible chat history. Journal retention and access are independently configured and must avoid copying message content unless explicitly required.

**Verification:** typed identity mix-up tests; message-key uniqueness tests; ordering/pagination tests; opt-in/retention/deletion tests; permission and audit tests; separation tests proving a technical event does not create history content; and multi-user locale/session tests. **Dependencies:** storage, sessions, localization, and delivery contracts.

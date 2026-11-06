# Users, chats, and history contract

**Semantic owner:** domain-service maintainers. **Status:** contract-only.

## Identity and storage separation

Application domains remain separate as well. A form draft, support ticket,
order/payment, inventory reservation, moderation decision and raid state each
have their own stable identity, revision, retention and authorization rules.
They must not be encoded into a `SessionRecord` merely because a UI currently
displays them. Headless handlers may update these records without creating a
session or render intent.

The shared permission boundary evaluates actor, bot, chat, role and action
against one consistent storage revision. Telegram administrator status is an
input to that decision, not a substitute for an application policy. A stale
worker receives an explicit conflict and cannot undo a newer override.

`UserId` identifies a Telegram user; `ChatId` identifies a private, group, or channel chat; `ConversationKey` identifies a serialized interaction scope. They are separate typed values and may not be substituted. User profile, chat metadata, permissions/roles/bans, settings, runtime session, technical event journal, and message history are separate logical services/records. Runtime state is mandatory for recovery; profile and history services are optional application capabilities.

## Message records

A history record is identified by `(bot_id, chat_id, message_id, revision)` with optional `thread_id`, `sender_user_id`, and update identity. The Telegram message ID is not globally unique without bot/chat context. Ordering uses a stored event timestamp plus a monotonic ingest sequence to break equal timestamps. Pagination is bounded and uses a continuation key rather than unbounded offsets; queries must state whether edits/deletions are represented as new revisions or tombstones.

Content capture is opt-in per application/chat policy. Metadata needed for runtime diagnostics may be retained separately from message text/media. Retention policy includes maximum age/count, legal deletion scope, and a deletion operation that removes content, indexes, derived search data, and associated media references. Deletion is idempotent and observable; expired content is not silently resurrected by replay.

## Authorization and journals

Attachments are represented by bounded metadata and an external media
reference; the adapter owns download/upload retries and deletion. Ticket and
commerce modules must not put private files, payment secrets or digital goods
in ordinary diagnostic logs. Moderation may retain only the evidence needed by
the configured appeal/audit policy, with redaction and expiry.

Permissions are evaluated against a consistent storage revision and are separate from transport delivery. A service returns an explicit authorization/not-found/conflict result; it does not send Telegram messages directly. The technical event journal records processing, errors, transitions, and delivery uncertainty without becoming a user-visible chat history. Journal retention and access are independently configured and must avoid copying message content unless explicitly required.

**Verification:** typed identity mix-up tests; message-key uniqueness tests;
ordering/pagination tests; opt-in/retention/deletion tests; permission and
audit tests; separation tests proving a technical event does not create history
content; domain tests for duplicate tickets/payments and unique-item
allocation; bounded moderation counters; and multi-user locale/session tests.
**Dependencies:** storage, sessions, localization, routing, jobs and delivery
contracts.

#include "imtg/sessions/session.hpp"

#include <utility>

namespace imtg {

namespace {

constexpr std::string_view session_namespace = "sessions";
constexpr std::string_view processed_namespace = "processed_updates";
constexpr std::string_view render_namespace = "render_intents";
constexpr std::string_view session_magic = "ISN1";
constexpr std::uint32_t session_format_version = 1;
constexpr std::uint32_t max_encoded_field = 64 * 1024;

Error encoding_error(ErrorCode code, const char* message)
{
    return Error{code, message};
}

void append_u32(std::string& output, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

void append_u64(std::string& output, std::uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

void append_string(std::string& output, std::string_view value)
{
    append_u32(output, static_cast<std::uint32_t>(value.size()));
    output.append(value.data(), value.size());
}

std::int64_t time_millis(ClockTime value)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        value.time_since_epoch()).count();
}

ClockTime from_millis(std::int64_t value)
{
    return ClockTime{std::chrono::milliseconds(value)};
}

class Reader final {
public:
    explicit Reader(std::string_view input) : m_input(input) {}

    Result<std::uint32_t> u32()
    {
        if (remaining() < 4) return encoding_error(ErrorCode::TruncatedEncoding, "u32 is truncated");
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(m_input[m_offset++])) << shift;
        }
        return value;
    }

    Result<std::uint64_t> u64()
    {
        if (remaining() < 8) return encoding_error(ErrorCode::TruncatedEncoding, "u64 is truncated");
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(m_input[m_offset++])) << shift;
        }
        return value;
    }

    Result<std::uint8_t> byte()
    {
        if (remaining() < 1) return encoding_error(ErrorCode::TruncatedEncoding, "byte is truncated");
        return static_cast<std::uint8_t>(static_cast<unsigned char>(m_input[m_offset++]));
    }

    Result<std::string> string()
    {
        const auto length = u32();
        if (!length) return length.error();
        if (length.value() > max_encoded_field) {
            return encoding_error(ErrorCode::StorageRecordTooLarge, "encoded field is too large");
        }
        if (remaining() < length.value()) {
            return encoding_error(ErrorCode::TruncatedEncoding, "encoded field is truncated");
        }
        std::string value(m_input.substr(m_offset, length.value()));
        m_offset += length.value();
        return value;
    }

    [[nodiscard]] std::size_t remaining() const noexcept { return m_input.size() - m_offset; }

private:
    std::string_view m_input;
    std::size_t m_offset = 0;
};

Result<void> validate_identity(const ConversationKey& key)
{
    if (key.encode().empty()) return encoding_error(ErrorCode::InvalidArgument, "conversation key is empty");
    return Result<void>();
}

Result<void> validate_record_shape(const SessionRecord& record, std::uint32_t expected_schema)
{
    if (!record.conversation.encode().empty() && record.schema_version == expected_schema
        && !record.state.name.empty() && record.state.version != 0
        && !record.screen.name.empty() && record.screen.version != 0) {
        if (record.pending_input &&
            (record.pending_input->name.empty() || record.pending_input->version == 0)) {
            return encoding_error(ErrorCode::InvalidArgument, "pending input identity is invalid");
        }
        return Result<void>();
    }
    if (record.schema_version != expected_schema) {
        return encoding_error(ErrorCode::UnsupportedVersion, "session schema version is unsupported");
    }
    return encoding_error(ErrorCode::InvalidArgument, "session record identity is invalid");
}

UpdateProcessResult commit_result(CommitResult result, std::optional<SessionRecord> session = std::nullopt,
                                   std::optional<RenderIntentId> intent = std::nullopt)
{
    UpdateProcessResult output{UpdateProcessOutcome::Failed, std::move(result.error),
                               std::move(session), std::move(intent)};
    switch (result.outcome) {
    case CommitOutcome::Committed: output.outcome = UpdateProcessOutcome::Committed; break;
    case CommitOutcome::RejectedConflict: output.outcome = UpdateProcessOutcome::RejectedConflict; break;
    case CommitOutcome::Failed: output.outcome = UpdateProcessOutcome::Failed; break;
    case CommitOutcome::Unknown: output.outcome = UpdateProcessOutcome::Unknown; break;
    }
    return output;
}

} // namespace

Result<StorageBytes> encode_session_record(const SessionRecord& record)
{
    if (record.schema_version == 0 || record.state.name.empty() || record.state.version == 0
        || record.screen.name.empty() || record.screen.version == 0) {
        return encoding_error(ErrorCode::InvalidArgument, "session record contains an invalid identity");
    }
    const auto key = record.conversation.encode();
    std::string output(session_magic);
    append_u32(output, session_format_version);
    append_u32(output, record.schema_version);
    append_string(output, key);
    append_string(output, record.state.name);
    append_u32(output, record.state.version);
    append_string(output, record.screen.name);
    append_u32(output, record.screen.version);
    append_u64(output, record.revision);
    output.push_back(record.pending_input ? '\x01' : '\x00');
    if (record.pending_input) {
        append_string(output, record.pending_input->name);
        append_u32(output, record.pending_input->version);
    }
    output.push_back(record.deadline ? '\x01' : '\x00');
    if (record.deadline) append_u64(output, static_cast<std::uint64_t>(time_millis(*record.deadline)));
    append_u64(output, static_cast<std::uint64_t>(time_millis(record.created_at)));
    append_u64(output, static_cast<std::uint64_t>(time_millis(record.last_transition_at)));
    if (output.size() > max_encoded_field) {
        return encoding_error(ErrorCode::StorageRecordTooLarge, "session record is too large");
    }
    return output;
}

Result<SessionRecord> decode_session_record(std::string_view encoded)
{
    if (encoded.size() > max_encoded_field) {
        return encoding_error(ErrorCode::StorageRecordTooLarge, "session record is too large");
    }
    if (encoded.size() < session_magic.size() || encoded.substr(0, session_magic.size()) != session_magic) {
        return encoding_error(ErrorCode::InvalidEncoding, "session record magic is invalid");
    }
    Reader reader(encoded.substr(session_magic.size()));
    const auto format = reader.u32();
    if (!format) return format.error();
    if (format.value() != session_format_version) {
        return encoding_error(ErrorCode::UnsupportedVersion, "session record format is unsupported");
    }
    SessionRecord record;
    const auto schema = reader.u32();
    if (!schema) return schema.error();
    record.schema_version = schema.value();
    const auto key = reader.string();
    if (!key) return key.error();
    const auto parsed_key = parse_conversation_key(key.value());
    if (!parsed_key) return parsed_key.error();
    record.conversation = parsed_key.value();
    const auto state_name = reader.string();
    if (!state_name) return state_name.error();
    const auto state_version = reader.u32();
    if (!state_version) return state_version.error();
    record.state = FsmStateId{state_name.value(), state_version.value()};
    const auto screen_name = reader.string();
    if (!screen_name) return screen_name.error();
    const auto screen_version = reader.u32();
    if (!screen_version) return screen_version.error();
    record.screen = ScreenIdentity{screen_name.value(), screen_version.value()};
    const auto revision = reader.u64();
    if (!revision) return revision.error();
    record.revision = revision.value();
    const auto pending_marker = reader.byte();
    if (!pending_marker) return pending_marker.error();
    if (pending_marker.value() > 1) {
        return encoding_error(ErrorCode::InvalidEncoding, "session pending marker is malformed");
    }
    if (pending_marker.value() != 0) {
        const auto pending_name = reader.string();
        if (!pending_name) return pending_name.error();
        const auto pending_version = reader.u32();
        if (!pending_version) return pending_version.error();
        record.pending_input = PendingInput{pending_name.value(), pending_version.value()};
    }
    const auto deadline_marker = reader.byte();
    if (!deadline_marker) return deadline_marker.error();
    if (deadline_marker.value() > 1) {
        return encoding_error(ErrorCode::InvalidEncoding, "session deadline marker is malformed");
    }
    if (deadline_marker.value() != 0) {
        const auto deadline = reader.u64();
        if (!deadline) return deadline.error();
        record.deadline = from_millis(static_cast<std::int64_t>(deadline.value()));
    }
    const auto created = reader.u64();
    if (!created) return created.error();
    const auto transitioned = reader.u64();
    if (!transitioned) return transitioned.error();
    record.created_at = from_millis(static_cast<std::int64_t>(created.value()));
    record.last_transition_at = from_millis(static_cast<std::int64_t>(transitioned.value()));
    if (reader.remaining() != 0) {
        return encoding_error(ErrorCode::InvalidEncoding, "session record has trailing bytes");
    }
    return record;
}

SessionRepository::SessionRepository(std::uint32_t schema_version,
                                     std::size_t max_record_size) noexcept
    : m_schema_version(schema_version), m_max_record_size(max_record_size)
{
}

Result<void> SessionRepository::validate(const SessionRecord& record) const
{
    if (m_max_record_size == 0) return encoding_error(ErrorCode::StorageRecordTooLarge, "session record limit is zero");
    return validate_record_shape(record, m_schema_version);
}

Result<std::optional<SessionRecord>> SessionRepository::load(
    const ConversationKey& key, ITransaction& transaction) const
{
    const auto identity = validate_identity(key);
    if (!identity) return identity.error();
    const auto stored = transaction.read(session_namespace, key.encode());
    if (!stored) return stored.error();
    if (!stored.value()) return std::optional<SessionRecord>{};
    if (stored.value()->value.size() > m_max_record_size) {
        return encoding_error(ErrorCode::StorageRecordTooLarge, "stored session exceeds repository limit");
    }
    auto decoded = decode_session_record(stored.value()->value);
    if (!decoded) return decoded.error();
    if (decoded.value().conversation != key) {
        return encoding_error(ErrorCode::StorageSerializationFailure, "session key does not match record");
    }
    if (decoded.value().schema_version != m_schema_version) {
        return encoding_error(ErrorCode::UnsupportedVersion, "session schema version is unsupported");
    }
    decoded.value().revision = stored.value()->revision;
    return std::optional<SessionRecord>{std::move(decoded).value()};
}

Result<SessionRecord> SessionRepository::create(SessionRecord record, ITransaction& transaction) const
{
    const auto valid_record = validate(record);
    if (!valid_record) return valid_record.error();
    if (record.revision != 0) {
        return encoding_error(ErrorCode::StorageConflict, "new session must have revision zero");
    }
    const auto key = record.conversation.encode();
    auto encoded = encode_session_record(record);
    if (!encoded) return encoded.error();
    if (encoded.value().size() > m_max_record_size) {
        return encoding_error(ErrorCode::StorageRecordTooLarge, "session record exceeds repository limit");
    }
    const auto created = transaction.create_if_absent(session_namespace, key, encoded.value());
    if (!created) return created.error();
    record.revision = created.value();
    encoded = encode_session_record(record);
    if (!encoded) return encoded.error();
    if (encoded.value().size() > m_max_record_size) {
        return encoding_error(ErrorCode::StorageRecordTooLarge, "session record exceeds repository limit");
    }
    const auto rewritten = transaction.write(session_namespace, key, encoded.value(), created.value());
    if (!rewritten) return rewritten.error();
    record.revision = rewritten.value();
    return record;
}

Result<SessionRecord> SessionRepository::save(SessionRecord record,
                                              StorageRevision expected_revision,
                                              ITransaction& transaction) const
{
    const auto valid_record = validate(record);
    if (!valid_record) return valid_record.error();
    if (expected_revision == 0 || (record.revision != 0 && record.revision != expected_revision)) {
        return encoding_error(ErrorCode::StorageConflict, "session expected revision is invalid");
    }
    const auto key = record.conversation.encode();
    record.revision = 0;
    auto encoded = encode_session_record(record);
    if (!encoded) return encoded.error();
    if (encoded.value().size() > m_max_record_size) {
        return encoding_error(ErrorCode::StorageRecordTooLarge, "session record exceeds repository limit");
    }
    const auto written = transaction.write(session_namespace, key, encoded.value(), expected_revision);
    if (!written) return written.error();
    record.revision = written.value();
    encoded = encode_session_record(record);
    if (!encoded) return encoded.error();
    if (encoded.value().size() > m_max_record_size) {
        return encoding_error(ErrorCode::StorageRecordTooLarge, "session record exceeds repository limit");
    }
    const auto rewritten = transaction.write(session_namespace, key, encoded.value(), written.value());
    if (!rewritten) return rewritten.error();
    record.revision = rewritten.value();
    return record;
}

Result<void> SessionRepository::erase(const ConversationKey& key,
                                       StorageRevision expected_revision,
                                       ITransaction& transaction) const
{
    const auto identity = validate_identity(key);
    if (!identity) return identity.error();
    if (expected_revision == 0) return encoding_error(ErrorCode::StorageConflict, "session revision is zero");
    return transaction.erase(session_namespace, key.encode(), expected_revision);
}

std::string UpdateIdentity::encode() const
{
    return "v1|b=" + std::to_string(bot_id.value) + "|u=" + std::to_string(update_id);
}

Result<bool> ProcessedUpdateRepository::record_if_new(
    const UpdateIdentity& identity, ITransaction& transaction) const
{
    const auto key = identity.encode();
    const auto found = transaction.read(processed_namespace, key);
    if (!found) return found.error();
    if (found.value()) return false;
    const auto created = transaction.create_if_absent(processed_namespace, key, "v1");
    if (!created) return created.error();
    return true;
}

RenderIntentId RenderIntentRepository::make_id(const ConversationKey& conversation,
                                               StorageRevision session_revision)
{
    return RenderIntentId{conversation.encode() + "|r=" + std::to_string(session_revision)};
}

Result<RenderIntentId> RenderIntentRepository::append(const RenderIntent& intent,
                                                      ITransaction& transaction) const
{
    if (intent.session_revision == 0 || intent.conversation.encode().empty()
        || intent.id.value != make_id(intent.conversation, intent.session_revision).value) {
        return encoding_error(ErrorCode::InvalidArgument, "render intent identity is invalid");
    }
    if (intent.metadata.size() > max_encoded_field) {
        return encoding_error(ErrorCode::StorageRecordTooLarge, "render intent metadata is too large");
    }
    auto value = intent.conversation.encode();
    value.push_back('\0');
    append_u64(value, intent.session_revision);
    append_string(value, intent.metadata);
    const auto created = transaction.create_if_absent(render_namespace, intent.id.value, value);
    if (!created) return created.error();
    return intent.id;
}

Result<std::optional<RenderIntent>> RenderIntentRepository::load(
    const RenderIntentId& id, ITransaction& transaction) const
{
    const auto stored = transaction.read(render_namespace, id.value);
    if (!stored) return stored.error();
    if (!stored.value()) return std::optional<RenderIntent>{};
    const auto separator = stored.value()->value.find('\0');
    if (separator == std::string::npos) {
        return encoding_error(ErrorCode::StorageSerializationFailure, "render intent is malformed");
    }
    const auto key = parse_conversation_key(
        std::string_view(stored.value()->value).substr(0, separator));
    if (!key) return key.error();
    Reader reader(std::string_view(stored.value()->value).substr(separator + 1));
    const auto revision = reader.u64();
    if (!revision) return revision.error();
    const auto metadata = reader.string();
    if (!metadata || reader.remaining() != 0) {
        return encoding_error(ErrorCode::StorageSerializationFailure, "render intent payload is malformed");
    }
    RenderIntent intent{ id, key.value(), revision.value(), metadata.value() };
    if (id.value != make_id(intent.conversation, intent.session_revision).value) {
        return encoding_error(ErrorCode::StorageSerializationFailure, "render intent identity does not match payload");
    }
    return std::optional<RenderIntent>{std::move(intent)};
}

AtomicUpdateProcessor::AtomicUpdateProcessor(
    IStorageBackend& backend, const ISessionRepository& sessions,
    const IProcessedUpdateStore& processed, const IRenderIntentStore& render_intents)
    : m_backend(backend), m_sessions(sessions), m_processed(processed), m_render_intents(render_intents)
{
}

UpdateProcessResult AtomicUpdateProcessor::process(
    const UpdateIdentity& update, const ConversationKey& conversation, const Fsm& fsm,
    const FsmEvent& event, ClockTime now, std::optional<SessionRecord> initial_session,
    UpdateProcessOptions options) const
{
    auto work = UnitOfWork::begin(m_backend, TransactionMode::ReadWrite);
    if (!work) return UpdateProcessResult{UpdateProcessOutcome::Failed, work.error(), {}, {}};
    auto& transaction = *work.value().transaction();
    const auto recorded = m_processed.record_if_new(update, transaction);
    if (!recorded) return UpdateProcessResult{UpdateProcessOutcome::Failed, recorded.error(), {}, {}};
    if (!recorded.value()) {
        const auto result = work.value().commit();
        if (result.outcome != CommitOutcome::Committed) return commit_result(result);
        return UpdateProcessResult{UpdateProcessOutcome::Duplicate, std::nullopt, {}, {}};
    }

    auto loaded = m_sessions.load(conversation, transaction);
    if (!loaded) return UpdateProcessResult{UpdateProcessOutcome::Failed, loaded.error(), {}, {}};
    SessionRecord session;
    if (loaded.value()) {
        session = *loaded.value();
    } else {
        if (!initial_session || initial_session->conversation != conversation) {
            return UpdateProcessResult{UpdateProcessOutcome::Failed,
                encoding_error(ErrorCode::StorageNotFound, "initial session is required"), {}, {}};
        }
        session = *initial_session;
        session.revision = 0;
        if (session.created_at == ClockTime{}) session.created_at = now;
        session.last_transition_at = now;
    }
    FsmEvent effective_event = event;
    if (session.deadline && now >= *session.deadline && event.kind == FsmEventKind::Normal) {
        effective_event = FsmEvent::timeout();
    }
    const auto step = fsm.step(session.state, effective_event);
    if (!step) return UpdateProcessResult{UpdateProcessOutcome::Failed, step.error(), {}, {}};
    const bool session_changed = !loaded.value() || step.value().changed;
    if (step.value().changed) {
        session.state = step.value().state;
        session.last_transition_at = now;
        if (session.revision == 0) {
            auto created = m_sessions.create(session, transaction);
            if (!created) return UpdateProcessResult{UpdateProcessOutcome::Failed, created.error(), {}, {}};
            session = std::move(created).value();
        } else {
            auto saved = m_sessions.save(session, session.revision, transaction);
            if (!saved) return UpdateProcessResult{UpdateProcessOutcome::Failed, saved.error(), {}, {}};
            session = std::move(saved).value();
        }
    } else if (!loaded.value()) {
        auto created = m_sessions.create(session, transaction);
        if (!created) return UpdateProcessResult{UpdateProcessOutcome::Failed, created.error(), {}, {}};
        session = std::move(created).value();
    }

    std::optional<RenderIntentId> intent_id;
    if (options.create_render_intent && session_changed) {
        RenderIntent intent{RenderIntentRepository::make_id(conversation, session.revision),
                            conversation, session.revision, {}};
        const auto appended = m_render_intents.append(intent, transaction);
        if (!appended) return UpdateProcessResult{UpdateProcessOutcome::Failed, appended.error(), {}, {}};
        intent_id = appended.value();
    }
    const auto committed = work.value().commit();
    if (committed.outcome != CommitOutcome::Committed) return commit_result(committed);
    return UpdateProcessResult{UpdateProcessOutcome::Committed, std::nullopt,
                               std::move(session), std::move(intent_id)};
}

} // namespace imtg

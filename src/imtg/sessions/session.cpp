#include "imtg/sessions/session.hpp"

#include <limits>
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

Result<ClockTime> decode_time_millis(std::uint64_t raw)
{
    // Decode the existing two's-complement wire representation without an
    // implementation-defined uint64_t -> int64_t conversion.
    std::int64_t millis = 0;
    if (raw <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        millis = static_cast<std::int64_t>(raw);
    } else if (raw == (std::uint64_t{1} << 63)) {
        millis = std::numeric_limits<std::int64_t>::min();
    } else {
        const auto magnitude = static_cast<std::int64_t>(~raw);
        millis = -1 - magnitude;
    }
    using Duration = ClockTime::duration;
    using Rep = Duration::rep;
    const long double ticks = static_cast<long double>(millis) *
        (1000.0L * static_cast<long double>(Duration::period::den)) /
        static_cast<long double>(Duration::period::num);
    const long double min_rep = static_cast<long double>(std::numeric_limits<Rep>::lowest());
    const long double max_rep = static_cast<long double>(std::numeric_limits<Rep>::max());
    if (ticks < min_rep || ticks > max_rep) {
        return encoding_error(ErrorCode::InvalidEncoding, "timestamp is outside the platform clock range");
    }
    return ClockTime{std::chrono::duration_cast<Duration>(std::chrono::milliseconds(millis))};
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
    const auto identity = validate_identity(record.conversation);
    if (!identity) return identity.error();
    if (record.schema_version == 0 || record.state.name.empty() || record.state.version == 0
        || record.screen.name.empty() || record.screen.version == 0) {
        return encoding_error(ErrorCode::InvalidArgument, "session record contains an invalid identity");
    }
    const auto field_ok = [](std::string_view field) {
        return field.size() <= max_encoded_field && field.size() <=
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    };
    const auto key = record.conversation.encode();
    if (!field_ok(key) || !field_ok(record.state.name) || !field_ok(record.screen.name) ||
        (record.pending_input && !field_ok(record.pending_input->name))) {
        return encoding_error(ErrorCode::StorageRecordTooLarge, "session record field is too large");
    }
    if (record.pending_input && record.pending_input->version == 0) {
        return encoding_error(ErrorCode::InvalidArgument, "pending input identity is invalid");
    }
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
    if (schema.value() == 0) {
        return encoding_error(ErrorCode::UnsupportedVersion, "session schema version is zero");
    }
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
        const auto decoded_deadline = decode_time_millis(deadline.value());
        if (!decoded_deadline) return decoded_deadline.error();
        record.deadline = decoded_deadline.value();
    }
    const auto created = reader.u64();
    if (!created) return created.error();
    const auto transitioned = reader.u64();
    if (!transitioned) return transitioned.error();
    const auto decoded_created = decode_time_millis(created.value());
    if (!decoded_created) return decoded_created.error();
    const auto decoded_transitioned = decode_time_millis(transitioned.value());
    if (!decoded_transitioned) return decoded_transitioned.error();
    record.created_at = decoded_created.value();
    record.last_transition_at = decoded_transitioned.value();
    if (reader.remaining() != 0) {
        return encoding_error(ErrorCode::InvalidEncoding, "session record has trailing bytes");
    }
    const auto shape = validate_record_shape(record, record.schema_version);
    if (!shape) return shape.error();
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

RenderIntentId RenderIntentRepository::make_id(const ConversationKey& conversation,
                                               StorageRevision session_revision,
                                               const UpdateIdentity& origin)
{
    return RenderIntentId{conversation.encode() + "|r=" + std::to_string(session_revision) +
                          "|u=" + origin.encode()};
}

Result<RenderIntentId> RenderIntentRepository::append(const RenderIntent& intent,
                                                      ITransaction& transaction) const
{
    if (intent.session_revision == 0 || intent.conversation.encode().empty()
        || (intent.origin
            ? intent.id.value != make_id(intent.conversation, intent.session_revision, *intent.origin).value
            : intent.id.value != make_id(intent.conversation, intent.session_revision).value)) {
        return encoding_error(ErrorCode::InvalidArgument, "render intent identity is invalid");
    }
    if (intent.metadata.size() > max_encoded_field) {
        return encoding_error(ErrorCode::StorageRecordTooLarge, "render intent metadata is too large");
    }
    auto value = intent.conversation.encode();
    value.push_back('\0');
    append_u64(value, intent.session_revision);
    append_string(value, intent.metadata);
    value.push_back(intent.origin ? '\x01' : '\x00');
    if (intent.origin) append_string(value, intent.origin->encode());
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
    if (!metadata) {
        return encoding_error(ErrorCode::StorageSerializationFailure, "render intent payload is malformed");
    }
    const auto origin_marker = reader.byte();
    if (!origin_marker || origin_marker.value() > 1) {
        return encoding_error(ErrorCode::StorageSerializationFailure, "render intent origin marker is malformed");
    }
    std::optional<UpdateIdentity> origin;
    if (origin_marker.value() != 0) {
        const auto encoded_origin = reader.string();
        if (!encoded_origin) return encoded_origin.error();
        const auto marker = encoded_origin.value().find("v1|b=");
        const auto separator = encoded_origin.value().find("|u=");
        if (marker != 0 || separator == std::string::npos) {
            return encoding_error(ErrorCode::StorageSerializationFailure, "render intent origin is malformed");
        }
        const auto bot_text = encoded_origin.value().substr(5, separator - 5);
        const auto update_text = encoded_origin.value().substr(separator + 3);
        const auto digits = [](std::string_view text) {
            if (text.empty()) return false;
            for (const auto ch : text) if (ch < '0' || ch > '9') return false;
            return true;
        };
        if (!digits(bot_text) || !digits(update_text)) {
            return encoding_error(ErrorCode::StorageSerializationFailure, "render intent origin is malformed");
        }
        try {
            origin = UpdateIdentity{BotId{std::stoull(std::string(bot_text))},
                                    std::stoull(std::string(update_text))};
        } catch (...) {
            return encoding_error(ErrorCode::StorageSerializationFailure, "render intent origin is malformed");
        }
    }
    if (reader.remaining() != 0) {
        return encoding_error(ErrorCode::StorageSerializationFailure, "render intent payload is malformed");
    }
    RenderIntent intent{ id, key.value(), revision.value(), metadata.value(), origin };
    if (origin && !(origin->bot_id == intent.conversation.bot_id)) {
        return encoding_error(ErrorCode::StorageSerializationFailure,
                              "render intent origin bot does not match conversation");
    }
    if ((origin ? make_id(intent.conversation, intent.session_revision, *origin)
                : make_id(intent.conversation, intent.session_revision)).value != id.value) {
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
    if (!(update.bot_id == conversation.bot_id)) {
        return UpdateProcessResult{UpdateProcessOutcome::Failed,
            encoding_error(ErrorCode::InvalidArgument, "update bot identity does not match conversation"), {}, {}};
    }
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
    if (options.require_expected_revision) {
        if (!options.expected_revision || session.revision != *options.expected_revision) {
            return UpdateProcessResult{UpdateProcessOutcome::RejectedConflict,
                encoding_error(ErrorCode::StorageConflict, "session revision expectation is stale"), {}, {}};
        }
    } else if (options.expected_revision && session.revision != *options.expected_revision) {
        return UpdateProcessResult{UpdateProcessOutcome::RejectedConflict,
            encoding_error(ErrorCode::StorageConflict, "session revision expectation is stale"), {}, {}};
    }
    if (options.require_expected_revision) {
        const auto watched = transaction.watch(session_namespace, conversation.encode(), session.revision);
        if (!watched) return UpdateProcessResult{UpdateProcessOutcome::RejectedConflict,
            watched.error(), {}, {}};
    }
    FsmEvent effective_event = event;
    if (event.kind == FsmEventKind::Timeout &&
        (!session.deadline || now < *session.deadline)) {
        return UpdateProcessResult{UpdateProcessOutcome::Failed,
            encoding_error(ErrorCode::ExpiredAction, "timeout is not currently eligible"), {}, {}};
    }
    if (session.deadline && now >= *session.deadline && event.kind == FsmEventKind::Normal) {
        effective_event = FsmEvent::timeout();
    }
    const auto step = fsm.step(session.state, effective_event);
    if (!step) return UpdateProcessResult{UpdateProcessOutcome::Failed, step.error(), {}, {}};
    const auto original_pending = session.pending_input;
    const auto original_deadline = session.deadline;
    const auto original_screen = session.screen;
    if (step.value().changed) {
        session.state = step.value().state;
        session.last_transition_at = now;
        // Default transition policy: metadata belonging to the previous state is
        // cleared, and the new state becomes the screen identity. Applications
        // can explicitly repopulate these fields in a subsequent transaction.
        session.pending_input.reset();
        session.deadline.reset();
        session.screen = ScreenIdentity{session.state.name, session.state.version};
        if (options.next_pending_input) session.pending_input = options.next_pending_input;
        if (options.next_deadline) session.deadline = options.next_deadline;
        if (options.next_screen) session.screen = *options.next_screen;
    } else {
        // Metadata patches are also valid for a self-transition. The legacy
        // next_* fields remain shorthand for Set(value).
        if (options.next_pending_input) session.pending_input = options.next_pending_input;
        if (options.next_deadline) session.deadline = options.next_deadline;
        if (options.next_screen) session.screen = *options.next_screen;
    }
    const auto apply_pending = [&session](const MetadataPatch<PendingInput>& patch) {
        if (patch.mode == MetadataPatchMode::Clear) session.pending_input.reset();
        else if (patch.mode == MetadataPatchMode::Set && patch.value) session.pending_input = patch.value;
    };
    const auto apply_deadline = [&session](const MetadataPatch<ClockTime>& patch) {
        if (patch.mode == MetadataPatchMode::Clear) session.deadline.reset();
        else if (patch.mode == MetadataPatchMode::Set && patch.value) session.deadline = patch.value;
    };
    const auto apply_screen = [&session](const MetadataPatch<ScreenIdentity>& patch) -> Result<void> {
        if (patch.mode == MetadataPatchMode::Clear) {
            return encoding_error(ErrorCode::InvalidArgument, "screen identity cannot be cleared");
        }
        if (patch.mode == MetadataPatchMode::Set && patch.value) session.screen = *patch.value;
        return Result<void>();
    };
    apply_pending(options.pending_input_patch);
    apply_deadline(options.deadline_patch);
    const auto screen_patch = apply_screen(options.screen_patch);
    if (!screen_patch) return UpdateProcessResult{UpdateProcessOutcome::Failed, screen_patch.error(), {}, {}};
    const bool metadata_changed = !(original_pending == session.pending_input) ||
        !(original_deadline == session.deadline) || !(original_screen == session.screen);
    const bool session_changed = !loaded.value() || step.value().changed || metadata_changed;
    if (session_changed && (step.value().changed || metadata_changed)) {
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
    const bool wants_render = options.create_render_intent &&
        (options.render_policy == RenderPolicy::Always ||
         (options.render_policy == RenderPolicy::OnChange && session_changed));
    if (wants_render) {
        const auto id = options.render_policy == RenderPolicy::Always
            ? RenderIntentRepository::make_id(conversation, session.revision, update)
            : RenderIntentRepository::make_id(conversation, session.revision);
        RenderIntent intent{id, conversation, session.revision, {},
                            options.render_policy == RenderPolicy::Always
                                ? std::optional<UpdateIdentity>{update} : std::nullopt};
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

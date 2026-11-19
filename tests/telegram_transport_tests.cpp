#include <imtg/telegram_transport.hpp>

#include "test_support.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {

class FixtureTransport final : public imtg::telegram_transport::IHttpTransport {
public:
    std::vector<imtg::telegram_transport::HttpExchange> fixtures;
    std::vector<imtg::telegram_transport::HttpRequest> requests;

    imtg::telegram_transport::HttpExchange execute(
        const imtg::telegram_transport::HttpRequest& request,
        const imtg::telegram_transport::CancellationToken&) noexcept override
    {
        requests.push_back(request);
        if (fixtures.empty()) {
            return {std::nullopt,
                    imtg::telegram_transport::HttpFailureKind::Network, false};
        }
        auto exchange = std::move(fixtures.front());
        fixtures.erase(fixtures.begin());
        return exchange;
    }
};

imtg::telegram_transport::HttpExchange response(
    int status, std::string body)
{
    return {imtg::telegram_transport::HttpResponse{status, {}, std::move(body)},
            std::nullopt, true};
}

std::string message_response(std::int64_t chat, std::int64_t message)
{
    return "{\"ok\":true,\"result\":{\"message_id\":" + std::to_string(message)
        + ",\"chat\":{\"id\":" + std::to_string(chat) + "}}}";
}

std::string error_response(unsigned int code, std::string parameters = {})
{
    return "{\"ok\":false,\"error_code\":" + std::to_string(code)
        + ",\"description\":\"not exposed\"" + parameters + "}";
}

} // namespace

int main()
{
    using namespace imtg::telegram_transport;

    FixtureTransport transport;
    TelegramBotApiConfig config;
    config.max_request_bytes = 1024;
    config.max_response_bytes = 512;
    config.max_text_bytes = 64;
    config.timeout = std::chrono::milliseconds(1250);
    TelegramBotApi api("123:fixture_token", transport, config);

    transport.fixtures.push_back(response(200, message_response(-100, 91)));
    const auto sent = api.send_message(imtg::ChatId{-100}, "hello & world");
    CHECK(sent.succeeded());
    CHECK(transport.requests.size() == 1);
    CHECK(transport.requests[0].url
          == "https://api.telegram.org/bot123:fixture_token/sendMessage");
    CHECK(sent.message_id.has_value());
    CHECK(sent.message_id->chat_id.value == -100);
    CHECK(sent.message_id->value == 91);
    CHECK(transport.requests[0].body == "chat_id=-100&text=hello%20%26%20world");
    CHECK(transport.requests[0].headers.size() == 1);
    CHECK(transport.requests[0].headers[0].first == "Content-Type");
    CHECK(transport.requests[0].headers[0].second
          == "application/x-www-form-urlencoded");
    CHECK(transport.requests[0].timeout == std::chrono::milliseconds(1250));
    CHECK(transport.requests[0].max_response_bytes == 512);

    transport.fixtures.push_back(response(200, message_response(4, 17)));
    const auto edited = api.edit_message_text(imtg::ChatId{4}, 17, "edited");
    CHECK(edited.succeeded());
    CHECK(transport.requests.back().url
          == "https://api.telegram.org/bot123:fixture_token/editMessageText");
    CHECK(edited.message_id.has_value());
    CHECK(edited.message_id->value == 17);
    CHECK(transport.requests.back().body == "chat_id=4&message_id=17&text=edited");

    transport.fixtures.push_back(response(200, "{\"ok\":true,\"result\":true}"));
    const auto answered = api.answer_callback_query("callback/1");
    CHECK(answered.succeeded());
    CHECK(transport.requests.back().url
          == "https://api.telegram.org/bot123:fixture_token/answerCallbackQuery");
    CHECK(!answered.message_id.has_value());
    CHECK(transport.requests.back().body == "callback_query_id=callback%2F1");
    transport.fixtures.push_back(response(200,
        "{\"ok\":true,\"result\":true,\"description\":\"optional success description\"}"));
    CHECK(api.answer_callback_query("callback").succeeded());

    auto limited = response(429, error_response(429, ",\"parameters\":{\"retry_after\":7}"));
    limited.response->headers.emplace_back("rEtRy-AfTeR", "7");
    transport.fixtures.push_back(std::move(limited));
    const auto rate_limited = api.send_message(imtg::ChatId{1}, "slow");
    CHECK(rate_limited.outcome == TelegramOutcome::RateLimited);
    CHECK(rate_limited.retry_after_seconds.has_value());
    CHECK(*rate_limited.retry_after_seconds == 7U);
    CHECK(rate_limited.api_error_code == std::optional<std::uint32_t>(429));

    transport.fixtures.push_back(response(400, error_response(400)));
    CHECK(api.send_message(imtg::ChatId{1}, "bad").outcome
          == TelegramOutcome::ClientError);
    transport.fixtures.push_back(response(503, error_response(503)));
    CHECK(api.send_message(imtg::ChatId{1}, "down").outcome
          == TelegramOutcome::ServerError);

    transport.fixtures.push_back(
        {std::nullopt, HttpFailureKind::Timeout, false});
    CHECK(api.send_message(imtg::ChatId{1}, "timeout").outcome
          == TelegramOutcome::Timeout);
    transport.fixtures.push_back(
        {std::nullopt, HttpFailureKind::Timeout, true});
    CHECK(api.send_message(imtg::ChatId{1}, "ambiguous").outcome
          == TelegramOutcome::UnknownOutcome);

    CancellationSource source;
    source.cancel();
    const auto before_cancel = transport.requests.size();
    CHECK(api.send_message(imtg::ChatId{1}, "cancel", source.token()).outcome
          == TelegramOutcome::Cancelled);
    CHECK(transport.requests.size() == before_cancel);

    transport.fixtures.push_back(
        {std::nullopt, HttpFailureKind::Cancelled, false});
    CHECK(api.send_message(imtg::ChatId{1}, "cancelled").outcome
          == TelegramOutcome::Cancelled);

    transport.fixtures.push_back(
        {std::nullopt, HttpFailureKind::ResponseTooLarge, true});
    CHECK(api.send_message(imtg::ChatId{1}, "large").outcome
          == TelegramOutcome::UnknownOutcome);
    transport.fixtures.push_back(response(200, std::string(513, 'x')));
    CHECK(api.send_message(imtg::ChatId{1}, "large-response").outcome
          == TelegramOutcome::UnknownOutcome);

    const auto before_invalid = transport.requests.size();
    CHECK(api.send_message(imtg::ChatId{1}, std::string(65, 'x')).outcome
          == TelegramOutcome::InvalidRequest);
    CHECK(api.answer_callback_query("").outcome == TelegramOutcome::InvalidRequest);
    CHECK(transport.requests.size() == before_invalid);

    // A status alone, malformed JSON, or conflicting evidence never confirms delivery.
    for (const auto& body : std::vector<std::string>{
             "", "not-json", "{\"ok\":true", "{}", "[]", "{\"ok\":1,\"result\":true}",
             "{\"ok\":true}", "{\"ok\":true,\"result\":true}",
             "{\"ok\":true,\"result\":{\"message_id\":91,\"chat\":{\"id\":-101}}}",
             "{\"ok\":true,\"result\":{\"message_id\":0,\"chat\":{\"id\":-100}}}",
             "{\"ok\":true,\"result\":{\"message_id\":1.0,\"chat\":{\"id\":-100}}}",
             "{\"ok\":true,\"result\":{\"message_id\":18446744073709551615,\"chat\":{\"id\":-100}}}",
             "{\"ok\":true,\"ok\":false,\"result\":true}",
             "{\"ok\":true,\"result\":{\"message_id\":91,\"chat\":{\"id\":-100}},\"error_code\":400}",
             "{\"ok\":false,\"error_code\":400,\"description\":\"bad\",\"result\":true}",
             "{\"ok\":false,\"error_code\":400}",
             "{\"ok\":false,\"error_code\":\"400\",\"description\":\"bad\"}"}) {
        transport.fixtures.push_back(response(200, body));
        const auto malformed = api.send_message(imtg::ChatId{-100}, "test");
        CHECK(malformed.outcome == TelegramOutcome::UnknownOutcome);
        CHECK(!malformed.message_id.has_value());
    }
    transport.fixtures.push_back(response(200, error_response(400)));
    const auto api_error = api.send_message(imtg::ChatId{1}, "not-ok");
    CHECK(api_error.outcome == TelegramOutcome::ClientError);
    CHECK(api_error.api_error_code == std::optional<std::uint32_t>(400));

    transport.fixtures.push_back(response(400, message_response(-100, 91)));
    CHECK(api.send_message(imtg::ChatId{-100}, "contradiction").outcome
          == TelegramOutcome::UnknownOutcome);
    transport.fixtures.push_back(response(401, error_response(400)));
    CHECK(api.send_message(imtg::ChatId{-100}, "contradiction").outcome
          == TelegramOutcome::UnknownOutcome);
    transport.fixtures.push_back(response(200, message_response(4, 18)));
    CHECK(api.edit_message_text(imtg::ChatId{4}, 17, "wrong-id").outcome
          == TelegramOutcome::UnknownOutcome);
    for (const auto& body : {"{\"ok\":true,\"result\":false}",
                             "{\"ok\":true,\"result\":{}}"}) {
        transport.fixtures.push_back(response(200, body));
        CHECK(api.answer_callback_query("callback").outcome == TelegramOutcome::UnknownOutcome);
    }

    transport.fixtures.push_back(response(429, error_response(429, ",\"parameters\":{\"retry_after\":13}")));
    CHECK(api.send_message(imtg::ChatId{1}, "body-delay").retry_after_seconds
          == std::optional<std::uint32_t>(13));
    auto conflicting_delay = response(429,
        error_response(429, ",\"parameters\":{\"retry_after\":13}"));
    conflicting_delay.response->headers.emplace_back("Retry-After", "7");
    transport.fixtures.push_back(std::move(conflicting_delay));
    CHECK(api.send_message(imtg::ChatId{1}, "conflicting-delay").outcome
          == TelegramOutcome::UnknownOutcome);
    transport.fixtures.push_back(response(429,
        error_response(429, ",\"parameters\":{\"retry_after\":-1}")));
    CHECK(api.send_message(imtg::ChatId{1}, "invalid-delay").outcome
          == TelegramOutcome::UnknownOutcome);

    auto contradictory_exchange = response(200, message_response(1, 1));
    contradictory_exchange.failure = HttpFailureKind::Timeout;
    transport.fixtures.push_back(std::move(contradictory_exchange));
    CHECK(api.send_message(imtg::ChatId{1}, "response-and-failure").outcome
          == TelegramOutcome::UnknownOutcome);
    transport.fixtures.push_back({});
    CHECK(api.send_message(imtg::ChatId{1}, "absent-evidence").outcome
          == TelegramOutcome::UnknownOutcome);
    transport.fixtures.push_back({std::nullopt, HttpFailureKind::Network});
    const auto before_lost_send = transport.requests.size();
    CHECK(api.send_message(imtg::ChatId{1}, "lost-response").outcome
          == TelegramOutcome::UnknownOutcome);
    CHECK(transport.requests.size() == before_lost_send + 1);

    const auto before_bad_token = transport.requests.size();
    for (const auto& token : {"", "fixture-token", "0:secret", "123:", "123:has/slash",
                              "123:has?query", "123:has\r\nheader"}) {
        TelegramBotApi invalid_api(token, transport, config);
        CHECK(invalid_api.send_message(imtg::ChatId{1}, "test").outcome
              == TelegramOutcome::InvalidRequest);
    }
    CHECK(api.send_message(imtg::ChatId{}, "test").outcome == TelegramOutcome::InvalidRequest);
    CHECK(api.send_message(imtg::ChatId{1}, "").outcome == TelegramOutcome::InvalidRequest);
    CHECK(api.edit_message_text(imtg::ChatId{1}, 0, "test").outcome == TelegramOutcome::InvalidRequest);
    CHECK(transport.requests.size() == before_bad_token);

    TelegramBotApiConfig tiny_config = config;
    tiny_config.max_request_bytes = 20;
    TelegramBotApi tiny("123:fixture_token", transport, tiny_config);
    CHECK(tiny.send_message(imtg::ChatId{1}, "&&&&").outcome == TelegramOutcome::InvalidRequest);
    CHECK(transport.requests.size() == before_bad_token);

    // The JSON library owns syntax; imtg adds a strict nesting and duplicate-key boundary.
    auto deeply_nested = message_response(1, 1);
    deeply_nested.pop_back();
    deeply_nested += ",\"extra\":" + std::string(40, '[') + "0" + std::string(40, ']') + "}";
    transport.fixtures.push_back(response(200, deeply_nested));
    CHECK(api.send_message(imtg::ChatId{1}, "deep-response").outcome
          == TelegramOutcome::UnknownOutcome);
    transport.fixtures.push_back(response(200,
        "{\"ok\":true,\"result\":{\"message_id\":1,\"chat\":{\"id\":2,\"id\":1}}}"));
    CHECK(api.send_message(imtg::ChatId{1}, "duplicate-key").outcome
          == TelegramOutcome::UnknownOutcome);
    auto impossible_exchange = response(200, message_response(1, 1));
    impossible_exchange.request_may_have_been_sent = false;
    transport.fixtures.push_back(std::move(impossible_exchange));
    CHECK(api.send_message(imtg::ChatId{1}, "response-without-send").outcome
          == TelegramOutcome::UnknownOutcome);
    return 0;
}

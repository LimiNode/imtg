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
    int status, std::string body = "{\"ok\":true}")
{
    return {imtg::telegram_transport::HttpResponse{status, {}, std::move(body)},
            std::nullopt, true};
}

} // namespace

int main()
{
    using namespace imtg::telegram_transport;

    FixtureTransport transport;
    TelegramBotApiConfig config;
    config.max_request_bytes = 1024;
    config.max_response_bytes = 32;
    config.max_text_bytes = 64;
    config.timeout = std::chrono::milliseconds(1250);
    TelegramBotApi api("fixture-token", transport, config);

    transport.fixtures.push_back(response(200));
    const auto sent = api.send_message(imtg::ChatId{-100}, "hello & world");
    CHECK(sent.succeeded());
    CHECK(transport.requests.size() == 1);
    CHECK(transport.requests[0].url
          == "https://api.telegram.org/botfixture-token/sendMessage");
    CHECK(transport.requests[0].body == "chat_id=-100&text=hello%20%26%20world");
    CHECK(transport.requests[0].headers.size() == 1);
    CHECK(transport.requests[0].headers[0].first == "Content-Type");
    CHECK(transport.requests[0].headers[0].second
          == "application/x-www-form-urlencoded");
    CHECK(transport.requests[0].timeout == std::chrono::milliseconds(1250));
    CHECK(transport.requests[0].max_response_bytes == 32);

    transport.fixtures.push_back(response(200));
    const auto edited = api.edit_message_text(imtg::ChatId{4}, 17, "edited");
    CHECK(edited.succeeded());
    CHECK(transport.requests.back().url
          == "https://api.telegram.org/botfixture-token/editMessageText");
    CHECK(transport.requests.back().body == "chat_id=4&message_id=17&text=edited");

    transport.fixtures.push_back(response(200));
    const auto answered = api.answer_callback_query("callback/1");
    CHECK(answered.succeeded());
    CHECK(transport.requests.back().url
          == "https://api.telegram.org/botfixture-token/answerCallbackQuery");
    CHECK(transport.requests.back().body == "callback_query_id=callback%2F1");

    auto limited = response(429);
    limited.response->headers.emplace_back("rEtRy-AfTeR", "7");
    transport.fixtures.push_back(std::move(limited));
    const auto rate_limited = api.send_message(imtg::ChatId{1}, "slow");
    CHECK(rate_limited.outcome == TelegramOutcome::RateLimited);
    CHECK(rate_limited.retry_after_seconds.has_value());
    CHECK(*rate_limited.retry_after_seconds == 7U);

    transport.fixtures.push_back(response(400));
    CHECK(api.send_message(imtg::ChatId{1}, "bad").outcome
          == TelegramOutcome::ClientError);
    transport.fixtures.push_back(response(503));
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
    transport.fixtures.push_back(response(200, std::string(33, 'x')));
    CHECK(api.send_message(imtg::ChatId{1}, "large-response").outcome
          == TelegramOutcome::UnknownOutcome);

    const auto before_invalid = transport.requests.size();
    CHECK(api.send_message(imtg::ChatId{1}, std::string(65, 'x')).outcome
          == TelegramOutcome::InvalidRequest);
    CHECK(api.answer_callback_query("").outcome == TelegramOutcome::InvalidRequest);
    CHECK(transport.requests.size() == before_invalid);
    return 0;
}

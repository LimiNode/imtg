#include <imtg/telegram_transport.hpp>

#include <string>

class FixtureTransport final : public imtg::telegram_transport::IHttpTransport {
public:
    unsigned int calls = 0;
    bool valid_request = false;

    imtg::telegram_transport::HttpExchange execute(
        const imtg::telegram_transport::HttpRequest& request,
        const imtg::telegram_transport::CancellationToken&) noexcept override
    {
        ++calls;
        valid_request = request.method == "POST"
            && request.url == "https://api.telegram.org/bot123:fixture_secret/sendMessage"
            && request.body == "chat_id=-100&text=installed%20consumer"
            && request.max_response_bytes != 0;
        return {imtg::telegram_transport::HttpResponse{
                    200, {}, "{\"ok\":true,\"result\":{\"message_id\":17,\"chat\":{\"id\":-100}}}"},
                std::nullopt, true};
    }
};

int main()
{
    FixtureTransport transport;
    imtg::telegram_transport::TelegramBotApi api("123:fixture_secret", transport);
    const auto sent = api.send_message(imtg::ChatId{-100}, "installed consumer");
    return transport.calls == 1 && transport.valid_request && sent.succeeded()
            && sent.message_id.has_value() && sent.message_id->value == 17
            && sent.message_id->chat_id.value == -100
        ? 0 : 1;
}

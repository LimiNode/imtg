#include <imtg/telegram_transport/KurlykHttpTransport.hpp>

int main()
{
    imtg::telegram_transport::KurlykHttpTransport transport;
    imtg::telegram_transport::HttpRequest request;
    request.max_response_bytes = 0;
    const auto exchange = transport.execute(request, {});
    return exchange.failure && !exchange.response && !exchange.request_may_have_been_sent ? 0 : 1;
}

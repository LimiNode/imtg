#include <imtg/telegram_transport/KurlykHttpTransport.hpp>

#include "test_support.hpp"

#include <kurlyk.hpp>
#include <server_http.hpp>

#include <atomic>
#include <future>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {

using Server = SimpleWeb::Server<SimpleWeb::HTTP>;
using imtg::telegram_transport::CancellationSource;
using imtg::telegram_transport::HttpFailureKind;
using imtg::telegram_transport::HttpRequest;
using imtg::telegram_transport::KurlykHttpTransport;
using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;

class Loopback {
public:
    Server server;
    std::string host;

    Loopback()
    {
        server.config.address = "127.0.0.1";
        server.config.port = 0;
        server.config.thread_pool_size = 2;
    }

    void start()
    {
        std::promise<unsigned short> ready;
        auto future = ready.get_future();
        m_thread = std::thread([this, &ready] {
            server.start([&ready](unsigned short port) { ready.set_value(port); });
        });
        CHECK(future.wait_for(5s) == std::future_status::ready);
        host = "http://127.0.0.1:" + std::to_string(future.get());
    }

    ~Loopback()
    {
        server.stop();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

private:
    std::thread m_thread;
};

HttpRequest request_for(const Loopback& loopback, const std::string& path)
{
    HttpRequest request;
    request.url = loopback.host + path;
    request.method = "POST";
    request.body = "a=hello%20world";
    request.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
    request.max_response_bytes = 8;
    request.timeout = 2s;
    return request;
}

void worker_barrier()
{
    auto completion = std::make_shared<std::promise<void>>();
    auto done = completion->get_future();
    kurlyk::core::NetworkWorker::get_instance().add_task([completion] { completion->set_value(); });
    CHECK(done.wait_for(5s) == std::future_status::ready);
}

} // namespace

int main()
{
    auto startup = std::make_shared<std::promise<void>>();
    auto started = startup->get_future();
    kurlyk::core::NetworkWorker::get_instance().add_task([startup] { startup->set_value(); });
    CHECK(started.wait_for(30ms) == std::future_status::timeout);

    bool rejected = false;
    try {
        KurlykHttpTransport invalid(0);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);

    const auto largest_body_bound = static_cast<std::size_t>((std::numeric_limits<long>::max)());
    KurlykHttpTransport largest_valid(largest_body_bound);
    if (largest_body_bound < (std::numeric_limits<std::size_t>::max)()) {
        rejected = false;
        try {
            KurlykHttpTransport invalid(largest_body_bound + 1);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        CHECK(rejected);
    }

    Loopback loopback;
    std::atomic<unsigned int> hits{0};
    std::atomic<unsigned int> redirect_hits{0};
    loopback.server.resource["^/exact$"]["POST"] = [&hits](auto response, auto request) {
        ++hits;
        CHECK(request->content.string() == "a=hello%20world");
        CHECK(request->header.find("Content-Type") != request->header.end());
        response->close_connection_after_response = true;
        *response << "HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\nabcdefgh";
    };
    loopback.server.resource["^/large$"]["POST"] = [&hits](auto response, auto) {
        ++hits;
        response->close_connection_after_response = true;
        *response << "HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nabcdefghij";
    };
    loopback.server.resource["^/chunked$"]["POST"] = [&hits](auto response, auto) {
        ++hits;
        response->close_connection_after_response = true;
        *response << "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                     "3\r\nabc\r\n7\r\ndefghij\r\n0\r\n\r\n";
    };
    loopback.server.resource["^/unknown-length$"]["POST"] = [&hits](auto response, auto) {
        ++hits;
        response->close_connection_after_response = true;
        *response << "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nabcdefghij";
    };
    loopback.server.resource["^/rate-limit$"]["POST"] = [](auto response, auto) {
        response->close_connection_after_response = true;
        *response << "HTTP/1.1 429 Too Many Requests\r\nContent-Length: 2\r\n"
                     "Retry-After: 3\r\nConnection: close\r\n\r\n{}";
    };
    loopback.server.resource["^/redirect$"]["POST"] = [&loopback](auto response, auto) {
        response->close_connection_after_response = true;
        *response << "HTTP/1.1 302 Found\r\nContent-Length: 2\r\nLocation: "
                  << loopback.host << "/redirect-target\r\nConnection: close\r\n\r\ngo";
    };
    loopback.server.resource["^/redirect-target$"]["POST"] = [&redirect_hits](auto response, auto) {
        ++redirect_hits;
        response->write("unexpected");
    };
    loopback.server.resource["^/lost$"]["POST"] = [&hits](auto response, auto) {
        ++hits;
        response->close_connection_after_response = true;
        *response << "HTTP/1.1 200 OK\r\nContent-Length: 7\r\nConnection: close\r\n\r\nabc";
    };
    std::mutex held_mutex;
    std::vector<std::shared_ptr<Server::Response>> held;
    std::promise<void> timeout_seen;
    std::promise<void> cancel_seen;
    auto timeout_ready = timeout_seen.get_future();
    auto cancel_ready = cancel_seen.get_future();
    loopback.server.resource["^/timeout$"]["POST"] = [&held_mutex, &held, &timeout_seen](auto response, auto) {
        std::lock_guard<std::mutex> lock(held_mutex);
        held.push_back(std::move(response));
        timeout_seen.set_value();
    };
    loopback.server.resource["^/cancel$"]["POST"] = [&held_mutex, &held, &cancel_seen](auto response, auto) {
        std::lock_guard<std::mutex> lock(held_mutex);
        held.push_back(std::move(response));
        cancel_seen.set_value();
    };
    loopback.start();
    kurlyk::init(true);
    CHECK(started.wait_for(5s) == std::future_status::ready);

    KurlykHttpTransport transport;
    auto request = request_for(loopback, "/exact");
    auto exchange = transport.execute(request, {});
    CHECK(exchange.response && !exchange.failure);
    CHECK(exchange.response->status_code == 200);
    CHECK(exchange.response->body == "abcdefgh");

    for (const std::string path : {"/large", "/chunked", "/unknown-length"}) {
        request = request_for(loopback, path);
        const auto before = hits.load();
        exchange = transport.execute(request, {});
        CHECK(exchange.failure == HttpFailureKind::ResponseTooLarge);
        CHECK(!exchange.response && exchange.request_may_have_been_sent);
        CHECK(hits.load() == before + 1);
        request.max_response_bytes = 10;
        exchange = transport.execute(request, {});
        CHECK(exchange.response && exchange.response->body == "abcdefghij");
    }

    request = request_for(loopback, "/rate-limit");
    exchange = transport.execute(request, {});
    CHECK(exchange.response && !exchange.failure);
    CHECK(exchange.response->status_code == 429);
    CHECK(exchange.response->body == "{}");
    bool retry_after = false;
    for (const auto& header : exchange.response->headers) {
        if (header.first == "retry-after" || header.first == "Retry-After") {
            retry_after = header.second == "3";
        }
    }
    CHECK(retry_after);

    request = request_for(loopback, "/redirect");
    exchange = transport.execute(request, {});
    CHECK(exchange.response && exchange.response->status_code == 302);
    CHECK(redirect_hits.load() == 0);

    request = request_for(loopback, "/lost");
    const auto before_lost = hits.load();
    exchange = transport.execute(request, {});
    CHECK(exchange.failure == HttpFailureKind::Network);
    CHECK(!exchange.response && exchange.request_may_have_been_sent);
    CHECK(hits.load() == before_lost + 1);

    const auto before_invalid = hits.load();
    request = request_for(loopback, "/exact");
    CancellationSource pre_cancelled;
    pre_cancelled.cancel();
    exchange = transport.execute(request, pre_cancelled.token());
    CHECK(exchange.failure == HttpFailureKind::Cancelled && !exchange.request_may_have_been_sent);
    request.max_response_bytes = 0;
    exchange = transport.execute(request, {});
    CHECK(exchange.failure && !exchange.request_may_have_been_sent);
    request = request_for(loopback, "/exact");
    request.timeout = 0ms;
    exchange = transport.execute(request, {});
    CHECK(exchange.failure == HttpFailureKind::Timeout && !exchange.request_may_have_been_sent);
    request = request_for(loopback, "/exact");
    request.headers.emplace_back("bad\r\nInjected", "header");
    exchange = transport.execute(request, {});
    CHECK(exchange.failure && !exchange.request_may_have_been_sent);
    request = request_for(loopback, "/exact");
    KurlykHttpTransport tiny(1);
    exchange = tiny.execute(request, {});
    CHECK(exchange.failure == HttpFailureKind::RequestTooLarge && !exchange.request_may_have_been_sent);
    CHECK(hits.load() == before_invalid);

    auto timeout = std::async(std::launch::async, [&loopback] {
        auto short_request = request_for(loopback, "/timeout");
        short_request.timeout = 150ms;
        KurlykHttpTransport temporary;
        return temporary.execute(short_request, {});
    });
    CHECK(timeout_ready.wait_for(5s) == std::future_status::ready);
    CHECK(timeout.wait_for(5s) == std::future_status::ready);
    exchange = timeout.get();
    CHECK(exchange.failure == HttpFailureKind::Timeout && exchange.request_may_have_been_sent);
    worker_barrier();

    CancellationSource cancellation;
    auto cancelled = std::async(std::launch::async, [&loopback, token = cancellation.token()] {
        auto cancel_request = request_for(loopback, "/cancel");
        KurlykHttpTransport temporary;
        return temporary.execute(cancel_request, token);
    });
    CHECK(cancel_ready.wait_for(5s) == std::future_status::ready);
    request = request_for(loopback, "/exact");
    exchange = transport.execute(request, {});
    CHECK(exchange.response && exchange.response->body == "abcdefgh");
    cancellation.cancel();
    CHECK(cancelled.wait_for(5s) == std::future_status::ready);
    exchange = cancelled.get();
    CHECK(exchange.failure == HttpFailureKind::Cancelled && exchange.request_may_have_been_sent);
    worker_barrier();
    {
        std::lock_guard<std::mutex> lock(held_mutex);
        held.clear();
    }
    exchange = transport.execute(request, {});
    CHECK(exchange.response && exchange.response->body == "abcdefgh");
    kurlyk::deinit();
    exchange = transport.execute(request, {});
    CHECK(exchange.failure == HttpFailureKind::Network);
    CHECK(!exchange.response && !exchange.request_may_have_been_sent);
}

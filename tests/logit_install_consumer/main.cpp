#include <imtg/logging.hpp>
#include <logit.hpp>

#include <string>

int main()
{
    imtg::logging::LogitAdapter adapter;
    const auto sink = adapter.runtime_error_sink();
    if (!sink || std::string(imtg::logging::log_event_code(
            imtg::logging::LogEvent::RuntimeShutdown)) != "runtime.shutdown") {
        return 1;
    }
    return 0;
}

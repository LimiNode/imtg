#include <imtg/logging.hpp>
#include <logit.hpp>

#include <string>

int main()
{
    LOGIT_ADD_MEMORY_LOGGER(4, 2048, 0);
    imtg::logging::LogitAdapter adapter;
    const auto sink = adapter.runtime_error_sink();
    if (!sink || std::string(imtg::logging::log_event_code(
            imtg::logging::LogEvent::RuntimeShutdown)) != "runtime.shutdown") {
        return 1;
    }
    adapter.record_runtime_shutdown(imtg::Result<void>());
    const auto messages = LOGIT_GET_BUFFERED_STRINGS(0);
    if (messages.size() != 1
        || messages[0].find("runtime.shutdown") == std::string::npos) {
        return 2;
    }
    return 0;
}

#ifndef IMTG_ACTIONS_PAYLOAD_CODEC_HPP
#define IMTG_ACTIONS_PAYLOAD_CODEC_HPP

#include <imtg/core/result.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace imtg {

template <typename Payload>
/// \brief Versioned application payload codec for one typed action route.
struct PayloadCodec {
    std::uint32_t version{}; ///< Persisted payload schema version.
    std::function<Result<std::string>(const Payload&)> encode; ///< Typed-to-bytes encoder.
    std::function<Result<Payload>(std::string_view)> decode; ///< Untrusted bytes decoder.
};

} // namespace imtg

#endif // IMTG_ACTIONS_PAYLOAD_CODEC_HPP

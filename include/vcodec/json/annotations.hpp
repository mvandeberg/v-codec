// JSON-namespace annotations (spec §5.4). These lower data-model elements JSON cannot
// represent natively; they have no meaning to core/ and none to other formats.
#ifndef VCODEC_JSON_ANNOTATIONS_HPP
#define VCODEC_JSON_ANNOTATIONS_HPP

#include <cstdint>

namespace vcodec::json {

enum class bytes_encoding : std::uint8_t { base64, base64url, hex };
inline constexpr bytes_encoding base64    = bytes_encoding::base64;
inline constexpr bytes_encoding base64url = bytes_encoding::base64url;
inline constexpr bytes_encoding hex       = bytes_encoding::hex;

// Required lowering for byte-like members. Also promotes a std::vector<std::uint8_t> from
// "array of numbers" to "bytes".
struct bytes_t { bytes_encoding encoding; };
consteval bytes_t bytes(bytes_encoding e) { return { e }; }

// Emit a 64-bit integer as a JSON string; accept either on decode.
struct as_string_t {};      inline constexpr as_string_t as_string{};

// Permit integral map keys, rendered as decimal strings.
struct stringify_keys_t {}; inline constexpr stringify_keys_t stringify_keys{};

// The format tag used for codec_for<T, json::format> and format_traits<json::format>.
struct format {};

} // namespace vcodec::json

#endif // VCODEC_JSON_ANNOTATIONS_HPP

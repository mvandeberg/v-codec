// Forward declarations.
#ifndef VCODEC_FWD_HPP
#define VCODEC_FWD_HPP

#include <cstdint>

namespace vcodec {

enum class errc : std::uint16_t;
class error;
class encode_error;
struct path_step;
struct line_col;
template<class T> struct collected;

template<class T, class Format> struct codec_for;
template<class T> struct describe;

namespace core {
enum class kind : std::uint8_t;
struct static_string;
struct field_info;
struct field_meta;
struct type_info;
}

namespace json {
struct format;
struct options;
}

} // namespace vcodec

#endif // VCODEC_FWD_HPP

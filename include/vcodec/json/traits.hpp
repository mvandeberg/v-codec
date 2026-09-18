// format_traits<json::format>: how JSON classifies and lowers model elements it cannot
// represent natively (spec §4.1).
#ifndef VCODEC_JSON_TRAITS_HPP
#define VCODEC_JSON_TRAITS_HPP

#include <vcodec/core/model.hpp>
#include <vcodec/json/annotations.hpp>

namespace vcodec::core {

template<>
struct format_traits<json::format> : format_traits_defaults {
    static constexpr std::string_view name = "JSON";

    // std::byte ranges are bytes; std::uint8_t ranges are bytes only with json::bytes(...).
    template<field_meta F, class T>
    static consteval bool is_bytes() {
        return byte_like<T> || (contiguous_of_uint8<T> && has_annotation<json::bytes_t>(F.anns()));
    }
    // Bytes need an explicit lowering — never a silent guess.
    template<field_meta F, class T>
    static consteval bool can_lower_bytes() { return has_annotation<json::bytes_t>(F.anns()); }

    // Non-text keys need json::stringify_keys.
    template<field_meta F, class K>
    static consteval bool can_lower_key() {
        return string_like<K> || has_annotation<json::stringify_keys_t>(F.anns());
    }

    static constexpr std::string_view bytes_hint = "Add [[=vcodec::json::bytes(vcodec::json::base64)]]";
    static constexpr std::string_view key_hint   = "Add [[=vcodec::json::stringify_keys]]";
};

} // namespace vcodec::core

#endif // VCODEC_JSON_TRAITS_HPP

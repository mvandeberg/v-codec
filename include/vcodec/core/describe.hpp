// Out-of-line customisation for a struct you do not own (spec §6.3):
//
//   template<> struct vcodec::describe<third_party::Point> {
//       static constexpr auto annotations = vcodec::type_annotations(vcodec::deny_unknown_fields);
//       static constexpr auto members = vcodec::members(
//           vcodec::member("x", vcodec::name("X")),
//           vcodec::member("y", vcodec::skip_if_default));
//   };
//
// Members are still enumerated by reflection; describe<T> supplies the annotations a source
// annotation would have. A described identifier that does not exist is a compile error.
#ifndef VCODEC_CORE_DESCRIBE_HPP
#define VCODEC_CORE_DESCRIBE_HPP

#include <vcodec/core/fixed_string.hpp>

#include <meta>
#include <array>
#include <string_view>

namespace vcodec {

template<class T>
struct describe;    // primary deliberately left undefined

namespace core {

inline constexpr std::size_t max_described_annotations = 8;

// A consteval-only record: identifier plus reflections of annotation values.
struct member_desc {
    static_string   identifier;
    std::meta::info annotations[max_described_annotations]{};
    std::size_t     count = 0;
};

struct type_desc {
    std::meta::info annotations[max_described_annotations]{};
    std::size_t     count = 0;
};

template<class T>
concept has_describe = requires { sizeof(describe<std::remove_cvref_t<T>>); };

template<class T>
concept describe_has_members = has_describe<T> && requires { describe<std::remove_cvref_t<T>>::members; };

template<class T>
concept describe_has_annotations = has_describe<T> && requires { describe<std::remove_cvref_t<T>>::annotations; };

} // namespace core

template<class... A>
consteval core::member_desc member(std::string_view identifier, A const&... anns) {
    static_assert(sizeof...(A) <= core::max_described_annotations,
                  "vcodec::member: at most 8 annotations per described member");
    core::member_desc d{ core::intern(identifier) };
    ((d.annotations[d.count++] = std::meta::reflect_constant(anns)), ...);
    return d;
}

template<class... M>
consteval std::array<core::member_desc, sizeof...(M)> members(M const&... ms) {
    return { ms... };
}

template<class... A>
consteval core::type_desc type_annotations(A const&... anns) {
    static_assert(sizeof...(A) <= core::max_described_annotations,
                  "vcodec::type_annotations: at most 8 annotations per described type");
    core::type_desc d{};
    ((d.annotations[d.count++] = std::meta::reflect_constant(anns)), ...);
    return d;
}

} // namespace vcodec

#endif // VCODEC_CORE_DESCRIBE_HPP

// Type classification (spec §7). Concepts, not overloads: user containers that satisfy
// the shape work with no library change. Classification is first-match in the order the
// traversal consults them; see traverse.hpp.
#ifndef VCODEC_CORE_CONCEPTS_HPP
#define VCODEC_CORE_CONCEPTS_HPP

#include <vcodec/core/compiler.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace vcodec::core {

template<class T> using bare = std::remove_cvref_t<T>;

// ---- scalars ----------------------------------------------------------------------------

template<class T> concept boolean_type  = std::same_as<bare<T>, bool>;
template<class T> concept char_type     = std::same_as<bare<T>, char>;      // one-char string
template<class T> concept byte_type     = std::same_as<bare<T>, std::byte>;

// wchar_t, char8_t, char16_t and char32_t are code units, not numbers; they have no codec
// (§7.1 lists only char), so a member of those types is rejected at compile time instead of
// being encoded as an integer.
template<class T> concept wide_char_type =
    std::same_as<bare<T>, wchar_t> || std::same_as<bare<T>, char8_t>
    || std::same_as<bare<T>, char16_t> || std::same_as<bare<T>, char32_t>;

template<class T> concept unsigned_integral_type =
    std::unsigned_integral<bare<T>> && !boolean_type<T> && !char_type<T> && !wide_char_type<T>;
template<class T> concept signed_integral_type =
    std::signed_integral<bare<T>> && !char_type<T> && !wide_char_type<T>;
template<class T> concept integral_type = unsigned_integral_type<T> || signed_integral_type<T>;
template<class T> concept floating_type = std::floating_point<bare<T>>;
template<class T> concept enum_type     = std::is_enum_v<bare<T>>;

// ---- strings ----------------------------------------------------------------------------

template<class T> concept owned_string =
    std::same_as<bare<T>, std::string> || std::same_as<bare<T>, std::u8string>;
template<class T> concept string_view_type =
    std::same_as<bare<T>, std::string_view> || std::same_as<bare<T>, std::u8string_view>;
template<class T> concept c_string = std::same_as<bare<T>, const char*> || std::same_as<bare<T>, char*>;

template<class T> concept string_like = owned_string<T> || string_view_type<T> || c_string<T>;

// Wide strings are neither strings (no defined transcoding in v0.1) nor sequences of numbers.
template<class T> concept wide_string =
    std::same_as<bare<T>, std::wstring> || std::same_as<bare<T>, std::u16string> || std::same_as<bare<T>, std::u32string>
    || std::same_as<bare<T>, std::wstring_view> || std::same_as<bare<T>, std::u16string_view> || std::same_as<bare<T>, std::u32string_view>;
template<class T> concept decodable_string = owned_string<T> || string_view_type<T>;

// ---- ranges -----------------------------------------------------------------------------

template<class R> using range_value = std::ranges::range_value_t<bare<R>>;

template<class T> concept contiguous_of_bytes =
    std::ranges::contiguous_range<bare<T>> && byte_type<range_value<T>>;

// std::vector<std::uint8_t> and friends: an array of numbers unless a format annotation says
// bytes (spec §7.2). The promotion decision is per field, in the format's format_traits.
template<class T> concept contiguous_of_uint8 =
    std::ranges::contiguous_range<bare<T>>
    && (std::same_as<range_value<T>, std::uint8_t> || std::same_as<range_value<T>, unsigned char>);

template<class T> concept byte_like = contiguous_of_bytes<T>;

template<class T> struct is_std_array : std::false_type {};
template<class E, std::size_t N> struct is_std_array<std::array<E, N>> : std::true_type {};
template<class T> concept std_array = is_std_array<bare<T>>::value;
template<class T> concept c_array   = std::is_bounded_array_v<bare<T>>;
template<class T> concept fixed_array = std_array<T> || c_array<T>;

template<class T> struct is_span : std::false_type {};
template<class E, std::size_t N> struct is_span<std::span<E, N>> : std::true_type {};
template<class T> concept span_like = is_span<bare<T>>::value;

// ---- optional-like: value or null -------------------------------------------------------

template<class T> struct is_optional : std::false_type {};
template<class V> struct is_optional<std::optional<V>> : std::true_type {};
// Only the default deleter: construction on decode uses std::make_unique.
template<class T> struct is_unique_ptr : std::false_type {};
template<class V> struct is_unique_ptr<std::unique_ptr<V>> : std::true_type {};
template<class T> struct is_shared_ptr : std::false_type {};
template<class V> struct is_shared_ptr<std::shared_ptr<V>> : std::true_type {};

template<class T> concept optional_like =
    is_optional<bare<T>>::value || is_unique_ptr<bare<T>>::value || is_shared_ptr<bare<T>>::value;

template<class T> struct optional_value;
template<class V> struct optional_value<std::optional<V>> { using type = V; };
template<class V> struct optional_value<std::unique_ptr<V>> { using type = V; };
template<class V> struct optional_value<std::shared_ptr<V>> { using type = V; };
template<class T> using optional_value_t = typename optional_value<bare<T>>::type;

// ---- sums and products ------------------------------------------------------------------

template<class T> struct is_variant : std::false_type {};
template<class... A> struct is_variant<std::variant<A...>> : std::true_type {};
template<class T> concept variant_like = is_variant<bare<T>>::value;

template<class T> struct is_pair : std::false_type {};
template<class A, class B> struct is_pair<std::pair<A, B>> : std::true_type {};
template<class T> struct is_tuple : std::false_type {};
template<class... A> struct is_tuple<std::tuple<A...>> : std::true_type {};
template<class T> concept tuple_like = is_pair<bare<T>>::value || is_tuple<bare<T>>::value;

// ---- maps: a range of pairs whose key is text or integral --------------------------------

template<class T> concept pair_like = requires { typename std::tuple_size<bare<T>>::type; }
    && std::tuple_size_v<bare<T>> == 2 && !std_array<T>;

template<class K> concept map_key = string_like<K> || integral_type<K>;

template<class T> concept map_like =
    std::ranges::input_range<bare<T>> && !string_like<T>
    && pair_like<range_value<T>>
    && map_key<std::tuple_element_t<0, range_value<T>>>;

template<class M> using map_key_t    = bare<std::tuple_element_t<0, range_value<M>>>;
template<class M> using map_mapped_t = bare<std::tuple_element_t<1, range_value<M>>>;

// ---- sequences --------------------------------------------------------------------------

template<class T> concept sequence =
    std::ranges::input_range<bare<T>> && !string_like<T> && !wide_string<T> && !byte_like<T>
    && !map_like<T> && !fixed_array<T> && !span_like<T>;

// Anything we can append to on decode: push_back, emplace_back, insert(end, v) or insert(v).
template<class R>
concept appendable_range = sequence<R> && requires(bare<R>& r, range_value<R>&& v) {
    requires std::default_initializable<range_value<R>>;
    requires requires { r.push_back(std::move(v)); }
          || requires { r.emplace_back(std::move(v)); }
          || requires { r.insert(r.end(), std::move(v)); }
          || requires { r.insert(std::move(v)); };
};

template<class R, class V = range_value<R>>
constexpr void append_to(R& r, V&& v) {
    if constexpr (requires { r.push_back(std::move(v)); })          r.push_back(std::move(v));
    else if constexpr (requires { r.emplace_back(std::move(v)); })  r.emplace_back(std::move(v));
    else if constexpr (requires { r.insert(r.end(), std::move(v)); }) r.insert(r.end(), std::move(v));
    else                                                            r.insert(std::move(v));
}

// Map insertion: emplace(k, v), insert(pair), or push_back(pair).
template<class M>
concept insertable_map = map_like<M> && requires(bare<M>& m, bare<std::tuple_element_t<0, range_value<M>>>&& k,
                                                 bare<std::tuple_element_t<1, range_value<M>>>&& v) {
    requires requires { m.emplace(std::move(k), std::move(v)); }
          || requires { m.insert(std::pair{std::move(k), std::move(v)}); }
          || requires { m.push_back(std::pair{std::move(k), std::move(v)}); }
          || requires { m.emplace_back(std::move(k), std::move(v)); };
};

template<class M, class K, class V>
constexpr void insert_into(M& m, K&& k, V&& v) {
    if constexpr (requires { m.emplace_back(std::move(k), std::move(v)); }) m.emplace_back(std::move(k), std::move(v));
    else if constexpr (requires { m.push_back(std::pair{std::move(k), std::move(v)}); }) m.push_back(std::pair{std::move(k), std::move(v)});
    else if constexpr (requires { m.emplace(std::move(k), std::move(v)); }) {
        // last_wins semantics for duplicate keys are handled by the caller; emplace is enough
        auto [it, inserted] = m.emplace(std::move(k), std::move(v));
        (void)it; (void)inserted;
    }
    else m.insert(std::pair{std::move(k), std::move(v)});
}

// ---- classes ----------------------------------------------------------------------------

// Anything that is a class and not one of the standard shapes above is traversed by
// reflection (or describe<T>).
template<class T> concept class_type = std::is_class_v<bare<T>> && !std::is_union_v<bare<T>>;

template<class T> concept reflectable_class =
    class_type<T> && !string_like<T> && !wide_string<T> && !optional_like<T> && !variant_like<T> && !tuple_like<T>
    && !byte_like<T> && !contiguous_of_uint8<T> && !map_like<T> && !fixed_array<T> && !span_like<T>
    && !sequence<T> && !std::ranges::input_range<bare<T>>;

// Keyed containers with unique keys (std::map, std::unordered_map): emplace reports success.
template<class M>
concept unique_keyed_map = map_like<M> && requires(bare<M>& m) {
    typename bare<M>::key_type;
    { m.emplace(std::declval<typename bare<M>::key_type>(), std::declval<typename bare<M>::mapped_type>()).second } -> std::convertible_to<bool>;
    { m.find(std::declval<typename bare<M>::key_type>()) } -> std::same_as<typename bare<M>::iterator>;
};

} // namespace vcodec::core

#endif // VCODEC_CORE_CONCEPTS_HPP

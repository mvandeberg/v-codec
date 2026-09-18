// Core annotation vocabulary (spec §5.1–5.3, §5.5) and the consteval helpers that find
// annotations on a reflection. Every payload here is a structural type.
#ifndef VCODEC_CORE_ANNOTATIONS_HPP
#define VCODEC_CORE_ANNOTATIONS_HPP

#include <vcodec/core/fixed_string.hpp>

#include <meta>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace vcodec {

// ---- field level ------------------------------------------------------------------------

struct name_t  { core::static_string value; };
struct alias_t { core::static_string value; };

consteval name_t  name(std::string_view s)  { return name_t{ core::intern(s) }; }
consteval alias_t alias(std::string_view s) { return alias_t{ core::intern(s) }; }

struct skip_t {};                 inline constexpr skip_t               skip{};
struct skip_serializing_t {};     inline constexpr skip_serializing_t   skip_serializing{};
struct skip_deserializing_t {};   inline constexpr skip_deserializing_t skip_deserializing{};
struct skip_if_null_t {};         inline constexpr skip_if_null_t       skip_if_null{};
struct skip_if_default_t {};      inline constexpr skip_if_default_t    skip_if_default{};
struct required_t {};             inline constexpr required_t           required{};
struct optional_t {};             inline constexpr optional_t           optional{};
struct flatten_t {};              inline constexpr flatten_t            flatten{};

// default_value(v): the value used when the key is absent on decode. The payload must be
// structural (§5.1, §5.1.1). Strings are interned into static storage.
template<class V>
struct default_value_t { V value; };

template<class V>
    requires (std::is_arithmetic_v<V> || std::is_enum_v<V>)
consteval default_value_t<V> default_value(V v) { return { v }; }

consteval default_value_t<core::static_string> default_value(std::string_view s) {
    return { core::intern(s) };
}
consteval default_value_t<core::static_string> default_value(const char* s) {
    return { core::intern(std::string_view{s}) };
}

template<class V>
    requires (!std::is_arithmetic_v<V> && !std::is_enum_v<V>
              && !std::convertible_to<V, std::string_view>)
consteval auto default_value(V const&) {
    static_assert(false,
        "vcodec::default_value(v): v must be a structural type (an arithmetic type, an enum, "
        "or a string). For std::string, std::vector and other non-structural defaults, give "
        "the member [[=vcodec::with<YourCodec>]] and supply the default from the codec "
        "(spec §5.1.1).");
    return default_value_t<int>{};
}

// with<Codec>: per-field custom codec.
template<class Codec> struct with_t {};
template<class Codec> inline constexpr with_t<Codec> with{};

// ---- type level -------------------------------------------------------------------------

enum class case_ : std::uint8_t { camel, pascal, snake, kebab, screaming_snake };

struct rename_all_t { case_ style; };
consteval rename_all_t rename_all(case_ c) { return { c }; }

struct deny_unknown_fields_t {};  inline constexpr deny_unknown_fields_t deny_unknown_fields{};
struct transparent_t {};          inline constexpr transparent_t         transparent{};

// tag("s"): discriminator key for a variant member. Allowed on the enclosing struct (applies
// to every variant member) or on the variant member itself (wins over the type-level one).
struct tag_t { core::static_string key; };
consteval tag_t tag(std::string_view s) { return tag_t{ core::intern(s) }; }

// ---- enum level -------------------------------------------------------------------------

struct as_integer_t {};           inline constexpr as_integer_t as_integer{};
// as_text: encode an enum by name even where the format's default is the integer (CBOR).
struct as_text_t {};              inline constexpr as_text_t    as_text{};

// ---- reflection helpers -----------------------------------------------------------------

namespace core {

// The type of an annotation, stripped of the const that annotations_of reports (M0 finding 3).
consteval std::meta::info annotation_type(std::meta::info a) {
    return std::meta::remove_cvref(std::meta::type_of(a));
}

consteval bool is_annotation_of_type(std::meta::info a, std::meta::info type) {
    return annotation_type(a) == type;
}

// True if the annotation is a specialisation of the class template `tmpl`.
consteval bool is_annotation_of_template(std::meta::info a, std::meta::info tmpl) {
    auto t = annotation_type(a);
    return std::meta::has_template_arguments(t) && std::meta::template_of(t) == tmpl;
}

// Search a range of annotation reflections.
template<class A, class Range>
consteval bool has_annotation(Range const& anns) {
    for (std::meta::info a : anns)
        if (is_annotation_of_type(a, ^^A)) return true;
    return false;
}

template<class A, class Range>
consteval std::optional<A> find_annotation(Range const& anns) {
    for (std::meta::info a : anns)
        if (is_annotation_of_type(a, ^^A)) return std::meta::extract<A>(a);
    return std::nullopt;
}

// Reflection of the first annotation that is a specialisation of `tmpl`, or nullopt.
template<class Range>
consteval std::optional<std::meta::info> find_annotation_template(Range const& anns,
                                                                    std::meta::info tmpl) {
    for (std::meta::info a : anns)
        if (is_annotation_of_template(a, tmpl)) return a;
    return std::nullopt;
}

// Identifier case transformation for rename_all (spec §5.3). Runs on the C++ identifier,
// which is assumed to be snake_case or camelCase; words are split on '_' and on
// lower→upper transitions.
consteval std::vector<char> apply_case(std::string_view ident, case_ style) {
    std::vector<std::vector<char>> words;
    std::vector<char> cur;
    auto flush = [&] { if (!cur.empty()) { words.push_back(cur); cur.clear(); } };
    auto is_upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    auto is_lower = [](char c) { return c >= 'a' && c <= 'z'; };
    auto to_lower = [&](char c) { return is_upper(c) ? char(c + ('a' - 'A')) : c; };
    auto to_upper = [&](char c) { return is_lower(c) ? char(c - ('a' - 'A')) : c; };
    for (std::size_t i = 0; i < ident.size(); ++i) {
        char c = ident[i];
        if (c == '_') { flush(); continue; }
        if (is_upper(c) && !cur.empty() && is_lower(cur.back())) flush();
        cur.push_back(to_lower(c));
    }
    flush();
    std::vector<char> out;
    for (std::size_t w = 0; w < words.size(); ++w) {
        auto const& word = words[w];
        switch (style) {
        case case_::snake:
            if (w) out.push_back('_');
            out.insert(out.end(), word.begin(), word.end());
            break;
        case case_::kebab:
            if (w) out.push_back('-');
            out.insert(out.end(), word.begin(), word.end());
            break;
        case case_::screaming_snake:
            if (w) out.push_back('_');
            for (char c : word) out.push_back(to_upper(c));
            break;
        case case_::camel:
            for (std::size_t i = 0; i < word.size(); ++i)
                out.push_back(w && i == 0 ? to_upper(word[i]) : word[i]);
            break;
        case case_::pascal:
            for (std::size_t i = 0; i < word.size(); ++i)
                out.push_back(i == 0 ? to_upper(word[i]) : word[i]);
            break;
        }
    }
    return out;
}

} // namespace core
} // namespace vcodec

#endif // VCODEC_CORE_ANNOTATIONS_HPP

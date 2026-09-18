// format_traits<cbor::format> (spec v0.2 §3.2): how CBOR classifies, keys, orders and tags
// members. Everything in the model is native; the hooks express protocol intent.
#ifndef VCODEC_CBOR_TRAITS_HPP
#define VCODEC_CBOR_TRAITS_HPP

#include <vcodec/cbor/annotations.hpp>
#include <vcodec/cbor/head.hpp>
#include <vcodec/core/model.hpp>
#include <vcodec/core/schema.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <string>
#include <vector>

namespace vcodec::cbor::detail {

template<class T> struct is_sys_time_t : std::false_type {};
template<class D> struct is_sys_time_t<std::chrono::sys_time<D>> : std::true_type {};
template<class T> inline constexpr bool is_sys_time_v = is_sys_time_t<std::remove_cvref_t<T>>::value;

template<class T>
concept typed_array_element =
    (std::integral<T> && !std::same_as<T, bool> && !core::char_type<T> && !core::wide_char_type<T>
     && (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8))
    || std::same_as<T, float> || std::same_as<T, double>;

template<class T>
concept floating_or_optional_floating = core::floating_type<T> || (core::optional_like<T> && requires { requires core::floating_type<core::optional_value_t<T>>; });

template<class R>
concept typed_array_range = std::ranges::contiguous_range<R> && std::ranges::sized_range<R>
    && typed_array_element<std::remove_cvref_t<std::ranges::range_value_t<R>>>;

// Allocate declaration-order integer keys for a type declared [[=cbor::integer_keys]]:
// explicit keys are kept, the others get the smallest positive integers not taken, in order.
template<class Owner>
consteval std::optional<std::int64_t> allocated_key(std::string_view identifier) {
    if (!core::has_annotation<integer_keys_t>(core::detail::type_annotations_of<Owner>())) return std::nullopt;
    std::vector<std::int64_t> taken;
    template for (constexpr auto f : core::fields_of<Owner>) {
        constexpr auto k = core::find_annotation<key_t>(f.anns());
        if constexpr (k.has_value()) taken.push_back(k->key);
    }
    std::int64_t next = 1;
    auto bump = [&] { while (std::find(taken.begin(), taken.end(), next) != taken.end()) ++next; };
    template for (constexpr auto f : core::fields_of<Owner>) {
        constexpr core::field_info fi = f.info;
        constexpr auto k = core::find_annotation<key_t>(f.anns());
        if constexpr (!k.has_value()) {
            bump();
            if (fi.identifier.view() == identifier) return next;
            ++next;
        }
    }
    return std::nullopt;
}

} // namespace vcodec::cbor::detail

namespace vcodec::core {

template<>
struct format_traits<cbor::format> : format_traits_defaults {
    static constexpr std::string_view name = "CBOR";
    static constexpr std::string_view integer_key_spelling = "cbor::key";
    static constexpr bool enum_default_integer = true;
    static constexpr bool bytes_borrowable = true;

    template<field_meta F, class T>
    static consteval bool is_bytes() {
        return byte_like<T> || (contiguous_of_uint8<T> && has_annotation<cbor::byte_string_t>(F.anns()));
    }

    template<field_meta F>
    static consteval std::optional<std::int64_t> integer_key() {
        if constexpr (F.depth == 0) return std::nullopt;
        else {
            if (auto k = find_annotation<cbor::key_t>(F.anns())) return k->key;
            return cbor::detail::allocated_key<typename [:F.owner:]>(F.info.identifier.view());
        }
    }

    template<field_meta F>
    static consteval bool accepts_text_key() { return has_annotation<cbor::text_key_alias_t>(F.anns()); }

    // Canonical order of the encoded keys (RFC 8949 §4.2.1), always: a struct's emission
    // order is fixed at compile time, and canonical order is as good as any other.
    template<class T>
    static consteval std::vector<std::size_t> member_order(std::size_t n) {
        std::vector<std::vector<std::byte>> keys(n);
        template for (constexpr auto f : fields_of<T>) {
            constexpr field_info fi = f.info;
            constexpr auto k = integer_key<f>();
            if constexpr (k.has_value()) keys[fi.member_index] = cbor::encode_int_key(*k);
            else keys[fi.member_index] = cbor::encode_text_key(fi.wire_name.view());
        }
        std::vector<std::size_t> order(n);
        for (std::size_t i = 0; i < n; ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return std::lexicographical_compare(keys[a].begin(), keys[a].end(), keys[b].begin(), keys[b].end());
        });
        return order;
    }

    template<field_meta F, class T>
    static consteval std::vector<std::uint64_t> expected_tags() {
        std::vector<std::uint64_t> v;
        for (auto a : F.anns())
            if (is_annotation_of_type(a, ^^cbor::tag_t)) v.push_back(std::meta::extract<cbor::tag_t>(a).tag);
        // A time point without an explicit tag is tag 1 (epoch) by default.
        if (v.empty() && cbor::detail::is_sys_time_v<T>) v.push_back(1);
        return v;
    }

    template<field_meta F, class R>
    static consteval bool bulk_range() {
        return has_annotation<cbor::typed_array_t>(F.anns()) && cbor::detail::typed_array_range<R>;
    }
    template<class R>
    static consteval bool accepts_bulk_range() { return cbor::detail::typed_array_range<R>; }

    template<field_meta F, class T>
    static consteval std::string check_field() {
        using namespace std::string_literals;
        if (has_annotation<cbor::typed_array_t>(F.anns()) && !cbor::detail::typed_array_range<T>)
            return "is declared [[=cbor::typed_array]] but is not a contiguous range of a\n  fixed-width integer or floating type."s;
        if (find_annotation<cbor::float_width_t>(F.anns()).has_value() && !cbor::detail::floating_or_optional_floating<T>)
            return "is declared [[=cbor::float_width(...)]] but is not a floating type."s;
        if constexpr (cbor::detail::is_sys_time_v<T>) {
            bool t0 = false, t1 = false;
            for (auto a : F.anns()) if (is_annotation_of_type(a, ^^cbor::tag_t)) { auto t = std::meta::extract<cbor::tag_t>(a).tag; t0 |= t == 0; t1 |= t == 1; }
            if (t0 && t1) return "carries [[=cbor::tag(1)]] and [[=cbor::tag(0)]]; a time point takes one of the two."s;
        }
        if constexpr (F.depth > 0) {
            using Owner = typename [:F.owner:];
            auto owner_anns = detail::type_annotations_of<Owner>();
            if (has_annotation<cbor::indefinite_t>(F.anns()) && has_annotation<cbor::deterministic_t>(owner_anns))
                return "is declared [[=cbor::indefinite]] inside "s + std::string(type_name_of(^^Owner))
                     + ", which is declared [[=cbor::deterministic]];\n  deterministic encoding forbids indefinite lengths."s;
            if (find_annotation<cbor::key_t>(F.anns()) && has_annotation<transparent_t>(owner_anns))
                return "carries [[=cbor::key(n)]] but "s + std::string(type_name_of(^^Owner)) + " is [[=vcodec::transparent]] and has no map."s;
        }
        if constexpr (class_type<T>) {
            if (has_annotation<cbor::self_describe_t>(detail::type_annotations_of<T>()))
                return "(a member) is declared [[=cbor::self_describe]]; the self-describe tag applies to a top-level value only."s;
        }
        return {};
    }

    template<class T>
    static consteval std::string check_type() {
        using namespace std::string_literals;
        auto anns = detail::type_annotations_of<T>();
        if (has_annotation<cbor::integer_keys_t>(anns)) {
            for (auto const& f : schema_of<T>) {
                if (has(f.flags, field_flags::flattened) || has(f.flags, field_flags::inherited)) {
                    return "(declared [[=cbor::integer_keys]]) has a flattened or inherited member "s + std::string(f.qualified.view())
                         + ";\n  declaration-order key allocation is not defined across a flatten or a base class.\n  Give every member an explicit [[=vcodec::cbor::key(n)]]."s;
                }
            }
            if (has_annotation<transparent_t>(anns))
                return "is declared both [[=cbor::integer_keys]] and [[=vcodec::transparent]]."s;
        }
        if (has_annotation<cbor::indefinite_t>(anns) && has_annotation<cbor::deterministic_t>(anns))
            return "is declared both [[=cbor::indefinite]] and [[=cbor::deterministic]]; deterministic encoding forbids indefinite lengths."s;
        return {};
    }
};

} // namespace vcodec::core

namespace vcodec::cbor {

// Whether a type demands deterministic handling in both directions.
template<class T>
inline constexpr bool is_deterministic_type =
    core::has_annotation<deterministic_t>(core::detail::type_annotations_of<std::remove_cvref_t<T>>());
template<class T>
inline constexpr bool wants_self_describe =
    core::has_annotation<self_describe_t>(core::detail::type_annotations_of<std::remove_cvref_t<T>>());

// RFC 8746 tag for an element type in native endianness.
template<class E>
consteval std::uint64_t typed_array_tag() {
    constexpr bool little = std::endian::native == std::endian::little;
    if constexpr (std::same_as<E, float>)  return little ? 85 : 81;
    else if constexpr (std::same_as<E, double>) return little ? 86 : 82;
    else if constexpr (std::is_signed_v<E>) {
        if constexpr (sizeof(E) == 1) return 72;
        else if constexpr (sizeof(E) == 2) return little ? 77 : 73;
        else if constexpr (sizeof(E) == 4) return little ? 78 : 74;
        else return little ? 79 : 75;
    } else {
        if constexpr (sizeof(E) == 1) return 64;
        else if constexpr (sizeof(E) == 2) return little ? 69 : 65;
        else if constexpr (sizeof(E) == 4) return little ? 70 : 66;
        else return little ? 71 : 67;
    }
}

} // namespace vcodec::cbor

#endif // VCODEC_CBOR_TRAITS_HPP

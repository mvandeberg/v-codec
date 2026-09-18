// A sink that records the token stream as strings (spec §12 M2: "tested against a
// recording mock sink"). Format-free.
#pragma once
#include <vcodec/core/model.hpp>
#include <vcodec/core/lower.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace vcodec_test {

struct recording_sink {
    std::vector<std::string> tokens;

    void null()                 { tokens.push_back("null"); }
    void boolean(bool b)        { tokens.push_back(b ? "true" : "false"); }
    void uint(std::uint64_t u)  { tokens.push_back("u:" + std::to_string(u)); }
    void sint(std::int64_t i)   { tokens.push_back("i:" + std::to_string(i)); }
    void real(double d)         { tokens.push_back("r:" + std::to_string(d)); }
    void text(std::string_view t) { tokens.push_back("t:" + std::string(t)); }
    void bytes(std::span<const std::byte> b) { std::string s = "b:"; vcodec::core::hex_encode(b, s); tokens.push_back(s); }
    void begin_array(std::optional<std::size_t> n) { tokens.push_back(n ? "[" + std::to_string(*n) : std::string("[?")); }
    void end_array()            { tokens.push_back("]"); }
    void begin_map(std::optional<std::size_t> n)   { tokens.push_back(n ? "{" + std::to_string(*n) : std::string("{?")); }
    void end_map()              { tokens.push_back("}"); }
    void key(std::string_view k){ tokens.push_back("k:" + std::string(k)); }
    void key(std::uint64_t k)   { tokens.push_back("ku:" + std::to_string(k)); }
    void key(std::int64_t k)    { tokens.push_back("ki:" + std::to_string(k)); }
    void tag(std::uint64_t t)   { tokens.push_back("tag:" + std::to_string(t)); }
};
static_assert(vcodec::core::sink<recording_sink>);

// A format tag with its own format_traits, to test every hook core consults.
struct mock_format {};
struct as_bytes_t {};        inline constexpr as_bytes_t as_bytes_marker{};
struct as_text_t {};         inline constexpr as_text_t as_text_marker{};
struct int_key_marker_t { std::int64_t key; };
consteval int_key_marker_t int_key_marker(std::int64_t k) { return { k }; }
struct tag_marker_t { std::uint64_t tag; };
consteval tag_marker_t tag_marker(std::uint64_t t) { return { t }; }
struct bulk_marker_t {};     inline constexpr bulk_marker_t bulk_marker{};
struct reversed_t {};        inline constexpr reversed_t reversed_members{};   // type-level: emit members in reverse

struct hooked_sink : recording_sink {
    using format = mock_format;
    using recording_sink::uint;
    using recording_sink::bytes;

    template<vcodec::core::static_string Name>
    void prepared_key() { tokens.push_back("pk:" + std::string(Name.view())); }

    template<vcodec::core::field_meta F>
    void uint(std::uint64_t u) {
        if constexpr (vcodec::core::has_annotation<as_text_t>(F.anns())) tokens.push_back("t:" + std::to_string(u));
        else recording_sink::uint(u);
    }
    template<vcodec::core::field_meta F>
    void bytes(std::span<const std::byte> b) {
        tokens.push_back("fb:" + std::to_string(b.size()));
    }
    template<class T>
        requires requires { T::prepared_marker; }
    void write_prepared(T const&) { tokens.push_back("prepared:" + std::string(T::prepared_marker)); }

    template<std::int64_t Key>
    void prepared_key() { tokens.push_back("pik:" + std::to_string(Key)); }
    void begin_map_ordered(std::optional<std::size_t> n) { tokens.push_back(n ? "{o" + std::to_string(*n) : std::string("{o?")); }
    template<vcodec::core::field_meta F, class R>
    void write_range(R const& r) { tokens.push_back("bulk:" + std::to_string(std::ranges::size(r))); }
};
static_assert(vcodec::core::sink<hooked_sink>);

} // namespace vcodec_test

template<>
struct vcodec::core::format_traits<vcodec_test::mock_format> : vcodec::core::format_traits_defaults {
    static constexpr std::string_view name = "mock";
    static constexpr std::string_view integer_key_spelling = "mock::int_key";
    template<field_meta F, class T>
    static consteval bool is_bytes() {
        return byte_like<T> || (contiguous_of_uint8<T> && has_annotation<vcodec_test::as_bytes_t>(F.anns()));
    }
    template<field_meta F>
    static consteval std::optional<std::int64_t> integer_key() {
        if (auto k = find_annotation<vcodec_test::int_key_marker_t>(F.anns())) return k->key;
        return std::nullopt;
    }
    template<class T>
    static consteval std::vector<std::size_t> member_order(std::size_t n) {
        std::vector<std::size_t> v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = i;
        if (has_annotation<vcodec_test::reversed_t>(detail::type_annotations_of<T>())) std::reverse(v.begin(), v.end());
        return v;
    }
    template<field_meta F>
    static consteval std::vector<std::uint64_t> expected_tags() {
        std::vector<std::uint64_t> v;
        for (auto a : F.anns()) if (is_annotation_of_type(a, ^^vcodec_test::tag_marker_t)) v.push_back(std::meta::extract<vcodec_test::tag_marker_t>(a).tag);
        return v;
    }
    static constexpr bool enum_default_integer = true;
    template<field_meta F, class R>
    static consteval bool bulk_range() { return has_annotation<vcodec_test::bulk_marker_t>(F.anns()); }
    template<field_meta F, class T>
    static consteval std::string check_field() {
        if (has_annotation<vcodec_test::bulk_marker_t>(F.anns()) && !std::ranges::contiguous_range<T>)
            return std::string("is marked bulk but is not a contiguous range.");
        return {};
    }
};

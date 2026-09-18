// A sink that records the token stream as strings (spec §12 M2: "tested against a
// recording mock sink"). Format-free.
#pragma once
#include <vcodec/core/model.hpp>
#include <vcodec/core/lower.hpp>

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

// A format tag with its own format_traits, to test the hooks core consults.
struct mock_format {};
struct as_bytes_t {};        inline constexpr as_bytes_t as_bytes_marker{};
struct as_text_t {};         inline constexpr as_text_t as_text_marker{};

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
};
static_assert(vcodec::core::sink<hooked_sink>);

} // namespace vcodec_test

template<>
struct vcodec::core::format_traits<vcodec_test::mock_format> {
    static constexpr std::string_view name = "mock";
    template<field_meta F, class T>
    static consteval bool is_bytes() {
        return byte_like<T> || (contiguous_of_uint8<T> && has_annotation<vcodec_test::as_bytes_t>(F.anns()));
    }
    template<field_meta F, class T> static consteval bool can_lower_bytes() { return true; }
    template<field_meta F, class K> static consteval bool can_lower_key() { return true; }
    static constexpr std::string_view bytes_hint = "";
    static constexpr std::string_view key_hint = "";
};

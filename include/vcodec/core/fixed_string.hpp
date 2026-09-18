// Structural strings for annotations and schema tables (spec §5.1, M0 finding 1).
#ifndef VCODEC_CORE_FIXED_STRING_HPP
#define VCODEC_CORE_FIXED_STRING_HPP

#include <vcodec/core/compiler.hpp>

#include <meta>
#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace vcodec::core {

// A pointer into static storage plus a length. Structural, so it can be an annotation
// payload, live in a define_static_array table, and be a non-type template argument.
struct static_string {
    const char* ptr  = nullptr;
    std::size_t len  = 0;

    constexpr std::string_view view() const noexcept { return {ptr, len}; }
    constexpr operator std::string_view() const noexcept { return view(); }
    constexpr bool empty() const noexcept { return len == 0; }
    constexpr std::size_t size() const noexcept { return len; }
    constexpr const char* data() const noexcept { return ptr; }

    friend constexpr bool operator==(static_string a, static_string b) noexcept {
        return a.view() == b.view();
    }
    friend constexpr bool operator==(static_string a, std::string_view b) noexcept {
        return a.view() == b;
    }
};

// Place a string in static storage. Empty strings intern to a non-null pointer so that
// view().data() is always dereferenceable.
consteval static_string intern(std::string_view s) {
    return static_string{ std::define_static_string(s), s.size() };
}
consteval static_string intern(const std::vector<char>& v) {
    return intern(std::string_view{v.data(), v.size()});
}

// A fixed-capacity character buffer for building text at compile time (diagnostic
// messages, mangled keys). Structural; N is the capacity, size() the used length.
template<std::size_t N>
struct fixed_string {
    std::array<char, N> buf{};
    std::size_t         n = 0;

    constexpr fixed_string() = default;
    constexpr fixed_string(std::string_view s) { append(s); }

    constexpr const char* data() const noexcept { return buf.data(); }
    constexpr std::size_t size() const noexcept { return n; }
    constexpr std::string_view view() const noexcept { return {buf.data(), n}; }
    constexpr bool truncated() const noexcept { return n == N; }
    constexpr void clear() noexcept { n = 0; }

    constexpr fixed_string& append(std::string_view s) {
        for (char c : s) { if (n < N) buf[n++] = c; }
        return *this;
    }
    constexpr fixed_string& append(char c) { if (n < N) buf[n++] = c; return *this; }
    template<class I>
        requires std::is_integral_v<I>
    constexpr fixed_string& append_int(I value) {
        char tmp[24]; std::size_t k = 0;
        bool neg = value < 0;
        using U = std::make_unsigned_t<I>;
        U u = neg ? U(0) - U(value) : U(value);
        do { tmp[k++] = char('0' + u % 10); u /= 10; } while (u);
        if (neg) append('-');
        while (k) append(tmp[--k]);
        return *this;
    }
    friend constexpr bool operator==(fixed_string const& a, std::string_view b) noexcept {
        return a.view() == b;
    }
};

} // namespace vcodec::core

#endif // VCODEC_CORE_FIXED_STRING_HPP

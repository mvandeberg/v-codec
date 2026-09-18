// Test helpers: hex ↔ bytes for CBOR test vectors.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vcodec_test {

inline std::string to_hex(std::span<const std::byte> b) {
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    for (auto x : b) { s += d[std::to_integer<unsigned>(x) >> 4]; s += d[std::to_integer<unsigned>(x) & 15]; }
    return s;
}
inline std::string to_hex(std::vector<std::byte> const& b) { return to_hex(std::span<const std::byte>(b)); }

inline std::vector<std::byte> from_hex(std::string_view h) {
    std::vector<std::byte> out;
    auto val = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    std::string clean;
    for (char c : h) if (c != ' ') clean += c;
    for (std::size_t i = 0; i + 1 < clean.size(); i += 2)
        out.push_back(std::byte((val(clean[i]) << 4) | val(clean[i + 1])));
    return out;
}

} // namespace vcodec_test

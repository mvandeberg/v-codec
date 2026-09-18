// THROWAWAY (spec §12 M3). A write-only CBOR sink whose single purpose is to prove the
// seam: core/ must be able to drive it without knowing it exists. Never included by
// anything under include/; built only by test/cross with VCODEC_BUILD_SPIKE=ON.
//
// RFC 8949 encoding, no canonicalisation, doubles always as 64-bit. Definite lengths
// where core supplies them, indefinite (0x9F/0xBF … 0xFF) where it does not.
#pragma once

#include <vcodec/core/model.hpp>
#include <vcodec/core/schema.hpp>

#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vcodec_spike {

struct cbor_format {};

class cbor_sink {
public:
    using format = cbor_format;

    std::string out;

    void null()           { out.push_back(char(0xF6)); }
    void boolean(bool b)  { out.push_back(char(b ? 0xF5 : 0xF4)); }
    void uint(std::uint64_t u) { head(0, u); }
    void sint(std::int64_t i) {
        if (i >= 0) head(0, std::uint64_t(i));
        else        head(1, std::uint64_t(-1 - i));
    }
    void real(double d) {
        out.push_back(char(0xFB));
        auto bits = std::bit_cast<std::uint64_t>(d);
        for (int s = 56; s >= 0; s -= 8) out.push_back(char((bits >> s) & 0xFF));
    }
    void text(std::string_view t)             { head(3, t.size()); out.append(t.data(), t.size()); }
    void bytes(std::span<const std::byte> b)  { head(2, b.size()); out.append(reinterpret_cast<const char*>(b.data()), b.size()); }
    void begin_array(std::optional<std::size_t> n) { open(4, n); }
    void end_array()                          { close(); }
    void begin_map(std::optional<std::size_t> n)   { open(5, n); }
    void end_map()                            { close(); }
    void key(std::string_view k)              { text(k); }
    void key(std::uint64_t k)                 { uint(k); }
    void key(std::int64_t k)                  { sint(k); }
    void tag(std::uint64_t t)                 { head(6, t); }

    // Seam hook: a key whose CBOR text-string header and bytes are computed at compile time.
    template<vcodec::core::static_string Name>
    void prepared_key() {
        static constexpr auto blob = [] {
            vcodec::core::fixed_string<80> b;
            std::size_t n = Name.size();
            if (n < 24)        b.append(char(0x60 | n));
            else if (n < 256) { b.append(char(0x78)); b.append(char(n)); }
            else              { b.append(char(0x79)); b.append(char(n >> 8)); b.append(char(n & 0xFF)); }
            b.append(Name.view());
            return b;
        }();
        out.append(blob.data(), blob.size());
    }

private:
    std::vector<bool> indefinite_;

    void head(unsigned major, std::uint64_t v) {
        unsigned char mt = static_cast<unsigned char>(major << 5);
        if (v < 24)              { out.push_back(char(mt | v)); }
        else if (v <= 0xFF)      { out.push_back(char(mt | 24)); out.push_back(char(v)); }
        else if (v <= 0xFFFF)    { out.push_back(char(mt | 25)); be(v, 2); }
        else if (v <= 0xFFFFFFFFull) { out.push_back(char(mt | 26)); be(v, 4); }
        else                     { out.push_back(char(mt | 27)); be(v, 8); }
    }
    void be(std::uint64_t v, int bytes) {
        for (int s = (bytes - 1) * 8; s >= 0; s -= 8) out.push_back(char((v >> s) & 0xFF));
    }
    void open(unsigned major, std::optional<std::size_t> n) {
        if (n) { head(major, *n); indefinite_.push_back(false); }
        else   { out.push_back(char((major << 5) | 31)); indefinite_.push_back(true); }
    }
    void close() {
        if (indefinite_.back()) out.push_back(char(0xFF));
        indefinite_.pop_back();
    }
};
static_assert(vcodec::core::sink<cbor_sink>);

} // namespace vcodec_spike

// CBOR represents every element of the model natively: the primary format_traits apply.

// Test-only: dumps a CBOR byte string into the same token vocabulary recording_sink uses, so
// cross-format tests can compare a CBOR encoding against the recorded model stream.
#pragma once
#include <vcodec/core/lower.hpp>

#include <cstring>

#include <bit>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace vcodec_test {

class cbor_dumper {
public:
    static std::vector<std::string> dump(std::string_view in) {
        cbor_dumper d{in};
        while (d.pos_ < in.size()) d.item(false);
        return d.out_;
    }
private:
    std::string_view in_; std::size_t pos_ = 0; std::vector<std::string> out_;
    explicit cbor_dumper(std::string_view in) : in_(in) {}

    unsigned char byte() { if (pos_ >= in_.size()) throw std::runtime_error("truncated cbor"); return static_cast<unsigned char>(in_[pos_++]); }
    std::uint64_t arg(unsigned char ib) {
        unsigned ai = ib & 31;
        if (ai < 24) return ai;
        int n = ai == 24 ? 1 : ai == 25 ? 2 : ai == 26 ? 4 : 8;
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) v = (v << 8) | byte();
        return v;
    }
    // is_key: emit map keys with the recording sink's key spellings
    void item(bool is_key) {
        unsigned char ib = byte();
        unsigned major = ib >> 5;
        bool indefinite = (ib & 31) == 31;
        switch (major) {
        case 0: { auto v = arg(ib); out_.push_back((is_key ? "ku:" : "u:") + std::to_string(v)); break; }
        case 1: { auto v = arg(ib); out_.push_back((is_key ? "ki:" : "i:") + std::to_string(-1 - std::int64_t(v))); break; }
        case 2: { auto n = arg(ib); std::string s = "b:";
                  std::vector<std::byte> b; for (std::uint64_t i = 0; i < n; ++i) b.push_back(std::byte(byte()));
                  vcodec::core::hex_encode(b, s); out_.push_back(s); break; }
        case 3: { auto n = arg(ib); std::string s(in_.substr(pos_, n)); pos_ += n; out_.push_back((is_key ? "k:" : "t:") + s); break; }
        case 4: {
            if (indefinite) { out_.push_back("[?"); while (static_cast<unsigned char>(in_[pos_]) != 0xFF) item(false); ++pos_; }
            else { auto n = arg(ib); out_.push_back("[" + std::to_string(n)); for (std::uint64_t i = 0; i < n; ++i) item(false); }
            out_.push_back("]"); break;
        }
        case 5: {
            if (indefinite) { out_.push_back("{?"); while (static_cast<unsigned char>(in_[pos_]) != 0xFF) { item(true); item(false); } ++pos_; }
            else { auto n = arg(ib); out_.push_back("{" + std::to_string(n)); for (std::uint64_t i = 0; i < n; ++i) { item(true); item(false); } }
            out_.push_back("}"); break;
        }
        case 6: { out_.push_back("tag:" + std::to_string(arg(ib))); item(false); break; }
        case 7: {
            unsigned ai = ib & 31;
            if (ai == 20) out_.push_back("false");
            else if (ai == 21) out_.push_back("true");
            else if (ai == 22) out_.push_back("null");
            else if (ai == 25) { std::uint16_t v = 0; for (int i = 0; i < 2; ++i) v = static_cast<std::uint16_t>((v << 8) | byte()); out_.push_back("r:" + std::to_string(vcodec::core::from_half(v))); }
            else if (ai == 26) { std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | byte(); out_.push_back("r:" + std::to_string(double(std::bit_cast<float>(v)))); }
            else if (ai == 27) { std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | byte(); out_.push_back("r:" + std::to_string(std::bit_cast<double>(v))); }
            else throw std::runtime_error("unsupported simple value");
            break;
        }
        }
    }
};

} // namespace vcodec_test

// v0.2 §13.4 harness 4: arbitrary bytes under require_deterministic. A success must re-encode
// byte-identically; a value that decodes only without the option must re-encode to bytes that
// then pass.
#include <vcodec/cbor.hpp>

#include "dom.hpp"

#include "fuzz_common.hpp"

#include <cstdint>
#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    auto in = std::as_bytes(std::span(data, size));
    constexpr vcodec::cbor::options strict{ .require_deterministic = true };

    auto s = vcodec::cbor::decode<vcodec_test::dom::value, strict>(in);
    auto loose = vcodec::cbor::decode<vcodec_test::dom::value>(in);
    if (s) {
        VCODEC_FUZZ_CHECK(loose.has_value(), "strict accepted what loose rejected");
        // The DOM cannot distinguish -0.0 from 0, preserve unknown tags, or keep `undefined`
        // (decoded as null by default), so an exact byte identity is only required when no
        // byte could be one of those. A tag on a map key can even make two distinct keys
        // collide once the tag is dropped, which the canonical re-encode rightly refuses.
        bool has_tag = false, neg_zero = false;
        for (std::size_t i = 0; i < in.size(); ++i) {
            if ((std::to_integer<unsigned>(in[i]) >> 5) == 6 || std::to_integer<unsigned>(in[i]) == 0xf7) has_tag = true;
            if (i + 2 < in.size() && std::to_integer<unsigned>(in[i]) == 0xf9 && std::to_integer<unsigned>(in[i + 1]) == 0x80 && std::to_integer<unsigned>(in[i + 2]) == 0x00) neg_zero = true;
        }
        std::vector<std::byte> again;
        try { again = vcodec::cbor::encode(*s); }
        catch (vcodec::encode_error const& e) {
            VCODEC_FUZZ_CHECK(has_tag && e.code() == vcodec::errc::duplicate_key, e.what());
            return 0;
        }
        if (again.size() != in.size() || !std::equal(again.begin(), again.end(), in.begin()))
            VCODEC_FUZZ_CHECK(has_tag || neg_zero, "deterministic input did not re-encode byte-identically");
    } else if (loose) {
        VCODEC_FUZZ_CHECK(s.error().code() == vcodec::errc::non_deterministic, vcodec::render_terse(s.error()).c_str());
        VCODEC_FUZZ_CHECK(!vcodec::render_hex(s.error(), in).empty(), "rendered error is empty");
        std::vector<std::byte> canon;
        try { canon = vcodec::cbor::encode(*loose); }
        catch (vcodec::encode_error const& e) {
            // A loosely-valid map with duplicate keys has no canonical form: refusing is correct.
            VCODEC_FUZZ_CHECK(e.code() == vcodec::errc::duplicate_key, e.what());
            return 0;
        }
        { auto c = vcodec::cbor::decode<vcodec_test::dom::value, strict>(canon); VCODEC_FUZZ_CHECK(c.has_value(), c ? "" : vcodec::render_terse(c.error()).c_str()); }
    }
    return 0;
}

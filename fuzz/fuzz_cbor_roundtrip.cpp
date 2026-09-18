// v0.2 §13.4 harness 3: structured input → deterministic encode → decode → compare; the
// re-encoding must be byte-identical and the non-deterministic encoding must decode equal.
#include "fuzz_cbor_kitchen.hpp"

#include "generators.hpp"

#include "fuzz_common.hpp"

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    for (std::size_t i = 0; i < size; ++i) seed = (seed ^ data[i]) * 0x100000001B3ull;
    vcodec_test::rng g(seed);
    g.depth = size % 5;

    auto v = vcodec_test::generate<fuzz_cbor::Deep>(g);
    std::vector<std::byte> bytes;
    try { bytes = vcodec::cbor::encode(v); }
    catch (vcodec::encode_error const& e) { VCODEC_FUZZ_CHECK(false, e.what()); }

    auto back = vcodec::cbor::decode<fuzz_cbor::Deep>(bytes);
    VCODEC_FUZZ_CHECK(back.has_value(), back ? "" : vcodec::render_terse(back.error()).c_str());
    VCODEC_FUZZ_CHECK(vcodec_test::equal(v, *back), "decoded value differs from the generated one");
    VCODEC_FUZZ_CHECK(vcodec::cbor::encode(*back) == bytes, "re-encoding is not byte-identical");

    constexpr vcodec::cbor::options strict{ .require_deterministic = true };
    { auto s = vcodec::cbor::decode<fuzz_cbor::Deep, strict>(bytes); VCODEC_FUZZ_CHECK(s.has_value(), s ? "" : vcodec::render_terse(s.error()).c_str()); }

    constexpr vcodec::cbor::options loose{ .deterministic = false, .indefinite = true };
    auto loose_bytes = vcodec::cbor::encode<loose>(v);
    auto loose_back = vcodec::cbor::decode<fuzz_cbor::Deep>(loose_bytes);
    VCODEC_FUZZ_CHECK(loose_back.has_value(), loose_back ? "" : vcodec::render_terse(loose_back.error()).c_str());
    VCODEC_FUZZ_CHECK(vcodec_test::equal(v, *loose_back), "non-deterministic encoding decoded to a different value");
    VCODEC_FUZZ_CHECK(vcodec::cbor::encode(*loose_back) == bytes, "canonical re-encoding of the non-deterministic form differs");
    return 0;
}

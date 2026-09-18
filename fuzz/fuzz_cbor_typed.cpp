// v0.2 §13.4 harness 2: arbitrary bytes → decode<Borrowed>. A success must re-encode and
// decode to an equal value; decode_all must terminate; strict options must be safe.
#include "fuzz_cbor_kitchen.hpp"

#include "fuzz_common.hpp"

#include <cstdint>
#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::vector<std::byte> in(reinterpret_cast<const std::byte*>(data), reinterpret_cast<const std::byte*>(data) + size);

    auto r = vcodec::cbor::decode<fuzz_cbor::Borrowed>(in);
    if (r) {
        std::vector<std::byte> again;
        try { again = vcodec::cbor::encode(*r); }
        catch (vcodec::encode_error const& e) { VCODEC_FUZZ_CHECK(false, e.what()); }
        auto back = vcodec::cbor::decode<fuzz_cbor::Borrowed>(again);
        VCODEC_FUZZ_CHECK(back.has_value(), back ? "" : vcodec::render_terse(back.error()).c_str());
        VCODEC_FUZZ_CHECK(vcodec::cbor::encode(*back) == again, "re-encoding of a decoded value is not byte-identical");
    } else {
        auto s = vcodec::render_hex(r.error(), in);
        VCODEC_FUZZ_CHECK(!s.empty() && r.error().offset() <= in.size(), "rendered error is empty or its offset is past the input");
        VCODEC_FUZZ_CHECK(!r.error().position(), "a CBOR error carries a line/column");
    }

    auto all = vcodec::cbor::decode_all<fuzz_cbor::Deep>(in);
    for (auto const& e : all.errors) VCODEC_FUZZ_CHECK(!vcodec::render_terse(e).empty(), "collected error renders empty");

    constexpr vcodec::cbor::options strict{ .require_deterministic = true, .ignore_unknown_tags = false, .undefined_as_null = false,
                                            .accept_typed_arrays = false, .max_depth = 6, .duplicates = vcodec::duplicate_key::error };
    (void)vcodec::cbor::decode<fuzz_cbor::Deep, strict>(in);
    (void)vcodec::cbor::decode<fuzz_cbor::Canonical>(in);
    return 0;
}

// v0.2 §13.4 harness 1: arbitrary bytes → CBOR skip_value. Oracle: no crash/UB, terminates,
// skip path and DOM path agree, rendered error is well-formed with offset ≤ size.
#include <vcodec/cbor.hpp>

#include "dom.hpp"

#include "fuzz_common.hpp"

#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    auto in = std::as_bytes(std::span(data, size));

    vcodec::cbor::reader<> r(in);
    r.start();
    auto st = r.skip_value();
    bool skip_ok = st.has_value() && r.finish().has_value();
    if (!skip_ok) {
        vcodec::error e = st ? vcodec::error(vcodec::errc::trailing_content, 0) : std::move(st.error());
        e.finalize();
        auto s = vcodec::render_hex(e, in);
        VCODEC_FUZZ_CHECK(!s.empty() && e.offset() <= in.size(), "rendered error is empty or its offset is past the input");
    }

    auto typed = vcodec::cbor::decode<vcodec_test::dom::value>(in);
    VCODEC_FUZZ_CHECK(typed.has_value() == skip_ok, typed.has_value() ? "typed path accepted what skip_value rejected" : "skip_value accepted what the typed path rejected");

    constexpr vcodec::cbor::options strict{ .require_deterministic = true, .ignore_unknown_tags = false, .undefined_as_null = false, .max_depth = 8 };
    vcodec::cbor::reader<strict> r2(in);
    (void)r2.skip_value();
    (void)vcodec::cbor::decode<vcodec_test::dom::value, strict>(in);
    return 0;
}

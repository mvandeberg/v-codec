// Harness 1 (§13.4): arbitrary bytes → skip_value. Oracle: no crash, no UB, no unbounded
// memory, terminates. Also checks that accept/reject agrees with the typed DOM path.
#include <vcodec/json.hpp>

#include "dom.hpp"

#include <cstdint>
#include <cstdlib>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::string_view text(reinterpret_cast<const char*>(data), size);

    vcodec::json::reader<> r(text);
    auto st = r.skip_value();
    bool skip_ok = st.has_value() && r.finish().has_value();
    if (!skip_ok) {
        // The error must be renderable and well-formed.
        vcodec::error e = st ? vcodec::error(vcodec::errc::trailing_content, 0) : std::move(st.error());
        e.finalize();
        auto s = vcodec::render_framed(e, text);
        if (s.empty()) std::abort();
        if (e.offset() > text.size()) std::abort();
    }

    auto typed = vcodec::json::decode<vcodec_test::dom::value>(text);
    if (typed.has_value() != skip_ok) std::abort();   // the two paths must agree

    // A depth-limited configuration must also be safe.
    constexpr vcodec::json::options shallow{ .max_depth = 8, .validate_utf8 = false };
    vcodec::json::reader<shallow> r2(text);
    (void)r2.skip_value();
    return 0;
}

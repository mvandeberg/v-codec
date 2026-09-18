// Harness 2 (§13.4): arbitrary bytes → decode<Kitchen>. Oracle: no crash, no UB; a success
// implies a valid Kitchen, which we check by re-encoding it and decoding that again.
#include "fuzz_kitchen.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::string text(reinterpret_cast<const char*>(data), size);

    auto r = vcodec::json::decode<fuzz::Borrowed>(text);
    if (r) {
        // Valid value: it must encode, and the encoding must decode.
        std::string again;
        try { again = vcodec::json::encode(*r); }
        catch (vcodec::encode_error const&) { std::abort(); }   // decoded values never contain unencodable state
        auto back = vcodec::json::decode<fuzz::Borrowed>(again);
        if (!back) std::abort();
        if (vcodec::json::encode(*back) != again) std::abort();
    } else {
        auto s = vcodec::render_framed(r.error(), text);
        if (s.empty() || r.error().offset() > text.size()) std::abort();
        if (!r.error().position()) std::abort();
    }

    // Collect-all must terminate and produce renderable errors.
    auto all = vcodec::json::decode_all<fuzz::Deep>(text);
    for (auto const& e : all.errors) {
        if (vcodec::render_terse(e).empty()) std::abort();
    }
    // Strict duplicate policy and a shallow depth must be safe too.
    constexpr vcodec::json::options strict{ .max_depth = 6, .duplicates = vcodec::duplicate_key::error };
    (void)vcodec::json::decode<fuzz::Deep, strict>(text);
    return 0;
}

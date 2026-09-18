// Harness 3 (§13.4): structured input → encode → decode → compare. The bytes seed a
// deterministic generator that builds a Deep value; equality is the reflection-based
// structural comparison from test/support/generators.hpp.
#include "fuzz_kitchen.hpp"

#include "generators.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    for (std::size_t i = 0; i < size; ++i) seed = (seed ^ data[i]) * 0x100000001B3ull;
    vcodec_test::rng g(seed);
    g.depth = size % 5;   // vary the nesting budget with the input

    auto v = vcodec_test::generate<fuzz::Deep>(g);
    std::string text;
    try { text = vcodec::json::encode(v); }
    catch (vcodec::encode_error const&) { std::abort(); }   // generated values are always encodable

    auto back = vcodec::json::decode<fuzz::Deep>(text);
    if (!back) std::abort();
    if (!vcodec_test::equal(v, *back)) std::abort();
    if (vcodec::json::encode(*back) != text) std::abort();

    constexpr vcodec::json::options pretty{ .pretty = true };
    auto spaced = vcodec::json::encode<pretty>(v);
    auto back2 = vcodec::json::decode<fuzz::Deep, pretty>(spaced);
    if (!back2 || !vcodec_test::equal(v, *back2)) std::abort();
    return 0;
}

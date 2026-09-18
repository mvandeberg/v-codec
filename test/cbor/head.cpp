// Item heads and half floats: every additional-information form, shortest-form selection,
// and all 65,536 binary16 patterns.
#include <vcodec/cbor/head.hpp>
#include <vcodec/core/lower.hpp>

#include "cbor_hex.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>

using namespace vcodec_test;
namespace cb = vcodec::cbor;

TEST_CASE("head: shortest-form encoding for every argument width") {
    auto h = [](cb::major m, std::uint64_t v) { auto e = cb::make_head(m, v); return to_hex(e.view()); };
    CHECK(h(cb::major::uint, 0) == "00");
    CHECK(h(cb::major::uint, 23) == "17");
    CHECK(h(cb::major::uint, 24) == "1818");
    CHECK(h(cb::major::uint, 255) == "18ff");
    CHECK(h(cb::major::uint, 256) == "190100");
    CHECK(h(cb::major::uint, 65535) == "19ffff");
    CHECK(h(cb::major::uint, 65536) == "1a00010000");
    CHECK(h(cb::major::uint, 4294967295ull) == "1affffffff");
    CHECK(h(cb::major::uint, 4294967296ull) == "1b0000000100000000");
    CHECK(h(cb::major::uint, 18446744073709551615ull) == "1bffffffffffffffff");
    CHECK(h(cb::major::nint, 0) == "20");
    CHECK(h(cb::major::text, 5) == "65");
    CHECK(h(cb::major::array, 30) == "981e");
    CHECK(h(cb::major::map, 2) == "a2");
    CHECK(h(cb::major::tag, 1) == "c1");
    CHECK(h(cb::major::tag, 55799) == "d9d9f7");
    CHECK(to_hex(cb::make_indefinite_head(cb::major::array).view()) == "9f");
}

TEST_CASE("head: parsing reports form, truncation and ill-formed bytes") {
    auto parse = [](std::string_view hex) { auto b = from_hex(hex); return cb::parse_head(b, 0); };
    auto h = parse("1903e8");
    CHECK(h.mt == cb::major::uint); CHECK(h.value == 1000); CHECK(h.length == 3); CHECK(h.shortest());
    CHECK_FALSE(parse("1800").shortest());          // 0 in two bytes
    CHECK_FALSE(parse("190017").shortest());        // 23 in three bytes
    CHECK(parse("18ff").shortest());
    CHECK(parse("19").length == 0);                 // truncated argument
    CHECK(parse("").length == 0);
    CHECK(parse("1c").reserved);                    // ai 28
    CHECK(parse("1f").reserved);                    // indefinite uint is ill-formed
    CHECK(parse("9f").indefinite);
    CHECK_FALSE(parse("9f").reserved);
    CHECK(parse("ff").is_break);
    CHECK(parse("f97e00").mt == cb::major::simple);
    CHECK(parse("f97e00").ai == 25);
    CHECK(parse("fb3ff8000000000000").value == 0x3ff8000000000000ull);
}

TEST_CASE("half: exact conversions and round trip over every pattern") {
    using vcodec::core::from_half; using vcodec::core::to_half; using vcodec::core::to_half_rounded;
    CHECK(from_half(0x0000) == 0.0);
    CHECK(std::signbit(from_half(0x8000)));
    CHECK(from_half(0x3c00) == 1.0);
    CHECK(from_half(0x3e00) == 1.5);
    CHECK(from_half(0x7bff) == 65504.0);
    CHECK(from_half(0x0001) == 5.960464477539063e-8);
    CHECK(from_half(0x0400) == 6.103515625e-5);
    CHECK(std::isinf(from_half(0x7c00)));
    CHECK(std::isnan(from_half(0x7e00)));
    CHECK(to_half(1.0) == 0x3c00);
    CHECK(to_half(-0.0) == 0x8000);
    CHECK(to_half(65504.0) == 0x7bff);
    CHECK(to_half(5.960464477539063e-8) == 0x0001);
    CHECK_FALSE(to_half(65505.0).has_value());
    CHECK_FALSE(to_half(0.1).has_value());
    CHECK_FALSE(to_half(1e-8).has_value());
    CHECK(to_half(std::numeric_limits<double>::infinity()) == 0x7c00);
    CHECK(to_half(std::numeric_limits<double>::quiet_NaN()) == 0x7e00);
    int exact = 0;
    for (unsigned p = 0; p < 65536; ++p) {
        auto h = static_cast<std::uint16_t>(p);
        double d = from_half(h);
        if (std::isnan(d)) { CHECK(to_half(d) == 0x7e00); continue; }
        auto back = to_half(d);
        REQUIRE(back.has_value());
        CHECK(*back == h);
        CHECK(to_half_rounded(d) == h);
        ++exact;
    }
    CHECK(exact > 60000);
    // rounding: nearest even
    CHECK(to_half_rounded(1.0 + 1.0 / 4096) == 0x3c00);      // just above 1 → rounds down to 1.0 (tie to even)
    CHECK(to_half_rounded(1.0 + 3.0 / 4096) == 0x3c01);      // 0.75 ulp above 1 → 1 + 1/1024
    CHECK(to_half_rounded(1e5) == 0x7c00);                   // overflow → inf
    CHECK(vcodec::core::fits_single(0.5));
    CHECK_FALSE(vcodec::core::fits_single(0.1));
}

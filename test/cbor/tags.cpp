// C5: standard tag codecs — std::chrono time points as tag 1 (epoch) or tag 0 (RFC 3339),
// bignums for user codecs.
#include <vcodec/cbor.hpp>

#include "cbor_hex.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace vcodec_test;
namespace cb = vcodec::cbor;
using namespace std::chrono;

namespace {
struct Event {
    sys_seconds when{ seconds(1363896240) };
    [[=cb::tag(0)]] sys_seconds text{ seconds(1363896240) };
    sys_time<milliseconds> precise{ milliseconds(1363896240500) };
    std::optional<sys_seconds> maybe;
};
template<class T> std::string enc(T const& v) { return to_hex(cb::encode(v)); }
}

TEST_CASE("tags: time points encode as tag 1 by default, tag 0 as text on request") {
    auto h = enc(Event{});
    // canonical key order: "text", "when" (4 chars) < "maybe" (5) < "precise" (7)
    CHECK(h == "a4"
        "6474657874" "c0" "74323031332d30332d32315432303a30343a30305a"
        "647768656e" "c11a514b67b0"
        "656d61796265" "f6"
        "6770726563697365" "c1fb41d452d9ec200000");
}

TEST_CASE("tags: time points decode from integer, float and text forms") {
    auto e = cb::decode<Event>(from_hex("a4"
        "656d61796265" "c11a514b67b0"
        "6770726563697365" "c1fb41d452d9ec200000"
        "6474657874" "c0" "74323031332d30332d32315432303a30343a30305a"
        "647768656e" "c11a514b67b0"));
    REQUIRE(e);
    CHECK(e->when == sys_seconds{ seconds(1363896240) });
    CHECK(e->text == sys_seconds{ seconds(1363896240) });
    CHECK(e->precise == sys_time<milliseconds>{ milliseconds(1363896240500) });
    CHECK(e->maybe == sys_seconds{ seconds(1363896240) });
    // wrong tag on a time point
    auto wrong = cb::decode<Event>(from_hex("a1" "647768656e" "c01a514b67b0"));
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().code() == vcodec::errc::tag_mismatch);
    CHECK(wrong.error().expected() == "tag 1 (epoch datetime)");
    CHECK(wrong.error().found() == "tag 0 (RFC 3339 string)");
    // malformed text under tag 0
    auto bad = cb::decode<Event>(from_hex("a1" "6474657874" "c0" "63616263"));
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code() == vcodec::errc::type_mismatch);
    CHECK(bad.error().expected() == "RFC 3339 date-time");
    // offset form accepted
    auto off = cb::decode<Event>(from_hex("a1" "6474657874" "c0" "7819323031332d30332d32315432313a30343a30302b30313a3030"));
    REQUIRE(off);
    CHECK(off->text == sys_seconds{ seconds(1363896240) });
}

namespace {
struct BigId { std::vector<std::byte> magnitude; bool negative = false; };
}
template<> struct vcodec::codec_for<BigId, vcodec::cbor::format> {
    static void encode(vcodec::core::sink auto& s, BigId const& v) { s.tag(v.negative ? 3 : 2); s.bytes(v.magnitude); }
    static vcodec::status decode(vcodec::cbor::reader<> & r, BigId& out) {
        auto b = r.expect_bignum();
        if (!b) return std::unexpected(std::move(b.error()));
        out.negative = b->negative;
        out.magnitude.assign(b->magnitude.begin(), b->magnitude.end());
        return {};
    }
    static vcodec::status decode(vcodec::core::reader auto& r, BigId&) {
        return std::unexpected(r.error(vcodec::errc::type_mismatch, r.offset()).with_expected("bignum").with_found("a reader without expect_bignum"));
    }
};

TEST_CASE("tags: bignums through a user codec") {
    BigId id{ from_hex("010000000000000000"), false };   // 2^64
    auto bytes = cb::encode(id);
    CHECK(to_hex(bytes) == "c249010000000000000000");
    auto back = cb::decode<BigId>(bytes);
    REQUIRE(back);
    CHECK(back->magnitude == id.magnitude);
    CHECK_FALSE(back->negative);
    auto neg = cb::decode<BigId>(from_hex("c349010000000000000000"));
    REQUIRE(neg);
    CHECK(neg->negative);
    auto not_big = cb::decode<BigId>(from_hex("01"));
    REQUIRE_FALSE(not_big);
    CHECK(not_big.error().code() == vcodec::errc::tag_mismatch);
}

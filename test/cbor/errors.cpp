// C3: the §9.2 hex-window outputs, byte for byte, from real malformed input.
#include <vcodec/cbor.hpp>

#include "cbor_hex.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace vcodec_test;
namespace cb = vcodec::cbor;

namespace {
struct User { int id = 0; std::string name; std::string email; };
struct Doc { std::vector<User> users; };
struct [[=cb::integer_keys]] Claims { std::string issuer; std::string subject; std::int64_t expiry = 0; [[=cb::key(7)]] std::vector<std::byte> cti; };
struct Event { [[=cb::tag(1)]] std::int64_t issued_at = 0; };
struct [[=cb::deterministic]] Signed { [[=cb::key(2)]] int a = 0; [[=cb::key(4)]] int b = 0; };
struct Wrapper { Signed claims; };

template<class T, cb::options O = {}>
vcodec::error fail(std::vector<std::byte> const& in) {
    auto r = cb::decode<T, O>(in);
    if (r) FAIL("decode unexpectedly succeeded");
    return std::move(r.error());
}
}

TEST_CASE("§9.2: expected text string, found unsigned integer") {
    // {"users": [u, u, u, {"id": 7, "name": 42, "email": "a@b.c"}]}; the fourth user begins at
    // byte 80 and its "name" value at byte 90.
    std::string user = "a3" "626964" "0N" "646e616d65" "6175" "6565" "6d61696c" "65" "6140622e63";
    std::string hex = "a1" "6575736572" "73" "84";
    for (char n : {'0', '1', '2'}) { auto u = user; u[7] = n; hex += u; }
    hex += "a3" "626964" "07" "646e616d65" "182a" "6565" "6d61696c" "65" "6140622e63";
    auto in = from_hex(hex);
    auto e = fail<Doc>(in);
    CHECK(e.code() == vcodec::errc::type_mismatch);
    CHECK(e.offset() == 90);
    CHECK(vcodec::render_hex(e, in) ==
        "error: expected text string, found unsigned integer\n"
        "  --> $.users[3].name\n"
        "   |\n"
        "   |  0000_0050:  \xE2\x80\xA6" "a3 62 69 64 07 64 6e 61 6d 65 18 2a 65 65 6d 61 \xE2\x80\xA6\n"
        "   |                                             ^^ ^^\n"
        "   |                                             major type 0 (unsigned integer), value 42\n");
    CHECK(vcodec::render_terse(e) == "error: expected text string, found unsigned integer at $.users[3].name (byte offset 90)");
}

TEST_CASE("§9.2: missing required field with its integer key and the keys present") {
    auto in = from_hex("a3" "02" "63626f62" "04" "1a4d859380" "07" "40");   // subject, expiry, cti — no issuer
    auto e = fail<Claims>(in);
    CHECK(e.code() == vcodec::errc::missing_field);
    CHECK(vcodec::render_hex(e, in) ==
        "error: missing required field 'issuer' (key 1)\n"
        "  --> $\n"
        "   |  map has 3 entries, keys present: 2, 4, 7\n");
}

TEST_CASE("§9.2: tag mismatch") {
    auto in = from_hex("a1" "69" "6973737565645f6174" "c0" "1a514b67b0");
    auto e = fail<Event>(in);
    CHECK(e.code() == vcodec::errc::tag_mismatch);
    CHECK(vcodec::render_hex(e, in) ==
        "error: tag mismatch on 'issued_at'\n"
        "  --> $.issued_at\n"
        "   |  expected tag 1 (epoch datetime), found tag 0 (RFC 3339 string)\n");
}

TEST_CASE("§9.2: non-deterministic encoding") {
    auto in = from_hex("a2" "0401" "0202");
    auto e = fail<Signed>(in);
    CHECK(e.code() == vcodec::errc::non_deterministic);
    CHECK(vcodec::render_hex(e, in) ==
        "error: non-deterministic encoding: map keys out of canonical order\n"
        "  --> $\n"
        "   |  key 2 precedes key 4; Signed is declared [[=cbor::deterministic]]\n");
    // via the option, on a nested member, the path localises it
    constexpr cb::options strict{ .require_deterministic = true };
    auto nested = from_hex("a1" "66636c61696d73" "a2" "0401" "0202");
    auto n = fail<Wrapper, strict>(nested);
    CHECK(vcodec::render_hex(n, nested) ==
        "error: non-deterministic encoding: map keys out of canonical order\n"
        "  --> $.claims\n"
        "   |  key 2 precedes key 4\n");
}

TEST_CASE("§9.2: out of range without a window") {
    struct S { std::int64_t offset = 0; };
    auto in = from_hex("a1" "666f6666736574" "3bffffffffffffffff");
    auto e = fail<S>(in);
    CHECK(vcodec::render_hex(e, in) ==
        "error: value -18446744073709551616 out of range for std::int64_t\n"
        "  --> $.offset\n");
}

TEST_CASE("hex window: syntax errors decode the head byte") {
    auto in = from_hex("83" "01" "1c" "02");
    auto e = fail<std::vector<int>>(in);
    CHECK(e.code() == vcodec::errc::malformed_item);
    CHECK(vcodec::render_hex(e, in) ==
        "error: malformed item: reserved additional information\n"
        "  --> $[1]\n"
        "   |\n"
        "   |  0000_0000:   83 01 1c 02 \n"
        "   |                     ^^\n"
        "   |                     major type 0 (unsigned integer), reserved additional information 28\n");
    auto trunc = from_hex("82" "01");
    auto t = fail<std::vector<int>>(trunc);
    CHECK(t.code() == vcodec::errc::truncated);
    CHECK(vcodec::render_hex(t, trunc).starts_with("error: unexpected end of input\n  --> $\n"));
    auto tagged = from_hex("d9d9f7" "9f" "ff");                       // self-describe tag, then an empty array
    CHECK(cb::decode<std::vector<int>>(tagged).value().empty());
    auto in2 = from_hex("a1" "6161" "f7");
    constexpr cb::options strict_undef{ .undefined_as_null = false };
    struct S { int a = 0; };
    auto u = fail<S, strict_undef>(in2);
    CHECK(vcodec::render_hex(u, in2) ==
        "error: expected integer, found undefined\n"
        "  --> $.a\n"
        "   |\n"
        "   |  0000_0000:   a1 61 61 f7 \n"
        "   |                        ^^\n"
        "   |                        major type 7 (simple/float), undefined\n");
}

TEST_CASE("hex window: every new errc has a headline") {
    for (auto c : { vcodec::errc::malformed_item, vcodec::errc::unsupported_simple_value, vcodec::errc::tag_mismatch,
                    vcodec::errc::non_deterministic, vcodec::errc::indefinite_in_borrowed_string }) {
        vcodec::error e(c, 0);
        e.with_detail("d").with_expected("e").with_found("f").with_context("c");
        CHECK(vcodec::render_terse(e).starts_with("error: "));
        CHECK_FALSE(vcodec::to_string(c).empty());
    }
    CHECK(vcodec::is_syntax_error(vcodec::errc::malformed_item));
    CHECK_FALSE(vcodec::is_syntax_error(vcodec::errc::tag_mismatch));
}

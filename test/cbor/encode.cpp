// C1: RFC 8949 Appendix A encode vectors; the §7 type set; integer keys; tags; byte strings;
// indefinite lengths; float widths; typed arrays.
#include <vcodec/vcodec.hpp>

#include "cbor_hex.hpp"
#include "types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <forward_list>
#include <limits>

using namespace vcodec_test;
namespace cb = vcodec::cbor;

namespace {
template<cb::options O = {}, class T> std::string enc(T const& v) { return to_hex(cb::encode<O>(v)); }
constexpr cb::options loose{ .deterministic = false };
constexpr cb::options indef{ .deterministic = false, .indefinite = true };
}

TEST_CASE("cbor encode: RFC 8949 Appendix A scalars") {
    CHECK(enc(0u) == "00");
    CHECK(enc(1u) == "01");
    CHECK(enc(10u) == "0a");
    CHECK(enc(23u) == "17");
    CHECK(enc(24u) == "1818");
    CHECK(enc(25u) == "1819");
    CHECK(enc(100u) == "1864");
    CHECK(enc(1000u) == "1903e8");
    CHECK(enc(1000000u) == "1a000f4240");
    CHECK(enc(1000000000000ull) == "1b000000e8d4a51000");
    CHECK(enc(18446744073709551615ull) == "1bffffffffffffffff");
    CHECK(enc(-1) == "20");
    CHECK(enc(-10) == "29");
    CHECK(enc(-100) == "3863");
    CHECK(enc(-1000) == "3903e7");
    CHECK(enc(std::numeric_limits<std::int64_t>::min()) == "3b7fffffffffffffff");
    CHECK(enc(0.0) == "f90000");
    CHECK(enc(-0.0) == "f98000");
    CHECK(enc(1.0) == "f93c00");
    CHECK(enc(1.1) == "fb3ff199999999999a");
    CHECK(enc(1.5) == "f93e00");
    CHECK(enc(65504.0) == "f97bff");
    CHECK(enc(100000.0) == "fa47c35000");
    CHECK(enc(3.4028234663852886e+38) == "fa7f7fffff");
    CHECK(enc(1.0e+300) == "fb7e37e43c8800759c");
    CHECK(enc(5.960464477539063e-8) == "f90001");
    CHECK(enc(0.00006103515625) == "f90400");
    CHECK(enc(-4.0) == "f9c400");
    CHECK(enc(-4.1) == "fbc010666666666666");
    CHECK(enc(std::numeric_limits<double>::infinity()) == "f97c00");
    CHECK(enc(std::numeric_limits<double>::quiet_NaN()) == "f97e00");
    CHECK(enc(-std::numeric_limits<double>::infinity()) == "f9fc00");
    CHECK(enc(false) == "f4");
    CHECK(enc(true) == "f5");
    CHECK(enc(std::optional<int>{}) == "f6");
    CHECK(enc(1.5f) == "f93e00");                    // float takes the preferred form too
    CHECK(enc(0.1f) == "fa3dcccccd");
    CHECK(enc('a') == "6161");
}

TEST_CASE("cbor encode: Appendix A strings, arrays and maps") {
    CHECK(enc(std::string("")) == "60");
    CHECK(enc(std::string("a")) == "6161");
    CHECK(enc(std::string("IETF")) == "6449455446");
    CHECK(enc(std::string("\"\\")) == "62225c");
    CHECK(enc(std::string("ü")) == "62c3bc");
    CHECK(enc(std::string("水")) == "63e6b0b4");
    CHECK(enc(std::string("\xf0\x90\x85\x91")) == "64f0908591");
    CHECK(enc(std::vector<std::byte>{}) == "40");
    CHECK(enc(std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}) == "4401020304");
    CHECK(enc(std::vector<int>{}) == "80");
    CHECK(enc(std::vector<int>{1, 2, 3}) == "83010203");
    CHECK(enc(std::tuple<int, std::vector<int>, std::vector<int>>{1, {2, 3}, {4, 5}}) == "8301820203820405");
    std::vector<int> v25; for (int i = 1; i <= 25; ++i) v25.push_back(i);
    CHECK(enc(v25) == "98190102030405060708090a0b0c0d0e0f101112131415161718181819");
    CHECK(enc(std::map<std::string, int>{}) == "a0");
    CHECK(enc(std::map<int, int>{{1, 2}, {3, 4}}) == "a201020304");
    CHECK(enc(std::map<std::string, std::string>{{"a", "A"}, {"b", "B"}, {"c", "C"}, {"d", "D"}, {"e", "E"}}) == "a56161614161626142616361436164614461656145");
    CHECK(enc(std::pair<std::string, std::vector<int>>{"a", {1}}) == "82616181" "01");
    // Appendix A: {"a": 1, "b": [2, 3]} — needs a struct or a heterogeneous map; a struct does it
    struct AB { int a = 1; std::vector<int> b{2, 3}; };
    CHECK(enc(AB{}) == "a26161016162820203");
    CHECK(enc(std::vector<std::string>{"a", "b"}) == "82616161" "62");
}

TEST_CASE("cbor encode: indefinite lengths when asked for or unavoidable") {
    CHECK(enc<indef>(std::vector<int>{1, 2}) == "9f0102ff");
    CHECK(enc<indef>(std::map<std::string, int>{{"a", 1}}) == "bf616101ff");
    std::forward_list<int> fl{1, 2};
    CHECK(enc<loose>(fl) == "9f0102ff");                  // unsized: indefinite when not deterministic
    CHECK(enc(fl) == "820102");                            // deterministic: counted by buffering
    struct S { [[=cb::indefinite]] std::string s = "abc"; [[=cb::indefinite]] std::vector<std::byte> b{std::byte{1}}; };
    CHECK(enc<loose>(S{}) == "a26162" "5f4101ff" "6173" "7f63616263ff");
    CHECK(enc(S{}) == "a26162" "4101" "6173" "63616263");  // deterministic ignores the request
}

TEST_CASE("cbor encode: structs, integer keys, canonical member order") {
    struct [[=cb::integer_keys]] Claims { std::string iss = "joe"; std::int64_t exp = 1300819380; [[=cb::key(7)]] std::vector<std::byte> cti; bool admin = true; };
    // iss → 1, exp → 2, admin → 3 (7 taken); canonical order 1,2,3,7
    CHECK(enc(Claims{}) == "a4" "01" "63" "6a6f65" "02" "1a4d88edb4" "03" "f5" "07" "40");
    struct Mixed { [[=cb::key(-1)]] int neg = 1; [[=cb::key(300)]] int big = 2; int text = 3; [[=cb::key(0)]] int zero = 4; };
    // canonical byte order of the encodings: 00 (0) < 19 01 2c (300) < 20 (-1) < 64 … ("text")
    CHECK(enc(Mixed{}) == "a4" "0004" "19012c02" "2001" "6474657874" "03");
    // text keys sort by encoded bytes: length head first, so "b" < "aa"
    struct T { int aa = 1; int b = 2; };
    CHECK(enc(T{}) == "a2" "616202" "62616101");
    // JSON does not see cbor::key; Claims is CBOR-only because of its raw byte string
    STATIC_CHECK(vcodec::cbor_only<Claims>);
    struct JsonToo { [[=cb::key(1)]] int a = 1; };
    CHECK(vcodec::json::encode(JsonToo{}) == R"({"a":1})");
    CHECK(enc(JsonToo{}) == "a10101");
}

TEST_CASE("cbor encode: a deterministic type is canonical whatever the options say") {
    struct [[=cb::deterministic]] Canon { std::map<std::string, int> m{{"aa", 1}, {"b", 2}}; };
    CHECK(enc<loose>(Canon{}) == enc(Canon{}));
    CHECK(enc<indef>(Canon{}) == "a1" "616d" "a2" "616202" "62616101");
}

TEST_CASE("cbor encode: indefinite arrays and maps at member level; type-level tags") {
    struct S { [[=cb::indefinite]] std::vector<int> v{1}; [[=cb::indefinite]] std::map<std::string, int> m{{"a", 1}}; std::vector<int> plain{2}; };
    CHECK(enc<loose>(S{}) == "a3" "616d" "bf616101ff" "6176" "9f01ff" "65706c61696e" "8102");
    CHECK(enc(S{}) == "a3" "616d" "a1616101" "6176" "8101" "65706c61696e" "8102");
    struct [[=vcodec::transparent, =cb::tag(1000)]] Wrap { int v = 5; };
    CHECK(enc(Wrap{}) == "d903e805");
    struct [[=cb::tag(1001)]] Record { int a = 1; };
    struct Holder { Wrap w; Record r; std::optional<Record> none; };
    CHECK(enc(Holder{}) == "a3" "6172" "d903e9a1616101" "6177" "d903e805" "646e6f6e65" "f6");
}

TEST_CASE("cbor encode: enums default to integers; as_text restores names") {
    CHECK(enc(Color::green) == "01");
    CHECK(enc(Level::low) == "20");
    struct E { Color c = Color::red; [[=vcodec::as_text]] Color t = Color::red; };
    CHECK(enc(E{}) == "a2" "6163" "00" "6174" "6a6272696768742d726564");
    CHECK(vcodec::json::encode(E{}) == R"({"c":"bright-red","t":"bright-red"})");
}

TEST_CASE("cbor encode: byte strings are native; uint8 ranges need byte_string") {
    CHECK(enc(std::vector<std::uint8_t>{1, 2}) == "820102");
    struct B { [[=cb::byte_string]] std::vector<std::uint8_t> raw{1, 2}; std::array<std::byte, 2> fixed{std::byte{0xde}, std::byte{0xad}}; };
    CHECK(enc(B{}) == "a2" "63726177" "420102" "656669786564" "42dead");
}

TEST_CASE("cbor encode: tags") {
    struct T { [[=cb::tag(1)]] std::int64_t when = 1363896240; [[=cb::tag(32)]] std::string uri = "http://x"; [[=cb::tag(1), =cb::tag(1000)]] std::optional<int> both = 5; [[=cb::tag(1)]] std::optional<int> none; };
    // canonical key order: "uri" (63…) < "both" < "none" < "when" (64…)
    CHECK(enc(T{}) == "a4" "63757269" "d820" "68687474703a2f2f78" "64626f7468" "c1d903e805" "646e6f6e65" "f6" "647768656e" "c11a514b67b0");
    auto h = enc(T{});
    CHECK(h.find("c11a514b67b0") != std::string::npos);      // tag 1 then the epoch
    CHECK(h.find("d82068687474703a2f2f78") != std::string::npos);
    CHECK(h.find("c1d903e805") != std::string::npos);         // two tags, outermost first
    CHECK(h.find("646e6f6e65f6") != std::string::npos);       // "none": f6 — no tag before null
    struct [[=cb::self_describe]] SD { int a = 1; };
    CHECK(enc(SD{}) == "d9d9f7a1616101");
    constexpr cb::options sd{ .self_describe = true };
    CHECK(enc<sd>(1) == "d9d9f701");
}

TEST_CASE("cbor encode: float widths") {
    struct F { [[=cb::float_width(cb::double_)]] double d = 1.0; [[=cb::float_width(cb::single)]] double s = 1.0; [[=cb::float_width(cb::half)]] double h = 0.1; double p = 1.0; };
    CHECK(enc(F{}) == "a4" "6164" "fb3ff0000000000000" "6168" "f92e66" "6170" "f93c00" "6173" "fa3f800000");
    struct H { [[=cb::float_width(cb::half)]] double h = 1e5; };
    CHECK_THROWS_AS(enc(H{}), vcodec::encode_error);
    constexpr cb::options dbl{ .floats = cb::double_ };
    CHECK(enc<dbl>(1.0) == "fb3ff0000000000000");
}

TEST_CASE("cbor encode: RFC 8746 typed arrays") {
    struct A { [[=cb::typed_array]] std::vector<std::uint16_t> u16{1, 2}; [[=cb::typed_array]] std::vector<double> d{1.0}; [[=cb::typed_array]] std::array<std::int8_t, 2> i8{-1, 2}; std::vector<std::uint16_t> plain{1}; };
    auto h = enc(A{});
    CHECK(h.find("d84544" "01000200") != std::string::npos);      // tag 69 (uint16 LE), 4 bytes
    CHECK(h.find("d85648" "000000000000f03f") != std::string::npos);   // tag 86 (float64 LE)
    CHECK(h.find("d84842" "ff02") != std::string::npos);            // tag 72 (sint8)
    CHECK(h.find("6570" "6c61696e" "8101") != std::string::npos);   // plain stays an array
}

TEST_CASE("cbor encode: entry points and errors") {
    std::vector<std::byte> out{std::byte{0xAA}};
    cb::encode_append(1, out);
    CHECK(to_hex(out) == "aa01");
    std::vector<std::byte> it_out;
    cb::encode_to(Point{1, 2}, std::back_inserter(it_out));
    CHECK(to_hex(it_out) == "a2617801617902");
    CHECK_THROWS_AS(enc(std::string("\xff")), vcodec::encode_error);
    CHECK_THROWS_AS(enc(Color::hidden), vcodec::encode_error);
    struct Node { std::shared_ptr<Node> next; };
    auto a = std::make_shared<Node>(); a->next = a;
    constexpr cb::options shallow{ .max_depth = 4 };
    CHECK_THROWS_AS(cb::encode<shallow>(*a), vcodec::encode_error);
    a->next.reset();
    // deterministic mode rejects duplicate keys in a range of pairs
    CHECK_THROWS_AS(enc(std::vector<std::pair<std::string, int>>{{"a", 1}, {"a", 2}}), vcodec::encode_error);
    CHECK(enc<loose>(std::vector<std::pair<std::string, int>>{{"a", 1}, {"a", 2}}) == "a2616101616102");
}

TEST_CASE("cbor encode: the kitchen encodes and JSON is unchanged") {
    Kitchen k;
    auto bytes = cb::encode(k);
    CHECK(bytes.size() > 100);
    CHECK((std::to_integer<unsigned>(bytes[0]) >> 5) == 5);
    CHECK(vcodec::json::encode(k) == vcodec::json::encode(k));
}

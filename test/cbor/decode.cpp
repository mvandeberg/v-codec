// C4: Appendix A decode vectors; the §7 type set; integer keys; borrowing; tags; typed arrays;
// undefined; options.
#include <vcodec/cbor.hpp>

#include "cbor_hex.hpp"
#include "types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace vcodec_test;
namespace cb = vcodec::cbor;

namespace {
template<class T, cb::options O = {}>
T ok(std::string_view hex) {
    auto b = from_hex(hex);
    auto r = cb::decode<T, O>(b);
    if (!r) FAIL("decode failed: " << vcodec::render_terse(r.error()));
    return std::move(*r);
}
template<class T, cb::options O = {}>
vcodec::error err(std::string_view hex) {
    auto b = from_hex(hex);
    auto r = cb::decode<T, O>(b);
    if (r) FAIL("decode unexpectedly succeeded");
    return std::move(r.error());
}
std::string path_of(vcodec::error const& e) { std::string s; vcodec::detail::render_path(s, e.path(), e.path_truncated()); return s; }
}

TEST_CASE("cbor decode: Appendix A scalars") {
    CHECK(ok<unsigned>("00") == 0);
    CHECK(ok<unsigned>("17") == 23);
    CHECK(ok<unsigned>("1818") == 24);
    CHECK(ok<unsigned>("1903e8") == 1000);
    CHECK(ok<std::uint64_t>("1bffffffffffffffff") == 18446744073709551615ull);
    CHECK(ok<int>("20") == -1);
    CHECK(ok<int>("3863") == -100);
    CHECK(ok<std::int64_t>("3b7fffffffffffffff") == std::numeric_limits<std::int64_t>::min());
    CHECK(ok<double>("f90000") == 0.0);
    CHECK(std::signbit(ok<double>("f98000")));
    CHECK(ok<double>("f93c00") == 1.0);
    CHECK(ok<double>("fb3ff199999999999a") == 1.1);
    CHECK(ok<double>("f97bff") == 65504.0);
    CHECK(ok<double>("fa47c35000") == 100000.0);
    CHECK(ok<double>("f90001") == 5.960464477539063e-8);
    CHECK(ok<double>("fbc010666666666666") == -4.1);
    CHECK(std::isinf(ok<double>("f97c00")));
    CHECK(std::isnan(ok<double>("f97e00")));
    CHECK(std::isinf(ok<double>("fa7f800000")));
    CHECK(std::isnan(ok<double>("fb7ff8000000000000")));
    CHECK(ok<double>("01") == 1.0);                              // integers widen to real
    CHECK(ok<double>("20") == -1.0);
    CHECK(ok<float>("f93e00") == 1.5f);
    CHECK(ok<bool>("f4") == false);
    CHECK(ok<bool>("f5") == true);
    CHECK_FALSE(ok<std::optional<int>>("f6").has_value());
    CHECK_FALSE(ok<std::optional<int>>("f7").has_value());        // undefined as null by default
    CHECK(ok<char>("6161") == 'a');
}

TEST_CASE("cbor decode: strictness and ranges") {
    CHECK(err<int>("f93c00").code() == vcodec::errc::type_mismatch);     // 1.0 is not an integer
    auto neg = err<unsigned>("20");
    CHECK(neg.code() == vcodec::errc::out_of_range);
    CHECK(neg.detail() == "-1");
    CHECK(neg.context() == "std::uint32_t");
    auto big = err<std::int64_t>("1b8000000000000000");
    CHECK(big.code() == vcodec::errc::out_of_range);
    auto huge_neg = err<std::int64_t>("3bffffffffffffffff");
    CHECK(huge_neg.code() == vcodec::errc::out_of_range);
    CHECK(huge_neg.detail() == "-18446744073709551616");
    CHECK(err<std::uint8_t>("190100").code() == vcodec::errc::out_of_range);
    auto tm = err<int>("6161");
    CHECK(tm.code() == vcodec::errc::type_mismatch);
    CHECK(tm.expected() == "integer");
    CHECK(tm.found() == "text string");
    CHECK(err<std::string>("01").found() == "unsigned integer");
    CHECK(err<std::string>("20").found() == "negative integer");
    CHECK(err<std::string>("f5").found() == "boolean");
    CHECK(err<std::string>("f6").found() == "null");
    constexpr cb::options strict_undef{ .undefined_as_null = false };
    auto u = err<std::optional<int>, strict_undef>("f7");
    CHECK(u.code() == vcodec::errc::type_mismatch);
    CHECK(u.found() == "undefined");
}

TEST_CASE("cbor decode: strings, bytes, borrowing, chunks") {
    CHECK(ok<std::string>("6449455446") == "IETF");
    CHECK(ok<std::string>("62c3bc") == "ü");
    CHECK(ok<std::string>("7f657374726561646d696e67ff") == "streaming");       // indefinite chunks
    CHECK(ok<std::vector<std::byte>>("4401020304") == std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
    CHECK(ok<std::vector<std::byte>>("5f42010243030405ff") == std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}});
    auto buf = from_hex("a2" "6173" "63616263" "6162" "42dead");
    struct B { std::string_view s; std::span<const std::byte> b; };
    auto b = cb::decode<B>(buf);
    REQUIRE(b);
    CHECK(b->s == "abc");
    CHECK(b->s.data() == reinterpret_cast<const char*>(buf.data()) + 4);     // aliases the input
    CHECK(b->b.size() == 2);
    CHECK(b->b.data() == buf.data() + 10);
    // indefinite strings cannot be borrowed
    auto chunked = from_hex("a2" "6173" "7f6161ff" "6162" "42dead");
    auto c = cb::decode<B>(chunked);
    REQUIRE_FALSE(c);
    CHECK(c.error().code() == vcodec::errc::escape_in_borrowed_string);
    auto chunked_bytes = from_hex("a2" "6173" "6161" "6162" "5f41deff");
    auto d = cb::decode<B>(chunked_bytes);
    REQUIRE_FALSE(d);
    CHECK(d.error().code() == vcodec::errc::indefinite_in_borrowed_string);
    CHECK(err<std::string>("61ff").code() == vcodec::errc::invalid_utf8);
    CHECK(err<std::string>("62edA080").code() == vcodec::errc::invalid_utf8);
    constexpr cb::options lax{ .validate_utf8 = false };
    CHECK(ok<std::string, lax>("61ff") == "\xff");
    CHECK(err<std::string>("7f4101ff").code() == vcodec::errc::malformed_item);     // byte chunk in a text string
    CHECK(err<std::string>("7f7f6161ffff").code() == vcodec::errc::malformed_item); // nested indefinite chunk
    CHECK(err<std::string>("63616").code() == vcodec::errc::truncated);
}

TEST_CASE("cbor decode: containers") {
    CHECK(ok<std::vector<int>>("80") == std::vector<int>{});
    CHECK(ok<std::vector<int>>("83010203") == std::vector<int>{1, 2, 3});
    CHECK(ok<std::vector<int>>("9f010203ff") == std::vector<int>{1, 2, 3});
    CHECK(ok<std::tuple<int, std::vector<int>, std::vector<int>>>("8301820203820405") == std::tuple<int, std::vector<int>, std::vector<int>>{1, {2, 3}, {4, 5}});
    CHECK(ok<std::map<int, int>>("a201020304") == std::map<int, int>{{1, 2}, {3, 4}});
    CHECK(ok<std::map<std::string, std::string>>("bf6161614161626142ff") == std::map<std::string, std::string>{{"a", "A"}, {"b", "B"}});
    CHECK(ok<std::map<int, std::string>>("a2" "20" "616d" "07" "6173") == std::map<int, std::string>{{-1, "m"}, {7, "s"}});   // native negative keys
    CHECK(ok<std::map<std::string, int>>("a1" "6131" "05") == std::map<std::string, int>{{"1", 5}});
    CHECK(ok<std::array<int, 2>>("820102") == std::array<int, 2>{1, 2});
    CHECK(err<std::array<int, 2>>("8101").code() == vcodec::errc::type_mismatch);
    CHECK(ok<std::set<int>>("83030102") == std::set<int>{1, 2, 3});
    CHECK(ok<ring<int>>("8109").v == std::vector<int>{9});
    CHECK(err<std::vector<std::uint8_t>>("8201ff").code() == vcodec::errc::malformed_item);
    auto el = err<std::vector<std::uint8_t>>("82011901f4");
    CHECK(el.code() == vcodec::errc::out_of_range);
    CHECK(path_of(el) == "$[1]");
    // a map key that is not text or integer
    auto fk = err<std::map<std::string, int>>("a1" "f5" "01");
    CHECK(fk.code() == vcodec::errc::type_mismatch);
    // duplicate policy applies to keyed containers
    constexpr cb::options strict{ .duplicates = vcodec::duplicate_key::error };
    CHECK(err<std::map<int, int>, strict>("a2" "0101" "0102").code() == vcodec::errc::duplicate_key);
    CHECK(ok<std::map<int, int>>("a2" "0101" "0102") == std::map<int, int>{{1, 2}});
}

TEST_CASE("cbor decode: structs with integer keys, text keys, and aliases") {
    struct [[=cb::integer_keys]] Claims { std::string iss; std::int64_t exp = 0; [[=cb::key(7)]] std::vector<std::byte> cti{}; bool admin = false; };
    auto c = ok<Claims>("a4" "01" "636a6f65" "02" "1a4d88edb4" "03" "f5" "07" "4401020304");
    CHECK(c.iss == "joe"); CHECK(c.exp == 1300819380); CHECK(c.admin); CHECK(c.cti.size() == 4);
    // any key order on read
    auto c2 = ok<Claims>("a2" "03" "f5" "01" "6161");
    CHECK(c2.admin); CHECK(c2.iss == "a");
    // the text name is not accepted for an integer-keyed member unless text_key_alias
    struct Strict { [[=cb::key(1)]] int a = 0; };
    struct [[=vcodec::deny_unknown_fields]] StrictDeny { [[=cb::key(1)]] int a = 0; };
    CHECK(ok<Strict>("a1" "6161" "05").a == 0);                                  // ignored
    auto den = err<StrictDeny>("a1" "6161" "05");
    CHECK(den.code() == vcodec::errc::unknown_field);
    CHECK(den.detail() == "a");
    struct Alias { [[=cb::key(1), =cb::text_key_alias]] int a = 0; };
    CHECK(ok<Alias>("a1" "6161" "05").a == 5);
    CHECK(ok<Alias>("a1" "01" "06").a == 6);
    // unknown integer key under deny renders with the key
    auto unk = err<StrictDeny>("a1" "09" "05");
    CHECK(unk.code() == vcodec::errc::unknown_field);
    CHECK(unk.detail() == "9");
    CHECK(path_of(unk) == "$[9]");
    // missing required member names its key
    struct Req { [[=cb::key(1)]] int a; [[=cb::key(2)]] int b; };
    auto m = err<Req>("a1" "0105");
    CHECK(m.code() == vcodec::errc::missing_field);
    CHECK(m.detail() == "b");
    CHECK(m.expected() == "2");
    CHECK(m.found() == "1");                       // one entry present
    REQUIRE(m.candidates().size() == 1);
    CHECK(m.candidates()[0] == "1");
    // paths speak in member terms
    struct Outer { [[=cb::key(4)]] Req r; };
    auto deep = err<Outer>("a1" "04" "a2" "01" "6178" "0202");
    CHECK(path_of(deep) == "$.r.a");
}

TEST_CASE("cbor decode: enums") {
    CHECK(ok<Color>("01") == Color::green);
    CHECK(ok<Level>("20") == Level::low);
    struct E { Color c = Color::red; [[=vcodec::as_text]] Color t = Color::red; };
    auto e = ok<E>("a2" "6163" "01" "6174" "6567726565" "6e");
    CHECK(e.c == Color::green); CHECK(e.t == Color::green);
    auto bad = err<Color>("05");
    CHECK(bad.code() == vcodec::errc::unknown_enumerator);
    CHECK(bad.candidates()[0] == "0");
    auto skipped = err<Color>("02");                // hidden is skipped
    CHECK(skipped.code() == vcodec::errc::unknown_enumerator);
}

TEST_CASE("cbor decode: tags") {
    struct T { [[=cb::tag(1)]] std::int64_t when = 0; std::optional<int> plain; [[=cb::tag(2)]] std::optional<int> maybe; };
    auto t = ok<T>("a3" "647768656e" "c11a514b67b0" "65706c61696e" "d82a05" "656d61796265" "f6");
    CHECK(t.when == 1363896240);
    CHECK(t.plain == 5);                            // unknown tag 42 ignored by default
    CHECK_FALSE(t.maybe);
    auto missing = err<T>("a1" "647768656e" "1a514b67b0");
    CHECK(missing.code() == vcodec::errc::tag_mismatch);
    CHECK(missing.expected() == "tag 1 (epoch datetime)");
    CHECK(missing.found() == "no tag");
    CHECK(missing.context() == "when");
    auto wrong = err<T>("a1" "647768656e" "c01a514b67b0");
    CHECK(wrong.found() == "tag 0 (RFC 3339 string)");
    constexpr cb::options no_ignore{ .ignore_unknown_tags = false };
    auto unk = err<T, no_ignore>("a1" "65706c61696e" "d82a05");
    CHECK(unk.code() == vcodec::errc::tag_mismatch);
    CHECK(unk.expected() == "no tag");
    CHECK(ok<int>("d9d9f701") == 1);                // self-describe tag skipped at the top
    CHECK(ok<int>("d82a" "d82b" "01") == 1);        // nested unknown tags skipped
}

TEST_CASE("cbor decode: typed arrays, plainly") {
    struct A { [[=cb::typed_array]] std::vector<std::uint16_t> u16; std::vector<double> d; std::vector<std::int32_t> i32; std::array<float, 2> f{}; };
    // u16 as a typed array (tag 69, LE), d as a plain array, i32 from a uint16 LE typed array
    // (converted), f from a float32 LE typed array
    auto a = ok<A>("a4" "6164" "81f93e00" "6166" "d855" "48" "0000803f" "00000040" "63693332" "d845" "44" "0100" "0200" "63753136" "d845" "44" "0100" "0200");
    CHECK(a.u16 == std::vector<std::uint16_t>{1, 2});
    CHECK(a.d == std::vector<double>{1.5});
    CHECK(a.i32 == std::vector<std::int32_t>{1, 2});
    CHECK(a.f == std::array<float, 2>{1.0f, 2.0f});
    // the annotated member also accepts a plain array
    auto b = ok<A>("a4" "6164" "80" "6166" "82f93c00f94000" "63693332" "80" "63753136" "820102");
    CHECK(b.u16 == std::vector<std::uint16_t>{1, 2});
    CHECK(b.f == std::array<float, 2>{1.0f, 2.0f});
    // big-endian variants are accepted
    auto c = ok<A>("a4" "6164" "80" "6166" "82f93c00f94000" "63693332" "80" "63753136" "d841" "44" "0001" "0002");
    CHECK(c.u16 == std::vector<std::uint16_t>{1, 2});
    // width mismatch: bytes not a multiple of the element
    CHECK(err<A>("a4" "6164" "80" "6166" "82f93c00f94000" "63693332" "80" "63753136" "d845" "43" "010002").code() == vcodec::errc::malformed_item);
    // out of range when converting
    CHECK(err<A>("a4" "6164" "80" "6166" "82f93c00f94000" "63693332" "80" "63753136" "d846" "44" "00000100").code() == vcodec::errc::out_of_range);
    constexpr cb::options off{ .accept_typed_arrays = false };
    CHECK(err<A, off>("a4" "6164" "80" "6166" "82f93c00f94000" "63693332" "80" "63753136" "d845" "44" "01000200").code() == vcodec::errc::type_mismatch);
}

TEST_CASE("cbor decode: well-formedness errors") {
    CHECK(err<int>("").code() == vcodec::errc::truncated);
    CHECK(err<int>("19").code() == vcodec::errc::truncated);
    CHECK(err<std::vector<int>>("8201").code() == vcodec::errc::truncated);
    CHECK(err<std::vector<int>>("9f01").code() == vcodec::errc::truncated);
    CHECK(err<int>("1c").code() == vcodec::errc::malformed_item);
    CHECK(err<int>("ff").code() == vcodec::errc::malformed_item);
    CHECK(err<std::vector<int>>("82ff01").code() == vcodec::errc::malformed_item);
    CHECK(err<int>("f0").code() == vcodec::errc::unsupported_simple_value);       // simple 16
    CHECK(err<int>("f820").code() == vcodec::errc::unsupported_simple_value);     // simple 32 (two-byte form)
    CHECK(err<int>("f810").code() == vcodec::errc::unsupported_simple_value);     // simple 16 in two bytes: ill-formed
    CHECK(err<int>("0102").code() == vcodec::errc::trailing_content);
    auto tc = err<int>("0102");
    CHECK(tc.offset() == 1);
    constexpr cb::options shallow{ .max_depth = 2 };
    CHECK(err<std::vector<std::vector<std::vector<int>>>, shallow>("818180").code() == vcodec::errc::depth_exceeded);
    CHECK(ok<std::vector<std::vector<int>>, shallow>("8180").size() == 1);
    struct S { int a = 0; };
    CHECK(err<S, shallow>("a1" "6178" "8180").code() == vcodec::errc::depth_exceeded);   // skip_value obeys the same limit
    CHECK(ok<S, shallow>("a1" "6178" "80").a == 0);
    // a tag with no content, and a tag before a break
    CHECK(err<std::vector<int>>("9fc1ff").code() == vcodec::errc::malformed_item);
    CHECK(err<S>("a1" "6178" "c1").code() == vcodec::errc::truncated);
}

TEST_CASE("cbor decode: collect mode, decode_into, positions") {
    struct Cfg { std::uint8_t a = 0; std::string b; Coord p{}; int c = 0; };
    auto all = cb::decode_all<Cfg>(from_hex("a4" "6161" "190100" "6162" "01" "6170" "a2" "6178" "6161" "6179" "02" "6163" "05"));
    CHECK_FALSE(all.complete());
    CHECK(all.value.c == 5);
    CHECK(all.value.p.y == 2);
    REQUIRE(all.errors.size() == 3);
    CHECK(path_of(all.errors[0]) == "$.a");
    CHECK(path_of(all.errors[1]) == "$.b");
    CHECK(path_of(all.errors[2]) == "$.p.x");
    for (auto const& e : all.errors) CHECK_FALSE(e.position().has_value());   // binary: no line/column
    Point p{9, 9};
    CHECK(cb::decode_into(p, from_hex("a2" "6178" "01" "6179" "02")));
    CHECK(p == Point{1, 2});
}

TEST_CASE("cbor decode: the kitchen round-trips and JSON output is byte-identical to before") {
    Kitchen k;
    k.sptr = std::make_shared<std::string>("sp");
    k.uptr = std::make_unique<int>(5);
    auto bytes = cb::encode(k);
    auto back = cb::decode<Kitchen>(bytes);
    REQUIRE(back);
    CHECK(back->s == "str"); CHECK(*back->sptr == "sp"); CHECK(*back->uptr == 5);
    CHECK(back->color == Color::green); CHECK(back->shape == k.shape); CHECK(back->server == k.server);
    CHECK(cb::encode(*back) == bytes);
}

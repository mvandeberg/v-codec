// M6: decode correct for the §7 type set, plus options (duplicates, depth, UTF-8, collect).
#include <vcodec/json.hpp>

#include "types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace vcodec_test;
namespace js = vcodec::json;

namespace {
template<class T, js::options O = {}>
T ok(std::string_view in) {
    auto r = js::decode<T, O>(in);
    if (!r) FAIL("decode failed: " << vc::render_terse(r.error()));
    return std::move(*r);
}
template<class T, js::options O = {}>
vc::error err(std::string_view in) {
    auto r = js::decode<T, O>(in);
    if (r) FAIL("decode unexpectedly succeeded");
    return std::move(r.error());
}
}

TEST_CASE("json decode: scalars") {
    CHECK(ok<bool>("true") == true);
    CHECK(ok<bool>(" false ") == false);
    CHECK(ok<int>("42") == 42);
    CHECK(ok<int>("-42") == -42);
    CHECK(ok<std::uint64_t>("18446744073709551615") == 18446744073709551615ull);
    CHECK(ok<std::int64_t>("-9223372036854775808") == std::numeric_limits<std::int64_t>::min());
    CHECK(ok<double>("1.5") == 1.5);
    CHECK(ok<double>("1e2") == 100.0);
    CHECK(ok<double>("-0") == 0.0);
    CHECK(ok<double>("7") == 7.0);
    CHECK(ok<double>("1E+2") == 100.0);
    CHECK(ok<float>("0.5") == 0.5f);
    CHECK(ok<char>("\"c\"") == 'c');
    CHECK(ok<std::string>("\"hello\"") == "hello");
    CHECK(ok<std::u8string>("\"\\u00e9\"") == u8"\u00e9");
    CHECK(ok<Color>("\"bright-red\"") == Color::red);
    CHECK(ok<Level>("-1") == Level::low);
    CHECK(ok<std::optional<int>>("null") == std::nullopt);
    CHECK(ok<std::optional<int>>("3") == 3);
    CHECK(*ok<std::unique_ptr<std::string>>("\"u\"") == "u");
}

TEST_CASE("json decode: integer strictness and range") {
    auto e = err<int>("1.0");
    CHECK(e.code() == vc::errc::type_mismatch);
    CHECK(e.expected() == "integer");
    CHECK(e.found() == "number");
    CHECK(err<int>("1e2").code() == vc::errc::type_mismatch);
    auto big = err<std::uint64_t>("18446744073709551616");
    CHECK(big.code() == vc::errc::out_of_range);
    CHECK(big.detail() == "18446744073709551616");
    CHECK(big.context() == "std::uint64_t");
    auto neg = err<unsigned>("-1");
    CHECK(neg.code() == vc::errc::out_of_range);
    CHECK(neg.detail() == "-1");
    CHECK(neg.context() == "std::uint32_t");
    auto i64 = err<std::int64_t>("9223372036854775808");
    CHECK(i64.code() == vc::errc::out_of_range);
    auto small = err<std::int8_t>("200");
    CHECK(small.code() == vc::errc::out_of_range);
    CHECK(small.context() == "std::int8_t");
    CHECK(ok<double>("18446744073709551616") == 18446744073709551616.0);   // integer literal too big for 64 bits → real
    CHECK(err<double>("1e400").code() == vc::errc::out_of_range);
    CHECK(ok<double>("1e-400") == 0.0);                                    // underflow is zero, not an error
}

TEST_CASE("json decode: strings, escapes, borrowing") {
    CHECK(ok<std::string>(R"("a\"b\\c\/d")") == "a\"b\\c/d");
    CHECK(ok<std::string>(R"("\b\f\n\r\t")") == "\b\f\n\r\t");
    CHECK(ok<std::string>(R"("\u0041\u00e9\u20ac")") == "A\xc3\xa9\xe2\x82\xac");
    CHECK(ok<std::string>(R"("\ud83d\ude00")") == "\xf0\x9f\x98\x80");   // surrogate pair
    CHECK(ok<std::string>("\"\xe2\x9c\x93\"") == "\xe2\x9c\x93");        // raw UTF-8 passes through
    CHECK(ok<std::string>("\"\"") == "");
    std::string doc = R"({"v":"borrowed"})";
    struct S { std::string_view v; };
    auto s = ok<S>(doc);
    CHECK(s.v == "borrowed");
    CHECK(s.v.data() == doc.data() + 6);                                  // aliases the input
    auto e = err<S>(R"({"v":"esc\n"})");
    CHECK(e.code() == vc::errc::escape_in_borrowed_string);
    CHECK(e.context() == "S::v");
    CHECK(e.suggestion() == "use std::string instead of std::string_view");
    CHECK(err<std::string>(R"("\x")").code() == vc::errc::invalid_escape);
    CHECK(err<std::string>(R"("\u12")").code() == vc::errc::invalid_escape);
    CHECK(err<std::string>(R"("\ud800")").code() == vc::errc::invalid_escape);      // lone surrogate
    CHECK(err<std::string>(R"("\udc00")").code() == vc::errc::invalid_escape);
    CHECK(err<std::string>("\"abc").code() == vc::errc::unterminated_string);
    CHECK(err<std::string>("\"a\nb\"").code() == vc::errc::unexpected_token);   // raw control character
    CHECK(err<std::string>("\"\xff\"").code() == vc::errc::invalid_utf8);
    CHECK(err<std::string>("\"\xed\xa0\x80\"").code() == vc::errc::invalid_utf8); // encoded surrogate
    constexpr js::options lax{ .validate_utf8 = false };
    CHECK(ok<std::string, lax>("\"\xff\"") == "\xff");
}

TEST_CASE("json decode: containers") {
    CHECK(ok<std::vector<int>>("[]") == std::vector<int>{});
    CHECK(ok<std::vector<int>>("[1, 2 ,3]") == std::vector<int>{1, 2, 3});
    CHECK(ok<std::list<std::string>>(R"(["a","b"])") == std::list<std::string>{"a", "b"});
    CHECK(ok<std::set<int>>("[3,1,2]") == std::set<int>{1, 2, 3});
    CHECK(ok<std::deque<double>>("[0.5]") == std::deque<double>{0.5});
    CHECK(ok<ring<int>>("[9]").v == std::vector<int>{9});
    CHECK(ok<std::array<int, 2>>("[1,2]") == std::array<int, 2>{1, 2});
    CHECK(ok<std::pair<std::string, int>>(R"(["p",1])") == std::pair<std::string, int>{"p", 1});
    CHECK(ok<std::tuple<int, bool>>("[1,true]") == std::tuple<int, bool>{1, true});
    CHECK(ok<std::map<std::string, int>>(R"({"a":1,"b":2})") == std::map<std::string, int>{{"a", 1}, {"b", 2}});
    CHECK(ok<std::map<std::string, int>>("{}") == std::map<std::string, int>{});
    CHECK(ok<std::unordered_map<std::string, std::vector<int>>>(R"({"n":[1]})") == std::unordered_map<std::string, std::vector<int>>{{"n", {1}}});
    CHECK(ok<std::vector<std::pair<std::string, int>>>(R"({"k":1})") == std::vector<std::pair<std::string, int>>{{"k", 1}});
    CHECK(ok<std::vector<std::uint8_t>>("[1,255]") == std::vector<std::uint8_t>{1, 255});
    CHECK(ok<std::vector<std::vector<int>>>("[[1],[],[2,3]]") == std::vector<std::vector<int>>{{1}, {}, {2, 3}});
    // integral keys arrive as strings and are parsed strictly (see the stringify_keys case)
    // arrays: trailing comma, missing comma, wrong closer
    CHECK(err<std::vector<int>>("[1,]").code() == vc::errc::unexpected_token);
    CHECK(err<std::vector<int>>("[1 2]").code() == vc::errc::unexpected_token);
    CHECK(err<std::vector<int>>("[1}").code() == vc::errc::unexpected_token);
    CHECK(err<std::vector<int>>("[1").code() == vc::errc::truncated);
    CHECK(err<std::array<int, 2>>("[1]").code() == vc::errc::type_mismatch);
}

namespace {
struct IntKeyed {
    [[=js::stringify_keys]] std::map<int, std::string> by_id;
    [[=js::stringify_keys]] std::map<std::uint8_t, int> small;
};
}

TEST_CASE("json decode: stringify_keys parses decimal keys strictly") {
    auto v = ok<IntKeyed>(R"({"by_id":{"-1":"m","7":"s"},"small":{"255":1}})");
    CHECK(v.by_id == std::map<int, std::string>{{-1, "m"}, {7, "s"}});
    CHECK(v.small == std::map<std::uint8_t, int>{{255, 1}});
    auto bad = err<IntKeyed>(R"({"by_id":{"x":1},"small":{}})");
    CHECK(bad.code() == vc::errc::type_mismatch);
    CHECK(bad.expected() == "integer key");
    auto range = err<IntKeyed>(R"({"by_id":{},"small":{"300":1}})");
    CHECK(range.code() == vc::errc::out_of_range);
    CHECK(range.context() == "std::uint8_t");
}

TEST_CASE("json decode: bytes lowerings") {
    struct B {
        [[=js::bytes(js::base64)]]    std::vector<std::byte> b64;
        [[=js::bytes(js::base64url)]] std::vector<std::uint8_t> url;
        [[=js::bytes(js::hex)]]       std::array<std::byte, 2> hex{};
    };
    auto b = ok<B>(R"({"b64":"aGk=","url":"-_-_","hex":"DEad"})");
    CHECK(b.b64 == std::vector<std::byte>{std::byte{'h'}, std::byte{'i'}});
    CHECK(b.url == std::vector<std::uint8_t>{0xfb, 0xff, 0xbf});
    CHECK(b.hex == std::array<std::byte, 2>{std::byte{0xde}, std::byte{0xad}});
    CHECK(ok<B>(R"({"b64":"aGk","url":"","hex":"0000"})").b64.size() == 2);   // unpadded accepted
    auto e = err<B>(R"({"b64":"not base64!","url":"","hex":"0000"})");
    CHECK(e.code() == vc::errc::type_mismatch);
    CHECK(e.expected() == "base64 string");
    CHECK(err<B>(R"({"b64":"","url":"","hex":"xyz"})").expected() == "hex string");
}

TEST_CASE("json decode: as_string integers accept both forms") {
    struct A { [[=js::as_string]] std::uint64_t u = 0; [[=js::as_string]] std::int64_t i = 0; std::uint64_t plain = 0; };
    auto a = ok<A>(R"({"u":"9007199254740993","i":"-5","plain":1})");
    CHECK(a.u == 9007199254740993ull);
    CHECK(a.i == -5);
    CHECK(ok<A>(R"({"u":7,"i":8,"plain":1})").u == 7);
    CHECK(err<A>(R"({"u":"abc","i":0,"plain":1})").code() == vc::errc::type_mismatch);
    CHECK(err<A>(R"({"u":"-1","i":0,"plain":1})").code() == vc::errc::out_of_range);
    CHECK(err<A>(R"({"u":0,"i":0,"plain":"1"})").code() == vc::errc::type_mismatch);   // plain does not accept strings
}

TEST_CASE("json decode: structs, order independence, defaults, aliases, unknown fields") {
    CHECK(ok<Point>(R"({"y":2,"x":1})") == Point{1, 2});
    CHECK(ok<Point>(R"({"x":1,"y":2,"z":{"deep":[1,2,{"a":null}]}})") == Point{1, 2});   // unknown skipped, nested
    auto s = ok<Server>(R"({"host-name":"h","x":1,"allowed-origins":[],"retry":9,"tls":true,"base_id":4})");
    CHECK(s.host_name == "h"); CHECK(s.retries == 9); CHECK(s.secure); CHECK(s.base_id == 4); CHECK(s.inner.y == 2);
    auto d = ok<Server>(R"({"host-name":"h","x":1,"allowed-origins":[]})");
    CHECK(d.retries == 3);
    std::string_view typo = R"({"host-name":"h","x":1,"allowed-origins":[],"hostname":"z"})";
    auto u = err<Server>(typo);
    CHECK(u.code() == vc::errc::unknown_field);
    CHECK(u.detail() == "hostname");
    CHECK(u.suggestion() == "host-name");
    CHECK(u.offset() == typo.find("\"hostname\""));
    CHECK(ok<UserId>("42").id == 42);
    CHECK(ok<Shape>(R"({"value":{"kind":"circle","radius":2}})") == Shape{Circle{2}});
    CHECK(ok<Shape>(R"({"value":{"w":1,"h":2,"kind":"rect"}})") == Shape{Rect{1, 2}});
    CHECK(ok<Shape>(R"({"value":{"kind":"int","value":7}})") == Shape{7});
}

TEST_CASE("json decode: duplicate key policies") {
    std::string_view in = R"({"x":1,"x":2,"y":0})";
    CHECK(ok<Point>(in).x == 2);
    constexpr js::options first{ .duplicates = vc::duplicate_key::first_wins };
    CHECK(ok<Point, first>(in).x == 1);
    constexpr js::options strict{ .duplicates = vc::duplicate_key::error };
    auto e = err<Point, strict>(in);
    CHECK(e.code() == vc::errc::duplicate_key);
    CHECK(e.detail() == "x");
    CHECK(e.offset() == 7);
}

TEST_CASE("json decode: duplicate key policy applies to maps with unique keys") {
    std::string_view in = R"({"a":1,"a":2,"b":3})";
    CHECK(ok<std::map<std::string, int>>(in) == std::map<std::string, int>{{"a", 2}, {"b", 3}});          // last_wins
    constexpr js::options first{ .duplicates = vc::duplicate_key::first_wins };
    CHECK(ok<std::map<std::string, int>, first>(in) == std::map<std::string, int>{{"a", 1}, {"b", 3}});
    constexpr js::options strict{ .duplicates = vc::duplicate_key::error };
    auto e = err<std::unordered_map<std::string, int>, strict>(in);
    CHECK(e.code() == vc::errc::duplicate_key);
    CHECK(e.detail() == "a");
    CHECK(e.offset() == 7);
    // a range of pairs is not keyed: every entry is kept, in order
    CHECK(ok<std::vector<std::pair<std::string, int>>, strict>(in) == std::vector<std::pair<std::string, int>>{{"a", 1}, {"a", 2}, {"b", 3}});
}

TEST_CASE("json decode: required wins over default_value") {
    struct R { [[=vc::required, =vc::default_value(3)]] int n; [[=vc::default_value(4)]] int m; };
    CHECK(ok<R>(R"({"n":1})").m == 4);
    auto e = err<R>(R"({"m":1})");
    CHECK(e.code() == vc::errc::missing_field);
    CHECK(e.detail() == "n");
}

TEST_CASE("json decode: the collect option on decode surfaces the first collected error") {
    constexpr js::options collect{ .errors = vc::error_mode::collect };
    struct C { std::uint8_t a = 0; std::string b; };
    auto e = err<C, collect>(R"({"a":300,"b":1})");
    CHECK(e.code() == vc::errc::out_of_range);
    C c;
    auto st = js::decode_into<collect>(c, R"({"a":1,"b":1})");
    REQUIRE_FALSE(st);
    CHECK(st.error().code() == vc::errc::type_mismatch);
    CHECK(c.a == 1);
}

TEST_CASE("json decode: as_integer enums list numeric candidates") {
    auto e = err<Level>("5");
    CHECK(e.code() == vc::errc::unknown_enumerator);
    REQUIRE(e.candidates().size() == 3);
    CHECK(e.candidates()[0] == "-1");
    CHECK(e.candidates()[2] == "1");
}

TEST_CASE("json decode: depth limit") {
    constexpr js::options shallow{ .max_depth = 3 };
    CHECK(ok<std::vector<std::vector<std::vector<int>>>, shallow>("[[[1]]]").size() == 1);
    auto e = err<std::vector<std::vector<std::vector<std::vector<int>>>>, shallow>("[[[[1]]]]");
    CHECK(e.code() == vc::errc::depth_exceeded);
    CHECK(e.offset() == 3);
    CHECK(e.detail() == "3");
    // skip_value obeys the same limit for unknown content
    struct S { int a = 0; };
    CHECK(err<S, shallow>(R"({"u":[[[1]]],"a":1})").code() == vc::errc::depth_exceeded);
    CHECK(ok<S, shallow>(R"({"u":[[1]],"a":1})").a == 1);
}

TEST_CASE("json decode: trailing content and whitespace") {
    CHECK(ok<int>("  7  \n") == 7);
    auto e = err<int>("7 8");
    CHECK(e.code() == vc::errc::trailing_content);
    CHECK(e.offset() == 2);
    CHECK(err<Point>(R"({"x":1,"y":2} x)").code() == vc::errc::trailing_content);
    CHECK(err<int>("").code() == vc::errc::truncated);
    CHECK(err<int>("   ").code() == vc::errc::truncated);
    CHECK(err<Point>("{\"x\":1,\"y\":2").code() == vc::errc::truncated);
    CHECK(err<bool>("tru").code() == vc::errc::truncated);
    CHECK(err<bool>("trux").code() == vc::errc::unexpected_token);
    CHECK(err<int>("\f7").code() == vc::errc::unexpected_token);      // form feed is not JSON whitespace
}

TEST_CASE("json decode: decode_into reuses the object and clears containers") {
    Point p{9, 9};
    CHECK(js::decode_into(p, R"({"x":1,"y":2})"));
    CHECK(p == Point{1, 2});
    std::vector<int> v{1, 2, 3};
    CHECK(js::decode_into(v, "[4]"));
    CHECK(v == std::vector<int>{4});
    auto bad = js::decode_into(p, "[1]");
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code() == vc::errc::type_mismatch);
}

TEST_CASE("json decode: collect-all reports every member error and keeps going") {
    struct Cfg { std::uint8_t a = 0; std::string b; Coord p{}; int c = 0; std::vector<int> v; };
    auto r = js::decode_all<Cfg>(R"({"a":300,"b":1,"p":{"x":"no","y":2},"c":5,"v":[1,"x"]})");
    CHECK_FALSE(r.complete());
    CHECK(r.value.c == 5);
    CHECK(r.value.p.y == 2);
    REQUIRE(r.errors.size() == 4);
    auto path = [](vc::error const& e) { std::string s; vc::detail::render_path(s, e.path(), e.path_truncated()); return s; };
    CHECK(path(r.errors[0]) == "$.a");   CHECK(r.errors[0].code() == vc::errc::out_of_range);
    CHECK(path(r.errors[1]) == "$.b");   CHECK(r.errors[1].code() == vc::errc::type_mismatch);
    CHECK(path(r.errors[2]) == "$.p.x"); CHECK(r.errors[2].code() == vc::errc::type_mismatch);
    CHECK(path(r.errors[3]) == "$.v[1]"); CHECK(r.errors[3].code() == vc::errc::type_mismatch);   // array element: recovered at the member `v`
    for (auto const& e : r.errors) CHECK(e.position().has_value());

    auto ok_all = js::decode_all<Point>(R"({"x":1,"y":2})");
    CHECK(ok_all.complete());
    CHECK(ok_all.value == Point{1, 2});

    // syntax errors are fatal and come last
    auto syntax = js::decode_all<Cfg>(R"({"a":300,"b":)");
    REQUIRE(syntax.errors.size() == 2);
    CHECK(syntax.errors[0].code() == vc::errc::out_of_range);
    CHECK(syntax.errors[1].code() == vc::errc::truncated);

    // missing required members are collected too
    auto missing = js::decode_all<Coord>(R"({"x":1})");
    REQUIRE(missing.errors.size() == 1);
    CHECK(missing.errors[0].code() == vc::errc::missing_field);
}

TEST_CASE("json decode: positions carry line and column") {
    auto e = err<Point>("{\n  \"x\": 1,\n  \"y\": \"two\"\n}");
    REQUIRE(e.position().has_value());
    CHECK(e.position()->line == 3);
    CHECK(e.position()->column == 8);
    CHECK(e.offset() == 19);
}

TEST_CASE("json decode: the whole kitchen round-trips through text") {
    Kitchen k;
    k.sptr = std::make_shared<std::string>("sp");
    k.uptr = std::make_unique<int>(5);
    k.opt_point = Point{3, 4};
    k.server.host_name = "h"; k.server.inner.x = 2;
    auto text = js::encode(k);
    auto back = js::decode<Kitchen>(text);
    REQUIRE(back);
    CHECK(back->i8 == -8); CHECK(back->u64 == 64); CHECK(back->f == 1.5f); CHECK(back->d == 2.5);
    CHECK(back->s == "str"); CHECK(back->u8s == u8"u8"); CHECK(back->letter == 'k');
    CHECK(back->color == Color::green); CHECK(back->level == Level::high);
    CHECK_FALSE(back->opt_none); CHECK(back->opt_some == 7);
    CHECK(*back->uptr == 5); CHECK(*back->sptr == "sp");
    CHECK(back->vec == k.vec); CHECK(back->lst == k.lst); CHECK(back->dq == k.dq); CHECK(back->st == k.st);
    CHECK(back->custom == k.custom); CHECK(back->arr == k.arr); CHECK(back->carr[1] == 8);
    CHECK(back->pr == k.pr); CHECK(back->tp == k.tp);
    CHECK(back->smap == k.smap); CHECK(back->pairs == k.pairs);
    CHECK(back->numbers == k.numbers); CHECK(back->point == k.point); CHECK(back->shape == k.shape);
    CHECK(back->user == k.user); CHECK(back->server == k.server); CHECK(back->points == k.points);
    CHECK(back->opt_point == k.opt_point); CHECK(back->nested == k.nested);
    CHECK(js::encode(*back) == text);
}

// M4: encode correct for the §7 type set; keys precomputed; pretty printing; lowerings.
#include <vcodec/json.hpp>

#include "types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <limits>

using namespace vcodec_test;
namespace js = vcodec::json;

TEST_CASE("json encode: scalars") {
    CHECK(js::encode(true) == "true");
    CHECK(js::encode(false) == "false");
    CHECK(js::encode(0) == "0");
    CHECK(js::encode(-42) == "-42");
    CHECK(js::encode(std::uint64_t(18446744073709551615ull)) == "18446744073709551615");   // > 2^53 emitted exactly
    CHECK(js::encode(std::int64_t(INT64_MIN)) == "-9223372036854775808");
    CHECK(js::encode(1.5) == "1.5");
    CHECK(js::encode(0.1) == "0.1");
    CHECK(js::encode(1e300) == "1e+300");
    CHECK(js::encode(-0.0) == "-0");
    CHECK(js::encode(1.0f / 3.0f) == "0.3333333432674408");   // float widened to double, shortest for the double
    CHECK(js::encode(std::numeric_limits<double>::quiet_NaN()) == "null");
    CHECK(js::encode(std::numeric_limits<double>::infinity()) == "null");
    CHECK(js::encode(3.0L) == "3");
    CHECK(js::encode('x') == "\"x\"");
    CHECK(js::encode(std::string("s")) == "\"s\"");
    CHECK(js::encode(std::string_view("v")) == "\"v\"");
    CHECK(js::encode(std::u8string(u8"é")) == "\"\xc3\xa9\"");
}

TEST_CASE("json encode: string escaping") {
    CHECK(js::encode(std::string("a\"b\\c")) == R"("a\"b\\c")");
    CHECK(js::encode(std::string("\n\r\t\b\f")) == R"("\n\r\t\b\f")");
    CHECK(js::encode(std::string("\x01\x1f")) == "\"\\u0001\\u001F\"");
    CHECK(js::encode(std::string("/ unicode: \xe2\x9c\x93")) == "\"/ unicode: \xe2\x9c\x93\"");   // no escaping of '/' or non-ASCII
    CHECK(js::encode(std::string("\x7f")) == "\"\x7f\"");                                  // DEL is not a control char in JSON
    CHECK_THROWS_AS(js::encode(std::string("bad \xff utf8")), vcodec::encode_error);
    CHECK_THROWS_AS(js::encode(std::string("\xed\xa0\x80")), vcodec::encode_error);         // surrogate
    CHECK_THROWS_AS(js::encode(std::string("\xc0\x80")), vcodec::encode_error);             // overlong
    constexpr js::options no_validate{ .validate_utf8 = false };
    CHECK(js::encode<no_validate>(std::string("\xff")) == "\"\xff\"");
    // key escaping goes through the same rules, precomputed
    struct Weird { [[=vc::name("a\"b\n")]] int v = 1; };
    CHECK(js::encode(Weird{}) == R"({"a\"b\n":1})");
}

TEST_CASE("json encode: containers") {
    CHECK(js::encode(std::vector<int>{}) == "[]");
    CHECK(js::encode(std::vector<int>{1, 2, 3}) == "[1,2,3]");
    CHECK(js::encode(std::array<int, 2>{1, 2}) == "[1,2]");
    CHECK(js::encode(std::pair<std::string, int>{"a", 1}) == R"(["a",1])");
    CHECK(js::encode(std::tuple<int, bool, std::string>{1, true, "x"}) == R"([1,true,"x"])");
    CHECK(js::encode(std::map<std::string, int>{}) == "{}");
    CHECK(js::encode(std::map<std::string, int>{{"a", 1}, {"b", 2}}) == R"({"a":1,"b":2})");
    CHECK(js::encode(std::vector<std::vector<int>>{{1}, {}, {2, 3}}) == "[[1],[],[2,3]]");
    CHECK(js::encode(std::optional<int>{}) == "null");
    CHECK(js::encode(std::optional<std::vector<int>>{{1}}) == "[1]");
    CHECK(js::encode(ring<int>{{4, 5}}) == "[4,5]");
    CHECK(js::encode(std::vector<std::uint8_t>{1, 2}) == "[1,2]");   // numbers, not bytes
}

TEST_CASE("json encode: structs") {
    CHECK(js::encode(Point{1, 2}) == R"({"x":1,"y":2})");
    Server s; s.host_name = "example.org"; s.inner.x = 1; s.allowed_origins = {"a", "b"};
    CHECK(js::encode(s) == R"({"base_id":0,"host-name":"example.org","port":8080,"tls":false,"allowed-origins":["a","b"],"x":1,"why":2,"retries":0,"read-only":5})");
    s.cert_path = "/etc/cert"; s.zero_if_unset = 3;
    CHECK(js::encode(s) == R"({"base_id":0,"host-name":"example.org","port":8080,"tls":false,"cert-path":"/etc/cert","allowed-origins":["a","b"],"x":1,"why":2,"retries":0,"zero-if-unset":3,"read-only":5})");
    CHECK(js::encode(UserId{42}) == "42");
    CHECK(js::encode(Shape{Circle{1.5}}) == R"({"value":{"kind":"circle","radius":1.5}})");
    CHECK(js::encode(Shape{7}) == R"({"value":{"kind":"int","value":7}})");
    CHECK(js::encode(Color::red) == "\"bright-red\"");
    CHECK(js::encode(Level::low) == "-1");
    CHECK(js::encode(std::vector<Point>{{1, 1}, {2, 2}}) == R"([{"x":1,"y":1},{"x":2,"y":2}])");
}

TEST_CASE("json encode: pretty printing") {
    constexpr js::options pretty{ .pretty = true };
    CHECK(js::encode<pretty>(Point{1, 2}) == "{\n  \"x\": 1,\n  \"y\": 2\n}");
    CHECK(js::encode<pretty>(std::vector<int>{}) == "[]");
    CHECK(js::encode<pretty>(std::map<std::string, int>{}) == "{}");
    CHECK(js::encode<pretty>(std::vector<Point>{{1, 1}}) == "[\n  {\n    \"x\": 1,\n    \"y\": 1\n  }\n]");
    constexpr js::options tabs4{ .pretty = true, .indent = 4 };
    CHECK(js::encode<tabs4>(std::vector<int>{1}) == "[\n    1\n]");
    struct Nested { std::vector<int> v{1}; std::optional<int> o; };
    CHECK(js::encode<pretty>(Nested{}) == "{\n  \"v\": [\n    1\n  ],\n  \"o\": null\n}");
}

namespace {
struct Lowered {
    [[=js::bytes(js::base64)]]    std::vector<std::byte> b64{std::byte{'h'}, std::byte{'i'}};
    [[=js::bytes(js::base64url)]] std::vector<std::uint8_t> url{0xfb, 0xff, 0xbf};
    [[=js::bytes(js::hex)]]       std::array<std::byte, 2> hex{std::byte{0xde}, std::byte{0xad}};
    [[=js::as_string]]            std::uint64_t big = 9007199254740993ull;
    [[=js::as_string]]            std::int64_t neg = -5;
    [[=js::stringify_keys]]       std::map<int, std::string> by_id{{-1, "m"}, {7, "s"}};
    [[=js::as_string]]            std::vector<std::uint64_t> all_big{1, 2};
};
}

TEST_CASE("json encode: lowerings — bytes encodings, as_string, stringify_keys") {
    CHECK(js::encode(Lowered{}) ==
        R"({"b64":"aGk=","url":"-_-_","hex":"dead","big":"9007199254740993","neg":"-5","by_id":{"-1":"m","7":"s"},"all_big":["1","2"]})");
    STATIC_CHECK_FALSE(js::encodable<std::vector<std::byte>>);   // §4.1: bytes without a lowering never guess
}

TEST_CASE("json encode: entry points") {
    std::string out = "prefix:";
    js::encode_append(Point{1, 2}, out);
    CHECK(out == R"(prefix:{"x":1,"y":2})");
    std::string it_out;
    js::encode_to(Point{3, 4}, std::back_inserter(it_out));
    CHECK(it_out == R"({"x":3,"y":4})");
    std::string into;
    js::writer<> w(into);
    js::encode_into(Point{5, 6}, w);
    CHECK(into == R"({"x":5,"y":6})");
}

TEST_CASE("json encode: depth limit turns a cycle into an error naming the path") {
    struct Node { std::shared_ptr<Node> next; int v = 0; };
    auto a = std::make_shared<Node>(); auto b = std::make_shared<Node>();
    a->next = b; b->next = a;
    constexpr js::options shallow{ .max_depth = 8 };
    try { js::encode<shallow>(*a); FAIL("expected throw"); }
    catch (vcodec::encode_error const& e) {
        CHECK(e.code() == vcodec::errc::depth_exceeded);
        CHECK_THAT(e.what(), Catch::Matchers::ContainsSubstring("max_depth"));
        REQUIRE(e.path().size() == 8);
        for (auto const& s : e.path()) CHECK(s.name == "next");
    }
    a->next.reset();   // break the cycle so the shared_ptrs free
}

TEST_CASE("json encode: the whole kitchen produces valid-looking JSON") {
    Kitchen k;
    auto s = js::encode(k);
    CHECK(s.front() == '{');
    CHECK(s.back() == '}');
    CHECK(s.find("\"letter\":\"k\"") != std::string::npos);
    CHECK(s.find("\"color\":\"green\"") != std::string::npos);
    CHECK(s.find("\"level\":1") != std::string::npos);
    CHECK(s.find("\"opt_none\":null") != std::string::npos);
}

namespace {
struct IntKeyed {
    [[=js::stringify_keys]] std::map<std::uint32_t, std::string> imap{{1, "one"}, {2, "two"}};
    [[=js::stringify_keys]] std::map<std::int32_t, int> nmap{{-1, 1}};
};
}

TEST_CASE("json encode: integral keys need stringify_keys and render as decimal strings") {
    CHECK(js::encode(IntKeyed{}) == R"({"imap":{"1":"one","2":"two"},"nmap":{"-1":1}})");
    STATIC_CHECK_FALSE(js::encodable<std::map<int, int>>);
}

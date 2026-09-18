// M6 exit criterion: every §9.4 error produced from real malformed input, at the right
// byte offset, rendering exactly as the spec shows.
#include <vcodec/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vc = vcodec;
namespace js = vcodec::json;

namespace {

struct User { int id = 0; std::string name; std::string email; };
struct [[=vc::deny_unknown_fields]] Server { std::vector<User> users; };
struct Config { std::uint8_t retries = 0; };
struct Settings { Config config; };
enum class Color { red [[=vc::name("bright-red")]], green };
struct Theme { Color accent = Color::green; };
struct Themed { Theme theme; };

// The §9.4 document: users[3] begins at byte 1261 and its "name" value is the number 42 at
// byte 1284. Padding is JSON whitespace so the document stays valid apart from the fault.
std::string users_doc(std::string fourth) {
    std::string doc = "{\"users\":[";
    for (int i = 0; i < 3; ++i) doc += "{\"id\": " + std::to_string(i) + ", \"name\": \"u\", \"email\": \"a@b.c\"},";
    doc.append(1261 - doc.size(), ' ');
    doc += fourth;
    doc += "]}";
    return doc;
}

template<class T>
vc::error fail(std::string_view in) {
    auto r = js::decode<T>(in);
    if (r) FAIL("decode unexpectedly succeeded");
    return std::move(r.error());
}
}

TEST_CASE("§9.4 from input: expected string, found number") {
    auto doc = users_doc("{     \"id\": 7, \"name\": 42, \"email\": \"a@b.c\"}");
    REQUIRE(doc.substr(1284, 2) == "42");
    auto e = fail<Server>(doc);
    CHECK(e.code() == vc::errc::type_mismatch);
    CHECK(e.offset() == 1284);
    CHECK(vc::render_framed(e, doc) ==
        "error: expected string, found number\n"
        "  --> $.users[3].name\n"
        "   |\n"
        "   |  \xE2\x80\xA6\"id\": 7, \"name\": 42, \"email\": \"a@b.c\"\xE2\x80\xA6\n"
        "   |                    ^^ at byte offset 1284\n");
}

TEST_CASE("§9.4 from input: missing required field") {
    auto doc = users_doc("{\"id\": 7, \"name\": \"n\"}");
    auto e = fail<Server>(doc);
    CHECK(e.code() == vc::errc::missing_field);
    CHECK(e.offset() == 1261);
    CHECK(vc::render_framed(e, doc) ==
        "error: missing required field 'email'\n"
        "  --> $.users[3]\n"
        "   |  object begins at byte offset 1261\n");
}

TEST_CASE("§9.4 from input: unknown field with did-you-mean") {
    // deny_unknown_fields is on Server; put the typo on a Server-level key so the note names it.
    struct [[=vc::deny_unknown_fields]] Row { int id = 0; std::string email; };
    struct Doc { std::vector<Row> users; };
    std::string doc = R"({"users":[{"id":1,"email":"x"},{"id":2,"email":"x"},{"id":3,"email":"x"},{"id":4,"emial":"x"}]})";
    auto e = fail<Doc>(doc);
    CHECK(e.code() == vc::errc::unknown_field);
    CHECK(e.offset() == doc.find("\"emial\""));
    CHECK(vc::render_framed(e, doc) ==
        "error: unknown field 'emial'\n"
        "  --> $.users[3].emial\n"
        "   |  did you mean 'email'?\n"
        "   |  (Row is declared [[=deny_unknown_fields]])\n");
}

TEST_CASE("§9.4 from input: value out of range") {
    std::string doc = R"({"config": {"retries": 512}})";
    auto e = fail<Settings>(doc);
    CHECK(e.code() == vc::errc::out_of_range);
    CHECK(e.offset() == doc.find("512"));
    CHECK(vc::render_framed(e, doc) ==
        "error: value 512 out of range for std::uint8_t\n"
        "  --> $.config.retries\n");
}

TEST_CASE("§9.4 from input: unknown enumerator") {
    std::string doc = R"({"theme": {"accent": "BRIGHT_RED"}})";
    auto e = fail<Themed>(doc);
    CHECK(e.code() == vc::errc::unknown_enumerator);
    CHECK(e.offset() == doc.find("\"BRIGHT_RED\""));
    CHECK(vc::render_framed(e, doc) ==
        "error: 'BRIGHT_RED' is not a valid Color\n"
        "  --> $.theme.accent\n"
        "   |  expected one of: bright-red, green\n");
}

TEST_CASE("errors from input: syntax errors point at the offending byte") {
    struct S { std::vector<int> a; };
    auto check = [](std::string_view in, vc::errc code, std::size_t offset) {
        auto e = fail<S>(in);
        CHECK(e.code() == code);
        CHECK(e.offset() == offset);
        return e;
    };
    check(R"({"a": [1, 2,]})", vc::errc::unexpected_token, 12);
    check(R"({"a": [1 2]})", vc::errc::unexpected_token, 9);
    check(R"({"a": [1], })", vc::errc::unexpected_token, 11);
    check(R"({"a" [1]})", vc::errc::unexpected_token, 5);
    check(R"({a: [1]})", vc::errc::unexpected_token, 1);
    check(R"({"a": [1)", vc::errc::truncated, 8);
    check(R"({"a": [01]})", vc::errc::invalid_number, 7);
    check(R"({"a": [1.]})", vc::errc::invalid_number, 7);
    check(R"({"a": [-]})", vc::errc::invalid_number, 7);
    check(R"({"a": [+1]})", vc::errc::unexpected_token, 7);
    check(R"({"a": [1]} tail)", vc::errc::trailing_content, 11);
    auto e = check("{\"a\": [1], \"b\": \"x\ty\"}", vc::errc::unexpected_token, 18);
    CHECK(e.detail() == "control character");
    auto f = check("{\"a\": [1], \"b\": \"\\q\"}", vc::errc::invalid_escape, 17);
    CHECK(f.detail() == "\\q");
    auto g = check("{\"a\": [1], \"b\": \"\xc3(\"}", vc::errc::invalid_utf8, 17);
    (void)g;
    auto t = fail<S>("{\"a\": tru}");
    CHECK(vc::render_framed(t, "{\"a\": tru}") ==
        "error: unexpected token 't', expected array\n"
        "  --> $.a\n"
        "   |\n"
        "   |  \xE2\x80\xA6\"a\": tru\xE2\x80\xA6\n"
        "   |        ^^^ at byte offset 6\n");
}

namespace {
struct A { int a = 0; };
struct B { int b = 0; };
struct [[=vc::tag("type")]] Ev { std::variant<A, B> payload; };
}

TEST_CASE("errors from input: tagged variant diagnostics") {
    auto m = fail<Ev>(R"({"payload": {"a": 1}})");
    CHECK(m.code() == vc::errc::missing_tag);
    CHECK(vc::render_framed(m, "") ==
        "error: missing discriminator 'type'\n"
        "  --> $.payload\n"
        "   |  object begins at byte offset 12\n");
    auto u = fail<Ev>(R"({"payload": {"type": "C", "a": 1}})");
    CHECK(u.code() == vc::errc::no_variant_alternative);
    CHECK(u.offset() == 21);
    CHECK(vc::render_framed(u, "") ==
        "error: 'C' is not an alternative of std::variant<A, B>\n"
        "  --> $.payload.type\n"
        "   |  expected one of: A, B\n");
}

TEST_CASE("errors from input: borrowed string with escapes") {
    struct S { std::string_view v; };
    std::string doc = R"({"v": "a\nb"})";
    auto e = fail<S>(doc);
    CHECK(vc::render_framed(e, doc) ==
        "error: string contains escapes but S::v borrows from the input\n"
        "  --> $.v\n"
        "   |\n"
        "   |  \xE2\x80\xA6\"v\": \"a\\nb\"\xE2\x80\xA6\n"
        "   |        ^^^^^^ at byte offset 6\n"
        "   |  use std::string instead of std::string_view\n");
}

TEST_CASE("errors from input: depth exceeded names the limit") {
    constexpr js::options shallow{ .max_depth = 4 };
    auto r = js::decode<std::vector<std::vector<std::vector<std::vector<std::vector<int>>>>>, shallow>("[[[[[1]]]]]");
    REQUIRE_FALSE(r);
    CHECK(vc::render_framed(r.error(), "[[[[[1]]]]]") ==
        "error: nesting depth exceeds 4\n"
        "  --> $[0][0][0][0]\n"
        "   |\n"
        "   |  \xE2\x80\xA6[1]\xE2\x80\xA6\n"
        "   |   ^ at byte offset 4\n");
}

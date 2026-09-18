// M5: every §9.4 output reproduced exactly, from hand-constructed errors.
#include <vcodec/json.hpp>

#include <catch2/catch_test_macros.hpp>

namespace vc = vcodec;

namespace {
// Build an error the way the reader will: leaf-first path pushes, then finalize.
vc::error make(vc::errc code, std::size_t offset, std::vector<vc::path_step> root_first) {
    vc::error e(code, offset);
    for (auto it = root_first.rbegin(); it != root_first.rend(); ++it) {
        switch (it->kind) {
        case vc::path_step::tag::member:   e.push_member(it->name); break;
        case vc::path_step::tag::index:    e.push_index(it->index); break;
        case vc::path_step::tag::key_text: e.push_key(it->name); break;
        case vc::path_step::tag::key_int:  e.push_key(it->index); break;
        }
    }
    e.finalize();
    return e;
}
vc::path_step member(std::string_view n) { return { vc::path_step::tag::member, n, 0 }; }
vc::path_step index(std::uint64_t i) { return { vc::path_step::tag::index, {}, i }; }
vc::path_step key(std::string_view n) { return { vc::path_step::tag::key_text, n, 0 }; }

// A document where "users"[3]."name" is the number 42 at byte offset 1284 and the object
// begins at 1261 — the offsets §9.4 uses.
std::string document() {
    auto user = [](int id, std::string name, std::string lead = "") {
        return "{" + lead + "\"id\": " + std::to_string(id) + ", \"name\": " + name + ", \"email\": \"a@b.c\"}";
    };
    std::string prefix = "{\"users\":[";
    for (int i = 0; i < 3; ++i) { prefix += user(i, "\"u\""); prefix += ","; }
    std::string pad(1261 - prefix.size(), ' ');
    return prefix + pad + user(7, "42", "     ") + "]}";
}
}

TEST_CASE("§9.4: type mismatch with source excerpt and caret") {
    auto doc = document();
    REQUIRE(doc.substr(1261, 1) == "{");
    REQUIRE(doc.substr(1284, 2) == "42");
    auto e = make(vc::errc::type_mismatch, 1284, { member("users"), index(3), member("name") });
    e.with_expected("string").with_found("number").with_length(2);
    CHECK(vc::render_framed(e, doc) ==
        "error: expected string, found number\n"
        "  --> $.users[3].name\n"
        "   |\n"
        "   |  \xE2\x80\xA6\"id\": 7, \"name\": 42, \"email\": \"a@b.c\"\xE2\x80\xA6\n"
        "   |                    ^^ at byte offset 1284\n");
    CHECK(vc::render_terse(e) == "error: expected string, found number at $.users[3].name (byte offset 1284)");
}

TEST_CASE("§9.4: missing required field") {
    auto e = make(vc::errc::missing_field, 1261, { member("users"), index(3) });
    e.with_detail("email").with_container_offset(1261);
    CHECK(vc::render_framed(e, document()) ==
        "error: missing required field 'email'\n"
        "  --> $.users[3]\n"
        "   |  object begins at byte offset 1261\n");
}

TEST_CASE("§9.4: unknown field with did-you-mean and deny_unknown_fields note") {
    auto e = make(vc::errc::unknown_field, 1290, { member("users"), index(3), key("emial") });
    e.with_detail("emial").with_suggestion("email").with_context("Server").with_deny_unknown();
    CHECK(vc::render_framed(e, document()) ==
        "error: unknown field 'emial'\n"
        "  --> $.users[3].emial\n"
        "   |  did you mean 'email'?\n"
        "   |  (Server is declared [[=deny_unknown_fields]])\n");
    // without a close match, no suggestion line
    auto f = make(vc::errc::unknown_field, 0, { key("zzz") });
    f.with_detail("zzz").with_context("Server").with_deny_unknown();
    CHECK(vc::render_framed(f, "") ==
        "error: unknown field 'zzz'\n"
        "  --> $.zzz\n"
        "   |  (Server is declared [[=deny_unknown_fields]])\n");
}

TEST_CASE("§9.4: out of range") {
    auto e = make(vc::errc::out_of_range, 40, { member("config"), member("retries") });
    e.with_detail("512").with_context("std::uint8_t");
    CHECK(vc::render_framed(e, document()) ==
        "error: value 512 out of range for std::uint8_t\n"
        "  --> $.config.retries\n");
}

TEST_CASE("§9.4: unknown enumerator lists the valid values") {
    auto e = make(vc::errc::unknown_enumerator, 12, { member("theme"), member("accent") });
    e.with_detail("BRIGHT_RED").with_context("Color").with_candidate("bright-red").with_candidate("green");
    CHECK(vc::render_framed(e, document()) ==
        "error: 'BRIGHT_RED' is not a valid Color\n"
        "  --> $.theme.accent\n"
        "   |  expected one of: bright-red, green\n");
}

TEST_CASE("framed: syntax errors show the excerpt; root path is $") {
    std::string doc = "{\"a\": tru}";
    auto e = make(vc::errc::unexpected_token, 6, {});
    e.with_detail("t").with_length(3);
    CHECK(vc::render_framed(e, doc) ==
        "error: unexpected token 't'\n"
        "  --> $\n"
        "   |\n"
        "   |  \xE2\x80\xA6\"a\": tru\xE2\x80\xA6\n"
        "   |        ^^^ at byte offset 6\n");
    // the enclosing brackets are outside the window, so they show as ellipses
    std::string tiny = "[1, tru]";
    auto f = make(vc::errc::unexpected_token, 4, {});
    f.with_detail("t").with_length(3);
    CHECK(vc::render_framed(f, tiny) ==
        "error: unexpected token 't'\n"
        "  --> $\n"
        "   |\n"
        "   |  \xE2\x80\xA6" "1, tru\xE2\x80\xA6\n"
        "   |      ^^^ at byte offset 4\n");
    // a bare scalar document has nothing to cut
    std::string bare = "tru";
    auto g = make(vc::errc::unexpected_token, 0, {});
    g.with_detail("t").with_length(3);
    CHECK(vc::render_framed(g, bare) ==
        "error: unexpected token 't'\n"
        "  --> $\n"
        "   |\n"
        "   |  tru\n"
        "   |  ^^^ at byte offset 0\n");
}

TEST_CASE("framed: excerpt is clipped to the line in multi-line input") {
    std::string doc = "{\n  \"a\": 1,\n  \"b\": \"x\"\n}";
    std::size_t off = doc.find("\"x\"");
    auto e = make(vc::errc::type_mismatch, off, { member("b") });
    e.with_expected("integer").with_found("string").with_length(3);
    CHECK(vc::render_framed(e, doc) ==
        "error: expected integer, found string\n"
        "  --> $.b\n"
        "   |\n"
        "   |  \"b\": \"x\"\n"
        "   |       ^^^ at byte offset " + std::to_string(off) + "\n");
}

TEST_CASE("framed: path rendering covers every step kind, truncation and odd keys") {
    auto e = make(vc::errc::type_mismatch, 0, { member("m"), index(2), key("plain-key"), key("needs quoting"),
                                                 { vc::path_step::tag::key_int, {}, 9 } });
    e.with_expected("x").with_found("y");
    CHECK(vc::render_terse(e) == "error: expected x, found y at $.m[2].plain-key[\"needs quoting\"][9] (byte offset 0)");

    vc::error t(vc::errc::truncated, 5);
    for (int i = 0; i < 20; ++i) t.push_member("n");
    t.finalize();
    CHECK(t.path_truncated());
    CHECK(t.path().size() == 16);
    CHECK(vc::render_terse(t).starts_with("error: unexpected end of input at $.\xE2\x80\xA6[truncated].n.n"));
}

TEST_CASE("framed: every errc has a headline") {
    for (std::uint16_t c = 0; c <= std::uint16_t(vc::errc::non_text_key); ++c) {
        vc::error e(vc::errc(c), 0);
        e.with_detail("d").with_expected("e").with_found("f").with_context("c");
        auto s = vc::render_terse(e);
        CHECK(s.starts_with("error: "));
        CHECK(s.size() > 20);
        CHECK_FALSE(vc::to_string(vc::errc(c)).empty());
    }
    CHECK(vc::is_syntax_error(vc::errc::truncated));
    CHECK_FALSE(vc::is_syntax_error(vc::errc::type_mismatch));
}

TEST_CASE("error: copies are deep and keys stay valid") {
    vc::error e(vc::errc::unknown_field, 3);
    e.push_key(std::string("temporary key"));
    vc::error copy = e;
    e = vc::error(vc::errc::truncated, 0);
    REQUIRE(copy.path().size() == 1);
    CHECK(copy.path()[0].name == "temporary key");
    CHECK(copy.offset() == 3);
    vc::error moved = std::move(copy);
    CHECK(moved.path()[0].name == "temporary key");
}

TEST_CASE("error: position is optional and carried when set") {
    vc::error e(vc::errc::truncated, 10);
    CHECK_FALSE(e.position().has_value());
    e.with_position({ 2, 5 });
    REQUIRE(e.position().has_value());
    CHECK(e.position()->line == 2);
    CHECK(e.position()->column == 5);
}

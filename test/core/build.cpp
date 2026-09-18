// M2: decode-side construction against a token-stream reader. No JSON.
#include <vcodec/core/build.hpp>

#include "token_reader.hpp"
#include "types.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vcodec_test;
using reader = token_reader<>;

namespace {
template<class T, class R = reader>
vc::result<T> dec(std::vector<tok> toks) {
    R r(std::move(toks));
    T out{};
    auto st = vc::core::decode(r, out);
    if (!st) return std::unexpected(std::move(st.error()));
    return out;
}
std::string path_of(vc::error const& e) { std::string s; vc::detail::render_path(s, e.path(), e.path_truncated()); return s; }
}

TEST_CASE("build: scalars with range checks") {
    CHECK(dec<bool>({t_bool(true)}).value() == true);
    CHECK(dec<std::uint8_t>({t_uint(255)}).value() == 255);
    auto over = dec<Narrow>({t_map(), t_key("small"), t_uint(512), t_end_map()});
    REQUIRE_FALSE(over);
    CHECK(over.error().code() == vc::errc::out_of_range);
    CHECK(over.error().detail() == "512");
    CHECK(over.error().context() == "std::uint8_t");        // the declared alias, not unsigned char
    auto neg = dec<Narrow>({t_map(), t_key("wide"), t_sint(-5), t_end_map()});
    REQUIRE_FALSE(neg);
    CHECK(neg.error().code() == vc::errc::out_of_range);
    CHECK(neg.error().context() == "std::uint32_t");
    auto s16 = dec<Narrow>({t_map(), t_key("signed16"), t_uint(70000), t_end_map()});
    REQUIRE_FALSE(s16);
    CHECK(s16.error().context() == "std::int16_t");
    CHECK(s16.error().detail() == "70000");
    auto top = dec<std::uint8_t>({t_uint(512)});
    REQUIRE_FALSE(top);
    CHECK(top.error().context() == "std::uint8_t");
    auto plain = dec<int>({t_uint(std::uint64_t(1) << 40)});
    REQUIRE_FALSE(plain);
    CHECK(plain.error().context() == "int");
    CHECK(dec<std::int8_t>({t_sint(-128)}).value() == -128);
    CHECK_FALSE(dec<std::int8_t>({t_sint(-129)}));
    CHECK(dec<std::int16_t>({t_uint(300)}).value() == 300);
    CHECK(dec<double>({t_real(2.5)}).value() == 2.5);
    CHECK(dec<double>({t_uint(2)}).value() == 2.0);
    CHECK(dec<float>({t_real(0.5)}).value() == 0.5f);
    CHECK(dec<char>({t_text("c")}).value() == 'c');
    auto bad_char = dec<char>({t_text("cc")});
    REQUIRE_FALSE(bad_char);
    CHECK(bad_char.error().code() == vc::errc::type_mismatch);
    CHECK(bad_char.error().expected() == "single-character string");
    auto mism = dec<int>({t_text("x")});
    REQUIRE_FALSE(mism);
    CHECK(mism.error().code() == vc::errc::type_mismatch);
    CHECK(mism.error().expected() == "integer");
    CHECK(mism.error().found() == "text");
}

TEST_CASE("build: strings and borrowing") {
    CHECK(dec<std::string>({t_text("abc")}).value() == "abc");
    CHECK(dec<std::string>({t_text("abc", true)}).value() == "abc");     // owned copies escaped text fine
    CHECK(dec<std::u8string>({t_text("u")}).value() == u8"u");
    {
        // A borrowed view aliases the reader's input, so the reader must outlive the check.
        reader r({t_text("view")});
        std::string_view sv;
        REQUIRE(vc::core::decode(r, sv));
        CHECK(sv == "view");
    }
    auto esc = dec<std::string_view>({t_text("esc", true)});
    REQUIRE_FALSE(esc);
    CHECK(esc.error().code() == vc::errc::escape_in_borrowed_string);
    CHECK(esc.error().suggestion() == "use std::string instead of std::string_view");
}

TEST_CASE("build: enums") {
    CHECK(dec<Color>({t_text("bright-red")}).value() == Color::red);
    CHECK(dec<Color>({t_text("green")}).value() == Color::green);
    auto hidden = dec<Color>({t_text("hidden")});
    REQUIRE_FALSE(hidden);
    CHECK(hidden.error().code() == vc::errc::unknown_enumerator);
    CHECK(hidden.error().detail() == "hidden");
    CHECK(hidden.error().context() == "Color");
    REQUIRE(hidden.error().candidates().size() == 2);
    CHECK(hidden.error().candidates()[0] == "bright-red");
    CHECK(hidden.error().candidates()[1] == "green");
    CHECK(dec<Level>({t_sint(-1)}).value() == Level::low);
    CHECK(dec<Level>({t_uint(1)}).value() == Level::high);
    auto bad = dec<Level>({t_uint(5)});
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code() == vc::errc::unknown_enumerator);
    CHECK(bad.error().detail() == "5");
}

TEST_CASE("build: optional-like") {
    CHECK_FALSE(dec<std::optional<int>>({t_null()}).value().has_value());
    CHECK(dec<std::optional<int>>({t_uint(3)}).value() == 3);
    CHECK(dec<std::unique_ptr<int>>({t_null()}).value() == nullptr);
    CHECK(*dec<std::unique_ptr<int>>({t_uint(4)}).value() == 4);
    CHECK(*dec<std::shared_ptr<std::string>>({t_text("s")}).value() == "s");
}

TEST_CASE("build: sequences, fixed arrays, tuples") {
    CHECK(dec<std::vector<int>>({t_arr(), t_uint(1), t_uint(2), t_end_arr()}).value() == std::vector<int>{1, 2});
    CHECK(dec<std::list<std::string>>({t_arr(), t_text("a"), t_end_arr()}).value() == std::list<std::string>{"a"});
    CHECK(dec<std::set<int>>({t_arr(), t_uint(2), t_uint(1), t_end_arr()}).value() == std::set<int>{1, 2});
    CHECK(dec<ring<int>>({t_arr(), t_uint(9), t_end_arr()}).value().v == std::vector<int>{9});
    CHECK(dec<std::array<int, 2>>({t_arr(), t_uint(1), t_uint(2), t_end_arr()}).value() == std::array<int, 2>{1, 2});
    auto short_arr = dec<std::array<int, 2>>({t_arr(), t_uint(1), t_end_arr()});
    REQUIRE_FALSE(short_arr);
    CHECK(short_arr.error().expected() == "array of 2 elements");
    CHECK(short_arr.error().found() == "array of 1 elements");
    auto long_arr = dec<std::array<int, 2>>({t_arr(), t_uint(1), t_uint(2), t_uint(3), t_end_arr()});
    REQUIRE_FALSE(long_arr);
    CHECK(long_arr.error().found() == "array of 3 elements");
    auto pr = dec<std::pair<std::string, int>>({t_arr(), t_text("p"), t_uint(1), t_end_arr()}).value();
    CHECK(pr == std::pair<std::string, int>{"p", 1});
    auto tp = dec<std::tuple<int, std::string, bool>>({t_arr(), t_uint(1), t_text("t"), t_bool(true), t_end_arr()}).value();
    CHECK(tp == std::tuple<int, std::string, bool>{1, "t", true});
    auto bad_tp = dec<std::tuple<int, bool>>({t_arr(), t_uint(1), t_end_arr()});
    REQUIRE_FALSE(bad_tp);
    CHECK(bad_tp.error().found() == "array of 1 elements");
    // element error paths
    auto bad_el = dec<std::vector<std::uint8_t>>({t_arr(), t_uint(1), t_uint(999), t_end_arr()});
    REQUIRE_FALSE(bad_el);
    CHECK(path_of(bad_el.error()) == "$[1]");
}

TEST_CASE("build: maps with text and integral keys") {
    auto m = dec<std::map<std::string, int>>({t_map(), t_key("a"), t_uint(1), t_key("b"), t_uint(2), t_end_map()}).value();
    CHECK(m == std::map<std::string, int>{{"a", 1}, {"b", 2}});
    auto im = dec<std::map<std::uint32_t, int>>({t_map(), t_uint(7), t_uint(1), t_end_map()}).value();
    CHECK(im == std::map<std::uint32_t, int>{{7, 1}});
    // stringified integral keys are parsed
    auto sm = dec<std::map<int, int>>({t_map(), t_key("-3"), t_uint(1), t_end_map()}).value();
    CHECK(sm == std::map<int, int>{{-3, 1}});
    auto badk = dec<std::map<int, int>>({t_map(), t_key("x"), t_uint(1), t_end_map()});
    REQUIRE_FALSE(badk);
    CHECK(badk.error().expected() == "integer key");
    auto pairs = dec<std::vector<std::pair<std::string, int>>>({t_map(), t_key("k"), t_uint(1), t_end_map()}).value();
    CHECK(pairs == std::vector<std::pair<std::string, int>>{{"k", 1}});
    auto badv = dec<std::map<std::string, std::uint8_t>>({t_map(), t_key("big"), t_uint(300), t_end_map()});
    REQUIRE_FALSE(badv);
    CHECK(path_of(badv.error()) == "$.big");
    auto badv2 = dec<std::map<std::string, std::uint8_t>>({t_map(), t_key("odd key"), t_uint(300), t_end_map()});
    CHECK(path_of(badv2.error()) == "$[\"odd key\"]");
}

TEST_CASE("build: bytes") {
    auto v = dec<std::vector<std::byte>>({t_bytes({std::byte{1}, std::byte{2}})}).value();
    CHECK(v == std::vector<std::byte>{std::byte{1}, std::byte{2}});
    auto a = dec<std::array<std::byte, 2>>({t_bytes({std::byte{1}, std::byte{2}})}).value();
    CHECK(a[1] == std::byte{2});
    auto bad = dec<std::array<std::byte, 3>>({t_bytes({std::byte{1}})});
    REQUIRE_FALSE(bad);
    CHECK(bad.error().expected() == "byte string of length 3");
    // uint8 vectors are arrays of numbers by default
    CHECK(dec<std::vector<std::uint8_t>>({t_arr(), t_uint(1), t_end_arr()}).value() == std::vector<std::uint8_t>{1});
}

TEST_CASE("build: structs — order-independent, unknown ignored by default, defaults applied") {
    auto p = dec<Point>({t_map(), t_key("y"), t_uint(2), t_key("x"), t_uint(1), t_key("z"), t_uint(9), t_end_map()}).value();
    CHECK(p == Point{1, 2});
    // Point's members have default member initialisers, so they are optional (§5.2)
    CHECK(dec<Point>({t_map(), t_end_map()}).value() == Point{0, 0});
    auto empty = dec<Coord>({t_map(), t_end_map()});
    REQUIRE_FALSE(empty);
    CHECK(empty.error().code() == vc::errc::missing_field);
    CHECK(empty.error().detail() == "x");
    CHECK(empty.error().container_offset() == 0);
    CHECK(path_of(empty.error()) == "$");
}

TEST_CASE("build: server — rename_all, alias, flatten, inheritance, deny_unknown_fields, defaults") {
    auto s = dec<Server>({t_map(),
        t_key("host-name"), t_text("h"),
        t_key("tls"), t_bool(true),
        t_key("allowed-origins"), t_arr(), t_text("o"), t_end_arr(),
        t_key("x"), t_uint(1),
        t_key("retry"), t_uint(9),            // alias
        t_key("base_id"), t_uint(4),
        t_key("read-only"), t_uint(77),       // skip_deserializing: known name, value skipped silently
        t_end_map()});
    REQUIRE(s);
    CHECK(s->read_only == 5);
    auto denied = dec<Server>({t_map(), t_key("host-name"), t_text("h"), t_key("x"), t_uint(1),
                               t_key("allowed-origins"), t_arr(), t_end_arr(), t_key("nope"), t_uint(1), t_end_map()});
    REQUIRE_FALSE(denied);
    CHECK(denied.error().code() == vc::errc::unknown_field);
    CHECK(denied.error().detail() == "nope");
    CHECK(denied.error().deny_unknown());
    CHECK(denied.error().context() == "Server");

    auto ok = dec<Server>({t_map(),
        t_key("host-name"), t_text("h"),
        t_key("tls"), t_bool(true),
        t_key("allowed-origins"), t_arr(), t_text("o"), t_end_arr(),
        t_key("x"), t_uint(1),
        t_key("retry"), t_uint(9),
        t_key("base_id"), t_uint(4),
        t_end_map()}).value();
    CHECK(ok.host_name == "h");
    CHECK(ok.secure);
    CHECK(ok.allowed_origins == std::vector<std::string>{"o"});
    CHECK(ok.inner.x == 1);
    CHECK(ok.inner.y == 2);
    CHECK(ok.retries == 9);
    CHECK(ok.base_id == 4);
    CHECK(ok.port == 8080);
    CHECK(ok.read_only == 5);
    CHECK_FALSE(ok.cert_path.has_value());

    auto dflt = dec<Server>({t_map(), t_key("host-name"), t_text("h"), t_key("x"), t_uint(1),
                             t_key("allowed-origins"), t_arr(), t_end_arr(), t_end_map()}).value();
    CHECK(dflt.retries == 3);                  // default_value(3)

    auto typo = dec<Server>({t_map(), t_key("hostname"), t_text("h"), t_end_map()});
    REQUIRE_FALSE(typo);
    CHECK(typo.error().code() == vc::errc::unknown_field);
    CHECK(typo.error().suggestion() == "host-name");
    CHECK(path_of(typo.error()) == "$.hostname");
}

TEST_CASE("build: missing required field names the object path, not the member") {
    struct Outer { Coord p; };
    auto r = dec<Outer>({t_map(), t_key("p"), t_map(), t_key("x"), t_uint(1), t_end_map(), t_end_map()});
    REQUIRE_FALSE(r);
    CHECK(r.error().code() == vc::errc::missing_field);
    CHECK(r.error().detail() == "y");
    CHECK(path_of(r.error()) == "$.p");
    CHECK(r.error().container_offset() == 2);
}

TEST_CASE("build: nested error paths are root-first") {
    struct Row { std::uint8_t n; };
    struct Doc { std::map<std::string, std::vector<Row>> groups; };
    auto r = dec<Doc>({t_map(), t_key("groups"), t_map(), t_key("g"), t_arr(),
                       t_map(), t_key("n"), t_uint(1), t_end_map(),
                       t_map(), t_key("n"), t_uint(300), t_end_map(),
                       t_end_arr(), t_end_map(), t_end_map()});
    REQUIRE_FALSE(r);
    CHECK(path_of(r.error()) == "$.groups.g[1].n");
    CHECK(r.error().offset() == 11);   // token index of the 300
}

TEST_CASE("build: duplicate key policies") {
    std::vector<tok> in{t_map(), t_key("x"), t_uint(1), t_key("x"), t_uint(2), t_key("y"), t_uint(0), t_end_map()};
    CHECK(dec<Point>(in).value().x == 2);                                                    // last_wins
    CHECK(dec<Point, token_reader<vc::duplicate_key::first_wins>>(in).value().x == 1);
    auto err = dec<Point, token_reader<vc::duplicate_key::error>>(in);
    REQUIRE_FALSE(err);
    CHECK(err.error().code() == vc::errc::duplicate_key);
    CHECK(err.error().detail() == "x");
    CHECK(err.error().offset() == 3);
}

TEST_CASE("build: transparent") {
    CHECK(dec<UserId>({t_uint(42)}).value().id == 42);
}

TEST_CASE("build: tagged variants, tag in any position") {
    // Shape is the struct carrying the tag; its member `value` is the variant.
    auto wrap = [](std::vector<tok> inner) {
        std::vector<tok> v{t_map(), t_key("value")};
        v.insert(v.end(), inner.begin(), inner.end());
        v.push_back(t_end_map());
        return v;
    };
    auto c = dec<Shape>(wrap({t_map(), t_key("kind"), t_text("circle"), t_key("radius"), t_real(2), t_end_map()})).value();
    CHECK(c == Shape{Circle{2}});
    auto r = dec<Shape>(wrap({t_map(), t_key("w"), t_real(1), t_key("h"), t_real(2), t_key("kind"), t_text("rect"), t_end_map()})).value();
    CHECK(r == Shape{Rect{1, 2}});
    auto i = dec<Shape>(wrap({t_map(), t_key("value"), t_uint(7), t_key("kind"), t_text("int"), t_end_map()})).value();
    CHECK(i == Shape{7});
    auto missing = dec<Shape>(wrap({t_map(), t_key("radius"), t_real(2), t_end_map()}));
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code() == vc::errc::missing_tag);
    CHECK(missing.error().detail() == "kind");
    CHECK(path_of(missing.error()) == "$.value");
    auto unknown = dec<Shape>(wrap({t_map(), t_key("kind"), t_text("hexagon"), t_end_map()}));
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code() == vc::errc::no_variant_alternative);
    CHECK(unknown.error().detail() == "hexagon");
    REQUIRE(unknown.error().candidates().size() == 3);
    CHECK(unknown.error().candidates()[0] == "circle");
    CHECK(path_of(unknown.error()) == "$.value.kind");
    // a nested error inside the chosen alternative keeps the full path
    auto bad = dec<Shape>(wrap({t_map(), t_key("kind"), t_text("circle"), t_key("radius"), t_text("x"), t_end_map()}));
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code() == vc::errc::type_mismatch);
    CHECK(path_of(bad.error()) == "$.value.radius");
}

TEST_CASE("build: type mismatch found/expected propagate with paths") {
    struct S { std::vector<int> v; };
    auto r = dec<S>({t_map(), t_key("v"), t_text("nope"), t_end_map()});
    REQUIRE_FALSE(r);
    CHECK(r.error().code() == vc::errc::type_mismatch);
    CHECK(r.error().expected() == "array");
    CHECK(r.error().found() == "text");
    CHECK(path_of(r.error()) == "$.v");
}

namespace { struct External { int a; }; }
template<> struct vcodec::codec_for<External, void> {
    static vcodec::result<External> decode(vcodec::core::reader auto& r) {
        auto u = r.expect_uint(); if (!u) return std::unexpected(std::move(u.error()));
        return External{ int(*u / 10) };
    }
};

namespace {
struct Id { int v; };
struct IdCodec {
    static vc::status decode(vc::core::reader auto& r, Id& out) {
        auto t = r.expect_text(); if (!t) return std::unexpected(std::move(t.error()));
        out.v = std::stoi(std::string(t->text.substr(3))); return {};
    }
};
struct Holder { [[=vc::with<IdCodec>]] Id id; };
}

TEST_CASE("build: codec_for and with<Codec> on decode") {
    CHECK(dec<External>({t_uint(40)}).value().a == 4);
    CHECK(dec<Holder>({t_map(), t_key("id"), t_text("id-3"), t_end_map()}).value().id.v == 3);
}

TEST_CASE("build: collect mode recovers at member granularity") {
    using creader = token_reader<vc::duplicate_key::last_wins, vc::error_mode::collect>;
    struct Cfg { std::uint8_t a = 0; std::string b; Point p; int c = 0; };
    creader r({t_map(),
        t_key("a"), t_uint(300),                                   // out of range
        t_key("b"), t_uint(1),                                     // type mismatch
        t_key("p"), t_map(), t_key("x"), t_text("no"), t_key("y"), t_uint(2), t_end_map(),   // nested mismatch, y fine
        t_key("c"), t_uint(5),
        t_end_map()});
    Cfg out{};
    auto st = vc::core::decode(r, out);
    CHECK(st);                                   // no fatal error
    CHECK(out.c == 5);
    CHECK(out.p.y == 2);
    REQUIRE(r.collected().size() == 3);
    CHECK(path_of(r.collected()[0]) == "$.a");
    CHECK(r.collected()[0].code() == vc::errc::out_of_range);
    CHECK(path_of(r.collected()[1]) == "$.b");
    CHECK(path_of(r.collected()[2]) == "$.p.x");
    CHECK(r.collected()[2].code() == vc::errc::type_mismatch);
}

TEST_CASE("build: collect mode records missing fields and unknown fields, and stops on syntax errors") {
    using creader = token_reader<vc::duplicate_key::last_wins, vc::error_mode::collect>;
    creader r({t_map(), t_key("x"), t_uint(1), t_end_map()});
    Coord out{};
    CHECK(vc::core::decode(r, out));
    REQUIRE(r.collected().size() == 1);
    CHECK(r.collected()[0].code() == vc::errc::missing_field);

    creader r2({t_map(), t_key("host-name"), t_text("h"), t_key("bogus"), t_uint(1), t_key("x"), t_uint(1),
                t_key("allowed-origins"), t_arr(), t_end_arr(), t_end_map()});
    Server s{};
    CHECK(vc::core::decode(r2, s));
    REQUIRE(r2.collected().size() == 1);
    CHECK(r2.collected()[0].code() == vc::errc::unknown_field);
    CHECK(s.inner.x == 1);

    creader r3({t_map(), t_key("x"), t_uint(1), t_key("y")});   // truncated
    Coord p{};
    auto st = vc::core::decode(r3, p);
    REQUIRE_FALSE(st);
    CHECK(st.error().code() == vc::errc::truncated);
}

TEST_CASE("build: path truncation keeps the deepest 16 steps") {
    struct N16 { std::vector<N16> kids; std::uint8_t leaf = 0; };
    std::vector<tok> toks;
    const int depth = 20;
    for (int i = 0; i < depth; ++i) { toks.push_back(t_map()); toks.push_back(t_key("kids")); toks.push_back(t_arr()); }
    toks.push_back(t_map()); toks.push_back(t_key("leaf")); toks.push_back(t_uint(999)); toks.push_back(t_end_map());
    for (int i = 0; i < depth; ++i) { toks.push_back(t_end_arr()); toks.push_back(t_end_map()); }
    auto r = dec<N16>(toks);
    REQUIRE_FALSE(r);
    CHECK(r.error().path_truncated());
    CHECK(r.error().path().size() == 16);
    auto p = path_of(r.error());
    CHECK(p.starts_with("$.\xE2\x80\xA6[truncated]"));
    CHECK(p.ends_with(".kids[0].leaf"));
}

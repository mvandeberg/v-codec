// M2: the §7 type set traverses to a recorded token stream matching expectations.
#include <vcodec/core/traverse.hpp>

#include "recording_sink.hpp"
#include "types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace vcodec_test;
using tokens = std::vector<std::string>;

namespace {
template<class T, class S = recording_sink>
tokens rec(T const& v) { S s; vc::core::encode(s, v); return s.tokens; }
}

TEST_CASE("traverse: scalars") {
    CHECK(rec(true) == tokens{"true"});
    CHECK(rec(std::uint8_t{200}) == tokens{"u:200"});
    CHECK(rec(std::int64_t{-5}) == tokens{"i:-5"});
    CHECK(rec(2.5) == tokens{"r:2.5"});
    CHECK(rec(1.5f) == tokens{"r:1.5"});
    CHECK(rec('c') == tokens{"t:c"});
    CHECK(rec(std::string("hi")) == tokens{"t:hi"});
    CHECK(rec(std::string_view("sv")) == tokens{"t:sv"});
    CHECK(rec(std::u8string(u8"u8")) == tokens{"t:u8"});
    const char* cs = "cstr";
    CHECK(rec(cs) == tokens{"t:cstr"});
}

TEST_CASE("traverse: enums") {
    CHECK(rec(Color::red) == tokens{"t:bright-red"});
    CHECK(rec(Color::green) == tokens{"t:green"});
    CHECK(rec(Level::low) == tokens{"i:-1"});
    CHECK_THROWS_AS(rec(Color::hidden), vc::encode_error);
    CHECK_THROWS_WITH(rec(Color::hidden), Catch::Matchers::ContainsSubstring("hidden") && Catch::Matchers::ContainsSubstring("skip"));
    CHECK_THROWS_AS(rec(Color(42)), vc::encode_error);
}

TEST_CASE("traverse: optional-like") {
    CHECK(rec(std::optional<int>{}) == tokens{"null"});
    CHECK(rec(std::optional<int>{3}) == tokens{"i:3"});
    CHECK(rec(std::optional<unsigned>{3}) == tokens{"u:3"});
    CHECK(rec(std::unique_ptr<int>{}) == tokens{"null"});
    CHECK(rec(std::make_shared<std::string>("s")) == tokens{"t:s"});
}

TEST_CASE("traverse: sequences use definite lengths when sized") {
    CHECK(rec(std::vector<int>{1, 2}) == tokens{"[2", "i:1", "i:2", "]"});
    CHECK(rec(std::vector<unsigned>{1, 2}) == tokens{"[2", "u:1", "u:2", "]"});
    CHECK(rec(std::list<int>{1}) == tokens{"[1", "i:1", "]"});
    CHECK(rec(std::set<int>{2, 1}) == tokens{"[2", "i:1", "i:2", "]"});
    CHECK(rec(std::array<int, 2>{1, 2}) == tokens{"[2", "i:1", "i:2", "]"});
    int carr[2] = {3, 4};
    CHECK(rec(carr) == tokens{"[2", "i:3", "i:4", "]"});
    std::vector<int> backing{5};
    CHECK(rec(std::span<const int>(backing)) == tokens{"[1", "i:5", "]"});
    CHECK(rec(ring<int>{{7}}) == tokens{"[1", "i:7", "]"});
    CHECK(rec(std::vector<bool>{true, false}) == tokens{"[2", "true", "false", "]"});
}

TEST_CASE("traverse: tuple-like become arrays") {
    CHECK(rec(std::pair<std::string, int>{"a", 1}) == tokens{"[2", "t:a", "i:1", "]"});
    CHECK(rec(std::tuple<int, bool>{1, true}) == tokens{"[2", "i:1", "true", "]"});
}

TEST_CASE("traverse: maps") {
    CHECK(rec(std::map<std::string, int>{{"a", 1}, {"b", 2}}) == tokens{"{2", "k:a", "i:1", "k:b", "i:2", "}"});
    CHECK(rec(std::map<std::uint32_t, int>{{7, 1}}) == tokens{"{1", "ku:7", "i:1", "}"});
    CHECK(rec(std::map<int, int>{{-7, 1}}) == tokens{"{1", "ki:-7", "i:1", "}"});
    CHECK(rec(std::vector<std::pair<std::string, int>>{{"k", 1}}) == tokens{"{1", "k:k", "i:1", "}"});
    CHECK(rec(std::unordered_map<std::string, int>{{"x", 1}}) == tokens{"{1", "k:x", "i:1", "}"});
}

TEST_CASE("traverse: bytes vs numbers") {
    CHECK(rec(std::vector<std::byte>{std::byte{0xAB}, std::byte{1}}) == tokens{"b:ab01"});
    CHECK(rec(std::array<std::byte, 1>{std::byte{0xFF}}) == tokens{"b:ff"});
    std::vector<std::byte> bb{std::byte{2}};
    CHECK(rec(std::span<const std::byte>(bb)) == tokens{"b:02"});
    // std::vector<std::uint8_t> is an array of numbers unless a format annotation says otherwise
    CHECK(rec(std::vector<std::uint8_t>{1, 2}) == tokens{"[2", "u:1", "u:2", "]"});
}

TEST_CASE("traverse: structs, keys in schema order, definite count when unconditional") {
    CHECK(rec(Point{1, 2}) == tokens{"{2", "k:x", "i:1", "k:y", "i:2", "}"});
}

TEST_CASE("traverse: server exercises rename_all, name, flatten, inheritance, skips") {
    Server s;
    s.host_name = "h"; s.inner.x = 1;
    auto t = rec(s);
    // conditional fields (skip_if_null / skip_if_default) → indefinite count
    CHECK(t.front() == "{?");
    CHECK(t == tokens{"{?",
        "k:base_id", "i:0",
        "k:host-name", "t:h",
        "k:port", "u:8080",
        "k:tls", "false",
        // cert-path omitted: skip_if_null and empty
        "k:allowed-origins", "[0", "]",
        "k:x", "i:1", "k:why", "i:2",              // flattened Inner, Inner's own naming
        "k:retries", "i:0",                        // default_value applies on decode, not construction
        // zero_if_unset omitted: skip_if_default
        // write_only omitted: skip_serializing
        "k:read-only", "i:5",
        "}"});
    s.cert_path = "c"; s.zero_if_unset = 1;
    auto t2 = rec(s);
    CHECK(std::find(t2.begin(), t2.end(), "k:cert-path") != t2.end());
    CHECK(std::find(t2.begin(), t2.end(), "k:zero-if-unset") != t2.end());
}

TEST_CASE("traverse: transparent wrapper encodes as its member") {
    CHECK(rec(UserId{42}) == tokens{"u:42"});
}

TEST_CASE("traverse: tagged variants") {
    // Shape is the struct carrying the tag; its member `value` is the variant.
    // struct alternative: internally tagged, tag first
    CHECK(rec(Shape{Circle{1.0}}) == tokens{"{1", "k:value", "{2", "k:kind", "t:circle", "k:radius", "r:1", "}", "}"});
    CHECK(rec(Shape{Rect{1, 2}}) == tokens{"{1", "k:value", "{3", "k:kind", "t:rect", "k:w", "r:1", "k:h", "r:2", "}", "}"});
    // non-struct alternative: externally tagged
    CHECK(rec(Shape{7}) == tokens{"{1", "k:value", "{2", "k:kind", "t:int", "k:value", "i:7", "}", "}"});
}

TEST_CASE("traverse: the whole kitchen traverses") {
    Kitchen k;
    k.sptr = std::make_shared<std::string>("sp");
    auto t = rec(k);
    CHECK(t.front() == "{39");  // Kitchen itself has no conditional members → definite count
    CHECK(t.back() == "}");
    CHECK(std::count(t.begin(), t.end(), "null") == 3);   // opt_none, uptr, opt_point
}

namespace {
struct Id { int v; };
struct IdCodec { static void encode(vc::core::sink auto& s, Id const& v) { s.text("id-" + std::to_string(v.v)); } };
struct Holder { [[=vc::with<IdCodec>]] Id id; };
}

TEST_CASE("traverse: with<Codec>") {
    CHECK(rec(Holder{{3}}) == tokens{"{1", "k:id", "t:id-3", "}"});
}

namespace { struct External { int a; }; struct Hexy { int v; }; }
template<> struct vcodec::codec_for<External, void> {
    static void encode(vcodec::core::sink auto& s, External const& v) { s.uint(std::uint64_t(v.a) * 10); }
};
template<> struct vcodec::codec_for<Hexy, mock_format> {
    static void encode(vcodec::core::sink auto& s, Hexy const&) { s.text("mock-specific"); }
};
template<> struct vcodec::codec_for<Hexy, void> {
    static void encode(vcodec::core::sink auto& s, Hexy const&) { s.text("generic"); }
};

TEST_CASE("traverse: codec_for lookup order is <T, Format> then <T, void> then reflection") {
    CHECK(rec(External{4}) == tokens{"u:40"});
    CHECK(rec(Hexy{}) == tokens{"t:generic"});
    CHECK(rec<Hexy, hooked_sink>(Hexy{}) == tokens{"t:mock-specific"});
}

namespace {
struct Prepared { int a; static constexpr std::string_view prepared_marker = "P"; };
struct Hooked {
    [[=as_text_marker]] std::uint64_t as_text = 5;
    std::uint64_t plain = 6;
    [[=as_bytes_marker]] std::vector<std::uint8_t> promoted{1, 2, 3};
    std::vector<std::uint8_t> not_promoted{1};
    std::vector<std::byte> native{std::byte{1}};
    Prepared p{1};
};
}

TEST_CASE("traverse: sink hooks — prepared keys, field-aware uint/bytes, write_prepared, format_traits") {
    auto t = rec<Hooked, hooked_sink>(Hooked{});
    CHECK(t == tokens{"{6",
        "pk:as_text", "t:5",
        "pk:plain", "u:6",
        "pk:promoted", "fb:3",
        "pk:not_promoted", "[1", "u:1", "]",
        "pk:native", "fb:1",
        "pk:p", "prepared:P",
        "}"});
}

TEST_CASE("traverse: encode_error carries the member path") {
    struct Row { Color c; };
    struct Table { std::vector<Row> rows; };
    Table t; t.rows.push_back({Color::red}); t.rows.push_back({Color::hidden});
    try { rec(t); FAIL("expected throw"); }
    catch (vc::encode_error const& e) {
        CHECK(e.code() == vc::errc::unknown_enumerator);
        auto p = e.path();
        REQUIRE(p.size() == 3);
        CHECK(p[0].kind == vc::path_step::tag::member); CHECK(p[0].name == "rows");
        CHECK(p[1].kind == vc::path_step::tag::index);  CHECK(p[1].index == 1);
        CHECK(p[2].kind == vc::path_step::tag::member); CHECK(p[2].name == "c");
    }
}

// Spec §11: cross-format consistency rules, asserted from v0.1 against the CBOR spike.
// Rules that exist only in prose stop being true.
#include <vcodec/vcodec.hpp>

#include "cbor_dump.hpp"
#include "recording_sink.hpp"
#include "types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <forward_list>

using namespace vcodec_test;
using tokens = std::vector<std::string>;

namespace {
// Non-deterministic, declaration-order-independent output is what the model comparison wants;
// the deterministic mode reorders struct members and is covered in test/cbor.
constexpr vc::cbor::options plain{ .deterministic = false };
template<class T> std::string cbor(T const& v) {
    std::vector<std::byte> out; vc::cbor::writer<plain> s(out); vc::core::encode(s, v);
    return std::string(reinterpret_cast<const char*>(out.data()), out.size());
}
// The recording sink declares itself a CBOR sink so core orders members and keys the way it
// does for the real writer; only the serialisation differs. The model carries C++ signedness
// (sint(5) for an `int` 5) and CBOR collapses non-negative values to major type 0 — a
// lowering, so comparisons normalise non-negative i: → u:.
struct cbor_recording_sink : recording_sink { using format = vc::cbor::format; };
template<class T> tokens model(T const& v) {
    cbor_recording_sink s; vc::core::encode(s, v);
    for (auto& t : s.tokens) {
        if (t.starts_with("i:") && t[2] != '-') t = "u:" + t.substr(2);
        if (t.starts_with("{o")) t = "{" + t.substr(2);
    }
    return s.tokens;
}
// Declaration order, text keys, enum names: what JSON sees.
template<class T> tokens json_model(T const& v) { recording_sink s; vc::core::encode(s, v); return s.tokens; }
std::string hex(std::string_view s) { std::string h; vc::core::hex_encode(std::as_bytes(std::span(s)), h); return h; }

struct IntKeyed { std::map<std::uint32_t, std::string> by_id{{1, "a"}, {300, "b"}}; };
struct Binary { std::vector<std::byte> blob{std::byte{0xDE}, std::byte{0xAD}}; std::array<std::byte, 2> fixed{std::byte{1}, std::byte{2}}; };
struct Mixed { std::int64_t neg = -500; std::uint64_t big = 1ull << 40; double d = 1.5; bool b = true; std::optional<int> none; };
}

TEST_CASE("cross: the CBOR writer encodes the §7 type set with RFC 8949 bytes") {
    CHECK(hex(cbor(0u)) == "00");
    CHECK(hex(cbor(23u)) == "17");
    CHECK(hex(cbor(24u)) == "1818");
    CHECK(hex(cbor(1000u)) == "1903e8");
    CHECK(hex(cbor(-1)) == "20");
    CHECK(hex(cbor(-500)) == "3901f3");
    CHECK(hex(cbor(1.5)) == "f93e00");                 // preferred float serialization
    CHECK(hex(cbor(true)) == "f5");
    CHECK(hex(cbor(std::optional<int>{})) == "f6");
    CHECK(hex(cbor(std::string("IETF"))) == "6449455446");
    CHECK(hex(cbor(std::vector<int>{1, 2})) == "820102");
    CHECK(hex(cbor(std::map<std::string, int>{{"a", 1}})) == "a1616101");
    CHECK(hex(cbor(Binary{})) == "a264626c6f6242dead656669786564420102");     // byte strings, natively
    CHECK(hex(cbor(IntKeyed{})) == "a16562795f6964a201616119012c6162");        // integer keys, natively
    CHECK(hex(cbor(Point{1, 2})) == "a2617801617902");
}

TEST_CASE("cross rule 3/4: core annotations mean the same thing in every format") {
    // Same traversal, same key set, same order, same skips: dump the CBOR back into the
    // model vocabulary and compare with the recording sink token for token.
    Server s; s.host_name = "h"; s.inner.x = 1; s.cert_path = "c";
    CHECK(cbor_dumper::dump(cbor(s)) == model(s));
    Kitchen k;
    CHECK(cbor_dumper::dump(cbor(k)) == model(k));
    CHECK(cbor_dumper::dump(cbor(Shape{Rect{1, 2}})) == model(Shape{Rect{1, 2}}));
    CHECK(cbor_dumper::dump(cbor(UserId{7})) == model(UserId{7}));
    CHECK(cbor_dumper::dump(cbor(Mixed{})) == model(Mixed{}));
}

TEST_CASE("cross rule 4: per-format defaults may differ — CBOR takes bytes and int keys natively, JSON needs a lowering") {
    // The model stream is the same; only the sink's representation differs.
    CHECK(model(Binary{}) == tokens{"{2", "k:blob", "b:dead", "k:fixed", "b:0102", "}"});
    CHECK(cbor_dumper::dump(cbor(Binary{})) == model(Binary{}));
    CHECK(model(IntKeyed{}) == tokens{"{1", "k:by_id", "{2", "ku:1", "t:a", "ku:300", "t:b", "}", "}"});
    CHECK(cbor_dumper::dump(cbor(IntKeyed{})) == model(IntKeyed{}));
    // Enum default differs per format; the value is the same enumerator (rule 4, not rule 3).
    CHECK(json_model(Color::red) == tokens{"t:bright-red"});
    CHECK(model(Color::red) == tokens{"u:0"});
    // The set of struct members and their values is identical; only order and key form differ.
    struct K { [[=vc::cbor::key(1)]] int a = 1; int b = 2; };
    CHECK(json_model(K{}) == tokens{"{2", "k:a", "i:1", "k:b", "i:2", "}"});
    CHECK(model(K{}) == tokens{"{2", "ki:1", "u:1", "k:b", "u:2", "}"});
    STATIC_CHECK(!vc::json::encodable<Binary>);
    STATIC_CHECK(vc::cbor::encodable<Binary>);
    STATIC_CHECK(vc::cbor_only<Binary>);
}

TEST_CASE("cross: definite lengths where core knows them, indefinite where it does not") {
    // Structs are always counted, conditional members included; unsized ranges are not.
    CHECK((static_cast<unsigned char>(cbor(Point{}).front()) >> 5) == 5);
    CHECK((static_cast<unsigned char>(cbor(Point{}).front()) & 31) == 2);
    Server s; s.cert_path = "c";
    CHECK(static_cast<unsigned char>(cbor(Server{}).front()) == 0xA9);   // 9 members emitted
    CHECK(static_cast<unsigned char>(cbor(s).front()) == 0xAA);          // 10 with cert-path present
    std::list<int> l{1, 2, 3};
    CHECK(static_cast<unsigned char>(cbor(l).front()) == 0x83);          // std::list is sized
    std::forward_list<int> fl{1, 2};
    CHECK(static_cast<unsigned char>(cbor(fl).front()) == 0x9F);         // not sized → indefinite
    CHECK(static_cast<unsigned char>(cbor(fl).back()) == 0xFF);
}

TEST_CASE("cross rule 5: encode-side failures speak in member terms in every format") {
    struct Row { Color c; };
    struct Table { std::vector<Row> rows{{Color::red}, {Color::hidden}}; };
    auto check = [](auto&& fn) {
        try { fn(); FAIL("expected throw"); }
        catch (vc::encode_error const& e) {
            REQUIRE(e.path().size() == 3);
            CHECK(e.path()[0].name == "rows"); CHECK(e.path()[1].index == 1); CHECK(e.path()[2].name == "c");
        }
    };
    check([] { cbor(Table{}); });
    check([] { model(Table{}); });
}

TEST_CASE("cross: the prepared_key hook is format-agnostic") {
    // Point's keys "x" and "y" go through writer::prepared_key<Name>; the bytes are the same
    // as the runtime path would produce.
    CHECK(hex(cbor(Point{1, 2})) == "a2617801617902");
}

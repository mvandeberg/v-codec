// C2: RFC 8949 §4.2 examples; invariance under iteration and declaration order; validation
// on read.
#include <vcodec/cbor.hpp>

#include "cbor_hex.hpp"
#include "dom.hpp"
#include "generators.hpp"
#include "types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <unordered_map>

using namespace vcodec_test;
namespace cb = vcodec::cbor;

namespace {
template<cb::options O = {}, class T> std::string enc(T const& v) { return to_hex(cb::encode<O>(v)); }
constexpr cb::options strict{ .require_deterministic = true };
constexpr cb::options loose{ .deterministic = false };
}

TEST_CASE("deterministic: §4.2.1 key order examples") {
    // RFC 8949 §4.2.1: 10, 100, -1, "z", "aa", [100], [-1], false
    std::map<std::string, int> m{{"aa", 1}, {"z", 2}};
    CHECK(enc(m) == "a2" "617a02" "62616101");
    std::unordered_map<int, int> u{{100, 1}, {10, 2}, {-1, 3}};
    CHECK(enc(u) == "a3" "0a02" "186401" "2003");
    std::map<std::string, int> ordered_wrong{{"b", 1}, {"aa", 2}};    // std::map order: "aa" < "b"; canonical: "b" < "aa"
    CHECK(enc(ordered_wrong) == "a2" "616201" "62616102");
}

TEST_CASE("deterministic: output is invariant under container iteration order") {
    rng g(1);
    for (int i = 0; i < 100; ++i) {
        auto v = generate<std::unordered_map<std::string, std::vector<int>>>(g);
        std::map<std::string, std::vector<int>> sorted(v.begin(), v.end());
        std::vector<std::pair<std::string, std::vector<int>>> pairs(v.begin(), v.end());
        std::reverse(pairs.begin(), pairs.end());
        auto a = cb::encode(v), b = cb::encode(sorted);
        CHECK(a == b);
        if (std::set<std::string>(std::views::keys(v).begin(), std::views::keys(v).end()).size() == pairs.size())
            CHECK(cb::encode(pairs) == a);
    }
}

TEST_CASE("deterministic: struct member order is canonical regardless of declaration") {
    struct A { int b = 1; int a = 2; [[=cb::key(1)]] int one = 3; };
    struct B { [[=cb::key(1)]] int one = 3; int a = 2; int b = 1; };
    CHECK(enc(A{}) == enc(B{}));
    CHECK(enc(A{}) == "a3" "0103" "616102" "616201");
}

TEST_CASE("deterministic: encode → decode → encode is byte-identical") {
    rng g(7);
    for (int i = 0; i < 200; ++i) {
        auto v = generate<Kitchen>(g);
        auto bytes = cb::encode(v);
        auto back = cb::decode<Kitchen>(bytes);
        REQUIRE(back);
        CHECK(equal(v, *back));
        CHECK(cb::encode(*back) == bytes);
        // every deterministic output validates as deterministic
        auto strict_back = cb::decode<Kitchen, strict>(bytes);
        REQUIRE(strict_back);
    }
}

TEST_CASE("deterministic: validation on read (§8.2)") {
    auto rej = [](std::string_view hex) {
        auto b = from_hex(hex);
        auto r = cb::decode<vcodec_test::dom::value, strict>(b);
        REQUIRE_FALSE(r);
        CHECK(r.error().code() == vcodec::errc::non_deterministic);
        return r.error();
    };
    auto e1 = rej("1805");                          // 5 in two bytes
    CHECK(e1.detail() == "integer 5 encoded in 2 bytes");
    CHECK(e1.suggestion() == "shortest form is 1 byte");
    auto e2 = rej("9f01ff");                        // indefinite array
    CHECK(e2.detail() == "indefinite-length array");
    auto e3 = rej("fb3ff0000000000000");            // 1.0 as double
    CHECK(e3.detail() == "float encoded as double");
    CHECK(e3.suggestion() == "preferred form is half");
    auto e4 = rej("a2" "0401" "0202");              // keys 4 then 2
    CHECK(e4.detail() == "map keys out of canonical order");
    CHECK(e4.suggestion() == "key 2 precedes key 4");
    auto e5 = rej("a2" "0201" "0202");              // duplicate key
    CHECK(e5.detail() == "duplicate map key");
    auto e6 = rej("7f6161ff");                      // indefinite string
    CHECK(e6.detail() == "indefinite-length text string");
    // valid canonical input passes
    CHECK(cb::decode<vcodec_test::dom::value, strict>(from_hex("a2" "0201" "0402")));
    CHECK(cb::decode<vcodec_test::dom::value, strict>(from_hex("f93c00")));
    // and the same bytes decode fine without the option
    CHECK(cb::decode<vcodec_test::dom::value>(from_hex("1805")));
    CHECK(cb::decode<vcodec_test::dom::value>(from_hex("9f01ff")));
}

TEST_CASE("deterministic: a [[=cbor::deterministic]] type validates on read and names itself") {
    struct [[=cb::deterministic]] Claims { [[=cb::key(1)]] int a = 0; [[=cb::key(2)]] int b = 0; };
    auto ok = cb::decode<Claims>(from_hex("a2" "0101" "0202"));
    REQUIRE(ok);
    auto bad = cb::decode<Claims>(from_hex("a2" "0202" "0101"));
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code() == vcodec::errc::non_deterministic);
    CHECK(bad.error().suggestion() == "key 1 precedes key 2; Claims is declared [[=cbor::deterministic]]");
    // the annotation also forbids indefinite members at compile time (see compile_fail)
}

TEST_CASE("deterministic: non-deterministic output still decodes and re-encodes canonically") {
    std::map<std::string, int> m{{"aa", 1}, {"b", 2}};
    auto loose_bytes = cb::encode<loose>(m);
    CHECK(to_hex(loose_bytes) == "a2" "62616101" "616202");       // std::map order
    auto back = cb::decode<std::map<std::string, int>>(loose_bytes);
    REQUIRE(back);
    CHECK(to_hex(cb::encode(*back)) == "a2" "616202" "62616101");
    CHECK_FALSE(cb::decode<std::map<std::string, int>, strict>(loose_bytes));
}

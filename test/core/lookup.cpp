#include <vcodec/core/lookup.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace vc = vcodec;

namespace {
struct Small { int alpha; [[=vc::alias("b"), =vc::alias("beta")]] int bravo; int charlie; [[=vc::skip_deserializing]] int delta; };
struct Large {
    int f01; int f02; int f03; int f04; int f05; int f06; int f07; int f08; int f09; int f10;
    [[=vc::name("a-long-field-name")]] int f11; [[=vc::name("x")]] int f12;
    [[=vc::alias("alias_for_13")]] int f13; int same_len_a; int same_len_b; int same_len_c;
};
enum class Color { red [[=vc::alias("RED")]], green, hidden [[=vc::skip]] };
}

TEST_CASE("lookup: small tables scan linearly") {
    constexpr auto& t = vc::core::lookup_of<Small>;
    STATIC_CHECK(t.bucket_count == 0);
    CHECK(t.find("alpha") == 0);
    CHECK(t.find("bravo") == 1);
    CHECK(t.find("b") == 1);
    CHECK(t.find("beta") == 1);
    CHECK(t.find("charlie") == 2);
    CHECK(t.find("delta") == 3);                     // skip_deserializing: known, skipped by the decoder
    CHECK(t.find("alph") == vc::core::npos);
    CHECK(t.find("alphaa") == vc::core::npos);
    CHECK(t.find("") == vc::core::npos);
    CHECK(t.find("Alpha") == vc::core::npos);        // case-sensitive
}

TEST_CASE("lookup: large tables hash into buckets and still find everything") {
    constexpr auto& t = vc::core::lookup_of<Large>;
    STATIC_CHECK(t.bucket_count > 0);
    STATIC_CHECK((t.bucket_count & (t.bucket_count - 1)) == 0);
    for (auto const& f : vc::core::schema_of<Large>) {
        CHECK(t.find(f.wire_name.view()) == f.member_index);
        for (auto a : f.aliases()) CHECK(t.find(a.view()) == f.member_index);
    }
    CHECK(t.find("f00") == vc::core::npos);
    CHECK(t.find("same_len_d") == vc::core::npos);   // same length, first and last byte as real keys
    CHECK(t.find("same_len_") == vc::core::npos);
    static_assert(t.find("f11") == vc::core::npos);  // renamed: identifier is not a key
    static_assert(t.find("a-long-field-name") == 10);
}

TEST_CASE("lookup: enum tables skip skipped enumerators and honour aliases") {
    constexpr auto& t = vc::core::enum_lookup_of<Color>;
    CHECK(t.find("red") == 0);
    CHECK(t.find("RED") == 0);
    CHECK(t.find("green") == 1);
    CHECK(t.find("hidden") == vc::core::npos);
}

TEST_CASE("lookup: did-you-mean suggests at distance <= 2") {
    constexpr auto s = vc::core::schema_of<Small>;
    CHECK(vc::core::suggest("alpah", s) == "alpha");
    CHECK(vc::core::suggest("bravo!", s) == "bravo");
    CHECK(vc::core::suggest("charlie", s) == "charlie");
    CHECK(vc::core::suggest("zzzzzzz", s).empty());
    CHECK(vc::core::suggest("delta", s).empty());     // not readable, not suggested
    CHECK(vc::core::levenshtein("kitten", "sitting") == 3);
    CHECK(vc::core::levenshtein("", "abc") == 3);
}

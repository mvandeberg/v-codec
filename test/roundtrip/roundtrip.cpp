// §13.2 property tests: encode → decode → compare over generated values for every §7 type,
// plus decode → encode → byte-compare for canonically formatted input.
#include <vcodec/json.hpp>

#include "generators.hpp"
#include "types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <list>
#include <map>
#include <set>
#include <unordered_map>

using namespace vcodec_test;
namespace js = vcodec::json;

namespace {

constexpr int iterations = 200;

template<class T, js::options O = {}>
void roundtrip_type(std::uint64_t seed) {
    rng g(seed);
    for (int i = 0; i < iterations; ++i) {
        T v = generate<T>(g);
        std::string text = js::encode<O>(v);
        auto back = js::decode<T, O>(text);
        INFO("iteration " << i << ": " << text);
        REQUIRE(back);
        CHECK(equal(v, *back));
        // Our own output is canonical: decode → encode reproduces it byte for byte.
        CHECK(js::encode<O>(*back) == text);
    }
}

struct Nested {
    std::map<std::string, std::vector<std::optional<Point>>> groups;
    std::vector<std::pair<std::string, std::tuple<int, double, bool>>> rows;
    std::optional<std::unique_ptr<Server>> server;
    std::array<Shape, 2> shapes{};
    std::deque<std::set<std::int8_t>> sets;
};

struct Recursive { std::vector<Recursive> kids; std::string name; };

struct [[=vc::rename_all(vc::case_::camel), =vc::deny_unknown_fields]] Renamed {
    std::string first_name;
    [[=vc::alias("lastName"), =vc::name("surname")]] std::string last_name;
    [[=vc::flatten]] Inner inner;
    [[=vc::skip_if_null]] std::optional<std::int64_t> big_number;
    [[=js::as_string]] std::uint64_t exact = 0;
    [[=js::bytes(js::hex)]] std::vector<std::byte> blob;
    [[=js::stringify_keys]] std::map<std::int32_t, std::string> by_id;
    Color color = Color::green;
    Level level = Level::mid;
};

} // namespace

TEST_CASE("roundtrip: scalars") {
    roundtrip_type<bool>(1); roundtrip_type<char>(2);
    roundtrip_type<std::int8_t>(3); roundtrip_type<std::uint8_t>(4);
    roundtrip_type<std::int16_t>(5); roundtrip_type<std::uint16_t>(6);
    roundtrip_type<std::int32_t>(7); roundtrip_type<std::uint32_t>(8);
    roundtrip_type<std::int64_t>(9); roundtrip_type<std::uint64_t>(10);
    roundtrip_type<float>(11); roundtrip_type<double>(12);
    roundtrip_type<std::string>(13); roundtrip_type<std::u8string>(14);
    roundtrip_type<Color>(15); roundtrip_type<Level>(16);
}

TEST_CASE("roundtrip: containers") {
    roundtrip_type<std::vector<int>>(20);
    roundtrip_type<std::list<std::string>>(21);
    roundtrip_type<std::deque<double>>(22);
    roundtrip_type<std::set<std::int16_t>>(23);
    roundtrip_type<std::array<std::uint8_t, 3>>(24);
    roundtrip_type<std::pair<std::string, int>>(25);
    roundtrip_type<std::tuple<int, std::string, bool, double>>(26);
    roundtrip_type<std::map<std::string, int>>(27);
    roundtrip_type<std::vector<std::pair<std::string, std::vector<int>>>>(28);
    roundtrip_type<std::optional<std::vector<std::optional<int>>>>(29);
    roundtrip_type<std::vector<std::vector<std::vector<std::string>>>>(30);
    roundtrip_type<ring<int>>(31);
}

TEST_CASE("roundtrip: structs") {
    roundtrip_type<Point>(40);
    roundtrip_type<Server>(41);
    roundtrip_type<Shape>(42);
    roundtrip_type<UserId>(43);
    roundtrip_type<Nested>(44);
    roundtrip_type<Recursive>(45);
    roundtrip_type<Renamed>(46);
    roundtrip_type<Kitchen>(47);
    roundtrip_type<std::vector<Kitchen>>(48);
}

TEST_CASE("roundtrip: pretty printing is a formatting choice, not a semantic one") {
    constexpr js::options pretty{ .pretty = true, .indent = 3 };
    roundtrip_type<Nested, pretty>(50);
    roundtrip_type<Kitchen, pretty>(51);
    rng g(52);
    for (int i = 0; i < 50; ++i) {
        auto v = generate<Renamed>(g);
        auto compact = js::encode(v);
        auto spaced = js::encode<pretty>(v);
        auto a = js::decode<Renamed>(compact);
        auto b = js::decode<Renamed>(spaced);
        REQUIRE(a); REQUIRE(b);
        CHECK(equal(*a, *b));
        CHECK(js::encode(*b) == compact);
    }
}

TEST_CASE("roundtrip: unordered containers round-trip by content, not by byte layout") {
    rng g(70);
    for (int i = 0; i < iterations; ++i) {
        auto v = generate<std::unordered_map<std::string, std::vector<std::optional<int>>>>(g);
        auto back = js::decode<std::unordered_map<std::string, std::vector<std::optional<int>>>>(js::encode(v));
        REQUIRE(back);
        CHECK(equal(v, *back));
    }
}

TEST_CASE("roundtrip: decode_all agrees with decode on valid input") {
    rng g(60);
    for (int i = 0; i < 100; ++i) {
        auto v = generate<Kitchen>(g);
        auto text = js::encode(v);
        auto all = js::decode_all<Kitchen>(text);
        REQUIRE(all.complete());
        CHECK(equal(v, all.value));
    }
}

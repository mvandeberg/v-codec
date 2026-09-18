// v0.2 §13.2 property tests for CBOR: encode → decode → compare over generated values; the
// deterministic byte-identity; invariance under member declaration order; the two modes
// decode to equal values; strict validation accepts every deterministic output.
#include <vcodec/cbor.hpp>

#include "generators.hpp"
#include "types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <unordered_map>

using namespace vcodec_test;
namespace cb = vcodec::cbor;

namespace {

constexpr int iterations = 200;
constexpr cb::options loose{ .deterministic = false };
constexpr cb::options strict{ .require_deterministic = true };

template<class T>
void roundtrip_type(std::uint64_t seed) {
    rng g(seed);
    for (int i = 0; i < iterations; ++i) {
        T v = generate<T>(g);
        auto bytes = cb::encode(v);
        auto back = cb::decode<T>(bytes);
        INFO("iteration " << i << (back ? std::string() : ": " + vcodec::render_terse(back.error())));
        REQUIRE(back);
        CHECK(equal(v, *back));
        CHECK(cb::encode(*back) == bytes);                 // deterministic: byte-identical
        CHECK(cb::decode<T, strict>(bytes));               // and passes validation
        auto loose_bytes = cb::encode<loose>(v);
        auto loose_back = cb::decode<T>(loose_bytes);
        REQUIRE(loose_back);
        CHECK(equal(v, *loose_back));                      // both modes decode to the same value
        CHECK(cb::encode(*loose_back) == bytes);           // and canonicalise to the same bytes
    }
}

enum class Mode : std::uint8_t { a, b [[=vcodec::name("bee")]], c [[=vcodec::skip]] };

struct [[=cb::integer_keys]] Claims {
    std::string iss;
    std::int64_t exp = 0;
    [[=cb::key(7)]] std::vector<std::byte> cti;
    [[=cb::key(-65537)]] std::optional<std::string> ext;
    [[=vcodec::as_text]] Mode mode = Mode::a;
    Mode imode = Mode::a;
    [[=cb::typed_array]] std::vector<double> samples;
    [[=cb::tag(32)]] std::string uri;
    std::chrono::sys_seconds when{};
    [[=cb::tag(0)]] std::chrono::sys_seconds when_text{};
};

struct Nested {
    std::map<std::string, std::vector<std::optional<Point>>> groups;
    std::vector<std::pair<std::string, std::tuple<int, double, bool>>> rows;
    std::optional<std::unique_ptr<Server>> server;
    std::array<Shape, 2> shapes{};
    std::deque<std::set<std::int8_t>> sets;
    std::unordered_map<std::int64_t, std::string> by_id;
    [[=cb::byte_string]] std::vector<std::uint8_t> raw;
    std::array<std::byte, 4> fixed{};
};

} // namespace

TEST_CASE("cbor roundtrip: scalars") {
    roundtrip_type<bool>(1); roundtrip_type<char>(2);
    roundtrip_type<std::int8_t>(3); roundtrip_type<std::uint8_t>(4);
    roundtrip_type<std::int16_t>(5); roundtrip_type<std::uint16_t>(6);
    roundtrip_type<std::int32_t>(7); roundtrip_type<std::uint32_t>(8);
    roundtrip_type<std::int64_t>(9); roundtrip_type<std::uint64_t>(10);
    roundtrip_type<float>(11); roundtrip_type<double>(12);
    roundtrip_type<std::string>(13); roundtrip_type<std::u8string>(14);
    roundtrip_type<Color>(15); roundtrip_type<Level>(16);
}

TEST_CASE("cbor roundtrip: containers") {
    roundtrip_type<std::vector<int>>(20);
    roundtrip_type<std::list<std::string>>(21);
    roundtrip_type<std::deque<double>>(22);
    roundtrip_type<std::set<std::int16_t>>(23);
    roundtrip_type<std::array<std::uint8_t, 3>>(24);
    roundtrip_type<std::pair<std::string, int>>(25);
    roundtrip_type<std::tuple<int, std::string, bool, double>>(26);
    roundtrip_type<std::map<std::string, int>>(27);
    roundtrip_type<std::map<std::int64_t, std::vector<int>>>(28);
    roundtrip_type<std::optional<std::vector<std::optional<int>>>>(29);
    roundtrip_type<std::vector<std::vector<std::vector<std::string>>>>(30);
    roundtrip_type<ring<int>>(31);
    roundtrip_type<std::vector<std::byte>>(32);
    roundtrip_type<std::unordered_map<std::string, std::vector<std::byte>>>(33);
}

TEST_CASE("cbor roundtrip: structs") {
    roundtrip_type<Point>(40);
    roundtrip_type<Server>(41);
    roundtrip_type<Shape>(42);
    roundtrip_type<UserId>(43);
    roundtrip_type<Nested>(44);
    roundtrip_type<Claims>(45);
    roundtrip_type<Kitchen>(46);
    roundtrip_type<std::vector<Kitchen>>(47);
}

TEST_CASE("cbor roundtrip: member declaration order does not affect deterministic output") {
    struct A { int x = 0; std::string y; [[=cb::key(3)]] double z = 0; std::vector<int> w; };
    struct B { std::vector<int> w; [[=cb::key(3)]] double z = 0; std::string y; int x = 0; };
    rng g(50);
    for (int i = 0; i < iterations; ++i) {
        A a = generate<A>(g);
        B b; b.x = a.x; b.y = a.y; b.z = a.z; b.w = a.w;
        CHECK(cb::encode(a) == cb::encode(b));
    }
}

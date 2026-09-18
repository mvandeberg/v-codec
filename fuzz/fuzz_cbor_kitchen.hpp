// The hostile struct for the CBOR fuzzers (spec v0.2 §13.4): integer keys, tags, typed arrays,
// borrowed spans and views, integer-keyed maps, undefined-tolerant optionals.
#pragma once
#include <vcodec/cbor.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

namespace fuzz_cbor {
namespace vc = vcodec;
namespace cb = vcodec::cbor;

enum class Mode { off [[=vc::name("OFF")]], on, auto_ [[=vc::alias("automatic")]], secret [[=vc::skip]] };
enum class [[=vc::as_integer]] Prio : std::int16_t { low = -1, mid = 0, high = 7 };

struct Leaf { std::int8_t a = 0; std::uint64_t b = 0; double d = 0; std::string s; };
struct [[=vc::name("circle")]] Circle { double r = 0; };
struct [[=vc::name("poly")]] Poly { std::vector<std::pair<float, float>> pts; };
struct [[=vc::tag("kind")]] Shape { std::variant<Circle, Poly, int, std::string> v; };

struct Node {
    std::string name;
    std::vector<Node> kids;
    std::unique_ptr<Node> next;
};

struct [[=cb::integer_keys]] Claims {
    std::string iss;
    std::int64_t exp = 0;
    [[=cb::key(7)]] std::vector<std::byte> cti;
    [[=cb::key(-65537)]] std::optional<std::string> ext;
    [[=cb::tag(1)]] std::int64_t iat = 0;
    std::chrono::sys_seconds nbf{};
    [[=cb::tag(0)]] std::chrono::sys_seconds text_time{};
};

struct Deep {
    [[=vc::alias("alias-one")]] std::int32_t aliased = 0;
    [[=vc::skip_if_null]] std::optional<Leaf> leaf;
    [[=vc::skip_if_default]] int dflt = 0;
    [[=vc::default_value(42)]] int with_default;
    std::map<std::string, std::vector<std::optional<Shape>>> shapes;
    std::map<std::int64_t, std::deque<Mode>> by_id;
    std::vector<std::byte> blob;
    std::array<std::byte, 4> fixed{};
    [[=cb::byte_string]] std::vector<std::uint8_t> raw;
    [[=cb::typed_array]] std::vector<double> samples;
    [[=cb::typed_array]] std::vector<std::int16_t> shorts;
    std::vector<float> maybe_typed;
    std::array<std::tuple<bool, char, Prio>, 2> tuples{};
    std::list<std::set<std::int8_t>> sets;
    std::map<std::string, Node> nodes;
    std::vector<std::vector<std::vector<std::vector<double>>>> deep;
    std::u8string u8;
    Mode mode = Mode::off;
    [[=vc::as_text]] Mode named = Mode::off;
    Claims claims;
    float narrow = 0;   // pinned widths are not preferred serialization, so they stay out of the round-trip oracle
    std::vector<std::uint8_t> numbers;
};

// Borrowing members: the decoded value aliases the input; the harnesses keep it alive.
struct Borrowed {
    std::string_view name;
    std::span<const std::byte> payload;
    std::vector<std::string_view> tags;
    std::map<std::string_view, std::span<const std::byte>> attrs;
    Deep deep;
};

struct [[=cb::deterministic]] Canonical {
    [[=cb::key(1)]] std::string a;
    [[=cb::key(2)]] std::map<std::string, int> m;
    [[=cb::key(3)]] std::vector<double> f;
    [[=cb::key(4)]] std::optional<Deep> d;
};

} // namespace fuzz_cbor

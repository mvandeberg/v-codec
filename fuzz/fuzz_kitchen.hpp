// The deliberately hostile struct for fuzz_typed and fuzz_roundtrip (§13.4): deep nesting,
// every §7 category, optionals, variants, flattening, aliases, borrowed views.
#pragma once
#include <vcodec/json.hpp>

#include <array>
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

namespace fuzz {
namespace vc = vcodec;
namespace js = vcodec::json;

enum class Mode { off [[=vc::name("OFF")]], on, auto_ [[=vc::alias("automatic")]], secret [[=vc::skip]] };
enum class [[=vc::as_integer]] Prio : std::int16_t { low = -1, mid = 0, high = 7 };

struct Leaf { std::int8_t a = 0; std::uint64_t b = 0; double d = 0; std::string s; };
struct [[=vc::name("circle")]] Circle { double r = 0; };
struct [[=vc::name("poly")]] Poly { std::vector<std::pair<float, float>> pts; };
struct [[=vc::tag("kind")]] Shape { std::variant<Circle, Poly, int, std::string> v; };

struct Inner { [[=vc::name("ix")]] int x = 0; std::optional<std::string> label; };
struct Base { std::uint16_t base_id = 0; };

struct Node {
    std::string name;
    std::vector<Node> kids;
    std::unique_ptr<Node> next;
    std::optional<std::shared_ptr<Node>> maybe;
};

struct [[=vc::rename_all(vc::case_::kebab)]] Deep : Base {
    [[=vc::flatten]] Inner inner;
    [[=vc::alias("alias-one"), =vc::alias("alias_two")]] std::int32_t aliased = 0;
    [[=vc::skip_if_null]] std::optional<Leaf> leaf;
    [[=vc::skip_if_default]] int dflt = 0;
    [[=vc::default_value(42)]] int with_default;
    [[=vc::default_value("hello")]] std::string with_str_default;
    std::map<std::string, std::vector<std::optional<Shape>>> shapes;
    [[=js::stringify_keys]] std::map<std::int64_t, std::deque<Mode>> by_id;
    [[=js::as_string]] std::uint64_t exact = 0;
    [[=js::bytes(js::base64)]] std::vector<std::byte> blob;
    [[=js::bytes(js::hex)]] std::array<std::byte, 4> fixed{};
    [[=js::bytes(js::base64url)]] std::vector<std::uint8_t> url;
    std::array<std::tuple<bool, char, Prio>, 2> tuples{};
    std::list<std::set<std::int8_t>> sets;
    std::map<std::string, Node> nodes;          // ordered: the harness byte-compares re-encoded output
    std::vector<std::vector<std::vector<std::vector<double>>>> deep;
    std::u8string u8;
    Mode mode = Mode::off;
    std::vector<std::uint8_t> numbers;
};

// Borrowed views make the decoded value alias the input; fuzz_typed keeps the buffer alive.
struct Borrowed {
    std::string_view name;
    std::vector<std::string_view> tags;
    std::map<std::string_view, std::string_view> attrs;
    Deep deep;
};

} // namespace fuzz

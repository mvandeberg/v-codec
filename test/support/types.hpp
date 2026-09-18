// Shared test types covering the §7 type set. Format-free.
#pragma once
#include <vcodec/core/annotations.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

namespace vcodec_test {
namespace vc = vcodec;

enum class Color { red [[=vc::name("bright-red")]], green, hidden [[=vc::skip]] };
enum class [[=vc::as_integer]] Level : std::int8_t { low = -1, mid = 0, high = 1 };

struct Point { int x = 0; int y = 0; bool operator==(Point const&) const = default; };
struct Coord { int x; int y; bool operator==(Coord const&) const = default; };   // no initialisers: both required
struct Narrow { std::uint8_t small; std::uint32_t wide; std::int16_t signed16; };
struct [[=vc::name("circle")]] Circle { double radius = 0; bool operator==(Circle const&) const = default; };
struct [[=vc::name("rect")]] Rect { double w = 0, h = 0; bool operator==(Rect const&) const = default; };

struct [[=vc::tag("kind")]] Shape {
    std::variant<Circle, Rect, int> value;
    bool operator==(Shape const&) const = default;
};

struct Base { int base_id = 0; bool operator==(Base const&) const = default; };
struct Inner { int x = 0; [[=vc::name("why")]] int y = 2; bool operator==(Inner const&) const = default; };

// A container never named in the library (§7.3).
template<class T>
struct ring {
    std::vector<T> v;
    auto begin() const { return v.begin(); }
    auto end() const { return v.end(); }
    auto begin() { return v.begin(); }
    auto end() { return v.end(); }
    std::size_t size() const { return v.size(); }
    void push_back(T t) { v.push_back(std::move(t)); }
    void clear() { v.clear(); }
    bool operator==(ring const&) const = default;
};

struct [[=vc::rename_all(vc::case_::kebab), =vc::deny_unknown_fields]] Server : Base {
    std::string host_name;
    std::uint16_t port = 8080;
    [[=vc::name("tls")]] bool secure = false;
    [[=vc::skip_if_null]] std::optional<std::string> cert_path;
    std::vector<std::string> allowed_origins;
    [[=vc::flatten]] Inner inner;
    [[=vc::alias("retry"), =vc::default_value(3)]] int retries;
    [[=vc::skip]] int internal = 99;
    [[=vc::skip_if_default]] int zero_if_unset = 0;
    [[=vc::skip_serializing]] int write_only = 0;
    [[=vc::skip_deserializing]] int read_only = 5;
    bool operator==(Server const&) const = default;
};

struct [[=vc::transparent]] UserId { std::uint64_t id = 0; bool operator==(UserId const&) const = default; };

struct Kitchen {
    bool flag = true;
    char letter = 'k';
    std::int8_t i8 = -8; std::int16_t i16 = -16; std::int32_t i32 = -32; std::int64_t i64 = -64;
    std::uint8_t u8 = 8; std::uint16_t u16 = 16; std::uint32_t u32 = 32; std::uint64_t u64 = 64;
    float f = 1.5f; double d = 2.5;
    std::string s = "str";
    std::u8string u8s = u8"u8";
    Color color = Color::green;
    Level level = Level::high;
    std::optional<int> opt_none;
    std::optional<int> opt_some = 7;
    std::unique_ptr<int> uptr;
    std::shared_ptr<std::string> sptr;
    std::vector<int> vec{1, 2, 3};
    std::list<std::string> lst{"a", "b"};
    std::deque<double> dq{0.5};
    std::set<int> st{3, 1, 2};
    ring<int> custom{{9, 8}};
    std::array<int, 3> arr{4, 5, 6};
    int carr[2] = {7, 8};
    std::pair<std::string, int> pr{"p", 1};
    std::tuple<int, std::string, bool> tp{1, "t", true};
    std::map<std::string, int> smap{{"a", 1}, {"b", 2}};
    std::vector<std::pair<std::string, int>> pairs{{"k", 1}};
    std::vector<std::uint8_t> numbers{1, 2, 255};
    Point point{1, 2};
    Shape shape{Circle{1.0}};
    UserId user{42};
    Server server;
    std::vector<Point> points{{1, 1}, {2, 2}};
    std::optional<Point> opt_point;
    std::map<std::string, std::vector<int>> nested{{"n", {1}}};
};

} // namespace vcodec_test

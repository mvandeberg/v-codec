// Structure generators for property tests (spec §13.2), and a structural equality that
// follows the same classification as the library, so any §7 type round-trips can be checked
// without hand-written operator==.
#pragma once
#include <vcodec/core/concepts.hpp>
#include <vcodec/core/schema.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace vcodec_test {

struct rng {
    std::uint64_t s;
    explicit rng(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    std::uint64_t below(std::uint64_t n) { return n ? next() % n : 0; }
    bool coin() { return next() & 1; }
    int depth = 0;   // recursion budget for nested containers
};

template<class T> T generate(rng& g);
template<class T> bool equal(T const& a, T const& b);

namespace detail {

inline std::string gen_string(rng& g) {
    static const char* const pieces[] = {
        "a", "Z", "0", " ", "\"", "\\", "/", "\n", "\t", "\x01", "\x1f", "\x7f",
        "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80", "key", "value", "-", "_", "{", "}", "[", "]", ":", ",", "null", "true", "1e5",
    };
    std::size_t n = g.below(8);
    std::string s;
    for (std::size_t i = 0; i < n; ++i) s += pieces[g.below(sizeof(pieces) / sizeof(*pieces))];
    return s;
}

template<class I>
I gen_integral(rng& g) {
    switch (g.below(6)) {
    case 0: return std::numeric_limits<I>::min();
    case 1: return std::numeric_limits<I>::max();
    case 2: return I(0);
    case 3: return I(1);
    default: return static_cast<I>(g.next());
    }
}

template<class F>
F gen_floating(rng& g) {
    switch (g.below(8)) {
    case 0: return F(0);
    case 1: return F(-0.0);
    case 2: return std::numeric_limits<F>::max();
    case 3: return std::numeric_limits<F>::min();
    case 4: return std::numeric_limits<F>::denorm_min();
    case 5: return F(0.1);
    case 6: return F(-1e-300);
    default: {
        // a random finite value across the exponent range
        double m = double(g.next() >> 11) / double(1ull << 53);
        int e = int(g.below(600)) - 300;
        F v = static_cast<F>(std::ldexp(m, e));
        return std::isfinite(v) ? v : F(1.5);
    }
    }
}

} // namespace detail

namespace detail {
template<class T> struct is_sys_time : std::false_type {};
template<class D> struct is_sys_time<std::chrono::sys_time<D>> : std::true_type {};
}

template<class T>
T generate(rng& g) {
    using namespace vcodec::core;
    if constexpr (detail::is_sys_time<T>::value) {
        // Years 1970..2106 so RFC 3339 text and epoch numbers both represent the value exactly.
        return std::chrono::time_point_cast<typename T::duration>(std::chrono::sys_seconds(std::chrono::seconds(g.below(std::uint64_t(1) << 32))));
    }
    else if constexpr (boolean_type<T>) return g.coin();
    else if constexpr (byte_type<T>) return std::byte(g.next());
    else if constexpr (char_type<T>) return char('a' + g.below(26));
    else if constexpr (integral_type<T>) return detail::gen_integral<T>(g);
    else if constexpr (floating_type<T>) return detail::gen_floating<T>(g);
    else if constexpr (enum_type<T>) {
        constexpr auto table = enum_schema_of<T>;
        for (;;) { auto const& e = table[g.below(table.size())]; if (!e.skipped) return e.value; }
    }
    else if constexpr (std::same_as<T, std::string>) return detail::gen_string(g);
    else if constexpr (std::same_as<T, std::u8string>) { auto s = detail::gen_string(g); return std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()); }
    else if constexpr (optional_like<T>) {
        using V = optional_value_t<T>;
        if (g.coin() || g.depth > 6) return T{};
        ++g.depth; auto v = generate<V>(g); --g.depth;
        // An engaged optional holding a null pointer is indistinguishable from a disengaged
        // one on the wire (both are `null`), so never generate it.
        if constexpr (optional_like<V>) { if (!v) return T{}; }
        if constexpr (is_optional<T>::value) return T(std::move(v));
        else if constexpr (is_unique_ptr<T>::value) return std::make_unique<V>(std::move(v));
        else return std::make_shared<V>(std::move(v));
    }
    else if constexpr (variant_like<T>) {
        T out;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            std::size_t pick = g.below(sizeof...(I));
            ((I == pick ? (out.template emplace<I>(generate<std::variant_alternative_t<I, T>>(g)), 0) : 0), ...);
        }(std::make_index_sequence<std::variant_size_v<T>>{});
        return out;
    }
    else if constexpr (tuple_like<T>) {
        return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return T{ generate<std::tuple_element_t<I, T>>(g)... };
        }(std::make_index_sequence<std::tuple_size_v<T>>{});
    }
    else if constexpr (std_array<T>) {
        T out{};
        for (auto& e : out) e = generate<std::remove_cvref_t<decltype(e)>>(g);
        return out;
    }
    else if constexpr (byte_like<T>) {
        T out{};
        std::size_t n = g.below(10);
        for (std::size_t i = 0; i < n; ++i) out.push_back(std::byte(g.next()));
        return out;
    }
    else if constexpr (map_like<T>) {
        // Keys are unique even for a range of pairs: deterministic formats reject duplicates.
        T out{};
        if (g.depth > 6) return out;
        std::size_t n = g.below(4);
        ++g.depth;
        for (std::size_t i = 0; i < n; ++i) {
            auto k = generate<map_key_t<T>>(g);
            bool dup = false;
            for (auto const& e : out) if (equal(std::get<0>(e), k)) dup = true;
            if (dup) continue;
            insert_into(out, std::move(k), generate<map_mapped_t<T>>(g));
        }
        --g.depth;
        return out;
    }
    else if constexpr (sequence<T>) {
        T out{};
        if (g.depth > 6) return out;
        std::size_t n = g.below(4);
        ++g.depth;
        for (std::size_t i = 0; i < n; ++i) append_to(out, generate<range_value<T>>(g));
        --g.depth;
        return out;
    }
    else if constexpr (reflectable_class<T>) {
        T out{};
        template for (constexpr auto f : fields_of<T>) {
            constexpr field_info fi = f.info;
            if constexpr (!fi.skip_serializing() && !fi.skip_deserializing()) {
                using M = typename [:f.type:];
                if constexpr (c_array<M>) { for (auto& e : access<f>(out)) e = generate<std::remove_cvref_t<decltype(e)>>(g); }
                else access<f>(out) = generate<M>(g);
            }
        }
        return out;
    }
    else static_assert(false, "no generator for this type");
}

// Structural equality following the library's classification. Floating values compare
// bitwise-equal-or-both-NaN; smart pointers compare by pointee.
template<class T>
bool equal(T const& a, T const& b) {
    using namespace vcodec::core;
    if constexpr (detail::is_sys_time<T>::value) return a == b;
    else if constexpr (floating_type<T>) return (std::isnan(a) && std::isnan(b)) || a == b;
    else if constexpr (boolean_type<T> || char_type<T> || integral_type<T> || enum_type<T> || string_like<T>) return a == b;
    else if constexpr (optional_like<T>) {
        if (!a || !b) return !a && !b;
        return equal(*a, *b);
    }
    else if constexpr (variant_like<T>) {
        if (a.index() != b.index()) return false;
        return std::visit([&]<class X>(X const& x) { return equal(x, std::get<X>(b)); }, a);
    }
    else if constexpr (tuple_like<T>) {
        return [&]<std::size_t... I>(std::index_sequence<I...>) { return (equal(std::get<I>(a), std::get<I>(b)) && ...); }(std::make_index_sequence<std::tuple_size_v<T>>{});
    }
    else if constexpr (byte_like<T>) return std::ranges::equal(a, b);
    else if constexpr (map_like<T> && requires { typename T::key_type; }) {
        // Keyed containers: order-insensitive, since unordered ones iterate differently after
        // a round trip.
        if (std::ranges::distance(a) != std::ranges::distance(b)) return false;
        for (auto const& ka : a) {
            bool found = false;
            for (auto const& kb : b)
                if (equal(std::get<0>(ka), std::get<0>(kb))) { found = equal(std::get<1>(ka), std::get<1>(kb)); break; }
            if (!found) return false;
        }
        return true;
    }
    else if constexpr (map_like<T>) {
        // A range of pairs may be reordered by a format that sorts map keys (deterministic
        // CBOR); compare as multisets of entries.
        if (std::ranges::distance(a) != std::ranges::distance(b)) return false;
        std::vector<bool> used(static_cast<std::size_t>(std::ranges::distance(b)), false);
        for (auto const& ea : a) {
            bool found = false;
            std::size_t i = 0;
            for (auto const& eb : b) {
                if (!used[i] && equal(std::get<0>(ea), std::get<0>(eb)) && equal(std::get<1>(ea), std::get<1>(eb))) { used[i] = true; found = true; break; }
                ++i;
            }
            if (!found) return false;
        }
        return true;
    }
    else if constexpr (fixed_array<T> || sequence<T>) {
        if (std::ranges::distance(a) != std::ranges::distance(b)) return false;
        auto ia = std::ranges::begin(a); auto ib = std::ranges::begin(b);
        for (; ia != std::ranges::end(a); ++ia, ++ib) if (!equal(*ia, *ib)) return false;
        return true;
    }
    else if constexpr (reflectable_class<T>) {
        bool ok = true;
        template for (constexpr auto f : fields_of<T>) {
            constexpr field_info fi = f.info;
            if constexpr (!fi.skip_serializing() && !fi.skip_deserializing())
                ok = ok && equal(access<f>(a), access<f>(b));
        }
        return ok;
    }
    else static_assert(false, "no equality for this type");
}

} // namespace vcodec_test

// Wire name → field index (spec §6.2). One interface so the mechanism can change on
// benchmark evidence without touching the parser:
//   N ≤ 8 : length pre-filter, then memcmp linear scan
//   N > 8 : consteval hash over (length, first byte, last byte) into power-of-two buckets,
//           each bucket a short linear run. Seeds are tried to minimise the longest bucket.
// Aliases are extra entries mapping to the same field index. Case-sensitive always.
#ifndef VCODEC_CORE_LOOKUP_HPP
#define VCODEC_CORE_LOOKUP_HPP

#include <vcodec/core/schema.hpp>

#include <meta>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace vcodec::core {

inline constexpr std::size_t npos = static_cast<std::size_t>(-1);
inline constexpr std::size_t linear_scan_limit = 8;

struct lookup_entry {
    static_string key;
    std::uint32_t index;
};

struct lookup_table {
    const lookup_entry*  entries = nullptr;
    std::size_t          count = 0;
    const std::uint32_t* bucket_start = nullptr;   // bucket_count + 1 offsets into entries
    std::size_t          bucket_count = 0;         // 0 → linear scan
    std::uint32_t        seed = 0;

    static constexpr std::uint32_t hash(std::string_view k, std::uint32_t seed) noexcept {
        std::uint32_t h = static_cast<std::uint32_t>(k.size()) * 0x9E3779B1u;
        if (!k.empty()) {
            h ^= static_cast<unsigned char>(k.front()) * 0x85EBCA6Bu;
            h ^= static_cast<unsigned char>(k.back())  * 0xC2B2AE35u;
        }
        h *= seed | 1u;
        return h ^ (h >> 15);
    }

    constexpr std::size_t find(std::string_view key) const noexcept {
        if (bucket_count == 0) {
            for (std::size_t i = 0; i < count; ++i) {
                auto const& e = entries[i];
                if (e.key.len == key.size() && equal(e.key.ptr, key.data(), key.size()))
                    return e.index;
            }
            return npos;
        }
        std::size_t b = hash(key, seed) & (bucket_count - 1);
        for (std::uint32_t i = bucket_start[b], end = bucket_start[b + 1]; i < end; ++i) {
            auto const& e = entries[i];
            if (e.key.len == key.size() && equal(e.key.ptr, key.data(), key.size()))
                return e.index;
        }
        return npos;
    }

private:
    static constexpr bool equal(const char* a, const char* b, std::size_t n) noexcept {
        if consteval {
            for (std::size_t i = 0; i < n; ++i) if (a[i] != b[i]) return false;
            return true;
        } else {
            return n == 0 || std::memcmp(a, b, n) == 0;
        }
    }
};

// Build from (key, index) pairs. Keys must be distinct; the schema layer guarantees that.
consteval lookup_table build_lookup(std::vector<lookup_entry> entries) {
    lookup_table t{};
    if (entries.size() <= linear_scan_limit) {
        auto sp = std::define_static_array(entries);
        t.entries = sp.data(); t.count = sp.size();
        return t;
    }
    std::size_t m = 1;
    while (m < entries.size() * 2) m <<= 1;

    std::uint32_t best_seed = 1; std::size_t best_worst = entries.size() + 1;
    for (std::uint32_t seed = 1; seed < 64; ++seed) {
        std::vector<std::size_t> load(m, 0);
        std::size_t worst = 0;
        for (auto const& e : entries) {
            auto& l = load[lookup_table::hash(e.key.view(), seed) & (m - 1)];
            worst = std::max(worst, ++l);
        }
        if (worst < best_worst) { best_worst = worst; best_seed = seed; }
        if (worst == 1) break;
    }
    std::vector<lookup_entry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [&](lookup_entry const& a, lookup_entry const& b) {
        auto ha = lookup_table::hash(a.key.view(), best_seed) & (m - 1);
        auto hb = lookup_table::hash(b.key.view(), best_seed) & (m - 1);
        return ha != hb ? ha < hb : a.index < b.index;
    });
    std::vector<std::uint32_t> starts(m + 1, 0);
    for (auto const& e : sorted) ++starts[(lookup_table::hash(e.key.view(), best_seed) & (m - 1)) + 1];
    for (std::size_t i = 1; i <= m; ++i) starts[i] += starts[i - 1];

    auto sp = std::define_static_array(sorted);
    auto bs = std::define_static_array(starts);
    t.entries = sp.data(); t.count = sp.size();
    t.bucket_start = bs.data(); t.bucket_count = m; t.seed = best_seed;
    return t;
}

// skip_deserializing members stay in the table: their wire name is known and is skipped
// silently, not rejected by deny_unknown_fields. The struct decoder checks the flag.
template<class T>
consteval lookup_table build_field_lookup() {
    std::vector<lookup_entry> v;
    for (auto const& f : schema_of<T>) {
        v.push_back({ f.wire_name, static_cast<std::uint32_t>(f.member_index) });
        for (auto a : f.aliases()) v.push_back({ a, static_cast<std::uint32_t>(f.member_index) });
    }
    return build_lookup(std::move(v));
}

template<class E>
consteval lookup_table build_enum_lookup() {
    std::vector<lookup_entry> v;
    std::uint32_t i = 0;
    for (auto const& e : enum_schema_of<E>) {
        if (!e.skipped) {
            v.push_back({ e.name, i });
            for (auto a : e.aliases()) v.push_back({ a, i });
        }
        ++i;
    }
    return build_lookup(std::move(v));
}

template<class T> inline constexpr lookup_table lookup_of      = build_field_lookup<std::remove_cvref_t<T>>();
template<class E> inline constexpr lookup_table enum_lookup_of = build_enum_lookup<std::remove_cvref_t<E>>();

// Did-you-mean (§9.4): Levenshtein against the field table, suggesting at distance ≤ 2.
// Error path only.
inline std::size_t levenshtein(std::string_view a, std::string_view b) noexcept {
    if (a.size() > b.size()) std::swap(a, b);
    std::vector<std::size_t> prev(a.size() + 1), cur(a.size() + 1);
    for (std::size_t i = 0; i <= a.size(); ++i) prev[i] = i;
    for (std::size_t j = 1; j <= b.size(); ++j) {
        cur[0] = j;
        for (std::size_t i = 1; i <= a.size(); ++i) {
            std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[i] = std::min({ prev[i] + 1, cur[i - 1] + 1, prev[i - 1] + cost });
        }
        std::swap(prev, cur);
    }
    return prev[a.size()];
}

inline std::string_view suggest(std::string_view key, std::span<const field_info> schema,
                                std::size_t max_distance = 2) noexcept {
    std::string_view best; std::size_t best_d = max_distance + 1;
    for (auto const& f : schema) {
        if (f.skip_deserializing()) continue;
        auto d = levenshtein(key, f.wire_name.view());
        if (d < best_d) { best_d = d; best = f.wire_name.view(); }
    }
    return best;
}

} // namespace vcodec::core

#endif // VCODEC_CORE_LOOKUP_HPP

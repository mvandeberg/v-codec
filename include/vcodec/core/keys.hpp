// Per-format struct keying (spec v0.2 §6). The format-free schema keeps text names; a format
// may assign integer keys (CBOR's cbor::key) and an emission order (deterministic encoding).
// Both are consteval, live in static storage, and are consulted by traversal and construction
// through format_traits<Format>.
#ifndef VCODEC_CORE_KEYS_HPP
#define VCODEC_CORE_KEYS_HPP

#include <vcodec/core/lookup.hpp>
#include <vcodec/core/model.hpp>
#include <vcodec/core/schema.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace vcodec::core {

struct int_key {
    bool         present = false;
    std::int64_t value = 0;
};

struct int_key_entry {
    std::int64_t  key;
    std::uint32_t index;
};

// Integer key → field index. Dense when the keys span a small range (the COSE case), sorted
// with binary search otherwise. Runtime-usable, structural.
struct key_table {
    const int_key*        keys = nullptr;     // per field index
    std::size_t           field_count = 0;
    bool                  dense = false;
    std::int64_t          base = 0;           // dense: key of slot 0
    const std::uint32_t*  slots = nullptr;    // dense: field index or UINT32_MAX
    std::size_t           slot_count = 0;
    const int_key_entry*  sorted = nullptr;   // sparse: sorted by key
    std::size_t           sorted_count = 0;
    static_string         error{};            // duplicate keys, §10 text

    constexpr bool ok() const noexcept { return error.len == 0; }
    constexpr bool any() const noexcept { return dense ? slot_count > 0 : sorted_count > 0; }

    constexpr std::size_t find(std::int64_t k) const noexcept {
        if (dense) {
            if (k < base || k - base >= static_cast<std::int64_t>(slot_count)) return npos;
            auto v = slots[k - base];
            return v == UINT32_MAX ? npos : v;
        }
        std::size_t lo = 0, hi = sorted_count;
        while (lo < hi) {
            std::size_t mid = (lo + hi) / 2;
            if (sorted[mid].key < k) lo = mid + 1;
            else if (sorted[mid].key > k) hi = mid;
            else return sorted[mid].index;
        }
        return npos;
    }
};

template<class T, class Format>
consteval key_table build_key_table() {
    key_table t{};
    std::vector<int_key> keys;
    std::vector<int_key_entry> entries;
    fixed_string<512> err;
    template for (constexpr auto f : fields_of<T>) {
        constexpr auto k = format_traits<Format>::template integer_key<f>();
        constexpr field_info fi = f.info;
        if constexpr (k.has_value()) {
            for (auto const& e : entries) {
                if (e.key == *k && err.size() == 0) {
                    err.append(schema_of<T>[e.index].qualified.view()).append(" and ").append(fi.qualified.view())
                       .append(" both map to the ").append(format_traits<Format>::name).append(" key ")
                       .append_int(*k).append(" (via ").append(format_traits<Format>::integer_key_spelling)
                       .append("(").append_int(*k).append(")).");
                }
            }
            entries.push_back({ *k, static_cast<std::uint32_t>(fi.member_index) });
            keys.push_back({ true, *k });
        } else {
            keys.push_back({ false, 0 });
        }
    }
    auto ks = std::define_static_array(keys);
    t.keys = ks.data(); t.field_count = ks.size();
    if (!entries.empty()) {
        std::sort(entries.begin(), entries.end(), [](auto const& a, auto const& b) { return a.key < b.key; });
        std::int64_t lo = entries.front().key, hi = entries.back().key;
        if (hi - lo < 256) {
            std::vector<std::uint32_t> slots(static_cast<std::size_t>(hi - lo + 1), UINT32_MAX);
            for (auto const& e : entries) slots[static_cast<std::size_t>(e.key - lo)] = e.index;
            auto sp = std::define_static_array(slots);
            t.dense = true; t.base = lo; t.slots = sp.data(); t.slot_count = sp.size();
        } else {
            auto sp = std::define_static_array(entries);
            t.sorted = sp.data(); t.sorted_count = sp.size();
        }
    }
    if (err.size()) t.error = intern(err.view());
    return t;
}

template<class T, class Format>
inline constexpr key_table key_table_of = build_key_table<std::remove_cvref_t<T>, Format>();

// Emission order of a struct's members under a format.
template<class T, class Format>
consteval std::span<const std::size_t> build_member_order() {
    auto v = format_traits<Format>::template member_order<std::remove_cvref_t<T>>(fields_of<T>.size());
    return std::define_static_array(v);
}
template<class T, class Format>
inline constexpr std::span<const std::size_t> member_order_of = build_member_order<T, Format>();

// Tags expected before a member's value.
template<field_meta F, class Format>
consteval std::span<const std::uint64_t> build_expected_tags() {
    auto v = format_traits<Format>::template expected_tags<F>();
    return std::define_static_array(v);
}
template<field_meta F, class Format>
inline constexpr std::span<const std::uint64_t> expected_tags_of = build_expected_tags<F, Format>();

} // namespace vcodec::core

#endif // VCODEC_CORE_KEYS_HPP

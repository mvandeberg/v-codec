// The CBOR sink (spec v0.2 §7). Shortest-form heads always; under `deterministic` (the
// default) definite lengths, preferred floats and canonical key order, with runtime maps and
// unsized ranges buffered and sorted. Struct maps arrive pre-ordered from core and are never
// buffered.
#ifndef VCODEC_CBOR_WRITE_HPP
#define VCODEC_CBOR_WRITE_HPP

#include <vcodec/cbor/annotations.hpp>
#include <vcodec/cbor/head.hpp>
#include <vcodec/cbor/options.hpp>
#include <vcodec/cbor/traits.hpp>
#include <vcodec/core/lower.hpp>
#include <vcodec/core/model.hpp>
#include <vcodec/core/traverse.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>
#include <vector>

namespace vcodec::cbor {

template<options Opts = {}>
class writer {
public:
    using format = cbor::format;
    static constexpr options opts = Opts;
    static_assert(!(Opts.deterministic && Opts.indefinite), "cbor::options: indefinite lengths are not deterministic; pick one");

    explicit writer(std::vector<std::byte>& out) noexcept : out_(out) {}

    // ---- core::sink ----
    void null()                { item(); put(0xF6); }
    void boolean(bool b)       { item(); put(b ? 0xF5 : 0xF4); }
    void uint(std::uint64_t u) { item(); head(major::uint, u); }
    void sint(std::int64_t i)  { item(); if (i >= 0) head(major::uint, std::uint64_t(i)); else head(major::nint, std::uint64_t(-1 - i)); }
    void real(double d)        { item(); write_real(d, Opts.floats); }
    void text(std::string_view t) {
        item();
        if constexpr (Opts.validate_utf8) check_utf8(t);
        head(major::text, t.size());
        append(t.data(), t.size());
    }
    void bytes(std::span<const std::byte> b) { item(); head(major::bytes, b.size()); append(b.data(), b.size()); }
    void begin_array(std::optional<std::size_t> n) { item(); open(major::array, n, false); }
    void end_array()           { close(); }
    void begin_map(std::optional<std::size_t> n)   { item(); open(major::map, n, true); }
    void end_map()             { close(); }
    void key(std::string_view k) {
        key_start();
        if constexpr (Opts.validate_utf8) check_utf8(k);
        head(major::text, k.size()); append(k.data(), k.size());
        key_end();
    }
    void key(std::uint64_t k)  { key_start(); head(major::uint, k); key_end(); }
    void key(std::int64_t k)   { key_start(); if (k >= 0) head(major::uint, std::uint64_t(k)); else head(major::nint, std::uint64_t(-1 - k)); key_end(); }
    void tag(std::uint64_t t)  { head(major::tag, t); }   // not an item: it prefixes one

    // ---- hooks ----
    template<core::field_meta F>
    void real(double d) {
        constexpr width w = [] {
            if (auto a = core::find_annotation<float_width_t>(F.anns())) return a->w;
            return Opts.floats;
        }();
        item(); write_real(d, w);
    }
    template<core::field_meta F>
    void text(std::string_view t) {
        if constexpr (core::has_annotation<indefinite_t>(F.anns()) && !Opts.deterministic) {
            item();
            if constexpr (Opts.validate_utf8) check_utf8(t);
            put(0x7F);
            for (std::size_t i = 0; i < t.size(); i += chunk_size) {
                auto n = std::min(chunk_size, t.size() - i);
                head(major::text, n); append(t.data() + i, n);
            }
            put(break_byte);
        } else text(t);
    }
    template<core::field_meta F>
    void bytes(std::span<const std::byte> b) {
        if constexpr (core::has_annotation<indefinite_t>(F.anns()) && !Opts.deterministic) {
            item();
            put(0x5F);
            for (std::size_t i = 0; i < b.size(); i += chunk_size) {
                auto n = std::min(chunk_size, b.size() - i);
                head(major::bytes, n); append(b.data() + i, n);
            }
            put(break_byte);
        } else bytes(b);
    }
    // A struct's members arrive in canonical order: no buffering, even when deterministic.
    void begin_map_ordered(std::optional<std::size_t> n) {
        item();
        if (n && (Opts.deterministic || !Opts.indefinite)) {
            if (VCODEC_UNLIKELY(frames_.size() >= Opts.max_depth))
                throw encode_error(errc::depth_exceeded, "nesting depth exceeds max_depth (" + std::to_string(Opts.max_depth) + "); a cycle through smart pointers is the usual cause");
            head(major::map, *n); push_frame(true, false, false);
        }
        else open(major::map, n, true);
    }
    // RFC 8746 typed array: one tag, one byte string, one memcpy.
    template<core::field_meta F, class R>
    void write_range(R const& r) {
        using E = std::remove_cvref_t<std::ranges::range_value_t<R>>;
        item();
        head(major::tag, typed_array_tag<E>());
        auto bytes_n = std::ranges::size(r) * sizeof(E);
        head(major::bytes, bytes_n);
        auto old = cur().size();
        cur().resize(old + bytes_n);
        if (bytes_n) std::memcpy(cur().data() + old, std::ranges::data(r), bytes_n);
    }
    template<core::static_string Name>
    void prepared_key() {
        static constexpr auto blob = [] {
            auto v = encode_text_key(Name.view());
            core::fixed_string<Name.size() + 9> b;   // reuse fixed_string as a byte buffer
            for (auto x : v) b.append(static_cast<char>(x));
            return b;
        }();
        key_start(); append(blob.data(), blob.size()); key_end();
    }
    template<std::int64_t Key>
    void prepared_key() {
        static constexpr auto blob = [] {
            auto v = encode_int_key(Key);
            core::fixed_string<9> b;
            for (auto x : v) b.append(static_cast<char>(x));
            return b;
        }();
        key_start(); append(blob.data(), blob.size()); key_end();
    }

    std::size_t depth() const noexcept { return frames_.size(); }

private:
    static constexpr std::size_t chunk_size = 4096;

    struct frame {
        bool is_map = false;
        bool buffered = false;    // body collected in `body`, head emitted at close
        bool indefinite = false;  // 0x9F/0xBF opened, break at close
        std::size_t items = 0;
        std::vector<std::byte> body{};
        struct entry { std::size_t start, key_end; };
        std::vector<entry> entries{};     // maps: one per key
    };
    void push_frame(bool is_map, bool buffered, bool indefinite) {
        frame f; f.is_map = is_map; f.buffered = buffered; f.indefinite = indefinite;
        frames_.push_back(std::move(f));
    }

    std::vector<std::byte>& out_;
    std::vector<frame> frames_;

    std::vector<std::byte>& cur() noexcept {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) if (it->buffered) return it->body;
        return out_;
    }
    void put(std::uint8_t b) { cur().push_back(std::byte(b)); }
    void append(const void* p, std::size_t n) {
        auto& c = cur();
        auto old = c.size();
        c.resize(old + n);
        if (n) std::memcpy(c.data() + old, p, n);
    }
    void head(major m, std::uint64_t v) {
        auto h = make_head(m, v);
        append(h.bytes.data(), h.size);
    }
    // Called for every value that is an item of the innermost container.
    void item() {
        if (!frames_.empty()) ++frames_.back().items;
    }
    void key_start() {
        auto& f = frames_.back();
        if (f.buffered) f.entries.push_back({ f.body.size(), 0 });
    }
    void key_end() {
        auto& f = frames_.back();
        if (f.buffered) f.entries.back().key_end = f.body.size();
    }
    void open(major m, std::optional<std::size_t> n, bool is_map) {
        if (VCODEC_UNLIKELY(frames_.size() >= Opts.max_depth))
            throw encode_error(errc::depth_exceeded, "nesting depth exceeds max_depth (" + std::to_string(Opts.max_depth) + "); a cycle through smart pointers is the usual cause");
        if constexpr (Opts.deterministic) {
            // Maps are buffered to sort; unsized arrays are buffered to count.
            bool buffer = is_map || !n;
            if (buffer) { push_frame(is_map, true, false); return; }
            head(m, *n);
            push_frame(is_map, false, false);
        } else {
            // Non-deterministic: definite when the count is known and indefinite lengths were
            // not asked for, indefinite otherwise.
            if (n && !Opts.indefinite) { head(m, *n); push_frame(is_map, false, false); }
            else {
                auto h = make_indefinite_head(m);
                append(h.bytes.data(), h.size);
                push_frame(is_map, false, true);
            }
        }
    }
    void close() {
        frame f = std::move(frames_.back());
        frames_.pop_back();
        if (f.indefinite) { put(break_byte); return; }
        if (!f.buffered) return;
        if (f.is_map) {
            // Sort entries by the bytewise lexicographic order of their encoded keys.
            struct slice { std::size_t start, key_end, end; };
            std::vector<slice> order;
            order.reserve(f.entries.size());
            for (std::size_t i = 0; i < f.entries.size(); ++i)
                order.push_back({ f.entries[i].start, f.entries[i].key_end, i + 1 < f.entries.size() ? f.entries[i + 1].start : f.body.size() });
            auto key_of = [&](slice const& e) { return std::span<const std::byte>(f.body.data() + e.start, e.key_end - e.start); };
            auto lt = [](std::byte x, std::byte y) { return std::to_integer<unsigned>(x) < std::to_integer<unsigned>(y); };
            std::stable_sort(order.begin(), order.end(), [&](slice const& a, slice const& b) {
                auto ka = key_of(a), kb = key_of(b);
                return std::lexicographical_compare(ka.begin(), ka.end(), kb.begin(), kb.end(), lt);
            });
            for (std::size_t i = 1; i < order.size(); ++i)
                if (std::ranges::equal(key_of(order[i - 1]), key_of(order[i])))
                    throw encode_error(errc::duplicate_key, "deterministic encoding forbids duplicate map keys");
            head(major::map, order.size());
            for (auto const& e : order) append(f.body.data() + e.start, e.end - e.start);
        } else {
            head(major::array, f.items);
            append(f.body.data(), f.body.size());
        }
    }
    void write_real(double d, width w) {
        switch (w) {
        case width::preferred:
            if (auto h = core::to_half(d)) { put(0xF9); be16(*h); return; }
            if (core::fits_single(d)) { put(0xFA); be32(std::bit_cast<std::uint32_t>(static_cast<float>(d))); return; }
            put(0xFB); be64(std::bit_cast<std::uint64_t>(d)); return;
        case width::half: {
            if (std::isfinite(d) && std::fabs(d) >= 65520.0)
                throw encode_error(errc::out_of_range, "value " + std::to_string(d) + " does not fit a half-precision float ([[=cbor::float_width(half)]])");
            put(0xF9); be16(core::to_half_rounded(d)); return;
        }
        case width::single: {
            if (std::isfinite(d) && std::fabs(d) > static_cast<double>(std::numeric_limits<float>::max()))
                throw encode_error(errc::out_of_range, "value " + std::to_string(d) + " does not fit a single-precision float ([[=cbor::float_width(single)]])");
            put(0xFA); be32(std::bit_cast<std::uint32_t>(static_cast<float>(d))); return;
        }
        case width::double_:
            put(0xFB); be64(std::bit_cast<std::uint64_t>(d)); return;
        }
    }
    void be16(std::uint16_t v) { put(std::uint8_t(v >> 8)); put(std::uint8_t(v)); }
    void be32(std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) put(std::uint8_t(v >> s)); }
    void be64(std::uint64_t v) { for (int s = 56; s >= 0; s -= 8) put(std::uint8_t(v >> s)); }
    static void check_utf8(std::string_view t) {
        if (std::size_t bad = core::validate_utf8(t); bad != core::utf8_npos)
            throw encode_error(errc::invalid_utf8, "text string contains invalid UTF-8 at byte " + std::to_string(bad));
    }
};
static_assert(core::sink<writer<>>);

namespace detail {

template<options Opts = {}, class T>
void encode_append(T const& v, std::vector<std::byte>& out) {
    if constexpr (Opts.self_describe || wants_self_describe<T>) append_head(out, major::tag, self_describe_tag);
    writer<Opts> w(out);
    core::encode(w, v);
}

template<options Opts = {}, class T>
std::vector<std::byte> encode(T const& v) {
    std::vector<std::byte> out;
    encode_append<Opts>(v, out);
    return out;
}

} // namespace detail

} // namespace vcodec::cbor

#endif // VCODEC_CBOR_WRITE_HPP

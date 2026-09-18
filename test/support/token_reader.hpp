// A reader over a pre-built token stream (format-free), so decode-side construction can be
// tested without JSON. Offsets are token indices.
#pragma once
#include <vcodec/core/model.hpp>

#include "recording_sink.hpp"

#include <span>
#include <string>
#include <vector>

namespace vcodec_test {

namespace vc = vcodec;
using vc::core::kind;

struct tok {
    kind                   k = kind::undefined;
    bool                   end = false;       // end of array/map
    bool                   b = false;
    std::uint64_t          u = 0;
    std::int64_t           i = 0;
    double                 d = 0;
    std::string            s;
    bool                   escaped = false;   // text that cannot be borrowed
    std::vector<std::byte> bytes;
};

inline tok mk(kind k, bool end = false) { tok t; t.k = k; t.end = end; return t; }
inline tok t_null()               { return mk(kind::null); }
inline tok t_bool(bool b)         { auto t = mk(kind::boolean); t.b = b; return t; }
inline tok t_uint(std::uint64_t u){ auto t = mk(kind::uint); t.u = u; return t; }
inline tok t_sint(std::int64_t i) { auto t = mk(kind::sint); t.i = i; return t; }
inline tok t_real(double d)       { auto t = mk(kind::real); t.d = d; return t; }
inline tok t_text(std::string s, bool escaped = false) { auto t = mk(kind::text); t.s = std::move(s); t.escaped = escaped; return t; }
inline tok t_bytes(std::vector<std::byte> b) { auto t = mk(kind::bytes); t.bytes = std::move(b); return t; }
inline tok t_arr()                { return mk(kind::array); }
inline tok t_end_arr()            { return mk(kind::array, true); }
inline tok t_map()                { return mk(kind::map); }
inline tok t_end_map()            { return mk(kind::map, true); }
inline tok t_key(std::string s)   { return t_text(std::move(s)); }
inline tok t_tag(std::uint64_t t) { auto k = mk(kind::tag); k.u = t; return k; }

template<vc::duplicate_key Dup = vc::duplicate_key::last_wins, vc::error_mode Errs = vc::error_mode::fail_fast, class Format = void>
class token_reader {
public:
    using format = Format;
    static constexpr vc::duplicate_key duplicates = Dup;
    static constexpr vc::error_mode errors = Errs;

    explicit token_reader(std::vector<tok> toks) : toks_(std::move(toks)) {}

    std::vector<vc::error>& collected() { return collected_; }

    vc::error error(vc::errc code, std::size_t off) const { return vc::error(code, off); }

    std::size_t offset() const noexcept { return pos_; }
    std::size_t save() const noexcept { return pos_; }
    void restore(std::size_t p) noexcept { pos_ = p; }

    kind peek() const noexcept { return pos_ < toks_.size() ? toks_[pos_].k : kind::undefined; }

    // Tags: consumed only when expected; an unexpected one is a mismatch.
    vc::status expect_tags(std::span<const std::uint64_t> expected) {
        for (auto t : expected) {
            if (pos_ >= toks_.size()) return std::unexpected(vc::error(vc::errc::truncated, pos_));
            if (toks_[pos_].k != kind::tag || toks_[pos_].u != t)
                return std::unexpected(vc::error(vc::errc::tag_mismatch, pos_).with_expected("tag " + std::to_string(t))
                    .with_found(toks_[pos_].k == kind::tag ? "tag " + std::to_string(toks_[pos_].u) : std::string("no tag")));
            ++pos_;
        }
        return {};
    }
    // Bulk range: a "bulk" text token followed by the element count, else not handled.
    template<vc::core::field_meta F, class R>
    vc::result<bool> read_range(R& out) {
        if (pos_ < toks_.size() && toks_[pos_].k == kind::text && toks_[pos_].s == "bulk") {
            ++pos_;
            auto n = expect_uint(); if (!n) return std::unexpected(std::move(n.error()));
            out.clear();
            for (std::uint64_t i = 0; i < *n; ++i) out.push_back(typename R::value_type(i));
            return true;
        }
        return false;
    }

    vc::status expect_null() { if (auto e = check(kind::null, "null")) return std::unexpected(*e); ++pos_; return {}; }
    vc::result<bool> expect_boolean() { if (auto e = check(kind::boolean, "boolean")) return std::unexpected(*e); return toks_[pos_++].b; }
    vc::result<std::uint64_t> expect_uint() {
        if (pos_ < toks_.size() && toks_[pos_].k == kind::sint)
            return std::unexpected(vc::error(vc::errc::out_of_range, pos_).with_detail(std::to_string(toks_[pos_].i)));
        if (auto e = check(kind::uint, "integer")) return std::unexpected(*e);
        return toks_[pos_++].u;
    }
    vc::result<std::int64_t> expect_sint() {
        if (pos_ < toks_.size() && toks_[pos_].k == kind::uint) {
            if (toks_[pos_].u > std::uint64_t(INT64_MAX))
                return std::unexpected(vc::error(vc::errc::out_of_range, pos_).with_detail(std::to_string(toks_[pos_].u)));
            return std::int64_t(toks_[pos_++].u);
        }
        if (auto e = check(kind::sint, "integer")) return std::unexpected(*e);
        return toks_[pos_++].i;
    }
    vc::result<double> expect_real() {
        if (pos_ < toks_.size() && toks_[pos_].k == kind::uint) return double(toks_[pos_++].u);
        if (pos_ < toks_.size() && toks_[pos_].k == kind::sint) return double(toks_[pos_++].i);
        if (auto e = check(kind::real, "number")) return std::unexpected(*e);
        return toks_[pos_++].d;
    }
    vc::result<vc::core::text_ref> expect_text() {
        if (auto e = check(kind::text, "string")) return std::unexpected(*e);
        auto const& t = toks_[pos_++];
        return vc::core::text_ref{ t.s, !t.escaped };
    }
    vc::result<vc::core::bytes_ref> expect_bytes() {
        if (auto e = check(kind::bytes, "bytes")) return std::unexpected(*e);
        auto const& t = toks_[pos_++];
        return vc::core::bytes_ref{ t.bytes, true };
    }

    struct seq_cursor {
        token_reader* r;
        vc::result<bool> next() {
            if (r->pos_ >= r->toks_.size()) return std::unexpected(vc::error(vc::errc::truncated, r->pos_));
            if (r->toks_[r->pos_].end) { ++r->pos_; return false; }
            return true;
        }
        std::optional<std::size_t> remaining() const { return std::nullopt; }
    };
    struct map_cursor {
        token_reader* r;
        vc::result<bool> next() {
            if (r->pos_ >= r->toks_.size()) return std::unexpected(vc::error(vc::errc::truncated, r->pos_));
            if (r->toks_[r->pos_].end) { ++r->pos_; return false; }
            return true;
        }
        kind key_kind() const { return r->peek(); }   // text, uint or sint keys
        vc::result<vc::core::text_ref> key_text() { return r->expect_text(); }
        vc::result<std::uint64_t> key_uint() { return r->expect_uint(); }
        vc::result<std::int64_t> key_sint() { return r->expect_sint(); }
        std::optional<std::size_t> remaining() const { return std::nullopt; }
    };

    vc::result<seq_cursor> expect_array() { if (auto e = check(kind::array, "array")) return std::unexpected(*e); ++pos_; return seq_cursor{this}; }
    vc::result<map_cursor> expect_map()   { if (auto e = check(kind::map, "map")) return std::unexpected(*e); ++pos_; return map_cursor{this}; }

    vc::status skip_value() {
        if (pos_ >= toks_.size()) return std::unexpected(vc::error(vc::errc::truncated, pos_));
        auto const& t = toks_[pos_];
        if (t.end) return std::unexpected(vc::error(vc::errc::unexpected_token, pos_));
        ++pos_;
        if (t.k == kind::array || t.k == kind::map) {
            for (;;) {
                if (pos_ >= toks_.size()) return std::unexpected(vc::error(vc::errc::truncated, pos_));
                if (toks_[pos_].end) { ++pos_; return {}; }
                if (t.k == kind::map) { auto k = skip_value(); if (!k) return k; }
                auto v = skip_value(); if (!v) return v;
            }
        }
        return {};
    }

    bool at_end() const noexcept { return pos_ == toks_.size(); }

private:
    std::vector<tok> toks_;
    std::size_t pos_ = 0;
    std::vector<vc::error> collected_;

    std::optional<vc::error> check(kind k, std::string_view expected) const {
        if (pos_ >= toks_.size()) return vc::error(vc::errc::truncated, pos_);
        auto const& t = toks_[pos_];
        if (t.k != k || t.end) {
            auto found = t.end ? std::string("end of ") + std::string(vc::core::kind_name(t.k)) : std::string(vc::core::kind_name(t.k));
            return vc::error(vc::errc::type_mismatch, pos_).with_expected(expected).with_found(found).with_length(1);
        }
        return std::nullopt;
    }
};
static_assert(vc::core::reader<token_reader<>>);
static_assert(vc::core::collecting_reader<token_reader<vc::duplicate_key::last_wins, vc::error_mode::collect>>);

} // namespace vcodec_test

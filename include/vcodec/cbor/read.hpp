// The type-directed CBOR reader (spec v0.2 §8). Every expect_* fails without consuming and
// reports what it found in CBOR vocabulary. Definite-length strings borrow; indefinite ones
// are assembled in scratch. Determinism validation (§8.2) is a compile-time option with no
// cost when off.
#ifndef VCODEC_CBOR_READ_HPP
#define VCODEC_CBOR_READ_HPP

#include <vcodec/cbor/annotations.hpp>
#include <vcodec/cbor/head.hpp>
#include <vcodec/cbor/options.hpp>
#include <vcodec/cbor/traits.hpp>
#include <vcodec/core/build.hpp>
#include <vcodec/core/lower.hpp>
#include <vcodec/core/model.hpp>
#include <vcodec/error.hpp>

#include <bit>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace vcodec::cbor {

struct bignum_ref {
    bool                       negative;   // tag 3: value is -1 - n
    std::span<const std::byte> magnitude;  // big-endian
};

template<options Opts = {}>
class reader {
public:
    using format = cbor::format;
    static constexpr duplicate_key duplicates = Opts.duplicates;
    static constexpr error_mode errors = Opts.errors;

    explicit reader(std::span<const std::byte> input) noexcept : in_(input) {}

    std::vector<vcodec::error>& collected() noexcept { return collected_; }
    std::span<const std::byte> input() const noexcept { return in_; }

    // Binary format: offset only; position() stays nullopt.
    vcodec::error error(errc code, std::size_t offset) const { return vcodec::error(code, offset); }

    std::size_t offset() const noexcept { return pos_; }
    std::size_t save() const noexcept { return pos_ | (depth_ << 48); }
    void restore(std::size_t s) noexcept { pos_ = s & ((std::size_t(1) << 48) - 1); depth_ = s >> 48; }
    bool at_end() const noexcept { return pos_ >= in_.size(); }

    // ---- classification ----
    core::kind peek() const noexcept {
        std::size_t p = pos_;
        for (;;) {
            auto h = parse_head(in_, p);
            if (!h.length || h.reserved || h.is_break) return core::kind::undefined;
            if (h.mt == major::tag) {
                if constexpr (Opts.ignore_unknown_tags) { p += h.length; continue; }
                return core::kind::tag;
            }
            switch (h.mt) {
            case major::uint: return core::kind::uint;
            case major::nint: return core::kind::sint;
            case major::bytes: return core::kind::bytes;
            case major::text: return core::kind::text;
            case major::array: return core::kind::array;
            case major::map: return core::kind::map;
            default: break;
            }
            if (h.ai == 20 || h.ai == 21) return core::kind::boolean;
            if (h.ai == 22) return core::kind::null;
            if (h.ai == 23) return Opts.undefined_as_null ? core::kind::null : core::kind::undefined;
            if (h.ai >= 25 && h.ai <= 27) return core::kind::real;
            return core::kind::undefined;
        }
    }

    // ---- scalars ----
    status expect_null() {
        auto h = item_head("null");
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt == major::simple && (h->ai == 22 || (Opts.undefined_as_null && h->ai == 23))) { pos_ += h->length; return {}; }
        return std::unexpected(type_error("null", *h));
    }
    result<bool> expect_boolean() {
        auto h = item_head("boolean");
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt == major::simple && (h->ai == 20 || h->ai == 21)) { pos_ += h->length; return h->ai == 21; }
        return std::unexpected(type_error("boolean", *h));
    }
    result<std::uint64_t> expect_uint() {
        auto h = item_head("unsigned integer");
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt == major::uint) { pos_ += h->length; return h->value; }
        if (h->mt == major::nint)
            return std::unexpected(error(errc::out_of_range, pos_).with_detail(negative_text(h->value)).with_length(h->length));
        return std::unexpected(type_error("unsigned integer", *h));
    }
    result<std::int64_t> expect_sint() {
        auto h = item_head("integer");
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt == major::uint) {
            if (h->value > std::uint64_t(INT64_MAX))
                return std::unexpected(error(errc::out_of_range, pos_).with_detail(std::to_string(h->value)).with_length(h->length));
            pos_ += h->length; return std::int64_t(h->value);
        }
        if (h->mt == major::nint) {
            if (h->value > std::uint64_t(INT64_MAX))
                return std::unexpected(error(errc::out_of_range, pos_).with_detail(negative_text(h->value)).with_length(h->length));
            pos_ += h->length; return -1 - std::int64_t(h->value);
        }
        return std::unexpected(type_error("integer", *h));
    }
    // An unassigned simple value (0–19, 32–255) for user codecs; the model has no kind for them.
    result<std::uint8_t> expect_simple() {
        auto h = item_head("simple value");
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt != major::simple || (h->ai >= 20 && h->ai <= 23) || (h->ai >= 25 && h->ai <= 27))
            return std::unexpected(type_error("simple value", *h));
        pos_ += h->length; return static_cast<std::uint8_t>(h->value);
    }
    // The raw argument n of a negative integer (value = -1 - n), for values below INT64_MIN.
    result<std::uint64_t> expect_nint() {
        auto h = item_head("negative integer");
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt != major::nint) return std::unexpected(type_error("negative integer", *h));
        pos_ += h->length; return h->value;
    }
    result<double> expect_real() {
        auto h = item_head("float");
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt == major::uint) { pos_ += h->length; return double(h->value); }
        if (h->mt == major::nint) { pos_ += h->length; return -1.0 - double(h->value); }
        if (h->mt == major::simple && h->ai >= 25 && h->ai <= 27) {
            double d = h->ai == 25 ? core::from_half(static_cast<std::uint16_t>(h->value))
                     : h->ai == 26 ? double(std::bit_cast<float>(static_cast<std::uint32_t>(h->value)))
                                   : std::bit_cast<double>(h->value);
            if constexpr (Opts.require_deterministic) {
                if (auto e = check_preferred_float(d, h->ai, h->value)) return std::unexpected(std::move(*e));
            }
            pos_ += h->length; return d;
        }
        return std::unexpected(type_error("float", *h));
    }

    // ---- strings and bytes ----
    result<core::text_ref> expect_text() {
        auto h = item_head("text string");
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt != major::text) return std::unexpected(type_error("text string", *h));
        auto r = read_string(*h, major::text, text_scratch_);
        if (!r) return std::unexpected(std::move(r.error()));
        std::span<const std::byte> bytes = r->second ? std::span<const std::byte>(text_scratch_) : r->first;
        std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if constexpr (Opts.validate_utf8) {
            if (std::size_t bad = core::validate_utf8(text); bad != core::utf8_npos)
                return std::unexpected(error(errc::invalid_utf8, start_ + (r->second ? 0 : h->length + bad)).with_length(1));
        }
        return core::text_ref{ text, !r->second, pos_ - start_ };
    }
    result<core::bytes_ref> expect_bytes() {
        auto h = item_head("byte string");
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt != major::bytes) return std::unexpected(type_error("byte string", *h));
        auto r = read_string(*h, major::bytes, bytes_scratch_);
        if (!r) return std::unexpected(std::move(r.error()));
        if (r->second) return core::bytes_ref{ std::span<const std::byte>(bytes_scratch_), false };
        return core::bytes_ref{ r->first, true };
    }
    // Tags 2 / 3 for user codecs.
    result<bignum_ref> expect_bignum() {
        auto h = raw_head();
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt != major::tag || (h->value != 2 && h->value != 3))
            return std::unexpected(error(errc::tag_mismatch, pos_).with_expected("tag 2 or 3 (bignum)").with_found(found_name(*h)).with_length(h->length));
        std::size_t save_pos = pos_;
        pos_ += h->length;
        auto b = expect_bytes();
        if (!b) { pos_ = save_pos; return std::unexpected(std::move(b.error())); }
        return bignum_ref{ h->value == 3, b->bytes };
    }

    // ---- tags ----
    status expect_tags(std::span<const std::uint64_t> expected) {
        for (auto t : expected) {
            auto h = raw_head();
            if (!h) return std::unexpected(std::move(h.error()));
            if (h->mt != major::tag || h->value != t) {
                std::string exp = "tag " + std::to_string(t);
                if (auto n = tag_name(t); !n.empty()) exp += " (" + std::string(n) + ")";
                return std::unexpected(error(errc::tag_mismatch, pos_).with_expected(exp)
                    .with_found(h->mt == major::tag ? tag_text(h->value) : std::string("no tag")).with_length(h->length));
            }
            pos_ += h->length;
        }
        return {};
    }

    // ---- containers ----
    class seq_cursor {
    public:
        seq_cursor(reader* r, std::optional<std::size_t> n) noexcept : r_(r), remaining_(n) {}
        result<bool> next() {
            if (remaining_) {
                if (*remaining_ == 0) { --r_->depth_; return false; }
                --*remaining_;
                if (r_->pos_ >= r_->in_.size()) return std::unexpected(r_->error(errc::truncated, r_->pos_).with_expected("array element"));
                return true;
            }
            if (r_->pos_ >= r_->in_.size()) return std::unexpected(r_->error(errc::truncated, r_->pos_).with_expected("array element or break"));
            if (r_->in_[r_->pos_] == std::byte(break_byte)) { ++r_->pos_; --r_->depth_; return false; }
            return true;
        }
        std::optional<std::size_t> remaining() const noexcept { return remaining_; }
    private:
        reader* r_; std::optional<std::size_t> remaining_;
    };

    class map_cursor {
    public:
        map_cursor(reader* r, std::optional<std::size_t> n) noexcept : r_(r), remaining_(n) {}
        result<bool> next() {
            if (remaining_) {
                if (*remaining_ == 0) { --r_->depth_; return false; }
                --*remaining_;
                if (r_->pos_ >= r_->in_.size()) return std::unexpected(r_->error(errc::truncated, r_->pos_).with_expected("map key"));
                return key_ahead();
            }
            if (r_->pos_ >= r_->in_.size()) return std::unexpected(r_->error(errc::truncated, r_->pos_).with_expected("map key or break"));
            if (r_->in_[r_->pos_] == std::byte(break_byte)) { ++r_->pos_; --r_->depth_; return false; }
            return key_ahead();
        }
        core::kind key_kind() const noexcept {
            auto k = r_->peek();
            return k;
        }
        result<core::text_ref> key_text() { return r_->expect_text(); }
        result<std::uint64_t> key_uint() { return r_->expect_uint(); }
        result<std::int64_t>  key_sint() { return r_->expect_sint(); }
        std::optional<std::size_t> remaining() const noexcept { return remaining_; }
    private:
        reader* r_; std::optional<std::size_t> remaining_;
        std::size_t prev_start_ = 0, prev_end_ = 0; bool have_prev_ = false;
        // Determinism: keys must be strictly increasing in bytewise order of their encoding,
        // whatever their kind and however the caller goes on to decode them. The key's extent
        // comes from a validating skip over a saved position; a malformed key is left for the
        // caller's decode to report.
        result<bool> key_ahead() {
            if constexpr (Opts.require_deterministic) {
                std::size_t start = r_->pos_;
                auto saved = r_->save();
                auto skipped = r_->skip_value();
                std::size_t end = r_->pos_;
                r_->restore(saved);
                if (!skipped) return true;
                if (auto e = check_key_order(start, end)) return std::unexpected(std::move(*e));
            }
            return true;
        }
        std::optional<vcodec::error> check_key_order(std::size_t start, std::size_t end) {
            auto cur = r_->in_.subspan(start, end - start);
            std::optional<vcodec::error> out;
            if (have_prev_) {
                auto prev = r_->in_.subspan(prev_start_, prev_end_ - prev_start_);
                out = reader::key_order_error(r_, cur, prev, start);
            }
            prev_start_ = start; prev_end_ = end; have_prev_ = true;
            return out;
        }
    };

    static std::optional<vcodec::error> key_order_error(const reader* r, std::span<const std::byte> cur, std::span<const std::byte> prev, std::size_t start) {
        auto lt = [](std::byte x, std::byte y) { return std::to_integer<unsigned>(x) < std::to_integer<unsigned>(y); };
        bool less = std::lexicographical_compare(cur.begin(), cur.end(), prev.begin(), prev.end(), lt);
        bool equal = std::ranges::equal(cur, prev);
        if (!less && !equal) return std::nullopt;
        return r->error(errc::non_deterministic, start)
            .with_detail(equal ? "duplicate map key" : "map keys out of canonical order")
            .with_suggestion("key " + r->diag(cur) + (equal ? " repeats" : " precedes key " + r->diag(prev)))
            .with_length(cur.size());
    }

    result<seq_cursor> expect_array() {
        auto h = item_head("array");
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt != major::array) return std::unexpected(type_error("array", *h));
        if (auto e = enter(*h)) return std::unexpected(std::move(*e));
        pos_ += h->length;
        return seq_cursor{ this, h->indefinite ? std::nullopt : std::optional<std::size_t>(h->value) };
    }
    result<map_cursor> expect_map() {
        auto h = item_head("map");
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt != major::map) return std::unexpected(type_error("map", *h));
        if (auto e = enter(*h)) return std::unexpected(std::move(*e));
        pos_ += h->length;
        return map_cursor{ this, h->indefinite ? std::nullopt : std::optional<std::size_t>(h->value) };
    }

    // Determinism violations found inside a cursor surface at the next reader call.
    status take_pending() {
        if (pending_) { auto e = std::move(*pending_); pending_.reset(); return std::unexpected(std::move(e)); }
        return {};
    }

    // ---- RFC 8746 typed arrays ----
    template<core::field_meta F, class R>
    result<bool> read_range(R& out) {
        if constexpr (!Opts.accept_typed_arrays) return false;
        auto h = raw_head();
        if (!h) return std::unexpected(std::move(h.error()));
        if (h->mt != major::tag || h->value < 64 || h->value > 87 || h->value == 76) return false;
        using E = std::remove_cvref_t<std::ranges::range_value_t<R>>;
        std::size_t start = pos_;
        pos_ += h->length;
        auto b = expect_bytes();
        if (!b) { pos_ = start; return std::unexpected(std::move(b.error())); }
        auto st = typed_array_into<E>(h->value, b->bytes, out, start);
        if (!st) return std::unexpected(std::move(st.error()));
        return true;
    }

    // ---- generic validator ----
    status skip_value() {
        struct level {
            bool is_map; std::optional<std::uint64_t> remaining; bool key_next;
            // deterministic mode: extent of the key being parsed and of the previous key
            std::size_t key_start = 0, prev_start = 0, prev_end = 0; bool in_key = false, have_prev = false;
        };
        std::vector<level> stack;
        for (;;) {
            if (pos_ >= in_.size()) return std::unexpected(error(errc::truncated, pos_));
            if constexpr (Opts.require_deterministic) {
                if (!stack.empty() && stack.back().is_map && stack.back().key_next && !stack.back().in_key) { stack.back().key_start = pos_; stack.back().in_key = true; }
            }
            auto h = parse_head(in_, pos_);
            if (!h.length) return std::unexpected(error(errc::truncated, pos_));
            if (h.is_break) {
                if (stack.empty() || stack.back().remaining) return std::unexpected(error(errc::malformed_item, pos_).with_detail("break outside an indefinite-length item").with_length(1));
                if (stack.back().is_map && !stack.back().key_next) return std::unexpected(error(errc::malformed_item, pos_).with_detail("break after a map key").with_length(1));
                ++pos_; --depth_; stack.pop_back();
                goto item_done;
            }
            if (h.reserved) return std::unexpected(error(errc::malformed_item, pos_).with_detail("reserved additional information").with_length(1));
            if constexpr (Opts.require_deterministic) { if (auto e = check_head_deterministic(h)) return std::unexpected(std::move(*e)); }
            switch (h.mt) {
            case major::uint: case major::nint: pos_ += h.length; break;
            case major::simple:
                if (h.ai == 24 && h.value < 32) return std::unexpected(error(errc::malformed_item, pos_).with_detail("two-byte simple value below 32").with_length(2));
                if constexpr (Opts.require_deterministic) {
                    if (h.ai >= 25) {
                        double d = h.ai == 25 ? core::from_half(static_cast<std::uint16_t>(h.value))
                                 : h.ai == 26 ? double(std::bit_cast<float>(static_cast<std::uint32_t>(h.value))) : std::bit_cast<double>(h.value);
                        if (auto e = check_preferred_float(d, h.ai, h.value)) return std::unexpected(std::move(*e));
                    }
                }
                pos_ += h.length; break;
            case major::bytes: case major::text: {
                std::size_t at = pos_;
                auto r = read_string(h, h.mt, skip_scratch_);
                if (!r) return std::unexpected(std::move(r.error()));
                if constexpr (Opts.validate_utf8) {
                    if (h.mt == major::text) {
                        auto bytes = r->second ? std::span<const std::byte>(skip_scratch_) : r->first;
                        std::string_view t(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                        if (std::size_t bad = core::validate_utf8(t); bad != core::utf8_npos)
                            return std::unexpected(error(errc::invalid_utf8, r->second ? at : at + h.length + bad).with_length(1));
                    }
                }
                break;
            }
            case major::tag:
                pos_ += h.length;
                if (pos_ >= in_.size()) return std::unexpected(error(errc::truncated, pos_).with_expected("tagged item"));
                if (in_[pos_] == std::byte(break_byte)) return std::unexpected(error(errc::malformed_item, pos_).with_detail("tag with no content").with_length(1));
                continue;   // the tagged item follows and is the same "item"
            case major::array: case major::map: {
                if (depth_ >= Opts.max_depth) return std::unexpected(depth_error());
                pos_ += h.length; ++depth_;
                if (h.indefinite) stack.push_back({ h.mt == major::map, std::nullopt, true });
                else {
                    if (h.value == 0) { --depth_; break; }
                    stack.push_back({ h.mt == major::map, h.mt == major::map ? h.value * 2 : h.value, true });
                }
                if (!stack.empty()) continue;
                break;
            }
            }
        item_done:
            if (stack.empty()) return {};
            auto& top = stack.back();
            if constexpr (Opts.require_deterministic) {
                if (top.is_map && top.key_next) {
                    auto cur = in_.subspan(top.key_start, pos_ - top.key_start);
                    if (top.have_prev)
                        if (auto e = key_order_error(this, cur, in_.subspan(top.prev_start, top.prev_end - top.prev_start), top.key_start)) return std::unexpected(std::move(*e));
                    top.prev_start = top.key_start; top.prev_end = pos_; top.have_prev = true; top.in_key = false;
                }
            }
            if (top.is_map) top.key_next = !top.key_next;
            if (top.remaining) {
                if (--*top.remaining == 0) { --depth_; stack.pop_back(); goto item_done; }
            }
        }
    }

    // After the top-level value: nothing may remain. A leading self-describe tag is skipped
    // by start().
    status finish() {
        if (pos_ < in_.size()) return std::unexpected(error(errc::trailing_content, pos_).with_length(1));
        return {};
    }
    void start() {
        auto h = parse_head(in_, pos_);
        if (h.length && h.mt == major::tag && h.value == self_describe_tag) pos_ += h.length;
    }

private:
    std::span<const std::byte> in_;
    std::size_t pos_ = 0;
    std::size_t depth_ = 0;
    std::size_t start_ = 0;      // offset of the current item's head (after skipped tags)
    std::vector<std::byte> text_scratch_;
    std::vector<std::byte> bytes_scratch_;
    std::vector<std::byte> skip_scratch_;
    std::vector<vcodec::error> collected_;
    std::optional<vcodec::error> pending_;

    static std::string negative_text(std::uint64_t n) {
        // -1 - n as decimal without overflowing
        if (n < std::uint64_t(INT64_MAX)) return std::to_string(-1 - std::int64_t(n));
        // n + 1 as an unsigned 65-bit-ish decimal: n+1 may overflow only when n == UINT64_MAX
        if (n == UINT64_MAX) return "-18446744073709551616";
        return "-" + std::to_string(n + 1);
    }
    std::string tag_text(std::uint64_t t) const {
        std::string s = "tag " + std::to_string(t);
        if (auto n = tag_name(t); !n.empty()) s += " (" + std::string(n) + ")";
        return s;
    }
    std::string diag(std::span<const std::byte> key) const {
        auto h = parse_head(key, 0);
        if (h.length && h.mt == major::uint) return std::to_string(h.value);
        if (h.length && h.mt == major::nint) return negative_text(h.value);
        if (h.length && h.mt == major::text) return "\"" + std::string(reinterpret_cast<const char*>(key.data()) + h.length, key.size() - h.length) + "\"";
        std::string hex = "h'";
        static constexpr char digits[] = "0123456789abcdef";
        for (auto b : key) { hex += digits[std::to_integer<unsigned>(b) >> 4]; hex += digits[std::to_integer<unsigned>(b) & 15]; }
        return hex + "'";
    }

    // Head of the next item, skipping ignorable tags, checking well-formedness and (when
    // required) determinism. Does not consume.
    result<parsed_head> item_head(std::string_view expected) {
        if (auto p = take_pending(); !p) return std::unexpected(std::move(p.error()));
        std::size_t p = pos_;
        for (;;) {
            if (p >= in_.size()) return std::unexpected(error(errc::truncated, p).with_expected(expected));
            auto h = parse_head(in_, p);
            if (!h.length) return std::unexpected(error(errc::truncated, p).with_expected(expected));
            if (h.is_break) return std::unexpected(error(errc::malformed_item, p).with_detail("break outside an indefinite-length item").with_length(1));
            if (h.reserved) return std::unexpected(error(errc::malformed_item, p).with_detail("reserved additional information").with_length(1));
            if (h.mt == major::tag) {
                if constexpr (Opts.ignore_unknown_tags) { p += h.length; continue; }
                return std::unexpected(error(errc::tag_mismatch, p).with_expected("no tag").with_found(tag_text(h.value)).with_length(h.length));
            }
            if (h.mt == major::simple && h.ai == 24 && h.value < 32)
                return std::unexpected(error(errc::malformed_item, p).with_detail("two-byte simple value below 32").with_length(2));
            if constexpr (Opts.require_deterministic) { if (auto e = check_head_deterministic(h, p)) return std::unexpected(std::move(*e)); }
            pos_ = p; start_ = p;
            return h;
        }
    }
    // Head without tag skipping, for expect_tags / read_range / bignum.
    result<parsed_head> raw_head() {
        if (auto p = take_pending(); !p) return std::unexpected(std::move(p.error()));
        if (pos_ >= in_.size()) return std::unexpected(error(errc::truncated, pos_));
        auto h = parse_head(in_, pos_);
        if (!h.length) return std::unexpected(error(errc::truncated, pos_));
        if (h.reserved) return std::unexpected(error(errc::malformed_item, pos_).with_detail("reserved additional information").with_length(1));
        if (h.is_break) return std::unexpected(error(errc::malformed_item, pos_).with_detail("break outside an indefinite-length item").with_length(1));
        return h;
    }

    std::string found_name(parsed_head const& h) const {
        if (h.mt == major::simple) {
            if (h.ai == 20 || h.ai == 21) return "boolean";
            if (h.ai == 22) return "null";
            if (h.ai == 23) return "undefined";
            if (h.ai >= 25 && h.ai <= 27) return "float";
            return "simple value " + std::to_string(h.value);
        }
        return std::string(major_name(h.mt));
    }
    vcodec::error type_error(std::string_view expected, parsed_head const& h) const {
        return error(errc::type_mismatch, pos_).with_expected(expected).with_found(found_name(h)).with_length(item_length(h));
    }
    std::size_t item_length(parsed_head const& h) const noexcept {
        if (h.mt == major::bytes || h.mt == major::text) return h.indefinite ? h.length : std::min<std::size_t>(h.length + h.value, in_.size() - pos_);
        return h.length;
    }
    vcodec::error depth_error() const {
        return error(errc::depth_exceeded, pos_).with_detail(std::to_string(Opts.max_depth)).with_length(1);
    }
    std::optional<vcodec::error> enter(parsed_head const&) {
        if (depth_ >= Opts.max_depth) return depth_error();
        ++depth_;
        return std::nullopt;
    }

    std::optional<vcodec::error> check_head_deterministic(parsed_head const& h, std::size_t at) const {
        if (h.indefinite) return error(errc::non_deterministic, at).with_detail(std::string("indefinite-length ") + std::string(major_name(h.mt)))
            .with_suggestion("deterministic encoding requires definite lengths").with_length(1);
        if (!h.shortest() && h.mt != major::simple) {
            std::string v = h.mt == major::nint ? negative_text(h.value) : std::to_string(h.value);
            std::size_t need = h.value < 24 ? 1 : h.value <= 0xFF ? 2 : h.value <= 0xFFFF ? 3 : h.value <= 0xFFFFFFFFull ? 5 : 9;
            return error(errc::non_deterministic, at).with_detail("integer " + v + " encoded in " + std::to_string(h.length) + " bytes")
                .with_suggestion("shortest form is " + std::to_string(need) + (need == 1 ? " byte" : " bytes")).with_length(h.length);
        }
        return std::nullopt;
    }
    std::optional<vcodec::error> check_head_deterministic(parsed_head const& h) const { return check_head_deterministic(h, pos_); }
    std::optional<vcodec::error> check_preferred_float(double d, std::uint8_t ai, std::uint64_t raw) const {
        if (std::isnan(d)) {
            // RFC 8949 §4.2.2: a deterministic protocol picks one NaN; this library emits 0xf97e00.
            if (ai == 25 && raw == 0x7e00) return std::nullopt;
            return error(errc::non_deterministic, pos_).with_detail("NaN not encoded as f97e00")
                .with_suggestion("deterministic encoding uses the canonical half NaN").with_length(std::size_t(1) + (ai == 25 ? 2 : ai == 26 ? 4 : 8));
        }
        std::uint8_t need = core::to_half(d) ? 25 : core::fits_single(d) ? 26 : 27;
        if (ai == need) return std::nullopt;
        auto name = [](std::uint8_t a) { return a == 25 ? "half" : a == 26 ? "single" : "double"; };
        return error(errc::non_deterministic, pos_).with_detail("float encoded as " + std::string(name(ai)))
            .with_suggestion(std::string("preferred form is ") + name(need)).with_length(std::size_t(1) + (ai == 25 ? 2 : ai == 26 ? 4 : 8));
    }

    // Reads a definite or indefinite string. Definite: returns the borrowed span. Indefinite:
    // the chunks are concatenated into `scratch` and the second member is true.
    result<std::pair<std::span<const std::byte>, bool>> read_string(parsed_head const& h, major expected_major, std::vector<std::byte>& scratch) {
        if (!h.indefinite) {
            if (h.value > in_.size() - pos_ - h.length)
                return std::unexpected(error(errc::truncated, pos_).with_expected(std::to_string(h.value) + " string bytes").with_length(h.length));
            auto span = in_.subspan(pos_ + h.length, static_cast<std::size_t>(h.value));
            pos_ += h.length + static_cast<std::size_t>(h.value);
            return std::pair{ span, false };
        }
        if constexpr (Opts.require_deterministic) {
            return std::unexpected(error(errc::non_deterministic, pos_).with_detail(std::string("indefinite-length ") + std::string(major_name(h.mt)))
                .with_suggestion("deterministic encoding requires definite lengths").with_length(1));
        }
        pos_ += h.length;
        scratch.clear();
        for (;;) {
            if (pos_ >= in_.size()) return std::unexpected(error(errc::truncated, pos_).with_expected("string chunk or break"));
            if (in_[pos_] == std::byte(break_byte)) { ++pos_; break; }
            auto c = parse_head(in_, pos_);
            if (!c.length) return std::unexpected(error(errc::truncated, pos_));
            if (c.mt != expected_major || c.indefinite)
                return std::unexpected(error(errc::malformed_item, pos_).with_detail("indefinite-length string chunk must be a definite-length string of the same type").with_length(c.length));
            if (c.value > in_.size() - pos_ - c.length)
                return std::unexpected(error(errc::truncated, pos_).with_expected(std::to_string(c.value) + " string bytes").with_length(c.length));
            auto span = in_.subspan(pos_ + c.length, static_cast<std::size_t>(c.value));
            if constexpr (Opts.validate_utf8) {
                // RFC 8949 §3.2.3: every chunk of an indefinite-length text string must itself be
                // well-formed UTF-8; a character cannot be split across chunks.
                if (expected_major == major::text) {
                    std::string_view t(reinterpret_cast<const char*>(span.data()), span.size());
                    if (std::size_t bad = core::validate_utf8(t); bad != core::utf8_npos)
                        return std::unexpected(error(errc::invalid_utf8, pos_ + c.length + bad).with_length(1));
                }
            }
            scratch.insert(scratch.end(), span.begin(), span.end());
            pos_ += c.length + static_cast<std::size_t>(c.value);
        }
        return std::pair{ std::span<const std::byte>{}, true };
    }

    template<class E, class R>
    status typed_array_into(std::uint64_t tag, std::span<const std::byte> bytes, R& out, std::size_t at) {
        // Element type and endianness described by the tag.
        bool is_float = tag >= 80;
        bool little = is_float ? tag >= 84 : ((tag & 4) != 0);
        unsigned size_code = is_float ? (tag - (little ? 84 : 80)) : (tag & 3);
        std::size_t esize = is_float ? (std::size_t(2) << size_code) : (std::size_t(1) << size_code);
        bool is_signed = !is_float && tag >= 72;
        if (is_float && esize == 16) return std::unexpected(error(errc::type_mismatch, at).with_expected("typed array of a supported width").with_found("float128 typed array"));
        if (bytes.size() % esize) return std::unexpected(error(errc::malformed_item, at).with_detail("typed array length is not a multiple of the element size"));
        std::size_t n = bytes.size() / esize;
        if constexpr (requires { out.clear(); out.resize(n); }) { out.clear(); out.resize(n); }
        else if (std::ranges::size(out) != n)
            return std::unexpected(error(errc::type_mismatch, at).with_expected("array of " + std::to_string(std::ranges::size(out)) + " elements").with_found("array of " + std::to_string(n) + " elements"));
        bool native_little = std::endian::native == std::endian::little;
        auto read_word = [&](std::size_t i) -> std::uint64_t {
            std::uint64_t w = 0;
            for (std::size_t b = 0; b < esize; ++b) {
                auto byte = std::to_integer<std::uint64_t>(bytes[i * esize + b]);
                w |= little ? (byte << (8 * b)) : (byte << (8 * (esize - 1 - b)));
            }
            return w;
        };
        // Fast path: identical representation.
        constexpr bool e_float = std::same_as<E, float> || std::same_as<E, double>;
        if (is_float == e_float && esize == sizeof(E) && little == native_little && (is_float || is_signed == std::is_signed_v<E>)) {
            if (n) std::memcpy(std::ranges::data(out), bytes.data(), bytes.size());
            return {};
        }
        auto it = std::ranges::begin(out);
        for (std::size_t i = 0; i < n; ++i, ++it) {
            std::uint64_t w = read_word(i);
            double dv = 0; std::int64_t sv = 0; std::uint64_t uv = 0;
            if (is_float) {
                dv = esize == 2 ? core::from_half(static_cast<std::uint16_t>(w)) : esize == 4 ? double(std::bit_cast<float>(static_cast<std::uint32_t>(w))) : std::bit_cast<double>(w);
                if constexpr (e_float) *it = static_cast<E>(dv);
                else return std::unexpected(error(errc::type_mismatch, at).with_expected("integer typed array").with_found("float typed array"));
            } else if (is_signed) {
                std::uint64_t sign_bit = std::uint64_t(1) << (8 * esize - 1);
                sv = (w & sign_bit) ? static_cast<std::int64_t>(w | ~((sign_bit << 1) - 1)) : static_cast<std::int64_t>(w);
                if constexpr (e_float) *it = static_cast<E>(sv);
                else if (sv < static_cast<std::int64_t>(std::numeric_limits<E>::min()) || (sv > 0 && static_cast<std::uint64_t>(sv) > static_cast<std::uint64_t>(std::numeric_limits<E>::max())))
                    return std::unexpected(error(errc::out_of_range, at).with_detail(std::to_string(sv)).with_context(core::type_display<E>()));
                else *it = static_cast<E>(sv);
            } else {
                uv = w;
                if constexpr (e_float) *it = static_cast<E>(uv);
                else if (uv > static_cast<std::uint64_t>(std::numeric_limits<E>::max()))
                    return std::unexpected(error(errc::out_of_range, at).with_detail(std::to_string(uv)).with_context(core::type_display<E>()));
                else *it = static_cast<E>(uv);
            }
        }
        return {};
    }
};
static_assert(core::reader<reader<>>);

namespace detail {

consteval options with_type_determinism(options o, bool det) { if (det) o.require_deterministic = true; return o; }
consteval options collecting(options o) { o.errors = error_mode::collect; return o; }

template<class T>
void annotate_type_determinism(vcodec::error& e) {
    if constexpr (is_deterministic_type<T>) {
        if (e.code() == errc::non_deterministic) {
            std::string s(e.suggestion());
            s += "; "; s += core::type_schema_name<T>(); s += " is declared [[=cbor::deterministic]]";
            e.with_suggestion(s);
        }
    }
}

template<class T, options Opts = {}>
result<T> decode(std::span<const std::byte> input) {
    constexpr options O = with_type_determinism(Opts, is_deterministic_type<T>);
    reader<O> r(input);
    r.start();
    T out{};
    auto st = core::decode(r, out);
    if (st) st = r.take_pending();
    if (!st) { st.error().finalize(); annotate_type_determinism<T>(st.error()); return std::unexpected(std::move(st.error())); }
    if constexpr (O.errors == error_mode::collect) {
        if (!r.collected().empty()) return std::unexpected(std::move(r.collected().front()));
    }
    if (auto f = r.finish(); !f) return std::unexpected(std::move(f.error()));
    return out;
}

template<options Opts = {}, class T>
status decode_into(T& out, std::span<const std::byte> input) {
    constexpr options O = with_type_determinism(Opts, is_deterministic_type<T>);
    reader<O> r(input);
    r.start();
    auto st = core::decode(r, out);
    if (st) st = r.take_pending();
    if (!st) { st.error().finalize(); annotate_type_determinism<T>(st.error()); return st; }
    if constexpr (O.errors == error_mode::collect) {
        if (!r.collected().empty()) return std::unexpected(std::move(r.collected().front()));
    }
    return r.finish();
}

template<class T, options Opts = {}>
collected<T> decode_all(std::span<const std::byte> input) {
    constexpr options O = collecting(with_type_determinism(Opts, is_deterministic_type<T>));
    reader<O> r(input);
    r.start();
    collected<T> out;
    auto st = core::decode(r, out.value);
    if (st) st = r.take_pending();
    out.errors = std::move(r.collected());
    for (auto& e : out.errors) annotate_type_determinism<T>(e);
    if (!st) { st.error().finalize(); annotate_type_determinism<T>(st.error()); out.errors.push_back(std::move(st.error())); return out; }
    if (auto f = r.finish(); !f) out.errors.push_back(std::move(f.error()));
    return out;
}

} // namespace detail

} // namespace vcodec::cbor

#endif // VCODEC_CBOR_READ_HPP

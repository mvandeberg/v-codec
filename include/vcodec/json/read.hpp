// The type-directed JSON reader (spec §4.3, M6) and the decode entry points (§8).
//
// Every expect_* either consumes the item or fails without consuming, reporting what it
// found. skip_value is a complete generic validator (§13.3). Whole-buffer only.
#ifndef VCODEC_JSON_READ_HPP
#define VCODEC_JSON_READ_HPP

#include <vcodec/core/build.hpp>
#include <vcodec/core/lower.hpp>
#include <vcodec/core/model.hpp>
#include <vcodec/error.hpp>
#include <vcodec/json/annotations.hpp>
#include <vcodec/json/escape.hpp>
#include <vcodec/json/number.hpp>
#include <vcodec/json/options.hpp>
#include <vcodec/json/traits.hpp>

#include <bitset>
#include <string>
#include <string_view>
#include <vector>

namespace vcodec::json {

template<options Opts = {}>
class reader {
public:
    using format = json::format;
    static constexpr duplicate_key duplicates = Opts.duplicates;
    static constexpr error_mode errors = Opts.errors;

    explicit reader(std::string_view input) noexcept : in_(input) {}

    std::vector<vcodec::error>& collected() noexcept { return collected_; }
    std::string_view input() const noexcept { return in_; }

    // ---- error factory: offset plus line/column (text format) ----
    vcodec::error error(errc code, std::size_t offset) const {
        vcodec::error e(code, offset);
        line_col lc{1, 1};
        std::size_t lim = offset < in_.size() ? offset : in_.size();
        for (std::size_t i = 0; i < lim; ++i) {
            if (in_[i] == '\n') { ++lc.line; lc.column = 1; } else ++lc.column;
        }
        e.with_position(lc);
        return e;
    }

    // ---- position ----
    std::size_t offset() noexcept { skip_ws(); return pos_; }
    std::size_t save() const noexcept { return pos_ | (depth_ << 48); }
    void restore(std::size_t s) noexcept { pos_ = s & ((std::size_t(1) << 48) - 1); depth_ = s >> 48; }
    bool at_end() noexcept { skip_ws(); return pos_ >= in_.size(); }

    // ---- classification ----
    core::kind peek() noexcept {
        skip_ws();
        if (pos_ >= in_.size()) return core::kind::undefined;
        switch (in_[pos_]) {
        case 'n': return core::kind::null;
        case 't': case 'f': return core::kind::boolean;
        case '"': return core::kind::text;
        case '[': return core::kind::array;
        case '{': return core::kind::map;
        case '-': case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': {
            auto n = scan_number(in_.substr(pos_));
            return n.kind == core::kind::undefined ? core::kind::real : n.kind;
        }
        default: return core::kind::undefined;
        }
    }

    // ---- scalars ----
    status expect_null() {
        skip_ws();
        if (auto e = mismatch("null", 'n')) return std::unexpected(std::move(*e));
        return literal("null");
    }
    result<bool> expect_boolean() {
        skip_ws();
        if (pos_ < in_.size() && in_[pos_] == 't') { if (auto st = literal("true"); !st) return std::unexpected(std::move(st.error())); return true; }
        if (pos_ < in_.size() && in_[pos_] == 'f') { if (auto st = literal("false"); !st) return std::unexpected(std::move(st.error())); return false; }
        return std::unexpected(type_error("boolean"));
    }
    result<std::uint64_t> expect_uint() {
        skip_ws();
        auto n = number("integer");
        if (!n) return std::unexpected(std::move(n.error()));
        if (n->kind == core::kind::uint) { pos_ += n->length; return n->u; }
        if (n->kind == core::kind::sint || n->overflow)
            return std::unexpected(error(errc::out_of_range, pos_).with_detail(in_.substr(pos_, n->length)).with_length(n->length));
        return std::unexpected(error(errc::type_mismatch, pos_).with_expected("integer").with_found("number").with_length(n->length));
    }
    result<std::int64_t> expect_sint() {
        skip_ws();
        auto n = number("integer");
        if (!n) return std::unexpected(std::move(n.error()));
        if (n->kind == core::kind::sint) { pos_ += n->length; return n->i; }
        if (n->kind == core::kind::uint) {
            if (n->u > std::uint64_t(INT64_MAX))
                return std::unexpected(error(errc::out_of_range, pos_).with_detail(in_.substr(pos_, n->length)).with_length(n->length));
            pos_ += n->length; return std::int64_t(n->u);
        }
        if (n->overflow)
            return std::unexpected(error(errc::out_of_range, pos_).with_detail(in_.substr(pos_, n->length)).with_length(n->length));
        return std::unexpected(error(errc::type_mismatch, pos_).with_expected("integer").with_found("number").with_length(n->length));
    }
    result<double> expect_real() {
        skip_ws();
        auto n = number("number");
        if (!n) return std::unexpected(std::move(n.error()));
        bool negative_zero = n->kind == core::kind::sint && n->i == 0 && in_[pos_] == '-';   // "-0" is an integer literal; keep the sign for doubles
        pos_ += n->length;
        switch (n->kind) {
        case core::kind::uint: return double(n->u);
        case core::kind::sint: return negative_zero ? -0.0 : double(n->i);
        default:               return n->d;
        }
    }
    // json::as_string: accept a quoted integer as well as a bare one.
    template<core::field_meta F>
    result<std::uint64_t> expect_uint() {
        if constexpr (core::has_annotation<as_string_t>(F.anns())) {
            skip_ws();
            if (pos_ < in_.size() && in_[pos_] == '"') return quoted_integer<std::uint64_t>();
        }
        return expect_uint();
    }
    template<core::field_meta F>
    result<std::int64_t> expect_sint() {
        if constexpr (core::has_annotation<as_string_t>(F.anns())) {
            skip_ws();
            if (pos_ < in_.size() && in_[pos_] == '"') return quoted_integer<std::int64_t>();
        }
        return expect_sint();
    }

    // ---- strings and bytes ----
    result<core::text_ref> expect_text() {
        skip_ws();
        if (auto e = mismatch("string", '"')) return std::unexpected(std::move(*e));
        return parse_string();
    }
    result<core::bytes_ref> expect_bytes() { return decode_bytes(bytes_encoding::base64); }
    template<core::field_meta F>
    result<core::bytes_ref> expect_bytes() {
        constexpr auto enc = [] {
            if (auto a = core::find_annotation<bytes_t>(F.anns())) return a->encoding;
            return bytes_encoding::base64;
        }();
        return decode_bytes(enc);
    }

    // ---- containers ----
    class seq_cursor {
    public:
        explicit seq_cursor(reader* r) noexcept : r_(r) {}
        result<bool> next() {
            r_->skip_ws();
            if (r_->pos_ >= r_->in_.size()) return std::unexpected(r_->error(errc::truncated, r_->pos_).with_expected(first_ ? "value or ']'" : "',' or ']'"));
            char c = r_->in_[r_->pos_];
            if (c == ']') { ++r_->pos_; --r_->depth_; return false; }
            if (first_) { first_ = false; return true; }
            if (c != ',') return std::unexpected(r_->unexpected("',' or ']'"));
            ++r_->pos_;
            r_->skip_ws();
            if (r_->pos_ < r_->in_.size() && r_->in_[r_->pos_] == ']') return std::unexpected(r_->unexpected("value"));   // trailing comma
            return true;
        }
        std::optional<std::size_t> remaining() const noexcept { return std::nullopt; }
    private:
        reader* r_; bool first_ = true;
    };

    class map_cursor {
    public:
        explicit map_cursor(reader* r) noexcept : r_(r) {}
        result<bool> next() {
            r_->skip_ws();
            if (r_->pos_ >= r_->in_.size()) return std::unexpected(r_->error(errc::truncated, r_->pos_).with_expected(first_ ? "key or '}'" : "',' or '}'"));
            char c = r_->in_[r_->pos_];
            if (c == '}') { ++r_->pos_; --r_->depth_; return false; }
            if (first_) { first_ = false; return true; }
            if (c != ',') return std::unexpected(r_->unexpected("',' or '}'"));
            ++r_->pos_;
            r_->skip_ws();
            if (r_->pos_ < r_->in_.size() && r_->in_[r_->pos_] == '}') return std::unexpected(r_->unexpected("key"));   // trailing comma
            return true;
        }
        core::kind key_kind() const noexcept { return core::kind::text; }
        result<core::text_ref> key_text() {
            r_->skip_ws();
            if (r_->pos_ >= r_->in_.size()) return std::unexpected(r_->error(errc::truncated, r_->pos_).with_expected("key"));
            if (r_->in_[r_->pos_] != '"') return std::unexpected(r_->unexpected("key"));
            auto k = r_->parse_string();
            if (!k) return k;
            r_->skip_ws();
            if (r_->pos_ >= r_->in_.size()) return std::unexpected(r_->error(errc::truncated, r_->pos_).with_expected("':'"));
            if (r_->in_[r_->pos_] != ':') return std::unexpected(r_->unexpected("':'"));
            ++r_->pos_;
            // The key may live in scratch; the value parse may overwrite scratch. Keys that
            // needed unescaping are moved to a second buffer so both stay valid.
            if (!k->borrowed) { r_->key_scratch_ = r_->scratch_; k->text = r_->key_scratch_; }
            return k;
        }
        result<std::uint64_t> key_uint() { return std::unexpected(r_->error(errc::non_text_key, r_->pos_).with_found("string")); }
        result<std::int64_t>  key_sint() { return std::unexpected(r_->error(errc::non_text_key, r_->pos_).with_found("string")); }
        std::optional<std::size_t> remaining() const noexcept { return std::nullopt; }
    private:
        reader* r_; bool first_ = true;
    };

    result<seq_cursor> expect_array() {
        skip_ws();
        if (auto e = mismatch("array", '[')) return std::unexpected(std::move(*e));
        if (auto e = enter()) return std::unexpected(std::move(*e));
        ++pos_;
        return seq_cursor{this};
    }
    result<map_cursor> expect_map() {
        skip_ws();
        if (auto e = mismatch("object", '{')) return std::unexpected(std::move(*e));
        if (auto e = enter()) return std::unexpected(std::move(*e));
        ++pos_;
        return map_cursor{this};
    }

    // ---- generic validator ----
    status skip_value() {
        std::bitset<Opts.max_depth + 1> is_object;
        std::size_t local = 0;
        enum { want_value, want_key, after_value } state = want_value;
        for (;;) {
            if (state == after_value && local == 0) return {};   // the value is complete
            skip_ws();
            if (pos_ >= in_.size()) return std::unexpected(error(errc::truncated, pos_));
            char c = in_[pos_];
            switch (state) {
            case want_value:
                if (c == '[' || c == '{') {
                    if (depth_ + local >= Opts.max_depth) return std::unexpected(depth_error());
                    is_object[local++] = (c == '{');
                    ++pos_;
                    skip_ws();
                    if (pos_ >= in_.size()) return std::unexpected(error(errc::truncated, pos_));
                    if (in_[pos_] == (c == '{' ? '}' : ']')) { ++pos_; --local; state = after_value; break; }
                    state = (c == '{') ? want_key : want_value;
                    break;
                }
                if (c == '"') { auto s = parse_string(); if (!s) return std::unexpected(std::move(s.error())); }
                else if (c == 't') { if (auto st = literal("true"); !st) return st; }
                else if (c == 'f') { if (auto st = literal("false"); !st) return st; }
                else if (c == 'n') { if (auto st = literal("null"); !st) return st; }
                else if (c == '-' || (c >= '0' && c <= '9')) {
                    auto n = scan_number(in_.substr(pos_));
                    if (n.kind == core::kind::undefined) return std::unexpected(number_error(n));
                    pos_ += n.length;
                } else return std::unexpected(unexpected("value"));
                state = after_value;
                break;
            case want_key:
                if (c != '"') return std::unexpected(unexpected("key"));
                { auto s = parse_string(); if (!s) return std::unexpected(std::move(s.error())); }
                skip_ws();
                if (pos_ >= in_.size()) return std::unexpected(error(errc::truncated, pos_).with_expected("':'"));
                if (in_[pos_] != ':') return std::unexpected(unexpected("':'"));
                ++pos_;
                state = want_value;
                break;
            case after_value:
                if (c == ',') {
                    ++pos_;
                    state = is_object[local - 1] ? want_key : want_value;
                    skip_ws();
                    if (pos_ < in_.size() && (in_[pos_] == '}' || in_[pos_] == ']'))
                        return std::unexpected(unexpected(is_object[local - 1] ? "key" : "value"));
                    break;
                }
                if (c == (is_object[local - 1] ? '}' : ']')) { ++pos_; --local; break; }
                return std::unexpected(unexpected(is_object[local - 1] ? "',' or '}'" : "',' or ']'"));
            }
        }
    }

    // After the top-level value: only whitespace may remain.
    status finish() {
        skip_ws();
        if (pos_ < in_.size()) return std::unexpected(error(errc::trailing_content, pos_).with_length(1));
        return {};
    }

private:
    std::string_view in_;
    std::size_t pos_ = 0;
    std::size_t depth_ = 0;
    std::string scratch_;
    std::string key_scratch_;
    std::vector<vcodec::error> collected_;

    VCODEC_ALWAYS_INLINE void skip_ws() noexcept {
        while (pos_ < in_.size()) {
            char c = in_[pos_];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') ++pos_; else break;
        }
    }

    static std::string_view found_name(char c) noexcept {
        switch (c) {
        case 'n': return "null";
        case 't': case 'f': return "boolean";
        case '"': return "string";
        case '[': return "array";
        case '{': return "object";
        case '-': case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': return "number";
        default: return {};
        }
    }
    std::size_t token_length() const noexcept {
        std::size_t i = pos_;
        if (i < in_.size() && in_[i] == '"') return 1;
        while (i < in_.size()) {
            char c = in_[i];
            if (c == ',' || c == '}' || c == ']' || c == ':' || c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '{' || c == '[' || c == '"') break;
            ++i;
        }
        return i > pos_ ? i - pos_ : 1;
    }

    vcodec::error type_error(std::string_view expected) const {
        if (pos_ >= in_.size()) return error(errc::truncated, pos_).with_expected(expected);
        auto f = found_name(in_[pos_]);
        if (f.empty()) return unexpected(expected);
        // A literal that is not actually `true`, `false` or `null` is a syntax error, not a
        // type mismatch against whatever its first letter suggested.
        char c = in_[pos_];
        if (c == 't' || c == 'f' || c == 'n') {
            std::string_view word = c == 't' ? "true" : c == 'f' ? "false" : "null";
            if (in_.substr(pos_, word.size()) != word) return unexpected(expected);
        }
        return error(errc::type_mismatch, pos_).with_expected(expected).with_found(f).with_length(token_length());
    }
    std::optional<vcodec::error> mismatch(std::string_view expected, char want) const {
        if (pos_ < in_.size() && in_[pos_] == want) return std::nullopt;
        return type_error(expected);
    }
    vcodec::error unexpected(std::string_view expected) const {
        if (pos_ >= in_.size()) return error(errc::truncated, pos_).with_expected(expected);
        return error(errc::unexpected_token, pos_).with_detail(in_.substr(pos_, 1)).with_expected(expected).with_length(token_length());
    }
    vcodec::error depth_error() const {
        return error(errc::depth_exceeded, pos_).with_detail(std::to_string(Opts.max_depth)).with_length(1);
    }
    vcodec::error number_error(number_scan const& n) const {
        if (n.error == errc::out_of_range)
            return error(errc::out_of_range, pos_).with_detail(in_.substr(pos_, n.length)).with_context("double").with_length(n.length);
        std::size_t len = token_length();
        return error(errc::invalid_number, pos_).with_detail(in_.substr(pos_, len)).with_length(len);
    }
    std::optional<vcodec::error> enter() {
        if (depth_ >= Opts.max_depth) return depth_error();
        ++depth_;
        return std::nullopt;
    }

    status literal(std::string_view word) {
        if (in_.substr(pos_, word.size()) == word) { pos_ += word.size(); return {}; }
        if (pos_ + word.size() > in_.size() && in_.substr(pos_) == word.substr(0, in_.size() - pos_))
            return std::unexpected(error(errc::truncated, in_.size()).with_expected(std::string("'") + std::string(word) + "'"));
        return std::unexpected(unexpected(std::string("'") + std::string(word) + "'"));
    }

    result<number_scan> number(std::string_view expected) {
        if (pos_ >= in_.size()) return std::unexpected(error(errc::truncated, pos_).with_expected(expected));
        char c = in_[pos_];
        if (!(c == '-' || (c >= '0' && c <= '9'))) return std::unexpected(type_error(expected));
        auto n = scan_number(in_.substr(pos_));
        if (n.kind == core::kind::undefined) return std::unexpected(number_error(n));
        return n;
    }

    template<class I>
    result<I> quoted_integer() {
        std::size_t start = pos_;
        auto s = parse_string();
        if (!s) return std::unexpected(std::move(s.error()));
        auto n = scan_number(s->text);
        if (n.kind == core::kind::undefined || n.length != s->text.size() || n.kind == core::kind::real) {
            pos_ = start;
            return std::unexpected(error(errc::type_mismatch, start).with_expected("integer").with_found("string").with_length(s->text.size() + 2));
        }
        if constexpr (std::is_unsigned_v<I>) {
            if (n.kind == core::kind::sint) { pos_ = start; return std::unexpected(error(errc::out_of_range, start + 1).with_detail(s->text)); }
            return n.u;
        } else {
            if (n.kind == core::kind::uint) {
                if (n.u > std::uint64_t(INT64_MAX)) { pos_ = start; return std::unexpected(error(errc::out_of_range, start + 1).with_detail(s->text)); }
                return std::int64_t(n.u);
            }
            return n.i;
        }
    }

    // At '"'. Borrows when the string has no escapes; otherwise unescapes into scratch_.
    result<core::text_ref> parse_string() {
        std::size_t open = pos_;
        std::size_t i = pos_ + 1;
        const std::size_t n = in_.size();
        while (i < n) {
            unsigned char c = static_cast<unsigned char>(in_[i]);
            if (c == '"') {
                std::string_view raw = in_.substr(open + 1, i - open - 1);
                if constexpr (Opts.validate_utf8) {
                    if (std::size_t bad = validate_utf8(raw); bad != npos)
                        return std::unexpected(error(errc::invalid_utf8, open + 1 + bad).with_length(1));
                }
                pos_ = i + 1;
                return core::text_ref{ raw, true, i + 1 - open };
            }
            if (c == '\\') return parse_escaped_string(open, i);
            if (c < 0x20) return std::unexpected(error(errc::unexpected_token, i).with_detail("control character").with_expected("escaped control character").with_length(1));
            ++i;
        }
        return std::unexpected(error(errc::unterminated_string, open).with_length(n - open));
    }

    result<core::text_ref> parse_escaped_string(std::size_t open, std::size_t first_escape) {
        const std::size_t n = in_.size();
        scratch_.assign(in_.data() + open + 1, first_escape - open - 1);
        std::size_t i = first_escape;
        std::size_t run = first_escape;
        auto flush = [&](std::size_t upto) { if (upto > run) scratch_.append(in_.data() + run, upto - run); };
        while (i < n) {
            unsigned char c = static_cast<unsigned char>(in_[i]);
            if (c == '"') {
                flush(i);
                if constexpr (Opts.validate_utf8) {
                    if (std::size_t bad = validate_utf8(in_.substr(open + 1, i - open - 1)); bad != npos)
                        return std::unexpected(error(errc::invalid_utf8, open + 1 + bad).with_length(1));
                }
                pos_ = i + 1;
                return core::text_ref{ scratch_, false, i + 1 - open };
            }
            if (c < 0x20) return std::unexpected(error(errc::unexpected_token, i).with_detail("control character").with_expected("escaped control character").with_length(1));
            if (c != '\\') { ++i; continue; }
            flush(i);
            if (i + 1 >= n) return std::unexpected(error(errc::unterminated_string, open).with_length(n - open));
            char e = in_[i + 1];
            switch (e) {
            case '"':  scratch_.push_back('"');  i += 2; break;
            case '\\': scratch_.push_back('\\'); i += 2; break;
            case '/':  scratch_.push_back('/');  i += 2; break;
            case 'b':  scratch_.push_back('\b'); i += 2; break;
            case 'f':  scratch_.push_back('\f'); i += 2; break;
            case 'n':  scratch_.push_back('\n'); i += 2; break;
            case 'r':  scratch_.push_back('\r'); i += 2; break;
            case 't':  scratch_.push_back('\t'); i += 2; break;
            case 'u': {
                auto cp = hex4(i + 2);
                if (!cp) return std::unexpected(error(errc::invalid_escape, i).with_detail(in_.substr(i, std::min<std::size_t>(6, n - i))).with_length(std::min<std::size_t>(6, n - i)));
                std::uint32_t code = *cp;
                std::size_t consumed = 6;
                if (code >= 0xD800 && code <= 0xDBFF) {
                    if (i + 11 < n + 0 && i + 6 < n && in_[i + 6] == '\\' && i + 7 < n && in_[i + 7] == 'u') {
                        auto lo = hex4(i + 8);
                        if (lo && *lo >= 0xDC00 && *lo <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (*lo - 0xDC00);
                            consumed = 12;
                        } else return std::unexpected(error(errc::invalid_escape, i).with_detail("lone high surrogate").with_length(6));
                    } else return std::unexpected(error(errc::invalid_escape, i).with_detail("lone high surrogate").with_length(6));
                } else if (code >= 0xDC00 && code <= 0xDFFF) {
                    return std::unexpected(error(errc::invalid_escape, i).with_detail("lone low surrogate").with_length(6));
                }
                append_utf8(code);
                i += consumed;
                break;
            }
            default:
                return std::unexpected(error(errc::invalid_escape, i).with_detail(in_.substr(i, 2)).with_length(2));
            }
            run = i;
        }
        return std::unexpected(error(errc::unterminated_string, open).with_length(n - open));
    }

    std::optional<std::uint32_t> hex4(std::size_t at) const noexcept {
        if (at + 4 > in_.size()) return std::nullopt;
        std::uint32_t v = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            char c = in_[at + k];
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) return std::nullopt;
            v = (v << 4) | std::uint32_t(d);
        }
        return v;
    }
    void append_utf8(std::uint32_t cp) {
        if (cp < 0x80) scratch_.push_back(char(cp));
        else if (cp < 0x800) { scratch_.push_back(char(0xC0 | (cp >> 6))); scratch_.push_back(char(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { scratch_.push_back(char(0xE0 | (cp >> 12))); scratch_.push_back(char(0x80 | ((cp >> 6) & 0x3F))); scratch_.push_back(char(0x80 | (cp & 0x3F))); }
        else { scratch_.push_back(char(0xF0 | (cp >> 18))); scratch_.push_back(char(0x80 | ((cp >> 12) & 0x3F))); scratch_.push_back(char(0x80 | ((cp >> 6) & 0x3F))); scratch_.push_back(char(0x80 | (cp & 0x3F))); }
    }

    result<core::bytes_ref> decode_bytes(bytes_encoding enc) {
        skip_ws();
        std::size_t start = pos_;
        if (auto e = mismatch("string", '"')) return std::unexpected(std::move(*e));
        auto s = parse_string();
        if (!s) return std::unexpected(std::move(s.error()));
        bytes_scratch_.clear();
        bool ok = enc == bytes_encoding::hex ? core::hex_decode(s->text, bytes_scratch_)
                                             : core::base64_decode(s->text, bytes_scratch_, enc == bytes_encoding::base64url);
        if (!ok) {
            pos_ = start;
            return std::unexpected(error(errc::type_mismatch, start)
                .with_expected(enc == bytes_encoding::hex ? "hex string" : enc == bytes_encoding::base64url ? "base64url string" : "base64 string")
                .with_found("string").with_length(s->text.size() + 2));
        }
        return core::bytes_ref{ bytes_scratch_, false };
    }
    std::vector<std::byte> bytes_scratch_;
};
static_assert(core::reader<reader<>>);
static_assert(core::collecting_reader<reader<options{ .errors = error_mode::collect }>>);

// ---- ungated entry points; the public, audited versions live in json/api.hpp ----
namespace detail {

// With errors = collect, decode still returns a single error: the first one collected.
// decode_all is the entry point that returns them all.
template<class T, options Opts = {}>
result<T> decode(std::string_view input) {
    reader<Opts> r(input);
    T out{};
    auto st = core::decode(r, out);
    if (!st) return std::unexpected(std::move(st.error()));
    if constexpr (Opts.errors == error_mode::collect) {
        if (!r.collected().empty()) return std::unexpected(std::move(r.collected().front()));
    }
    if (auto f = r.finish(); !f) return std::unexpected(std::move(f.error()));
    return out;
}

template<options Opts = {}, class T>
status decode_into(T& out, std::string_view input) {
    reader<Opts> r(input);
    auto st = core::decode(r, out);
    if (!st) return st;
    if constexpr (Opts.errors == error_mode::collect) {
        if (!r.collected().empty()) return std::unexpected(std::move(r.collected().front()));
    }
    return r.finish();
}

consteval options collecting(options o) { o.errors = error_mode::collect; return o; }

// Collect-all (§9.5): recovers at object-member granularity; syntax errors end the run.
template<class T, options Opts = {}>
collected<T> decode_all(std::string_view input) {
    constexpr options C = collecting(Opts);
    reader<C> r(input);
    collected<T> out;
    auto st = core::decode(r, out.value);
    out.errors = std::move(r.collected());
    if (!st) { st.error().finalize(); out.errors.push_back(std::move(st.error())); return out; }
    if (auto f = r.finish(); !f) out.errors.push_back(std::move(f.error()));
    return out;
}

} // namespace detail

} // namespace vcodec::json

#endif // VCODEC_JSON_READ_HPP

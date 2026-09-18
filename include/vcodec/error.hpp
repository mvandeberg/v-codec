// Structured, path-precise runtime errors (spec §9.1–9.3).
//
// The path is built during error unwinding (§9.2): each frame that receives a failed
// result appends its own step. Steps therefore accumulate leaf-first and are reversed once
// by finalize(), which the public entry points call. The happy path pays nothing.
//
// `error` is a handle to a heap block so that result<T> stays small on the hot path; the
// block holds up to 16 steps plus a truncation flag, as the spec describes.
#ifndef VCODEC_ERROR_HPP
#define VCODEC_ERROR_HPP

#include <vcodec/core/compiler.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace vcodec {

enum class errc : std::uint16_t {
    // syntax
    unexpected_token, unterminated_string, invalid_escape, invalid_utf8,
    invalid_number, trailing_content, depth_exceeded, truncated,
    // schema
    type_mismatch, missing_field, unknown_field, duplicate_key,
    out_of_range, unknown_enumerator, no_variant_alternative, missing_tag,
    // policy
    escape_in_borrowed_string, non_text_key,
    // v0.2 (CBOR): syntax
    malformed_item, unsupported_simple_value,
    // v0.2: schema
    tag_mismatch,
    // v0.2: policy
    non_deterministic, indefinite_in_borrowed_string,
};

constexpr bool is_syntax_error(errc c) noexcept {
    return c <= errc::truncated || c == errc::malformed_item || c == errc::unsupported_simple_value;
}

constexpr std::string_view to_string(errc c) noexcept {
    switch (c) {
    case errc::unexpected_token:          return "unexpected_token";
    case errc::unterminated_string:       return "unterminated_string";
    case errc::invalid_escape:            return "invalid_escape";
    case errc::invalid_utf8:              return "invalid_utf8";
    case errc::invalid_number:            return "invalid_number";
    case errc::trailing_content:          return "trailing_content";
    case errc::depth_exceeded:            return "depth_exceeded";
    case errc::truncated:                 return "truncated";
    case errc::type_mismatch:             return "type_mismatch";
    case errc::missing_field:             return "missing_field";
    case errc::unknown_field:             return "unknown_field";
    case errc::duplicate_key:             return "duplicate_key";
    case errc::out_of_range:              return "out_of_range";
    case errc::unknown_enumerator:        return "unknown_enumerator";
    case errc::no_variant_alternative:    return "no_variant_alternative";
    case errc::missing_tag:               return "missing_tag";
    case errc::escape_in_borrowed_string: return "escape_in_borrowed_string";
    case errc::non_text_key:              return "non_text_key";
    case errc::malformed_item:            return "malformed_item";
    case errc::unsupported_simple_value:  return "unsupported_simple_value";
    case errc::tag_mismatch:              return "tag_mismatch";
    case errc::non_deterministic:         return "non_deterministic";
    case errc::indefinite_in_borrowed_string: return "indefinite_in_borrowed_string";
    }
    return "?";
}

struct line_col {
    std::size_t line   = 1;   // 1-based
    std::size_t column = 1;   // 1-based, in bytes
    friend constexpr bool operator==(line_col, line_col) = default;
};

struct path_step {
    enum class tag : std::uint8_t { member, index, key_text, key_int };
    tag              kind;
    std::string_view name;    // member: C++ identifier. key_text: the key
    std::uint64_t    index;   // index / key_int
};

inline constexpr std::size_t max_path_steps = 16;

class error {
public:
    explicit error(errc code, std::size_t offset = 0) : p_(std::make_unique<impl>()) {
        p_->code = code; p_->offset = offset;
    }
    error(error const& o) : p_(std::make_unique<impl>(*o.p_)) { rebind(); }
    error(error&&) noexcept = default;
    error& operator=(error const& o) { p_ = std::make_unique<impl>(*o.p_); rebind(); return *this; }
    error& operator=(error&&) noexcept = default;

    // ---- observers (§9.1) ----
    errc                       code()     const noexcept { return p_->code; }
    std::span<const path_step> path()     const noexcept { return p_->steps; }   // root-first once finalized
    std::size_t                offset()   const noexcept { return p_->offset; }
    std::optional<line_col>    position() const noexcept { return p_->position; }
    std::string_view           detail()   const noexcept { return p_->detail; }

    // ---- extended observers, used by the renderers ----
    std::size_t      length()           const noexcept { return p_->length; }       // extent of the offending token, 0 if unknown
    std::string_view expected()         const noexcept { return p_->expected; }
    std::string_view found()            const noexcept { return p_->found; }
    std::string_view context()          const noexcept { return p_->context; }      // a type or member name
    std::string_view suggestion()       const noexcept { return p_->suggestion; }
    std::span<const std::string> candidates() const noexcept { return p_->candidates; }
    std::optional<std::size_t> container_offset() const noexcept { return p_->container_offset; }
    bool             path_truncated()   const noexcept { return p_->truncated; }
    bool             deny_unknown()     const noexcept { return p_->deny_unknown; }

    // ---- builders ----
    error& at(std::size_t offset) noexcept { p_->offset = offset; return *this; }
    error& with_length(std::size_t n) noexcept { p_->length = n; return *this; }
    error& with_position(line_col lc) noexcept { p_->position = lc; return *this; }
    error& with_detail(std::string_view s) { p_->detail.assign(s); return *this; }
    error& with_expected(std::string_view s) { p_->expected.assign(s); return *this; }
    error& with_found(std::string_view s) { p_->found.assign(s); return *this; }
    error& with_context(std::string_view s) { p_->context.assign(s); return *this; }
    error& with_suggestion(std::string_view s) { p_->suggestion.assign(s); return *this; }
    error& with_candidate(std::string_view s) { p_->candidates.emplace_back(s); return *this; }
    error& with_container_offset(std::size_t o) noexcept { p_->container_offset = o; return *this; }
    error& with_deny_unknown(bool b = true) noexcept { p_->deny_unknown = b; return *this; }

    // ---- unwind-time path construction (§9.2): called leaf-first ----
    error& push_member(std::string_view identifier) {
        return push({ path_step::tag::member, identifier, 0 });
    }
    error& push_index(std::uint64_t i) { return push({ path_step::tag::index, {}, i }); }
    error& push_key(std::string_view key) {
        p_->owned.emplace_back(key);
        return push({ path_step::tag::key_text, p_->owned.back(), 0 });
    }
    error& push_key(std::uint64_t k) { return push({ path_step::tag::key_int, {}, k }); }

    // Reverse the accumulated steps into root-first order. Idempotent.
    void finalize() noexcept {
        if (p_->finalized) return;
        std::reverse(p_->steps.begin(), p_->steps.end());
        p_->finalized = true;
    }
    bool finalized() const noexcept { return p_->finalized; }

private:
    struct impl {
        errc                       code{};
        std::size_t                offset = 0;
        std::size_t                length = 0;
        std::optional<line_col>    position;
        std::optional<std::size_t> container_offset;
        std::vector<path_step>     steps;          // leaf-first until finalized
        std::deque<std::string>    owned;          // storage for key_text steps (stable references)
        bool                       truncated = false;
        bool                       finalized = false;
        bool                       deny_unknown = false;
        std::string detail, expected, found, context, suggestion;
        std::vector<std::string>   candidates;
    };
    std::unique_ptr<impl> p_;

    error& push(path_step s) {
        if (p_->finalized) { // late push: keep root-first order
            if (p_->steps.size() >= max_path_steps) { p_->truncated = true; p_->steps.erase(p_->steps.begin()); }
            p_->steps.insert(p_->steps.begin(), s);
            return *this;
        }
        if (p_->steps.size() >= max_path_steps) { p_->truncated = true; return *this; }
        p_->steps.push_back(s);
        return *this;
    }
    // After copying the impl, key_text steps must point at our own strings.
    void rebind() noexcept {
        auto it = p_->owned.begin();
        for (auto& s : p_->steps)
            if (s.kind == path_step::tag::key_text && it != p_->owned.end()) s.name = *it++;
    }
};

template<class T> using result = std::expected<T, error>;
using status = std::expected<void, error>;

// Result of decode_all (§9.5): a possibly-incomplete value plus every error found.
template<class T>
struct collected {
    T                  value{};
    std::vector<error> errors;
    bool complete() const noexcept { return errors.empty(); }
    explicit operator bool() const noexcept { return complete(); }
};

// Encode-side runtime failures (§4.2): the value exists but cannot be represented — a
// skipped enumerator, a cycle that exhausts the depth limit, invalid UTF-8 in a string.
// Thrown, not returned, because sinks do not return errors. Carries a member path built by
// the same unwind rule as `error`.
class encode_error : public std::runtime_error {
public:
    encode_error(errc code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    errc code() const noexcept { return code_; }
    std::span<const path_step> path() const noexcept { return steps_; }   // root-first after finalize()
    void push_member(std::string_view identifier) { push({ path_step::tag::member, identifier, 0 }); }
    void push_index(std::uint64_t i) { push({ path_step::tag::index, {}, i }); }
    void push_key(std::string_view k) { owned_.emplace_back(k); push({ path_step::tag::key_text, owned_.back(), 0 }); }
    void finalize() noexcept { if (!finalized_) { std::reverse(steps_.begin(), steps_.end()); finalized_ = true; } }
    bool path_truncated() const noexcept { return truncated_; }
private:
    errc code_;
    std::vector<path_step> steps_;
    std::deque<std::string> owned_;
    bool truncated_ = false, finalized_ = false;
    void push(path_step s) {
        if (steps_.size() >= max_path_steps) { truncated_ = true; return; }
        steps_.push_back(s);
    }
};

// ---- rendering (§9.3) --------------------------------------------------------------------

namespace detail {

inline void render_path(std::string& out, std::span<const path_step> steps, bool truncated) {
    out += '$';
    if (truncated) out += ".\xE2\x80\xA6[truncated]";
    auto plain = [](std::string_view k) {
        if (k.empty()) return false;
        for (unsigned char c : k)
            if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
        return true;
    };
    for (auto const& s : steps) {
        switch (s.kind) {
        case path_step::tag::member:   out += '.'; out += s.name; break;
        case path_step::tag::index:    out += '['; out += std::to_string(s.index); out += ']'; break;
        case path_step::tag::key_int:  out += '['; out += std::to_string(s.index); out += ']'; break;
        case path_step::tag::key_text:
            if (plain(s.name)) { out += '.'; out += s.name; }
            else { out += "[\""; out += s.name; out += "\"]"; }
            break;
        }
    }
}

inline std::string headline(error const& e) {
    std::string h;
    auto q = [](std::string_view s) { std::string r = "'"; r += s; r += '\''; return r; };
    switch (e.code()) {
    case errc::unexpected_token:
        h = e.detail().empty() ? "unexpected token" : "unexpected token " + q(e.detail());
        if (!e.expected().empty()) { h += ", expected "; h += e.expected(); }
        break;
    case errc::unterminated_string: h = "unterminated string"; break;
    case errc::invalid_escape:
        h = e.detail().empty() ? "invalid escape sequence" : "invalid escape sequence " + q(e.detail()); break;
    case errc::invalid_utf8:        h = "invalid UTF-8"; break;
    case errc::invalid_number:
        h = e.detail().empty() ? "invalid number" : "invalid number " + q(e.detail()); break;
    case errc::trailing_content:    h = "trailing content after value"; break;
    case errc::depth_exceeded:
        h = "nesting depth exceeds "; h += e.detail().empty() ? std::string_view("the limit") : e.detail(); break;
    case errc::truncated:           h = "unexpected end of input"; break;
    case errc::type_mismatch:
        h = "expected "; h += e.expected(); h += ", found "; h += e.found(); break;
    case errc::missing_field:
        h = "missing required field " + q(e.detail());
        if (!e.expected().empty()) { h += " (key "; h += e.expected(); h += ")"; }
        break;
    case errc::unknown_field:       h = "unknown field " + q(e.detail()); break;
    case errc::duplicate_key:       h = "duplicate key " + q(e.detail()); break;
    case errc::out_of_range:
        h = "value "; h += e.detail(); h += " out of range for "; h += e.context(); break;
    case errc::unknown_enumerator:
        h = q(e.detail()); h += " is not a valid "; h += e.context(); break;
    case errc::no_variant_alternative:
        h = q(e.detail()); h += " is not an alternative of "; h += e.context(); break;
    case errc::missing_tag:         h = "missing discriminator " + q(e.detail()); break;
    case errc::escape_in_borrowed_string:
        h = "string contains escapes but "; h += e.context(); h += " borrows from the input"; break;
    case errc::non_text_key:
        h = "expected text key, found "; h += e.found(); break;
    case errc::malformed_item:
        h = e.detail().empty() ? "malformed item" : "malformed item: " + std::string(e.detail()); break;
    case errc::unsupported_simple_value:
        h = "unsupported simple value"; if (!e.detail().empty()) { h += ' '; h += e.detail(); } break;
    case errc::tag_mismatch:
        h = "tag mismatch"; if (!e.context().empty()) { h += " on "; h += q(e.context()); } break;
    case errc::non_deterministic:
        h = "non-deterministic encoding"; if (!e.detail().empty()) { h += ": "; h += e.detail(); } break;
    case errc::indefinite_in_borrowed_string:
        h = "indefinite-length string but "; h += e.context(); h += " borrows from the input"; break;
    }
    return h;
}

} // namespace detail

// One line: "<headline> at <path> (byte offset N)".
inline std::string render_terse(error const& e) {
    std::string out = "error: ";
    out += detail::headline(e);
    out += " at ";
    detail::render_path(out, e.path(), e.path_truncated());
    out += " (byte offset ";
    out += std::to_string(e.offset());
    out += ')';
    return out;
}

} // namespace vcodec

#endif // VCODEC_ERROR_HPP

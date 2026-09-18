// The framed, source-excerpt renderer (spec §9.3, §9.4). The required outputs in §9.4 are
// specification; test/json/errors.cpp asserts them byte for byte.
#ifndef VCODEC_JSON_RENDER_ERROR_HPP
#define VCODEC_JSON_RENDER_ERROR_HPP

#include <vcodec/error.hpp>

#include <string>
#include <string_view>

namespace vcodec {

namespace detail {

inline constexpr std::string_view ellipsis = "\xE2\x80\xA6";   // U+2026

// The excerpt window is structural, not a fixed byte count: it runs from the start of the
// previous sibling value to the end of the next one, at the token's own nesting level, and
// never crosses a newline. That is what §9.4 shows — `"id": 7, "name": 42, "email": "a@b.c"`
// for an error on the 42 — and it reads the same for compact and pretty-printed input.
// Scans are bounded so pathological input cannot make rendering slow.
inline constexpr std::size_t excerpt_scan_limit = 96;

inline bool is_ws(char c) noexcept { return c == ' ' || c == '\t' || c == '\r'; }

// Skip backwards over a string literal ending at input[i-1] == '"'. Returns the index of the
// opening quote, or 0 if none is found within the limit.
inline std::size_t string_start_before(std::string_view input, std::size_t i, std::size_t floor) noexcept {
    // i points one past the closing quote
    std::size_t j = i - 1;
    while (j > floor) {
        --j;
        if (input[j] == '"') {
            std::size_t bs = 0;
            while (j > bs && input[j - 1 - bs] == '\\') ++bs;
            if (bs % 2 == 0) return j;
        }
    }
    return floor;
}

inline std::size_t excerpt_start(std::string_view input, std::size_t offset, std::size_t line_start) noexcept {
    std::size_t floor = offset > line_start + excerpt_scan_limit ? offset - excerpt_scan_limit : line_start;
    std::size_t i = offset;
    int depth = 0, seps = 0;
    while (i > floor) {
        char c = input[i - 1];
        if (c == '"') { i = string_start_before(input, i, floor); continue; }
        if (c == '}' || c == ']') { ++depth; --i; continue; }
        if (c == '{' || c == '[') { if (depth == 0) break; --depth; --i; continue; }
        if (c == ',' && depth == 0) { if (++seps == 2) break; }
        --i;
    }
    while (i < offset && is_ws(input[i])) ++i;
    return i;
}

inline std::size_t excerpt_end(std::string_view input, std::size_t from, std::size_t line_end) noexcept {
    std::size_t ceil = from + excerpt_scan_limit < line_end ? from + excerpt_scan_limit : line_end;
    std::size_t i = from;
    int depth = 0, seps = 0;
    while (i < ceil) {
        char c = input[i];
        if (c == '"') {
            ++i;
            while (i < ceil) {
                if (input[i] == '\\') { i += 2; continue; }
                if (input[i] == '"') { ++i; break; }
                ++i;
            }
            if (i > ceil) i = ceil;
            continue;
        }
        if (c == '{' || c == '[') { ++depth; ++i; continue; }
        if (c == '}' || c == ']') { if (depth == 0) break; --depth; ++i; continue; }
        if (c == ',' && depth == 0) { if (++seps == 2) break; }
        ++i;
    }
    while (i > from && is_ws(input[i - 1])) --i;
    return i;
}

inline void render_excerpt(std::string& out, error const& e, std::string_view input) {
    std::size_t offset = e.offset() > input.size() ? input.size() : e.offset();
    std::size_t length = e.length() ? e.length() : 1;
    if (offset + length > input.size()) length = input.size() > offset ? input.size() - offset : 0;

    std::size_t line_start = 0;
    if (offset > 0) {
        auto nl = input.rfind('\n', offset - 1);
        if (nl != std::string_view::npos) line_start = nl + 1;
    }
    std::size_t line_end = input.find('\n', offset);
    if (line_end == std::string_view::npos) line_end = input.size();

    std::size_t start = excerpt_start(input, offset, line_start);
    // An error on an opening bracket shows the whole bracketed value.
    bool opener = offset < input.size() && (input[offset] == '[' || input[offset] == '{');
    std::size_t end = excerpt_end(input, opener ? offset : offset + length, line_end);
    if (end < offset + length) end = offset + length;
    if (end > line_end) end = line_end;

    // Ellipses mark content on the same line that the window left out.
    std::size_t first_visible = line_start;
    while (first_visible < offset && is_ws(input[first_visible])) ++first_visible;
    bool cut_left = start > first_visible;
    std::size_t last_visible = line_end;
    while (last_visible > end && is_ws(input[last_visible - 1])) --last_visible;
    bool cut_right = end < last_visible;

    out += "   |\n   |  ";
    if (cut_left) out += ellipsis;
    for (std::size_t i = start; i < end; ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        out += (c == '\t') ? ' ' : (c < 0x20 ? '?' : char(c));
    }
    if (cut_right) out += ellipsis;
    out += "\n   |";
    std::size_t col = 2 + (cut_left ? 1 : 0) + (offset - start);
    out.append(col, ' ');
    std::size_t carets = length ? length : 1;
    if (offset + carets > end && end > offset) carets = end - offset;
    if (carets == 0) carets = 1;
    out.append(carets, '^');
    out += " at byte offset ";
    out += std::to_string(e.offset());
    out += '\n';
}

} // namespace detail

// §9.4 framed output. `input` is the buffer the error was produced from; pass an empty view
// when it is no longer available and the excerpt lines are omitted.
inline std::string render_framed(error const& e, std::string_view input) {
    std::string out = "error: ";
    out += detail::headline(e);
    out += "\n  --> ";
    detail::render_path(out, e.path(), e.path_truncated());
    out += '\n';

    switch (e.code()) {
    case errc::missing_field:
    case errc::missing_tag:
        out += "   |  object begins at byte offset ";
        out += std::to_string(e.container_offset().value_or(e.offset()));
        out += '\n';
        break;
    case errc::unknown_field:
        if (!e.suggestion().empty()) {
            out += "   |  did you mean '"; out += e.suggestion(); out += "'?\n";
        }
        if (e.deny_unknown() && !e.context().empty()) {
            out += "   |  ("; out += e.context(); out += " is declared [[=deny_unknown_fields]])\n";
        }
        break;
    case errc::unknown_enumerator:
    case errc::no_variant_alternative:
        if (!e.candidates().empty()) {
            out += "   |  expected one of: ";
            bool first = true;
            for (auto const& c : e.candidates()) { if (!first) out += ", "; first = false; out += c; }
            out += '\n';
        }
        break;
    case errc::out_of_range:
        break;
    default:
        if (!input.empty()) detail::render_excerpt(out, e, input);
        if (!e.suggestion().empty()) { out += "   |  "; out += e.suggestion(); out += '\n'; }
        break;
    }
    return out;
}

} // namespace vcodec

#endif // VCODEC_JSON_RENDER_ERROR_HPP

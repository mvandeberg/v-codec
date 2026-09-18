// The hex-window renderer for CBOR errors (spec v0.2 §9.2). A binary format has no line or
// column, so the frame is a 16-byte window with the offending item's head byte decoded.
#ifndef VCODEC_CBOR_RENDER_ERROR_HPP
#define VCODEC_CBOR_RENDER_ERROR_HPP

#include <vcodec/cbor/head.hpp>
#include <vcodec/error.hpp>

#include <span>
#include <string>

namespace vcodec {

namespace detail {

inline void hex2(std::string& out, unsigned v) {
    static constexpr char digits[] = "0123456789abcdef";
    out += digits[(v >> 4) & 15]; out += digits[v & 15];
}

inline std::string describe_head(cbor::parsed_head const& h, std::span<const std::byte> in, std::size_t pos) {
    std::string d = "major type " + std::to_string(static_cast<unsigned>(h.mt)) + " (" + std::string(cbor::major_name(h.mt)) + ")";
    if (h.is_break) return "break";
    if (h.reserved) return d + ", reserved additional information " + std::to_string(h.ai);
    if (h.indefinite) return d + ", indefinite length";
    switch (h.mt) {
    case cbor::major::uint:  return d + ", value " + std::to_string(h.value);
    case cbor::major::nint:  return d + ", value -" + std::to_string(h.value) + "-1";
    case cbor::major::bytes:
    case cbor::major::text:  return d + ", length " + std::to_string(h.value);
    case cbor::major::array: return d + ", " + std::to_string(h.value) + " elements";
    case cbor::major::map:   return d + ", " + std::to_string(h.value) + " entries";
    case cbor::major::tag: {
        auto n = cbor::tag_name(h.value);
        return d + ", tag " + std::to_string(h.value) + (n.empty() ? "" : " (" + std::string(n) + ")");
    }
    case cbor::major::simple:
        if (h.ai == 20) return d + ", false";
        if (h.ai == 21) return d + ", true";
        if (h.ai == 22) return d + ", null";
        if (h.ai == 23) return d + ", undefined";
        if (h.ai == 25) return d + ", half-precision float";
        if (h.ai == 26) return d + ", single-precision float";
        if (h.ai == 27) return d + ", double-precision float";
        return d + ", simple value " + std::to_string(h.value);
    }
    (void)in; (void)pos;
    return d;
}

inline void render_hex_window(std::string& out, error const& e, std::span<const std::byte> in) {
    std::size_t offset = e.offset() > in.size() ? in.size() : e.offset();
    std::size_t start = offset - offset % 16;
    std::size_t end = std::min(in.size(), start + 16);
    auto h = cbor::parse_head(in, offset);
    std::size_t item_len = e.length() ? e.length() : (h.length ? h.length : 1);

    out += "   |\n   |  ";
    static constexpr char digits[] = "0123456789abcdef";
    std::string addr;
    for (int s = 28; s >= 0; s -= 4) { addr += digits[(start >> s) & 15]; if (s == 16) addr += '_'; }
    out += addr; out += ":  ";
    if (start > 0) out += "\xE2\x80\xA6"; else out += ' ';
    for (std::size_t i = start; i < end; ++i) { hex2(out, std::to_integer<unsigned>(in[i])); out += ' '; }
    if (end < in.size()) out += "\xE2\x80\xA6";
    out += "\n   |  ";
    // caret line: column of byte i is 12 (addr) + 1 (ellipsis/space) + 3*(i-start)
    std::string carets(13 + 3 * (offset - start), ' ');
    for (std::size_t i = offset; i < end && i < offset + item_len; ++i) { carets += "^^"; if (i + 1 < end && i + 1 < offset + item_len) carets += ' '; }
    out += carets; out += '\n';
    out += "   |  "; out.append(13 + 3 * (offset - start), ' ');
    out += describe_head(h, in, offset);
    out += '\n';
}

} // namespace detail

// §9.2 hex-window output. `input` is the buffer the error was produced from.
inline std::string render_hex(error const& e, std::span<const std::byte> input) {
    std::string out = "error: ";
    out += detail::headline(e);
    out += "\n  --> ";
    detail::render_path(out, e.path(), e.path_truncated());
    out += '\n';
    switch (e.code()) {
    case errc::missing_field:
    case errc::missing_tag:
        out += "   |  map has "; out += e.found().empty() ? std::string("?") : std::string(e.found());
        out += " entries";
        if (!e.candidates().empty()) {
            out += ", keys present: ";
            bool first = true;
            for (auto const& c : e.candidates()) { if (!first) out += ", "; first = false; out += c; }
        }
        out += '\n';
        break;
    case errc::unknown_field:
        if (!e.suggestion().empty()) { out += "   |  did you mean '"; out += e.suggestion(); out += "'?\n"; }
        if (e.deny_unknown() && !e.context().empty()) { out += "   |  ("; out += e.context(); out += " is declared [[=deny_unknown_fields]])\n"; }
        break;
    case errc::tag_mismatch:
        out += "   |  expected "; out += e.expected(); out += ", found "; out += e.found(); out += '\n';
        break;
    case errc::non_deterministic:
        if (!e.suggestion().empty()) { out += "   |  "; out += e.suggestion(); out += '\n'; }
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
        if (!input.empty()) detail::render_hex_window(out, e, input);
        if (!e.suggestion().empty()) { out += "   |  "; out += e.suggestion(); out += '\n'; }
        break;
    }
    return out;
}

inline std::string render_hex(error const& e, std::span<const std::uint8_t> input) {
    return render_hex(e, std::as_bytes(input));
}

} // namespace vcodec

#endif // VCODEC_CBOR_RENDER_ERROR_HPP

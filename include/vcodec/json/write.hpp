// The JSON sink (spec §4.2, M4) and the encode entry points (§8).
#ifndef VCODEC_JSON_WRITE_HPP
#define VCODEC_JSON_WRITE_HPP

#include <vcodec/core/lower.hpp>
#include <vcodec/core/model.hpp>
#include <vcodec/core/traverse.hpp>
#include <vcodec/json/annotations.hpp>
#include <vcodec/json/escape.hpp>
#include <vcodec/json/number.hpp>
#include <vcodec/json/options.hpp>
#include <vcodec/json/traits.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <string>

namespace vcodec::json {

template<options Opts = {}>
class writer {
public:
    using format = json::format;
    static constexpr options opts = Opts;

    explicit writer(std::string& out) noexcept : out_(out) {}

    // ---- sink ----
    void null()                 { before_value(); out_.append("null"); }
    void boolean(bool b)        { before_value(); out_.append(b ? "true" : "false"); }
    void uint(std::uint64_t u)  { before_value(); write_uint(out_, u); }
    void sint(std::int64_t i)   { before_value(); write_sint(out_, i); }
    void real(double d)         { before_value(); write_real(out_, d); }
    void text(std::string_view t) { before_value(); write_string<Opts.validate_utf8>(out_, t); }
    void bytes(std::span<const std::byte> b) { write_bytes(b, bytes_encoding::base64); }
    void begin_array(std::optional<std::size_t>) { open('['); }
    void end_array()            { close(']'); }
    void begin_map(std::optional<std::size_t>)   { open('{'); }
    void end_map()              { close('}'); }
    void key(std::string_view k) {
        before_value();
        write_string<Opts.validate_utf8>(out_, k);
        out_.push_back(':');
        if constexpr (Opts.pretty) out_.push_back(' ');
        after_key_ = true;
    }
    // Non-text keys are rendered as decimal strings (§4.1 json::stringify_keys). Whether a
    // member is allowed to have them is decided by the audit, not here.
    void key(std::uint64_t k) { before_value(); out_.push_back('"'); write_uint(out_, k); out_.append("\":"); if constexpr (Opts.pretty) out_.push_back(' '); after_key_ = true; }
    void key(std::int64_t k)  { before_value(); out_.push_back('"'); write_sint(out_, k); out_.append("\":"); if constexpr (Opts.pretty) out_.push_back(' '); after_key_ = true; }
    void tag(std::uint64_t)   {}   // §4.1: dropped on write

    // ---- field-aware hooks (§4.2) ----
    template<core::field_meta F>
    void bytes(std::span<const std::byte> b) {
        constexpr auto enc = [] {
            if (auto a = core::find_annotation<bytes_t>(F.anns())) return a->encoding;
            return bytes_encoding::base64;
        }();
        write_bytes(b, enc);
    }
    template<core::field_meta F>
    void uint(std::uint64_t u) {
        if constexpr (core::has_annotation<as_string_t>(F.anns())) { before_value(); out_.push_back('"'); write_uint(out_, u); out_.push_back('"'); }
        else uint(u);
    }
    template<core::field_meta F>
    void sint(std::int64_t i) {
        if constexpr (core::has_annotation<as_string_t>(F.anns())) { before_value(); out_.push_back('"'); write_sint(out_, i); out_.push_back('"'); }
        else sint(i);
    }
    // Key emission is one memcpy: the quoted, escaped key plus ':' is built at compile time.
    template<core::static_string Name>
    void prepared_key() {
        static constexpr auto blob = prepared_key_blob<Name.size() * 6 + 8>(Name.view(), Opts.pretty);
        before_value();
        out_.append(blob.data(), blob.size());
        after_key_ = true;
    }

    std::size_t depth() const noexcept { return depth_; }

private:
    std::string& out_;
    std::size_t depth_ = 0;
    bool after_key_ = false;
    std::array<bool, Opts.max_depth + 2> first_{};

    VCODEC_ALWAYS_INLINE void before_value() {
        if (after_key_) { after_key_ = false; return; }
        if (depth_ == 0) return;
        if (first_[depth_]) first_[depth_] = false;
        else out_.push_back(',');
        if constexpr (Opts.pretty) newline();
    }
    void newline() {
        out_.push_back('\n');
        out_.append(depth_ * Opts.indent, ' ');
    }
    void open(char c) {
        before_value();
        if (VCODEC_UNLIKELY(depth_ >= Opts.max_depth)) {
            std::string msg = "nesting depth exceeds max_depth (" + std::to_string(Opts.max_depth) + "); a cycle through smart pointers is the usual cause";
            throw encode_error(errc::depth_exceeded, std::move(msg));
        }
        out_.push_back(c);
        ++depth_;
        first_[depth_] = true;
    }
    void close(char c) {
        bool empty = first_[depth_];
        --depth_;
        if constexpr (Opts.pretty) { if (!empty) newline(); }
        out_.push_back(c);
    }
    void write_bytes(std::span<const std::byte> b, bytes_encoding enc) {
        before_value();
        out_.push_back('"');
        switch (enc) {
        case bytes_encoding::base64:    core::base64_encode(b, out_, false); break;
        case bytes_encoding::base64url: core::base64_encode(b, out_, true); break;
        case bytes_encoding::hex:       core::hex_encode(b, out_); break;
        }
        out_.push_back('"');
    }
};
static_assert(core::sink<writer<>>);

// ---- ungated entry points; the public, audited versions live in json/api.hpp ----
namespace detail {

template<options Opts = {}, class T>
std::string encode(T const& v) {
    std::string out;
    writer<Opts> w(out);
    core::encode(w, v);
    return out;
}

template<options Opts = {}, class T>
void encode_append(T const& v, std::string& out) {
    writer<Opts> w(out);
    core::encode(w, v);
}

} // namespace detail

} // namespace vcodec::json

#endif // VCODEC_JSON_WRITE_HPP

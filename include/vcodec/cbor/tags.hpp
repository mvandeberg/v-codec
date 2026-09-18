// Standard tags with built-in codecs (spec v0.2 §5.4): std::chrono time points as tag 1
// (epoch, default) or tag 0 (RFC 3339 text, when the member carries [[=cbor::tag(0)]]). The
// tags themselves are emitted and verified by core from format_traits::expected_tags; these
// codecs write and read only the payload.
#ifndef VCODEC_CBOR_TAGS_HPP
#define VCODEC_CBOR_TAGS_HPP

#include <vcodec/cbor/annotations.hpp>
#include <vcodec/cbor/traits.hpp>
#include <vcodec/core/codec_for.hpp>
#include <vcodec/core/model.hpp>

#include <chrono>
#include <cmath>
#include <format>
#include <sstream>
#include <string>

namespace vcodec::cbor::detail {

template<class T> struct is_sys_time : std::false_type {};
template<class D> struct is_sys_time<std::chrono::sys_time<D>> : std::true_type {};
template<class T> concept sys_time_type = is_sys_time<std::remove_cvref_t<T>>::value;

template<core::field_meta F>
consteval bool wants_rfc3339() {
    for (auto a : F.anns())
        if (core::is_annotation_of_type(a, ^^tag_t) && std::meta::extract<tag_t>(a).tag == 0) return true;
    return false;
}

} // namespace vcodec::cbor::detail

template<class Duration>
struct vcodec::codec_for<std::chrono::sys_time<Duration>, vcodec::cbor::format> {
    using tp = std::chrono::sys_time<Duration>;
    using fsecs = std::chrono::duration<double>;

    // Field-aware: the member's tag(0) selects the text form.
    template<vcodec::core::field_meta F>
    static void encode(vcodec::core::sink auto& s, tp const& v) {
        if constexpr (cbor::detail::wants_rfc3339<F>()) {
            auto secs = std::chrono::floor<std::chrono::seconds>(v);
            s.text(std::format("{:%FT%TZ}", secs));
        } else {
            encode(s, v);
        }
    }
    static void encode(vcodec::core::sink auto& s, tp const& v) {
        if constexpr (std::ratio_less_equal_v<std::ratio<1>, typename Duration::period> && std::is_integral_v<typename Duration::rep>) {
            s.sint(static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(v.time_since_epoch()).count()));
        } else {
            double secs = std::chrono::duration_cast<fsecs>(v.time_since_epoch()).count();
            if (secs == std::floor(secs) && std::fabs(secs) < 9.0e15) s.sint(static_cast<std::int64_t>(secs));
            else s.real(secs);
        }
    }

    template<vcodec::core::field_meta F>
    static vcodec::status decode(vcodec::core::reader auto& r, tp& out) {
        if constexpr (cbor::detail::wants_rfc3339<F>()) {
            std::size_t off = r.offset();
            auto t = r.expect_text();
            if (!t) return std::unexpected(std::move(t.error()));
            std::istringstream in{ std::string(t->text) };
            std::chrono::sys_seconds parsed;
            in >> std::chrono::parse("%FT%TZ", parsed);
            if (in.fail()) {
                std::istringstream in2{ std::string(t->text) };
                in2 >> std::chrono::parse("%FT%T%Ez", parsed);
                if (in2.fail())
                    return std::unexpected(r.error(vcodec::errc::type_mismatch, off).with_expected("RFC 3339 date-time").with_found("text string")
                        .with_length(t->raw_length ? t->raw_length : t->text.size()));
            }
            out = std::chrono::time_point_cast<Duration>(parsed);
            return {};
        } else {
            return decode(r, out);
        }
    }
    static vcodec::status decode(vcodec::core::reader auto& r, tp& out) {
        using vcodec::core::kind;
        auto k = r.peek();
        if (k == kind::uint || k == kind::sint) {
            auto i = r.expect_sint();
            if (!i) return std::unexpected(std::move(i.error()));
            out = std::chrono::time_point_cast<Duration>(std::chrono::sys_seconds(std::chrono::seconds(*i)));
            return {};
        }
        auto d = r.expect_real();
        if (!d) return std::unexpected(std::move(d.error()));
        out = std::chrono::time_point_cast<Duration>(std::chrono::sys_time<fsecs>(fsecs(*d)));
        return {};
    }
};

#endif // VCODEC_CBOR_TAGS_HPP

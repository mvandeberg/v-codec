// The public CBOR API (spec v0.2 §7.6, §8) with its compile-time gate.
#ifndef VCODEC_CBOR_API_HPP
#define VCODEC_CBOR_API_HPP

#include <vcodec/cbor/read.hpp>
#include <vcodec/cbor/write.hpp>
#include <vcodec/core/diagnose.hpp>

#include <algorithm>
#include <iterator>

namespace vcodec::cbor {

template<class T> concept encodable = core::audit_of<T, format, core::direction::encode>.ok;
template<class T> concept decodable = core::audit_of<T, format, core::direction::decode>.ok;
template<class T> inline constexpr bool borrows = core::audit_of<T, format, core::direction::decode>.borrows;

template<class T> consteval core::message explain_encode() { return core::explain<T, format, core::direction::encode>(); }
template<class T> consteval core::message explain_decode() { return core::explain<T, format, core::direction::decode>(); }

// ---- encode ----

template<options Opts = {}, class T>
std::vector<std::byte> encode(T const& v) {
    static_assert(encodable<T>, explain_encode<T>());
    if constexpr (encodable<T>) return detail::encode<Opts>(v); else return {};
}

template<options Opts = {}, class T>
void encode_append(T const& v, std::vector<std::byte>& out) {
    static_assert(encodable<T>, explain_encode<T>());
    if constexpr (encodable<T>) detail::encode_append<Opts>(v, out);
}

template<options Opts = {}, class T, std::output_iterator<std::byte> It>
It encode_to(T const& v, It it) {
    static_assert(encodable<T>, explain_encode<T>());
    if constexpr (encodable<T>) { auto out = detail::encode<Opts>(v); return std::copy(out.begin(), out.end(), it); }
    else return it;
}

template<options Opts = {}, class T, core::sink S>
void encode_into(T const& v, S& s) {
    static_assert(encodable<T>, explain_encode<T>());
    if constexpr (encodable<T>) core::encode(s, v);
}

// ---- decode ----

// Lifetime: std::string_view and std::span<const std::byte> members of T alias `input`.
template<class T, options Opts = {}>
result<T> decode(std::span<const std::byte> input) {
    static_assert(decodable<T>, explain_decode<T>());
    if constexpr (decodable<T>) return detail::decode<T, Opts>(input);
    else return std::unexpected(error(errc::type_mismatch));
}
template<class T, options Opts = {}>
result<T> decode(std::string_view input) { return decode<T, Opts>(std::as_bytes(std::span(input))); }

template<class T, options Opts = {}>
    requires (borrows<T>)
result<T> decode(std::vector<std::byte>&&) = delete("this type borrows from the input (it has std::string_view or std::span members); pass a buffer that outlives the result");

template<options Opts = {}, class T>
status decode_into(T& out, std::span<const std::byte> input) {
    static_assert(decodable<T>, explain_decode<T>());
    if constexpr (decodable<T>) return detail::decode_into<Opts>(out, input);
    else return std::unexpected(error(errc::type_mismatch));
}

template<class T, options Opts = {}>
collected<T> decode_all(std::span<const std::byte> input) {
    static_assert(decodable<T>, explain_decode<T>());
    if constexpr (decodable<T>) return detail::decode_all<T, Opts>(input);
    else return {};
}

} // namespace vcodec::cbor

#endif // VCODEC_CBOR_API_HPP

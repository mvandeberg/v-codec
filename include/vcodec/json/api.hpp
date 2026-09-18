// The public JSON API (spec §8) with its compile-time gate (§10). Every entry point checks
// the audit first, so a rejected type produces one static_assert naming the member path.
#ifndef VCODEC_JSON_API_HPP
#define VCODEC_JSON_API_HPP

#include <vcodec/core/diagnose.hpp>
#include <vcodec/json/read.hpp>
#include <vcodec/json/write.hpp>

namespace vcodec::json {

template<class T> concept encodable = core::audit_of<T, format, core::direction::encode>.ok;
template<class T> concept decodable = core::audit_of<T, format, core::direction::decode>.ok;
// True when T holds std::string_view (or other borrowing) members: a decoded value then
// aliases the input buffer.
template<class T> inline constexpr bool borrows = core::audit_of<T, format, core::direction::decode>.borrows;

template<class T> consteval core::message explain_encode() { return core::explain<T, format, core::direction::encode>(); }
template<class T> consteval core::message explain_decode() { return core::explain<T, format, core::direction::decode>(); }

// ---- encode ----

template<options Opts = {}, class T>
std::string encode(T const& v) {
    static_assert(encodable<T>, explain_encode<T>());
    if constexpr (encodable<T>) return detail::encode<Opts>(v); else return {};
}

template<options Opts = {}, class T>
void encode_append(T const& v, std::string& out) {
    static_assert(encodable<T>, explain_encode<T>());
    if constexpr (encodable<T>) { writer<Opts> w(out); core::encode(w, v); }
}

template<options Opts = {}, class T, std::output_iterator<char> It>
It encode_to(T const& v, It it) {
    static_assert(encodable<T>, explain_encode<T>());
    if constexpr (encodable<T>) { std::string out = detail::encode<Opts>(v); return std::copy(out.begin(), out.end(), it); }
    else return it;
}

template<options Opts = {}, class T, core::sink S>
void encode_into(T const& v, S& s) {
    static_assert(encodable<T>, explain_encode<T>());
    if constexpr (encodable<T>) core::encode(s, v);
}

// ---- decode ----

// Lifetime: std::string_view members of T alias `input`; the caller keeps the buffer alive
// for as long as the result is used. Owning members copy.
template<class T, options Opts = {}>
result<T> decode(std::string_view input) {
    static_assert(decodable<T>, explain_decode<T>());
    if constexpr (decodable<T>) return detail::decode<T, Opts>(input);
    else return std::unexpected(error(errc::type_mismatch));
}

// A temporary std::string cannot be borrowed from: the buffer dies with the full expression.
template<class T, options Opts = {}>
    requires (borrows<T>)
result<T> decode(std::string&&) = delete("this type borrows from the input (it has std::string_view members); pass a buffer that outlives the result");

template<options Opts = {}, class T>
status decode_into(T& out, std::string_view input) {
    static_assert(decodable<T>, explain_decode<T>());
    if constexpr (decodable<T>) return detail::decode_into<Opts>(out, input);
    else return std::unexpected(error(errc::type_mismatch));
}

template<class T, options Opts = {}>
collected<T> decode_all(std::string_view input) {
    static_assert(decodable<T>, explain_decode<T>());
    if constexpr (decodable<T>) return detail::decode_all<T, Opts>(input);
    else return {};
}

} // namespace vcodec::json

#endif // VCODEC_JSON_API_HPP

// Per-type custom codecs (spec §6.3). Specialise for a type you do not own:
//
//   template<> struct vcodec::codec_for<my::Id, vcodec::json::format> {
//       static void encode(vcodec::core::sink auto& s, my::Id const& v) { s.text(v.str()); }
//       static auto decode(vcodec::core::reader auto& r) -> vcodec::result<my::Id> { ... }
//   };
//
// Lookup order: codec_for<T, Format>, codec_for<T, void>, describe<T>, reflection.
#ifndef VCODEC_CORE_CODEC_FOR_HPP
#define VCODEC_CORE_CODEC_FOR_HPP

#include <vcodec/core/compiler.hpp>

#include <type_traits>

namespace vcodec {

template<class T, class Format = void>
struct codec_for;   // primary deliberately left undefined

namespace core {

template<class T, class Format>
concept has_codec_for_exact = requires { sizeof(codec_for<std::remove_cvref_t<T>, Format>); };

template<class T, class Format>
concept has_codec_for = has_codec_for_exact<T, Format> || has_codec_for_exact<T, void>;

// The codec that applies to T under Format, honouring the lookup order.
template<class T, class Format>
struct codec_lookup;

template<class T, class Format>
    requires has_codec_for_exact<T, Format>
struct codec_lookup<T, Format> { using type = codec_for<std::remove_cvref_t<T>, Format>; };

template<class T, class Format>
    requires (!has_codec_for_exact<T, Format> && has_codec_for_exact<T, void>)
struct codec_lookup<T, Format> { using type = codec_for<std::remove_cvref_t<T>, void>; };

template<class T, class Format>
using codec_for_t = typename codec_lookup<T, Format>::type;

} // namespace core
} // namespace vcodec

#endif // VCODEC_CORE_CODEC_FOR_HPP

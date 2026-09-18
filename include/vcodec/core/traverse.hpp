// Type → model mapping, the encode side (spec §4.2, §7). Every value is classified by the
// concepts in concepts.hpp, first match wins, in the order below. The field context F
// carries the member's annotations to the sink hooks that need them (bytes encoding,
// integer-as-string) and propagates through optionals and container elements, but never
// into a nested struct's own members.
//
// Encoding cannot fail for well-typed input; the exceptions are runtime facts the type
// system cannot see (a skipped enumerator, a cycle) and are thrown as encode_error with a
// member path built by the same unwind rule as decode errors.
#ifndef VCODEC_CORE_TRAVERSE_HPP
#define VCODEC_CORE_TRAVERSE_HPP

#include <vcodec/core/codec_for.hpp>
#include <vcodec/core/concepts.hpp>
#include <vcodec/core/keys.hpp>
#include <vcodec/core/model.hpp>
#include <vcodec/core/schema.hpp>

#include <meta>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

namespace vcodec::core {

template<field_meta F, sink S, class T> void encode_value(S& s, T const& v);

// ---- names and strings ---------------------------------------------------------------------

template<string_like T>
std::string_view to_string_view(T const& v) noexcept {
    if constexpr (std::same_as<bare<T>, std::u8string> || std::same_as<bare<T>, std::u8string_view>)
        return { reinterpret_cast<const char*>(v.data()), v.size() };
    else if constexpr (c_string<T>) return v ? std::string_view(v) : std::string_view();
    else return std::string_view(v);
}

// ---- byte views ----------------------------------------------------------------------------

template<class T>
std::span<const std::byte> as_bytes(T const& v) noexcept {
    if constexpr (span_like<T>) return std::as_bytes(v);
    else return std::as_bytes(std::span(std::ranges::data(v), std::ranges::size(v)));
}

// ---- sink hooks: prefer the field-aware form when the sink offers it ------------------------

template<field_meta F, sink S>
void call_uint(S& s, std::uint64_t u) {
    if constexpr (requires { s.template uint<F>(u); }) s.template uint<F>(u); else s.uint(u);
}
template<field_meta F, sink S>
void call_sint(S& s, std::int64_t i) {
    if constexpr (requires { s.template sint<F>(i); }) s.template sint<F>(i); else s.sint(i);
}
template<field_meta F, sink S>
void call_bytes(S& s, std::span<const std::byte> b) {
    if constexpr (requires { s.template bytes<F>(b); }) s.template bytes<F>(b); else s.bytes(b);
}
template<field_meta F, sink S>
void call_real(S& s, double d) {
    if constexpr (requires { s.template real<F>(d); }) s.template real<F>(d); else s.real(d);
}
template<field_meta F, sink S>
void call_text(S& s, std::string_view t) {
    if constexpr (requires { s.template text<F>(t); }) s.template text<F>(t); else s.text(t);
}
// Key emission: a sink may precompute the key's wire form from the static name (one memcpy).
template<static_string Name, sink S>
void emit_key(S& s) {
    if constexpr (requires { s.template prepared_key<Name>(); }) s.template prepared_key<Name>();
    else s.key(Name.view());
}
template<std::int64_t Key, sink S>
void emit_int_key(S& s) {
    if constexpr (requires { s.template prepared_key<Key>(); }) s.template prepared_key<Key>();
    else s.key(Key);
}
// Tags a format expects before a member's value (from format_traits::expected_tags).
template<field_meta F, class T, sink S>
void emit_tags(S& s) {
    constexpr auto tags = expected_tags_of<F, T, format_of<S>>;
    if constexpr (!tags.empty()) for (auto t : tags) s.tag(t);
}

// ---- path-carrying rethrow -----------------------------------------------------------------

template<class Fn>
VCODEC_ALWAYS_INLINE void with_member_path(std::string_view identifier, Fn&& fn) {
    try { fn(); }
    catch (encode_error& e) { e.push_member(identifier); throw; }
}
template<class Fn>
VCODEC_ALWAYS_INLINE void with_index_path(std::uint64_t i, Fn&& fn) {
    try { fn(); }
    catch (encode_error& e) { e.push_index(i); throw; }
}
template<class Fn>
VCODEC_ALWAYS_INLINE void with_key_path(std::string_view k, Fn&& fn) {
    try { fn(); }
    catch (encode_error& e) { e.push_key(k); throw; }
}

// ---- enums (§5.5) ----------------------------------------------------------------------------

template<field_meta F, sink S, enum_type E>
void encode_enum(S& s, E v) {
    using U = std::underlying_type_t<E>;
    constexpr auto table = enum_schema_of<E>;
    if constexpr (enum_encodes_as_integer<E, F, format_traits<format_of<S>>::enum_default_integer>) {
        for (auto const& e : table) {
            if (e.value == v && e.skipped) {
                std::string msg = "enumerator '"; msg += e.identifier.view();
                msg += "' of "; msg += type_schema_name<E>(); msg += " is marked [[=vcodec::skip]] and cannot be encoded";
                throw encode_error(errc::unknown_enumerator, std::move(msg));
            }
        }
        if constexpr (std::is_signed_v<U>) call_sint<F>(s, static_cast<std::int64_t>(static_cast<U>(v)));
        else                               call_uint<F>(s, static_cast<std::uint64_t>(static_cast<U>(v)));
        return;
    }
    for (auto const& e : table) {
        if (e.value != v) continue;
        if (e.skipped) {
            std::string msg = "enumerator '"; msg += e.identifier.view();
            msg += "' of "; msg += type_schema_name<E>(); msg += " is marked [[=vcodec::skip]] and cannot be encoded";
            throw encode_error(errc::unknown_enumerator, std::move(msg));
        }
        s.text(e.name.view());
        return;
    }
    std::string msg = "value "; msg += std::to_string(static_cast<long long>(static_cast<U>(v)));
    msg += " is not an enumerator of "; msg += type_schema_name<E>();
    throw encode_error(errc::unknown_enumerator, std::move(msg));
}

// ---- structs -------------------------------------------------------------------------------

template<class T>
consteval std::size_t serialized_field_count() {
    std::size_t n = 0;
    for (auto const& f : schema_of<T>) if (!f.skip_serializing()) ++n;
    return n;
}

// The value of one member: tags apply to the value when present, never to a null from an
// empty optional, and never to a container's elements (F propagates into elements without
// re-emitting tags because emission happens here, once per member).
template<field_meta F, sink S, class M>
void encode_field_value(S& s, M const& m) {
    if constexpr (optional_like<M>) {
        if (m) encode_field_value<F>(s, *m); else s.null();
    } else {
        emit_tags<F, M>(s);
        if constexpr (format_traits<format_of<S>>::template bulk_range<F, M>()
                      && requires { s.template write_range<F>(m); }) {
            s.template write_range<F>(m);
        } else {
            encode_value<F>(s, m);
        }
    }
}

// Emits the members of a struct into an already-open map, in the format's member order,
// with the format's keys.
template<sink S, reflectable_class T>
void encode_members(S& s, T const& v) {
    using Fmt = format_of<S>;
    template for (constexpr auto idx : member_order_of<T, Fmt>) {
        constexpr auto f = fields_of<T>[idx];
        if constexpr (!f.info.skip_serializing()) {
            constexpr field_info fi = f.info;
            auto const& m = access<f>(v);
            bool emit = true;
            if constexpr (has(f.info.flags, field_flags::skip_if_null)) {
                if constexpr (optional_like<decltype(m)>) emit = static_cast<bool>(m);
            }
            if constexpr (has(f.info.flags, field_flags::skip_if_default)) {
                static const T defaults{};
                if (m == access<f>(defaults)) emit = false;
            }
            if (emit) {
                constexpr auto ik = format_traits<Fmt>::template integer_key<f>();
                if constexpr (ik.has_value()) emit_int_key<*ik>(s);
                else emit_key<fi.wire_name>(s);
                with_member_path(fi.identifier.view(), [&] { encode_field_value<f>(s, m); });
            }
        }
    }
}

// Number of members that will be emitted when no member is conditionally skipped.
template<class T>
consteval std::size_t serialized_field_count_of() { return serialized_field_count<T>(); }

template<field_meta F, sink S, reflectable_class T>
void encode_struct(S& s, T const& v, std::string_view tag_key = {}, std::string_view tag_value = {}) {
    constexpr auto& ti = type_schema_of<T>;
    if constexpr (ti.transparent) {
        template for (constexpr auto f : fields_of<T>) {
            constexpr field_info fi = f.info;
            with_member_path(fi.identifier.view(), [&] { encode_value<f>(s, access<f>(v)); });
        }
        return;
    }
    std::optional<std::size_t> n;
    if constexpr (!ti.conditional_fields) n = serialized_field_count<T>() + (tag_key.empty() ? 0 : 1);
    else {
        // Conditional members: count what will actually be emitted so formats that need a
        // definite length (deterministic CBOR) never have to buffer a struct.
        std::size_t k = tag_key.empty() ? 0 : 1;
        template for (constexpr auto f : fields_of<T>) {
            if constexpr (!f.info.skip_serializing()) {
                auto const& m = access<f>(v);
                bool emit = true;
                if constexpr (has(f.info.flags, field_flags::skip_if_null)) { if constexpr (optional_like<decltype(m)>) emit = static_cast<bool>(m); }
                if constexpr (has(f.info.flags, field_flags::skip_if_default)) { static const T defaults{}; if (m == access<f>(defaults)) emit = false; }
                if (emit) ++k;
            }
        }
        n = k;
    }
    // A struct's members arrive in the format's own order, so a sink that sorts maps may skip
    // it — except when a discriminator entry is spliced in, whose place in that order is only
    // known at runtime; then the sink sorts.
    if (!tag_key.empty()) s.begin_map(n);
    else if constexpr (requires { s.begin_map_ordered(n); }) s.begin_map_ordered(n);
    else s.begin_map(n);
    if (!tag_key.empty()) { s.key(tag_key); s.text(tag_value); }
    encode_members(s, v);
    s.end_map();
}

// ---- variants (§5.3 tag) ---------------------------------------------------------------------

template<field_meta F, sink S, variant_like V>
void encode_variant(S& s, V const& v) {
    static_assert(has(F.info.flags, field_flags::has_tag), "variant without a discriminator reached traversal");
    constexpr std::string_view tag_key = F.info.tag_key.view();
    constexpr auto& names = variant_names<V>;
    if (v.valueless_by_exception())
        throw encode_error(errc::no_variant_alternative, "variant is valueless_by_exception");
    std::visit([&]<class A>(A const& alt) {
        constexpr std::size_t I = [] {
            std::size_t i = 0, found = 0;
            [&]<class... Alts>(std::variant<Alts...>*) { ((std::same_as<Alts, A> ? found = i : 0, ++i), ...); }(static_cast<bare<V>*>(nullptr));
            return found;
        }();
        constexpr std::string_view name = names[I].view();
        if constexpr (internally_tagged<A>) {
            encode_struct<no_field>(s, alt, tag_key, name);
        } else {
            s.begin_map(2);
            s.key(tag_key); s.text(name);
            emit_key<variant_value_key>(s);
            with_member_path(variant_value_key.view(), [&] { encode_value<no_field>(s, alt); });
            s.end_map();
        }
    }, v);
}

// ---- containers ----------------------------------------------------------------------------

template<field_meta F, sink S, class R>
void encode_sequence(S& s, R const& r) {
    std::optional<std::size_t> n;
    if constexpr (std::ranges::sized_range<R>) n = std::ranges::size(r);
    s.begin_array(n);
    std::uint64_t i = 0;
    for (auto const& e : r) {
        with_index_path(i, [&] { encode_value<F>(s, e); });
        ++i;
    }
    s.end_array();
}

template<field_meta F, sink S, map_like M>
void encode_map(S& s, M const& m) {
    std::optional<std::size_t> n;
    if constexpr (std::ranges::sized_range<M>) n = std::ranges::size(m);
    s.begin_map(n);
    for (auto const& kv : m) {
        auto const& k = std::get<0>(kv);
        auto const& val = std::get<1>(kv);
        using K = bare<decltype(k)>;
        if constexpr (string_like<K>) {
            std::string_view key = to_string_view(k);
            s.key(key);
            with_key_path(key, [&] { encode_value<F>(s, val); });
        } else if constexpr (unsigned_integral_type<K>) {
            s.key(static_cast<std::uint64_t>(k));
            with_index_path(static_cast<std::uint64_t>(k), [&] { encode_value<F>(s, val); });
        } else {
            s.key(static_cast<std::int64_t>(k));
            with_index_path(static_cast<std::uint64_t>(k), [&] { encode_value<F>(s, val); });
        }
    }
    s.end_map();
}

template<field_meta F, sink S, tuple_like T>
void encode_tuple(S& s, T const& t) {
    constexpr std::size_t N = std::tuple_size_v<bare<T>>;
    s.begin_array(N);
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (with_index_path(I, [&] { encode_value<F>(s, std::get<I>(t)); }), ...);
    }(std::make_index_sequence<N>{});
    s.end_array();
}

// ---- dispatch ------------------------------------------------------------------------------

template<field_meta F, sink S, class T>
void encode_value(S& s, T const& v) {
    using Fmt = format_of<S>;
    if constexpr (F.codec != ^^void) {
        using Codec = typename [:F.codec:];
        if constexpr (requires { Codec::template encode<F>(s, v); }) Codec::template encode<F>(s, v);
        else Codec::encode(s, v);
    } else if constexpr (has_codec_for<T, Fmt>) {
        using Codec = codec_for_t<T, Fmt>;
        if constexpr (requires { Codec::template encode<F>(s, v); }) Codec::template encode<F>(s, v);
        else Codec::encode(s, v);
    } else if constexpr (requires { s.template write_prepared<T>(v); }) {
        s.template write_prepared<T>(v);
    } else if constexpr (boolean_type<T>) {
        s.boolean(v);
    } else if constexpr (char_type<T>) {
        s.text(std::string_view(&v, 1));
    } else if constexpr (unsigned_integral_type<T>) {
        call_uint<F>(s, static_cast<std::uint64_t>(v));
    } else if constexpr (signed_integral_type<T>) {
        call_sint<F>(s, static_cast<std::int64_t>(v));
    } else if constexpr (floating_type<T>) {
        call_real<F>(s, static_cast<double>(v));
    } else if constexpr (enum_type<T>) {
        encode_enum<F>(s, v);
    } else if constexpr (string_like<T>) {
        call_text<F>(s, to_string_view(v));
    } else if constexpr (optional_like<T>) {
        if (v) encode_value<F>(s, *v); else s.null();
    } else if constexpr (variant_like<T>) {
        encode_variant<F>(s, v);
    } else if constexpr (format_traits<Fmt>::template is_bytes<F, bare<T>>()) {
        call_bytes<F>(s, as_bytes(v));
    } else if constexpr (map_like<T>) {
        encode_map<F>(s, v);
    } else if constexpr (tuple_like<T>) {
        encode_tuple<F>(s, v);
    } else if constexpr (fixed_array<T> || span_like<T> || sequence<T>) {
        encode_sequence<F>(s, v);
    } else if constexpr (reflectable_class<T>) {
        encode_struct<F>(s, v);
    } else {
        static_assert(false, "vcodec: type reached traversal without a codec; the audit should have rejected it");
    }
}

// Top-level entry. Finalizes the path on any encode_error before rethrowing.
template<sink S, class T>
void encode(S& s, T const& v) {
    try { encode_field_value<no_field>(s, v); }
    catch (encode_error& e) { e.finalize(); throw; }
}

} // namespace vcodec::core

#endif // VCODEC_CORE_TRAVERSE_HPP

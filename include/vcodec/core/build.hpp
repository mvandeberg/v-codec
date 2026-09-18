// Model → type mapping, the decode side (spec §4.3, §7, §9). Type-directed: the expected
// type drives the reader. Every failure is a `result` carrying an `error`; frames append
// their path step as the failure passes through (§9.2). In collect mode (§9.5) a struct
// frame records a member's failure, rewinds, skips the value and continues.
#ifndef VCODEC_CORE_BUILD_HPP
#define VCODEC_CORE_BUILD_HPP

#include <vcodec/core/codec_for.hpp>
#include <vcodec/core/concepts.hpp>
#include <vcodec/core/lookup.hpp>
#include <vcodec/core/model.hpp>
#include <vcodec/core/schema.hpp>

#include <meta>
#include <bitset>
#include <charconv>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

namespace vcodec::core {

template<field_meta F, reader R, class T> status decode_value(R& r, T& out);

// ---- small helpers -------------------------------------------------------------------------

template<class T>
constexpr std::string_view type_display() {
    constexpr auto n = [] { return intern(qualified_type_name(^^T)); }();
    return n.view();
}

// Prefer the member's declared spelling (std::uint8_t) over the deduced type (unsigned char)
// when the field's type is exactly T.
template<field_meta F, class T>
constexpr std::string_view type_display_for() {
    constexpr auto n = [] {
        if constexpr (F.depth > 0) {
            if (std::meta::dealias(F.type) == std::meta::dealias(^^T)) return intern(qualified_type_name(F.type));
        }
        return intern(qualified_type_name(^^T));
    }();
    return n.view();
}

template<class T>
[[nodiscard]] status fail(result<T>&& r) { return std::unexpected(std::move(r.error())); }

template<reader R> constexpr bool collecting = R::errors == error_mode::collect;

template<reader R>
std::size_t error_mark(R& r) {
    if constexpr (collecting<R>) return r.collected().size(); else { (void)r; return 0; }
}
template<reader R, class Step>
void annotate_collected(R& r, std::size_t mark, Step&& step) {
    if constexpr (collecting<R>) {
        auto& v = r.collected();
        for (std::size_t i = mark; i < v.size(); ++i) step(v[i]);
    } else { (void)r; (void)mark; }
}

template<field_meta F, reader R>
result<std::uint64_t> call_expect_uint(R& r) {
    if constexpr (requires { r.template expect_uint<F>(); }) return r.template expect_uint<F>(); else return r.expect_uint();
}
template<field_meta F, reader R>
result<std::int64_t> call_expect_sint(R& r) {
    if constexpr (requires { r.template expect_sint<F>(); }) return r.template expect_sint<F>(); else return r.expect_sint();
}
template<field_meta F, reader R>
result<bytes_ref> call_expect_bytes(R& r) {
    if constexpr (requires { r.template expect_bytes<F>(); }) return r.template expect_bytes<F>(); else return r.expect_bytes();
}

inline std::string decimal(std::uint64_t u) { return std::to_string(u); }
inline std::string decimal(std::int64_t i) { return std::to_string(i); }

// ---- scalars -------------------------------------------------------------------------------

template<field_meta F, reader R, unsigned_integral_type T>
status decode_uint(R& r, T& out) {
    std::size_t off = r.offset();
    auto u = call_expect_uint<F>(r);
    if (!u) {
        if (u.error().code() == errc::out_of_range && u.error().context().empty())
            u.error().with_context(type_display_for<F, T>());
        return fail(std::move(u));
    }
    if (*u > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
        return std::unexpected(r.error(errc::out_of_range, off).with_detail(decimal(*u)).with_context(type_display_for<F, T>()));
    out = static_cast<T>(*u);
    return {};
}

template<field_meta F, reader R, signed_integral_type T>
status decode_sint(R& r, T& out) {
    std::size_t off = r.offset();
    auto i = call_expect_sint<F>(r);
    if (!i) {
        if (i.error().code() == errc::out_of_range && i.error().context().empty())
            i.error().with_context(type_display_for<F, T>());
        return fail(std::move(i));
    }
    if (*i > static_cast<std::int64_t>(std::numeric_limits<T>::max())
        || *i < static_cast<std::int64_t>(std::numeric_limits<T>::min()))
        return std::unexpected(r.error(errc::out_of_range, off).with_detail(decimal(*i)).with_context(type_display_for<F, T>()));
    out = static_cast<T>(*i);
    return {};
}

template<field_meta F, reader R, enum_type E>
status decode_enum(R& r, E& out) {
    using U = std::underlying_type_t<E>;
    constexpr auto table = enum_schema_of<E>;
    std::size_t off = r.offset();
    auto candidates = [&](error& e) {
        for (auto const& en : table) if (!en.skipped) e.with_candidate(en.name.view());
    };
    if constexpr (enum_as_integer<E>) {
        U raw{};
        status st;
        if constexpr (std::is_signed_v<U>) st = decode_sint<F>(r, raw); else st = decode_uint<F>(r, raw);
        if (!st) return st;
        for (auto const& en : table)
            if (static_cast<U>(en.value) == raw && !en.skipped) { out = en.value; return {}; }
        error e = r.error(errc::unknown_enumerator, off);
        if constexpr (std::is_signed_v<U>) e.with_detail(decimal(static_cast<std::int64_t>(raw)));
        else e.with_detail(decimal(static_cast<std::uint64_t>(raw)));
        e.with_context(type_schema_name<E>());
        for (auto const& en : table) {
            if (en.skipped) continue;
            if constexpr (std::is_signed_v<U>) e.with_candidate(decimal(static_cast<std::int64_t>(static_cast<U>(en.value))));
            else e.with_candidate(decimal(static_cast<std::uint64_t>(static_cast<U>(en.value))));
        }
        return std::unexpected(std::move(e));
    } else {
        auto t = r.expect_text();
        if (!t) return fail(std::move(t));
        std::size_t idx = enum_lookup_of<E>.find(t->text);
        if (idx == npos) {
            error e = r.error(errc::unknown_enumerator, off).with_detail(t->text).with_context(type_schema_name<E>())
                          .with_length(t->raw_length ? t->raw_length : t->text.size() + 2);
            candidates(e);
            return std::unexpected(std::move(e));
        }
        out = table[idx].value;
        return {};
    }
}

template<field_meta F, reader R, decodable_string T>
status decode_string(R& r, T& out) {
    std::size_t off = r.offset();
    auto t = r.expect_text();
    if (!t) return fail(std::move(t));
    if constexpr (std::same_as<bare<T>, std::string>) {
        out.assign(t->text);
    } else if constexpr (std::same_as<bare<T>, std::u8string>) {
        out.assign(reinterpret_cast<const char8_t*>(t->text.data()), t->text.size());
    } else {
        // Borrowing (§7.4): a view can only alias the input buffer.
        if (!t->borrowed) {
            constexpr field_info fi = F.info;
            std::string_view who = F.depth ? fi.qualified.view() : type_display<T>();
            return std::unexpected(r.error(errc::escape_in_borrowed_string, off).with_context(who)
                                       .with_length(t->raw_length ? t->raw_length : t->text.size() + 2)
                                       .with_suggestion("use std::string instead of std::string_view"));
        }
        if constexpr (std::same_as<bare<T>, std::u8string_view>)
            out = std::u8string_view(reinterpret_cast<const char8_t*>(t->text.data()), t->text.size());
        else
            out = t->text;
    }
    return {};
}

template<field_meta F, reader R>
status decode_char(R& r, char& out) {
    std::size_t off = r.offset();
    auto t = r.expect_text();
    if (!t) return fail(std::move(t));
    if (t->text.size() != 1)
        return std::unexpected(r.error(errc::type_mismatch, off).with_expected("single-character string")
                                   .with_found("string of length " + std::to_string(t->text.size()))
                                   .with_length(t->text.size() + 2));
    out = t->text[0];
    return {};
}

// ---- bytes ---------------------------------------------------------------------------------

template<field_meta F, reader R, class T>
status decode_bytes(R& r, T& out) {
    std::size_t off = r.offset();
    auto b = call_expect_bytes<F>(r);
    if (!b) return fail(std::move(b));
    auto const& bytes = b->bytes;
    if constexpr (std_array<T>) {
        if (bytes.size() != out.size())
            return std::unexpected(r.error(errc::type_mismatch, off)
                .with_expected("byte string of length " + std::to_string(out.size()))
                .with_found("byte string of length " + std::to_string(bytes.size())));
        std::copy(bytes.begin(), bytes.end(), out.begin());
    } else if constexpr (contiguous_of_bytes<T>) {
        out.assign(bytes.begin(), bytes.end());
    } else {
        // std::vector<std::uint8_t> and friends
        out.clear();
        out.reserve(bytes.size());
        for (std::byte x : bytes) out.push_back(static_cast<range_value<T>>(x));
    }
    return {};
}

// ---- optional-like -------------------------------------------------------------------------

template<class O>
auto& emplace_value(O& o) {
    using V = optional_value_t<O>;
    if constexpr (is_optional<bare<O>>::value) return o.emplace();
    else if constexpr (is_unique_ptr<bare<O>>::value) { o = std::make_unique<V>(); return *o; }
    else { o = std::make_shared<V>(); return *o; }
}

template<field_meta F, reader R, optional_like T>
status decode_optional(R& r, T& out) {
    if (r.peek() == kind::null) {
        auto st = r.expect_null();
        if (!st) return st;
        out = T{};
        return {};
    }
    auto& v = emplace_value(out);
    return decode_value<F>(r, v);
}

// ---- sequences -----------------------------------------------------------------------------

template<field_meta F, reader R, class Seq>
status decode_sequence(R& r, Seq& out) {
    auto c = r.expect_array();
    if (!c) return fail(std::move(c));
    if constexpr (requires { out.clear(); }) out.clear();
    using V = range_value<Seq>;
    std::uint64_t i = 0;
    for (;;) {
        auto more = c->next();
        if (!more) return fail(std::move(more));
        if (!*more) break;
        V tmp{};
        std::size_t mark = error_mark(r);
        auto st = decode_value<F>(r, tmp);
        annotate_collected(r, mark, [&](error& e) { e.push_index(i); });
        if (!st) { st.error().push_index(i); return st; }
        append_to(out, std::move(tmp));
        ++i;
    }
    return {};
}

template<class Arr>
consteval std::size_t fixed_size() {
    if constexpr (c_array<Arr>) return std::extent_v<bare<Arr>>;
    else return std::tuple_size<bare<Arr>>::value;
}

template<field_meta F, reader R, class Arr>
status decode_fixed_array(R& r, Arr& out) {
    constexpr std::size_t N = fixed_size<Arr>();
    std::size_t off = r.offset();
    auto c = r.expect_array();
    if (!c) return fail(std::move(c));
    for (std::size_t i = 0; i < N; ++i) {
        auto more = c->next();
        if (!more) return fail(std::move(more));
        if (!*more)
            return std::unexpected(r.error(errc::type_mismatch, off)
                .with_expected("array of " + std::to_string(N) + " elements")
                .with_found("array of " + std::to_string(i) + " elements"));
        std::size_t mark = error_mark(r);
        auto st = decode_value<F>(r, out[i]);
        annotate_collected(r, mark, [&](error& e) { e.push_index(i); });
        if (!st) { st.error().push_index(i); return st; }
    }
    auto more = c->next();
    if (!more) return fail(std::move(more));
    if (*more) {
        std::size_t extra = N;
        while (*more) {
            auto st = r.skip_value(); if (!st) return st;
            ++extra;
            more = c->next(); if (!more) return fail(std::move(more));
        }
        return std::unexpected(r.error(errc::type_mismatch, off)
            .with_expected("array of " + std::to_string(N) + " elements")
            .with_found("array of " + std::to_string(extra) + " elements"));
    }
    return {};
}

template<field_meta F, reader R, tuple_like T>
status decode_tuple(R& r, T& out) {
    constexpr std::size_t N = std::tuple_size_v<bare<T>>;
    std::size_t off = r.offset();
    auto c = r.expect_array();
    if (!c) return fail(std::move(c));
    status result{};
    std::size_t got = 0;
    auto step = [&]<std::size_t I>(std::integral_constant<std::size_t, I>) -> bool {
        auto more = c->next();
        if (!more) { result = fail(std::move(more)); return false; }
        if (!*more) {
            result = std::unexpected(r.error(errc::type_mismatch, off)
                .with_expected("array of " + std::to_string(N) + " elements")
                .with_found("array of " + std::to_string(I) + " elements"));
            return false;
        }
        std::size_t mark = error_mark(r);
        auto st = decode_value<F>(r, std::get<I>(out));
        annotate_collected(r, mark, [&](error& e) { e.push_index(I); });
        if (!st) { st.error().push_index(I); result = std::move(st); return false; }
        ++got;
        return true;
    };
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (step(std::integral_constant<std::size_t, I>{}) && ...);
    }(std::make_index_sequence<N>{});
    if (!result) return result;
    auto more = c->next();
    if (!more) return fail(std::move(more));
    if (*more) {
        std::size_t extra = N;
        while (*more) {
            auto st = r.skip_value(); if (!st) return st;
            ++extra;
            more = c->next(); if (!more) return fail(std::move(more));
        }
        return std::unexpected(r.error(errc::type_mismatch, off)
            .with_expected("array of " + std::to_string(N) + " elements")
            .with_found("array of " + std::to_string(extra) + " elements"));
    }
    return {};
}

// ---- maps ----------------------------------------------------------------------------------

template<class K, reader R>
status parse_integral_key(R& r, std::string_view text, std::size_t off, K& out) {
    K v{};
    auto res = std::from_chars(text.data(), text.data() + text.size(), v);
    if (res.ec == std::errc::result_out_of_range)
        return std::unexpected(r.error(errc::out_of_range, off).with_detail(text).with_context(type_display<K>()));
    if (res.ec != std::errc{} || res.ptr != text.data() + text.size())
        return std::unexpected(r.error(errc::type_mismatch, off).with_expected("integer key").with_found("string")
                                   .with_length(text.size() + 2));
    out = v;
    return {};
}

template<field_meta F, reader R, map_like M>
status decode_map(R& r, M& out) {
    auto c = r.expect_map();
    if (!c) return fail(std::move(c));
    if constexpr (requires { out.clear(); }) out.clear();
    using K = map_key_t<M>;
    using V = map_mapped_t<M>;
    for (;;) {
        auto more = c->next();
        if (!more) return fail(std::move(more));
        if (!*more) break;
        std::size_t key_off = r.offset();
        K key{};
        if constexpr (string_like<K>) {
            auto kt = c->key_text();
            if (!kt) return fail(std::move(kt));
            if constexpr (string_view_type<K>) {
                constexpr field_info fi = F.info;
                if (!kt->borrowed)
                    return std::unexpected(r.error(errc::escape_in_borrowed_string, key_off)
                        .with_context(F.depth ? fi.qualified.view() : type_display<M>())
                        .with_suggestion("use std::string keys instead of std::string_view"));
                key = K(kt->text.data(), kt->text.size());
            } else {
                key = K(kt->text.begin(), kt->text.end());
            }
        } else {
            kind kk = c->key_kind();
            if (kk == kind::text) {
                auto kt = c->key_text();
                if (!kt) return fail(std::move(kt));
                auto st = parse_integral_key<K>(r, kt->text, key_off, key);
                if (!st) return st;
            } else if (kk == kind::uint) {
                auto ku = c->key_uint();
                if (!ku) return fail(std::move(ku));
                if (*ku > static_cast<std::uint64_t>(std::numeric_limits<K>::max()))
                    return std::unexpected(r.error(errc::out_of_range, key_off).with_detail(decimal(*ku)).with_context(type_display<K>()));
                key = static_cast<K>(*ku);
            } else {
                auto ki = c->key_sint();
                if (!ki) return fail(std::move(ki));
                if constexpr (std::is_unsigned_v<K>) {
                    return std::unexpected(r.error(errc::out_of_range, key_off).with_detail(decimal(*ki)).with_context(type_display<K>()));
                } else {
                    if (*ki > static_cast<std::int64_t>(std::numeric_limits<K>::max()) || *ki < static_cast<std::int64_t>(std::numeric_limits<K>::min()))
                        return std::unexpected(r.error(errc::out_of_range, key_off).with_detail(decimal(*ki)).with_context(type_display<K>()));
                    key = static_cast<K>(*ki);
                }
            }
        }
        auto push = [&](error& e) {
            if constexpr (string_like<K>) e.push_key(std::string_view(key));
            else e.push_key(static_cast<std::uint64_t>(key));
        };
        // Duplicate keys follow the same policy as struct members when the container has
        // unique keys. A range of pairs keeps every entry in order.
        if constexpr (unique_keyed_map<M>) {
            if (auto it = out.find(key); it != out.end()) {
                if constexpr (R::duplicates == duplicate_key::error) {
                    error e = r.error(errc::duplicate_key, key_off);
                    if constexpr (string_like<K>) e.with_detail(std::string_view(key)).with_length(std::string_view(key).size() + 2);
                    else e.with_detail(decimal(static_cast<std::int64_t>(key)));
                    push(e);
                    return std::unexpected(std::move(e));
                } else if constexpr (R::duplicates == duplicate_key::first_wins) {
                    auto st = r.skip_value(); if (!st) return st;
                    continue;
                } else {
                    std::size_t mark = error_mark(r);
                    auto st = decode_value<F>(r, it->second);
                    annotate_collected(r, mark, push);
                    if (!st) { push(st.error()); return st; }
                    continue;
                }
            }
        }
        V val{};
        std::size_t mark = error_mark(r);
        auto st = decode_value<F>(r, val);
        annotate_collected(r, mark, push);
        if (!st) { push(st.error()); return st; }
        insert_into(out, std::move(key), std::move(val));
    }
    return {};
}

// ---- structs -------------------------------------------------------------------------------

template<class T>
consteval std::size_t deserialized_field_count() {
    std::size_t n = 0;
    for (auto const& f : schema_of<T>) if (!f.skip_deserializing()) ++n;
    return n;
}

template<class T>
consteval std::vector<char> deny_context() {
    std::vector<char> v;
    auto n = type_name_of(^^T);
    v.insert(v.end(), n.begin(), n.end());
    return v;
}

template<field_meta F, reader R, class T>
status decode_field(R& r, T& out) {
    return decode_value<F>(r, access<F>(out));
}

// Decodes an object into a struct. `ignore_key` is the discriminator to skip when the struct
// is an internally-tagged variant alternative.
template<field_meta F, reader R, reflectable_class T>
status decode_struct(R& r, T& out, std::string_view ignore_key = {}) {
    constexpr auto& ti = type_schema_of<T>;
    constexpr auto schema = schema_of<T>;
    constexpr auto& lut = lookup_of<T>;
    constexpr std::size_t N = schema.size();

    if constexpr (ti.transparent) {
        template for (constexpr auto f : fields_of<T>) {
            constexpr field_info fi = f.info;
            std::size_t mark = error_mark(r);
            auto st = decode_field<f>(r, out);
            annotate_collected(r, mark, [&](error& e) { e.push_member(fi.identifier.view()); });
            if (!st) st.error().push_member(fi.identifier.view());
            return st;
        }
    }

    std::size_t obj_off = r.offset();
    auto c = r.expect_map();
    if (!c) return fail(std::move(c));
    std::bitset<N ? N : 1> seen;

    for (;;) {
        auto more = c->next();
        if (!more) return fail(std::move(more));
        if (!*more) break;
        std::size_t key_off = r.offset();
        auto kt = c->key_text();
        if (!kt) return fail(std::move(kt));
        std::string_view key = kt->text;
        std::size_t idx = lut.find(key);

        if (idx == npos) {
            if (!ignore_key.empty() && key == ignore_key) {
                auto st = r.skip_value(); if (!st) return st;
                continue;
            }
            if constexpr (ti.deny_unknown_fields) {
                error e = r.error(errc::unknown_field, key_off).with_detail(key).with_length(key.size() + 2)
                              .with_context(ti.name.view()).with_deny_unknown();
                if (auto s = suggest(key, schema); !s.empty()) e.with_suggestion(s);
                e.push_key(key);
                if constexpr (collecting<R>) {
                    r.collected().push_back(std::move(e));
                    auto st = r.skip_value(); if (!st) return st;
                    continue;
                } else {
                    return std::unexpected(std::move(e));
                }
            } else {
                auto st = r.skip_value(); if (!st) return st;
                continue;
            }
        }

        if (schema[idx].skip_deserializing()) {
            auto st = r.skip_value(); if (!st) return st;
            continue;
        }

        if (seen[idx]) {
            if constexpr (R::duplicates == duplicate_key::error) {
                error e = r.error(errc::duplicate_key, key_off).with_detail(key).with_length(key.size() + 2);
                e.push_key(key);
                if constexpr (collecting<R>) {
                    r.collected().push_back(std::move(e));
                    auto st = r.skip_value(); if (!st) return st;
                    continue;
                } else return std::unexpected(std::move(e));
            } else if constexpr (R::duplicates == duplicate_key::first_wins) {
                auto st = r.skip_value(); if (!st) return st;
                continue;
            }
            // last_wins: decode again into the same member
        }
        seen[idx] = true;

        std::size_t value_pos = r.save();
        std::size_t mark = error_mark(r);
        status st{};
        std::string_view ident;
        template for (constexpr auto f : fields_of<T>) {
            if constexpr (!f.info.skip_deserializing()) {
                constexpr field_info fi = f.info;
                if (idx == fi.member_index) {
                    ident = fi.identifier.view();
                    st = decode_field<f>(r, out);
                }
            }
        }
        annotate_collected(r, mark, [&](error& e) { e.push_member(ident); });
        if (!st) {
            st.error().push_member(ident);
            if constexpr (collecting<R>) {
                if (is_syntax_error(st.error().code())) return st;
                r.collected().push_back(std::move(st.error()));
                r.restore(value_pos);
                auto sk = r.skip_value(); if (!sk) return sk;
                continue;
            } else {
                return st;
            }
        }
    }

    // Missing members: apply defaults, then report required ones (§5.2).
    status missing{};
    template for (constexpr auto f : fields_of<T>) {
        if constexpr (!f.info.skip_deserializing()) {
            constexpr field_info fi = f.info;
            if (!seen[fi.member_index]) {
                if constexpr (fi.required()) {
                    // [[=required]] wins over a default_value: the key must be present.
                    if (missing) {
                        error e = r.error(errc::missing_field, obj_off).with_detail(fi.wire_name.view())
                                      .with_container_offset(obj_off).with_context(ti.name.view());
                        if constexpr (collecting<R>) r.collected().push_back(std::move(e));
                        else missing = std::unexpected(std::move(e));
                    }
                } else if constexpr (f.default_ann != ^^void) {
                    constexpr auto dv = std::meta::extract<typename [:annotation_type(f.default_ann):]>(f.default_ann).value;
                    auto& m = access<f>(out);
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(dv)>, static_string>) {
                        if constexpr (requires { m = std::string_view(dv.view()); }) m = dv.view();
                        else m = bare<decltype(m)>(dv.view().begin(), dv.view().end());
                    } else if constexpr (optional_like<decltype(m)>) {
                        emplace_value(m) = dv;
                    } else {
                        m = static_cast<bare<decltype(m)>>(dv);
                    }
                }
            }
        }
    }
    return missing;
}

// ---- variants (§5.3 tag) ---------------------------------------------------------------------

template<field_meta F, reader R, variant_like V>
status decode_variant(R& r, V& out) {
    static_assert(has(F.info.flags, field_flags::has_tag), "variant without a discriminator reached construction");
    constexpr std::string_view tag_key = F.info.tag_key.view();
    constexpr auto& names = variant_names<V>;
    constexpr std::size_t N = std::variant_size_v<bare<V>>;

    std::size_t start = r.save();
    std::size_t obj_off = r.offset();
    auto c = r.expect_map();
    if (!c) return fail(std::move(c));

    // First pass: find the discriminator.
    std::size_t alt = npos;
    std::size_t tag_off = obj_off;
    std::string tag_value;
    bool found = false;
    for (;;) {
        auto more = c->next();
        if (!more) return fail(std::move(more));
        if (!*more) break;
        std::size_t key_off = r.offset();
        auto kt = c->key_text();
        if (!kt) return fail(std::move(kt));
        if (kt->text == tag_key) {
            tag_off = r.offset();
            auto tv = r.expect_text();
            if (!tv) return fail(std::move(tv));
            tag_value.assign(tv->text);
            found = true;
            (void)key_off;
            // Consume the rest so a syntax error in the object is still reported.
            for (;;) {
                auto m2 = c->next(); if (!m2) return fail(std::move(m2));
                if (!*m2) break;
                auto k2 = c->key_text(); if (!k2) return fail(std::move(k2));
                auto st = r.skip_value(); if (!st) return st;
            }
            break;
        }
        auto st = r.skip_value();
        if (!st) return st;
    }
    if (!found) {
        error e = r.error(errc::missing_tag, obj_off).with_detail(tag_key).with_container_offset(obj_off)
                      .with_context(type_display<V>());
        for (auto const& n : names) e.with_candidate(n.view());
        return std::unexpected(std::move(e));
    }
    for (std::size_t i = 0; i < N; ++i) if (names[i].view() == tag_value) alt = i;
    if (alt == npos) {
        error e = r.error(errc::no_variant_alternative, tag_off).with_detail(tag_value).with_context(type_display<V>())
                      .with_length(tag_value.size() + 2);
        for (auto const& n : names) e.with_candidate(n.view());
        e.push_key(tag_key);
        return std::unexpected(std::move(e));
    }

    // Second pass: decode the chosen alternative from the object start.
    r.restore(start);
    status result{};
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        ([&] {
            if (alt != I) return;
            using A = std::variant_alternative_t<I, bare<V>>;
            A& a = out.template emplace<I>();
            if constexpr (internally_tagged<A>) {
                result = decode_struct<no_field>(r, a, tag_key);
            } else {
                // Externally tagged: { tag: name, "value": v }
                auto c2 = r.expect_map();
                if (!c2) { result = fail(std::move(c2)); return; }
                bool have_value = false;
                for (;;) {
                    auto more = c2->next(); if (!more) { result = fail(std::move(more)); return; }
                    if (!*more) break;
                    std::size_t key_off = r.offset();
                    auto kt = c2->key_text(); if (!kt) { result = fail(std::move(kt)); return; }
                    if (kt->text == variant_value_key.view()) {
                        have_value = true;
                        std::size_t mark = error_mark(r);
                        auto st = decode_value<no_field>(r, a);
                        annotate_collected(r, mark, [&](error& e) { e.push_member(variant_value_key.view()); });
                        if (!st) { st.error().push_member(variant_value_key.view()); result = std::move(st); return; }
                    } else if (kt->text == tag_key) {
                        auto st = r.skip_value(); if (!st) { result = st; return; }
                    } else {
                        (void)key_off;
                        auto st = r.skip_value(); if (!st) { result = st; return; }
                    }
                }
                if (!have_value)
                    result = std::unexpected(r.error(errc::missing_field, obj_off).with_detail(variant_value_key.view())
                                                 .with_container_offset(obj_off).with_context(type_display<V>()));
            }
        }(), ...);
    }(std::make_index_sequence<N>{});
    return result;
}

// ---- dispatch ------------------------------------------------------------------------------

template<field_meta F, reader R, class T>
status decode_value(R& r, T& out) {
    using Fmt = format_of<R>;
    if constexpr (F.codec != ^^void) {
        using Codec = typename [:F.codec:];
        if constexpr (requires { { Codec::decode(r, out) } -> std::same_as<status>; }) return Codec::decode(r, out);
        else { auto v = Codec::decode(r); if (!v) return fail(std::move(v)); out = std::move(*v); return {}; }
    } else if constexpr (has_codec_for<T, Fmt>) {
        using Codec = codec_for_t<T, Fmt>;
        if constexpr (requires { { Codec::decode(r, out) } -> std::same_as<status>; }) return Codec::decode(r, out);
        else { auto v = Codec::decode(r); if (!v) return fail(std::move(v)); out = std::move(*v); return {}; }
    } else if constexpr (boolean_type<T>) {
        auto b = r.expect_boolean(); if (!b) return fail(std::move(b)); out = *b; return {};
    } else if constexpr (char_type<T>) {
        return decode_char<F>(r, out);
    } else if constexpr (unsigned_integral_type<T>) {
        return decode_uint<F>(r, out);
    } else if constexpr (signed_integral_type<T>) {
        return decode_sint<F>(r, out);
    } else if constexpr (floating_type<T>) {
        auto d = r.expect_real(); if (!d) return fail(std::move(d)); out = static_cast<T>(*d); return {};
    } else if constexpr (enum_type<T>) {
        return decode_enum<F>(r, out);
    } else if constexpr (decodable_string<T>) {
        return decode_string<F>(r, out);
    } else if constexpr (optional_like<T>) {
        return decode_optional<F>(r, out);
    } else if constexpr (variant_like<T>) {
        return decode_variant<F>(r, out);
    } else if constexpr (format_traits<Fmt>::template is_bytes<F, bare<T>>()) {
        return decode_bytes<F>(r, out);
    } else if constexpr (map_like<T>) {
        return decode_map<F>(r, out);
    } else if constexpr (tuple_like<T>) {
        return decode_tuple<F>(r, out);
    } else if constexpr (fixed_array<T>) {
        return decode_fixed_array<F>(r, out);
    } else if constexpr (appendable_range<T>) {
        return decode_sequence<F>(r, out);
    } else if constexpr (reflectable_class<T>) {
        return decode_struct<F>(r, out);
    } else {
        static_assert(false, "vcodec: type reached construction without a codec; the audit should have rejected it");
    }
}

// Top-level entry: finalizes every error's path into root-first order.
template<reader R, class T>
status decode(R& r, T& out) {
    auto st = decode_value<no_field>(r, out);
    if (!st) st.error().finalize();
    if constexpr (collecting<R>) for (auto& e : r.collected()) e.finalize();
    return st;
}

} // namespace vcodec::core

#endif // VCODEC_CORE_BUILD_HPP

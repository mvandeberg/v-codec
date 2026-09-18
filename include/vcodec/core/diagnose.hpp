// Compile-time diagnostics (spec §10). A consteval audit walks a type for a given format and
// direction and either passes or produces one message naming the offending member path and
// the reason. Public entry points gate on the audit first, so instantiation never fails
// inside the traversal: the user sees one static_assert, not a nested-template wall.
//
// The audit is format-agnostic: it consults format_traits<Format> for the two places a
// format's annotations decide representability (bytes, non-text keys).
#ifndef VCODEC_CORE_DIAGNOSE_HPP
#define VCODEC_CORE_DIAGNOSE_HPP

#include <vcodec/core/codec_for.hpp>
#include <vcodec/core/concepts.hpp>
#include <vcodec/core/fixed_string.hpp>
#include <vcodec/core/keys.hpp>
#include <vcodec/core/model.hpp>
#include <vcodec/core/schema.hpp>

#include <meta>
#include <string>
#include <string_view>
#include <vector>

namespace vcodec::core {

using message = fixed_string<1024>;

enum class direction : std::uint8_t { encode, decode };

struct audit_result {
    bool    ok = true;
    bool    borrows = false;    // decode side: T holds std::string_view / std::span members
    message text{};
};

namespace detail {

struct audit_state {
    direction                    dir;
    std::vector<std::meta::info> visiting;   // types on the current path (cycle guard)
    std::vector<std::meta::info> done;       // types fully audited
    std::string                  error;      // first message, §10 text
    bool                         borrows = false;
    consteval bool failed() const { return !error.empty(); }
    consteval void fail(std::string msg) { if (error.empty()) error = std::move(msg); }
};

// Built from an iterator range: the (const char*, n) constructor compares the pointer with
// nullptr, which UBSan instrumentation turns into a non-constant expression under GCC 16.
consteval std::string str(std::string_view s) { return std::string(s.begin(), s.end()); }
consteval std::string type_str(std::meta::info t) { auto v = qualified_type_name(t); return std::string(v.begin(), v.end()); }

// §10 message shapes.
consteval std::string msg_no_codec(std::string_view path, std::string_view type) {
    return str(path) + " (type " + str(type) + ")\n  has no codec. Provide vcodec::codec_for<" + str(type)
        + ">, mark the member\n  [[=vcodec::skip]], or give it [[=vcodec::with<YourCodec>]].";
}
consteval std::string msg_no_bytes_lowering(std::string_view path, std::string_view type, std::string_view format, std::string_view hint) {
    std::string m = str(path) + " (" + str(type) + ")\n  is byte-like but has no " + str(format) + " lowering.\n  ";
    m += hint.empty() ? std::string("Give it a lowering") : str(hint);
    m += ", or mark it [[=vcodec::skip]].";
    return m;
}
consteval std::string msg_needs_tag(std::string_view path, std::string_view type, std::string_view owner) {
    return str(path) + " (" + str(type) + ")\n  needs a discriminator. Add [[=vcodec::tag(\"type\")]] to " + str(owner) + ".";
}
consteval std::string msg_key(std::string_view path, std::string_view type, std::string_view key, std::string_view format, std::string_view hint) {
    std::string m = str(path) + " (" + str(type) + ")\n  has " + str(key) + " keys, which " + str(format) + " cannot represent.\n  ";
    m += hint.empty() ? std::string("Give it a lowering") : str(hint);
    m += ", use string keys, or mark it [[=vcodec::skip]].";
    return m;
}
consteval std::string msg_encode_only(std::string_view path, std::string_view type, std::string_view use) {
    return str(path) + " (" + str(type) + ")\n  is encode-only and cannot be decoded into. Use " + str(use) + ", or mark it [[=vcodec::skip_deserializing]].";
}

template<class T, class Format, field_meta F> consteval void audit_type(audit_state& st, std::string path);

// Can a default_value payload of type D be assigned to a member of type M?
template<class M, class D>
consteval bool default_assignable() {
    if constexpr (std::same_as<D, static_string>)
        return std::is_assignable_v<M&, std::string_view> || std::is_constructible_v<M, const char*, const char*>;
    else if constexpr (optional_like<M>)
        return std::is_assignable_v<optional_value_t<M>&, D>;
    else
        return std::is_assignable_v<M&, D>;
}

template<class T, class Format>
consteval void audit_struct(audit_state& st, std::string const& path) {
    constexpr auto& ti = type_schema_of<T>;
    if (!ti.ok()) { st.fail(str(ti.error.view())); return; }
    template for (constexpr auto f : fields_of<T>) {
        if (st.failed()) return;
        constexpr field_info fi = f.info;
        if (st.dir == direction::encode && fi.skip_serializing()) continue;
        if (st.dir == direction::decode && fi.skip_deserializing()) continue;
        std::string member_path = path + "::" + str(fi.identifier.view());
        using M = typename [:f.type:];
        // Annotation sanity that does not depend on the member's classification.
        if (has(fi.flags, field_flags::skip_if_null) && !optional_like<M>) {
            st.fail(member_path + " (" + type_str(f.type) + ")\n  is declared [[=vcodec::skip_if_null]] but is not optional-like "
                    "(std::optional, std::unique_ptr, std::shared_ptr).");
            return;
        }
        if (has(fi.flags, field_flags::skip_if_default) && !std::equality_comparable<M>) {
            st.fail(member_path + " (" + type_str(f.type) + ")\n  is declared [[=vcodec::skip_if_default]] but is not equality-comparable.");
            return;
        }
        if (has(fi.flags, field_flags::skip_if_default) && !std::default_initializable<T>) {
            st.fail(member_path + " (" + type_str(f.type) + ")\n  is declared [[=vcodec::skip_if_default]], which compares against a default-constructed "
                    + str(type_name_of(^^T)) + ", but " + str(type_name_of(^^T)) + " is not default-constructible.");
            return;
        }
        if constexpr (f.default_ann != ^^void) {
            using D = std::remove_cvref_t<decltype(std::meta::extract<typename [:annotation_type(f.default_ann):]>(f.default_ann).value)>;
            if (!default_assignable<M, D>()) {
                st.fail(member_path + " (" + type_str(f.type) + ")\n  has a default_value whose type does not convert to the member's type.");
                return;
            }
        }
        if constexpr (variant_like<M>) {
            if (!has(fi.flags, field_flags::has_tag)) {
                st.fail(msg_needs_tag(member_path, type_str(f.type), type_name_of(^^T)));
                return;
            }
        }
        if (auto m = format_traits<Format>::template check_field<f, M>(); !m.empty()) {
            st.fail(member_path + " (" + type_str(f.type) + ")\n  " + m);
            return;
        }
        audit_type<M, Format, f>(st, member_path);
    }
}

template<class T, class Format, field_meta F>
consteval void audit_type(audit_state& st, std::string path) {
    if (st.failed()) return;
    using B = std::remove_cvref_t<T>;
    constexpr auto tinfo = std::meta::dealias(^^B);
    // Codecs win before classification.
    if constexpr (F.codec != ^^void) return;
    else if constexpr (has_codec_for<B, Format>) return;
    else if constexpr (boolean_type<B> || char_type<B> || integral_type<B> || floating_type<B> || enum_type<B>) {
        return;
    } else if constexpr (c_string<B>) {
        if (st.dir == direction::decode) st.fail(msg_encode_only(path, type_str(^^B), "std::string"));
        return;
    } else if constexpr (string_view_type<B>) {
        if (st.dir == direction::decode) st.borrows = true;
        return;
    } else if constexpr (owned_string<B>) {
        return;
    } else if constexpr (optional_like<B>) {
        audit_type<optional_value_t<B>, Format, F>(st, std::move(path));
        return;
    } else if constexpr (variant_like<B>) {
        if (!has(F.info.flags, field_flags::has_tag)) {
            st.fail(msg_needs_tag(path, type_str(^^B), path.substr(0, path.find("::"))));
            return;
        }
        [&]<class... A>(std::variant<A...>*) {
            (audit_type<A, Format, no_field>(st, path), ...);
        }(static_cast<B*>(nullptr));
        return;
    } else if constexpr (format_traits<Format>::template is_bytes<F, B>()) {
        // Bytes: the format decides both classification and representability.
        if constexpr (!format_traits<Format>::template can_lower_bytes<F, B>()) {
            st.fail(msg_no_bytes_lowering(path, type_str(^^B), format_traits<Format>::name, format_traits<Format>::bytes_hint));
            return;
        }
        if (st.dir == direction::decode) {
            if constexpr (span_like<B>) {
                if constexpr (format_traits<Format>::bytes_borrowable) { st.borrows = true; return; }
                st.fail(msg_encode_only(path, type_str(^^B), "std::vector<std::byte>")); return;
            }
            else if constexpr (!std_array<B> && !requires(B& b) { b.assign(b.begin(), b.end()); } && !requires(B& b) { b.push_back(*b.begin()); }) {
                st.fail(msg_encode_only(path, type_str(^^B), "std::vector<std::byte>")); return;
            }
        }
        return;
    } else if constexpr (map_like<B>) {
        using K = map_key_t<B>;
        if constexpr (!format_traits<Format>::template can_lower_key<F, K>()) {
            st.fail(msg_key(path, type_str(^^B), string_like<K> ? "text" : "integer", format_traits<Format>::name, format_traits<Format>::key_hint));
            return;
        }
        if (st.dir == direction::decode) {
            if constexpr (!insertable_map<B>) { st.fail(msg_encode_only(path, type_str(^^B), "std::map or a range with emplace/insert/push_back")); return; }
            if constexpr (string_view_type<K>) st.borrows = true;
            if constexpr (c_string<K>) { st.fail(msg_encode_only(path, type_str(^^B), "std::string keys")); return; }
        }
        audit_type<map_mapped_t<B>, Format, F>(st, std::move(path));
        return;
    } else if constexpr (tuple_like<B>) {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (audit_type<std::tuple_element_t<I, B>, Format, F>(st, path), ...);
        }(std::make_index_sequence<std::tuple_size_v<B>>{});
        return;
    } else if constexpr (span_like<B>) {
        if (st.dir == direction::decode) { st.fail(msg_encode_only(path, type_str(^^B), "std::vector")); return; }
        audit_type<range_value<B>, Format, F>(st, std::move(path));
        return;
    } else if constexpr (fixed_array<B>) {
        audit_type<range_value<B>, Format, F>(st, std::move(path));
        return;
    } else if constexpr (sequence<B>) {
        if (st.dir == direction::decode && !appendable_range<B>) {
            st.fail(path + " (" + type_str(^^B) + ")\n  is a range without push_back, emplace_back or insert, so it cannot be decoded into. "
                    "Use a container with one of those, or mark it [[=vcodec::skip_deserializing]].");
            return;
        }
        audit_type<range_value<B>, Format, F>(st, std::move(path));
        return;
    } else if constexpr (reflectable_class<B>) {
        // Cycle guard: a recursive type (a tree node holding a vector of itself) is audited once.
        for (auto v : st.visiting) if (v == tinfo) return;
        for (auto d : st.done) if (d == tinfo) return;
        st.visiting.push_back(tinfo);
        audit_struct<B, Format>(st, path);
        st.visiting.pop_back();
        st.done.push_back(tinfo);
        return;
    } else {
        st.fail(msg_no_codec(path, type_str(^^B)));
        return;
    }
}

} // namespace detail

template<class T, class Format, direction Dir>
consteval audit_result audit() {
    detail::audit_state st{ Dir, {}, {}, {}, false };
    using B = std::remove_cvref_t<T>;
    detail::audit_type<B, Format, no_field>(st, detail::str(type_name_of(^^B)));
    audit_result r;
    r.ok = !st.failed();
    r.borrows = st.borrows;
    if (!r.ok) r.text.append(st.error);
    return r;
}

template<class T, class Format, direction Dir>
inline constexpr audit_result audit_of = audit<T, Format, Dir>();

// The static_assert payload: `.data()` / `.size()` per P2741.
template<class T, class Format, direction Dir = direction::encode>
consteval message explain() {
    return audit<T, Format, Dir>().text;
}

} // namespace vcodec::core

#endif // VCODEC_CORE_DIAGNOSE_HPP

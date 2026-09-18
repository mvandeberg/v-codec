// Schema extraction (spec §6.1). Two tables per type, indexed identically:
//
//   fields_of<T>  — consteval-only field_meta: member paths, types, annotation reflections.
//                   Drives `template for` in traversal and construction.
//   schema_of<T>  — runtime-usable field_info: wire names, aliases, flags. Drives key lookup
//                   and did-you-mean.
//
// The split is forced by GCC 16 (M0 finding 2): a struct holding std::meta::info is
// consteval-only and cannot be read at runtime. Flatten and inheritance are resolved here,
// so traversal never needs to know either exists.
#ifndef VCODEC_CORE_SCHEMA_HPP
#define VCODEC_CORE_SCHEMA_HPP

#include <vcodec/core/annotations.hpp>
#include <vcodec/core/concepts.hpp>
#include <vcodec/core/describe.hpp>
#include <vcodec/core/fixed_string.hpp>

#include <meta>
#include <array>
#include <cstdint>
#include <span>
#include <variant>
#include <string_view>
#include <vector>

namespace vcodec::core {

inline constexpr std::size_t max_member_depth = 8;    // flatten nesting + base chain
inline constexpr std::size_t max_annotations  = 16;

enum class field_flags : std::uint16_t {
    none               = 0,
    skip_serializing   = 1u << 0,
    skip_deserializing = 1u << 1,
    skip_if_null       = 1u << 2,
    skip_if_default    = 1u << 3,
    required           = 1u << 4,   // resolved requiredness (§5.2)
    has_default        = 1u << 5,   // carries default_value
    flattened          = 1u << 6,   // reached through [[=flatten]]
    inherited          = 1u << 7,   // reached through a base class
    optional_like      = 1u << 8,   // type is optional-like
    has_dmi            = 1u << 9,   // has a default member initialiser
    explicit_name      = 1u << 10,  // wire name comes from name("…")
    renamed            = 1u << 11,  // wire name comes from rename_all
    has_tag            = 1u << 12,  // a discriminator applies to this (variant) member
};
constexpr field_flags operator|(field_flags a, field_flags b) noexcept {
    return field_flags(std::uint16_t(a) | std::uint16_t(b));
}
constexpr field_flags& operator|=(field_flags& a, field_flags b) noexcept { return a = a | b; }
constexpr bool has(field_flags a, field_flags b) noexcept {
    return (std::uint16_t(a) & std::uint16_t(b)) != 0;
}

// Runtime-usable, structural.
struct field_info {
    static_string        wire_name;      // resolved: name > rename_all > identifier
    static_string        identifier;     // C++ member name, for diagnostics and error paths
    static_string        qualified;      // Owner::identifier
    const static_string* alias_ptr = nullptr;
    std::size_t          alias_count = 0;
    bool                 has_int_key = false;
    std::uint64_t        int_key = 0;    // CBOR; unused in v0.1, present in the table
    field_flags          flags = field_flags::none;
    std::size_t          member_index = 0;
    static_string        tag_key{};      // discriminator key if has_tag

    constexpr std::span<const static_string> aliases() const noexcept { return {alias_ptr, alias_count}; }
    constexpr bool required() const noexcept { return has(flags, field_flags::required); }
    constexpr bool skip_serializing() const noexcept { return has(flags, field_flags::skip_serializing); }
    constexpr bool skip_deserializing() const noexcept { return has(flags, field_flags::skip_deserializing); }
};

// Consteval-only.
struct field_meta {
    field_info      info;
    std::meta::info path[max_member_depth]{};   // base subobjects and members, root-first
    std::size_t     depth = 0;
    std::meta::info type{};                     // declared type of the leaf member
    std::meta::info owner{};                    // class declaring the leaf member
    std::meta::info annotations[max_annotations]{};
    std::size_t     annotation_count = 0;
    std::meta::info codec = ^^void;             // with<C>: ^^C, else ^^void
    std::meta::info default_ann = ^^void;       // default_value_t<V> annotation, or ^^void

    consteval std::span<const std::meta::info> anns() const noexcept { return {annotations, annotation_count}; }
    consteval std::meta::info leaf() const noexcept { return path[depth - 1]; }
};

// Type-level information, runtime-usable.
struct type_info {
    static_string name;                 // identifier_of(T) or display string
    bool          deny_unknown_fields = false;
    bool          transparent = false;
    bool          has_tag = false;
    static_string tag_key{};
    bool          has_rename = false;
    case_         rename = case_::snake;
    bool          conditional_fields = false;   // any skip_if_null / skip_if_default → map length indefinite
    std::size_t   field_count = 0;
    static_string error{};              // non-empty: schema is unusable; message per §10
    constexpr bool ok() const noexcept { return error.len == 0; }
};

// ---- helpers ------------------------------------------------------------------------------

consteval std::string_view type_name_of(std::meta::info t) {
    t = std::meta::dealias(t);
    if (std::meta::has_identifier(t)) return std::meta::identifier_of(t);
    return std::meta::display_string_of(t);
}

// A readable, qualified spelling of a type for diagnostics. GCC 16 does not preserve the
// alias a member was declared with (type_of(m) for a std::uint8_t member is `unsigned char`),
// so integer types other than `int` are rendered by their fixed-width name — the spelling
// most code uses and the one that states the range. Display strings lose GCC's " {aka …}".
consteval std::vector<char> qualified_type_name(std::meta::info t) {
    std::vector<char> out;
    auto append = [&](std::string_view s) { out.insert(out.end(), s.begin(), s.end()); };
    t = std::meta::dealias(t);   // a template parameter or alias name says nothing to the user
    auto d = t;
    if (std::meta::is_integral_type(d) && d != (^^bool) && d != (^^char) && d != (^^int)
        && d != (^^wchar_t) && d != (^^char8_t) && d != (^^char16_t) && d != (^^char32_t)) {
        bool is_signed = std::meta::is_signed_type(d);
        append(is_signed ? "std::int" : "std::uint");
        switch (std::meta::size_of(d)) {
        case 1: append("8"); break;
        case 2: append("16"); break;
        case 4: append("32"); break;
        case 8: append("64"); break;
        default: append("128"); break;
        }
        append("_t");
        return out;
    }
    if (std::meta::is_type(t) && std::meta::has_identifier(t) && !std::meta::has_template_arguments(t)
        && !std::meta::is_fundamental_type(d)) {
        std::vector<std::string_view> parts{ std::meta::identifier_of(t) };
        if (std::meta::has_parent(t)) {
            auto p = std::meta::parent_of(t);
            while (p != ^^:: && std::meta::has_identifier(p)) {
                parts.push_back(std::meta::identifier_of(p));
                if (!std::meta::has_parent(p)) break;
                p = std::meta::parent_of(p);
            }
        }
        for (std::size_t i = parts.size(); i-- > 0;) {
            append(parts[i]);
            if (i) append("::");
        }
        return out;
    }
    std::string_view ds = std::meta::display_string_of(t);
    if (auto pos = ds.find(" {aka"); pos != std::string_view::npos) ds = ds.substr(0, pos);
    // Normalise libstdc++'s spellings to the ones users write, and drop the useless
    // {anonymous}:: prefix of types in unnamed namespaces.
    struct rewrite { std::string_view from, to; };
    constexpr rewrite rewrites[] = {
        { "{anonymous}::", "" },
        { "std::__cxx11::basic_string<char> >", "std::string>" },
        { "std::__cxx11::basic_string<char>", "std::string" },
        { "std::basic_string<char> >", "std::string>" },
        { "std::basic_string<char>", "std::string" },
        { "std::basic_string_view<char> >", "std::string_view>" },
        { "std::basic_string_view<char>", "std::string_view" },
        { "std::__cxx11::basic_string<char8_t> >", "std::u8string>" },
        { "std::__cxx11::basic_string<char8_t>", "std::u8string" },
        { "std::basic_string<char8_t> >", "std::u8string>" },
        { "std::basic_string<char8_t>", "std::u8string" },
        { "std::__cxx11::basic_string<wchar_t>", "std::wstring" },
        { "std::basic_string<wchar_t>", "std::wstring" },
        { "std::__cxx11::basic_string<char16_t>", "std::u16string" },
        { "std::basic_string<char16_t>", "std::u16string" },
        { "std::__cxx11::basic_string<char32_t>", "std::u32string" },
        { "std::basic_string<char32_t>", "std::u32string" },
        { "std::__cxx11::", "std::" },
        { "std::chrono::_V2::", "std::chrono::" },
        { "> >", ">>" },
    };
    for (std::size_t i = 0; i < ds.size();) {
        bool replaced = false;
        for (auto const& rw : rewrites) {
            if (ds.substr(i, rw.from.size()) == rw.from) {
                out.insert(out.end(), rw.to.begin(), rw.to.end());
                i += rw.from.size();
                replaced = true;
                break;
            }
        }
        if (!replaced) out.push_back(ds[i++]);
    }
    return out;
}

// The unqualified name of a type, usable at runtime (identifier, or display string).
template<class T>
constexpr std::string_view type_schema_name() {
    constexpr auto n = [] { return intern(type_name_of(^^T)); }();
    return n.view();
}

// Evaluate a variable template <V> at a type given as a reflection.
consteval bool eval_trait(std::meta::info var_tmpl, std::meta::info type) {
    return std::meta::extract<bool>(std::meta::substitute(var_tmpl, { type }));
}

template<class T> inline constexpr bool is_optional_like_v = optional_like<T>;
template<class T> inline constexpr bool is_variant_like_v  = variant_like<T>;
template<class T> inline constexpr bool is_class_type_v    = class_type<T>;

namespace detail {

struct build_state {
    std::vector<field_meta>      fields;
    std::vector<std::meta::info> flatten_stack;  // types currently being flattened, cycle check
    std::vector<char>            error;          // first schema error, §10 text
    bool                         conditional = false;

    consteval void fail(std::string_view msg) { if (error.empty()) error.assign(msg.begin(), msg.end()); }
};

// The rename_all style declared on a class, if any (source annotation or describe<T>).
template<class T>
consteval std::vector<std::meta::info> type_annotations_of() {
    std::vector<std::meta::info> out;
    for (auto a : std::meta::annotations_of(^^T)) out.push_back(a);
    if constexpr (describe_has_annotations<T>) {
        constexpr auto& d = describe<T>::annotations;
        for (std::size_t i = 0; i < d.count; ++i) out.push_back(d.annotations[i]);
    }
    return out;
}

// Annotations on a member: those in source plus those from describe<Owner>.
template<class Owner>
consteval std::vector<std::meta::info> member_annotations_of(std::meta::info m) {
    std::vector<std::meta::info> out;
    for (auto a : std::meta::annotations_of(m)) out.push_back(a);
    if constexpr (describe_has_members<Owner>) {
        constexpr auto& ms = describe<Owner>::members;
        for (auto const& d : ms) {
            if (d.identifier.view() == std::meta::identifier_of(m))
                for (std::size_t i = 0; i < d.count; ++i) out.push_back(d.annotations[i]);
        }
    }
    return out;
}

template<class Owner>
consteval void check_described_identifiers(build_state& st) {
    if constexpr (describe_has_members<Owner>) {
        constexpr auto ctx = std::meta::access_context::unchecked();
        constexpr auto& ms = describe<Owner>::members;
        for (auto const& d : ms) {
            bool found = false;
            for (auto m : std::meta::nonstatic_data_members_of(^^Owner, ctx))
                if (std::meta::has_identifier(m) && std::meta::identifier_of(m) == d.identifier.view()) found = true;
            if (!found) {
                fixed_string<256> msg;
                msg.append("vcodec::describe<").append(type_name_of(^^Owner)).append(">: '")
                   .append(d.identifier.view()).append("' is not a non-static data member of ")
                   .append(type_name_of(^^Owner)).append(".");
                st.fail(msg.view());
            }
        }
    }
}

using collect_fn = void(*)(build_state&, std::vector<std::meta::info>, bool, bool);

template<class T>
consteval void collect_fields(build_state& st, std::vector<std::meta::info> prefix,
                              bool via_flatten, bool via_base) {
    constexpr auto ctx = std::meta::access_context::unchecked();
    check_described_identifiers<T>(st);

    // Inherited members first, in declaration order of the bases.
    for (auto b : std::meta::bases_of(^^T, ctx)) {
        if (std::meta::is_virtual(b)) {
            fixed_string<256> msg;
            msg.append(type_name_of(^^T)).append(" has a virtual base class ")
               .append(type_name_of(std::meta::type_of(b)))
               .append("; virtual inheritance is not supported by vcodec v0.1.");
            st.fail(msg.view());
            continue;
        }
        auto p = prefix; p.push_back(b);
        auto fn = std::meta::extract<collect_fn>(std::meta::substitute(^^collect_fields, { std::meta::type_of(b) }));
        fn(st, p, via_flatten, true);
    }

    std::vector<std::meta::info> tanns = type_annotations_of<T>();
    auto rename = find_annotation<rename_all_t>(tanns);
    auto type_tag = find_annotation<tag_t>(tanns);

    for (auto m : std::meta::nonstatic_data_members_of(^^T, ctx)) {
        if (!std::meta::has_identifier(m)) {
            fixed_string<256> msg;
            msg.append(type_name_of(^^T)).append(" has an anonymous non-static data member; "
                       "anonymous unions and members are not supported by vcodec v0.1.");
            st.fail(msg.view());
            continue;
        }
        auto anns = member_annotations_of<T>(m);
        if (has_annotation<skip_t>(anns)) continue;
        if (anns.size() > max_annotations) {
            fixed_string<256> msg;
            msg.append(type_name_of(^^T)).append("::").append(std::meta::identifier_of(m))
               .append(" carries more than 16 annotations.");
            st.fail(msg.view());
            continue;
        }

        if (has_annotation<flatten_t>(anns)) {
            auto mt = std::meta::dealias(std::meta::type_of(m));
            if (!std::meta::is_class_type(mt) || eval_trait(^^is_optional_like_v, mt)
                || !eval_trait(^^is_class_type_v, mt)) {
                fixed_string<256> msg;
                msg.append(type_name_of(^^T)).append("::").append(std::meta::identifier_of(m))
                   .append(" is declared [[=vcodec::flatten]] but its type ")
                   .append(std::meta::display_string_of(mt))
                   .append(" is not a struct. flatten splices a nested struct's members into the parent.");
                st.fail(msg.view());
                continue;
            }
            bool cycle = mt == std::meta::dealias(^^T);
            for (auto s : st.flatten_stack) if (s == mt) cycle = true;
            if (cycle) {
                fixed_string<256> msg;
                msg.append(type_name_of(^^T)).append("::").append(std::meta::identifier_of(m))
                   .append(" is declared [[=vcodec::flatten]] but ").append(type_name_of(mt))
                   .append(" is already being flattened; flatten cycles are not allowed.");
                st.fail(msg.view());
                continue;
            }
            if (prefix.size() + 1 >= max_member_depth) {
                fixed_string<256> msg;
                msg.append(type_name_of(^^T)).append("::").append(std::meta::identifier_of(m))
                   .append(": flatten/inheritance nesting exceeds the supported depth of 8.");
                st.fail(msg.view());
                continue;
            }
            st.flatten_stack.push_back(std::meta::dealias(^^T));
            auto p = prefix; p.push_back(m);
            auto fn = std::meta::extract<collect_fn>(std::meta::substitute(^^collect_fields, { mt }));
            fn(st, p, true, via_base);
            st.flatten_stack.pop_back();
            continue;
        }

        field_meta f{};
        for (auto p : prefix) f.path[f.depth++] = p;
        f.path[f.depth++] = m;
        f.type  = std::meta::type_of(m);
        f.owner = ^^T;
        for (auto a : anns) f.annotations[f.annotation_count++] = a;
        f.codec = ^^void;
        f.default_ann = ^^void;

        auto& info = f.info;
        info.identifier = intern(std::meta::identifier_of(m));
        {
            fixed_string<256> q;
            q.append(type_name_of(^^T)).append("::").append(std::meta::identifier_of(m));
            info.qualified = intern(q.view());
        }
        if (auto n = find_annotation<name_t>(anns)) {
            info.wire_name = n->value;
            info.flags |= field_flags::explicit_name;
        } else if (rename) {
            info.wire_name = intern(apply_case(std::meta::identifier_of(m), rename->style));
            info.flags |= field_flags::renamed;
        } else {
            info.wire_name = info.identifier;
        }
        {
            std::vector<static_string> al;
            for (auto a : anns)
                if (is_annotation_of_type(a, ^^alias_t)) al.push_back(std::meta::extract<alias_t>(a).value);
            if (!al.empty()) {
                auto sp = std::define_static_array(al);
                info.alias_ptr = sp.data(); info.alias_count = sp.size();
            }
        }
        if (via_flatten) info.flags |= field_flags::flattened;
        if (via_base)    info.flags |= field_flags::inherited;
        if (std::meta::has_default_member_initializer(m)) info.flags |= field_flags::has_dmi;
        if (has_annotation<skip_serializing_t>(anns))   info.flags |= field_flags::skip_serializing;
        if (has_annotation<skip_deserializing_t>(anns)) info.flags |= field_flags::skip_deserializing;
        if (has_annotation<skip_if_null_t>(anns))    { info.flags |= field_flags::skip_if_null;    st.conditional = true; }
        if (has_annotation<skip_if_default_t>(anns)) { info.flags |= field_flags::skip_if_default; st.conditional = true; }
        if (auto d = find_annotation_template(anns, ^^default_value_t)) {
            f.default_ann = *d;
            info.flags |= field_flags::has_default;
        }
        if (auto w = find_annotation_template(anns, ^^with_t))
            f.codec = std::meta::template_arguments_of(annotation_type(*w))[0];

        bool opt_like = eval_trait(^^is_optional_like_v, std::meta::dealias(f.type));
        if (opt_like) info.flags |= field_flags::optional_like;

        // Default requiredness (§5.2): optional if optional-like, or has a default member
        // initialiser, or carries default_value. required/optional override.
        bool req = !(opt_like || has(info.flags, field_flags::has_dmi) || has(info.flags, field_flags::has_default));
        if (has_annotation<required_t>(anns)) req = true;
        if (has_annotation<optional_t>(anns)) req = false;
        if (has(info.flags, field_flags::skip_deserializing)) req = false;
        if (req) info.flags |= field_flags::required;

        // Discriminator: member-level tag wins over the enclosing type's.
        if (auto t = find_annotation<tag_t>(anns)) { info.flags |= field_flags::has_tag; info.tag_key = t->key; }
        else if (type_tag && eval_trait(^^is_variant_like_v, std::meta::dealias(f.type))) {
            info.flags |= field_flags::has_tag; info.tag_key = type_tag->key;
        }

        info.member_index = st.fields.size();
        st.fields.push_back(f);
    }
}

// Duplicate wire names, including aliases (§6.2): a compile error, never last-one-wins.
consteval void check_duplicates(build_state& st) {
    struct entry { std::string_view key; std::size_t field; bool alias; };
    std::vector<entry> entries;
    for (auto const& f : st.fields) {
        entries.push_back({ f.info.wire_name.view(), f.info.member_index, false });
        for (auto a : f.info.aliases()) entries.push_back({ a.view(), f.info.member_index, true });
    }
    for (std::size_t i = 0; i < entries.size(); ++i)
        for (std::size_t j = i + 1; j < entries.size(); ++j) {
            if (entries[i].key != entries[j].key) continue;
            auto const& a = st.fields[entries[i].field].info;
            auto const& b = st.fields[entries[j].field].info;
            fixed_string<512> msg;
            msg.append(a.qualified.view()).append(" and ").append(b.qualified.view())
               .append(" both map to the wire name \"").append(entries[i].key).append("\"");
            auto via = [&](field_info const& fi, bool alias) -> void {
                if (alias) { msg.append(" (via alias(\"").append(entries[j].key).append("\"))"); return; }
                if (has(fi.flags, field_flags::renamed))
                    msg.append(" (via rename_all)");
                else if (has(fi.flags, field_flags::explicit_name))
                    msg.append(" (via name(\"").append(fi.wire_name.view()).append("\"))");
            };
            if (entries[j].alias || entries[i].alias) via(b, true);
            else if (has(b.flags, field_flags::renamed) || has(a.flags, field_flags::renamed)) {
                msg.append(" (via rename_all)");
            } else via(b, false);
            msg.append(".");
            st.fail(msg.view());
            return;
        }
}

template<class T>
consteval build_state run_build() {
    build_state st;
    if constexpr (class_type<T>) {
        collect_fields<T>(st, {}, false, false);
        check_duplicates(st);
    }
    return st;
}

} // namespace detail

template<class T>
consteval std::span<const field_meta> build_fields() {
    auto st = detail::run_build<T>();
    return std::define_static_array(st.fields);
}

template<class T>
consteval std::span<const field_info> build_schema() {
    auto st = detail::run_build<T>();
    std::vector<field_info> v;
    for (auto const& f : st.fields) v.push_back(f.info);
    return std::define_static_array(v);
}

template<class T>
consteval type_info build_type_info() {
    auto st = detail::run_build<T>();
    type_info ti{};
    ti.name = intern(type_name_of(^^T));
    if constexpr (!class_type<T> && !std::is_enum_v<T>) return ti;
    auto tanns = detail::type_annotations_of<T>();
    ti.deny_unknown_fields = has_annotation<deny_unknown_fields_t>(tanns);
    ti.transparent = has_annotation<transparent_t>(tanns);
    if (auto t = find_annotation<tag_t>(tanns)) { ti.has_tag = true; ti.tag_key = t->key; }
    if (auto r = find_annotation<rename_all_t>(tanns)) { ti.has_rename = true; ti.rename = r->style; }
    ti.conditional_fields = st.conditional;
    ti.field_count = st.fields.size();
    if (ti.transparent) {
        std::size_t n = 0;
        for (auto const& f : st.fields) if (!f.info.skip_serializing()) ++n;
        if (st.fields.size() != 1) {
            fixed_string<256> msg;
            msg.append(ti.name.view()).append(" is declared [[=vcodec::transparent]] but has ")
               .append_int(st.fields.size()).append(" members; transparent requires exactly one.");
            st.fail(msg.view());
        }
        (void)n;
    }
    if (!st.error.empty()) ti.error = intern(st.error);
    return ti;
}

template<class T> inline constexpr std::span<const field_meta> fields_of = build_fields<std::remove_cvref_t<T>>();
template<class T> inline constexpr std::span<const field_info> schema_of = build_schema<std::remove_cvref_t<T>>();
template<class T> inline constexpr type_info type_schema_of = build_type_info<std::remove_cvref_t<T>>();

// Access the member a field describes, following its path from the root object.
template<field_meta F, std::size_t I = 0, class Obj>
constexpr auto& access(Obj& obj) noexcept {
    if constexpr (I + 1 == F.depth) return obj.[:F.path[I]:];
    else return access<F, I + 1>(obj.[:F.path[I]:]);
}

// ---- enums (§5.5) ---------------------------------------------------------------------------

template<class E>
struct enumerator_info {
    static_string        name;          // wire name: name > rename_all > identifier
    static_string        identifier;
    E                    value;
    bool                 skipped = false;
    const static_string* alias_ptr = nullptr;
    std::size_t          alias_count = 0;
    constexpr std::span<const static_string> aliases() const noexcept { return {alias_ptr, alias_count}; }
};

template<class E>
consteval std::span<const enumerator_info<E>> build_enum_schema() {
    std::vector<enumerator_info<E>> v;
    auto tanns = detail::type_annotations_of<E>();
    auto rename = find_annotation<rename_all_t>(tanns);
    for (auto e : std::meta::enumerators_of(^^E)) {
        enumerator_info<E> ei{};
        ei.identifier = intern(std::meta::identifier_of(e));
        ei.value = std::meta::extract<E>(e);
        auto anns = std::meta::annotations_of(e);
        if (auto n = find_annotation<name_t>(anns)) ei.name = n->value;
        else if (rename) ei.name = intern(apply_case(std::meta::identifier_of(e), rename->style));
        else ei.name = ei.identifier;
        ei.skipped = has_annotation<skip_t>(anns);
        std::vector<static_string> al;
        for (auto a : anns)
            if (is_annotation_of_type(a, ^^alias_t)) al.push_back(std::meta::extract<alias_t>(a).value);
        if (!al.empty()) { auto sp = std::define_static_array(al); ei.alias_ptr = sp.data(); ei.alias_count = sp.size(); }
        v.push_back(ei);
    }
    return std::define_static_array(v);
}

template<class E> inline constexpr std::span<const enumerator_info<std::remove_cvref_t<E>>> enum_schema_of = build_enum_schema<std::remove_cvref_t<E>>();

template<class E>
consteval bool enum_as_integer_of() {
    return has_annotation<as_integer_t>(detail::type_annotations_of<std::remove_cvref_t<E>>());
}
template<class E> inline constexpr bool enum_as_integer = enum_as_integer_of<E>();

template<class E>
consteval bool enum_as_text_of() {
    return has_annotation<as_text_t>(detail::type_annotations_of<std::remove_cvref_t<E>>());
}
template<class E> inline constexpr bool enum_as_text = enum_as_text_of<E>();

// The effective encoding of an enum under a format: a member-level as_integer / as_text wins,
// then the enum type's own annotation, then the format's default (§11 rule 4: defaults may
// differ, semantics may not).
template<class E, field_meta F, bool FormatDefaultInteger>
consteval bool enum_encodes_as_integer_of() {
    if (has_annotation<as_integer_t>(F.anns())) return true;
    if (has_annotation<as_text_t>(F.anns())) return false;
    if (enum_as_integer<E>) return true;
    if (enum_as_text<E>) return false;
    return FormatDefaultInteger;
}
template<class E, field_meta F, bool FormatDefaultInteger>
inline constexpr bool enum_encodes_as_integer = enum_encodes_as_integer_of<E, F, FormatDefaultInteger>();

// ---- variant alternatives (§5.3 tag) ----------------------------------------------------------
//
// An alternative is named by a type-level name("…") annotation (source or describe<A>), else
// by its identifier. Struct alternatives are internally tagged ({tag: name, ...members});
// anything else is externally tagged ({tag: name, "value": v}).

template<class A>
consteval static_string alternative_name() {
    auto tanns = detail::type_annotations_of<A>();
    if (auto n = find_annotation<name_t>(tanns)) return n->value;
    auto t = std::meta::dealias(^^A);
    if (std::meta::has_identifier(t) && !std::meta::has_template_arguments(t)) return intern(std::meta::identifier_of(t));
    return intern(qualified_type_name(t));   // std::string, std::vector<int>, ...
}

template<class V> struct variant_names_impl;
template<class... A> struct variant_names_impl<std::variant<A...>> {
    static constexpr std::array<static_string, sizeof...(A)> value{ alternative_name<A>()... };
};
template<class V> inline constexpr auto variant_names = variant_names_impl<std::remove_cvref_t<V>>::value;

inline constexpr static_string variant_value_key = intern("value");

// A struct alternative is internally tagged unless it is transparent (then it has no object
// of its own to carry the tag).
template<class A>
consteval bool internally_tagged_of() {
    if constexpr (reflectable_class<A>) return !type_schema_of<A>.transparent;
    else return false;
}
template<class A> inline constexpr bool internally_tagged = internally_tagged_of<std::remove_cvref_t<A>>();

} // namespace vcodec::core

#endif // VCODEC_CORE_SCHEMA_HPP

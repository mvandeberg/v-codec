// Test-only generic JSON value (spec §13.3): a recursive variant that exists only in the test
// tree. It is a normal user type with a codec_for specialisation built from the reader and
// sink primitives, so it is also an end-to-end check that recursive types work.
#pragma once
#include <vcodec/cbor.hpp>
#include <vcodec/json.hpp>

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace vcodec_test::dom {

struct value;
using array  = std::vector<value>;
using object = std::vector<std::pair<std::string, value>>;

using bytes = std::vector<std::byte>;
using int_object = std::vector<std::pair<std::int64_t, value>>;   // CBOR maps with integer keys
using any_object = std::vector<std::pair<value, value>>;          // CBOR maps with keys of any other kind
struct big_negative { std::uint64_t n; bool operator==(big_negative const&) const = default; };   // -1 - n, below INT64_MIN
struct simple { std::uint8_t v; bool operator==(simple const&) const = default; };                 // unassigned CBOR simple value

struct value {
    std::variant<std::nullptr_t, bool, double, std::string, array, object, bytes, std::uint64_t, std::int64_t, big_negative, simple, int_object, any_object> v;

    value() : v(nullptr) {}
    value(std::nullptr_t) : v(nullptr) {}
    value(bool b) : v(b) {}
    value(double d) : v(d) {}
    value(int i) : v(double(i)) {}
    value(std::string s) : v(std::move(s)) {}
    value(const char* s) : v(std::string(s)) {}
    value(array a) : v(std::move(a)) {}
    value(object o) : v(std::move(o)) {}
    value(bytes b) : v(std::move(b)) {}
    value(std::uint64_t u) : v(u) {}
    value(std::int64_t i) : v(i) {}
    value(big_negative b) : v(b) {}
    value(simple s) : v(s) {}
    value(int_object o) : v(std::move(o)) {}
    value(any_object o) : v(std::move(o)) {}

    // Numeric value as a double, for comparisons against JSON-derived values.
    std::optional<double> number() const {
        if (auto p = std::get_if<double>(&v)) return *p;
        if (auto p = std::get_if<std::uint64_t>(&v)) return double(*p);
        if (auto p = std::get_if<std::int64_t>(&v)) return double(*p);
        if (auto p = std::get_if<big_negative>(&v)) return -1.0 - double(p->n);
        return std::nullopt;
    }

    bool operator==(value const&) const = default;

    template<class T> bool is() const { return std::holds_alternative<T>(v); }
    template<class T> T const& as() const { return std::get<T>(v); }
};

} // namespace vcodec_test::dom

template<>
struct vcodec::codec_for<vcodec_test::dom::value, vcodec::json::format> {
    using value = vcodec_test::dom::value;

    static void encode(vcodec::core::sink auto& s, value const& v) {
        std::visit([&](auto const& x) {
            using X = std::remove_cvref_t<decltype(x)>;
            if constexpr (std::same_as<X, std::nullptr_t>) s.null();
            else if constexpr (std::same_as<X, bool>) s.boolean(x);
            else if constexpr (std::same_as<X, double>) s.real(x);
            else if constexpr (std::same_as<X, std::string>) s.text(x);
            else if constexpr (std::same_as<X, vcodec_test::dom::bytes>) s.bytes(x);
            else if constexpr (std::same_as<X, std::uint64_t>) s.uint(x);
            else if constexpr (std::same_as<X, std::int64_t>) s.sint(x);
            else if constexpr (std::same_as<X, vcodec_test::dom::big_negative>) {
                if constexpr (requires { s.negative(x.n); }) s.negative(x.n);
                else s.real(-1.0 - double(x.n));
            }
            else if constexpr (std::same_as<X, vcodec_test::dom::simple>) {
                if constexpr (requires { s.simple(x.v); }) s.simple(x.v);
                else s.null();
            }
            else if constexpr (std::same_as<X, vcodec_test::dom::int_object>) {
                s.begin_map(x.size());
                for (auto const& [k, e] : x) { s.key(k); encode(s, e); }
                s.end_map();
            }
            else if constexpr (std::same_as<X, vcodec_test::dom::any_object>) {
                s.begin_map(x.size());
                for (auto const& [k, e] : x) {
                    if constexpr (requires { s.begin_key(); }) { s.begin_key(); encode(s, k); s.end_key(); }
                    else s.key(std::string_view(vcodec::json::encode(k)));   // JSON: keys must be text
                    encode(s, e);
                }
                s.end_map();
            }
            else if constexpr (std::same_as<X, vcodec_test::dom::array>) {
                s.begin_array(x.size());
                for (auto const& e : x) encode(s, e);
                s.end_array();
            } else {
                s.begin_map(x.size());
                for (auto const& [k, e] : x) { s.key(std::string_view(k)); encode(s, e); }
                s.end_map();
            }
        }, v.v);
    }

    static vcodec::status decode(vcodec::core::reader auto& r, value& out) {
        using vcodec::core::kind;
        switch (r.peek()) {
        case kind::null: { auto st = r.expect_null(); if (!st) return st; out.v = nullptr; return {}; }
        case kind::boolean: { auto b = r.expect_boolean(); if (!b) return std::unexpected(std::move(b.error())); out.v = *b; return {}; }
        case kind::uint: case kind::sint: case kind::real: {
            auto d = r.expect_real(); if (!d) return std::unexpected(std::move(d.error())); out.v = *d; return {};
        }
        case kind::text: {
            auto t = r.expect_text(); if (!t) return std::unexpected(std::move(t.error()));
            out.v = std::string(t->text); return {};
        }
        case kind::array: {
            auto c = r.expect_array(); if (!c) return std::unexpected(std::move(c.error()));
            vcodec_test::dom::array a;
            for (;;) {
                auto more = c->next(); if (!more) return std::unexpected(std::move(more.error()));
                if (!*more) break;
                value e;
                auto st = decode(r, e);
                if (!st) { st.error().push_index(a.size()); return st; }
                a.push_back(std::move(e));
            }
            out.v = std::move(a); return {};
        }
        case kind::map: {
            auto c = r.expect_map(); if (!c) return std::unexpected(std::move(c.error()));
            vcodec_test::dom::object o;
            for (;;) {
                auto more = c->next(); if (!more) return std::unexpected(std::move(more.error()));
                if (!*more) break;
                auto k = c->key_text(); if (!k) return std::unexpected(std::move(k.error()));
                std::string key(k->text);
                value e;
                auto st = decode(r, e);
                if (!st) { st.error().push_key(key); return st; }
                o.emplace_back(std::move(key), std::move(e));
            }
            out.v = std::move(o); return {};
        }
        default: {
            // Not a value start: let the validator produce the precise syntax error.
            auto st = r.skip_value();
            if (!st) return st;
            return std::unexpected(r.error(vcodec::errc::unexpected_token, r.offset()));
        }
        }
    }
};

// CBOR: the same DOM, plus byte strings; integer map keys become their decimal text so the
// object alternative can hold them (a test-tree convenience, not a library rule).
template<>
struct vcodec::codec_for<vcodec_test::dom::value, vcodec::cbor::format> {
    using value = vcodec_test::dom::value;
    using json_codec = vcodec::codec_for<value, vcodec::json::format>;

    static void encode(vcodec::core::sink auto& s, value const& v) { json_codec::encode(s, v); }

    static vcodec::status decode(vcodec::core::reader auto& r, value& out) {
        using vcodec::core::kind;
        switch (r.peek()) {
        case kind::uint: { auto u = r.expect_uint(); if (!u) return std::unexpected(std::move(u.error())); out.v = *u; return {}; }
        case kind::sint: {
            if constexpr (requires { r.expect_nint(); }) {
                auto n = r.expect_nint(); if (!n) return std::unexpected(std::move(n.error()));
                if (*n <= std::uint64_t(INT64_MAX)) out.v = std::int64_t(-1 - std::int64_t(*n));
                else out.v = vcodec_test::dom::big_negative{ *n };
                return {};
            } else {
                auto i = r.expect_sint(); if (!i) return std::unexpected(std::move(i.error())); out.v = *i; return {};
            }
        }
        case kind::bytes: {
            auto b = r.expect_bytes(); if (!b) return std::unexpected(std::move(b.error()));
            out.v = vcodec_test::dom::bytes(b->bytes.begin(), b->bytes.end()); return {};
        }
        case kind::map: {
            // Any well-formed CBOR map: keys are decoded as values. All-text keys give an
            // object, all int64-representable integer keys an int_object, anything else an
            // any_object (test-tree convenience: the library itself only lowers text and
            // integer keys).
            auto c = r.expect_map(); if (!c) return std::unexpected(std::move(c.error()));
            std::vector<std::pair<value, value>> raw;
            for (;;) {
                auto more = c->next(); if (!more) return std::unexpected(std::move(more.error()));
                if (!*more) break;
                value k;
                auto kk = c->key_kind();
                if (kk == kind::text) { auto t = c->key_text(); if (!t) return std::unexpected(std::move(t.error())); k.v = std::string(t->text); }
                else if (kk == kind::uint) { auto u = c->key_uint(); if (!u) return std::unexpected(std::move(u.error())); k.v = *u; }
                else if (kk == kind::sint) {
                    if constexpr (requires { r.expect_nint(); }) {
                        auto n = r.expect_nint(); if (!n) return std::unexpected(std::move(n.error()));
                        if (*n <= std::uint64_t(INT64_MAX)) k.v = std::int64_t(-1 - std::int64_t(*n)); else k.v = vcodec_test::dom::big_negative{ *n };
                    } else { auto i = c->key_sint(); if (!i) return std::unexpected(std::move(i.error())); k.v = *i; }
                }
                else { auto st = decode(r, k); if (!st) return st; }
                value e;
                auto st = decode(r, e);
                if (!st) { st.error().push_key(vcodec::json::encode(k)); return st; }
                raw.emplace_back(std::move(k), std::move(e));
            }
            bool all_int = !raw.empty(), all_text = true;
            for (auto const& [k, e] : raw) {
                bool is_int = std::holds_alternative<std::int64_t>(k.v) || (std::holds_alternative<std::uint64_t>(k.v) && std::get<std::uint64_t>(k.v) <= std::uint64_t(INT64_MAX));
                if (!is_int) all_int = false;
                if (!std::holds_alternative<std::string>(k.v)) all_text = false;
            }
            if (all_text) {
                vcodec_test::dom::object o;
                for (auto& [k, e] : raw) o.emplace_back(std::move(std::get<std::string>(k.v)), std::move(e));
                out.v = std::move(o);
            } else if (all_int) {
                vcodec_test::dom::int_object io;
                for (auto& [k, e] : raw) io.emplace_back(std::holds_alternative<std::int64_t>(k.v) ? std::get<std::int64_t>(k.v) : std::int64_t(std::get<std::uint64_t>(k.v)), std::move(e));
                out.v = std::move(io);
            } else {
                out.v = vcodec_test::dom::any_object(std::move(raw));
            }
            return {};
        }
        case kind::array: {
            auto c = r.expect_array(); if (!c) return std::unexpected(std::move(c.error()));
            vcodec_test::dom::array a;
            for (;;) {
                auto more = c->next(); if (!more) return std::unexpected(std::move(more.error()));
                if (!*more) break;
                value e;
                auto st = decode(r, e);
                if (!st) { st.error().push_index(a.size()); return st; }
                a.push_back(std::move(e));
            }
            out.v = std::move(a); return {};
        }
        default:
            if constexpr (requires { r.expect_simple(); }) {
                if (auto sv = r.expect_simple()) { out.v = vcodec_test::dom::simple{ *sv }; return {}; }
            }
            return json_codec::decode(r, out);
        }
    }
};

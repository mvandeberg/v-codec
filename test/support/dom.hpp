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

struct value {
    std::variant<std::nullptr_t, bool, double, std::string, array, object, bytes> v;

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
        case kind::bytes: {
            auto b = r.expect_bytes(); if (!b) return std::unexpected(std::move(b.error()));
            out.v = vcodec_test::dom::bytes(b->bytes.begin(), b->bytes.end()); return {};
        }
        case kind::map: {
            auto c = r.expect_map(); if (!c) return std::unexpected(std::move(c.error()));
            vcodec_test::dom::object o;
            for (;;) {
                auto more = c->next(); if (!more) return std::unexpected(std::move(more.error()));
                if (!*more) break;
                std::string key;
                auto kk = c->key_kind();
                if (kk == kind::uint) { auto k = c->key_uint(); if (!k) return std::unexpected(std::move(k.error())); key = std::to_string(*k); }
                else if (kk == kind::sint) { auto k = c->key_sint(); if (!k) return std::unexpected(std::move(k.error())); key = std::to_string(*k); }
                else { auto k = c->key_text(); if (!k) return std::unexpected(std::move(k.error())); key = std::string(k->text); }
                value e;
                auto st = decode(r, e);
                if (!st) { st.error().push_key(key); return st; }
                o.emplace_back(std::move(key), std::move(e));
            }
            out.v = std::move(o); return {};
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
            return json_codec::decode(r, out);
        }
    }
};

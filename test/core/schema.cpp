// M1: schema extraction is correct for nested, inherited, private, flattened and annotated
// types. No JSON anywhere in this file — the include tree makes that a build error.
#include <vcodec/core/schema.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vc = vcodec;
using vc::core::field_flags;
using vc::core::has;

namespace {

struct Base { int base_id = 0; };
struct Inner { int x; [[=vc::name("why")]] int y = 2; };

struct [[=vc::rename_all(vc::case_::kebab), =vc::deny_unknown_fields, =vc::tag("type")]]
Outer : Base {
    std::string host_name;
    [[=vc::alias("p"), =vc::alias("prt")]] std::uint16_t port = 8080;
    [[=vc::skip_if_null]] std::optional<std::string> cert_path;
    [[=vc::flatten]] Inner inner;
    [[=vc::default_value(42)]] int retries;
    [[=vc::default_value("abc")]] std::string label;
    [[=vc::skip]] int internal;
    [[=vc::skip_serializing]] int write_only;
    [[=vc::skip_deserializing]] int read_only;
    std::variant<int, double> payload;
    [[=vc::optional]] int not_required;
private:
    [[=vc::required]] int secret = 1;
public:
    int& secret_ref() { return secret; }
};

enum class [[=vc::rename_all(vc::case_::screaming_snake)]] Color {
    bright_red [[=vc::name("bright-red")]],
    green,
    internal_only [[=vc::skip]],
};

struct Third { int x; int y; };

struct Dup { int host; [[=vc::name("host")]] int hostname; };
struct [[=vc::rename_all(vc::case_::kebab)]] DupRename { int host_name; [[=vc::name("host-name")]] int other; };
struct DupAlias { int a; [[=vc::alias("a")]] int b; };
struct Single { [[=vc::transparent]] int v; };
struct [[=vc::transparent]] Transparent { int v; };
struct [[=vc::transparent]] BadTransparent { int v; int w; };
struct FlatNonStruct { [[=vc::flatten]] int v; };
struct Cyc1; struct Cyc2 { [[=vc::flatten]] Cyc1* p; };

template<class T>
const vc::core::field_info* field(std::string_view identifier) {
    for (auto const& f : vc::core::schema_of<T>)
        if (f.identifier.view() == identifier) return &f;
    return nullptr;
}

} // namespace

template<> struct vc::describe<Third> {
    static constexpr auto annotations = vc::type_annotations(vc::deny_unknown_fields);
    static constexpr auto members = vc::members(
        vc::member("x", vc::name("X"), vc::skip_if_default));
};

TEST_CASE("schema: field order is bases first, then members, with flatten spliced in place") {
    constexpr auto s = vc::core::schema_of<Outer>;
    std::vector<std::string_view> ids;
    for (auto const& f : s) ids.push_back(f.identifier.view());
    CHECK(ids == std::vector<std::string_view>{
        "base_id", "host_name", "port", "cert_path", "x", "y", "retries", "label",
        "write_only", "read_only", "payload", "not_required", "secret" });
    for (std::size_t i = 0; i < s.size(); ++i) CHECK(s[i].member_index == i);
    CHECK(vc::core::type_schema_of<Outer>.ok());
    CHECK(vc::core::type_schema_of<Outer>.field_count == s.size());
}

TEST_CASE("schema: skip removes the member from the table entirely") {
    CHECK(field<Outer>("internal") == nullptr);
}

TEST_CASE("schema: wire names resolve name > rename_all > identifier") {
    CHECK(field<Outer>("host_name")->wire_name == "host-name");   // rename_all(kebab)
    CHECK(field<Outer>("cert_path")->wire_name == "cert-path");
    CHECK(field<Outer>("y")->wire_name == "why");                  // explicit name wins
    CHECK(field<Outer>("x")->wire_name == "x");                    // Inner has no rename_all
    CHECK(field<Outer>("base_id")->wire_name == "base_id");        // Base has no rename_all
    CHECK(has(field<Outer>("host_name")->flags, field_flags::renamed));
    CHECK(has(field<Outer>("y")->flags, field_flags::explicit_name));
}

TEST_CASE("schema: qualified names name the declaring class") {
    CHECK(field<Outer>("x")->qualified == "Inner::x");
    CHECK(field<Outer>("base_id")->qualified == "Base::base_id");
    CHECK(field<Outer>("host_name")->qualified == "Outer::host_name");
}

TEST_CASE("schema: aliases are recorded in order") {
    auto const* p = field<Outer>("port");
    REQUIRE(p->alias_count == 2);
    CHECK(p->aliases()[0] == "p");
    CHECK(p->aliases()[1] == "prt");
}

TEST_CASE("schema: default requiredness rule (§5.2)") {
    CHECK(field<Outer>("host_name")->required());          // plain member: required
    CHECK_FALSE(field<Outer>("port")->required());         // default member initialiser
    CHECK_FALSE(field<Outer>("cert_path")->required());    // optional-like
    CHECK_FALSE(field<Outer>("retries")->required());      // default_value
    CHECK_FALSE(field<Outer>("label")->required());
    CHECK(field<Outer>("x")->required());                  // flattened, plain
    CHECK_FALSE(field<Outer>("y")->required());            // flattened, has dmi
    CHECK_FALSE(field<Outer>("base_id")->required());      // inherited, has dmi
    CHECK(field<Outer>("secret")->required());             // [[=required]] overrides the dmi
    CHECK_FALSE(field<Outer>("not_required")->required()); // [[=optional]] overrides
    CHECK_FALSE(field<Outer>("read_only")->required());    // skip_deserializing is never required
}

TEST_CASE("schema: flags") {
    CHECK(has(field<Outer>("x")->flags, field_flags::flattened));
    CHECK(has(field<Outer>("base_id")->flags, field_flags::inherited));
    CHECK(has(field<Outer>("cert_path")->flags, field_flags::skip_if_null));
    CHECK(has(field<Outer>("cert_path")->flags, field_flags::optional_like));
    CHECK(has(field<Outer>("write_only")->flags, field_flags::skip_serializing));
    CHECK(has(field<Outer>("read_only")->flags, field_flags::skip_deserializing));
    CHECK(has(field<Outer>("retries")->flags, field_flags::has_default));
    CHECK(has(field<Outer>("port")->flags, field_flags::has_dmi));
    CHECK(has(field<Outer>("payload")->flags, field_flags::has_tag));
    CHECK(field<Outer>("payload")->tag_key == "type");
    CHECK_FALSE(has(field<Outer>("port")->flags, field_flags::has_tag));
}

TEST_CASE("schema: type-level information") {
    constexpr auto& ti = vc::core::type_schema_of<Outer>;
    CHECK(ti.name == "Outer");
    CHECK(ti.deny_unknown_fields);
    CHECK(ti.has_tag);
    CHECK(ti.tag_key == "type");
    CHECK(ti.has_rename);
    CHECK(ti.rename == vc::case_::kebab);
    CHECK(ti.conditional_fields);     // skip_if_null present
    CHECK_FALSE(ti.transparent);
    CHECK_FALSE(vc::core::type_schema_of<Inner>.conditional_fields);
}

TEST_CASE("schema: access<> follows the member path through bases and flattens") {
    Outer o{};
    template for (constexpr auto f : vc::core::fields_of<Outer>) {
        constexpr auto& rt = vc::core::schema_of<Outer>[f.info.member_index];
        auto& ref = vc::core::access<f>(o);
        if constexpr (rt.identifier == "x") { ref = 7; CHECK(o.inner.x == 7); }
        if constexpr (rt.identifier == "base_id") { ref = 9; CHECK(o.base_id == 9); }
        if constexpr (rt.identifier == "secret") { ref = 11; CHECK(o.secret_ref() == 11); }
        if constexpr (f.default_ann != ^^void) {
            constexpr auto dv = std::meta::extract<typename [:vc::core::annotation_type(f.default_ann):]>(f.default_ann).value;
            if constexpr (std::same_as<std::remove_cvref_t<decltype(dv)>, vc::core::static_string>) ref = std::string(dv.view());
            else ref = dv;
        }
    }
    CHECK(o.retries == 42);
    CHECK(o.label == "abc");
}

TEST_CASE("schema: fields_of and schema_of are indexed identically") {
    template for (constexpr auto f : vc::core::fields_of<Outer>) {
        constexpr auto& rt = vc::core::schema_of<Outer>[f.info.member_index];
        static_assert(rt.identifier == f.info.identifier);
        static_assert(rt.wire_name == f.info.wire_name);
    }
}

TEST_CASE("schema: enums") {
    constexpr auto e = vc::core::enum_schema_of<Color>;
    REQUIRE(e.size() == 3);
    CHECK(e[0].name == "bright-red");
    CHECK(e[0].identifier == "bright_red");
    CHECK(e[0].value == Color::bright_red);
    CHECK(e[1].name == "GREEN");               // rename_all(screaming_snake)
    CHECK(e[2].skipped);
    CHECK_FALSE(vc::core::enum_as_integer<Color>);
}

TEST_CASE("schema: describe<T> supplies annotations for a type you do not own") {
    CHECK(field<Third>("x")->wire_name == "X");
    CHECK(has(field<Third>("x")->flags, field_flags::skip_if_default));
    CHECK(field<Third>("y")->wire_name == "y");
    CHECK(vc::core::type_schema_of<Third>.deny_unknown_fields);
    CHECK(vc::core::type_schema_of<Third>.conditional_fields);
}

TEST_CASE("schema: duplicate wire names are a schema error naming both members and the cause") {
    constexpr auto& d = vc::core::type_schema_of<Dup>;
    CHECK_FALSE(d.ok());
    CHECK(d.error.view() == "Dup::host and Dup::hostname both map to the wire name \"host\" (via name(\"host\")).");
    CHECK(vc::core::type_schema_of<DupRename>.error.view().find("via rename_all") != std::string_view::npos);
    CHECK(vc::core::type_schema_of<DupAlias>.error.view().find("via alias(\"a\")") != std::string_view::npos);
}

TEST_CASE("schema: transparent requires exactly one member") {
    CHECK(vc::core::type_schema_of<Transparent>.transparent);
    CHECK(vc::core::type_schema_of<Transparent>.ok());
    CHECK_FALSE(vc::core::type_schema_of<BadTransparent>.ok());
    CHECK(vc::core::type_schema_of<BadTransparent>.error.view().find("transparent") != std::string_view::npos);
}

TEST_CASE("schema: flatten of a non-struct is a schema error") {
    CHECK_FALSE(vc::core::type_schema_of<FlatNonStruct>.ok());
    CHECK(vc::core::type_schema_of<FlatNonStruct>.error.view().find("is not a struct") != std::string_view::npos);
}

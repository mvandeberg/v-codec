#include <vcodec/core/annotations.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace vc = vcodec;

namespace {
consteval std::string cased(std::string_view id, vc::case_ c) {
    auto v = vc::core::apply_case(id, c);
    return std::string(v.begin(), v.end());
}
}

TEST_CASE("annotations: rename_all case transforms") {
    STATIC_CHECK(cased("host_name", vc::case_::kebab) == "host-name");
    STATIC_CHECK(cased("host_name", vc::case_::camel) == "hostName");
    STATIC_CHECK(cased("host_name", vc::case_::pascal) == "HostName");
    STATIC_CHECK(cased("host_name", vc::case_::snake) == "host_name");
    STATIC_CHECK(cased("host_name", vc::case_::screaming_snake) == "HOST_NAME");
    STATIC_CHECK(cased("hostName", vc::case_::snake) == "host_name");
    STATIC_CHECK(cased("hostName", vc::case_::kebab) == "host-name");
    STATIC_CHECK(cased("HostName", vc::case_::snake) == "host_name");
    STATIC_CHECK(cased("allowed_origins", vc::case_::camel) == "allowedOrigins");
    STATIC_CHECK(cased("port", vc::case_::pascal) == "Port");
    STATIC_CHECK(cased("__x__", vc::case_::kebab) == "x");
    STATIC_CHECK(cased("tls_v1_2", vc::case_::kebab) == "tls-v1-2");
}

TEST_CASE("annotations: string payloads are interned into static storage") {
    constexpr auto n = vc::name("first-name");
    STATIC_CHECK(n.value.view() == "first-name");
    STATIC_CHECK(std::meta::is_structural_type(^^vc::name_t));
    STATIC_CHECK(std::meta::is_structural_type(^^vc::alias_t));
    STATIC_CHECK(std::meta::is_structural_type(^^vc::tag_t));
    STATIC_CHECK(std::meta::is_structural_type(^^vc::rename_all_t));
    STATIC_CHECK(std::meta::is_structural_type(^^vc::default_value_t<int>));
    STATIC_CHECK(std::meta::is_structural_type(^^vc::default_value_t<vc::core::static_string>));
    constexpr auto d = vc::default_value("abc");
    STATIC_CHECK(d.value.view() == "abc");
    constexpr auto e = vc::default_value(3.5);
    STATIC_CHECK(e.value == 3.5);
}

TEST_CASE("annotations: fixed_string builder") {
    constexpr auto f = [] { vc::core::fixed_string<16> s; s.append("ab").append('c').append_int(-42).append_int(7u); return s; }();
    STATIC_CHECK(f.view() == "abc-427");
    constexpr auto t = [] { vc::core::fixed_string<4> s; s.append("abcdef"); return s; }();
    STATIC_CHECK(t.view() == "abcd");
    STATIC_CHECK(t.truncated());
}

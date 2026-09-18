// M7: the audit accepts the §7 type set and produces the §10 messages, checked as strings.
#include <vcodec/json.hpp>

#include "types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <span>
#include <variant>

using namespace vcodec_test;
namespace js = vcodec::json;

namespace {
struct Pool { int size = 0; void* handle; };
struct Database { std::optional<std::vector<Pool>> pool; };
struct Config { Database database; };
struct Claims { std::vector<std::byte> nonce; };
struct A { int a = 0; }; struct B { int b = 0; };
struct Event { std::variant<A, B> payload; };
struct [[=vc::tag("type")]] TaggedEvent { std::variant<A, B> payload; };
struct DupServer { std::string host; [[=vc::name("host")]] std::string hostname; };
struct Index { std::map<int, std::string> by_id; };
struct OkIndex { [[=js::stringify_keys]] std::map<int, std::string> by_id; };
struct Row { const char* label = ""; };
struct Frame { std::span<const int> samples; };
struct Borrowing { std::string_view v; int n = 0; };
struct Node { std::vector<Node> kids; int v = 0; };            // recursive
struct Tree { std::unique_ptr<Tree> left, right; int v = 0; }; // recursive through pointers
struct BadDefault { [[=vc::default_value(3)]] std::vector<int> v; };
struct OkDefaults { [[=vc::default_value(3)]] std::optional<int> o; [[=vc::default_value("x")]] std::string s; [[=vc::default_value(2.5)]] float f; [[=vc::default_value(Color::green)]] Color c; };

std::string text(vc::core::message const& m) { return std::string(m.view()); }
}

TEST_CASE("audit: the §7 type set is encodable and decodable") {
    STATIC_CHECK(js::encodable<Kitchen>);
    STATIC_CHECK(js::decodable<Kitchen>);
    STATIC_CHECK(js::encodable<Server>);
    STATIC_CHECK(js::decodable<Server>);
    STATIC_CHECK(js::encodable<Shape>);
    STATIC_CHECK(js::decodable<Shape>);
    STATIC_CHECK(js::encodable<TaggedEvent>);
    STATIC_CHECK(js::encodable<OkIndex>);
    STATIC_CHECK(js::decodable<OkIndex>);
    STATIC_CHECK(js::encodable<Node>);
    STATIC_CHECK(js::decodable<Node>);
    STATIC_CHECK(js::encodable<Tree>);
    STATIC_CHECK(js::decodable<Tree>);
    STATIC_CHECK(js::encodable<Row>);          // const char* encodes
    STATIC_CHECK(js::encodable<Frame>);        // span encodes
    STATIC_CHECK(js::encodable<int>);
    STATIC_CHECK(js::decodable<std::vector<std::map<std::string, std::optional<Point>>>>);
    STATIC_CHECK(js::decodable<OkDefaults>);
    STATIC_CHECK(js::encodable<ring<int>>);
    STATIC_CHECK(js::decodable<ring<int>>);
}

TEST_CASE("audit: borrowing is detected") {
    STATIC_CHECK(js::borrows<Borrowing>);
    STATIC_CHECK(js::borrows<std::vector<std::string_view>>);
    STATIC_CHECK_FALSE(js::borrows<Server>);
    STATIC_CHECK_FALSE(js::borrows<std::string>);
}

TEST_CASE("§10: no codec, with the full member path") {
    STATIC_CHECK_FALSE(js::encodable<Config>);
    CHECK(text(js::explain_encode<Config>()) ==
        "Config::database::pool::handle (type void*)\n"
        "  has no codec. Provide vcodec::codec_for<void*>, mark the member\n"
        "  [[=vcodec::skip]], or give it [[=vcodec::with<YourCodec>]].");
}

TEST_CASE("§10: byte-like without a JSON lowering") {
    STATIC_CHECK_FALSE(js::encodable<Claims>);
    CHECK(text(js::explain_encode<Claims>()) ==
        "Claims::nonce (std::vector<std::byte>)\n"
        "  is byte-like but has no JSON lowering.\n"
        "  Add [[=vcodec::json::bytes(vcodec::json::base64)]], or mark it [[=vcodec::skip]].");
}

TEST_CASE("§10: variant without a discriminator") {
    STATIC_CHECK_FALSE(js::encodable<Event>);
    STATIC_CHECK_FALSE(js::decodable<Event>);
    CHECK(text(js::explain_encode<Event>()) ==
        "Event::payload (std::variant<A, B>)\n"
        "  needs a discriminator. Add [[=vcodec::tag(\"type\")]] to Event.");
}

TEST_CASE("§10: duplicate wire names") {
    STATIC_CHECK_FALSE(js::encodable<DupServer>);
    CHECK(text(js::explain_encode<DupServer>()) ==
        "DupServer::host and DupServer::hostname both map to the wire name \"host\" (via name(\"host\")).");
}

TEST_CASE("§10: non-text map keys") {
    STATIC_CHECK_FALSE(js::encodable<Index>);
    CHECK(text(js::explain_encode<Index>()) ==
        "Index::by_id (std::map<int, std::string>)\n"
        "  has integer keys, which JSON cannot represent.\n"
        "  Add [[=vcodec::json::stringify_keys]], use string keys, or mark it [[=vcodec::skip]].");
}

TEST_CASE("§10: encode-only members on the decode side") {
    STATIC_CHECK_FALSE(js::decodable<Row>);
    STATIC_CHECK_FALSE(js::decodable<Frame>);
    CHECK(text(js::explain_decode<Row>()).starts_with("Row::label (const char*)\n  is encode-only and cannot be decoded into. Use std::string"));
    CHECK(text(js::explain_decode<Frame>()).starts_with("Frame::samples (std::span<const int>)\n  is encode-only"));
}

TEST_CASE("§10: annotation misuse") {
    STATIC_CHECK_FALSE(js::decodable<BadDefault>);
    CHECK(text(js::explain_decode<BadDefault>()) ==
        "BadDefault::v (std::vector<int>)\n  has a default_value whose type does not convert to the member's type.");
    struct S { [[=vc::skip_if_null]] int port = 0; };
    STATIC_CHECK_FALSE(js::encodable<S>);
    CHECK(text(js::explain_encode<S>()).find("is declared [[=vcodec::skip_if_null]] but is not optional-like") != std::string::npos);
}

namespace {
struct Wide { std::wstring w; };
struct WideChar { char16_t c = 0; };
struct NoDefault { explicit NoDefault(int) {} [[=vc::skip_if_default]] int v = 0; };
struct Deleter { void operator()(int*) const {} };
struct CustomDeleter { std::unique_ptr<int, Deleter> p; };
}

TEST_CASE("§10: wide strings and code units have no codec instead of becoming integer arrays") {
    STATIC_CHECK_FALSE(js::encodable<Wide>);
    STATIC_CHECK_FALSE(js::encodable<WideChar>);
    STATIC_CHECK_FALSE(js::encodable<std::u16string>);
    CHECK(text(js::explain_encode<Wide>()).starts_with("Wide::w (type std::wstring)\n  has no codec."));
    CHECK(text(js::explain_encode<WideChar>()).starts_with("WideChar::c (type char16_t)\n  has no codec."));
}

TEST_CASE("§10: audit gaps closed — skip_if_default parent, custom deleter") {
    STATIC_CHECK_FALSE(js::encodable<NoDefault>);
    CHECK(text(js::explain_encode<NoDefault>()).find("is not default-constructible") != std::string::npos);
    STATIC_CHECK_FALSE(js::encodable<CustomDeleter>);
    CHECK(text(js::explain_encode<CustomDeleter>()).find("has no codec") != std::string::npos);
}

TEST_CASE("§10: a passing audit has an empty message") {
    CHECK(text(js::explain_encode<Kitchen>()).empty());
    CHECK(text(js::explain_decode<Kitchen>()).empty());
}

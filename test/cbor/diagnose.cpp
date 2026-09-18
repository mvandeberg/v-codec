// C6: the CBOR audit accepts the §7 set plus CBOR-only types and produces the §10 messages.
#include <vcodec/vcodec.hpp>

#include "types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <list>

using namespace vcodec_test;
namespace cb = vcodec::cbor;

namespace {
struct Claims { [[=cb::key(2)]] std::string issuer; [[=cb::key(2)]] std::string subject; };
struct Ext { int extra = 0; };
struct [[=cb::integer_keys]] Flat { int a = 0; [[=vcodec::flatten]] Ext ext; };
struct [[=cb::integer_keys]] Keyed { int p = 0; int q = 0; };
struct FlattensKeyed { int a = 0; [[=vcodec::flatten]] Keyed k; };
struct Timed { std::chrono::sys_seconds t{}; };
struct Frame { [[=cb::typed_array]] std::list<double> samples; };
struct [[=cb::deterministic]] Stream { [[=cb::indefinite]] std::vector<int> events; };
struct Event { [[=cb::tag(1), =cb::tag(0)]] std::chrono::sys_seconds when; };
struct Binary { std::vector<std::byte> blob; std::span<const std::byte> view; };
struct [[=vcodec::transparent]] Wrapped { [[=cb::key(1)]] int v = 0; };
struct Inner { int a = 0; };
struct [[=cb::self_describe]] SD { int a = 0; };
struct HoldsSD { SD sd; };
struct Ok { [[=cb::key(1)]] std::string a; [[=cb::integer_keys]] struct Inner2 { int x = 0; int y = 0; } in; std::chrono::sys_seconds t{}; [[=cb::typed_array]] std::vector<float> f; std::map<int, int> m; };
std::string text(vcodec::core::message const& m) { return std::string(m.view()); }
}

TEST_CASE("cbor audit: accepted types") {
    STATIC_CHECK(cb::encodable<Kitchen>);
    STATIC_CHECK(cb::decodable<Kitchen>);
    STATIC_CHECK(cb::encodable<Server>);
    STATIC_CHECK(cb::encodable<Ok>);
    STATIC_CHECK(cb::decodable<Ok>);
    STATIC_CHECK(cb::encodable<Binary>);
    STATIC_CHECK(cb::decodable<Binary>);              // spans borrow byte strings in CBOR
    STATIC_CHECK(cb::borrows<Binary>);
    STATIC_CHECK_FALSE(vcodec::json::decodable<Binary>);
    STATIC_CHECK(vcodec::cbor_only<Binary>);
    STATIC_CHECK(cb::encodable<std::map<int, int>>);    // integer keys native
    STATIC_CHECK_FALSE(vcodec::json::encodable<std::map<int, int>>);
    STATIC_CHECK(cb::encodable<SD>);
}

TEST_CASE("cbor audit: chrono is CBOR-only; integer_keys does not survive a flatten") {
    STATIC_CHECK(cb::encodable<Timed>);
    STATIC_CHECK_FALSE(vcodec::json::encodable<Timed>);
    STATIC_CHECK(vcodec::cbor_only<Timed>);
    CHECK(text(vcodec::json::explain_encode<Timed>()).starts_with("Timed::t (type std::chrono::time_point<std::chrono::system_clock, std::chrono::duration<long int>>)\n  has no codec."));
    STATIC_CHECK_FALSE(cb::encodable<FlattensKeyed>);
    CHECK(text(cb::explain_encode<FlattensKeyed>()).starts_with("FlattensKeyed flattens Keyed, which is declared [[=cbor::integer_keys]];"));
}

TEST_CASE("cbor audit: §10 messages") {
    STATIC_CHECK_FALSE(cb::encodable<Claims>);
    CHECK(text(cb::explain_encode<Claims>()) == "Claims::issuer and Claims::subject both map to the CBOR key 2 (via cbor::key(2)).");
    STATIC_CHECK_FALSE(cb::encodable<Flat>);
    CHECK(text(cb::explain_encode<Flat>()).starts_with("Flat (declared [[=cbor::integer_keys]]) has a flattened or inherited member Ext::extra;"));
    STATIC_CHECK_FALSE(cb::encodable<Frame>);
    CHECK(text(cb::explain_encode<Frame>()) == "Frame::samples (std::list<double>)\n  is declared [[=cbor::typed_array]] but is not a contiguous range of a\n  fixed-width integer or floating type.");
    STATIC_CHECK_FALSE(cb::encodable<Stream>);
    CHECK(text(cb::explain_encode<Stream>()).find("deterministic encoding forbids indefinite lengths") != std::string::npos);
    STATIC_CHECK_FALSE(cb::encodable<Event>);
    CHECK(text(cb::explain_encode<Event>()) == "Event::when (std::chrono::time_point<std::chrono::system_clock, std::chrono::duration<long int>>)\n  carries [[=cbor::tag(1)]] and [[=cbor::tag(0)]]; a time point takes one of the two.");
    STATIC_CHECK_FALSE(cb::encodable<Wrapped>);
    CHECK(text(cb::explain_encode<Wrapped>()).find("is [[=vcodec::transparent]] and has no map") != std::string::npos);
    STATIC_CHECK_FALSE(cb::encodable<HoldsSD>);
    CHECK(text(cb::explain_encode<HoldsSD>()).find("self-describe tag applies to a top-level value only") != std::string::npos);
    // JSON messages are unchanged and still fire for a CBOR-only type
    STATIC_CHECK_FALSE(vcodec::json::encodable<Binary>);
    CHECK(text(vcodec::json::explain_encode<Binary>()).starts_with("Binary::blob (std::vector<std::byte>)\n  is byte-like but has no JSON lowering."));
}

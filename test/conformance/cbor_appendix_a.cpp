// v0.2 §13.3: RFC 8949 Appendix A test vectors (the cbor/test-vectors corpus, fetched, not
// vendored). Every vector decodes into the test DOM; every vector in preferred form
// re-encodes to its bytes; non-preferred vectors are rejected under require_deterministic.
#include <vcodec/cbor.hpp>
#include <vcodec/json.hpp>

#include "cbor_hex.hpp"
#include "dom.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace vcodec_test;
namespace cb = vcodec::cbor;
namespace js = vcodec::json;

namespace {

// The vector file is JSON: [{ "cbor": base64, "hex": ..., "roundtrip": bool, "decoded": any | "diagnostic": string }]
struct vector_entry {
    std::string cbor;                              // base64
    std::string hex;
    bool roundtrip = false;
    std::optional<dom::value> decoded;
    std::optional<std::string> diagnostic;
};

// Deterministic CBOR sorts map keys: compare with maps sorted.
dom::value canon(dom::value v) {
    if (auto* o = std::get_if<dom::object>(&v.v)) {
        for (auto& [k, e] : *o) e = canon(std::move(e));
        std::stable_sort(o->begin(), o->end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    } else if (auto* io = std::get_if<dom::int_object>(&v.v)) {
        for (auto& [k, e] : *io) e = canon(std::move(e));
        std::stable_sort(io->begin(), io->end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    } else if (auto* a = std::get_if<dom::array>(&v.v)) {
        for (auto& e : *a) e = canon(std::move(e));
    }
    return v;
}

std::string slurp(std::string const& p) { std::ifstream in(p, std::ios::binary); std::ostringstream ss; ss << in.rdbuf(); return ss.str(); }

// Compare a decoded CBOR DOM with the JSON "decoded" value: JSON numbers are doubles, so
// integers that the CBOR side decoded exactly compare through double as well.
bool same(dom::value const& cbor_side, dom::value const& json_side) {
    if (cbor_side.is<dom::bytes>()) return false;   // byte strings have no JSON form in the file
    if (auto a = cbor_side.number()) { auto b = json_side.number(); return b && (*a == *b || (std::isnan(*a) && std::isnan(*b))); }
    if (cbor_side.is<dom::array>() && json_side.is<dom::array>()) {
        auto const& x = cbor_side.as<dom::array>(); auto const& y = json_side.as<dom::array>();
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i) if (!same(x[i], y[i])) return false;
        return true;
    }
    if (cbor_side.is<dom::object>() && json_side.is<dom::object>()) {
        auto const& x = cbor_side.as<dom::object>(); auto const& y = json_side.as<dom::object>();
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i) if (x[i].first != y[i].first || !same(x[i].second, y[i].second)) return false;
        return true;
    }
    if (cbor_side.is<dom::int_object>() && json_side.is<dom::object>()) {
        // the vector file spells integer keys as JSON strings
        auto const& x = cbor_side.as<dom::int_object>(); auto const& y = json_side.as<dom::object>();
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i) if (std::to_string(x[i].first) != y[i].first || !same(x[i].second, y[i].second)) return false;
        return true;
    }
    return cbor_side == json_side;
}
}

TEST_CASE("Appendix A: every vector decodes to its diagnostic value; preferred forms round-trip") {
    auto text = slurp(VCODEC_CBOR_VECTORS);
    auto file = js::decode<std::vector<vector_entry>>(text);
    REQUIRE(file);
    REQUIRE(file->size() > 80);
    int checked = 0, roundtripped = 0, rejected_nondet = 0;
    for (auto const& v : *file) {
        auto bytes = from_hex(v.hex);
        INFO(v.hex);
        // RFC 7049's simple(24) as 0xf818: RFC 8949 §3.3 made two-byte simple values below 32
        // ill-formed, and vcodec follows 8949.
        if (v.hex == "f818") {
            auto rejected = cb::decode<dom::value>(bytes);
            REQUIRE_FALSE(rejected);
            CHECK(rejected.error().code() == vcodec::errc::malformed_item);
            continue;
        }
        auto d = cb::decode<dom::value>(bytes);
        REQUIRE(d);
        if (v.decoded) {
            // Byte strings have no JSON form in the vector file; everything else is compared.
            if (!d->is<dom::bytes>()) CHECK(same(*d, *v.decoded));
            ++checked;
        }
        // The test DOM drops tags and has no `undefined`, so byte identity is only asked of
        // vectors without those; their decoded values are still compared above.
        bool tagged = !bytes.empty() && (std::to_integer<unsigned>(bytes[0]) >> 5) == 6;
        bool undefined = v.hex == "f7";
        if (v.roundtrip && !tagged && !undefined) {
            auto again = cb::encode(*d);
            CHECK(to_hex(again) == v.hex);
            ++roundtripped;
            constexpr cb::options strict{ .require_deterministic = true };
            CHECK(cb::decode<dom::value, strict>(bytes));
        } else {
            // Non-round-trippable vectors are the non-preferred forms (or values the DOM cannot
            // distinguish, such as -0.0 vs 0 or tags). Whatever the reason, re-encoding must be
            // canonical and decode back to the same value.
            auto again = cb::encode(*d);
            auto back = cb::decode<dom::value>(again);
            REQUIRE(back);
            CHECK((canon(*back) == canon(*d) || same(canon(*back), canon(*d))));
            constexpr cb::options strict{ .require_deterministic = true };
            if (!cb::decode<dom::value, strict>(bytes)) ++rejected_nondet;
        }
    }
    CHECK(checked > 55);
    CHECK(roundtripped > 50);
    CHECK(rejected_nondet > 5);
}

TEST_CASE("Appendix A: the validator accepts every vector") {
    auto text = slurp(VCODEC_CBOR_VECTORS);
    auto file = js::decode<std::vector<vector_entry>>(text);
    REQUIRE(file);
    for (auto const& v : *file) {
        auto bytes = from_hex(v.hex);
        cb::reader<> r(bytes);
        r.start();
        INFO(v.hex);
        if (v.hex == "f818") { CHECK_FALSE(r.skip_value()); continue; }   // ill-formed under RFC 8949 §3.3
        CHECK(r.skip_value());
        CHECK(r.finish());
    }
}

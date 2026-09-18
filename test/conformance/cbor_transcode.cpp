// v0.2 §11 rule 6: transcoding through the test DOM is lossless for the JSON-representable
// subset — every JSONTestSuite y_ document → dom → CBOR → dom is equal and re-encodes to the
// same JSON.
#include <vcodec/vcodec.hpp>

#include "dom.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace cb = vcodec::cbor;
namespace js = vcodec::json;

namespace {
// Deterministic CBOR sorts object keys, so compare documents with their objects sorted.
vcodec_test::dom::value canon(vcodec_test::dom::value v) {
    if (auto* o = std::get_if<vcodec_test::dom::object>(&v.v)) {
        for (auto& [k, e] : *o) e = canon(std::move(e));
        std::stable_sort(o->begin(), o->end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    } else if (auto* a = std::get_if<vcodec_test::dom::array>(&v.v)) {
        for (auto& e : *a) e = canon(std::move(e));
    }
    return v;
}
}

TEST_CASE("transcoding: JSONTestSuite y_ documents survive JSON → CBOR → JSON") {
    fs::path dir = VCODEC_JSONTESTSUITE_DIR;
    int checked = 0, duplicates = 0;
    for (auto const& e : fs::directory_iterator(dir)) {
        auto name = e.path().filename().string();
        if (!name.starts_with("y_")) continue;
        std::ifstream in(e.path(), std::ios::binary); std::ostringstream ss; ss << in.rdbuf();
        auto text = ss.str();
        auto v = js::decode<vcodec_test::dom::value>(text);
        REQUIRE(v);
        std::vector<std::byte> bytes;
        try { bytes = cb::encode(*v); }
        catch (vcodec::encode_error const& e) {
            // y_object_duplicated_key: JSON permits repeated keys, deterministic CBOR does not.
            CHECK(e.code() == vcodec::errc::duplicate_key);
            ++duplicates;
            continue;
        }
        auto back = cb::decode<vcodec_test::dom::value>(bytes);
        INFO(name);
        REQUIRE(back);
        CHECK(canon(*back) == canon(*v));
        CHECK(js::encode(canon(*back)) == js::encode(canon(*v)));
        // the CBOR form is deterministic: decoding and re-encoding reproduces the bytes
        CHECK(cb::encode(*back) == bytes);
        ++checked;
    }
    CHECK(checked > 90);
    CHECK(duplicates == 2);   // y_object_duplicated_key and y_object_duplicated_key_and_value
}

// v0.2 §13.3: the committed hostile corpus. Each .cbor has a sidecar .expect naming the
// errc and byte offset the validator must report (or "accept"); the typed DOM path must agree.
#include <vcodec/cbor.hpp>

#include "dom.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace cb = vcodec::cbor;

namespace {
std::vector<std::byte> slurp(fs::path const& p) {
    std::ifstream in(p, std::ios::binary);
    std::vector<char> c((std::istreambuf_iterator<char>(in)), {});
    return std::vector<std::byte>(reinterpret_cast<std::byte*>(c.data()), reinterpret_cast<std::byte*>(c.data() + c.size()));
}
std::string expect_of(fs::path const& p) { std::ifstream in(p); std::string s; std::getline(in, s); return s; }
}

TEST_CASE("hostile corpus: each file is rejected with the named code at the named offset, or accepted") {
    fs::path dir = VCODEC_CBOR_HOSTILE_DIR;
    REQUIRE(fs::is_directory(dir));
    int files = 0;
    for (auto const& e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".cbor") continue;
        ++files;
        auto bytes = slurp(e.path());
        auto expect = expect_of(fs::path(e.path()).replace_extension(".expect"));
        INFO(e.path().filename().string() << " expecting: " << expect);

        cb::reader<> r(bytes);
        r.start();
        auto skip = r.skip_value();
        if (skip) skip = r.finish();
        auto typed = cb::decode<vcodec_test::dom::value>(bytes);

        if (expect == "accept") {
            CHECK(skip);
            CHECK(typed);
            continue;
        }
        std::istringstream in(expect);
        std::string code; std::size_t offset = 0;
        in >> code >> offset;
        REQUIRE_FALSE(skip);
        CHECK(vcodec::to_string(skip.error().code()) == code);
        CHECK(skip.error().offset() == offset);
        REQUIRE_FALSE(typed);
        CHECK(vcodec::to_string(typed.error().code()) == code);
        // The rendered form must be well-formed for every case.
        auto rendered = vcodec::render_hex(typed.error(), bytes);
        CHECK(rendered.starts_with("error: "));
    }
    CHECK(files >= 45);
}

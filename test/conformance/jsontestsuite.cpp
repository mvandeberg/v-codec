// §13.3 conformance: every y_ file must be accepted and every n_ file rejected, on both the
// skip path (the generic validator) and the typed path (decode into the test DOM). Every i_
// file's disposition is recorded and compared against the committed i_dispositions.txt, so a
// change in behaviour on implementation-defined input is a reviewed diff, not a surprise.
#include <vcodec/json.hpp>

#include "dom.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
namespace js = vcodec::json;

namespace {

std::string slurp(fs::path const& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf();
    return ss.str();
}

bool skip_accepts(std::string_view text) {
    js::reader<> r(text);
    auto st = r.skip_value();
    return st && r.finish();
}

bool typed_accepts(std::string_view text) {
    return js::decode<vcodec_test::dom::value>(text).has_value();
}

std::map<std::string, std::string> read_dispositions(fs::path const& p) {
    std::map<std::string, std::string> out;
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto sp = line.find(' ');
        if (sp == std::string::npos) continue;
        out[line.substr(0, sp)] = line.substr(sp + 1);
    }
    return out;
}

} // namespace

TEST_CASE("JSONTestSuite: y_ accepted and n_ rejected on both paths; i_ dispositions recorded") {
    fs::path dir = VCODEC_JSONTESTSUITE_DIR;
    REQUIRE(fs::is_directory(dir));

    std::vector<fs::path> files;
    for (auto const& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    REQUIRE(files.size() > 250);

    std::map<std::string, std::string> observed;
    int y = 0, n = 0, i = 0;
    for (auto const& f : files) {
        std::string name = f.filename().string();
        std::string text = slurp(f);
        bool skip = skip_accepts(text);
        bool typed = typed_accepts(text);
        INFO(name);
        // The two paths must never disagree: skip_value is the same grammar the typed
        // parser uses.
        CHECK(skip == typed);
        if (name.starts_with("y_")) { ++y; CHECK(skip); CHECK(typed); }
        else if (name.starts_with("n_")) { ++n; CHECK_FALSE(skip); CHECK_FALSE(typed); }
        else if (name.starts_with("i_")) { ++i; observed[name] = skip ? "accept" : "reject"; }
    }
    CHECK(y > 90);
    CHECK(n > 180);
    CHECK(i > 30);

    // Write what we observed, then compare with the committed dispositions.
    {
        std::ofstream out(VCODEC_I_DISPOSITIONS_OUT);
        out << "# JSONTestSuite i_ (implementation-defined) files: vcodec's disposition on both paths.\n";
        out << "# Regenerate by copying the .observed.txt from the build tree after reviewing the diff.\n";
        for (auto const& [k, v] : observed) out << k << ' ' << v << '\n';
    }
    auto expected = read_dispositions(VCODEC_I_DISPOSITIONS);
    INFO("committed dispositions: " VCODEC_I_DISPOSITIONS "  observed: " VCODEC_I_DISPOSITIONS_OUT);
    REQUIRE_FALSE(expected.empty());
    for (auto const& [k, v] : observed) {
        INFO(k);
        auto it = expected.find(k);
        REQUIRE(it != expected.end());
        CHECK(it->second == v);
    }
    CHECK(expected.size() == observed.size());
}

TEST_CASE("JSONTestSuite: the DOM round-trips what it accepts") {
    fs::path dir = VCODEC_JSONTESTSUITE_DIR;
    int checked = 0;
    for (auto const& e : fs::directory_iterator(dir)) {
        auto name = e.path().filename().string();
        if (!name.starts_with("y_")) continue;
        auto text = slurp(e.path());
        auto v = js::decode<vcodec_test::dom::value>(text);
        REQUIRE(v);
        auto again = js::encode(*v);
        auto back = js::decode<vcodec_test::dom::value>(again);
        INFO(name << ": " << again);
        REQUIRE(back);
        CHECK(*back == *v);
        // Our own output is canonical for our own parser.
        CHECK(js::encode(*back) == again);
        ++checked;
    }
    CHECK(checked > 90);
}

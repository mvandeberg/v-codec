// Shared benchmark corpus (spec §13.7): four shapes, one struct definition each, used by
// every library under test. The structs are plain aggregates so that vcodec and Glaze
// reflect them automatically; nlohmann needs the macros below; simdjson's adapters live in
// simdjson_adapters.hpp because they depend on simdjson's headers.
//
// Shapes:
//   small config     — SmallConfig: 8 mixed scalar/string members
//   array-of-structs — std::vector<Record>, 10,000 records with a vector<string> each
//   deeply nested    — Node: a tree of depth 8, branching 3 (3280 nodes)
//   string-heavy     — Document: a 2 KB body plus 200 long paragraphs with escapes
//
// Generation is deterministic (fixed seed) so runs are comparable across machines.
#pragma once

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace bench {

struct SmallConfig {
    std::string   name;
    std::string   host;
    std::uint16_t port = 0;
    int           timeout_ms = 0;
    int           retries = 0;
    bool          enable_tls = false;
    double        ratio = 0;
    std::string   log_level;
};

struct Record {
    std::uint64_t            id = 0;
    std::string              name;
    std::string              email;
    bool                     active = false;
    double                   score = 0;
    std::vector<std::string> tags;
};

struct Node {
    std::string       name;
    int               value = 0;
    std::vector<Node> children;
};

struct Document {
    std::string              title;
    std::string              body;
    std::vector<std::string> paragraphs;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SmallConfig, name, host, port, timeout_ms, retries, enable_tls, ratio, log_level)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Record, id, name, email, active, score, tags)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Node, name, value, children)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Document, title, body, paragraphs)

// ---- generators ----

inline SmallConfig make_small_config() {
    return SmallConfig{
        .name = "payments-gateway",
        .host = "gateway.internal.example.com",
        .port = 8443,
        .timeout_ms = 1500,
        .retries = 3,
        .enable_tls = true,
        .ratio = 0.75,
        .log_level = "info",
    };
}

inline std::vector<Record> make_records(std::size_t n = 10'000) {
    static constexpr std::string_view first[] = {"ada", "grace", "linus", "edsger", "barbara", "ken", "dennis", "margaret"};
    static constexpr std::string_view last[]  = {"lovelace", "hopper", "torvalds", "dijkstra", "liskov", "thompson", "ritchie", "hamilton"};
    static constexpr std::string_view tag_pool[] = {"admin", "beta", "billing", "eu-west", "legacy", "premium", "trial", "verified"};
    std::mt19937_64 rng(0x5eed'c0de'0001ull);
    std::uniform_int_distribution<int> pick(0, 7), ntags(0, 4), pct(0, 99);
    std::uniform_real_distribution<double> score(0.0, 100.0);
    std::vector<Record> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        Record r;
        r.id = 1'000'000 + i;
        std::string f(first[pick(rng)]), l(last[pick(rng)]);
        r.name = f + " " + l;
        r.email = f + "." + l + "+" + std::to_string(i) + "@example.org";
        r.active = pct(rng) < 80;
        r.score = std::round(score(rng) * 1000.0) / 1000.0;   // 3 decimals, like real data
        int k = ntags(rng);
        for (int t = 0; t < k; ++t) r.tags.emplace_back(tag_pool[pick(rng)]);
        out.push_back(std::move(r));
    }
    return out;
}

inline Node make_tree(int depth = 8, int branching = 3) {
    std::mt19937_64 rng(0x5eed'c0de'0002ull);
    std::uniform_int_distribution<int> val(-1'000'000, 1'000'000);
    int counter = 0;
    auto build = [&](auto& self, int level) -> Node {
        Node n;
        n.name = "node-" + std::to_string(level) + "-" + std::to_string(counter++);
        n.value = val(rng);
        if (level < depth) {
            n.children.reserve(static_cast<std::size_t>(branching));
            for (int i = 0; i < branching; ++i) n.children.push_back(self(self, level + 1));
        }
        return n;
    };
    return build(build, 1);
}

inline Document make_document(std::size_t paragraphs = 200) {
    static constexpr std::string_view words[] = {
        "lorem", "ipsum", "dolor", "sit", "amet", "consectetur", "adipiscing", "elit",
        "sed", "do", "eiusmod", "tempor", "incididunt", "ut", "labore", "et", "dolore",
        "magna", "aliqua", "quotation", "backslash", "tab", "newline", "caf\xc3\xa9", "na\xc3\xafve",
    };
    // Roughly one escape per 25 words: quotes, backslashes, tabs, newlines, a control char.
    static constexpr std::string_view escapes[] = {"\"quoted\"", "C:\\path\\to\\file", "\tindented", "line\nbreak", "\x01"};
    std::mt19937_64 rng(0x5eed'c0de'0003ull);
    std::uniform_int_distribution<int> w(0, 24), e(0, 4), len(40, 60);
    auto sentence = [&](int n) {
        std::string s;
        for (int i = 0; i < n; ++i) {
            if (i) s += ' ';
            if (i % 25 == 12) s += escapes[e(rng)]; else s += words[w(rng)];
        }
        return s;
    };
    Document d;
    d.title = "Benchmark \"string-heavy\" document \xe2\x80\x94 with escapes";
    d.body = sentence(350);
    d.paragraphs.reserve(paragraphs);
    for (std::size_t i = 0; i < paragraphs; ++i) d.paragraphs.push_back(sentence(len(rng)));
    return d;
}

// The shared decode input for a shape: nlohmann's compact dump, so no library is parsing
// its own preferred formatting.
template<class T>
std::string reference_json(T const& v) { return nlohmann::json(v).dump(); }

} // namespace bench

// A libFuzzer-compatible driver for compilers without libFuzzer (GCC). It replays every file
// in the given corpus directories, then mutates them for -max_total_time seconds (or -runs=N
// iterations) with byte-level and JSON-token-level mutations. Crashes and sanitizer reports
// abort the process, which is what the ctest job checks. Not a substitute for libFuzzer's
// coverage guidance; it exists so the harnesses run under ASan+UBSan on the primary toolchain.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

struct rng {
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    std::uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    std::size_t below(std::size_t n) { return n ? next() % n : 0; }
};

const char* const tokens[] = {
    "{", "}", "[", "]", ",", ":", "\"", "\\", "\\u", "\\ud800", "null", "true", "false", "-", "0", "1e", "1e400", ".", "E+",
    "\"kind\"", "\"type\"", "\"value\"", "\"x\"", "\"y\"", "\"host-name\"", "\"radius\"", "\xff", "\xed\xa0\x80", "\n", " ", "\t", "\x00",
};

void mutate(std::vector<std::uint8_t>& d, rng& r, std::vector<std::vector<std::uint8_t>> const& seeds) {
    int n = 1 + int(r.below(4));
    for (int k = 0; k < n; ++k) {
        switch (r.below(8)) {
        case 0: if (!d.empty()) d[r.below(d.size())] = std::uint8_t(r.next()); break;
        case 1: if (!d.empty()) d[r.below(d.size())] ^= std::uint8_t(1u << r.below(8)); break;
        case 2: if (!d.empty()) d.erase(d.begin() + std::ptrdiff_t(r.below(d.size()))); break;
        case 3: d.insert(d.begin() + std::ptrdiff_t(r.below(d.size() + 1)), std::uint8_t(r.next())); break;
        case 4: {
            const char* t = tokens[r.below(sizeof(tokens) / sizeof(*tokens))];
            std::size_t len = std::strlen(t); if (len == 0) len = 1;
            d.insert(d.begin() + std::ptrdiff_t(r.below(d.size() + 1)), t, t + len);
            break;
        }
        case 5: if (d.size() > 1) { std::size_t a = r.below(d.size()), b = r.below(d.size()); d.erase(d.begin() + std::ptrdiff_t(std::min(a, b)), d.begin() + std::ptrdiff_t(std::max(a, b))); } break;
        case 6: if (!seeds.empty()) {
            auto const& s = seeds[r.below(seeds.size())];
            if (!s.empty()) {
                std::size_t from = r.below(s.size()), len = r.below(s.size() - from + 1);
                d.insert(d.begin() + std::ptrdiff_t(r.below(d.size() + 1)), s.begin() + std::ptrdiff_t(from), s.begin() + std::ptrdiff_t(from + len));
            }
            break;
        }
        case 7: { std::size_t reps = 1 + r.below(64); auto copy = d; for (std::size_t i = 0; i < reps && d.size() < 1 << 16; ++i) d.insert(d.end(), copy.begin(), copy.end()); break; }
        }
        if (d.size() > (1u << 16)) d.resize(1u << 16);
    }
}

} // namespace

int main(int argc, char** argv) {
    double max_time = 0; long runs = -1;
    std::vector<std::string> dirs;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.starts_with("-max_total_time=")) max_time = std::atof(a.c_str() + 16);
        else if (a.starts_with("-runs=")) runs = std::atol(a.c_str() + 6);
        else if (a.starts_with("-")) { /* ignore other libFuzzer flags */ }
        else dirs.push_back(a);
    }
    std::vector<std::vector<std::uint8_t>> seeds;
    for (auto const& d : dirs) {
        if (!std::filesystem::exists(d)) continue;
        if (std::filesystem::is_regular_file(d)) {
            std::ifstream in(d, std::ios::binary);
            seeds.emplace_back(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
            continue;
        }
        for (auto const& e : std::filesystem::recursive_directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            std::ifstream in(e.path(), std::ios::binary);
            seeds.emplace_back(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
        }
    }
    std::fprintf(stderr, "standalone fuzz driver: %zu seeds, max_total_time=%.0fs runs=%ld\n", seeds.size(), max_time, runs);
    long executed = 0;
    for (auto const& s : seeds) { LLVMFuzzerTestOneInput(s.data(), s.size()); ++executed; }
    if (seeds.empty()) { std::uint8_t z = 0; LLVMFuzzerTestOneInput(&z, 0); ++executed; }

    rng r;
    auto start = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
    std::vector<std::uint8_t> buf;
    while ((runs < 0 || executed < runs) && (max_time <= 0 ? executed < 10000 : elapsed() < max_time)) {
        buf = seeds.empty() ? std::vector<std::uint8_t>{} : seeds[r.below(seeds.size())];
        mutate(buf, r, seeds);
        LLVMFuzzerTestOneInput(buf.data(), buf.size());
        ++executed;
    }
    std::fprintf(stderr, "standalone fuzz driver: %ld executions in %.1fs, no crash\n", executed, elapsed());
    return 0;
}

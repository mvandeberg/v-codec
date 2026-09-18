// Encode benchmarks (spec §13.7): each corpus shape × {vcodec, Glaze, nlohmann}. simdjson
// has no struct serialiser in v3.13.0 — see simdjson_adapters.hpp.
//
// Each library gets its idiomatic fast path. vcodec appears twice: `encode` (returns a
// fresh std::string, the documented entry point) and `encode_append` (reuses a buffer,
// the like-for-like comparison with Glaze's write_json into a reused buffer). nlohmann has
// no way to skip the DOM, so `json(v).dump()` is what a user pays.
#include "corpus.hpp"

#include <vcodec/json.hpp>
#include <glaze/glaze.hpp>
#include <nlohmann/json.hpp>
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using bench::SmallConfig, bench::Record, bench::Node, bench::Document;

template<class T> T const& corpus();
template<> SmallConfig const& corpus<SmallConfig>()                 { static auto const v = bench::make_small_config(); return v; }
template<> std::vector<Record> const& corpus<std::vector<Record>>() { static auto const v = bench::make_records(); return v; }
template<> Node const& corpus<Node>()                               { static auto const v = bench::make_tree(); return v; }
template<> Document const& corpus<Document>()                       { static auto const v = bench::make_document(); return v; }

void finish(benchmark::State& st, std::size_t bytes_per_op) {
    st.SetBytesProcessed(static_cast<std::int64_t>(st.iterations()) * static_cast<std::int64_t>(bytes_per_op));
}

template<class T>
void vcodec_encode(benchmark::State& st) {
    T const& v = corpus<T>();
    std::size_t n = 0;
    for (auto _ : st) {
        std::string out = vcodec::json::encode(v);
        benchmark::DoNotOptimize(out.data());
        n = out.size();
    }
    finish(st, n);
}

template<class T>
void vcodec_encode_append(benchmark::State& st) {
    T const& v = corpus<T>();
    std::string out;
    for (auto _ : st) {
        out.clear();
        vcodec::json::encode_append(v, out);
        benchmark::DoNotOptimize(out.data());
    }
    finish(st, out.size());
}

template<class T>
void glaze_encode(benchmark::State& st) {
    T const& v = corpus<T>();
    std::string out;
    for (auto _ : st) {
        if (auto ec = glz::write_json(v, out)) { st.SkipWithError("glaze write_json failed"); return; }
        benchmark::DoNotOptimize(out.data());
    }
    finish(st, out.size());
}

template<class T>
void nlohmann_encode(benchmark::State& st) {
    T const& v = corpus<T>();
    std::size_t n = 0;
    for (auto _ : st) {
        std::string out = nlohmann::json(v).dump();
        benchmark::DoNotOptimize(out.data());
        n = out.size();
    }
    finish(st, n);
}

#define BENCH_SHAPE(shape, T)                                                            \
    BENCHMARK(vcodec_encode<T>)->Name("encode/" shape "/vcodec");                        \
    BENCHMARK(vcodec_encode_append<T>)->Name("encode/" shape "/vcodec_append");          \
    BENCHMARK(glaze_encode<T>)->Name("encode/" shape "/glaze");                          \
    BENCHMARK(nlohmann_encode<T>)->Name("encode/" shape "/nlohmann");

BENCH_SHAPE("small_config",  SmallConfig)
BENCH_SHAPE("large_array",   std::vector<Record>)
BENCH_SHAPE("deep_nested",   Node)
BENCH_SHAPE("string_heavy",  Document)

#undef BENCH_SHAPE

} // namespace

// Decode benchmarks (spec §13.7): each corpus shape × {vcodec, Glaze, nlohmann, simdjson}.
//
// Every library parses the same text (bench::reference_json, nlohmann's compact dump) into
// the same struct. Each case decodes once before timing and skips with an error if the
// library rejects the input, so a silent mismatch cannot masquerade as a fast result.
#include "corpus.hpp"
#include "simdjson_adapters.hpp"

#include <vcodec/json.hpp>
#include <glaze/glaze.hpp>
#include <nlohmann/json.hpp>
#include <simdjson.h>
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using bench::SmallConfig, bench::Record, bench::Node, bench::Document;

template<class T> std::string const& input();
template<> std::string const& input<SmallConfig>()         { static auto const s = bench::reference_json(bench::make_small_config()); return s; }
template<> std::string const& input<std::vector<Record>>() { static auto const s = bench::reference_json(bench::make_records()); return s; }
template<> std::string const& input<Node>()                { static auto const s = bench::reference_json(bench::make_tree()); return s; }
template<> std::string const& input<Document>()            { static auto const s = bench::reference_json(bench::make_document()); return s; }

void finish(benchmark::State& st, std::size_t bytes_per_op) {
    st.SetBytesProcessed(static_cast<std::int64_t>(st.iterations()) * static_cast<std::int64_t>(bytes_per_op));
}

template<class T>
void vcodec_decode(benchmark::State& st) {
    std::string const& in = input<T>();
    if (!vcodec::json::decode<T>(in)) { st.SkipWithError("vcodec rejected the input"); return; }
    for (auto _ : st) {
        vcodec::result<T> r = vcodec::json::decode<T>(in);
        benchmark::DoNotOptimize(r);
    }
    finish(st, in.size());
}

template<class T>
void glaze_decode(benchmark::State& st) {
    std::string const& in = input<T>();
    { T probe{}; if (glz::read_json(probe, in)) { st.SkipWithError("glaze rejected the input"); return; } }
    for (auto _ : st) {
        T out{};
        auto ec = glz::read_json(out, in);
        benchmark::DoNotOptimize(ec);
        benchmark::DoNotOptimize(out);
    }
    finish(st, in.size());
}

template<class T>
void nlohmann_decode(benchmark::State& st) {
    std::string const& in = input<T>();
    for (auto _ : st) {
        T out = nlohmann::json::parse(in).template get<T>();
        benchmark::DoNotOptimize(out);
    }
    finish(st, in.size());
}

template<class T>
void simdjson_decode(benchmark::State& st) {
    simdjson::padded_string const in(input<T>());
    simdjson::ondemand::parser parser;
    { T probe{}; if (bench::simdjson_decode(parser, in, probe)) { st.SkipWithError("simdjson rejected the input"); return; } }
    for (auto _ : st) {
        T out{};
        auto ec = bench::simdjson_decode(parser, in, out);
        benchmark::DoNotOptimize(ec);
        benchmark::DoNotOptimize(out);
    }
    finish(st, in.size());
}

#define BENCH_SHAPE(shape, T)                                                    \
    BENCHMARK(vcodec_decode<T>)->Name("decode/" shape "/vcodec");                \
    BENCHMARK(glaze_decode<T>)->Name("decode/" shape "/glaze");                  \
    BENCHMARK(nlohmann_decode<T>)->Name("decode/" shape "/nlohmann");            \
    BENCHMARK(simdjson_decode<T>)->Name("decode/" shape "/simdjson");

BENCH_SHAPE("small_config",  SmallConfig)
BENCH_SHAPE("large_array",   std::vector<Record>)
BENCH_SHAPE("deep_nested",   Node)
BENCH_SHAPE("string_heavy",  Document)

#undef BENCH_SHAPE

} // namespace

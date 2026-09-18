// CBOR decode benchmarks (spec §13.7): each shape × {vcodec, vcodec with
// require_deterministic, Glaze, QCBOR}.
//
// The input is vcodec's deterministic encoding of each shape. Glaze cannot read integer keys,
// so for the two protocol shapes its input is vcodec's deterministic encoding of the text-key
// twin (cbor_corpus.hpp); every other shape's bytes are identical for all four rows. Before
// timing, every library decodes the input once and the result is compared value-for-value
// with the corpus; a mismatch or rejection aborts the run rather than posting a number.
#include "cbor_corpus.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace {

using bench::SmallConfig, bench::Record, bench::Node, bench::Document, bench::CwtClaims, bench::CoseKeySet, bench::MetricMap;

template<class T> T const& corpus();
template<> SmallConfig const& corpus<SmallConfig>()                 { static auto const v = bench::make_small_config(); return v; }
template<> std::vector<Record> const& corpus<std::vector<Record>>() { static auto const v = bench::make_records(); return v; }
template<> Node const& corpus<Node>()                               { static auto const v = bench::make_tree(); return v; }
template<> Document const& corpus<Document>()                       { static auto const v = bench::make_document(); return v; }
template<> CwtClaims const& corpus<CwtClaims>()                     { static auto const v = bench::make_cwt_claims(); return v; }
template<> CoseKeySet const& corpus<CoseKeySet>()                   { static auto const v = bench::make_cose_keys(); return v; }
template<> MetricMap const& corpus<MetricMap>()                     { static auto const v = bench::make_metric_map(); return v; }

constexpr vcodec::cbor::options strict{ .require_deterministic = true };

[[noreturn]] void fail(char const* shape, char const* what) {
    std::fprintf(stderr, "bench_cbor_decode: %s: %s\n", shape, what);
    std::exit(1);
}

struct inputs {
    std::vector<std::byte> bytes;   // vcodec deterministic encoding of the shape
    std::string            glaze;   // the same for Glaze's form of the shape (a std::string, Glaze's buffer type)
};

// Built once per shape; verifies every library against the corpus value while doing so.
template<class T>
inputs const& input(char const* shape) {
    static inputs const in = [shape] {
        T const& v = corpus<T>();
        inputs r;
        r.bytes = vcodec::cbor::encode(v);
        auto const g = vcodec::cbor::encode(bench::to_text(v));
        r.glaze.assign(reinterpret_cast<char const*>(g.data()), g.size());

        if (auto d = vcodec::cbor::decode<T>(r.bytes); !d || !(*d == v)) fail(shape, "vcodec does not round-trip its own output");
        if (auto d = vcodec::cbor::decode<T, strict>(r.bytes); !d || !(*d == v)) fail(shape, "vcodec (require_deterministic) rejects its own deterministic output");
        { bench::text_form_t<T> g_out{}; if (glz::read_cbor(g_out, r.glaze)) fail(shape, "Glaze rejected the input"); if (!(bench::from_text(g_out) == v)) fail(shape, "Glaze decoded different values"); }
        { T q{}; if (!bench::qc::decode(r.bytes, q)) fail(shape, "QCBOR rejected the input"); if (!(q == v)) fail(shape, "QCBOR decoded different values"); }
        return r;
    }();
    return in;
}

void finish(benchmark::State& st, std::size_t bytes_per_op) {
    st.SetBytesProcessed(static_cast<std::int64_t>(st.iterations()) * static_cast<std::int64_t>(bytes_per_op));
    st.counters["payload_bytes"] = static_cast<double>(bytes_per_op);
}

template<class T, vcodec::cbor::options Opts = {}>
void vcodec_decode(benchmark::State& st, char const* shape) {
    std::span<std::byte const> const in = input<T>(shape).bytes;
    for (auto _ : st) {
        vcodec::result<T> r = vcodec::cbor::decode<T, Opts>(in);
        benchmark::DoNotOptimize(r);
    }
    finish(st, in.size());
}

template<class T>
void glaze_decode(benchmark::State& st, char const* shape) {
    std::string const& in = input<T>(shape).glaze;
    for (auto _ : st) {
        bench::text_form_t<T> out{};
        auto ec = glz::read_cbor(out, in);
        benchmark::DoNotOptimize(ec);
        benchmark::DoNotOptimize(out);
    }
    finish(st, in.size());
}

template<class T>
void qcbor_decode(benchmark::State& st, char const* shape) {
    std::span<std::byte const> const in = input<T>(shape).bytes;
    for (auto _ : st) {
        T out{};
        bool ok = bench::qc::decode(in, out);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(out);
    }
    finish(st, in.size());
}

#define BENCH_SHAPE(shape, T)                                                                                \
    BENCHMARK_CAPTURE((vcodec_decode<T>), shape, shape)->Name("cbor_decode/" shape "/vcodec");               \
    BENCHMARK_CAPTURE((vcodec_decode<T, strict>), shape, shape)->Name("cbor_decode/" shape "/vcodec_strict"); \
    BENCHMARK_CAPTURE((glaze_decode<T>), shape, shape)->Name("cbor_decode/" shape "/glaze");                 \
    BENCHMARK_CAPTURE((qcbor_decode<T>), shape, shape)->Name("cbor_decode/" shape "/qcbor");

BENCH_SHAPE("small_config",  SmallConfig)
BENCH_SHAPE("large_array",   std::vector<Record>)
BENCH_SHAPE("deep_nested",   Node)
BENCH_SHAPE("string_heavy",  Document)
BENCH_SHAPE("cwt_claims",    CwtClaims)
BENCH_SHAPE("cose_key_set",  CoseKeySet)
BENCH_SHAPE("metric_map",    MetricMap)

#undef BENCH_SHAPE

} // namespace

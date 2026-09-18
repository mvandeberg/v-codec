// CBOR encode benchmarks (spec §13.7): each shape × {vcodec deterministic, vcodec
// non-deterministic, vcodec deterministic into a reused buffer, Glaze, QCBOR}.
//
// vcodec's default is RFC 8949 §4.2.1 deterministic encoding; `loose` turns it off so the
// cost of key sorting is a separate row. `vcodec_append` reuses the output buffer, the
// like-for-like comparison with Glaze (reused std::string) and QCBOR (caller-owned buffer
// sized once). Glaze benchmarks the text-key form of the two protocol shapes — see
// cbor_corpus.hpp. Before timing, each library's output is decoded with vcodec and compared
// with the corpus value, so a wrong hand-written QCBOR encoder cannot post a fast number.
#include "cbor_corpus.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

template<class T> bench::text_form_t<T> const& glaze_corpus() { static auto const v = bench::to_text(corpus<T>()); return v; }

constexpr vcodec::cbor::options loose{ .deterministic = false };

[[noreturn]] void fail(char const* shape, char const* what) {
    std::fprintf(stderr, "bench_cbor_encode: %s: %s\n", shape, what);
    std::exit(1);
}

// Once per shape: every library's encoding decodes (with vcodec) back to the corpus value.
template<class T>
void verify(char const* shape) {
    static bool const done = [shape] {
        T const& v = corpus<T>();
        auto check = [&](std::vector<std::byte> const& bytes, char const* who) {
            auto r = vcodec::cbor::decode<T>(bytes);
            if (!r) fail(shape, who);
            if (!(*r == v)) fail(shape, who);
        };
        check(vcodec::cbor::encode(v), "vcodec deterministic output does not round-trip");
        check(vcodec::cbor::encode<loose>(v), "vcodec non-deterministic output does not round-trip");
        {
            std::size_t const cap = bench::qc::encoded_size(v);
            std::vector<std::byte> buf(cap);
            std::size_t const n = bench::qc::encode(v, buf);
            if (n == 0) fail(shape, "QCBOR encoder failed");
            buf.resize(n);
            check(buf, "QCBOR output does not decode to the corpus value");
        }
        {
            std::string out;
            if (glz::write_cbor(glaze_corpus<T>(), out)) fail(shape, "Glaze write_cbor failed");
            std::vector<std::byte> bytes(out.size());
            std::memcpy(bytes.data(), out.data(), out.size());
            auto r = vcodec::cbor::decode<bench::text_form_t<T>>(bytes);
            if (!r) fail(shape, "Glaze output does not decode with vcodec");
            if (!(bench::from_text(*r) == v)) fail(shape, "Glaze output does not decode to the corpus value");
        }
        return true;
    }();
    (void)done;
}

void finish(benchmark::State& st, std::size_t bytes_per_op) {
    st.SetBytesProcessed(static_cast<std::int64_t>(st.iterations()) * static_cast<std::int64_t>(bytes_per_op));
    st.counters["payload_bytes"] = static_cast<double>(bytes_per_op);
}

template<class T, vcodec::cbor::options Opts = {}>
void vcodec_encode(benchmark::State& st, char const* shape) {
    verify<T>(shape);
    T const& v = corpus<T>();
    std::size_t n = 0;
    for (auto _ : st) {
        std::vector<std::byte> out = vcodec::cbor::encode<Opts>(v);
        benchmark::DoNotOptimize(out.data());
        n = out.size();
    }
    finish(st, n);
}

template<class T>
void vcodec_encode_append(benchmark::State& st, char const* shape) {
    verify<T>(shape);
    T const& v = corpus<T>();
    std::vector<std::byte> out;
    for (auto _ : st) {
        out.clear();
        vcodec::cbor::encode_append(v, out);
        benchmark::DoNotOptimize(out.data());
    }
    finish(st, out.size());
}

template<class T>
void glaze_encode(benchmark::State& st, char const* shape) {
    verify<T>(shape);
    auto const& v = glaze_corpus<T>();
    std::string out;
    for (auto _ : st) {
        if (auto ec = glz::write_cbor(v, out)) { st.SkipWithError("glaze write_cbor failed"); return; }
        benchmark::DoNotOptimize(out.data());
    }
    finish(st, out.size());
}

template<class T>
void qcbor_encode(benchmark::State& st, char const* shape) {
    verify<T>(shape);
    T const& v = corpus<T>();
    std::vector<std::byte> buf(bench::qc::encoded_size(v));
    std::size_t n = 0;
    for (auto _ : st) {
        n = bench::qc::encode(v, buf);
        if (n == 0) { st.SkipWithError("QCBOR encode failed"); return; }
        benchmark::DoNotOptimize(buf.data());
    }
    finish(st, n);
}

#define BENCH_SHAPE(shape, T)                                                                              \
    BENCHMARK_CAPTURE((vcodec_encode<T>), shape, shape)->Name("cbor_encode/" shape "/vcodec");             \
    BENCHMARK_CAPTURE((vcodec_encode<T, loose>), shape, shape)->Name("cbor_encode/" shape "/vcodec_loose"); \
    BENCHMARK_CAPTURE((vcodec_encode_append<T>), shape, shape)->Name("cbor_encode/" shape "/vcodec_append"); \
    BENCHMARK_CAPTURE((glaze_encode<T>), shape, shape)->Name("cbor_encode/" shape "/glaze");               \
    BENCHMARK_CAPTURE((qcbor_encode<T>), shape, shape)->Name("cbor_encode/" shape "/qcbor");

BENCH_SHAPE("small_config",  SmallConfig)
BENCH_SHAPE("large_array",   std::vector<Record>)
BENCH_SHAPE("deep_nested",   Node)
BENCH_SHAPE("string_heavy",  Document)
BENCH_SHAPE("cwt_claims",    CwtClaims)
BENCH_SHAPE("cose_key_set",  CoseKeySet)
BENCH_SHAPE("metric_map",    MetricMap)

#undef BENCH_SHAPE

} // namespace

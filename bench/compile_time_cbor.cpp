// CBOR compile-time budget subject (spec v0.2 §13.7): the same 50-member struct and 5-deep
// nest as compile_time.cpp, encoded and decoded with vcodec::cbor instead of vcodec::json.
// CompileTimeBudget.cmake times the compilation of this file against
// compile_time_cbor_baseline.txt; the bench_compile_time_cbor target also builds it as a
// program so the measured code is known to link and run. The struct definitions are
// duplicated rather than shared through a header so each TU is a self-contained measurement.
#include <vcodec/cbor.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

// 50 members across the common scalar, string, optional and container kinds.
struct Wide {
    int                  m00{};
    std::string          m01{};
    double               m02{};
    bool                 m03{};
    std::int64_t         m04{};
    std::uint32_t        m05{};
    std::optional<int>   m06{};
    std::vector<int>     m07{};
    std::string          m08{};
    float                m09{};
    int                  m10{};
    std::string          m11{};
    double               m12{};
    bool                 m13{};
    std::int64_t         m14{};
    std::uint32_t        m15{};
    std::optional<int>   m16{};
    std::vector<int>     m17{};
    std::string          m18{};
    float                m19{};
    int                  m20{};
    std::string          m21{};
    double               m22{};
    bool                 m23{};
    std::int64_t         m24{};
    std::uint32_t        m25{};
    std::optional<int>   m26{};
    std::vector<int>     m27{};
    std::string          m28{};
    float                m29{};
    int                  m30{};
    std::string          m31{};
    double               m32{};
    bool                 m33{};
    std::int64_t         m34{};
    std::uint32_t        m35{};
    std::optional<int>   m36{};
    std::vector<int>     m37{};
    std::string          m38{};
    float                m39{};
    int                  m40{};
    std::string          m41{};
    double               m42{};
    bool                 m43{};
    std::int64_t         m44{};
    std::uint32_t        m45{};
    std::optional<int>   m46{};
    std::vector<int>     m47{};
    std::string          m48{};
    float                m49{};
};

// Five levels of nesting, with a container at each level so every level is a real frame.
struct L5 { int leaf = 5; std::string tag; };
struct L4 { L5 inner; std::vector<L5> items; double d = 4; };
struct L3 { L4 inner; std::optional<L4> maybe; std::int64_t n = 3; };
struct L2 { L3 inner; std::vector<L3> items; bool b = true; };
struct L1 { L2 inner; std::vector<L2> items; std::string name = "root"; };

} // namespace

int main() {
    Wide w;
    w.m01 = "wide";
    L1 nest;
    nest.items.push_back(nest.inner);
    std::vector<std::byte> const wb = vcodec::cbor::encode(w);
    std::vector<std::byte> const nb = vcodec::cbor::encode(nest);
    auto const wr = vcodec::cbor::decode<Wide>(std::span<std::byte const>(wb));
    auto const nr = vcodec::cbor::decode<L1>(std::span<std::byte const>(nb));
    std::printf("wide: %zu bytes, decode %s\n", wb.size(), wr ? "ok" : "failed");
    std::printf("nest: %zu bytes, decode %s\n", nb.size(), nr ? "ok" : "failed");
    return (wr && nr) ? 0 : 1;
}

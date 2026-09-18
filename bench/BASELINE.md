# Benchmark baseline

Recorded baseline for spec §13.7 / §14 items 11 and 12. v0.1 sets no throughput target;
this file exists so later work has something to regress against. Re-record it whenever the
encoder, decoder or corpus changes materially, and always on the same class of machine.

## Machine

| | |
|---|---|
| CPU | 12th Gen Intel(R) Core(TM) i7-1260P (`lscpu`), 16 logical CPUs (`nproc`), 4.7 GHz max, CPU frequency scaling **enabled** (google/benchmark warned; expect ±5% run-to-run noise) |
| Caches | as recorded by google/benchmark: L1d 48 KiB, L1i 32 KiB, L2 1.25 MiB (per core), L3 18 MiB shared |
| OS | Linux 7.1.9-arch1-2 |
| Compiler | g++ (GCC) 16.2.1 20260810, `-std=c++26 -freflection -O2`, CMake `Release` |
| Build | CMake 4.4.2, Ninja 1.13.2 |
| Comparators | Glaze v5.5.4, nlohmann/json v3.12.0, simdjson v3.13.0 (haswell kernel selected at runtime), google/benchmark v1.9.4 |
| Library state | commit `2abe014` plus uncommitted edits to `include/vcodec/{core/diagnose,json/read,json/render_error}.hpp` that were in progress in the tree when this was recorded |
| Command | `./bench_encode --benchmark_min_time=0.05s`, `./bench_decode --benchmark_min_time=0.05s`, single-threaded, 2026-09-17 |
| Noise | two full passes were run; per-case times moved by up to 2× between them for the fastest cases (e.g. Glaze small_config encode 78.7 → 38.7 ns) while the machine had another compile job running. The tables are the second, quieter pass. Treat differences under ~30% as noise on this machine |

## How to reproduce

```sh
cmake -S . -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DVCODEC_BUILD_TESTS=OFF -DVCODEC_BUILD_BENCHMARKS=ON
ninja -C build-bench
./build-bench/bench/bench_encode --benchmark_min_time=0.05s
./build-bench/bench/bench_decode --benchmark_min_time=0.05s
ctest --test-dir build-bench/bench -R compile_time_budget     # from the root when VCODEC_BUILD_TESTS=ON
```

## What is measured

The corpus (`bench/corpus.hpp`) is deterministic. Every library encodes the same in-memory
struct and decodes the same text (nlohmann's compact dump, so nobody parses their own
formatting). Decode cases decode once before timing and abort with an error if the library
rejects the input, so all rows below are for successful, complete decodes into the struct.

* `vcodec` encode: `vcodec::json::encode(v)` — returns a fresh `std::string` each iteration.
* `vcodec_append`: `vcodec::json::encode_append(v, buf)` into a cleared, reused buffer;
  the like-for-like comparison with Glaze.
* `glaze`: `glz::write_json(v, buf)` (reused buffer) / `glz::read_json(out, str)`, default `glz::opts`.
* `nlohmann`: `nlohmann::json(v).dump()` / `nlohmann::json::parse(str).get<T>()`; the DOM
  round-trip is the only way to use it, so it is what is measured.
* `simdjson`: On-Demand, one reused parser, `tag_invoke(deserialize_tag, …)` adapters
  looking fields up in declaration order (`bench/simdjson_adapters.hpp`). **Decode only**:
  simdjson v3.13.0 has no builder API for user-defined types, so there is no simdjson
  encode row (see the header comment in `simdjson_adapters.hpp`).

ns/op is wall-clock (`real_time`); MB/s is decimal megabytes of JSON text produced or
consumed per second, from google/benchmark's `bytes_per_second`. Payload sizes are the
vcodec output size (encode) and the shared input size (decode).

## Results

### Small config (`SmallConfig`, 8 mixed members)

Encode (vcodec output 157 bytes):

| library | ns/op | MB/s |
|---|---:|---:|
| vcodec | 262 | 602 |
| vcodec_append | 212 | 742 |
| glaze | 38.7 | 4,067 |
| nlohmann | 915 | 172 |
| simdjson | — | — |

Decode (input 157 bytes):

| library | ns/op | MB/s |
|---|---:|---:|
| vcodec | 415 | 379 |
| glaze | 104 | 1,518 |
| nlohmann | 1,700 | 93 |
| simdjson | 171 | 923 |

### Large array-of-structs (`std::vector<Record>`, 10,000 records)

Encode (vcodec output 1,374,668 bytes):

| library | ns/op | MB/s |
|---|---:|---:|
| vcodec | 2,950,055 | 466 |
| vcodec_append | 2,217,501 | 619 |
| glaze | 691,659 | 1,987 |
| nlohmann | 9,897,827 | 139 |
| simdjson | — | — |

Decode (input 1,374,470 bytes):

| library | ns/op | MB/s |
|---|---:|---:|
| vcodec | 4,841,601 | 284 |
| glaze | 2,215,881 | 620 |
| nlohmann | 16,547,047 | 83 |
| simdjson | 2,904,232 | 473 |

### Deeply nested (`Node` tree, depth 8, branching 3, 3,280 nodes)

Encode (vcodec output 170,032 bytes):

| library | ns/op | MB/s |
|---|---:|---:|
| vcodec | 179,059 | 950 |
| vcodec_append | 164,894 | 1,031 |
| glaze | 50,086 | 3,396 |
| nlohmann | 1,253,395 | 136 |
| simdjson | — | — |

Decode (input 170,202 bytes):

| library | ns/op | MB/s |
|---|---:|---:|
| vcodec | 502,171 | 339 |
| glaze | 250,308 | 680 |
| nlohmann | 2,222,297 | 77 |
| simdjson | 525,216 | 324 |

### String-heavy (`Document`, 2 KB body + 200 paragraphs with escapes)

Encode (vcodec output 70,022 bytes):

| library | ns/op | MB/s |
|---|---:|---:|
| vcodec | 76,713 | 913 |
| vcodec_append | 73,014 | 959 |
| glaze | 10,707 | 6,511 |
| nlohmann | 231,015 | 303 |
| simdjson | — | — |

Decode (input 70,086 bytes):

| library | ns/op | MB/s |
|---|---:|---:|
| vcodec | 82,409 | 850 |
| glaze | 23,393 | 2,998 |
| nlohmann | 246,653 | 284 |
| simdjson | 18,018 | 3,890 |

## Compile-time baseline

`bench/compile_time.cpp` (a 50-member struct and a 5-level nest, encoded and decoded with
vcodec), compiled with `g++ -std=c++26 -freflection -O2 -c`, median of 3 runs:

| | |
|---|---|
| Baseline | **5.106 s** (runs: 5.106 / 4.985 / 5.130 s), recorded in `bench/compile_time_baseline.txt` |
| Budget | +15% → the `compile_time_budget` ctest fails above 5.871 s |
| Verification run | median 5.036 s, −1.3% vs baseline, passed |

For scale: a TU that only includes `<vcodec/json.hpp>` compiles in 0.80 s with the same flags, so roughly 4.3 s of the 5.1 s is instantiating the encoder and decoder for the two structs (about 60 members and 6 nesting levels in total).

## Observations

Nothing here is a target; these are the facts the next round of work starts from.

* Encode: vcodec sits between nlohmann and Glaze on every shape — 3–7× faster than nlohmann
  and 3.5–7× slower than Glaze. The gap to Glaze is widest on `string_heavy` (7.2×) and
  `small_config` (6.8×), narrowest on `deep_nested` (3.6×). `string_heavy` points at the
  string writer (UTF-8 validation is on by default in vcodec and absent in Glaze);
  `small_config` at fixed per-object cost.
* Encode, `large_array`: `encode` (fresh string) is 1.3× slower than `encode_append` (reused
  buffer) on the 1.4 MB output — the allocation and growth pattern of the result string.
* Decode: vcodec is 3–4.4× faster than nlohmann and 2–4× slower than Glaze. Against simdjson
  On-Demand it is 1.7–4.6× slower except on `deep_nested`, where it is 4% faster (On-Demand
  pays per field lookup, which 3,280 three-field objects exercise heavily). `string_heavy`
  is again the widest gap (3.5× to Glaze, 4.6× to simdjson).
* The absolute numbers are single-threaded on a laptop part with frequency scaling on; the
  ratios are more trustworthy than the throughputs.

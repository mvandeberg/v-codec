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
| Baseline | **5.106 s** (runs: 5.106 / 4.985 / 5.130 s), recorded in `bench/compile_time_baseline.txt`. **Re-recorded for v0.2 at 6.124 s** — see the CBOR section below for why and for the second (CBOR) baseline |
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

## CBOR

Recorded 2026-09-18 for spec v0.2 §13.7 / §14 item 13. Same machine and compiler as above;
additional comparators: **Glaze v8.4.0** (CBOR backend; the JSON tables above are against
v5.5.4, which has no CBOR — the two checkouts are separate targets in `bench/CMakeLists.txt`)
and **QCBOR v1.5.1** (static library, default configuration, preferred float serialization on).

| | |
|---|---|
| Command | `./bench_cbor_encode --benchmark_min_time=0.05s`, `./bench_cbor_decode --benchmark_min_time=0.05s`, single-threaded |
| Noise | the machine was not idle: another session's fuzzers (`fuzz_cbor_typed`, `fuzz_typed`) held one to two cores at 100% for most of the session and compile jobs came and went. Three passes were run; the tables are the last pass, taken at load average 1.4, the quietest. Per-case times moved up to 15% between passes; treat differences under ~20% as noise |
| Library state | commit `d615729` (C1–C5) plus the bench/ files added here |

### What is measured

The corpus is `bench/cbor_corpus.hpp`: the four v0.1 shapes from `corpus.hpp` plus two protocol
shapes and one runtime map.

| shape | type | bytes | why |
|---|---|---:|---|
| `small_config` | `SmallConfig` | 125 | per-call overhead (as for JSON) |
| `large_array` | `std::vector<Record>` ×10,000 | 1,106,209 | steady-state throughput |
| `deep_nested` | `Node` tree, depth 8 | 126,373 | recursion and depth tracking; 16 nesting levels, which is one more than QCBOR allows |
| `string_heavy` | `Document` | 68,884 | the text-string path (UTF-8 validation on both sides in vcodec) |
| `cwt_claims` | `CwtClaims` — 7 members, `cbor::key(1..7)`, `exp` under `cbor::tag(1)`, a byte-string `cti` | 81 | integer keys, a tag, a byte string: the protocol vocabulary on a small map |
| `cose_key_set` | `CoseKeySet` — 20 EC2 keys with keys 1, 3, −1..−4 and three 32-byte byte strings | 2,241 | negative integer keys, byte-string bulk copy |
| `metric_map` | `std::unordered_map<std::string, double>` ×64 | 1,114 | the only shape whose key order is unknown at compile time — the shape on which deterministic encoding has to buffer and sort |

Rows:

* `vcodec` — `vcodec::cbor::encode(v)` (fresh `std::vector<std::byte>`), default options, i.e.
  **deterministic** (RFC 8949 §4.2.1). `vcodec_loose` — the same with
  `options{ .deterministic = false }`. `vcodec_append` — deterministic into a cleared, reused
  buffer (like-for-like with Glaze and QCBOR, which both write into a caller-owned buffer).
* `glaze` — `glz::write_cbor(v, buf)` (reused `std::string`) / `glz::read_cbor(out, buf)`,
  default `glz::opts`. Glaze's CBOR reflects struct members to **text keys only** and rejects
  non-text map keys on read, and it does not accept a tag on a plain integer, so the two protocol
  shapes are benchmarked in their **text-key, untagged form** (`CwtClaimsText`, `CoseKeyText`,
  101 and 2,481 bytes). Every other shape is the same bytes for every row.
* `qcbor` — hand-written `QCBOREncode_*` encoders and spiffy-decode (`QCBORDecode_*InMapSZ/N`)
  decoders per shape, members added in canonical order (QCBOR has no run-time sorting in v1.x;
  protocol code sorts at authoring time). Two departures, both in `cbor_corpus.hpp` comments:
  the tree's empty leaf arrays are spliced in pre-encoded (`QCBOREncode_AddEncoded`) because
  QCBOR's compile-time nesting limit is 15 open containers and the depth-8 tree needs 16, and the
  tree and the runtime map are decoded with a `QCBORDecode_GetNext` preorder traversal, because
  spiffy's bounded map search needs one level more than the data (depth 7 decodes with spiffy,
  depth 8 does not) and a map with unknown keys has no spiffy form. Output buffers are sized
  once with QCBOR's size-calculation pass, outside the timed loop.
* Decode rows: `vcodec` default options; `vcodec_strict` with
  `options{ .require_deterministic = true }`, which validates shortest-form heads, float widths,
  definite lengths and key order while reading.

Decode input is vcodec's deterministic encoding of each shape (for Glaze on the protocol shapes,
of the text twin). Before timing, every row's encoding is decoded back with vcodec and compared
with the corpus value, and every decoder's output is compared with the corpus value; a mismatch
aborts the binary. All rows below are complete, verified round trips. Payload sizes are reported
as the `payload_bytes` counter; MB/s is decimal megabytes.

### Encode

| shape | vcodec (det.) | vcodec_loose | vcodec_append | glaze | qcbor |
|---|---:|---:|---:|---:|---:|
| small_config | 272 ns · 460 MB/s | 277 · 452 | 201 · 623 | 48.0 · 2,611 | 257 · 487 |
| large_array | 2,489,522 · 446 | 2,429,971 · 456 | 2,351,716 · 472 | 431,440 · 2,572 | 2,293,108 · 484 |
| deep_nested | 390,395 · 325 | 421,069 · 301 | 399,652 · 317 | 60,008 · 2,113 | 335,137 · 378 |
| string_heavy | 36,285 · 1,903 | 38,173 · 1,809 | 37,397 · 1,845 | 3,355 · 20,600 | 6,020 · 11,480 |
| cwt_claims | 299 · 272 | 302 · 268 | 221 · 368 | 43.8 · 2,310 | 171 · 475 |
| cose_key_set | 3,077 · 730 | 3,030 · 741 | 2,621 · 857 | 486 · 5,120 | 2,403 · 935 |
| metric_map | **5,065 · 220** | **3,235 · 345** | 4,789 · 233 | 700 · 1,594 | 2,220 · 503 (unsorted) |

### Decode

| shape | vcodec | vcodec_strict | glaze | qcbor |
|---|---:|---:|---:|---:|
| small_config | 426 ns · 294 MB/s | 515 · 243 | 74.8 · 1,677 | 2,058 · 61 |
| large_array | 4,657,718 · 238 | 5,187,507 · 214 | 1,340,415 · 829 | 25,288,118 · 44 |
| deep_nested | 581,471 · 218 | 611,584 · 207 | 119,765 · 1,057 | 374,363 · 339 |
| string_heavy | 37,812 · 1,826 | 37,545 · 1,840 | 4,482 · 15,402 | 25,213 · 2,741 |
| cwt_claims | 305 · 267 | 359 · 226 | 78.8 · 1,286 | 1,510 · 54 |
| cose_key_set | 5,615 · 400 | 6,102 · 368 | 2,016 · 1,234 | 23,648 · 95 |
| metric_map | 6,757 · 165 | 7,688 · 145 | 3,327 · 336 | 7,353 · 152 |

### Compile-time baselines

Both baselines were recorded back-to-back with `-DRECORD=ON`, same flags as v0.1
(`g++ -std=c++26 -freflection -O2 -c`, median of 3), while a fuzzer from another session held
one core at 100%.

| TU | baseline | runs | note |
|---|---:|---|---|
| `compile_time.cpp` (JSON) | **6.124 s** | 6.123 / 6.124 / 6.215 | previous baseline 5.106 s (idle machine, 2026-09-17), kept as a `#` comment in the file. An include-only `<vcodec/json.hpp>` TU took 1.02 s under the same load against 0.80 s idle (+27%), so the +20% is the load, not core/. Verification after the load dropped: median **5.482 s** (−10.4% vs the new baseline, +7% vs the old one, inside the 15% budget either way) |
| `compile_time_cbor.cpp` (CBOR) | **7.794 s** | 7.794 / 7.904 / 7.675 | first recording. Include-only `<vcodec/cbor.hpp>`: 1.48 s. Verification: median 7.495 s (−3.8%). One run of the ctest that coincided with two fuzzers and another session's compiles measured 16.7 s and failed the budget; the rerun a minute later passed — the budget test needs a quiet machine |

### Observations

* **Deterministic encoding is free for reflected structs.** On the six struct shapes `vcodec`
  and `vcodec_loose` are within noise of each other (−7% to +8%). A struct's members are emitted
  in canonical order straight from core — the sort happened at compile time — so nothing is
  buffered. The cost appears exactly where the spec said it would: `metric_map`, the runtime
  `unordered_map`, where deterministic mode is **1.57× slower** (5,065 vs 3,235 ns for 64
  entries) — the entries are encoded into a scratch buffer, sorted bytewise by encoded key, and
  copied out. This is the price of the default; the one-line opt-out is the `vcodec_loose` row.
* **`require_deterministic` on read costs 0–21%.** Highest on the shapes made of small maps
  with many heads (`small_config` +21%, `cwt_claims` +18%, `metric_map` +14%), where the
  per-head shortest-form check and the per-entry key-order comparison are a visible fraction of
  the work; zero on `string_heavy`, where the payload is bulk text.
* **vcodec vs Glaze.** Glaze is 5.7–6.8× faster on encode for the struct shapes and 10.8× on
  `string_heavy`; 2.8–5.7× on decode and 8.4× on `string_heavy`. The same picture as JSON:
  Glaze validates no UTF-8 on either side and writes strings with a `memcpy`; vcodec validates by
  default. The protocol-shape ratios (6.3–6.8× encode, 2.8–3.9× decode) are against Glaze's
  text-key form, which is 25% and 11% larger on the wire.
* **vcodec vs QCBOR, encode.** Within 1.1–1.3× of hand-written QCBOR on every struct shape
  (`vcodec_append` is faster than QCBOR on `small_config`; QCBOR is 1.3× faster on
  `cwt_claims` and 1.1× on `cose_key_set`). `string_heavy` is the exception at 6×: QCBOR copies
  text without validating it. On `metric_map` QCBOR is 2.3× faster than deterministic vcodec
  because it does not sort — its output there is not the deterministic encoding.
* **vcodec vs QCBOR, decode.** vcodec is 4.2–5.4× faster than spiffy decode on every struct
  shape: each `QCBORDecode_Get*InMap*` call rescans the map from its start, so a map of *n*
  members costs O(n²) label comparisons plus a skip of every nested item. Where QCBOR is used
  through its `GetNext` traversal instead (`deep_nested`, `metric_map`) it is 1.55× faster than
  vcodec and level with it, respectively; on `string_heavy` it is 1.5× faster. That is the
  trade QCBOR makes deliberately for constrained devices: the spiffy API is the readable one,
  not the fast one.
* Absolute numbers are single-threaded on a loaded laptop part with frequency scaling on; the
  ratios are more trustworthy than the throughputs, and the deterministic/loose and
  strict/default pairs (same binary, same input, adjacent in the run) are the most trustworthy
  of all.

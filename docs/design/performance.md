# Performance and compile-time budget

How `bench/` is built, what it measures, and how the compile-time budget is enforced.
The recorded numbers live in [`bench/BASELINE.md`](../../bench/BASELINE.md) and
[`bench/compile_time_baseline.txt`](../../bench/compile_time_baseline.txt); this note is
about the design decisions behind them. Spec references: §13.7, §14 items 11 and 12.

## Position

v0.1 sets **no throughput target**. Design clarity beats benchmark placement while GCC 16 is
the only toolchain. What v0.1 requires is a recorded baseline for runtime and an *enforced*
budget for compile time, because compile time is the metric that degrades silently:
nobody notices a header that has become 20% slower to include until a downstream build
does.

## Layout

```
bench/
  CMakeLists.txt             FetchContent for benchmark, Glaze, nlohmann, simdjson; targets; ctest
  corpus.hpp                 the four shapes, their generators, nlohmann adapters
  simdjson_adapters.hpp      simdjson On-Demand tag_invoke adapters (decode only)
  encode.cpp                 bench_encode: shape × {vcodec, vcodec_append, glaze, nlohmann}
  decode.cpp                 bench_decode: shape × {vcodec, glaze, nlohmann, simdjson}
  compile_time.cpp           the budget subject: 50-member struct + 5-deep nest
  CompileTimeBudget.cmake    script-mode timer/comparator
  compile_time_baseline.txt  one number (seconds) plus '#' comment lines
  BASELINE.md                recorded results and machine description
```

Everything is opt-in behind `VCODEC_BUILD_BENCHMARKS`; nothing here is built by default and
the library's own build has no dependencies.

## The corpus

One struct definition per shape, shared by every library, in `bench/corpus.hpp`:

| shape | type | why |
|---|---|---|
| small config | `SmallConfig`, 8 scalar/string members | per-call overhead: the fixed cost of an object frame, key lookup, and result construction dominates |
| large array-of-structs | `std::vector<Record>`, 10,000 records with a `vector<string>` each | steady-state throughput; allocation pattern of the output buffer and of the decoded vectors |
| deeply nested | `Node` tree, depth 8, branching 3 (3,280 nodes) | recursion and depth tracking; the encoder's `max_depth` check and the decoder's frame push/pop on every level |
| string-heavy | `Document`, a 2 KB body and 200 ~300-byte paragraphs with quotes, backslashes, tabs, newlines, a control byte and some non-ASCII | the string escape/unescape path and UTF-8 validation |

The structs are plain aggregates with no annotations, so vcodec and Glaze reflect them with
no registration; nlohmann gets `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE`; simdjson gets a
`tag_invoke(deserialize_tag, …)` overload per type, which lets `doc.get<T>()` and
`std::vector<T>` work through simdjson's own container machinery.

Generation is seeded (`std::mt19937_64` with fixed seeds), so two machines produce the same
bytes. The decode input is nlohmann's compact `dump()`, chosen because it is the one
formatting none of the fast libraries produced themselves.

## Fairness rules

* Every library gets its idiomatic fast path, not its most convenient one: Glaze writes into
  a reused buffer, simdjson reuses a parser and takes a `padded_string`, vcodec is shown
  both as `encode` (the documented entry point, fresh string) and `encode_append` (reused
  buffer). nlohmann has no non-DOM path, so the DOM round-trip is what is measured.
* Decode cases decode once before the timed loop and `SkipWithError` if the library rejects
  the input. A fast number for an incomplete decode is worse than no number.
* `benchmark::DoNotOptimize` on every result, `SetBytesProcessed` on every case so MB/s
  appears next to ns/op.
* Defaults are left alone: vcodec validates UTF-8 on encode, Glaze does not. This is a
  real difference in what users get and is reported as such rather than normalised away.

## simdjson encode is absent

Spec §13.7 names "simdjson's builder". simdjson v3.13.0 has no builder API for user-defined
types: its `to_json_string` overloads re-emit an already-parsed On-Demand value, and
`dom::string_builder` is a private helper for DOM elements. The
`simdjson::builder::to_json_string(T)` API arrives in a later release. The decode column is
complete; the encode column says so in `BASELINE.md`, and `simdjson_adapters.hpp` carries the
explanation so the next person to bump simdjson knows to add the encode cases.

## Compile-time budget

`bench/compile_time.cpp` is the subject: a 50-member `Wide` struct spread across `int`,
`std::string`, `double`, `bool`, `std::int64_t`, `std::uint32_t`, `std::optional<int>`,
`std::vector<int>` and `float`, and a five-level nest `L1 … L5` with a container at every
level so every level is a real frame. `main` encodes and decodes both, so the target
`bench_compile_time` proves the measured code links and runs.

`CompileTimeBudget.cmake` is a CMake script (`-P`), not a shell script, so it runs wherever
CMake does and has no extra dependencies. It compiles the subject with `-c` using the same
compiler and flags as the benchmark targets, `RUNS` times (3), takes the **median** of the
wall-clock times, and compares it with the number in `compile_time_baseline.txt`. The
median rather than the minimum because CI machines are noisy in both directions and the
goal is to catch a real regression, not to reward a lucky run. `-c` rather than
`-fsyntax-only` because `-O2` codegen on reflection-heavy code is part of what a user pays;
both are one flag away.

Arithmetic is in integer microseconds because `math(EXPR)` has no floating point. Lines
beginning with `#` in the baseline file are ignored, so the file carries its own machine
description. `-DRECORD=ON` re-records: it writes the median plus the CPU, core count, OS,
compiler and flags into the file. Re-record deliberately and mention it in the commit
message; a baseline that is silently re-recorded enforces nothing.

The test is registered as `compile_time_budget`. The root `CMakeLists.txt` only calls
`enable_testing()` when `VCODEC_BUILD_TESTS` is on, so `bench/CMakeLists.txt` calls it too;
in a benchmark-only configuration run `ctest --test-dir <build>/bench`, in the default
configuration `ctest` from the build root finds it alongside the unit tests.

**Budget: fail at more than 15% slower than the baseline** (spec §13.7). The baseline was
recorded at 5.106 s on the machine in `BASELINE.md`; the verification run after recording
came in at −1.3%. An include-only TU takes 0.80 s, so the budget is almost entirely
instantiation time, which is what it should be tracking. A different machine class will need its own baseline: the budget is
about drift on one CI fleet, not portability of a number.

## What the first baseline says

Recorded 2026-09-17 (see `BASELINE.md` for the tables). In ratios, which are more stable
than the throughputs on a frequency-scaling laptop part:

* vcodec is 3–7× faster than nlohmann and 2–7× slower than Glaze on every shape, both
  directions. The widest gap is the string-heavy shape on encode (7×); on decode of the
  deep tree vcodec is level with simdjson On-Demand (4% faster).
* `encode` (fresh string) is 1.3× slower than `encode_append` on the 1.4 MB output — the
  allocation and growth pattern of the result string, which is the first obvious thing to
  look at when performance work starts.
* The string path (escape/unescape plus UTF-8 validation) is the second.

None of this is a v0.1 concern. It is written down so that when the library has a second
toolchain and performance becomes a goal, the work starts from data.

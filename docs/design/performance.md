# Performance and compile-time budget

How `bench/` is built, what it measures, and how the compile-time budget is enforced.
The recorded numbers live in [`bench/BASELINE.md`](../../bench/BASELINE.md) and
[`bench/compile_time_baseline.txt`](../../bench/compile_time_baseline.txt); this note is
about the design decisions behind them. Spec references: §13.7, §14 items 11 and 12; for
CBOR, spec v0.2 §13.7 and §14 item 13 (the [CBOR section](#cbor-v02) below).

## Position

v0.1 sets **no throughput target**. Design clarity beats benchmark placement while GCC 16 is
the only toolchain. What v0.1 requires is a recorded baseline for runtime and an *enforced*
budget for compile time, because compile time is the metric that degrades silently:
nobody notices a header that has become 20% slower to include until a downstream build
does.

## Layout

```
bench/
  CMakeLists.txt                  FetchContent for benchmark, Glaze, nlohmann, simdjson, QCBOR; targets; ctest
  corpus.hpp                      the four shapes, their generators, nlohmann adapters
  simdjson_adapters.hpp           simdjson On-Demand tag_invoke adapters (decode only)
  encode.cpp                      bench_encode: shape × {vcodec, vcodec_append, glaze, nlohmann}
  decode.cpp                      bench_decode: shape × {vcodec, glaze, nlohmann, simdjson}
  cbor_corpus.hpp                 corpus.hpp + CWT, COSE_Key and runtime-map shapes; QCBOR encoders/decoders; Glaze text twins
  cbor_encode.cpp                 bench_cbor_encode: shape × {vcodec, vcodec_loose, vcodec_append, glaze, qcbor}
  cbor_decode.cpp                 bench_cbor_decode: shape × {vcodec, vcodec_strict, glaze, qcbor}
  compile_time.cpp                the JSON budget subject: 50-member struct + 5-deep nest
  compile_time_cbor.cpp           the same structs through vcodec::cbor
  CompileTimeBudget.cmake         script-mode timer/comparator
  compile_time_baseline.txt       one number (seconds) plus '#' comment lines
  compile_time_cbor_baseline.txt  the CBOR TU's number
  BASELINE.md                     recorded results and machine description
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

The test is registered as `compile_time_budget` (and, since v0.2, `compile_time_budget_cbor`
for the CBOR TU; see below). The root `CMakeLists.txt` only calls
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

## CBOR (v0.2)

Spec v0.2 §13.7 asks for the same corpus through the CBOR backend against Glaze's CBOR and
QCBOR, plus two protocol shapes, with deterministic mode measured separately from
non-deterministic "so the cost of buffering runtime maps is visible", and for the compile-time
budget to gain a CBOR TU while the JSON TU keeps its own budget. The numbers are in
[`BASELINE.md`](../../bench/BASELINE.md#cbor); this section is the reasoning.

### What is measured

Seven shapes (`bench/cbor_corpus.hpp`): the four v0.1 shapes, included from `corpus.hpp` so
the JSON and CBOR benchmarks cannot drift apart; a **CWT claims set** (`CwtClaims`: seven
members under `[[=cbor::key(1..7)]]`, the expiry under `[[=cbor::tag(1)]]`, a byte-string
claim id — RFC 8392's Appendix A values); a **COSE_Key set** (`CoseKeySet`: twenty EC2 keys,
keys 1, 3 and −1..−4, three 32-byte byte strings each); and a **runtime map**
(`std::unordered_map<std::string, double>`, 64 entries). The last is not in the spec's list.
It is there because without it the deterministic/non-deterministic pair measures nothing —
see the next subsection — and the spec's stated purpose for that pair is the buffering cost.

Rows on encode: vcodec deterministic (the default), vcodec with
`options{ .deterministic = false }`, vcodec deterministic into a reused buffer (like-for-like
with the two comparators, which write into caller-owned buffers), Glaze, QCBOR. On decode:
vcodec default, vcodec with `options{ .require_deterministic = true }`, Glaze, QCBOR. The
decode input is vcodec's deterministic encoding, so every decoder reads the same bytes; the
fairness rules from the JSON section hold (idiomatic fast path each, `DoNotOptimize`,
`SetBytesProcessed`, defaults left alone). Two verifications run before timing and abort the
binary on failure: every row's *encoding* is decoded back with vcodec and compared with the
corpus value (so a hand-written QCBOR encoder that dropped a field could not post a fast time),
and every *decoder's* output is compared with the corpus value. Payload sizes are recorded as a
`payload_bytes` counter so the tables do not have to be reconstructed from MB/s.

**Glaze's CBOR is a text-key format.** Glaze v8.4.0 reflects members to text-string keys and its
reader rejects any non-text key; it also does not accept a tag in front of a plain integer. It
therefore benchmarks a text-key, untagged twin of each protocol shape, converted to and from the
protocol struct for the value comparison, and its input on those two shapes is vcodec's
deterministic encoding of the twin. The tables say so; the ratios on those rows are against a
wire form 11–25% larger. Glaze's CBOR backend arrived after v5.5.4, the version the JSON
tables were recorded against, so `bench/CMakeLists.txt` takes a second, download-only checkout
of Glaze v8.4.0 (an INTERFACE target over its `include/`) rather than moving the JSON
comparator; the two never meet in one TU.

**QCBOR's two nesting departures.** QCBOR's nesting limit is a compile-time constant of the
library (`QCBOR_MAX_ARRAY_NESTING`, 15 open containers). The depth-8 tree has 16 (eight maps,
eight `children` arrays, the innermost empty). On encode, the empty leaf arrays are spliced in
pre-encoded with `QCBOREncode_AddEncoded` — one constant byte — instead of being opened. On
decode, QCBOR does not descend into empty definite-length arrays, so the data itself fits in 15
levels, but spiffy decode's bounded map search needs one more (a depth-7 tree decodes with
spiffy, depth 8 does not), so the tree is read with a `QCBORDecode_GetNext` preorder traversal.
The runtime map is read the same way because a map with unknown keys has no spiffy form. Every
other shape uses spiffy decode, which is the API QCBOR recommends and the one COSE/CWT stacks
use. Both departures are marked in `cbor_corpus.hpp`.

### Why deterministic mode costs what it costs

RFC 8949 §4.2.1 deterministic encoding needs shortest-form heads, preferred float widths,
definite lengths and map keys sorted bytewise by their encoded form. The first three are
properties of how each item is written and cost nothing extra: the writer always emits the
shortest head, and preferred float serialization is the default in both modes. Only the key
order can cost, and it costs only when the order is not already known.

For a reflected struct the keys are compile-time constants, so core hands the members to the
CBOR sink already in canonical order (`cbor/write.hpp`: "struct maps arrive pre-ordered from
core and are never buffered"). That is why `vcodec` and `vcodec_loose` are within noise of
each other on all six struct shapes, including the integer-keyed protocol shapes, whose
canonical order (1, 3, −1, −2, −3, −4 for a COSE_Key) is anything but declaration order. The
sort happened during instantiation and is paid in the compile-time budget, not at run time.

For a runtime map — `std::map`, `std::unordered_map`, anything whose keys arrive at run time —
the sink has no choice: it encodes each entry into a scratch buffer, sorts the entries by the
bytes of their encoded keys, checks for duplicates (which deterministic encoding forbids) and
copies them out. On the 64-entry `metric_map` that is a 1.57× slowdown against the same map
encoded in iteration order. This is the number Open Question 1 of the spec asked to have in
front of the reader when deciding the default: the default stays deterministic because
signatures need it and a protocol user never wants the other default; the telemetry user who
does not care flips one option and gets the `vcodec_loose` row.

On the read side, `require_deterministic` is a validation pass folded into the parse: each head
is checked for shortest form, each float for preferred width, each container for a definite
length, and each map key is compared with its predecessor. It is off by default because a
decoder that rejects valid-but-non-canonical input is a protocol decision, not a parsing one;
the `cbor::deterministic` type annotation turns it on for types that are canonical by
definition. Its cost, 0–21%, is highest on shapes made of many small maps (`small_config`,
`cwt_claims`) where the per-head checks are a visible fraction of the work, and zero on the
string-heavy shape where the payload is bulk text.

### Why QCBOR is the comparator for the protocol axis

Glaze answers the question the JSON benchmark asked — how far a reflection-driven codec is
from the fastest reflection-driven codec on the same structs — and the CBOR answer is the same
as the JSON one (5–7× on encode, 3–6× on decode, the gap widest on strings where vcodec
validates UTF-8 and Glaze does not). But Glaze cannot express the shapes the CBOR backend
exists for: integer keys, negative keys, tags on plain values. Its CBOR is JSON's data model in
a binary encoding.

QCBOR is the other reference point. It is the C implementation that COSE, CWT and EAT stacks
are built on (t_cose and the IETF interop work use it), it produces the exact bytes those
protocols require, and its API is what a protocol engineer writes against today: an explicit
`OpenMap` / `AddInt64ToMapN(-1, …)` / `CloseMap` sequence per message, sorted by hand. That
makes it the right floor and the right ceiling at once. As a floor: it is hand-written,
reflection-free code with no per-field abstraction, so a reflection-driven encoder that lands
within 1.1–1.3× of it (the struct shapes) is not paying a meaningful abstraction tax; the one
6× gap (`string_heavy`) is UTF-8 validation, which QCBOR does not perform on text strings. As a
ceiling on what protocol code should cost to *read*: QCBOR's spiffy decode rescans the map from
its start for every field lookup, an O(n²) pattern chosen for code size and readability on
constrained devices, and vcodec's single-pass decode is 4–5× faster on every struct shape while
producing the same values. Where QCBOR is used through its `GetNext` traversal instead (the tree,
the runtime map) it is level with or ahead of vcodec, which locates the difference precisely in
the lookup strategy rather than in the parsing.

Neither comparator measures the annotations' correctness — the conformance and cross tests do
that. What the benchmark establishes is that expressing a COSE_Key as an annotated struct costs
about what writing it out by hand in C costs, and reading it costs a fifth.

### The second compile-time budget

`bench/compile_time_cbor.cpp` is `compile_time.cpp` with `vcodec::cbor` in place of
`vcodec::json`: the same `Wide` and `L1…L5` definitions, duplicated rather than shared so each
TU is a self-contained measurement. It has its own test (`compile_time_budget_cbor`) and its
own baseline file, and the JSON TU keeps its own, because the spec's check is specifically that
the JSON TU did not get slower when core/ was extended for CBOR (spec v0.2 §13.7: "the
measurable form of 'C0 changed core without taxing JSON'"). One combined number could hide a
JSON regression behind a CBOR improvement.

Both baselines were re-recorded on 2026-09-18 on the machine in `BASELINE.md`, back-to-back and
under the same load: JSON 6.124 s (previously 5.106 s), CBOR 7.794 s. The JSON number moved
+20%, which would have failed its own budget — but the machine was not idle (a fuzzer held one
core at 100%), an include-only `<vcodec/json.hpp>` TU took 1.02 s against 0.80 s idle under
the same load (+27%), and a verification run after the load dropped measured 5.482 s, +7% on
the old baseline and inside the 15% tolerance. The honest reading is that core/ did not tax
JSON measurably and the recorded number carries some load; the previous value is kept as a
comment in the baseline file so the next quiet re-recording has both to compare against. The
CBOR TU is 1.27× the JSON TU, and its include-only cost is 1.48 s against 1.02 s, so the
difference is mostly the larger header set (tags, hex renderer, the annotation vocabulary),
not instantiation. The budget test itself needs a quiet machine: one `ctest` run that coincided
with two fuzzers and another session's compiles measured 16.7 s for the CBOR TU and failed; the
rerun a minute later passed at −3.8%.

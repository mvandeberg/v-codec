# Compatibility: what v0.1 promises and does not

The version number is doing real work. v0.1 says the annotation vocabulary and the traversal
seam are hypotheses that contact with real types will revise; it does not say the code is
unfinished. Correctness, conformance, robustness and diagnostics are held to a 1.0 bar. The
API surface is not.

## Toolchain

| | Required | Exercised for v0.1 |
|---|---|---|
| Compiler | GCC 16, `-std=c++26 -freflection` | GCC 16.2.1 only |
| Standard library | libstdc++ shipped with that GCC (needs `<meta>`, `std::define_static_string`, `std::define_static_array`, constexpr exceptions) | same |
| Build | CMake 3.28+ | 3.28+ |
| Platform | anything the compiler targets; the library has no OS dependency | Linux x86-64 |

The `vcodec::vcodec` CMake target adds `cxx_std_26` and `-freflection` (and
`-fexpansion-statements` when the compiler identifies as Clang). If you do not use CMake,
pass `-std=c++26 -freflection` and add `include/` to the include path; there is nothing to
link. `<vcodec/core/compiler.hpp>` refuses to compile without `<meta>` and is the only header
allowed to contain a compiler conditional.

### Compilers not covered

- **Bloomberg clang-p2996** is the spec's secondary, best-effort target. It was **not
  exercised** for v0.1: no build of it was available on the development machine
  (`docs/design/m0-spellings.md`, finding 9). The CMake flags for it are in place; whether
  the headers compile under it is unknown, and a failure there does not block a release.
- **Clang mainline** has no reflection implementation. Not supported.
- **MSVC** has no reflection implementation. Not supported, and no promise can be made until
  one exists.
- **GCC 16.1** is the spec's stated minimum; only 16.2.1 was used. Nothing known depends on
  the point release, but it was not checked.

Fuzzing under GCC uses a standalone driver (GCC has no libFuzzer); a true libFuzzer run
requires Clang and is part of the same unexercised secondary target.

### Test-only dependencies

Catch2 v3.8.1, google/benchmark v1.9.4, the nst/JSONTestSuite corpus and, for the
benchmarks, Glaze, nlohmann/json and simdjson are fetched with `FetchContent` when the
corresponding `VCODEC_BUILD_*` option is on. None is needed to use the library.

## What v0.1 promises

These hold now and are what the test suite enforces:

1. **Correctness on the documented type set.** Every type in [types.md](types.md) encodes
   and decodes, with property-based round-trip tests over generated values
   (`test/roundtrip/`).
2. **Every documented annotation** in [annotations.md](annotations.md) is implemented and
   tested, except the two the spec defers (`skip_if(pred)`, `collect_unknown_fields`).
3. **JSON grammar conformance** (RFC 8259, strict): every JSONTestSuite `y_` file is
   accepted and every `n_` file is rejected by the typed decoder. Implementation-defined
   `i_` files have a recorded disposition, below.
4. **No crash and no undefined behaviour on hostile input.** The full suite runs under
   ASan+UBSan (`-DVCODEC_SANITIZE=ON`), and three fuzz harnesses (arbitrary bytes into the
   validator, arbitrary bytes into a hostile struct, structured round-trip) run from a
   committed seed corpus.
5. **The rendered error outputs** in [errors.md](errors.md) are produced byte for byte from
   real malformed input, at the correct offset, for every error code.
6. **The compile-time diagnostics** in [types.md](types.md) and [annotations.md](annotations.md)
   are reproduced by `test/compile_fail/`, and every rejected type produces exactly one
   `static assertion failed` line and at most one instantiation frame naming the public
   entry point; there is no cascade into the traversal.
7. **`core/` includes nothing from `json/`**, checked at configure time and again by
   `ctest`, so a second format is not held back by JSON-shaped assumptions.

### JSONTestSuite `i_` dispositions

The corpus's `i_` files are inputs the JSON RFCs leave to the implementation. vcodec's
policy, as exercised by the typed decoder on every `i_` file in the corpus:

| Input | Disposition | Error code |
|---|---|---|
| lone or inverted `\u` surrogates, in values or keys (`i_string_1st_surrogate_but_2nd_missing`, `i_string_lone_second_surrogate`, `i_object_key_lone_2nd_surrogate`, …) | reject | `invalid_escape` |
| malformed UTF-8: invalid sequences, overlong forms, encoded surrogates, code points above U+10FFFF, Latin-1 bytes, truncated sequences (`i_string_invalid_utf-8`, `i_string_overlong_sequence_*`, `i_string_UTF8_surrogate_U+D800`, `i_string_iso_latin_1`, …) | reject while `validate_utf8` is on (the default) | `invalid_utf8` |
| UTF-16 input, with or without BOM (`i_string_utf16BE_no_BOM`, `i_string_UTF-16LE_with_BOM`, …) | reject | `unexpected_token` |
| a UTF-8 BOM before the document (`i_structure_UTF-8_BOM_empty_object`) | reject | `unexpected_token` |
| 500 nested arrays (`i_structure_500_nested_arrays`) | reject at the default `max_depth` of 256; accepted with a larger limit | `depth_exceeded` |
| exponents that overflow `double` (`i_number_huge_exp`, `i_number_pos_double_huge_exp`, `i_number_real_pos_overflow`, `i_number_real_neg_overflow`, `i_number_neg_int_huge_exp`) | reject | `out_of_range` |
| exponents that underflow to zero (`i_number_real_underflow`, `i_number_double_huge_neg_exp`) | accept as `0.0` | — |
| integer literals beyond 64 bits (`i_number_too_big_pos_int`, `i_number_too_big_neg_int`, `i_number_very_big_negative_int`) | accept as `double` where a floating-point value is expected; `out_of_range` where an integer is expected | — |

`test/conformance/i_dispositions.txt` records the per-file result and the conformance test
fails when the observed dispositions differ from it, so a change on this input is a reviewed
diff rather than a surprise.

## What v0.1 does not promise

- **The annotation vocabulary may change.** Names, placement rules and the set itself are
  the hypothesis v0.1 exists to test. Expect renames and additions in v0.2; expect the
  structural-type restriction on `default_value` to be lifted by a new mechanism rather than
  relaxed in place.
- **The error `path_step` representation may change.** The *rendered* output of
  `render_terse` and `render_framed` is expected to be stable; the `path_step` struct, the
  extended accessors on `error` (`expected()`, `found()`, `context()`, ...) and the builder
  functions are not.
- **The sink and reader concepts may change.** They are near-public, since custom codecs
  see them, and the CBOR work will exert pressure on them: definite/indefinite lengths,
  `save`/`restore`, the field-aware hooks and `text_ref` are the likely points of change.
  A custom sink or reader written against v0.1 should expect to be revisited.
- **`format_traits`, the audit (`core::audit_of`, `core::explain`) and everything else under
  `vcodec::core`** are implementation detail that a format author must touch today. They
  are documented so that a second format can be written, not as a stable interface.
- **The public API may change.** Entry-point signatures (in particular the parameter order
  of `Opts` and `T`), the `options` fields, the `errc` list and `collected<T>` may all be
  adjusted.
- **No ABI stability.** Header-only; there is no compiled interface to stabilise, and no
  attempt is made to keep object layouts or symbol names between releases.
- **No MSVC, no Clang mainline.** Not deferred promises: promises that cannot be made until a
  reflection implementation exists.
- **No performance target.** Benchmarks against Glaze, simdjson and nlohmann exist to record
  a baseline, not to claim a placement. Compile time is budgeted against a recorded baseline
  (15% tolerance) so it cannot degrade silently.

## Out of scope for v0.1

Deferred, with the reason, from the specification:

| Deferred | Why |
|---|---|
| CBOR (a write-only spike exists under `spike/`, never installed) | second format waits for the seam to be proven on the first |
| DOM / generic value type | not the library's purpose; a test-only one exists in `test/support/dom.hpp` |
| `collect_unknown_fields` | needs a decision on the container protocol |
| untagged `std::variant` | alternative-trying has ambiguous failure modes |
| streaming / incremental parse | whole-buffer only; `save`/`restore` depends on it |
| allocator customisation | `std::string` and default allocators only |
| JSON Schema generation | strong candidate for 0.2 |
| validation annotations | widens the remit |
| non-structural annotation values | needs its own research step (spec §5.1.1) |
| vcpkg port, Conan recipe | post-0.1 packaging |

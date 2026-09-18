# Compatibility: what v0.2 promises and does not

The version number is doing real work. v0.1 said the annotation vocabulary and the traversal
seam were hypotheses that contact with real types would revise; v0.2 adds the CBOR backend
and says the same of the `cbor::` vocabulary and the format hooks it needed. Neither says the
code is unfinished: correctness, conformance, robustness and diagnostics are held to a 1.0
bar. The API surface is not.

Everything v0.1 promised for JSON still holds under v0.2, byte for byte: every v0.1 JSON
test runs unchanged and the JSON compile-time budget is within its tolerance. The CBOR
additions to `core/` are additive hooks with defaults that reproduce the v0.1 behaviour.

## Toolchain

| | Required | Exercised for v0.2 |
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

Catch2 v3.8.1, google/benchmark v1.9.4, the nst/JSONTestSuite corpus, the cbor/test-vectors
corpus (RFC 8949 Appendix A) and, for the benchmarks, Glaze, nlohmann/json and simdjson are
fetched with `FetchContent` when the corresponding `VCODEC_BUILD_*` option is on.
`VCODEC_BUILD_CBOR` (default on) gates only the CBOR test, fuzz and conformance targets; the
header `<vcodec/cbor.hpp>` is always installed. None is needed to use the library.

## What v0.2 promises

These hold now and are what the test suite enforces. Items 1–7 are the v0.1 promises,
unchanged; items 8–13 are new for CBOR.

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
7. **`core/` includes nothing from `json/` or `cbor/`, and the two formats include nothing
   from each other**, checked at configure time and again by `ctest`, so neither format is
   held back by the other's assumptions.
8. **RFC 8949 conformance, both directions.** Every Appendix A vector (the cbor/test-vectors
   corpus) decodes to its diagnostic value through the test DOM, every vector in preferred
   form re-encodes to its bytes, and every non-preferred vector is rejected under
   `require_deterministic` (`test/conformance/cbor_appendix_a.cpp`).
9. **Deterministic output is stable.** For a given type and value, `cbor::encode` with the
   default options produces the same bytes across versions and independent of container
   iteration order and member declaration order; encode → decode → encode is byte-identical;
   every deterministic output passes `require_deterministic`
   (`test/cbor/deterministic.cpp`, `test/roundtrip/cbor_roundtrip.cpp`). This is the promise
   that lets a signature computed over v0.2 output be verified against v0.3 output.
10. **No crash and no undefined behaviour on hostile CBOR.** A committed hostile corpus
    (`test/conformance/corpus/cbor_hostile/`, each file with the expected `errc` and offset)
    is rejected identically by the validator and the typed path; four fuzz harnesses
    (`fuzz_cbor_skip`, `fuzz_cbor_typed`, `fuzz_cbor_roundtrip`, `fuzz_cbor_deterministic`)
    run under ASan+UBSan from committed seeds.
11. **The rendered CBOR errors** in [errors.md](errors.md#cbor-codes) and [cbor.md](cbor.md)
    are produced byte for byte by `render_hex` from real malformed input at the correct
    offset (`test/cbor/errors.cpp`).
12. **The CBOR compile-time diagnostics** are reproduced by `test/compile_fail/cbor_*.cpp`
    under the same no-cascade guard, including the two-format case: a CBOR-only type through
    a JSON entry point fails with the v0.1 message.
13. **Cross-format consistency.** The same type produces the same model token stream, key
    set, skips and error paths under both formats, with the documented per-format defaults
    (enum integers, native bytes and integer keys) as the only differences; JSON → CBOR →
    JSON transcoding through the test DOM is lossless over every JSONTestSuite `y_`
    document (`test/cross/`, `test/conformance/cbor_transcode.cpp`).

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

## What v0.2 does not promise

- **The `cbor::` vocabulary may change.** It is v0.2's hypothesis as the core vocabulary was
  v0.1's: `key`, `integer_keys`, `text_key_alias`, `tag`, `byte_string`, `indefinite`,
  `float_width`, `typed_array`, `deterministic`, `self_describe` may be renamed, re-scoped
  or merged. In particular `cbor::indefinite` currently affects only string members
  ([cbor.md](cbor.md#indefinite-lengths)) and its scope is likely to change.
- **The bytes of non-deterministic output may change** between versions. Deterministic
  output may not; that is the point of it.
- **The format hook set may change.** `format_traits` (`integer_key`, `member_order`,
  `expected_tags`, `enum_default_integer`, `bulk_range`, `accepts_bulk_range`,
  `accepts_text_key`, `bytes_borrowable`, `check_field`, `check_type`) and the optional sink
  and reader hooks (`real<F>`, `text<F>`, `write_range<F>`, `prepared_key<int64>`,
  `begin_map_ordered`, `expect_tags`, `read_range<F>`, `expect_bignum`) are near-public,
  since custom codecs and formats see them, and a third format will exert pressure on them.
- **The annotation vocabulary may change.** Names, placement rules and the set itself are
  the hypothesis v0.1 exists to test. Expect renames and additions; expect the
  structural-type restriction on `default_value` to be lifted by a new mechanism rather than
  relaxed in place.
- **The error `path_step` representation may change.** The *rendered* output of
  `render_terse` and `render_framed` is expected to be stable; the `path_step` struct, the
  extended accessors on `error` (`expected()`, `found()`, `context()`, ...) and the builder
  functions are not.
- **The sink and reader concepts may change.** They are near-public, since custom codecs
  see them. The CBOR work left the required members unchanged and added only optional hooks,
  which is encouraging, but `save`/`restore`, `text_ref` / `bytes_ref` and the shape of the
  field-aware hooks remain the likely points of change. A custom sink or reader written
  against v0.1 or v0.2 should expect to be revisited.
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

## Out of scope for v0.2

Deferred, with the reason, from the specifications ([design/cbor-spec.md](design/cbor-spec.md)
§1.2 for the CBOR items):

| Deferred | Why |
|---|---|
| COSE / CWT signing and encryption | a downstream library; v0.2 is what it would be built on (integer keys, tags, deterministic encoding, byte strings) |
| CDDL generation | wanted, paired with JSON Schema generation as a 0.3 candidate |
| RFC 8949 §4.2.2 extended deterministic requirements ("length-first" / RFC 7049 canonical key order) | the core requirements are what COSE uses; a `key_order` option is a candidate once someone needs interop with an older producer |
| Bignum (tags 2/3) *encode* into built-in types | no standard C++ type maps to it; decoding into a user codec is provided through `expect_bignum` |
| Streaming / incremental CBOR decode | whole-buffer only, as in JSON; `save`/`restore` depends on it |
| Arbitrary user tags on arbitrary types without a codec | `cbor::tag(n)` covers the protocol cases; anything richer belongs in a codec |
| `write_prepared<T>` whole-struct fast path for the CBOR writer | keys are one memcpy each (`prepared_key<Key>`); value heads depend on magnitude, so the whole-struct case is rare |
| CBOR benchmarks against Glaze CBOR / QCBOR | not yet recorded; the JSON benchmark harness is in place |
| DOM / generic value type | not the library's purpose; a test-only one exists in `test/support/dom.hpp` (now covering bytes, tags and integer keys, and used for transcoding) |
| `collect_unknown_fields` | needs a decision on the container protocol |
| untagged `std::variant` | alternative-trying has ambiguous failure modes |
| streaming / incremental parse | whole-buffer only; `save`/`restore` depends on it |
| allocator customisation | `std::string` and default allocators only |
| JSON Schema generation | strong candidate for 0.2 |
| validation annotations | widens the remit |
| non-structural annotation values | needs its own research step (spec §5.1.1) |
| vcpkg port, Conan recipe | post-0.1 packaging |

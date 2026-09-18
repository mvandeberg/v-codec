# Changelog

## 0.2.0 — 2026-09-18

The CBOR backend. JSON behaviour is unchanged; every 0.1 JSON test passes as it was.

### Added

- `<vcodec/cbor.hpp>`: RFC 8949 encode and decode for the whole 0.1 type set over the same
  traversal as JSON, plus natively: byte strings (`std::byte` ranges without annotation,
  `std::uint8_t` ranges with `cbor::byte_string`), integer map keys, semantic tags, bignums
  (tags 2/3, via `expect_bignum` / `negative`), RFC 8746 typed arrays, indefinite lengths,
  simple values, and `std::chrono::sys_time` as tag 1 (epoch) or tag 0 (RFC 3339 text).
- Deterministic encoding (RFC 8949 §4.2.1) on by default: shortest heads, definite lengths,
  preferred float serialization (half/single/double, NaN as `f9 7e00`), bytewise canonical
  key order — struct members permuted at compile time, runtime maps buffered and sorted.
  `options::require_deterministic` validates all of it on read, including key order for
  keys of any kind and the canonical NaN, with `errc::non_deterministic`.
- CBOR annotations: `cbor::key(n)`, `cbor::integer_keys`, `cbor::text_key_alias`,
  `cbor::tag(n)` (field and type level), `cbor::byte_string`, `cbor::typed_array`,
  `cbor::indefinite`, `cbor::float_width(half | single | double)`, `cbor::deterministic`
  (forces deterministic encode and validated decode for the type), `cbor::self_describe`;
  core gains `as_text`.
- CBOR options: `deterministic`, `indefinite`, `floats`, `self_describe`,
  `require_deterministic`, `ignore_unknown_tags`, `undefined_as_null`,
  `accept_typed_arrays`, `max_depth`, `validate_utf8`, `duplicates`, `errors`.
- Entry points mirroring JSON: `cbor::encode`, `encode_append`, `encode_to`, `encode_into`,
  `decode`, `decode_into`, `decode_all`; `cbor::encodable`, `cbor::decodable`,
  `cbor::borrows`; `vcodec::cbor_only<T>` / `vcodec::json_only<T>`.
- Error codes `malformed_item`, `unsupported_simple_value`, `tag_mismatch`,
  `non_deterministic`, `indefinite_in_borrowed_string`; `render_hex` (16-byte hex window
  with carets and a head description); `missing_field` names the integer key.
- Compile-time diagnostics for CBOR: duplicate integer keys (including after flattening or
  allocation), `integer_keys` on a flattened type, `float_width` on a non-float,
  `indefinite` under `deterministic`, conflicting options, two time tags on one member,
  `typed_array` on a non-contiguous range, a CBOR-only type through JSON.
- Format seam: `format_traits` hooks `integer_key`, `accepts_text_key`, `member_order`,
  `expected_tags`, `bulk_range`, `enum_default_integer`, `bytes_borrowable`, `check_field`,
  `check_type`; sink hooks `prepared_key<int64>`, `begin_map_ordered`, `write_range`,
  field-aware `real`/`text`/`begin_array`/`begin_map`; reader hooks `expect_tags`,
  `read_range`, `start`/`finish`; field-aware `codec_for::encode<F>` / `decode<F>`.
- Tests: CBOR unit suites, RFC 8949 Appendix A conformance (pinned `cbor/test-vectors`),
  a hostile corpus with expected code and offset for both paths, JSON↔CBOR transcoding of
  every JSONTestSuite `y_` document, property-based round trips, cross-format consistency
  against the real CBOR traits, eight compile-fail cases, and four fuzz harnesses (skip,
  typed, round-trip, deterministic) with seed corpora, clean for 150 s each locally and
  one hour each in the nightly job. The standalone fuzz driver saves the failing input on an
  oracle failure.
- Benchmarks against Glaze (CBOR) and QCBOR with a recorded baseline for deterministic and
  non-deterministic modes; a CBOR compile-time budget.
- `docs/cbor.md`, and CBOR sections in the annotations, types, errors, customization and
  compatibility docs; `docs/design/cbor-spec.md` with a shipped-versus-specified appendix.

### Changed

- The CBOR write spike and `VCODEC_BUILD_SPIKE` are gone; `VCODEC_BUILD_CBOR` (default on)
  gates the CBOR tests, fuzzers and benchmarks.
- `std::chrono` time points and durations are no longer reflected as structs in any format;
  under JSON they have no codec and fail at compile time.
- `std::unique_ptr` is recognised only with the default deleter.
- Type names in diagnostics are rewritten to their standard spellings
  (`std::string`, `std::chrono::sys_seconds`, no `__cxx11`).
- The compile-time baseline for the JSON translation unit was re-recorded on the extended
  core (`bench/compile_time_baseline.txt`).

### Known limitations

See `docs/compatibility.md` and `docs/design/cbor-spec.md` §17. Highlights: map keys must be
text or integers unless a custom codec writes them (`begin_key` / `end_key`); chunked text
cannot be borrowed into `std::string_view`; tags 21–23 are treated as unknown; NaN payloads
are not preserved; length-first (RFC 7049) key order is not offered.

## 0.1.0 — 2026-09-17

First release. A header-only C++26 JSON codec driven by reflection and annotations.

### Added

- JSON encode and decode for structs (private and inherited members included), enums, and
  the standard-library type set: `bool`, all integer types with range checks, `float`,
  `double`, `long double`, `char`, `std::string`, `std::string_view` (borrowing),
  `std::u8string`, `const char*` (encode only), `std::optional`, `std::unique_ptr`,
  `std::shared_ptr`, `std::variant` with a discriminator, `std::pair`, `std::tuple`,
  `std::array`, C arrays, `std::span` (encode only), any range with `push_back`,
  `emplace_back` or `insert`, any range of pairs as a map, and byte strings
  (`std::vector<std::byte>`, `std::array<std::byte, N>`, `std::span<const std::byte>`,
  `std::vector<std::uint8_t>` when annotated).
- Core annotations: `name`, `alias`, `skip`, `skip_serializing`, `skip_deserializing`,
  `skip_if_null`, `skip_if_default`, `required`, `optional`, `default_value`, `flatten`,
  `with<Codec>`, `rename_all` (camel, pascal, snake, kebab, screaming_snake),
  `deny_unknown_fields`, `transparent`, `tag`, `as_integer`.
- JSON annotations: `json::bytes(base64 | base64url | hex)`, `json::as_string`,
  `json::stringify_keys`.
- Options as a structural non-type template parameter: `pretty`, `indent`, `max_depth`,
  `validate_utf8`, `duplicates` (last wins, first wins, error), `errors` (fail fast,
  collect).
- Entry points: `encode`, `encode_append`, `encode_to`, `encode_into`, `decode`,
  `decode_into`, `decode_all`.
- Structured errors (`vcodec::error`) with a root-first member path built during unwinding,
  byte offset, line and column, expected/found, did-you-mean suggestions and candidate
  lists; `render_terse` and `render_framed` (source excerpt with caret).
- Collect-all decoding at object-member granularity.
- Compile-time diagnostics via a consteval audit and P2741 `static_assert` messages, gated on
  every entry point; `json::encodable`, `json::decodable`, `json::borrows`.
- Extension points: `codec_for<T, Format>` (per format, then generic), `describe<T>` for
  annotating types you do not own, sink and reader concepts for custom formats,
  `format_traits` for a format's lowering decisions.
- A format-free core enforced by a layering check, and a throwaway CBOR write spike that the
  cross-format tests assert against.
- Tests: Catch2 suites by layer, JSONTestSuite conformance on both the validator and the
  typed path with recorded `i_` dispositions, property-based round trips, a compile-fail
  suite with a no-cascade guard, three fuzz harnesses (ASan+UBSan), benchmarks against Glaze,
  nlohmann/json and simdjson with a recorded baseline, and an enforced compile-time budget.

### Known limitations

See `docs/compatibility.md`. Highlights: untagged variants, `collect_unknown_fields`,
streaming input, allocator customisation and schema generation are out of scope for 0.1;
`std::string_view` members cannot hold escaped strings; NaN and infinity encode as `null`;
GCC 16 is the only exercised compiler.

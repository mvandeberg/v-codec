# Changelog

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

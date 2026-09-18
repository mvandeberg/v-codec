# The seam: why `core/` knows nothing about formats

`include/vcodec/core/` maps C++ types to and from an abstract data model (see
[data-model.md](data-model.md)). `include/vcodec/json/` implements that model for JSON. The
two meet at three concepts and one traits template, and nothing else:

| Seam element | Defined in | Implemented by a format |
|---|---|---|
| `core::sink` | `core/model.hpp` | `json::writer<Opts>` |
| `core::reader` (+ `seq_cursor`, `map_cursor`) | `core/model.hpp` | `json::reader<Opts>` |
| `core::format_traits<Format>` | `core/model.hpp` | specialised in `json/traits.hpp` |
| `codec_for<T, Format>` | `core/codec_for.hpp` | specialised by users per format |

Three mechanical rules keep it that way, all enforced by `cmake/LayeringCheck.cmake` at
configure time and again as a test:

1. Nothing under `core/` includes `json/`, `cbor/` or `spike/`.
2. Nothing under `include/` includes `spike/`.
3. Only `core/compiler.hpp` contains a compiler `#if`.

`test/core/` builds against an include tree that contains only the format-free headers, so a
JSON dependency in core code or core tests is a build failure rather than a review comment.

## How a format influences core without core knowing the format

Two questions cannot be answered by the type alone and depend on the format:

- Is this `std::vector<std::uint8_t>` a byte string or an array of numbers?
- Can this format represent a byte string, or an integer map key, for this member?

Core asks `format_traits<Format>`, where `Format` comes from the sink's or reader's nested
`format` type. The primary template is the CBOR-shaped default (everything in the model is
native). JSON's specialisation says: `std::byte` ranges are bytes, `std::uint8_t` ranges are
bytes only with `json::bytes`, bytes need `json::bytes`, non-text keys need
`json::stringify_keys`. Both the traversal and the compile-time audit consult the same traits,
so a rejected type is rejected before any traversal is instantiated.

Three sink/reader methods take the member's `field_meta` as a template argument so a format
can read its own annotations: `bytes<F>`, `uint<F>` / `sint<F>` (and the `expect_*<F>`
counterparts), plus `prepared_key<Name>` for compile-time key blobs. Core prefers the
templated form when the sink offers it and falls back to the plain one otherwise. A format
never sees a core annotation it did not ask for, and core never interprets a format's.

## What the CBOR spike found (M3)

The spike (`spike/cbor_write.hpp`) is a write-only RFC 8949 sink. Building it before the
JSON writer, as the spec ordered, surfaced these leaks, each fixed in `core/` before M4:

1. **Map lengths.** JSON never needs a count; CBOR wants one. `begin_map(n)` takes
   `std::optional<std::size_t>`, and core supplies a definite count for a struct unless any
   member is conditionally omitted (`skip_if_null`, `skip_if_default`), in which case it
   passes `nullopt` and the spike emits an indefinite-length map. The schema records
   `type_info::conditional_fields` so this costs nothing at runtime.
2. **Negative integer keys.** The spec's sink had `key(text)` and `key(uint64)`. A
   `std::map<int, …>` has negative keys; `key(int64)` was added.
3. **Signedness.** Core emits `sint(v)` for signed C++ types and `uint(v)` for unsigned ones
   regardless of the value's sign; CBOR collapses non-negative values to major type 0. That
   is a lowering the format performs, so the cross-format test normalises it. Core keeps the
   C++ signedness because it is more information, not less.
4. **Bytes classification is per format**, not per type: the same `std::vector<std::uint8_t>`
   member is bytes in a format that says so and numbers otherwise. Hence `format_traits`.
5. **Tags are one-way in v0.1.** The model has `tag`; no core annotation produces one, and
   JSON drops them. A CBOR `cbor::tag(n)` annotation will live in the format namespace.
6. **The reader needs `save()` / `restore()`.** Two decode features need to re-read input: a
   tagged union whose discriminator is not the first key, and member-granularity recovery in
   collect mode. Both formats in scope are whole-buffer, so this is cheap; the reader
   concept carries it and `docs/compatibility.md` says it may change.
7. **The reader needs an error factory.** Text formats know line and column; binary ones do
   not. Core constructs errors through `r.error(code, offset)` so each format fills what it
   has, and `error::position()` is `std::optional` for exactly this reason.

`test/cross/consistency.cpp` asserts the §11 rules against the spike: the same traversal
produces the same token stream (dumped back out of the CBOR bytes) as the recording sink,
bytes and integer keys are native in CBOR and need lowerings in JSON, definite and
indefinite lengths land where core's knowledge says they should, and encode-side failures
name members identically in both formats.

## What did not leak

- The schema layer (`fields_of`, `schema_of`, `lookup_of`) has no format in it at all.
- Error paths are in C++ member terms on both sides; a future CBOR integer key will render
  as `$.issuer`, not `$.1`, because field identity is the member, not the wire key.
- Compile-time diagnostics are produced by one audit parameterised on the format; the JSON
  messages come from JSON's `format_traits` hints, not from core text.

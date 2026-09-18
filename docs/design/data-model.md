# The data model

The traversal in `core/` does not speak JSON or CBOR. It speaks a small format-neutral
vocabulary of tokens, defined in `<vcodec/core/model.hpp>`, and a format's sink or reader
translates between that vocabulary and bytes. This note records what the vocabulary is, why
it is shaped as it is, what JSON does with the parts it cannot represent, and how CBOR maps
every part natively.

## The kinds

```cpp
namespace vcodec::core {

enum class kind : std::uint8_t {
    null, boolean, uint, sint, real,
    text, bytes,                  // bytes: no JSON representation
    array, map,
    tag,                          // no JSON representation
    undefined,                    // no JSON representation
};

}
```

| Kind | Sink call | Reader call | Payload |
|---|---|---|---|
| `null` | `null()` | `expect_null()` | — |
| `boolean` | `boolean(bool)` | `expect_boolean()` | `bool` |
| `uint` | `uint(std::uint64_t)` | `expect_uint()` | non-negative integer |
| `sint` | `sint(std::int64_t)` | `expect_sint()` | signed integer |
| `real` | `real(double)` | `expect_real()` | binary64 |
| `text` | `text(std::string_view)` | `expect_text()` → `text_ref` | UTF-8 |
| `bytes` | `bytes(std::span<const std::byte>)` | `expect_bytes()` → `bytes_ref` | raw octets |
| `array` | `begin_array(std::optional<std::size_t>)` … `end_array()` | `expect_array()` → cursor | ordered values |
| `map` | `begin_map(std::optional<std::size_t>)`, `key(...)` … `end_map()` | `expect_map()` → cursor | key/value pairs |
| `tag` | `tag(std::uint64_t)` | `expect_tags(list)`, `peek() == kind::tag` (CBOR) | semantic tag on the next value |
| `undefined` | — | `peek()` result; CBOR `0xf7` when not treated as `null` | "nothing to classify", or CBOR's undefined |

Map keys may be `text`, `uint` or `sint`; the sink has `key(std::string_view)`,
`key(std::uint64_t)` and `key(std::int64_t)`, and the map cursor reports `key_kind()` and
offers `key_text()`, `key_uint()`, `key_sint()`. The spec's sink concept listed only text and
unsigned keys; the implementation added the signed overload because a `std::map<int, T>`
with negative keys is real and CBOR represents it natively.

`text_ref` and `bytes_ref` carry the payload plus a `borrowed` flag (does the view alias the
input, or reader scratch?) and, for text, the token's raw length in the input for the caret
in error rendering.

## Why the union of JSON and CBOR

The model is defined as the **union** of what JSON and CBOR can express, and lowering the
parts one format lacks is that format's job. v0.1 shipped only JSON, and JSON cannot
represent four of the model's elements (`bytes`, non-text keys, `tag`, `undefined`), so the
obvious simplification would have been to leave them out until CBOR arrived.

That simplification is the specific mistake the design guards against. A model shaped like
JSON would make CBOR a second-class format later: byte strings would arrive as base64 text
and need un-lowering; integer keys would be strings; tags would have nowhere to go; the
definite/indefinite length distinction would be lost. The seam between `core/` and a format
would have JSON's shape baked in, and JSON-first is exactly the condition under which such a
seam rots fastest.

Two mechanisms keep the seam honest:

- `core/` may not include a format header. `cmake/LayeringCheck.cmake` fails the build on a
  hit, at configure time and as a test.
- Where a format's *annotations* legitimately change how core classifies, keys, orders or
  tags a member, or whether it is representable, core asks `format_traits<Format>` rather
  than knowing the answer. v0.1 had three hooks (`is_bytes`, `can_lower_bytes`,
  `can_lower_key`); v0.2 added the ones CBOR needed (`integer_key`, `accepts_text_key`,
  `member_order`, `expected_tags`, `enum_default_integer`, `bytes_borrowable`, `bulk_range`,
  `accepts_bulk_range`, `check_field`, `check_type`), each with a default that reproduces
  the v0.1 behaviour ([customization.md](../customization.md#the-format_traits-hooks)). The
  defaults are the "everything is representable" shape; JSON specialises the three
  representability hooks, CBOR the protocol ones.

A throwaway CBOR write spike was built before the JSON writer precisely so that the model
was tested against a second format before the first one could bias it; v0.2 replaced the
spike with the real backend, and `test/cross/consistency.cpp` asserts that the same type
produces the same token stream, key set, skips and error paths under both sinks.

## JSON lowerings

What the JSON format does with each element it cannot write natively:

| Model element | JSON behaviour |
|---|---|
| `bytes` | Written as text under the member's `json::bytes(enc)` annotation: `base64` (padded), `base64url` (unpadded) or `hex` (lowercase). **Without the annotation the type does not compile**; the library never guesses an encoding. `std::uint8_t` ranges are arrays of numbers unless the same annotation promotes them. |
| non-text map key | Compile error by default. `json::stringify_keys` on the member opts into decimal strings, parsed strictly on the way back. |
| `uint` above 2⁵³ | Written exactly as a number. `json::as_string` opts into a quoted decimal for consumers that hold numbers as `double`; decoding then accepts either form. |
| `tag` | `writer::tag()` is a no-op: dropped on write. The reader never produces one, so a codec that handles tags sees none under JSON. |
| `undefined` | Nothing encodes to it. `reader::peek()` returns it at end of input or on a byte that cannot start a value; no value is ever built from it. |
| `real` NaN, ±infinity | Written as `null` (JSON has no spelling). Reading `null` into a floating-point type is `type_mismatch`. |

Everything else maps one to one: `null`, `boolean`, `text`, `array`, `map` with text keys,
and the three number kinds as JSON numbers.

## CBOR mapping

CBOR represents every element natively, so this is a mapping to RFC 8949 major types, not a
lowering table (spec §4). The writer is `vcodec::cbor::writer<Opts>` in
`<vcodec/cbor/write.hpp>`, the reader `vcodec::cbor::reader<Opts>` in `<vcodec/cbor/read.hpp>`.

| Model element | CBOR | Notes |
|---|---|---|
| `null` | `f6` | |
| `boolean` | `f4` / `f5` | |
| `uint` | major 0 | shortest head, always |
| `sint` | major 0 when ≥ 0; major 1 with argument `-1 - n` when negative | the signedness collapse below |
| `real` | major 7, additional info 25 / 26 / 27 | preferred serialization: the shortest of half, single, double that round-trips; `cbor::float_width` or `options::floats` pins one. NaN is always `f9 7e00` |
| `text` | major 3 | UTF-8 validated when `validate_utf8`; definite length, or chunked (`7f … ff`) for a `cbor::indefinite` string member under `deterministic = false`; definite strings borrow on read |
| `bytes` | major 2 | native, no annotation; `cbor::byte_string` promotes `uint8_t` ranges; definite strings borrow into `std::span<const std::byte>` |
| `array` | major 4 | definite when core supplies a count or `deterministic` buffers to count one; indefinite (`9f … ff`) for an unsized range or under the `indefinite` option when not deterministic. A `cbor::typed_array` member becomes tag 64–87 + one byte string instead |
| `map` | major 5 | keys `text`, `uint`, `sint` (major 3 / 0 / 1); struct maps in canonical key order always, runtime maps sorted when `deterministic`; other key kinds on input are `type_mismatch` |
| `tag` | major 6 | from `cbor::tag(n)`, from `expected_tags` (tag 1 for time points), from `self_describe` (55799), or from a codec's `s.tag(n)`; unknown tags skipped on read when `ignore_unknown_tags` |
| `undefined` | `f7` | decode only: `null` for optional-like targets when `undefined_as_null` (default), else `type_mismatch` with `found() == "undefined"`. Nothing encodes to it |

Well-formedness the reader enforces beyond the mapping: additional information 28–30 and a
misplaced break are `malformed_item`; simple values outside 20–23 are
`unsupported_simple_value`; a chunk of an indefinite string must be a definite string of the
same major type. [cbor.md](../cbor.md) is the user-facing account.

## Definite and indefinite lengths

`begin_array` and `begin_map` take `std::optional<std::size_t>`: a value is a definite
length, `nullopt` is indefinite. The traversal supplies a length whenever it knows one
cheaply:

- a `std::ranges::sized_range` sequence or map: its `size()`;
- a `std::array`, C array, `std::pair`, `std::tuple`: the static size;
- a struct: the number of serialised members (plus one for an internally tagged variant's
  discriminator). When a member carries `skip_if_null` or `skip_if_default` the count
  depends on the values, and the traversal evaluates the skip predicates in a first pass to
  compute it, so a struct always has a definite count and a deterministic CBOR writer never
  has to buffer one (v0.1 emitted an indefinite length here; the CBOR work changed it);
- a `transparent` struct emits no map at all;
- an unsized range (a `std::forward_list`, a filtered view): indefinite.

JSON ignores the distinction; `writer::begin_array` and `begin_map` discard the argument. The
CBOR writer uses it: with a count it writes a definite head (`a2 …`); without one it either
writes an indefinite head (`bf … ff`) when `deterministic` is off or buffers the body to
count it when on. `test/cross/consistency.cpp` checks that `Point{}` is `a2`, that a
`Server` with and without its `skip_if_null` member present is `a9` and `aa`, and that a
`std::forward_list` is `9f … ff` under the non-deterministic writer. The distinction costs
nothing on the JSON side and is exactly the kind of information that could not be recovered
later had the model been designed from JSON alone.

On the reader side both cursors offer `remaining()`, which returns `nullopt` for JSON (no
length prefix) and for an indefinite-length CBOR container, and the remaining count for a
definite-length one.

## Signedness: `uint` and `sint` carry the C++ type, not the value

The traversal emits `uint` for values of unsigned C++ types and `sint` for values of signed
C++ types, regardless of the value: an `int` holding `5` is `sint(5)`, a `std::uint8_t`
holding `5` is `uint(5)`. Enums follow their underlying type under `as_integer`. Map keys do
the same through `key(std::uint64_t)` and `key(std::int64_t)`.

This is deliberate. The model records what the type system knew; whether that distinction
matters is the format's call:

- **JSON** does not care. `write_uint` and `write_sint` both produce decimal digits, and
  `sint(5)` and `uint(5)` are indistinguishable in the output.
- **CBOR** collapses them: `writer::sint()` writes major type 0 for non-negative values
  and major type 1 (argument `-1 - n`) for negative ones, so `sint(5)` and `uint(5)` both
  become `05` and `sint(-1)` becomes `20`. The same collapse applies to integer keys
  (`key(std::int64_t)`). That is a lowering the format performs; `core/` does not
  pre-collapse because a format that *does* distinguish (a typed binary format with explicit
  `i32`/`u32` headers, say) would then have lost the information.
  `test/cross/consistency.cpp` normalises non-negative `sint` to `uint` before comparing the
  two sinks, which documents the collapse as expected.

On the decode side the reader classifies from the *input*: the JSON reader reports a
non-negative integer literal as `uint`, a negative one as `sint`, and anything with a
fraction or exponent (or too large for 64 bits) as `real`; the CBOR reader reports major 0
as `uint`, major 1 as `sint`, and additional info 25–27 as `real`. A major 1 argument above
`INT64_MAX - 1` cannot be a `std::int64_t` and is `out_of_range` with the exact literal
(`-18446744073709551616`). The type-directed decoder then
reconciles that with the target type: `expect_sint()` accepts a `uint` item that fits in
`int64_t`; `expect_uint()` rejects a `sint` item as `out_of_range`; `expect_real()` accepts
all three. The narrowing to the member's own width (`std::uint8_t`, `std::int16_t`, ...)
happens in `core/build.hpp`, format-independently, so every format gets the same
`value 512 out of range for std::uint8_t` message.

## The field context

One more piece of the seam: every sink and reader call made on behalf of a struct member
happens under a `core::field_meta` value `F` that carries the member's annotations. A format
that wants to honour a per-member annotation (JSON's `as_string`, `bytes`; CBOR's
`float_width`, `indefinite`, `typed_array`) provides the templated hooks `uint<F>`,
`sint<F>`, `real<F>`, `text<F>`, `bytes<F>`, `write_range<F>`, `prepared_key<Name>` /
`prepared_key<Key>` on the sink and `expect_uint<F>`, `expect_sint<F>`, `expect_bytes<F>`,
`read_range<F>` on the reader; core prefers them when present and falls back to the plain
calls otherwise. Codecs get the same context through `encode<F>` / `decode<F>`
([customization.md](../customization.md#field-aware-codecs)). `core::no_field` is the empty
context used for top-level values. This is how a format's vocabulary reaches the wire without
core knowing the vocabulary exists.

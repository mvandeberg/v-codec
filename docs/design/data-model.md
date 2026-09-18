# The data model

The traversal in `core/` does not speak JSON. It speaks a small format-neutral vocabulary of
tokens, defined in `<vcodec/core/model.hpp>`, and a format's sink or reader translates between
that vocabulary and bytes. This note records what the vocabulary is, why it is shaped as it
is, and what JSON does with the parts it cannot represent.

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
| `tag` | `tag(std::uint64_t)` | — (never produced by JSON) | semantic tag on the next value |
| `undefined` | — | `peek()` result only | "nothing to classify" |

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
parts one format lacks is that format's job. v0.1 ships only JSON, and JSON cannot represent
four of the model's elements (`bytes`, non-text keys, `tag`, `undefined`), so the obvious
simplification would have been to leave them out until CBOR arrives.

That simplification is the specific mistake the design guards against. A model shaped like
JSON would make CBOR a second-class format later: byte strings would arrive as base64 text
and need un-lowering; integer keys would be strings; tags would have nowhere to go; the
definite/indefinite length distinction would be lost. The seam between `core/` and a format
would have JSON's shape baked in, and JSON-first is exactly the condition under which such a
seam rots fastest.

Two mechanisms keep the seam honest:

- `core/` may not include a format header. `cmake/LayeringCheck.cmake` fails the build on a
  hit, at configure time and as a test.
- Where a format's *annotations* legitimately change how core classifies a member or whether
  it is representable, core asks `format_traits<Format>` (`is_bytes`, `can_lower_bytes`,
  `can_lower_key`) rather than knowing the answer. The primary template is the CBOR-shaped
  default: everything is representable. JSON specialises it.

The throwaway CBOR write spike (`spike/cbor_write.hpp`) was built before the JSON writer
precisely so that the model was tested against a second format before the first one could
bias it; `test/cross/consistency.cpp` asserts that the same type produces the same token
stream, key order, skips and error paths under both sinks.

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

## Definite and indefinite lengths

`begin_array` and `begin_map` take `std::optional<std::size_t>`: a value is a definite
length, `nullopt` is indefinite. The traversal supplies a length whenever it knows one
cheaply:

- a `std::ranges::sized_range` sequence or map: its `size()`;
- a `std::array`, C array, `std::pair`, `std::tuple`: the static size;
- a struct: the number of serialised members (plus one for an internally tagged variant's
  discriminator), **unless** any member carries `skip_if_null` or `skip_if_default`, in which
  case the count depends on the values and the map is indefinite;
- a `transparent` struct emits no map at all;
- an unsized range (a `std::forward_list`, a filtered view): indefinite.

JSON ignores the distinction; `writer::begin_array` and `begin_map` discard the argument. The
CBOR spike uses it: a definite map is `0xA2 …`, an indefinite one `0xBF … 0xFF`, and
`test/cross/consistency.cpp` checks that `Point{}` (two plain members) is definite while a
struct with a `skip_if_null` member is indefinite. The distinction costs nothing on the JSON
side and is exactly the kind of information that could not be recovered later had the model
been designed from JSON alone.

On the reader side both cursors offer `remaining()`, which returns `nullopt` for JSON (no
length prefix) and would return the remaining count for a definite-length CBOR container.

## Signedness: `uint` and `sint` carry the C++ type, not the value

The traversal emits `uint` for values of unsigned C++ types and `sint` for values of signed
C++ types, regardless of the value: an `int` holding `5` is `sint(5)`, a `std::uint8_t`
holding `5` is `uint(5)`. Enums follow their underlying type under `as_integer`. Map keys do
the same through `key(std::uint64_t)` and `key(std::int64_t)`.

This is deliberate. The model records what the type system knew; whether that distinction
matters is the format's call:

- **JSON** does not care. `write_uint` and `write_sint` both produce decimal digits, and
  `sint(5)` and `uint(5)` are indistinguishable in the output.
- **CBOR** collapses them: the spike's `sint()` writes major type 0 for non-negative values
  and major type 1 for negative ones, so `sint(5)` and `uint(5)` both become `0x05`. That is
  a lowering the format performs; `core/` does not pre-collapse because a format that *does*
  distinguish (a typed binary format with explicit `i32`/`u32` headers, say) would then have
  lost the information. `test/cross/consistency.cpp` normalises non-negative `sint` to `uint`
  before comparing the two sinks, which documents the collapse as expected.

On the decode side the reader classifies from the *input*: the JSON reader reports a
non-negative integer literal as `uint`, a negative one as `sint`, and anything with a
fraction or exponent (or too large for 64 bits) as `real`. The type-directed decoder then
reconciles that with the target type: `expect_sint()` accepts a `uint` item that fits in
`int64_t`; `expect_uint()` rejects a `sint` item as `out_of_range`; `expect_real()` accepts
all three. The narrowing to the member's own width (`std::uint8_t`, `std::int16_t`, ...)
happens in `core/build.hpp`, format-independently, so every format gets the same
`value 512 out of range for std::uint8_t` message.

## The field context

One more piece of the seam: every sink and reader call made on behalf of a struct member
happens under a `core::field_meta` value `F` that carries the member's annotations. A format
that wants to honour a per-member annotation (JSON's `as_string`, `bytes`) provides the
templated hooks `uint<F>`, `sint<F>`, `bytes<F>`, `prepared_key<Name>` on the sink and
`expect_uint<F>`, `expect_sint<F>`, `expect_bytes<F>` on the reader; core prefers them when
present and falls back to the plain calls otherwise. `core::no_field` is the empty context
used for top-level values. This is how a format's vocabulary reaches the wire without core
knowing the vocabulary exists.

# Supported types

vcodec classifies a member's type with concepts, not with a list of specialisations, so a
user container that has the right shape works without any library change. The concepts live
in `<vcodec/core/concepts.hpp>` (namespace `vcodec::core`); classification is first match in
the order the traversal consults them, which is the order of this page after the codec hooks
(`with<Codec>`, `codec_for<T, Format>`, `codec_for<T, void>`, then a sink's
`write_prepared<T>`; see [customization.md](customization.md)).

A type outside this set is a compile error naming the member and the remedies:

```
static assertion failed: Config::database::pool::handle (type void*)
  has no codec. Provide vcodec::codec_for<void*>, mark the member
  [[=vcodec::skip]], or give it [[=vcodec::with<YourCodec>]].
```

`cv` and reference qualifiers are stripped before classification everywhere.

The classification is format-independent; only the wire shape differs. Each table below
gives the JSON and the CBOR shape side by side, and the per-category notes say where CBOR
adds something (borrowing into spans, integer keys, typed arrays, time points). The CBOR
shapes are described in full in [cbor.md](cbor.md#wire-shapes).

## Scalars

| Type | Concept | JSON | CBOR |
|---|---|---|---|
| `bool` | `boolean_type` | `true` / `false` | `f5` / `f4` |
| `char` | `char_type` | one-character string: `'x'` ↔ `"x"` | one-character text string `61 78` |
| signed integers | `signed_integral_type` | number | major 0 when ≥ 0, major 1 when negative |
| unsigned integers | `unsigned_integral_type` | number | major 0 |
| `float`, `double`, `long double` | `floating_type` | number | major 7, shortest of half / single / double that round-trips |
| enumerations | `enum_type` | string by name, or number with `as_integer` | integer by underlying value, or text with `as_text` |
| `std::byte` | `byte_type` | only as an element of a byte-like range | same |

### Integers

Every standard integer type is supported, including `signed char`, `unsigned char` and the
`<cstdint>` aliases. `bool` and `char` have their own rules above. The code-unit types
`wchar_t`, `char8_t`, `char16_t` and `char32_t` are *not* integers to vcodec and have no
codec: a member of one of those types is rejected at compile time ("has no codec") rather
than silently encoded as a number. Give it a `codec_for` or `with<Codec>` if you need it.

Encoding writes the exact decimal value; 64-bit values above 2⁵³ are emitted exactly, not
rounded. Use [`json::as_string`](annotations.md#jsonas_string) for consumers that cannot hold
them. CBOR writes the shortest head for the magnitude (`1000u` → `19 03e8`) and represents
every 64-bit value; a negative CBOR integer below `INT64_MIN` (`3b ffffffffffffffff`) is
`out_of_range` with the exact literal (`value -18446744073709551616 out of range for std::int64_t`).

Decoding accepts only integer literals: `1.0` and `1e2` are `type_mismatch`
(`expected integer, found number`); in CBOR a float item into an integer member is likewise
`expected integer, found float`. The literal must fit the member's type in both
directions, else `out_of_range`:

```
error: value 512 out of range for std::uint8_t
error: value -1 out of range for std::uint32_t
```

Messages spell integer types by their fixed-width name (`std::uint8_t`, `std::int64_t`),
whatever alias the member was declared with, because GCC does not preserve the alias
(`docs/design/m0-spellings.md`, finding 11). Plain `int` stays `int`.

### Floating point

All three types are written through `double` with the shortest representation that round-trips
(`std::to_chars`): `1.5` → `1.5`, `1e300` → `1e+300`, `-0.0` → `-0`. Consequences:

- `float` is widened, so `0.1f` writes `0.10000000149011612`. It decodes back to the same
  `float`.
- `long double` is narrowed to `double`; `1.0L / 3` writes `0.3333333333333333`. Precision
  beyond `double` is lost.
- **NaN and infinities are written as `null`**, since JSON has no spelling for them.
  Decoding `null` into a floating-point member is `type_mismatch` (`expected number, found
  null`); a round trip of a NaN therefore fails on the way back. Put the member in a
  `std::optional` if that is a possible value.

Decoding accepts any JSON number, including integer literals (`7` → `7.0`) and integer
literals too large for 64 bits (`18446744073709551616` → `1.8446744073709552e19`). A
literal whose magnitude overflows `double` (`1e400`) is `out_of_range` for `double`;
underflow (`1e-400`) is `0.0`, not an error.

**CBOR** has three float widths and uses the shortest that round-trips (`1.0` → `f9 3c00`,
`100000.0` → `fa 47c35000`, `1.1` → `fb 3ff199999999999a`); `float` members widen to
`double` first so they are always half or single. NaN and infinities have a spelling
(`f9 7e00`, `f9 7c00`, `f9 fc00`) and round-trip. `cbor::float_width` pins a width per
member. Decoding accepts any width and integers ([cbor.md](cbor.md#floats)).

### Enumerations

```cpp
enum class Color { red [[=vcodec::name("bright-red")]], green, hidden [[=vcodec::skip]] };
enum class [[=vcodec::as_integer]] Level : std::int8_t { low = -1, mid = 0, high = 1 };
```

In JSON an enumerator is written by default as a string: its `name`, else the enum's
`rename_all` transformation of the identifier, else the identifier. Decoding matches names
and `alias`es exactly; anything else is `unknown_enumerator` with the valid names listed.
With `as_integer` on the enum type (or on the member) the underlying integer is used in both
directions and the value must equal a non-skipped enumerator.

**In CBOR the default is the integer** (`Color::green` → `01`); `as_text` on the enum type or
the member restores names. The annotations mean the same in both formats; only the default
differs ([annotations.md](annotations.md#enum-level-annotations)).

Encoding a value that is a skipped enumerator, or no enumerator at all, throws
`encode_error` ([errors.md](errors.md#encode-side-failures)).

## Strings

| Type | Concept | Encode | Decode |
|---|---|---|---|
| `std::string`, `std::u8string` | `owned_string` | yes | yes, copies |
| `std::string_view`, `std::u8string_view` | `string_view_type` | yes | yes, **borrows** (CBOR: definite-length strings only) |
| `const char*`, `char*` | `c_string` | yes (`nullptr` → `""`) | compile error |

In CBOR every string is a major type 3 text string (`"IETF"` → `64 49455446`), validated
as UTF-8 in both directions when `validate_utf8` is on; there is no escaping. An
indefinite-length (chunked) text string is assembled and copied into owning strings and, for
a `std::string_view`, is `escape_in_borrowed_string` — the same error as JSON escapes,
because the meaning (a view cannot alias assembled text) is the same.

JSON encoding escapes `"`, `\` and control characters below U+0020 (`\b \f \n \r \t`, otherwise
`\u00XX` with uppercase hex). `/`, DEL and non-ASCII bytes are written raw. Strings are
validated as UTF-8 first when `options::validate_utf8` is on (the default); a malformed
string throws `encode_error` with code `invalid_utf8`. `u8string` bytes are written as they
are.

JSON decoding handles every escape including `\uXXXX` surrogate pairs; lone surrogates are
`invalid_escape`, raw control characters are `unexpected_token`, malformed UTF-8 is
`invalid_utf8` (when validating). Whitespace between tokens is exactly space, `\n`, `\r`
and `\t`.

Decoding into `const char*` is rejected at compile time:

```
static assertion failed: Row::label (const char*)
  is encode-only and cannot be decoded into. Use std::string, or mark it [[=vcodec::skip_deserializing]].
```

`std::wstring`, `std::u16string`, `std::u32string` and their views have no codec in v0.1:
there is no defined transcoding, so a member of one of those types is rejected at compile
time ("has no codec") rather than encoded as an array of code units. Give such a member a
`codec_for` or `with<Codec>` that transcodes to UTF-8 if you need it.

### Borrowing

A `std::string_view` member decodes by pointing into the input buffer. The rules:

1. **The result's lifetime is bound to the input.** `decode` takes a `std::string_view`
   (JSON) or a `std::span<const std::byte>` (CBOR); the caller keeps that buffer alive as long
   as the decoded value is used. `vcodec::json::borrows<T>` / `vcodec::cbor::borrows<T>` is
   `true` for any type that borrows somewhere inside (a `std::string_view` member, an
   element, a map key or value; in CBOR also a `std::span<const std::byte>`).
2. **A temporary `std::string` cannot be the input for a borrowing type.** The overload is
   deleted with a message:
   ```
   this type borrows from the input (it has std::string_view members); pass a buffer that outlives the result
   ```
   Non-borrowing types accept a temporary `std::string` (it converts to `string_view` for the
   duration of the call, which is all they need).
3. **A borrowed string containing escapes is an error**, `errc::escape_in_borrowed_string`,
   with the member named and the fix suggested. Unescaping needs storage the library does not
   own; allocating silently would defeat the point of the view.
   ```
   error: string contains escapes but S::v borrows from the input
     --> $.v
      |  use std::string instead of std::string_view
   ```
   The same rule applies to `std::string_view` map keys
   (`use std::string keys instead of std::string_view`).
4. `decode_into` with a borrowing type has the same lifetime rule: the object's views alias
   the buffer passed to that call.

**`std::span<const std::byte>` borrows in CBOR, not in JSON.** Under JSON it is encode-only
(there is nothing to borrow *into*, since base64 must be decoded first). Under CBOR a
definite-length byte string is a contiguous run of input bytes, so the span aliases the
input exactly as a `std::string_view` does, with rules 1, 2 and 4 above; a chunked
(indefinite-length) byte string is `errc::indefinite_in_borrowed_string`
(`use std::vector<std::byte> instead of std::span<const std::byte>`). A type with such a
member is therefore `cbor::decodable` and not `json::decodable`
(`vcodec::cbor_only<T>`).

## Bytes

| Type | Bytes? | JSON | CBOR |
|---|---|---|---|
| `std::vector<std::byte>`, `std::array<std::byte, N>`, any contiguous range of `std::byte` | always (`byte_like`) | needs `json::bytes(enc)` | major type 2 (`44 01020304`) |
| `std::span<const std::byte>` | always | encode-only, with `json::bytes` | major type 2; decodes by **borrowing** |
| `std::vector<std::uint8_t>`, `std::vector<unsigned char>`, other contiguous `uint8_t` ranges (`contiguous_of_uint8`) | only when promoted | with `json::bytes(...)`; otherwise an array of numbers | with `cbor::byte_string`; otherwise an array of integers |

A byte-like member must say how JSON should spell it; there is never a silent guess:

```cpp
struct Claims { [[=vcodec::json::bytes(vcodec::json::base64)]] std::vector<std::byte> nonce; };
// {"nonce":"aGk="}
```

Without the annotation:

```
static assertion failed: Claims::nonce (std::vector<std::byte>)
  is byte-like but has no JSON lowering.
  Add [[=vcodec::json::bytes(vcodec::json::base64)]], or mark it [[=vcodec::skip]].
```

Encodings and their decode rules are under [`json::bytes`](annotations.md#jsonbytes). A
`std::array<std::byte, N>` must decode to exactly `N` bytes
(`expected byte string of length 2, found byte string of length 3`). Decoding into a byte
container needs `assign(first, last)` or `push_back`; under JSON `std::span` is rejected
with the encode-only message (`Use std::vector<std::byte>`). CBOR needs no annotation at
all (`is_bytes` and `can_lower_bytes` are the CBOR-shaped defaults), so the same type
compiles under CBOR and fails under JSON until it gets `json::bytes`.

## Optional-like

| Type | Concept |
|---|---|
| `std::optional<T>`, `std::unique_ptr<T>`, `std::shared_ptr<T>` | `optional_like` |

Empty encodes as `null`; engaged encodes as the value. `null` decodes to empty; any other
value is decoded into a freshly emplaced `T` (`std::make_unique` / `std::make_shared` for the
pointers). A member of optional-like type is optional on input by default
([annotations.md](annotations.md#required-and-optional)); `skip_if_null` omits it on output
when empty. Field annotations on the member (`json::bytes`, `json::as_string`, ...) apply to
the wrapped value.

Nested optionals collapse: `std::optional<std::optional<int>>` encodes both "outer empty"
and "inner empty" as `null`, and `null` decodes to outer-empty.

**Smart pointers carry no identity.** Two `shared_ptr`s to the same object encode as two
copies of the value; decoding produces two separate objects. There is no deduplication and
no reference tracking. A cycle through pointers is not detected as such: encoding recurses
until `options::max_depth` (default 256) and then throws `encode_error` with code
`depth_exceeded` and a member path (`next.next.next...`, truncated to 16 steps):

```
nesting depth exceeds max_depth (256); a cycle through smart pointers is the usual cause
```

## Tagged variants

`std::variant<...>` (`variant_like`) is supported only as a struct member with a
discriminator, supplied by `tag("key")` on the struct or on the member
([annotations.md](annotations.md#tag)). The two wire shapes:

```cpp
struct [[=vcodec::name("circle")]] Circle { double radius = 0; };
struct [[=vcodec::name("rect")]]   Rect   { double w = 0, h = 0; };
struct [[=vcodec::tag("kind")]]    Shape  { std::variant<Circle, Rect, int> value; };
```

| Alternative | Shape | Example |
|---|---|---|
| a struct (traversed by reflection, not `transparent`) | internally tagged: `{tag: name, ...members}` | `{"value":{"kind":"circle","radius":1.5}}` |
| anything else (scalars, strings, containers, transparent structs) | externally tagged: `{tag: name, "value": v}` | `{"value":{"kind":"int","value":7}}` |

The alternative name is the type's `name("...")` if it has one, else its identifier. The
discriminator may appear anywhere in the object on input. A top-level `std::variant` (not a
member) has no place to carry a tag and does not compile. Untagged variants are not supported.

## Tuple-like

`std::pair<A, B>` and `std::tuple<Ts...>` (`tuple_like`) encode as fixed-length arrays,
element by element: `std::pair<std::string, int>{"a", 1}` → `["a",1]`. Decoding requires
exactly the right number of elements (`expected array of 2 elements, found array of 3
elements`). Element annotations propagate from the member to every element.

## Fixed arrays and spans

`std::array<T, N>` and `T[N]` (`fixed_array`) encode as arrays and decode from arrays of
exactly `N` elements. `std::span<T, N>` (`span_like`) encodes as an array and is encode-only:

```
static assertion failed: Frame::samples (std::span<const int>)
  is encode-only and cannot be decoded into. Use std::vector, or mark it [[=vcodec::skip_deserializing]].
```

(`std::array<std::byte, N>` and `std::span<const std::byte>` are bytes, above, not arrays.)

In CBOR, a `std::array` or `T[N]` of 8–64-bit integers, `float` or `double` also accepts an
RFC 8746 typed array of exactly `N` elements on decode, and is written as one with
`cbor::typed_array` ([Typed arrays](#typed-arrays-cbor)).

## Sequences and custom containers

```cpp
template<class T> concept sequence =
    std::ranges::input_range<bare<T>> && !string_like<T> && !byte_like<T>
    && !map_like<T> && !fixed_array<T> && !span_like<T>;

template<class R>
concept appendable_range = sequence<R> && requires(bare<R>& r, range_value<R>&& v) {
    requires std::default_initializable<range_value<R>>;
    requires requires { r.push_back(std::move(v)); }
          || requires { r.emplace_back(std::move(v)); }
          || requires { r.insert(r.end(), std::move(v)); }
          || requires { r.insert(std::move(v)); };
};
```

Any input range that is not one of the more specific shapes encodes as a JSON array:
`std::vector`, `std::list`, `std::deque`, `std::set`, `std::forward_list`, views, and your
own containers. A sized range gives the data model a definite length; an unsized one does
not, which JSON ignores.

Decoding needs `appendable_range`: a default-initialisable element type and one of
`push_back`, `emplace_back`, `insert(end, v)` or `insert(v)` (tried in that order). If the
container has `clear()`, it is called first. A range without an append operation is a compile
error:

```
… is a range without push_back, emplace_back or insert, so it cannot be decoded into.
Use a container with one of those, or mark it [[=vcodec::skip_deserializing]].
```

This is a container never named in the library that round-trips in the test suite:

```cpp
template<class T>
struct ring {
    std::vector<T> v;
    auto begin() const { return v.begin(); }
    auto end() const { return v.end(); }
    auto begin() { return v.begin(); }
    auto end() { return v.end(); }
    std::size_t size() const { return v.size(); }
    void push_back(T t) { v.push_back(std::move(t)); }
    void clear() { v.clear(); }
};
```

Elements are decoded one at a time into a temporary and moved in; an element failure carries
its index in the path (`$.items[3]`).

### Typed arrays (CBOR)

A contiguous, sized range of 8–64-bit integers, `float` or `double` (`std::vector`,
`std::array`, `T[N]`, `std::span`) has a bulk form in CBOR, RFC 8746: one tag naming the
element type, signedness and endianness, then one byte string holding the elements in
memory order.

```cpp
struct A { [[=vcodec::cbor::typed_array]] std::vector<std::uint16_t> u16{1, 2}; std::vector<std::uint16_t> plain{1}; };
// u16:   d845 44 0100 0200      tag 69 (uint16, little-endian), 4 bytes
// plain: 81 01                  an ordinary array
```

Encoding uses the bulk form only where `cbor::typed_array` asks for it, in the host's native
endianness, with one `memcpy`. Decoding, with `options::accept_typed_arrays` (the default),
accepts a typed array of *any* width, signedness and endianness into *any* such range,
annotated or not, converting element by element (or copying when the representation
matches); values that do not fit are `out_of_range`, a float typed array into an integer
range is `type_mismatch`, and a byte string that is not a whole number of elements is
`malformed_item`. The annotated member accepts a plain array too. `std::vector<bool>` and
non-contiguous ranges cannot be typed arrays (compile error). JSON has no equivalent; the
annotation is ignored there and the member is an ordinary array.

## Maps

```cpp
template<class T> concept pair_like = /* tuple_size == 2, not std::array */;
template<class K> concept map_key  = string_like<K> || integral_type<K>;
template<class T> concept map_like =
    std::ranges::input_range<bare<T>> && !string_like<T>
    && pair_like<range_value<T>> && map_key<std::tuple_element_t<0, range_value<T>>>;
```

Any range whose elements are pairs with a string-like or integral first element is a map and
encodes as a JSON object: `std::map`, `std::unordered_map`, `std::multimap`, and also
**`std::vector<std::pair<std::string, T>>`**, which is an object, not an array of two-element
arrays. Wrap the pair in a struct if you want the array form.

Under JSON, integral keys need `[[=vcodec::json::stringify_keys]]`
([annotations.md](annotations.md#jsonstringify_keys)); without it the type is a compile
error. Under CBOR integral keys are native, negative ones included
(`std::map<int, std::string>{{-1, "m"}, {7, "s"}}` → `a2 20 616d 07 6173`), and a
`std::map<int, T>` without the JSON annotation is `vcodec::cbor_only`. String-view keys
borrow like string-view values. A CBOR map whose keys are neither text nor integers (a
boolean key, say) is `type_mismatch` against the key type.

Decoding needs `insertable_map`: `emplace(k, v)`, `insert(pair)`, `push_back(pair)` or
`emplace_back(k, v)`. `clear()` is called first if present. Values are decoded into a
temporary and then inserted, so a value failure carries the key in the path (`$.groups.g`).

Duplicate keys in a container with unique keys (`std::map`, `std::unordered_map`, anything
whose `emplace` reports success) follow `options::duplicates` exactly as struct members do:
`last_wins` (the default) decodes the later value over the earlier one, `first_wins` skips
the later value, and `error` reports `duplicate_key` with the key in the path. A
`std::multimap` or a range of pairs is not keyed and keeps every occurrence, in order.

Under deterministic CBOR (the default) a runtime map is buffered and its entries sorted by
encoded key on encode, and two entries with the same key throw `encode_error`
([cbor.md](cbor.md#structs-never-buffer-runtime-maps-do)); on decode with
`require_deterministic`, keys out of canonical order or repeated are `non_deterministic`.

## Classes

Anything that is a class, is not one of the shapes above, and is not a union is traversed by
reflection (`reflectable_class`): every non-static data member in declaration order, public
or private, with base-class members first (in base declaration order). Annotations are read
from the source or from a `describe<T>` specialisation
([customization.md](customization.md#describet)). Not supported, each a compile error with a
message: virtual bases, anonymous unions and anonymous members, more than 16 annotations on
a member, flatten/inheritance nesting deeper than 8.

Recursive types work. A tree node holding a `std::vector<Node>` or `std::unique_ptr<Node>`
children is audited once and traversed as deep as the data goes, subject to `max_depth`.

A class with a `codec_for` specialisation is never reflected; a member with `with<Codec>` is
never classified. Standard library classes that are not in the table above (a
`std::filesystem::path`, a `std::chrono::time_point`) are reflected like any other class,
which is rarely what you want; give them a codec.

### `std::chrono::sys_time` (CBOR only)

CBOR ships `codec_for<std::chrono::sys_time<Duration>, cbor::format>`
(`<vcodec/cbor/tags.hpp>`): tag 1 with epoch seconds as an integer, or as a double when the
value is not whole seconds; with `[[=vcodec::cbor::tag(0)]]` on the member, tag 0 with RFC
3339 text. Decoding accepts integer, float and (under tag 0) text payloads and requires the
matching tag ([cbor.md](cbor.md#time-points)).

```cpp
struct Event { std::chrono::sys_seconds when{ std::chrono::seconds(1363896240) }; };
// CBOR: a1 647768656e c1 1a 514b67b0        JSON: {"when":{"__d":{"__r":1363896240}}}  (reflected; give it a codec)
```

Under JSON there is no codec, so the time point is reflected through its private duration
member as shown; that compiles, but it is an accident of the standard library's layout, not
a wire format. `std::chrono::duration` is a number in its own units in both formats.

## Summary of the classification order

For a type `T` with field context `F`, the traversal takes the first that applies:

1. `F` carries `with<Codec>` → `Codec`
2. `codec_for<T, Format>` is defined → it
3. `codec_for<T, void>` is defined → it
4. the sink has `write_prepared<T>` → it (encode only)
5. `bool`, `char`, integers, floating point, enums
6. strings (`owned_string`, `string_view_type`, `c_string`)
7. `optional_like`
8. `variant_like`
9. bytes, as decided by the format (`format_traits<Format>::is_bytes<F, T>()`: `std::byte`
   ranges everywhere, plus `uint8_t` ranges under `json::bytes` or `cbor::byte_string`)
10. `map_like`
11. `tuple_like`
12. `fixed_array`, `span_like`, `sequence` (decode: `fixed_array`, then `appendable_range`);
    before the element-wise path, a format's bulk-range hook when it asks for it
    (CBOR typed arrays)
13. `reflectable_class`
14. otherwise: compile error, "has no codec"

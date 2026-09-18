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

## Scalars

| Type | Concept | JSON |
|---|---|---|
| `bool` | `boolean_type` | `true` / `false` |
| `char` | `char_type` | one-character string: `'x'` ↔ `"x"` |
| signed integers | `signed_integral_type` | number |
| unsigned integers | `unsigned_integral_type` | number |
| `float`, `double`, `long double` | `floating_type` | number |
| enumerations | `enum_type` | string by name, or number with `as_integer` |
| `std::byte` | `byte_type` | only as an element of a byte-like range |

### Integers

Every standard integer type is supported, including `signed char`, `unsigned char` and the
`<cstdint>` aliases. `bool` and `char` have their own rules above. The code-unit types
`wchar_t`, `char8_t`, `char16_t` and `char32_t` are *not* integers to vcodec and have no
codec: a member of one of those types is rejected at compile time ("has no codec") rather
than silently encoded as a number. Give it a `codec_for` or `with<Codec>` if you need it.

Encoding writes the exact decimal value; 64-bit values above 2⁵³ are emitted exactly, not
rounded. Use [`json::as_string`](annotations.md#jsonas_string) for consumers that cannot hold
them.

Decoding accepts only integer literals: `1.0` and `1e2` are `type_mismatch`
(`expected integer, found number`). The literal must fit the member's type in both
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

### Enumerations

```cpp
enum class Color { red [[=vcodec::name("bright-red")]], green, hidden [[=vcodec::skip]] };
enum class [[=vcodec::as_integer]] Level : std::int8_t { low = -1, mid = 0, high = 1 };
```

By default an enumerator is written as a string: its `name`, else the enum's `rename_all`
transformation of the identifier, else the identifier. Decoding matches names and `alias`es
exactly; anything else is `unknown_enumerator` with the valid names listed. With
`as_integer` on the enum type the underlying integer is used in both directions and the value
must equal a non-skipped enumerator.

Encoding a value that is a skipped enumerator, or no enumerator at all, throws
`encode_error` ([errors.md](errors.md#encode-side-failures)).

## Strings

| Type | Concept | Encode | Decode |
|---|---|---|---|
| `std::string`, `std::u8string` | `owned_string` | yes | yes, copies |
| `std::string_view`, `std::u8string_view` | `string_view_type` | yes | yes, **borrows** |
| `const char*`, `char*` | `c_string` | yes (`nullptr` → `""`) | compile error |

Encoding escapes `"`, `\` and control characters below U+0020 (`\b \f \n \r \t`, otherwise
`\u00XX` with uppercase hex). `/`, DEL and non-ASCII bytes are written raw. Strings are
validated as UTF-8 first when `options::validate_utf8` is on (the default); a malformed
string throws `encode_error` with code `invalid_utf8`. `u8string` bytes are written as they
are.

Decoding handles every JSON escape including `\uXXXX` surrogate pairs; lone surrogates are
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

1. **The result's lifetime is bound to the input.** `decode` takes a `std::string_view`; the
   caller keeps that buffer alive as long as the decoded value is used. `vcodec::json::borrows<T>`
   is `true` for any type that borrows somewhere inside (a `std::string_view` member, an
   element, a map key or value).
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

`std::span<const std::byte>` is encode-only in v0.1 (there is nothing to borrow *into* on the
JSON side, since base64 must be decoded). It will borrow the way views do once a binary
format lands.

## Bytes

| Type | Bytes? |
|---|---|
| `std::vector<std::byte>`, `std::array<std::byte, N>`, any contiguous range of `std::byte` | always (`byte_like`) |
| `std::span<const std::byte>` | always; encode-only |
| `std::vector<std::uint8_t>`, `std::vector<unsigned char>`, other contiguous `uint8_t` ranges (`contiguous_of_uint8`) | only with `json::bytes(...)`; otherwise an array of numbers |

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
container needs `assign(first, last)` or `push_back`; `std::span` is rejected with the
encode-only message (`Use std::vector<std::byte>`).

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

Integral keys need `[[=vcodec::json::stringify_keys]]`
([annotations.md](annotations.md#jsonstringify_keys)); without it the type is a compile
error. String-view keys borrow like string-view values.

Decoding needs `insertable_map`: `emplace(k, v)`, `insert(pair)`, `push_back(pair)` or
`emplace_back(k, v)`. `clear()` is called first if present. Values are decoded into a
temporary and then inserted, so a value failure carries the key in the path (`$.groups.g`).

Duplicate keys in a container with unique keys (`std::map`, `std::unordered_map`, anything
whose `emplace` reports success) follow `options::duplicates` exactly as struct members do:
`last_wins` (the default) decodes the later value over the earlier one, `first_wins` skips
the later value, and `error` reports `duplicate_key` with the key in the path. A
`std::multimap` or a range of pairs is not keyed and keeps every occurrence, in order.

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
9. bytes, as decided by the format (`format_traits<Format>::is_bytes<F, T>()`)
10. `map_like`
11. `tuple_like`
12. `fixed_array`, `span_like`, `sequence` (decode: `fixed_array`, then `appendable_range`)
13. `reflectable_class`
14. otherwise: compile error, "has no codec"

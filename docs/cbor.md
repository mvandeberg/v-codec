# CBOR

`<vcodec/cbor.hpp>` is the second format. It drives the same traversal as JSON, so every
type, annotation and error path documented for JSON applies unchanged; what this page adds
is the wire shape (RFC 8949), the `cbor::` annotation vocabulary for protocol work (integer
keys, tags, deterministic encoding), and the hex-window error renderer. The design and its
rationale are in [design/cbor-spec.md](design/cbor-spec.md); where this page and the
specification differ, this page describes the code.

```cpp
#include <vcodec/cbor.hpp>

namespace cbor = vcodec::cbor;

std::vector<std::byte> bytes = cbor::encode(v);                // deterministic by default
auto                   back  = cbor::decode<T>(bytes);         // vcodec::result<T>
if (!back) std::cerr << vcodec::render_hex(back.error(), bytes);
```

## Quick start

A CWT-style claims set (RFC 8392): integer keys, a tagged time point, a byte string, and a
private-use negative key.

```cpp
#include <vcodec/cbor.hpp>
#include <chrono>

namespace cbor = vcodec::cbor;

struct [[=cbor::deterministic]] Claims {
    [[=cbor::key(1)]] std::string              issuer;
    [[=cbor::key(2)]] std::string              subject;
    [[=cbor::key(4)]] std::chrono::sys_seconds expiry;      // tag 1 (epoch seconds) by default
    [[=cbor::key(7)]] std::vector<std::byte>   cwt_id;      // a byte string, natively
    [[=cbor::key(-65537), =vcodec::skip_if_null]]
                      std::optional<std::string> ext;       // COSE private-use range
};
```

Nothing here is JSON-specific to undo: `cbor::key` and `cbor::deterministic` live in the
CBOR namespace and JSON never sees them. The raw byte string, on the other hand, makes the
type CBOR-only (JSON would need `json::bytes`), which the two-format concept states in one
line:

```cpp
static_assert(vcodec::cbor_only<Claims>);
```

### Encode

```cpp
Claims c;
c.issuer  = "joe";
c.subject = "bob";
c.expiry  = std::chrono::sys_seconds{ std::chrono::seconds{ 1444064944 } };
c.cwt_id  = { std::byte{0x0b}, std::byte{0x71} };

std::vector<std::byte> bytes = cbor::encode(c);
```

```
a4                      map of 4 entries
   01 63 6a6f65            1: "joe"
   02 63 626f62            2: "bob"
   04 c1 1a 5612aeb0       4: tag(1) 1444064944
   07 42 0b71              7: h'0b71'
```

`ext` is absent because it is empty and marked `skip_if_null`. Set it and the map grows to
five entries, the last keyed `-65537` (`3a 00010000`):

```
a5 01 63 6a6f65 02 63 626f62 04 c1 1a 5612aeb0 07 42 0b71 3a 00010000 61 78
```

Output is deterministic by default: shortest heads, definite lengths, canonical key order.
Members are emitted in canonical key order regardless of declaration order, so the struct
above would produce the same bytes with its members declared in any order.

### Decode

```cpp
auto back = cbor::decode<Claims>(bytes);      // result<Claims>
if (back) std::cout << back->issuer << '\n';   // joe
```

Keys may arrive in any order (unless the type is `cbor::deterministic`, below). Unknown keys
are skipped unless the type is `deny_unknown_fields`. A missing required member names its
integer key and lists the keys that were present:

```cpp
auto bad = from_hex("a3 02 63626f62 04 c11a5612aeb0 07 420b71");    // no issuer
auto r = cbor::decode<Claims>(bad);
std::cerr << vcodec::render_hex(r.error(), bad);
```

```
error: missing required field 'issuer' (key 1)
  --> $
   |  map has 3 entries, keys present: 2, 4, 7
```

A value of the wrong kind gets a hex window with the offending head decoded:

```
error: expected text string, found unsigned integer
  --> $.subject
   |
   |  0000_0000:   a4 01 63 6a 6f 65 02 05 04 c1 1a 56 12 ae b0 07 …
   |                                    ^^
   |                                    major type 0 (unsigned integer), value 5
```

Because the type is declared `cbor::deterministic`, it also refuses non-canonical input —
here the same claims with keys 2 and 1 swapped:

```
error: non-deterministic encoding: map keys out of canonical order
  --> $
   |  key 1 precedes key 2; Claims is declared [[=cbor::deterministic]]
```

The path speaks in member terms (`$.subject`, never `$[2]`), exactly as in JSON: field
identity is the member, the key is a wire detail.

## Wire shapes

Every element of the [data model](design/data-model.md) is native in CBOR; nothing needs a
lowering annotation. The mapping to RFC 8949 major types:

| C++ | CBOR | Example |
|---|---|---|
| `bool` | simple 20 / 21 | `false` → `f4`, `true` → `f5` |
| unsigned integers | major 0 | `1000u` → `19 03e8` |
| signed integers ≥ 0 | major 0 | `5` → `05` |
| signed integers < 0 | major 1, argument `-1 - n` | `-1` → `20`, `-100` → `38 63`, `INT64_MIN` → `3b 7fffffffffffffff` |
| `float`, `double`, `long double` | major 7, ai 25/26/27, preferred serialization | `1.0` → `f9 3c00`, `1.1` → `fb 3ff199999999999a`, `100000.0` → `fa 47c35000` |
| `char` | one-character text string | `'a'` → `61 61` |
| enums | major 0/1 by underlying value (default), or text with `as_text` | `Color::green` → `01` |
| `std::string`, `std::u8string`, `std::string_view`, `const char*` | major 3, definite length, UTF-8 validated | `"IETF"` → `64 49455446` |
| `std::byte` ranges, `std::span<const std::byte>` | major 2 | `{1,2,3,4}` → `44 01020304` |
| `std::uint8_t` / `unsigned char` ranges | array of integers; major 2 with `cbor::byte_string` | `{1,2}` → `82 0102`; annotated → `42 0102` |
| `std::optional`, `unique_ptr`, `shared_ptr` | the value, or simple 22 (`null`) when empty | `optional<int>{}` → `f6` |
| sequences, `std::array`, `T[N]`, `std::span<T>` | major 4 | `{1,2,3}` → `83 010203` |
| `std::pair`, `std::tuple` | major 4, fixed length | `{1, {2,3}, {4,5}}` → `83 01 820203 820405` |
| maps with text keys | major 5, text keys | `{"a":1}` → `a1 6161 01` |
| maps with integral keys | major 5, integer keys (major 0/1), **no annotation** | `{{1,2},{3,4}}` → `a2 01020304` |
| structs | major 5, text keys or `cbor::key` integers | `Point{1,2}` → `a2 6178 01 6179 02` |
| `transparent` structs | the single member | `UserId{42}` → `18 2a` |
| tagged `std::variant` | a map with the discriminator entry (internal or external tag, as JSON) | |
| `std::chrono::sys_time<D>` | tag 1 + epoch seconds (or tag 0 + RFC 3339 text) | `c1 1a 514b67b0` |
| any member with `cbor::tag(n)` | major 6 tag(s), then the value | `c1 1a 514b67b0` |

The signedness note from the data model applies: `sint(5)` and `uint(5)` both become `05`.
On decode the reader classifies from the head byte and the type-directed decoder reconciles
with the target (`expect_sint` accepts a major 0 item that fits, `expect_uint` rejects a
major 1 item as `out_of_range`, `expect_real` accepts all three), and width narrowing to
`std::uint8_t` and friends happens in core with the same messages as JSON:

```
error: value 512 out of range for std::uint8_t
error: value -1 out of range for std::uint32_t
error: value -18446744073709551616 out of range for std::int64_t      (3b ffffffffffffffff)
```

Integers never accept floats, even integral ones (`f9 3c00` into an `int` is
`expected integer, found float`), and floats accept integers (`01` into a `double` is `1.0`).

What the reader rejects as ill-formed: additional information 28–30 and a break byte (`ff`)
outside an indefinite-length item are `errc::malformed_item`; simple values other than
20–23 (`f0`, `f8 20`, and the two-byte encoding of a value below 32) are
`errc::unsupported_simple_value`. `undefined` (`f7`) decodes as `null` into optional-like
targets by default ([options](#options)).

## Deterministic encoding

`options::deterministic` is **on by default**. The reasoning (spec §15, question 1): a
protocol user who signs their output can never accept accidental non-determinism, while a
telemetry user pays a cost that is small and measurable. The one-line opt-out:

```cpp
constexpr cbor::options loose{ .deterministic = false };
auto bytes = cbor::encode<loose>(v);
```

### What it guarantees

RFC 8949 §4.2.1, the core deterministic encoding requirements:

1. **Shortest-form heads.** Always, in both modes; there is no reason to emit `18 05` for 5.
2. **Definite lengths only.** Where core supplies no count (an unsized range such as
   `std::forward_list`, or a view), the writer buffers the container's body, counts the
   items, then emits head and body. `cbor::indefinite` and the `indefinite` option are
   ignored under `deterministic` (the option pair is a `static_assert`).
3. **Preferred float serialization.** The shortest of half, single and double that
   round-trips ([Floats](#floats)). NaN is always `f9 7e00`.
4. **Canonical key order.** Map entries sorted by the bytewise lexicographic order of their
   *encoded* keys.

Point 4 has two consequences worth internalising, both from RFC 8949 §4.2.1's own example
(`10, 100, -1, "z", "aa", [100], [-1], false`):

- **Integers do not sort numerically.** A key's encoding starts with its major type and
  length class, so every major-0 key precedes every major-1 key, and a one-byte head precedes
  a two-byte one. `300` (`19 012c`) sorts before `-1` (`20`), and `10` (`0a`) before `100`
  (`18 64`):
  ```cpp
  std::unordered_map<int, int>{{100, 1}, {10, 2}, {-1, 3}}   // a3 0a02 186401 2003
  std::map<int, int>{{300, 1}, {-1, 2}}                        // a2 19012c01 2002
  ```
- **Text keys sort by length first.** The length is in the head byte, so `"b"` (`61 62`)
  sorts before `"aa"` (`62 6161`). A `std::map<std::string, …>` is therefore *not* already
  in canonical order and cannot skip the sort:
  ```cpp
  std::map<std::string, int>{{"aa", 1}, {"b", 2}}   // a2 616202 62616101  (deterministic)
                                                    // a2 62616101 616202  (deterministic = false: std::map order)
  ```

### Structs never buffer; runtime maps do

For a struct the key set is known at compile time, so `format_traits<cbor::format>::member_order`
computes the canonical permutation consteval from the encoded key bytes and core emits the
members in that order through the sink's `begin_map_ordered` hook. The writer opens the map
with its definite count and writes straight to the output. When a member is conditionally
skipped (`skip_if_null`, `skip_if_default`), core evaluates the skip predicates in a first
pass to compute the count, still without buffering.

**Member order is canonical in both modes.** The permutation is a property of the type, not
of the option, so a struct encodes its members in canonical key order even under
`deterministic = false`. (The specification's §7.2 describes declaration order for the
non-deterministic mode; the code does not do that, and there is no reason to prefer
declaration order once the type's order is fixed anyway.)

For a **runtime map** (`std::map`, `std::unordered_map`, a range of pairs) under
`deterministic`, the writer buffers each `(encoded key, encoded value)` pair into a side
buffer, sorts the entries at `end_map` by encoded key, and then emits head and body. That is
one allocation per map level and an O(n log n) sort. Two keys that encode identically are an
error, since a canonical encoding has no place for duplicates: encoding
`std::vector<std::pair<std::string, int>>{{"a", 1}, {"a", 2}}` throws `encode_error`
(`deterministic encoding forbids duplicate map keys`); under `deterministic = false` it
writes both (`a2 616101 616102`).

One struct shape does go through the buffered path: an internally tagged `std::variant`
alternative, whose discriminator entry is spliced in at runtime and whose place in the
canonical order is therefore not known at compile time.

Unsized sequences under `deterministic` are buffered only to count, not sorted.

### Validation on read

`options::require_deterministic` (off by default) makes the reader check RFC 8949 §4.2.1
incrementally while decoding, with no cost when off. Every violation is
`errc::non_deterministic` at the offending item; `detail()` names the violation and
`suggestion()` the fix:

| Input | `detail()` | `suggestion()` |
|---|---|---|
| `18 05` | `integer 5 encoded in 2 bytes` | `shortest form is 1 byte` |
| `9f 01 ff` | `indefinite-length array` | `deterministic encoding requires definite lengths` |
| `7f 6161 ff` | `indefinite-length text string` | same |
| `fb 3ff0000000000000` | `float encoded as double` | `preferred form is half` |
| `f9 7e01` | `NaN not encoded as f97e00` | `deterministic encoding uses the canonical half NaN` |
| `a2 0401 0202` | `map keys out of canonical order` | `key 2 precedes key 4` |
| `a2 0201 0202` | `duplicate map key` | `key 2 repeats` |

(The suggestion names the key that *should* come first.) The same bytes decode without
complaint when the option is off. Every deterministic output of the writer passes this
check; that is asserted over generated values in `test/cbor/deterministic.cpp`.

### The `cbor::deterministic` type annotation

```cpp
struct [[=cbor::deterministic]] Signed { [[=cbor::key(2)]] int a = 0; [[=cbor::key(4)]] int b = 0; };
```

The annotation is for types whose *identity* is a canonical encoding — anything that gets
signed or hashed. It does three things:

1. **Compile time:** a `cbor::indefinite` member of the type, or the type itself carrying
   both annotations, is a compile error
   (`Stream::events (std::vector<int>) is declared [[=cbor::indefinite]] inside Stream, which is declared [[=cbor::deterministic]]; deterministic encoding forbids indefinite lengths.`).
2. **Decode:** `require_deterministic` is turned on for any decode whose top-level type
   carries the annotation, regardless of the call's options, and the resulting error names
   the type:
   ```
   error: non-deterministic encoding: map keys out of canonical order
     --> $
      |  key 2 precedes key 4; Signed is declared [[=cbor::deterministic]]
   ```
   A canonical type refuses non-canonical input; that is what makes "the encoding is the
   identity" true in both directions. The check applies to the whole document under that
   top-level type. Via the option instead, on a nested member, the path localises the
   violation and the type is not named (`--> $.claims`, `key 2 precedes key 4`).
3. **Encode:** nothing beyond what the default option already does. The annotation does
   **not** force deterministic output when the call passes `deterministic = false`; the
   option alone decides how the writer behaves. (The specification's §7.2 reads as if the
   annotation forced it; the code does not.) Since structs are always emitted in canonical
   order, the only way a `cbor::deterministic` type's encoding becomes non-canonical is a
   runtime map or unsized range inside it under the `loose` option.

## Integer keys

### `cbor::key(n)`

```cpp
struct S { [[=cbor::key(1)]] int a = 1; };
// CBOR: a1 01 01        JSON: {"a":1}
```

Replaces the member's text key with the integer `n` (`std::int64_t`, negative allowed) on
both encode and decode. JSON ignores the annotation and uses the wire name. Two members
resolving to the same integer key is a compile error, as duplicate wire names are:

```
static assertion failed: Claims::issuer and Claims::subject both map to the CBOR key 2 (via cbor::key(2)).
```

On decode, a member with an integer key accepts **only** that integer. Its text name is not
an alias: `a1 6161 05` (`{"a": 5}`) into `S` leaves `a` untouched (the key is unknown and
skipped), and under `deny_unknown_fields` is `unknown field 'a'`. An unknown *integer* key
under `deny_unknown_fields` renders with the key in the path:

```
error: unknown field '9'
  --> $[9]
   |  (StrictDeny is declared [[=deny_unknown_fields]])
```

Known keys always render by member name (`$.issuer`, never `$[1]`).

### `cbor::text_key_alias`

```cpp
struct S { [[=cbor::key(1), =cbor::text_key_alias]] int a = 0; };
// decodes from a1 01 05 and from a1 6161 05; always encodes a1 01 05
```

Also accept the member's wire name (and its `alias("…")` spellings) as a text key on decode,
for migrating from a text-keyed producer. Never emitted. Without it, `alias` on an
integer-keyed member has no effect under CBOR either.

### `cbor::integer_keys`

```cpp
struct [[=cbor::integer_keys]] Claims {
    std::string   iss;      // 1
    std::int64_t  exp;      // 2
    [[=cbor::key(7)]] std::vector<std::byte> cti;
    bool          admin;    // 3
};
// a4 01 636a6f65 02 1a4d88edb4 03 f5 07 40
```

Every member without an explicit `cbor::key` is allocated one: in declaration order,
starting at **1**, skipping values already taken by explicit keys. Above, `iss` → 1,
`exp` → 2, `cti` keeps 7, `admin` → 3. Adding a member in the middle renumbers everything
after it; that is the refactoring hazard, and it is why the allocation is confined to one
struct:

- a type carrying `integer_keys` that has a `flatten` member, or a base class, is a compile
  error: `Claims (declared [[=cbor::integer_keys]]) has a flattened or inherited member Ext::extra; declaration-order key allocation is not defined across a flatten or a base class. Give every member an explicit [[=vcodec::cbor::key(n)]].`
- `integer_keys` together with `transparent` is a compile error.

Explicit `cbor::key` on members of a flattened struct is fine: the key travels with the
member and must be unique across the flattened result. The reverse case, a type carrying
`integer_keys` that is *flattened into* another struct, is not rejected: its allocated keys
(1, 2, …) land in the parent's map, and only a collision with an explicit key of the parent
is diagnosed (`OuterK::a and Inner::x both map to the CBOR key 1 (via cbor::key(1)).`); the
specification (§5.2) asks for a compile error here and the code does not yet give one, so
prefer explicit keys on anything that is flattened. A struct with some `cbor::key` members
and some without produces a map with mixed integer and text keys; the decoder branches on
the key kind and consults the right table.

## Tags

### `cbor::tag(n)`

```cpp
struct T {
    [[=cbor::tag(1)]]                std::int64_t       when = 1363896240;   // c1 1a514b67b0
    [[=cbor::tag(32)]]               std::string        uri  = "http://x";   // d820 68687474703a2f2f78
    [[=cbor::tag(1), =cbor::tag(1000)]] std::optional<int> both = 5;         // c1 d903e8 05
    [[=cbor::tag(1)]]                std::optional<int> none;                // f6
};
```

Emit tag `n` before the member's value and require it on read. Repeatable; tags are written
outermost first in annotation order. Tags apply to the *value*: an empty optional is written
as a bare `null` with no tag, and `null` is accepted for it without one. Tags are emitted
once per member, never per element of a container member.

On read, `expect_tags` consumes exactly the listed tags in order. Anything else is
`errc::tag_mismatch` with `expected()` and `found()` in tag vocabulary, using IANA names
for the tags the library knows:

```
error: tag mismatch on 'issued_at'
  --> $.issued_at
   |  expected tag 1 (epoch datetime), found tag 0 (RFC 3339 string)
```

`found()` is `no tag` when the value arrives untagged.

`cbor::tag` is a member annotation. Placing it on a type (including a `transparent`
wrapper) has no effect; wrap the tagged newtype's member instead, or use a codec that calls
`s.tag(n)` itself.

### Time points

`std::chrono::sys_time<Duration>` has a built-in `codec_for<…, cbor::format>`
(`<vcodec/cbor/tags.hpp>`, included by `<vcodec/cbor.hpp>`):

| Member | Wire | Example |
|---|---|---|
| `sys_seconds when` (no annotation) | **tag 1** + integer seconds | `c1 1a 514b67b0` |
| `sys_time<milliseconds> precise` | tag 1 + double seconds when the value is not whole seconds, integer otherwise | `c1 fb 41d452d9ec200000` |
| `[[=cbor::tag(0)]] sys_seconds text` | tag 0 + RFC 3339 text | `c0 74 323031332d30332d32315432303a30343a30305a` (`2013-03-21T20:04:00Z`) |

Tag 1 is the default because `format_traits<cbor::format>::expected_tags` returns `{1}` for
a time point member with no `cbor::tag`. Decoding tag 1 accepts integer or float payloads;
tag 0 accepts `%FT%TZ` and `%FT%T%Ez` (offset) forms and reports anything else as
`type_mismatch` (`expected RFC 3339 date-time, found text string`). Both tags on one member
is a compile error (`a time point takes one of the two`). `std::chrono::duration` is
unchanged: a number in its own units, no tag.

Under JSON a `sys_time` has no codec and is *reflected* as an ordinary class
(`{"when":{"__d":{"__r":5}}}` on libstdc++), which is rarely what you want; give it a
`codec_for<…, json::format>` there.

### Unknown tags

`options::ignore_unknown_tags` (default `true`): a tag the type does not expect is skipped
transparently, wherever it appears (`d82a 05` decodes into an `int` as 5; so does
`d82a d82b 01`). With the option off, an unexpected tag is `tag_mismatch` with
`expected() == "no tag"`. Tags 21–23 (expected conversions) get no special treatment beyond
this; they are unknown tags.

### Self-describing CBOR (tag 55799)

```cpp
struct [[=cbor::self_describe]] SD { int a = 1; };     // d9d9f7 a1 6161 01
constexpr cbor::options sd{ .self_describe = true };
cbor::encode<sd>(1);                                    // d9d9f7 01
```

Prefixes the top-level value with tag 55799, by type annotation or by option. The type
annotation is honoured only on the top-level type; a nested occurrence is a compile error
(`the self-describe tag applies to a top-level value only`). On read a leading tag 55799 is
always skipped before the top-level value; elsewhere it is an unknown tag.

### Bignums (tags 2 and 3)

No built-in target type: `std::int64_t` is the widest integer, and a major 1 value below
`INT64_MIN` is `out_of_range` with the exact literal in `detail()`. For a user type, the CBOR
reader exposes `expect_bignum() -> result<bignum_ref>`:

```cpp
struct bignum_ref { bool negative; std::span<const std::byte> magnitude; };   // tag 3: value is -1 - n

struct BigId { std::vector<std::byte> magnitude; bool negative = false; };

template<> struct vcodec::codec_for<BigId, vcodec::cbor::format> {
    static void encode(vcodec::core::sink auto& s, BigId const& v) { s.tag(v.negative ? 3 : 2); s.bytes(v.magnitude); }

    template<class R> requires requires(R& r) { r.expect_bignum(); }     // any cbor::reader<Opts>
    static vcodec::status decode(R& r, BigId& out) {
        auto b = r.expect_bignum();
        if (!b) return std::unexpected(std::move(b.error()));
        out.negative = b->negative;
        out.magnitude.assign(b->magnitude.begin(), b->magnitude.end());
        return {};
    }
};
// BigId{2^64} → c2 49 010000000000000000
```

`expect_bignum` fails with `tag_mismatch` (`expected tag 2 or 3 (bignum)`) when the item is
not a bignum, without consuming. `expect_bignum` is a member of `cbor::reader<Opts>` only,
not of the `core::reader` concept, so constrain the codec's `decode` as above rather than
naming `cbor::reader<>` (which would match only the default options).

## Byte strings

Byte-like members (`std::vector<std::byte>`, `std::array<std::byte, N>`, any contiguous
range of `std::byte`, `std::span<const std::byte>`) are major type 2 with no annotation.

`std::vector<std::uint8_t>` and other `uint8_t` / `unsigned char` ranges are arrays of
integers by default; `cbor::byte_string` promotes them, the same rule as `json::bytes`
without the alphabet argument:

```cpp
struct B { [[=cbor::byte_string]] std::vector<std::uint8_t> raw{1, 2}; std::array<std::byte, 2> fixed{…}; };
// a2 63726177 42 0102 656669786564 42 dead
std::vector<std::uint8_t>{1, 2}   // 82 0102 without the annotation
```

On decode, a definite-length byte string **borrows** into `std::span<const std::byte>`: the
span aliases the input buffer, with the same lifetime rules as `std::string_view`
([Entry points and lifetime](#entry-points-and-lifetime)). `std::array<std::byte, N>` must
receive exactly `N` bytes. An indefinite-length (chunked) byte string is copied into owning
containers and rejected for a span:

```
error: indefinite-length string but B::b borrows from the input
  --> $.b
   |
   |  0000_0000:   a1 61 62 5f 41 de ff
   |                        ^^
   |                        major type 2 (byte string), indefinite length
   |  use std::vector<std::byte> instead of std::span<const std::byte>
```

## Floats

All floating members go through `double` and then take the **preferred serialization**: the
shortest of half (`f9`), single (`fa`) and double (`fb`) that round-trips exactly. `float`
members widen first, so a `float` always encodes as half or single. `-0.0` is preserved
(`f9 8000`), infinities are half (`f9 7c00`, `f9 fc00`), and NaN is the canonical half NaN
`f9 7e00` in both modes (payloads are not preserved). Decoding accepts all three widths and
integers.

| Value | Bytes |
|---|---|
| `0.0` | `f9 0000` |
| `1.0`, `1.5`, `65504.0` | `f9 3c00`, `f9 3e00`, `f9 7bff` |
| `5.960464477539063e-8` (smallest half subnormal) | `f9 0001` |
| `100000.0` | `fa 47c35000` |
| `0.1f` | `fa 3dcccccd` |
| `1.1`, `0.1` | `fb 3ff199999999999a`, `fb 3fb999999999999a` |
| `1.0e300` | `fb 7e37e43c8800759c` |

### `cbor::float_width`

```cpp
struct F {
    [[=cbor::float_width(cbor::double_)]] double d = 1.0;   // fb 3ff0000000000000
    [[=cbor::float_width(cbor::single)]]  double s = 1.0;   // fa 3f800000
    [[=cbor::float_width(cbor::half)]]    double h = 0.1;   // f9 2e66   (rounded to nearest half)
    double p = 1.0;                                          // f9 3c00   (preferred)
};
constexpr cbor::options dbl{ .floats = cbor::double_ };      // pins every unannotated float
```

Pins the width for one member (or, via `options::floats`, for the whole call). `half` and
`single` **round** a value that does not fit exactly; only a magnitude the width cannot
represent at all throws `encode_error` with code `out_of_range`
(`value 1e+05 does not fit a half-precision float ([[=cbor::float_width(half)]])`; the
half limit is 65520). On a non-floating member the annotation is a compile error
(`is declared [[=cbor::float_width(...)]] but is not a floating type.`). Decode is
unaffected; the reader accepts any width. Under `require_deterministic` a pinned width that
is not the preferred one is, of course, rejected on read.

## Typed arrays (RFC 8746)

```cpp
struct A {
    [[=cbor::typed_array]] std::vector<std::uint16_t> u16{1, 2};   // d845 44 0100 0200
    [[=cbor::typed_array]] std::vector<double>        d{1.0};      // d856 48 000000000000f03f
    [[=cbor::typed_array]] std::array<std::int8_t, 2> i8{-1, 2};   // d848 42 ff02
    std::vector<std::uint16_t> plain{1};                            // 81 01  (an ordinary array)
};
```

`cbor::typed_array` encodes a contiguous, sized range of 8–64-bit integers, `float` or
`double` as one tag (64–87, selected by element type, signedness and **native endianness**)
followed by a byte string of `size * sizeof(element)` bytes copied with one `memcpy`. On a
little-endian machine `std::vector<std::uint16_t>` is tag 69, `double` tag 86; on a
big-endian one the same members would be tags 65 and 82. `std::vector<bool>` and
non-contiguous ranges are compile errors
(`Frame::samples (std::list<double>) is declared [[=cbor::typed_array]] but is not a contiguous range of a fixed-width integer or floating type.`).

On decode, with `options::accept_typed_arrays` (default `true`), **any** contiguous sized
numeric range accepts a typed array, annotated or not, and the annotated member also accepts
a plain array. Element width, signedness and endianness are converted: a `uint16` LE typed
array decodes into `std::vector<std::int32_t>`, a `uint32` typed array into
`std::vector<double>`, a big-endian `uint16` (tag 65) into `std::vector<std::uint16_t>`.
When the source and target representations match the copy is one `memcpy`. Errors: a byte
string whose length is not a multiple of the element size is `malformed_item`; a value that
does not fit the target element is `out_of_range`; a float typed array into an integer range
is `type_mismatch`; a `std::array` of the wrong size is `type_mismatch`; float128 typed
arrays (tags 83, 87) are `type_mismatch`. With the option off, a typed array where the type
expects an array is `type_mismatch`.

## Indefinite lengths

The writer emits indefinite lengths only when `deterministic` is off, in two cases:

```cpp
constexpr cbor::options loose{ .deterministic = false };
constexpr cbor::options indef{ .deterministic = false, .indefinite = true };

cbor::encode<loose>(std::forward_list<int>{1, 2});               // 9f 0102 ff   unsized range: no count
cbor::encode<indef>(std::vector<int>{1, 2});                     // 9f 0102 ff   asked for
cbor::encode<indef>(std::map<std::string, int>{{"a", 1}});       // bf 616101 ff
cbor::encode(std::forward_list<int>{1, 2});                      // 82 0102      deterministic: counted by buffering

struct S { [[=cbor::indefinite]] std::string s = "abc"; [[=cbor::indefinite]] std::vector<std::byte> b{std::byte{1}}; };
cbor::encode<loose>(S{});   // a2 6162 5f 4101 ff 6173 7f 63616263 ff   chunked strings
cbor::encode(S{});          // a2 6162 4101 6173 63616263             deterministic ignores the request
```

- `options::indefinite` makes every array and map indefinite (`9f … ff`, `bf … ff`), structs
  included.
- `cbor::indefinite` on a **text or byte string member** writes it as an indefinite-length
  string of definite chunks of at most 4 KiB. That is the only place the member annotation
  has an effect in the current code: on an array or map member, and at type level, it is
  accepted and does nothing (the specification describes a wider scope). It is still audited:
  inside a `cbor::deterministic` type it is a compile error.
- `deterministic = true` with `indefinite = true` is a `static_assert` on the writer.

The reader accepts indefinite arrays, maps and strings everywhere unless
`require_deterministic`. Chunked strings are assembled in reader scratch, so they cannot be
borrowed: into `std::string_view` that is `escape_in_borrowed_string` (the JSON code, reused
because the meaning — a view cannot alias assembled text — is the same), into
`std::span<const std::byte>` it is `indefinite_in_borrowed_string`. Each chunk must be a
definite-length string of the same major type (`7f 41 01 ff` is `malformed_item`).

## Options

```cpp
namespace vcodec::cbor {
enum class width : std::uint8_t { preferred, half, single, double_ };   // also cbor::half, cbor::single, cbor::double_

struct options {
    // encode
    bool          deterministic         = true;    // §4.2.1: definite lengths, preferred floats, canonical key order
    bool          indefinite            = false;   // indefinite arrays and maps; only with deterministic = false
    width         floats                = width::preferred;
    bool          self_describe         = false;   // tag 55799 on the top-level value
    // decode
    bool          require_deterministic = false;   // validate §4.2.1 on read (forced on by cbor::deterministic types)
    bool          ignore_unknown_tags   = true;    // skip tags the type does not expect
    bool          undefined_as_null     = true;    // f7 decodes like f6; else type_mismatch "found undefined"
    bool          accept_typed_arrays   = true;    // RFC 8746 into any numeric range
    // both
    std::size_t   max_depth             = 256;
    bool          validate_utf8         = true;    // text strings and keys, encode and decode
    duplicate_key duplicates            = duplicate_key::last_wins;
    error_mode    errors                = error_mode::fail_fast;
};
}
```

Options are a structural NTTP, resolved at compile time, exactly as `json::options`.
`max_depth`, `validate_utf8`, `duplicates` and `errors` mean what they mean in JSON:
depth applies to encode (`encode_error` `depth_exceeded`) and decode, including
`skip_value`; a text string with invalid UTF-8 throws `encode_error`
(`text string contains invalid UTF-8 at byte 0`) on encode and is `invalid_utf8` on decode;
`duplicates` governs struct keys and keyed containers (`a2 0101 0102` into
`std::map<int,int>` is `{{1,2}}` under `last_wins`, `duplicate_key` under `error`);
`decode_all` forces `collect`.

## Entry points and lifetime

All in `vcodec::cbor`; the parameter order matches JSON (`Opts` first for encode and
`decode_into`, second for `decode<T>` and `decode_all<T>`):

```cpp
template<options Opts = {}, class T> std::vector<std::byte> encode(T const& v);
template<options Opts = {}, class T> void   encode_append(T const& v, std::vector<std::byte>& out);
template<options Opts = {}, class T, std::output_iterator<std::byte> It> It encode_to(T const& v, It it);
template<options Opts = {}, class T, core::sink S> void encode_into(T const& v, S& s);

template<class T, options Opts = {}> result<T>    decode(std::span<const std::byte> input);
template<class T, options Opts = {}> result<T>    decode(std::string_view input);        // reinterpreted as bytes
template<options Opts = {}, class T> status       decode_into(T& out, std::span<const std::byte> input);
template<class T, options Opts = {}> collected<T> decode_all(std::span<const std::byte> input);

template<class T> concept encodable;      // passes the CBOR audit for encode
template<class T> concept decodable;
template<class T> inline constexpr bool borrows;                 // T has string_view or span<const byte> members
template<class T> consteval core::message explain_encode();      // the static_assert text, .view()
template<class T> consteval core::message explain_decode();
```

`encode_append` appends to an existing buffer (`{0xAA}` then `encode_append(1, out)` gives
`aa 01`); `encode_to` encodes into a temporary vector and copies it through the iterator;
`encode_into` runs the CBOR audit and then drives any `core::sink`. `decode_into` reuses an
object: containers are cleared, members absent from the input keep their values. After the
top-level value nothing may remain (`01 02` is `trailing_content` at offset 1).

**Lifetime.** `decode` takes a `std::span<const std::byte>`. Members of type
`std::string_view`, `std::u8string_view` and `std::span<const std::byte>` alias that buffer
and are valid only as long as it is (`cbor::borrows<T>` is `true` for such types). Passing a
temporary `std::vector<std::byte>` for a borrowing type is deleted:

```
this type borrows from the input (it has std::string_view or std::span members); pass a buffer that outlives the result
```

Only definite-length strings borrow; a chunked string is copied for owning members and an
error for views ([Indefinite lengths](#indefinite-lengths)).

The writer and reader are usable directly, as sinks and readers for the traversal
(`vcodec::cbor::writer<Opts>` over a `std::vector<std::byte>&`, `vcodec::cbor::reader<Opts>`
over a span); see [customization.md](customization.md).

## Errors and `render_hex`

CBOR errors are the same `vcodec::error` as JSON's, with the same codes, paths and
renderers, plus five codes and one renderer of their own. Two differences from JSON follow
from the format being binary:

- `error::position()` is always `std::nullopt`: there is no line and column. `offset()` is
  the byte offset of the offending item's head (after any skipped tags).
- `render_framed` works but has no text to excerpt; it prints the headline and path only.
  `render_terse` is unchanged: `error: expected text string, found unsigned integer at $.users[3].name (byte offset 90)`.

`render_hex(error const&, std::span<const std::byte> input)`, declared in
`<vcodec/cbor/render_error.hpp>` in namespace `vcodec`, is the CBOR counterpart of
`render_framed`. For codes that point at an item it shows a **16-byte window** aligned to a
16-byte boundary containing the item's head, an address column (`hhhh_hhhh`), `…` where
bytes were cut on either side, carets under the head byte and as many payload bytes as
`length()` covers within the window, and a decoded description of the head:

```
error: expected text string, found unsigned integer
  --> $.users[3].name
   |
   |  0000_0050:  …a3 62 69 64 07 64 6e 61 6d 65 18 2a 65 65 6d 61 …
   |                                             ^^ ^^
   |                                             major type 0 (unsigned integer), value 42
```

```
error: malformed item: reserved additional information
  --> $[1]
   |
   |  0000_0000:   83 01 1c 02
   |                     ^^
   |                     major type 0 (unsigned integer), reserved additional information 28
```

Descriptions read `major type N (name), value V` for integers, `length L` for strings,
`N elements` / `N entries` for containers, `indefinite length`, `tag N (name)`, `false` /
`true` / `null` / `undefined`, `half-precision float`, and `break` for `ff`.
`suggestion()`, when set, follows the window. For `missing_field` the window is replaced by
a map summary (`map has 3 entries, keys present: 2, 4, 7`, collected on the error path
only); `tag_mismatch` prints `expected …, found …`; `non_deterministic` prints its
suggestion; `unknown_field`, `unknown_enumerator`, `no_variant_alternative` and
`out_of_range` print what `render_framed` prints. Pass an empty span when the buffer is gone
and the window is omitted. [errors.md](errors.md#cbor-codes) has one real example per new
code.

## Two formats, one type

`<vcodec/vcodec.hpp>` (everything) adds two concepts stating spec §11 rule 2 — a type may
support one format and must fail at compile time under the other:

```cpp
template<class T> concept vcodec::cbor_only = cbor::encodable<T> && cbor::decodable<T> && !json::encodable<T>;
template<class T> concept vcodec::json_only = json::encodable<T> && json::decodable<T> && !cbor::encodable<T>;
```

`cbor_only<T>` holds for any type with a raw byte-string member without `json::bytes`, or
an integer-keyed map without `json::stringify_keys`, or a `std::span<const std::byte>` you
want to decode into. Because CBOR represents everything JSON can and ignores the JSON
annotations, `json_only<T>` holds only when a CBOR-specific audit check fails (a
`cbor::typed_array` on a non-contiguous range, duplicate `cbor::key`s, and so on); it is
provided for symmetry. The JSON audit message for a CBOR-only type is the v0.1 one:

```
static assertion failed: Claims::cwt_id (std::vector<std::byte>)
  is byte-like but has no JSON lowering.
  Add [[=vcodec::json::bytes(vcodec::json::base64)]], or mark it [[=vcodec::skip]].
```

A type annotated for both formats works in both: `json::bytes`, `json::as_string` and
`json::stringify_keys` are ignored by CBOR (the member is native there), and `cbor::key`,
`cbor::tag`, `cbor::deterministic` are ignored by JSON.

## Differences from JSON

| | JSON | CBOR |
|---|---|---|
| Enum default | text (`"bright-red"`) | **integer** by underlying value (`00`); `as_text` restores names in CBOR, `as_integer` forces integers in JSON; both mean the same in both formats |
| Byte strings | compile error without `json::bytes(enc)` | native major type 2; `cbor::byte_string` only to promote `uint8_t` ranges |
| Integral map keys | compile error without `json::stringify_keys` | native, including negative keys |
| Struct keys | text (`name` / `rename_all` / identifier) | text by default; `cbor::key(n)` / `cbor::integer_keys` for integers |
| Member order on the wire | declaration order | canonical order of the encoded keys, always |
| Definite lengths | irrelevant | definite unless asked otherwise; indefinite lengths need `deterministic = false` |
| `std::span<const std::byte>` | encode-only | decodes by borrowing |
| `std::chrono::sys_time` | reflected as a class (give it a codec) | tag 1 epoch / tag 0 text codec built in |
| Floats | shortest decimal via `double`; NaN and infinities are `null` | preferred half/single/double; NaN and infinities are half floats |
| `error::position()` | line and column | `nullopt`; `render_hex` instead of `render_framed` |
| Tags | dropped on write, never read | `cbor::tag`, time-point defaults, unknown tags skipped, self-describe |
| Deterministic output | n/a | on by default; validated on read on request or by type annotation |

Everything else — required/optional rules, `skip_*`, `flatten`, `transparent`, `tag` for
variants, `deny_unknown_fields`, `default_value`, `with<Codec>`, `codec_for`, `describe`,
`decode_all` recovery, error paths — is identical, and `test/cross/consistency.cpp` asserts
it: the same type produces the same token stream, key set, skips and error paths under both
sinks.

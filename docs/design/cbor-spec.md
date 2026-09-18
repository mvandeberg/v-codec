# v-codec v0.2 — CBOR Backend Specification

**Status:** draft
**Date:** 2026-09-18
**Scope:** v0.1 (JSON, shipped) → v0.2, a full CBOR codec, both directions
**Companions:** `v-codec-v0.1-spec.md` (the JSON codec this builds on), `v-codec-scope.md`
(§2.3 CBOR competitive analysis, §5.4 CBOR vocabulary draft, §6.2 CBOR diagnostics draft)

v0.1 shipped the whole §4 data model and a write-only CBOR spike whose only job was to find
the places the format seam leaked. It found seven, all fixed in `core/` before the JSON writer
was built (`docs/design/seam.md`). v0.2 retires the spike and ships CBOR proper: an RFC 8949
encoder and a type-directed decoder, the `cbor::` annotation namespace, deterministic encoding
as a first-class feature, a hex-window error renderer, and the same conformance, fuzzing,
compile-fail and benchmark discipline the JSON codec has.

The positioning comes from the scoping document's audit (§2.3): the throughput axis is served
by Glaze; the **protocol axis** — integer keys, tags, deterministic encoding, the things
COSE/CWT/CDDL-described protocols need — is served by nobody. v0.2 leads with that axis and
treats RFC 8746 typed arrays as parity work.

Where this document names a `core/` change, it is because the v0.1 seam did not anticipate
the need. Each such change is listed in §3 with the reason, and each is additive: JSON's
behaviour under v0.2 must be byte-identical to v0.1 for every input in the v0.1 test suite.

---

## 1. What v0.2 is

```cpp
#include <vcodec/cbor.hpp>

struct [[=vcodec::cbor::integer_keys, =vcodec::cbor::deterministic]] Claims {
    [[=vcodec::cbor::key(1)]]  std::string                             issuer;
    [[=vcodec::cbor::key(2)]]  std::string                             subject;
    [[=vcodec::cbor::key(4)]]  std::int64_t                            expiry;
    [[=vcodec::cbor::key(6), =vcodec::cbor::tag(1)]]
                               std::chrono::sys_seconds                issued_at;
    [[=vcodec::cbor::key(7)]]  std::vector<std::byte>                  cwt_id;
    [[=vcodec::cbor::key(-65537)]] std::optional<std::string>          private_ext;
};

std::vector<std::byte> bytes = vcodec::cbor::encode(claims);          // a5 01 66 ... deterministic
auto back = vcodec::cbor::decode<Claims>(bytes);                       // result<Claims>
if (!back) std::cerr << vcodec::render_hex(back.error(), bytes);
```

The same `Claims` still encodes to JSON with text keys (`"issuer"`), because `cbor::key` lives
in the CBOR namespace and JSON does not see it. That is the seam doing its job.

### 1.1 In scope

- RFC 8949 encode and decode for the entire §7 type set of v0.1, plus the CBOR-native
  representation of every element the JSON codec had to lower: byte strings, non-text map
  keys, tags, `undefined`, indefinite lengths, half-precision floats
- The `cbor::` annotation vocabulary (§5): `key(n)`, `integer_keys`, `tag(n)`,
  `byte_string`, `deterministic`, `indefinite`, `float_width(w)`, `typed_array`, `as_text`
- Deterministic encoding per RFC 8949 §4.2 (core deterministic requirements) on write, and
  **validation of it on read**, both as a type annotation and as a call option
- RFC 8746 typed arrays for contiguous numeric ranges, encode and decode
- Standard tags with codecs: 0/1 (`std::chrono` time points), 2/3 (bignums, decode into
  user codecs only), 21–23 (expected-conversion hints, ignored), 32 (URI as text),
  55799 (self-describe, skipped on read, emitted on request)
- The hex-window error renderer (§9) and CBOR-specific error codes
- Compile-time diagnostics for the CBOR-only failure modes (§10), including the two-format
  cases: a type that is fine in CBOR and not expressible in JSON, and vice versa
- Zero-copy borrowing of definite-length text and byte strings into `std::string_view` and
  `std::span<const std::byte>`
- Test vectors, a hostile-input corpus, fuzzing, sanitizers, cross-format tests, compile-fail
  tests, benchmarks against Glaze CBOR and QCBOR, compile-time budget, CI (§13)
- Retiring `spike/` and moving `test/cross/` onto the real backend

### 1.2 Out of scope for v0.2

| Deferred | Why |
|---|---|
| COSE / CWT signing and encryption | A downstream library. v0.2 is what it would be built on |
| CDDL generation | Wanted, paired with JSON Schema generation as a 0.3 candidate |
| DOM-mediated JSON↔CBOR transcoding | No DOM; the test tree's `dom.hpp` does transcode in tests, which is enough to assert §11 |
| Streaming / incremental decode | Whole-buffer, as in v0.1 |
| Bignum (tags 2/3) *encode* into built-in types | No C++ standard type maps to it; decoding into a user codec is provided |
| Extended deterministic requirements (RFC 8949 §4.2.2, "length-first" ordering) | The core requirements are the ones COSE uses. §4.2.2 is a `key_order` option candidate for later |
| Untagged variants | Same reasoning as v0.1 |
| Arbitrary user tags on arbitrary types without `with<Codec>` | `cbor::tag(n)` is enough for the protocol cases; anything richer belongs in a codec |

---

## 2. Toolchain and build

Unchanged from v0.1: GCC 16.1+, `-std=c++26 -freflection`, CMake 3.28+, header-only, no
runtime dependencies. Test-only dependencies added: the `cbor/test-vectors` corpus (RFC 8949
Appendix A, via `FetchContent`), Glaze (already present for JSON benchmarks) and QCBOR for
the CBOR benchmark. All opt-in.

```cmake
option(VCODEC_BUILD_CBOR       "Include the CBOR backend in tests and benchmarks" ON)
```

The library itself has no option: `<vcodec/cbor.hpp>` is always installed. The option only
gates test targets, so a JSON-only consumer's test time does not grow.

---

## 3. Project structure and core changes

### 3.1 New and changed files

```
include/vcodec/
├── cbor.hpp                          # CBOR only
├── vcodec.hpp                        # now core + json + cbor
├── error.hpp                         # + errc::tag_mismatch, non_deterministic, malformed_item,
│                                     #   indefinite_in_borrowed_string, unsupported_simple_value
├── core/
│   ├── annotations.hpp               # + as_text (enum), member_order hook types
│   ├── model.hpp                     # + format_traits hooks (§3.2), sink/reader hooks (§3.3)
│   ├── traverse.hpp                  # consults integer_key / member_order / expected_tags / write_range
│   ├── build.hpp                     # consults integer_key / expected_tags / read_range; kind::tag, kind::undefined
│   ├── diagnose.hpp                  # audit consults the new hooks
│   └── lower.hpp                     # + half-float encode/decode (shared with any future format)
└── cbor/
    ├── options.hpp
    ├── annotations.hpp               # vcodec::cbor:: vocabulary + cbor::format
    ├── traits.hpp                    # format_traits<cbor::format>
    ├── head.hpp                      # major type / additional info encode + decode, shortest form
    ├── write.hpp                     # sink, deterministic buffering, typed arrays
    ├── read.hpp                      # type-directed reader, cursors, skip_value, determinism validation
    ├── tags.hpp                      # standard tag codecs (chrono, URI, self-describe)
    ├── render_error.hpp              # hex-window renderer
    └── api.hpp                       # gated entry points
test/
├── cbor/                             # encode, decode, deterministic, tags, typed_array, borrowing, errors, options
├── conformance/
│   ├── cbor_appendix_a.cpp           # RFC 8949 Appendix A vectors, both directions
│   ├── cbor_hostile.cpp              # the committed malformed-input corpus (§13.3)
│   └── corpus/cbor_hostile/          # hand-written .cbor files with expected errc
├── cross/                            # now JSON vs CBOR, spike removed
├── compile_fail/cbor_*.cpp
└── roundtrip/cbor_roundtrip.cpp
fuzz/
├── fuzz_cbor_skip.cpp
├── fuzz_cbor_typed.cpp
├── fuzz_cbor_roundtrip.cpp
└── fuzz_cbor_deterministic.cpp
bench/
├── cbor_encode.cpp                   # vs Glaze CBOR, QCBOR
├── cbor_decode.cpp
└── compile_time.cpp                  # + a CBOR TU, budget re-recorded
docs/
├── cbor.md                           # the format guide: wire shapes, determinism, tags
├── annotations.md                    # + cbor:: section
├── errors.md                         # + CBOR codes and the hex renderer
└── design/seam.md                    # + what CBOR proper changed
```

`spike/` is deleted in the same change that lands `cbor/write.hpp`. Its tests move to
`test/cross/`, asserted against the real backend.

### 3.2 `format_traits` additions (core change)

The v0.1 traits answered two questions (bytes classification, key/bytes representability).
CBOR needs five more. All have defaults that reproduce v0.1 behaviour exactly, so JSON is
untouched.

```cpp
template<class Format>
struct format_traits {
    // v0.1
    template<field_meta F, class T> static consteval bool is_bytes();
    template<field_meta F, class T> static consteval bool can_lower_bytes();
    template<field_meta F, class K> static consteval bool can_lower_key();

    // v0.2 — all defaults are the v0.1 behaviour
    // Integer key for a struct member, or nullopt for its text name.
    template<field_meta F>  static consteval std::optional<std::int64_t> integer_key() { return std::nullopt; }
    // Emission order of a struct's members: a permutation of [0, N). Identity by default.
    template<class T>       static consteval std::vector<std::size_t> member_order();
    // Tags that must precede this member's value (outermost first). Empty by default.
    template<field_meta F>  static consteval std::vector<std::uint64_t> expected_tags() { return {}; }
    // Whether enums encode as integers when the member carries neither as_integer nor as_text.
    static constexpr bool   enum_default_integer = false;
    // Whether the format wants the bulk-range hooks tried before element-wise traversal.
    template<field_meta F, class R> static consteval bool bulk_range() { return false; }
};
```

Why each exists rather than being a CBOR-private concern:

- `integer_key` — key emission happens in `core::encode_members` and key lookup in
  `core::decode_struct`. Core must know the key is an integer to call `s.key(int64)` and to
  build an integer lookup table. The *value* comes from `cbor::key(n)`, which core never
  names.
- `member_order` — deterministic encoding needs struct members emitted in canonical key
  order. Ordering is known at compile time for structs, so core iterates `fields_of<T>` in
  the traits' permutation and the writer never buffers a struct map.
- `expected_tags` — a tag precedes the value in the byte stream, so the reader must know
  what to expect *before* `expect_uint()` is called. Core calls `r.expect_tags(list)` when
  the list is non-empty. The value comes from `cbor::tag(n)`.
- `enum_default_integer` — the scoping document's §5.5: CBOR's idiomatic enum encoding is the
  integer. Per-format *defaults* may differ (§11 rule 4); the core annotations `as_integer`
  and the new `as_text` override in both formats with identical meaning.
- `bulk_range` — RFC 8746 typed arrays replace N element events with one tagged byte string.
  Core tries `s.write_range<F>(r)` / `r.read_range<F>(out)` when the traits say so.

### 3.3 Sink and reader hooks (core change, optional members)

```cpp
// Optional sink members, tried by core before the element-wise path:
template<field_meta F, class R> void write_range(R const& contiguous_numeric_range);
void begin_map_ordered(std::optional<std::size_t> n);   // entries will arrive in canonical order
void tag(std::uint64_t);                                 // already in the concept; now emitted by core

// Optional reader members:
status expect_tags(std::span<const std::uint64_t> expected);    // consume and verify
result<std::uint64_t> peek_tag();                                // nullopt-like: kind() != tag
template<field_meta F, class R> status read_range(R& out);
```

`kind::tag` and `kind::undefined`, present but unproduced in v0.1, become live: `peek()` may
return them. Core's handling:

- `kind::tag` where no tag is expected: if `Opts.ignore_unknown_tags` (default true) the
  reader skips it internally and core never sees it; otherwise `errc::tag_mismatch` with
  `expected` = "no tag".
- `kind::undefined`: for optional-like targets, treated as absent when
  `Opts.undefined_as_null` (default true); otherwise `errc::type_mismatch` with `found` =
  "undefined". Never produced on encode: a C++ value is never undefined.

### 3.4 Structural rules

Unchanged, plus one: **`json/` and `cbor/` must not include each other.** The layering check
gains the rule. Shared lowerings (base64, hex, half-float) live in `core/lower.hpp`.

---

## 4. The data model, CBOR side

The v0.1 model was defined as the union of JSON and CBOR. CBOR represents every element
natively; the table is the mapping to RFC 8949 major types, not a lowering table.

| Model element | CBOR | Notes |
|---|---|---|
| `null` | 0xF6 | |
| `boolean` | 0xF4 / 0xF5 | |
| `uint` | major 0 | shortest head under `deterministic` |
| `sint` (negative) | major 1, value `-1 - n` | non-negative `sint` collapses to major 0 (documented v0.1 lowering, now the real thing) |
| `real` | major 7, ai 25/26/27 | preferred serialization: shortest of half/single/double that round-trips (§7.3) |
| `text` | major 3 | UTF-8 validated on read when `validate_utf8`; definite unless `indefinite` |
| `bytes` | major 2 | native; no annotation required (contrast JSON) |
| `array` | major 4 | definite when core supplies a count, indefinite otherwise, unless `deterministic` forces definite |
| `map` | major 5 | keys: text, uint, sint; other key kinds are a compile error (§10) |
| `tag` | major 6 | from `cbor::tag(n)` or a codec; standard tags in `cbor/tags.hpp` |
| `undefined` | 0xF7 | decode only, §3.3 |

Simple values other than 20–23 (false, true, null, undefined) are `errc::unsupported_simple_value`
on read. Additional-info values 28–30 and a break byte outside an indefinite item are
`errc::malformed_item`.

### 4.1 Integer range

CBOR major 1 reaches `-2^64`; `std::int64_t` reaches `-2^63`. A negative integer below
`INT64_MIN` decodes as `errc::out_of_range` with the exact literal value in `detail()`
(rendered as `-18446744073709551616`), through the same `type_display_for` machinery JSON
uses, so the message reads `value -18446744073709551616 out of range for std::int64_t`.

### 4.2 What CBOR cannot represent from the C++ side

Nothing in the §7 type set. The two-format compile-time diagnostics (§10) therefore run in
one direction only for representability — JSON rejects what CBOR accepts — plus the CBOR-only
annotation misuses.

---

## 5. Annotations — the `cbor::` namespace

All payloads are structural. `cbor::format` is the format tag, used by `codec_for<T,
cbor::format>` and `format_traits<cbor::format>`.

### 5.1 Field level

| Annotation | Effect | Notes |
|---|---|---|
| `cbor::key(n)` | Integer map key for this member in place of its text name. `n` is `std::int64_t`; negative keys are allowed (COSE private-use range) | Decode accepts the integer key **only**; the text name is not an alias unless `cbor::text_key_alias` is also present. JSON ignores it |
| `cbor::text_key_alias` | On decode, also accept the member's wire name as a text key | For migrating text-keyed producers. Never emitted |
| `cbor::tag(n)` | Emit tag `n` before the value; require it on read | Repeatable: `[[=cbor::tag(1), =cbor::tag(1000)]]` emits outermost first. A read that finds a different tag is `errc::tag_mismatch` |
| `cbor::byte_string` | Treat a `std::uint8_t` / `unsigned char` contiguous range as a byte string (major 2) | `std::byte` ranges are byte strings without it. Same promotion rule as `json::bytes`, without the encoding argument |
| `cbor::indefinite` | Encode this array, map or string with indefinite length | Compile error together with `deterministic` on the same type (§10). Strings are chunked at 4 KiB |
| `cbor::float_width(half \| single \| double)` | Pin the encoded float width | Default is preferred serialization. `half` on a value that does not round-trip is a runtime `encode_error(errc::out_of_range)` |
| `cbor::typed_array` | RFC 8746: encode a contiguous range of `std::uint8_t…std::uint64_t`, `std::int8_t…std::int64_t`, `float` or `double` as one tagged byte string in native endianness | Decode accepts both the typed-array form and a plain array of numbers. Big-endian tags (64–71 without the little-endian bit, 80–87) are accepted on read; emitted form is native |
| `cbor::as_text` (core `as_text`) | Encode an enum by name even where the format default is integer | Core annotation, counterpart of `as_integer`; also meaningful in JSON (no-op there) |

`cbor::key` on a member of a type that is `flatten`ed into another: the key travels with the
member and must be unique across the flattened result. Two members mapping to the same
integer key — including after flattening or `integer_keys` allocation — is a compile error,
exactly as duplicate wire names are.

### 5.2 Type level

| Annotation | Effect | Notes |
|---|---|---|
| `cbor::integer_keys` | Every member without an explicit `cbor::key` gets one: declaration order, starting at 1, skipping values taken by explicit keys | **Refactoring hazard, deliberately constrained:** a type carrying `integer_keys` that is `flatten`ed into another, or has `flatten` members, is a compile error naming the member and telling the user to write explicit keys. Declaration-order allocation only within one struct |
| `cbor::deterministic` | RFC 8949 §4.2.1 for this type and everything nested in it: shortest-form heads, definite lengths, preferred floats, canonical key order | Also a call option (§8). Per-type is for types whose *identity* is a canonical encoding (anything that gets signed) |
| `cbor::self_describe` | Prefix the top-level value with tag 55799 | Only honoured when the type is the top-level value; nested occurrence is a compile error |
| `cbor::indefinite` | Type-level form of the field annotation: this struct's map is indefinite | |

### 5.3 Interactions with the core vocabulary

- `name("s")` and `cbor::key(n)` coexist: JSON uses the name, CBOR the key. `alias` is a
  text alias and is honoured on CBOR decode only when the map key is text.
- `rename_all` is irrelevant to a member with an integer key.
- `skip_if_null` / `skip_if_default` make the member count dynamic. Under `deterministic`
  (definite lengths required) the writer computes the count before emitting the head by
  evaluating the skip predicates first — a second pass over the struct's members, not over
  its values, so the cost is N branches, not a buffer.
- `transparent` and `tag`: a transparent wrapper with `cbor::tag(n)` on the type emits the
  tag before its single member's value. This is the idiom for "a tagged newtype", the CBOR
  equivalent of Glaze's `epoch_time` wrapper without the wrapper.
- Enums: default **integer** in CBOR (`enum_default_integer = true`). `as_text` restores
  names; `as_integer` is then redundant but allowed. Unknown integer values are
  `errc::unknown_enumerator` with numeric candidates, as in JSON.

### 5.4 Standard tags with built-in codecs (`cbor/tags.hpp`)

Provided as `codec_for<T, cbor::format>` specialisations, so they are looked up before
reflection and never collide with a user's `codec_for<T, void>`:

| Type | Tag | Encoding |
|---|---|---|
| `std::chrono::sys_time<Duration>` | 1 | integer seconds when `Duration` is whole seconds and the value is integral, else double |
| `std::chrono::sys_time<Duration>` with `[[=cbor::tag(0)]]` on the member | 0 | RFC 3339 text |
| `std::chrono::duration<Rep, Period>` | none | number in the duration's own units, unchanged from JSON |
| A member `std::string` with `[[=cbor::tag(32)]]` | 32 | URI text; no validation beyond UTF-8 |

Tags 2 and 3 (bignums): the reader exposes `expect_bignum() -> result<bignum_ref>` (sign plus
byte span) for user codecs; no built-in target type. Tags 21–23 (expected conversion) are
skipped on read and never emitted. Tag 55799 is skipped on read anywhere it appears.

---

## 6. Schema layer changes

`field_info` already carries `has_int_key` / `int_key` ("CBOR; unused in v0.1, present in the
table"). v0.2 fills them from `format_traits<Format>::integer_key<F>()` — which means the
runtime table becomes **per format**: `schema_of<T>` stays format-free (text names, flags)
and a new `key_table_of<T, Format>` holds the integer keys and the lookup structure.

```cpp
namespace vcodec::core {

template<class T, class Format>
struct key_table {
    // For each field index: the integer key, or nullopt when the member uses its text name.
    std::span<const std::optional<std::int64_t>> keys;
    // Emission order under this format (identity unless the format's member_order says otherwise).
    std::span<const std::size_t>                order;
    // Integer key → field index. Dense array when keys are small and contiguous (the COSE
    // case: keys 1..N), sorted array with binary search otherwise.
    std::size_t find(std::int64_t key) const noexcept;
};

template<class T, class Format>
inline constexpr key_table<T, Format> key_table_of = build_key_table<T, Format>();

}
```

Text-key lookup keeps using `lookup_of<T>` from v0.1. A CBOR map may mix text and integer keys
(a struct with some `cbor::key` members and some without), so the struct decoder branches on
`c->key_kind()` and consults the right table. Both are built consteval into static storage.

Duplicate detection extends to integer keys: two members with the same key under the same
format is a schema error surfaced by the audit as a §10 message.

---

## 7. Encoder (`cbor/write.hpp`)

### 7.1 Sink

```cpp
template<options Opts = {}>
class writer {
public:
    using format = cbor::format;
    explicit writer(std::vector<std::byte>& out);

    // core::sink
    void null(); void boolean(bool); void uint(std::uint64_t); void sint(std::int64_t); void real(double);
    void text(std::string_view); void bytes(std::span<const std::byte>);
    void begin_array(std::optional<std::size_t>); void end_array();
    void begin_map(std::optional<std::size_t>);   void end_map();
    void key(std::string_view); void key(std::uint64_t); void key(std::int64_t);
    void tag(std::uint64_t);

    // hooks
    template<core::field_meta F> void real(double);                          // float_width
    template<core::field_meta F> void text(std::string_view);                // indefinite chunking
    template<core::field_meta F, class R> void write_range(R const&);        // typed_array
    template<core::static_string Name> void prepared_key();                  // precomputed text-key head + bytes
    template<std::int64_t Key> void prepared_key();                          // precomputed integer-key bytes
    void begin_map_ordered(std::optional<std::size_t>);                      // no buffering needed
    template<class T> requires fixed_layout<T> void write_prepared(T const&); // §7.4
};
```

Heads are always shortest-form (there is no reason to emit a non-shortest head; RFC 8949
§4.2.1 requires it for deterministic output and it is smaller regardless).

### 7.2 Deterministic encoding

When `Opts.deterministic` (default **true**, per the scoping document's argument that
accidental non-determinism breaks signatures) or the type carries `cbor::deterministic`:

1. Shortest-form integer and length heads — always on anyway.
2. Definite lengths only. `begin_array(nullopt)` / `begin_map(nullopt)` from core (a struct
   with conditional members, or an unsized range) are handled by **buffering the container's
   body** into a side buffer, counting items, then emitting head + body. Core avoids this for
   structs by pre-counting (§5.3), so buffering happens only for unsized runtime ranges
   (`std::forward_list`, input ranges).
3. Preferred float serialization (§7.3).
4. Canonical key order: bytewise lexicographic order of the encoded keys. For struct maps,
   core has already permuted the members via `member_order` (computed consteval from the
   encoded key bytes, so the writer's `begin_map_ordered` path is taken and nothing is
   buffered). For runtime maps (`std::map`, `std::unordered_map`, ranges of pairs), the
   writer buffers `(encoded key, encoded value)` pairs in the map's body buffer and sorts at
   `end_map`. `std::map<std::string, …>` is *not* already canonical (a one-byte key sorts
   before any two-byte key), so this cannot be skipped.
5. `cbor::indefinite` anywhere under a deterministic type is a compile error; the
   `indefinite` *option* together with the `deterministic` option is a `static_assert`.

Under `deterministic = false`: indefinite lengths where core has no count, declaration order,
no sorting, no buffering, and `float_width` / preferred floats still apply.

### 7.3 Floats

Preferred serialization (RFC 8949 §4.2.1, §3.3): encode as the shortest of half (ai 25),
single (26), double (27) that round-trips to the same `double`. NaN encodes as `0xF9 7E00`
(the canonical half NaN) under deterministic mode; payload-preserving NaN is not attempted.
Infinities encode as half. `-0.0` is preserved (half `0x8000`). `float` members widen to
`double` first and then take the preferred form, so a `float` value always encodes as half or
single, never double.

Half-float conversion lives in `core/lower.hpp` (`to_half`, `from_half`) with subnormal and
rounding handled exactly; it is unit-tested against every one of the 65,536 half patterns
(decode → encode identity) and against the Appendix A vectors.

### 7.4 Whole-type specialisation

`write_prepared<T>` fires for a struct whose members are all fixed-width integers or booleans
with integer keys and no conditional skips, under deterministic mode. The map head, every key,
and every value head can be computed at compile time when the values are booleans; for
integers only the keys are precomputed and the value heads depend on magnitude, so the
"fixed layout" case is booleans and `std::uint8_t`-range values known to be small… which is
rare. The honest deliverable is: **keys are one memcpy per key** (`prepared_key<Key>`) and
the map head is precomputed when the count is static. The v0.1 spec's "one memcpy for the
whole struct" is reachable only for all-boolean structs and is implemented for those, tested,
and not advertised.

### 7.5 Typed arrays (RFC 8746)

`write_range<F>(range)` when `F` carries `cbor::typed_array` and the element type is an
integer of width 8–64 or `float`/`double`: emit tag 64–87 selected by element type, signedness
and native endianness, then a byte string of `size * sizeof(element)` bytes copied with one
`memcpy`. `std::vector<bool>` is excluded (no RFC 8746 mapping). Non-contiguous ranges are a
compile error naming the member.

### 7.6 Entry points

```cpp
namespace vcodec::cbor {
template<options Opts = {}, class T> auto encode(T const&) -> std::vector<std::byte>;
template<options Opts = {}, class T> void encode_append(T const&, std::vector<std::byte>&);
template<options Opts = {}, class T, std::output_iterator<std::byte> It> It encode_to(T const&, It);
template<options Opts = {}, class T, core::sink S> void encode_into(T const&, S&);
}
```

All gated by `cbor::encodable<T>` (§10). `encode_error` semantics as in JSON: thrown for
runtime facts the type system cannot see (skipped enumerator, depth exceeded, `float_width(half)`
on a value that does not round-trip, `bignum` payload too long).

---

## 8. Decoder (`cbor/read.hpp`) and options

```cpp
namespace vcodec::cbor {

enum class float_width : std::uint8_t { preferred, half, single, double_ };
enum class key_order   : std::uint8_t { canonical };           // §4.2.2 length-first is a later candidate

struct options {
    // encode
    bool          deterministic        = true;
    bool          indefinite           = false;   // only when !deterministic
    float_width   floats               = float_width::preferred;
    bool          self_describe        = false;   // tag 55799 on the top-level value
    // decode
    bool          require_deterministic = false;  // validate §4.2.1 on read
    bool          ignore_unknown_tags   = true;
    bool          undefined_as_null     = true;
    bool          accept_typed_arrays   = true;   // decode RFC 8746 into any numeric range, annotated or not
    // both
    std::size_t   max_depth            = 256;
    bool          validate_utf8        = true;
    duplicate_key duplicates           = duplicate_key::last_wins;
    error_mode    errors               = error_mode::fail_fast;
};

template<class T, options Opts = {}> auto decode(std::span<const std::byte>) -> result<T>;
template<options Opts = {}, class T>  auto decode_into(T&, std::span<const std::byte>) -> status;
template<class T, options Opts = {}> auto decode_all(std::span<const std::byte>) -> collected<T>;
// A temporary buffer cannot be borrowed from: deleted for borrowing types, as in JSON.
template<class T, options Opts = {}> requires (borrows<T>) auto decode(std::vector<std::byte>&&) = delete("…");

}
```

`std::string_view` and `std::span<const char>` inputs are accepted through an overload that
reinterprets to bytes; the lifetime rule is the same.

### 8.1 Reader

Implements `core::reader` with `format = cbor::format`, `save()`/`restore()` (offset plus
depth, as JSON), and the error factory (offset only: `position()` is `nullopt`, which is the
reason it is optional).

- `peek()` decodes the head byte without consuming: `null`, `boolean`, `uint`, `sint`,
  `real`, `text`, `bytes`, `array`, `map`, `tag`, `undefined`. Simple values outside 20–23 and
  reserved additional info make `peek()` return `undefined` and the next `expect_*` return
  `malformed_item` / `unsupported_simple_value`.
- Every `expect_*` fails without consuming, reporting `expected`/`found` in CBOR vocabulary:
  `unsigned integer`, `negative integer`, `float`, `text string`, `byte string`, `array`,
  `map`, `tag`, `simple value`, `undefined`, `null`, `boolean`. `length()` on the error is the
  whole item (head + payload) when the item is well-formed, else the head.
- `expect_uint()` on a negative integer is `out_of_range` (as JSON on `-5`); `expect_sint()` on
  an unsigned above `INT64_MAX` likewise. `expect_real()` accepts integers (widening) and all
  three float widths. Integers never accept floats, even integral ones (`1.0` is a `float`),
  matching JSON's strictness.
- `expect_text()`: definite-length → borrowed view into the input (`text_ref::borrowed =
  true`, `raw_length` = item length); indefinite-length → chunks concatenated into scratch
  (`borrowed = false`). Each chunk must itself be a definite-length text string (RFC 8949
  §3.2.3), otherwise `malformed_item`. UTF-8 validated over the assembled string when
  `validate_utf8`.
- `expect_bytes()`: same rules; definite-length byte strings borrow into
  `std::span<const std::byte>` members — the zero-copy case the scoping document expected
  to be common. An indefinite byte string into a `span` member is
  `errc::indefinite_in_borrowed_string` with the suggestion `use std::vector<std::byte>`.
- Cursors: `seq_cursor::remaining()` and `map_cursor::remaining()` return the definite count
  when there is one (core uses it to `reserve`), `nullopt` for indefinite. `map_cursor::key_kind()`
  returns `text`, `uint` or `sint`; any other key kind (a float or array key from a hostile
  producer) is `errc::non_text_key` with `found` set — the code name is historical, the
  meaning is "key kind the target cannot take".
- `expect_tags(list)`: consumes exactly the listed tags in order; a missing, extra or
  different tag is `tag_mismatch` with `expected` = `tag 1 (epoch datetime)` using the IANA
  name table for well-known tags, `found` = `tag 0 (RFC 3339 string)` or `no tag`.
- `skip_value()`: iterative, head-driven, O(1) per scalar and O(payload) for strings, honouring
  `max_depth` and validating well-formedness (break bytes only where allowed, chunks of the
  right major type, UTF-8 in text when validating). This is the CBOR well-formedness validator
  the hostile corpus and `fuzz_cbor_skip` exercise.
- `finish()`: trailing bytes are `trailing_content`; a leading tag 55799 is skipped before
  the top-level value.

### 8.2 Determinism validation (`require_deterministic`)

Checked incrementally during any decode, in the reader, at no cost when the option is off
(compile-time branch):

| Check | Error detail |
|---|---|
| Head not shortest form (e.g. `0x18 05`) | `integer 5 encoded in 2 bytes; shortest form is 1` |
| Indefinite length anywhere | `indefinite-length array` |
| Float not in preferred form (e.g. `1.0` as double) | `1 encoded as double; preferred form is half` |
| Map keys not in canonical order | `key 4 precedes key 2` (keys rendered as CBOR diagnostic notation) |
| Duplicate map key | `duplicate key 2` — also an error under any `duplicates` policy in this mode |

All are `errc::non_deterministic`, at the offending item's offset, with the path. The
`cbor::deterministic` *annotation* on a type turns this on for decodes of that type regardless
of the option, because a type that promises a canonical encoding should refuse non-canonical
input — that is what makes "the encoding is the identity" true in both directions.

### 8.3 Struct decoding with integer keys

`core::decode_struct` gains one branch: on `key_kind() == uint || sint`, read the integer,
look it up in `key_table_of<T, Format>`; on `text`, use `lookup_of<T>` as today (which also
serves `cbor::text_key_alias`). Everything else — `seen` bitset, duplicates policy,
`deny_unknown_fields`, defaults, required, collect-mode recovery, path steps in member terms —
is the v0.1 code unchanged. An unknown integer key under `deny_unknown_fields` is
`unknown_field` with `detail()` = `7` and a path step `key_int(7)`, rendered `$[7]`; a known
key is rendered by its member name, so COSE debugging shows `$.issuer`, never `$.1`.

Missing required member: `detail()` is the wire name, and the error additionally carries the
member's integer key so the renderer can say `'issuer' (cbor key 1)`.

---

## 9. Errors

### 9.1 New codes (additive)

```cpp
enum class errc : std::uint16_t {
    /* v0.1 … */
    // v0.2, syntax
    malformed_item,                 // reserved additional info, misplaced break, bad chunk
    unsupported_simple_value,       // simple value outside 20–23
    // v0.2, schema
    tag_mismatch,                   // expected tag n, found m / no tag
    // v0.2, policy
    non_deterministic,              // require_deterministic violated
    indefinite_in_borrowed_string,  // chunked string into a view/span member
};
```

`is_syntax_error()` covers the two syntax additions so collect mode stops on them.

### 9.2 The hex-window renderer

`render_hex(error const&, std::span<const std::byte> input)`, in `cbor/render_error.hpp`,
namespace `vcodec`. The required outputs (specification, asserted byte for byte in
`test/cbor/errors.cpp` from real malformed input):

```
error: expected text string, found unsigned integer
  --> $.users[3].name
   |
   |  0000_0504:  a2 62 69 64 07 64 6e 61 6d 65 18 2a 65 65 6d …
   |                                             ^^ ^^
   |                                             major type 0 (unsigned integer), value 42

error: missing required field 'issuer' (cbor key 1)
  --> $
   |  map has 3 entries, keys present: 2, 4, 7

error: tag mismatch on 'issued_at'
  --> $.issued_at
   |  expected tag 1 (epoch datetime), found tag 0 (RFC 3339 string)

error: non-deterministic encoding: map keys out of canonical order
  --> $.claims
   |  key 4 precedes key 2; Claims is declared [[=cbor::deterministic]]

error: value -18446744073709551616 out of range for std::int64_t
  --> $.offset
```

Rules:

- The hex window is 16 bytes wide, aligned so the offending item's head byte falls in the
  window; the address column is the window start as `hhhh_hhhh` (underscore-grouped hex,
  eight digits) — wide enough for any buffer v0.2 targets, and the same width in every line.
- Leading and trailing `…` mark bytes outside the window.
- Carets cover the head byte and the payload bytes that fall in the window; a decoded
  description of the head follows: `major type N (name), value V` for scalars, `length L`
  for strings and containers, `indefinite length` where applicable, `tag N (name)` for tags.
- `missing_field` and `missing_tag` show the map summary line instead of a window; the
  "keys present" list comes from the struct decoder recording the keys it saw (integer keys
  in diagnostic notation, text keys quoted), collected only on the error path.
- `render_terse` is unchanged and works for CBOR errors. `render_framed` on a CBOR error
  renders the header and path and omits the excerpt (it has no text to excerpt); calling it is
  not an error, just less useful.

### 9.3 Path resolution

Field identity is the member (§11 rule 5): `push_member(identifier)` in the struct decoder is
unchanged, so integer-keyed members render by name for free. `key_int` steps appear only for
runtime maps with integer keys (`std::map<int, …>` → `$.by_id[7]`) and for unknown keys.

---

## 10. Compile-time diagnostics

The v0.1 audit runs per format; CBOR adds a `format_traits<cbor::format>` and its own messages.
Every message below gets a `test/compile_fail/cbor_*.cpp` case and the no-cascade guard.

```
static assertion failed: Claims::issuer and Claims::subject both
  map to the CBOR key 2 (via cbor::key(2)).

static assertion failed: Claims (declared [[=cbor::integer_keys]]) has a
  [[=vcodec::flatten]] member Claims::ext; declaration-order key allocation is not
  defined across a flatten. Give every member of Claims and Ext an explicit
  [[=vcodec::cbor::key(n)]].

static assertion failed: Frame::samples (std::list<double>)
  is declared [[=cbor::typed_array]] but is not a contiguous range of a
  fixed-width integer or floating type.

static assertion failed: Stream::events is declared [[=cbor::indefinite]]
  inside Stream, which is declared [[=cbor::deterministic]]; deterministic
  encoding forbids indefinite lengths.

static assertion failed: Event::when (std::chrono::sys_seconds) carries
  [[=cbor::tag(1)]] and [[=cbor::tag(0)]]; a time point takes one of the two.

static assertion failed: Claims::cwt_id (std::vector<std::byte>)
  is byte-like but has no JSON lowering.
  Add [[=vcodec::json::bytes(vcodec::json::base64)]], or restrict Claims to CBOR.
```

The last is the v0.1 message with the two-format ending the scoping document asked for; it
fires only when the type is passed to a JSON entry point, which is the rule: **a type may
support one format and must fail at compile time under the other** (§11 rule 2).
`vcodec::cbor::only<T>` / `vcodec::json::only<T>` are provided as `static_assert`able
concepts so a protocol library can state its intent in one line.

Cross-format additions to the audit:

- `cbor::key` on a member whose enclosing struct is `transparent`: error (a transparent wrapper
  has no map).
- `cbor::self_describe` on a nested type: error.
- `cbor::float_width(half)` on an integer member: error.
- Map key type not text or integral: error in both formats already; CBOR's message drops the
  `stringify_keys` hint and names the key type.

---

## 11. Cross-format consistency rules

Unchanged from v0.1, now asserted JSON against CBOR in `test/cross/`:

1. Round-trip holds within a format. Asserted by both round-trip suites.
2. A type may support only one format; it fails at compile time under the other. Asserted
   with `static_assert(!json::encodable<CborOnly>)` and `static_assert(!cbor::encodable<…>)`
   pairs plus compile-fail cases.
3. Core annotations mean the same in both formats. Asserted by dumping the CBOR token stream
   back into the model vocabulary and comparing with JSON's, for every §7 type set fixture
   (`test/support/cbor_dump.hpp` graduates from the spike's helper to a real decoder-based
   dumper).
4. Per-format defaults may differ (enum integers, bytes native, definite lengths); per-format
   semantics may not. The enum default is the one place v0.2 exercises this rule, and the
   test shows `as_text`/`as_integer` giving the same result in both formats.
5. Field identity is the member. Asserted: the same malformed value in JSON and CBOR produces
   errors with identical `path()` sequences, and the CBOR path renders `$.issuer` for key 1.
6. **New:** transcoding through the test DOM is lossless for the JSON-representable subset:
   every JSONTestSuite `y_` document decodes to `dom::value`, encodes to CBOR, decodes back to
   an equal `dom::value`, and re-encodes to identical JSON.

---

## 12. Milestones

Sizing relative, one developer. Dependency order is strict except where noted.

### C0 — Seam extension in core (M)

`format_traits` hooks (§3.2), optional sink/reader hooks (§3.3), `key_table_of`, the `as_text`
annotation, `enum_default_integer`, `kind::tag` / `kind::undefined` handling in `build.hpp`,
`errc` additions, half-float in `core/lower.hpp`. All JSON tests still pass byte-identically;
`test/core/` gains a mock format that sets every hook, so the hooks are tested without CBOR.

**Exit:** v0.1's full suite green; the mock-format tests cover every new hook; the JSON
compile-time budget is within tolerance.

### C1 — Writer (M)

`head.hpp`, `write.hpp` non-deterministic path, integer keys, tags from `cbor::tag`, byte
strings, indefinite lengths, preferred floats, `float_width`. Spike deleted; its cross tests
rewritten against the writer.

**Exit:** Appendix A encode vectors reproduced; the §7 type set encodes; `test/cross/` green.

### C2 — Deterministic encoding (M)

`member_order` consteval permutation, `begin_map_ordered`, body buffering for runtime maps
and unsized ranges, canonical key sort, deterministic NaN, `deterministic` option and
annotation, the `indefinite` × `deterministic` compile errors.

**Exit:** RFC 8949 §4.2 examples reproduced; a property test shows that for any generated
value, encode → decode → encode is byte-identical and independent of `std::unordered_map`
iteration order and member declaration order permutations.

### C3 — Errors and renderer (S)

New `errc`s, `render_hex`, head decoding for the description line, "keys present" capture in
the struct decoder's error path.

**Exit:** every §9.2 output reproduced from hand-built errors.

### C4 — Reader (L)

`read.hpp`: heads, cursors, `skip_value` validator, strings with borrowing and chunking,
integer-key struct decoding, tags (`expect_tags`, unknown-tag skipping, tag 55799), undefined
policy, typed-array decode, collect mode.

**Exit:** Appendix A decode vectors; every §9.2 error produced from real malformed input at
the right offset; the §7 type set round-trips; borrowing of `span<const std::byte>` verified
to alias the input.

### C5 — Determinism validation and standard tags (M)

`require_deterministic` checks (§8.2); `cbor/tags.hpp` codecs for chrono and URI; bignum
reader support.

**Exit:** every §8.2 row has a positive and negative test; chrono round-trips through tag 1
and tag 0; a COSE_Key and a CWT claims-set example from the RFCs decode into annotated
structs and re-encode byte-identically.

### C6 — Compile-time diagnostics (S)

`cbor::encodable` / `cbor::decodable` / `only<T>`, every §10 message, compile-fail cases.

**Exit:** all messages reproduced; the guard holds on every case.

### C7 — Hardening (L)

Hostile corpus (§13.3), four fuzzers with seed corpora, ASan/UBSan over the suite, round-trip
property tests, transcoding test (§11 rule 6), layering rule for `json/`↔`cbor/`.

**Exit:** §14 criteria 6–10.

### C8 — Benchmarks, documentation, release (M)

`bench/cbor_*.cpp` vs Glaze CBOR and QCBOR, compile-time budget re-recorded with the CBOR TU,
`docs/cbor.md`, updates to annotations/errors/types/customization/compatibility/seam docs,
CHANGELOG 0.2.0, tag.

**Dependency order:** C0 → C1 → C2 → C3 → C4 → C5 → C6 → C7 → C8. C2 and C3 may proceed in
parallel; C5 and C6 may proceed in parallel.

---

## 13. Testing strategy

Same five kinds as v0.1, with the CBOR-specific content below.

### 13.1 Unit — `test/cbor/`

Catch2, one file per concern: `head.cpp` (every additional-info form, shortest-form
selection, all 65,536 half patterns), `encode.cpp`, `decode.cpp`, `deterministic.cpp`,
`tags.cpp`, `typed_array.cpp`, `borrowing.cpp`, `errors.cpp` (§9.2 byte-for-byte),
`options.cpp`. `test/core/` gains `mock_format.cpp` exercising every §3.2/§3.3 hook without
CBOR.

### 13.2 Property — `test/roundtrip/cbor_roundtrip.cpp`

The v0.1 generators over every §7 type plus the CBOR fixtures (integer keys, tags, typed
arrays, spans): encode → decode → structural equality; deterministic encode → decode →
encode byte-identical; deterministic output invariant under member-order permutation and
container iteration order; non-deterministic and deterministic encodings decode to equal
values; every deterministic output passes `require_deterministic`; every non-deterministic
output containing an indefinite length fails it.

### 13.3 Conformance — `test/conformance/`

CBOR has no JSONTestSuite. Three sources stand in:

- **RFC 8949 Appendix A** via the `cbor/test-vectors` repository (`appendix_a.json`, fetched,
  not vendored): every vector with a `decoded` value decodes into `dom::value` equal to it,
  and encodes back to the `hex` bytes when the vector is in preferred form (the file marks
  round-trip ability). Vectors that are non-preferred (e.g. `1.0` as double) decode correctly
  and are rejected under `require_deterministic`.
- **A committed hostile corpus**, `corpus/cbor_hostile/*.cbor`, each with a sidecar `.expect`
  naming the `errc` and offset: truncated heads and payloads at every length class,
  additional info 28–30, simple values 24–31 and 0–19, break outside an indefinite item,
  indefinite string with a non-string or indefinite chunk, tag with no content, tag on a
  break, nesting at `max_depth` and `max_depth + 1`, invalid UTF-8 in text, integer keys of
  every width, negative integers below `INT64_MIN`, a map with 2^32 declared entries and two
  bytes of payload, a byte string declaring 2^64−1 bytes. `skip_value` must reject each with
  the named code at the named offset; the typed DOM path must agree.
- **JSON→CBOR transcoding** over JSONTestSuite `y_` (§11 rule 6).

The test DOM (`test/support/dom.hpp`) gains `bytes`, `tag` and integer-key alternatives so it
can represent the full model; its `codec_for<value, cbor::format>` is the typed path.

### 13.4 Fuzzing — `fuzz/`

Same driver as v0.1 (standalone under GCC, libFuzzer under Clang), ASan+UBSan:

| Harness | Input | Oracle |
|---|---|---|
| `fuzz_cbor_skip` | arbitrary bytes | no crash/UB, terminates, skip path and DOM path agree on accept/reject, rendered error non-empty with offset ≤ size |
| `fuzz_cbor_typed` | arbitrary bytes → `decode<Kitchen>` | same; a success re-encodes and decodes to an equal value; `decode_all` terminates |
| `fuzz_cbor_roundtrip` | structured → deterministic encode → decode → compare | equality, byte-identical re-encode |
| `fuzz_cbor_deterministic` | arbitrary bytes → `decode<dom::value, require_deterministic>` | a success re-encodes byte-identically; a value that decodes without the option and fails with it re-encodes to bytes that then pass |

`Kitchen` for CBOR adds integer keys, tags, typed arrays, `span<const std::byte>` members,
`std::map<std::int64_t, …>`, and an `undefined`-tolerant optional. Seed corpora: Appendix A
hex vectors, the hostile corpus, and encodings of the fixtures. CI runs 60 s per harness,
nightly one hour.

### 13.5 Compile-fail — `test/compile_fail/cbor_*.cpp`

One case per §10 message, plus the two-format pair (a CBOR-only type through
`json::encode`, a JSON-only annotation set through `cbor::encode`) and the option conflict
(`indefinite` with `deterministic`). Same harness and guard.

### 13.6 Layering

`cmake/LayeringCheck.cmake` gains: `json/` includes nothing from `cbor/` and vice versa;
`core/` includes neither. `test/core/` keeps its format-free include tree, now excluding both
format directories.

### 13.7 Performance and compile time

`bench/cbor_encode.cpp` / `cbor_decode.cpp` against Glaze CBOR and QCBOR on the v0.1 corpus
shapes plus two protocol shapes: a CWT claims set (small, integer keys, one tag, one byte
string) and a COSE_Key set (array of small maps with negative integer keys). Deterministic
mode is benchmarked separately from non-deterministic so the cost of buffering runtime maps
is visible. No placement target; a recorded baseline. `bench/compile_time.cpp` gains a CBOR TU
and the budget baseline is re-recorded; the JSON TU's time must not regress from v0.1 by more
than the existing 15% tolerance — that is the measurable form of "C0 changed core without
taxing JSON".

---

## 14. Definition of done

v0.2 ships when all of these hold:

1. The §7 type set encodes and decodes in CBOR with property-based round-trip coverage,
   including byte strings, integer keys, tags and typed arrays natively
2. Every §5 annotation is implemented and tested
3. Deterministic encoding is on by default, reproduces RFC 8949 §4.2 examples, is invariant
   under container iteration and member declaration order, and is validated on read
4. Every §9.2 output is reproduced exactly from real malformed input at the correct offset
5. Every §10 message is reproduced with a compile-fail case; no diagnostic path cascades
6. RFC 8949 Appendix A: every vector decodes to its value; every preferred-form vector
   re-encodes to its bytes
7. The hostile corpus is rejected with the named code at the named offset on both paths
8. Four fuzzers clean for one hour each under ASan+UBSan from the committed seeds
9. The full suite passes under ASan and UBSan
10. `core/`, `json/`, `cbor/` layering verified by CI
11. `test/cross/` asserts §11 rules 1–6 JSON against CBOR; the spike is gone
12. Every v0.1 JSON test passes unchanged, and the JSON compile-time budget holds
13. Benchmarks run and a baseline is recorded for both deterministic and non-deterministic
    modes
14. `docs/cbor.md` and the updated reference docs are complete; `docs/compatibility.md`
    states what v0.2 promises for CBOR
15. CI green on GCC 16; clang-p2996 status recorded

---

## 15. Open questions

| # | Question | Forced at |
|---|---|---|
| 1 | Should `deterministic` really default to on? It makes `std::map<std::string, …>` encoding O(n log n) with a buffer. The scoping document argues yes (signatures); a protocol user never wants the other default, a telemetry user pays for nothing they need. Decision: default on, with `docs/cbor.md` showing the one-line opt-out and the benchmark making the cost visible | C2 |
| 2 | `integer_keys` allocation: start at 1 (COSE convention) or 0? | C0 |
| 3 | Should an integer-keyed struct accept its text names on decode by default? This spec says no (explicit `text_key_alias`), on the grounds that a protocol decoder should not silently accept a producer's mistake | C4 |
| 4 | Do dense integer-key tables beat sorted search at COSE sizes (≤ 16 keys)? Almost certainly; benchmark decides whether the sorted variant is even needed | C4 |
| 5 | Should `require_deterministic` be implied by `cbor::deterministic` on the type (this spec: yes)? The alternative — annotation affects encode only — is what Glaze-style libraries would do; the argument here is that a canonical type is canonical in both directions | C5 |
| 6 | Half-float emission for `float` members that are exactly representable as half: always (this spec) or only under `deterministic`? Always is smaller and still round-trips; the only cost is a producer that expects fixed widths, which `float_width` serves | C1 |
| 7 | Is a `key_order::length_first` (RFC 8949 §4.2.2 / RFC 7049 canonical) option worth adding for interop with older producers? Cheap once buffering exists; decide on demand | post-0.2 |
| 8 | Should the test DOM become a shipped `vcodec::value`, now that it can represent the full model and is needed for transcoding? The v0.1 position (not the library's purpose) still stands; revisit if users ask for JSON↔CBOR conversion | post-0.2 |

---

## 16. What v0.2 does not promise

- The `cbor::` vocabulary may change; it is v0.2's hypothesis as the core vocabulary was
  v0.1's.
- The exact bytes of non-deterministic output may change between versions (deterministic
  output may not — that is the point of it).
- The hook set in §3.2/§3.3 is near-public and may change as a third format (MessagePack is
  the obvious candidate) exerts pressure.
- No ABI, no MSVC, clang-p2996 best-effort, as before.

What v0.2 **does** promise: RFC 8949 conformance, deterministic output stable across
versions for the same type and value, no crash or UB on hostile input, diagnostics as
specified, and JSON behaviour byte-identical to v0.1.

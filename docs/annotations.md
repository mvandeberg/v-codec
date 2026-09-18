# Annotations

Annotations are C++26 P3394 attributes of the form `[[=value]]`, where `value` is a
constant expression of a structural type. vcodec's vocabulary lives in namespace `vcodec`
(format-neutral) and `vcodec::json` (JSON lowerings). Everything here is declared by
`<vcodec/core/annotations.hpp>` and `<vcodec/json/annotations.hpp>`; both are included by
`<vcodec/json.hpp>`.

## Placement

```cpp
// Type level: before the class name.
struct [[=vcodec::rename_all(vcodec::case_::kebab), =vcodec::deny_unknown_fields]] Server {

    // Member level: before the declaration. Several annotations share one bracket pair,
    // each with its own leading '='.
    [[=vcodec::name("tls")]]                       bool secure = false;
    [[=vcodec::alias("retry"), =vcodec::default_value(3)]] int retries;
};

// Enumerator level: AFTER the identifier. This is the grammar, not a vcodec choice
// (docs/design/m0-spellings.md, finding 4).
enum class Color { red [[=vcodec::name("bright-red")]], green, hidden [[=vcodec::skip]] };

// Enum type level: before the enum name.
enum class [[=vcodec::as_integer]] Level : std::int8_t { low = -1, mid, high };
```

An annotation that is a value (`skip`, `required`, `flatten`, ...) is spelled bare. An
annotation that carries a payload is a consteval function call (`name("...")`,
`default_value(3)`, `rename_all(case_::snake)`). Payloads must be structural types; strings
are interned into static storage, so `name("...")` is fine but `default_value(std::string{})`
is not (see [default_value](#default_value)).

A member may carry at most 16 annotations; more is a compile error. Annotations can also be
supplied out of line for a type you do not own, with identical semantics, through
`vcodec::describe<T>` ([customization.md](customization.md#describet)).

## Summary

| Annotation | Level | Encode | Decode |
|---|---|---|---|
| `name("s")` | member, enumerator, type | writes `s` as the key / enumerator text / variant alternative name | accepts `s` |
| `alias("s")` | member, enumerator | no effect | also accepts `s`; repeatable |
| `skip` | member, enumerator | never written | never read; the key is unknown |
| `skip_serializing` | member | never written | read normally |
| `skip_deserializing` | member | written normally | key recognised and skipped silently; never required |
| `skip_if_null` | member (optional-like only) | omitted when empty | no effect |
| `skip_if_default` | member (equality-comparable) | omitted when equal to the member of a default-constructed parent | no effect |
| `required` | member | no effect | key must be present |
| `optional` | member | no effect | key may be absent |
| `default_value(v)` | member | no effect | assigned when the key is absent; makes the member optional |
| `flatten` | member (struct-typed) | nested members spliced into the parent object | same |
| `with<Codec>` | member | `Codec::encode` used for the whole member | `Codec::decode` used |
| `rename_all(case_::…)` | type, enum | transforms identifiers without an explicit `name` | same |
| `deny_unknown_fields` | type | no effect | unknown key is `errc::unknown_field` |
| `transparent` | type (one member) | encodes as its single member | decodes from it |
| `tag("s")` | type or variant member | discriminator key for `std::variant` members | same |
| `as_integer` | enum type | enumerators as their underlying integer | same |
| `json::bytes(enc)` | member (byte-like) | required lowering: `base64`, `base64url`, `hex` | same |
| `json::as_string` | member (integer) | integer written as a JSON string | accepts string or number |
| `json::stringify_keys` | member (map with integral keys) | keys written as decimal strings | keys parsed as decimal |

Not implemented in v0.1 (spec §5.2/§5.3, deferred): `skip_if(pred)`,
`collect_unknown_fields`. Untagged variants are a compile error, not a deferred feature.

## Member-level annotations

### name

```cpp
struct S { [[=vcodec::name("first-name")]] std::string first_name; };
// {"first-name":"…"}
```

Sets the wire name. The wire name is resolved as `name` > `rename_all` > identifier; an
explicit `name` always wins over the type's `rename_all`. The name may contain any
characters; it is escaped when written (`name("a\"b\n")` writes the key `"a\"b\n"`).

Two members of one object resolving to the same wire name, whether through `name`,
`rename_all`, or an `alias`, is a compile error naming both members and the cause:

```
static assertion failed: Server::host and Server::hostname both map to the wire name "host" (via name("host")).
```

`name` is also accepted at type level, where it has exactly one effect: it names the type
as a `std::variant` alternative ([tag](#tag)). It does not rename members and does not
affect a type used anywhere else.

### alias

```cpp
struct S { [[=vcodec::alias("retry"), =vcodec::alias("retries_count")]] int retries = 3; };
```

Adds input names that are accepted in addition to the wire name. Repeatable. Output always
uses the wire name. Aliases participate in duplicate detection (two members sharing an alias,
or an alias equal to another member's wire name, is the same compile error as above) and in
did-you-mean suggestions only through the wire name (suggestions are computed against wire
names, not aliases).

On enumerators, `alias` adds extra accepted spellings for the enumerator's text.

### skip

```cpp
struct S { [[=vcodec::skip]] std::mutex lock; };
```

Removes the member from the schema entirely: never written, never read, never audited. A
member whose type has no codec is a common reason to use it. On input, a key with the skipped
member's name is an unknown key (so `deny_unknown_fields` rejects it).

On an enumerator, `skip` hides the enumerator from both directions: decoding its text is
`errc::unknown_enumerator` (and it is left out of the "expected one of" list), and encoding
a value equal to it throws `vcodec::encode_error` with code `unknown_enumerator` and the
message `enumerator 'hidden' of Color is marked [[=vcodec::skip]] and cannot be encoded`.

### skip_serializing, skip_deserializing

One direction only.

`skip_serializing`: the member is never written but is decoded normally, and its
requiredness is computed by the usual rule.

`skip_deserializing`: the member is written normally. On input its wire name is still known,
so a document that contains it is not rejected by `deny_unknown_fields`; the value is skipped
silently and the member keeps whatever value it had. Such a member is never required.

### skip_if_null

```cpp
struct S { [[=vcodec::skip_if_null]] std::optional<std::string> cert_path; };
```

Omits the key when an optional-like member (`std::optional`, `std::unique_ptr`,
`std::shared_ptr`) is empty. Without it, an empty optional is written as `null`. On any other
type it is a compile error:

```
static assertion failed: Cfg::port (int)
  is declared [[=vcodec::skip_if_null]] but is not optional-like (std::optional, std::unique_ptr, std::shared_ptr).
```

Decoding is unaffected: an absent key leaves the optional empty, and `null` sets it empty.

### skip_if_default

```cpp
struct S { [[=vcodec::skip_if_default]] std::uint16_t port = 8080; std::string host; };
// S{} encodes as {"host":""}; S{.port = 80} encodes as {"port":80,"host":""}
```

Omits the key when the member compares equal to the same member of a default-constructed
parent object (`static const T defaults{}` inside the traversal). That means the comparison
honours default member initialisers: `port` above is skipped when it is `8080`, not when it
is `0`. The parent type must be default-constructible and the member equality-comparable;
otherwise:

```
… is declared [[=vcodec::skip_if_default]] but is not equality-comparable.
```

Decoding is unaffected. Note that `skip_if_default` says nothing about requiredness: a member
with `skip_if_default` and no default member initialiser is still required on input, so a
document produced by encoding it would fail to decode. Give such a member an initialiser or
`default_value`.

Either `skip_if_null` or `skip_if_default` anywhere in a struct makes the number of keys
unknown until runtime, so the struct's map is emitted with an indefinite length. JSON does
not care; CBOR does ([design/data-model.md](design/data-model.md)).

### required and optional

The default rule: a member is optional if its type is optional-like, or it has a default
member initialiser, or it carries `default_value`. Otherwise it is required, and its absence
is `errc::missing_field`. A member with `skip_deserializing` is never required.

```cpp
struct S {
    std::string host;                              // required: no initialiser
    std::uint16_t port = 8080;                     // optional: default member initialiser
    std::optional<std::string> cert;               // optional: optional-like
    [[=vcodec::default_value(3)]] int retries;     // optional: default_value
    [[=vcodec::required]] int id = 0;              // required despite the initialiser
    [[=vcodec::optional]] std::vector<int> tags;   // optional despite no initialiser
};
```

`required` and `optional` override the rule in either direction. A missing optional member
without `default_value` is left as it was: default-constructed by `decode`, unchanged by
`decode_into`.

Requiredness is per declaring type and travels with the member through `flatten` and
inheritance: a required member of a flattened struct is required in the parent object.

### default_value

```cpp
struct S {
    [[=vcodec::default_value(3)]]            int retries;
    [[=vcodec::default_value(2.5)]]          float ratio;
    [[=vcodec::default_value("en")]]         std::string locale;
    [[=vcodec::default_value(Color::green)]] Color accent;
    [[=vcodec::default_value(7)]]            std::optional<int> level;   // engaged with 7
};
```

The value assigned when the key is absent on decode. It makes the member optional. It has no
effect on encode and no effect on construction: `S{}` still has `retries == 0` (or
indeterminate, for a type without an initialiser) until something decodes into it.

The payload must be a structural type, which P3394 requires of every annotation. The
overload set accepts:

- arithmetic types and enums, stored as written and converted to the member's type with
  `static_cast` (so `default_value(2.5)` on a `float` is fine);
- `std::string_view` / string literals, interned into static storage and assigned to any
  member assignable from `std::string_view` or constructible from a `(const char*, const char*)`
  range (`std::string`, `std::string_view`, `std::u8string`, ...);
- for an optional-like member, any of the above for the wrapped type; the optional is engaged.

Anything else is a compile error at the annotation itself:

```
vcodec::default_value(v): v must be a structural type (an arithmetic type, an enum, or a string).
For std::string, std::vector and other non-structural defaults, give the member
[[=vcodec::with<YourCodec>]] and supply the default from the codec (spec §5.1.1).
```

A payload that does not convert to the member's type is caught by the audit:

```
static assertion failed: BadDefault::v (std::vector<int>)
  has a default_value whose type does not convert to the member's type.
```

The escape hatch for non-structural defaults is `with<Codec>`: the codec's `decode` runs only
when the key is present, so give the member a default member initialiser and the codec need
not know about defaults at all. When the key is absent the initialiser stands.

### flatten

```cpp
struct Inner { int x = 0; [[=vcodec::name("why")]] int y = 2; };
struct Outer { std::string host; [[=vcodec::flatten]] Inner inner; };
// {"host":"…","x":0,"why":2}
```

Splices the members of a struct-typed member into the parent object, in place, in the
member's declaration position. The nested struct's own annotations govern its members
(`Inner`'s `name("why")` applies; `Outer`'s `rename_all` would not reach `x` and `y`).
Flatten resolves at schema-extraction time, so duplicate detection sees the spliced names,
`deny_unknown_fields` on the parent covers them, and error paths name the leaf member
directly: a failure in `y` renders as `$.y`, not `$.inner.y`.

Nested flattens resolve recursively, up to a combined flatten-plus-inheritance depth of 8.
Restrictions, each a compile error:

- the member must be a struct that is not optional-like or otherwise a standard shape
  (`Cfg::port is declared [[=vcodec::flatten]] but its type int is not a struct.`);
- a type may not flatten itself directly or indirectly (`flatten cycles are not allowed`).

Inheritance behaves like an implicit flatten: base-class members come first, in base
declaration order, then the derived class's own members. Virtual bases are not supported.

### with<Codec>

```cpp
struct SecondsCodec {
    static void encode(vcodec::core::sink auto& s, std::chrono::seconds const& v) { s.sint(v.count()); }
    static vcodec::status decode(vcodec::core::reader auto& r, std::chrono::seconds& out) {
        auto n = r.expect_sint();
        if (!n) return std::unexpected(std::move(n.error()));
        out = std::chrono::seconds(*n);
        return {};
    }
};
struct Timeout { [[=vcodec::with<SecondsCodec>]] std::chrono::seconds after{30}; };
// {"after":30}
```

Routes the whole member through `Codec` instead of the built-in classification. `Codec` is
any class with the codec shape described in
[customization.md](customization.md#codec-shape); it wins over `codec_for` specialisations
and over reflection. The member's type is not audited, so `with` is also how a member whose
type has no codec (a handle, a third-party type you cannot specialise for) is made encodable.
The codec receives the member's full type; on `std::vector<std::chrono::seconds>` it would
be handed the vector.

## Type-level annotations

### rename_all

```cpp
enum class case_ { camel, pascal, snake, kebab, screaming_snake };

struct [[=vcodec::rename_all(vcodec::case_::camel)]] S { std::string host_name; int maxConnections; };
// {"hostName":…,"maxConnections":…}
```

Transforms every member identifier that has no explicit `name`. The identifier is split into
words on `_` and on lower-to-upper transitions, then rejoined:

| identifier | camel | pascal | snake | kebab | screaming_snake |
|---|---|---|---|---|---|
| `host_name` | `hostName` | `HostName` | `host_name` | `host-name` | `HOST_NAME` |
| `maxConnections` | `maxConnections` | `MaxConnections` | `max_connections` | `max-connections` | `MAX_CONNECTIONS` |
| `port` | `port` | `Port` | `port` | `port` | `PORT` |

Two identifiers that transform to the same name are a compile error
(`Server::host_name and Server::hostName both map to the wire name "host-name" (via rename_all).`).
`rename_all` does not propagate into flattened or nested structs or base classes; each type
applies its own.

On an enum, `rename_all` transforms enumerator identifiers the same way
(`enum class [[=vcodec::rename_all(vcodec::case_::screaming_snake)]] Color { bright_red, green };`
writes `"BRIGHT_RED"` and `"GREEN"`).

### deny_unknown_fields

```cpp
struct [[=vcodec::deny_unknown_fields]] S { int id = 0; std::string email; };
```

By default an unknown key is skipped (its value is validated but discarded). With
`deny_unknown_fields`, it is `errc::unknown_field`, rendered with a did-you-mean suggestion
when a wire name is within Levenshtein distance 2, and a note naming the type:

```
error: unknown field 'emial'
  --> $.users[3].emial
   |  did you mean 'email'?
   |  (Row is declared [[=deny_unknown_fields]])
```

The policy is per type: it applies to the object this type decodes from, including keys
spliced in by `flatten`, and not to nested structs, which carry their own. The key of a
`skip_deserializing` member is known, not unknown. For an internally tagged variant
alternative the discriminator key is not unknown either.

### transparent

```cpp
struct [[=vcodec::transparent]] UserId { std::uint64_t id = 0; };
// UserId{42} encodes as 42, not {"id":42}
```

A single-member wrapper encodes as its member and decodes from it. The type must have exactly
one member after `skip` is applied:

```
static assertion failed: Pair is declared [[=vcodec::transparent]] but has 2 members; transparent requires exactly one.
```

A transparent struct used as a `std::variant` alternative is externally tagged (it has no
object of its own to carry the discriminator).

### tag

`std::variant` members need a discriminator. Without one the type does not compile:

```
static assertion failed: Event::payload (std::variant<A, B>)
  needs a discriminator. Add [[=vcodec::tag("type")]] to Event.
```

`tag("key")` may be placed on the enclosing struct, where it applies to every variant member
of that struct, or on the variant member itself, which wins over the type-level one:

```cpp
struct [[=vcodec::name("circle")]] Circle { double radius = 0; };
struct [[=vcodec::name("rect")]]   Rect   { double w = 0, h = 0; };

struct [[=vcodec::tag("kind")]] Shape {                       // type level: every variant here
    std::variant<Circle, Rect, int> value;
};
struct Event {
    [[=vcodec::tag("type")]] std::variant<Circle, Rect> payload;   // member level
};
```

Alternative names come from a type-level `name("…")` on the alternative (in source or via
`describe<A>::annotations`), else from the type's identifier (`Circle`, `int`), else from its
qualified spelling with the common library aliases restored (`std::string`,
`std::vector<int>`). A `name("…")` is still the stable choice for wire compatibility: the
fallback follows the C++ type name.

Wire shape depends on the alternative:

- a struct alternative (anything traversed by reflection, except `transparent` ones) is
  **internally tagged**: the discriminator is written first, then the members, in one object.
  `Shape{Circle{1.5}}` → `{"value":{"kind":"circle","radius":1.5}}`
- any other alternative is **externally tagged** under the fixed key `"value"`.
  `Shape{7}` → `{"value":{"kind":"int","value":7}}`

On decode the discriminator may appear anywhere in the object; the reader finds it, rewinds
and decodes the chosen alternative. Errors: no discriminator key → `errc::missing_tag`;
unknown discriminator value → `errc::no_variant_alternative`, listing the valid names;
externally tagged with no `"value"` key → `errc::missing_field 'value'`. A variant that is
`valueless_by_exception` throws `encode_error` on encode. Untagged variants (trying each
alternative) are out of scope for v0.1.

## Enum-level annotations

```cpp
enum class Color { red [[=vcodec::name("bright-red")]], green, hidden [[=vcodec::skip]] };
enum class [[=vcodec::as_integer]] Level : std::int8_t { low = -1, mid = 0, high = 1 };
```

By default an enum is written as the enumerator's text: `name` > `rename_all` > identifier.
On decode the text is matched case-sensitively against names and aliases; anything else is
`errc::unknown_enumerator` with the non-skipped names listed. A value that is not an
enumerator at all (`Color(42)`) throws `encode_error` (`value 42 is not an enumerator of Color`).

`as_integer` on the enum type switches to the underlying integer in both directions, signed
or unsigned as the underlying type is. On decode the integer must equal a non-skipped
enumerator; `7` for `Level` is `'7' is not a valid Level`, with the enumerator *names* listed
as the candidates.

`skip` on an enumerator, and `alias` on an enumerator, are described under
[skip](#skip) and [alias](#alias).

## JSON-namespace annotations

These belong to `vcodec::json` because they describe a JSON representation for things the
data model can carry but JSON cannot write natively. They have no meaning to other formats.

### json::bytes

```cpp
struct Claims {
    [[=vcodec::json::bytes(vcodec::json::base64)]]    std::vector<std::byte> nonce;
    [[=vcodec::json::bytes(vcodec::json::base64url)]] std::vector<std::uint8_t> sig;   // promoted to bytes
    [[=vcodec::json::bytes(vcodec::json::hex)]]       std::array<std::byte, 16> id;
};
```

Byte-like members (contiguous ranges of `std::byte`) have no JSON representation and are a
compile error without this annotation:

```
static assertion failed: Claims::nonce (std::vector<std::byte>)
  is byte-like but has no JSON lowering.
  Add [[=vcodec::json::bytes(vcodec::json::base64)]], or mark it [[=vcodec::skip]].
```

Encodings: `base64` (RFC 4648 §4, padded), `base64url` (§5, unpadded), `hex` (lowercase).
Decoding accepts padded or unpadded base64 of the selected alphabet and either case of hex;
malformed text is `errc::type_mismatch` (`expected base64 string, found string`).

`std::vector<std::uint8_t>` and `std::vector<unsigned char>` are arrays of numbers by default;
`json::bytes` promotes them to bytes. That is the only way to get a byte string from a
`uint8_t` container.

### json::as_string

```cpp
struct S { [[=vcodec::json::as_string]] std::uint64_t id = 9007199254740993; };
// {"id":"9007199254740993"}
```

Writes an integer member as a JSON string, for consumers (JavaScript) that cannot hold 64-bit
integers exactly. Decoding accepts either a string containing a plain integer literal or a
bare number; a string that is not an integer is `type_mismatch`, a negative string for an
unsigned member is `out_of_range`. Members without the annotation do not accept strings.

Without `as_string`, integers above 2⁵³ are emitted exactly as numbers.

### json::stringify_keys

```cpp
struct S { [[=vcodec::json::stringify_keys]] std::map<int, std::string> by_id; };
// {"by_id":{"-1":"m","7":"s"}}
```

Maps with integral keys have no JSON representation and are a compile error without it:

```
static assertion failed: Index::by_id (std::map<int, std::string>)
  has integer keys, which JSON cannot represent.
  Add [[=vcodec::json::stringify_keys]], use string keys, or mark it [[=vcodec::skip]].
```

Keys are written as decimal strings and parsed strictly on the way back (`"x"` is
`type_mismatch: expected integer key, found string`; a value outside the key type's range is
`out_of_range`).

## Interactions and propagation

- **Field annotations that select a representation propagate through wrappers and
  elements, but not into nested structs.** `[[=json::as_string]] std::vector<std::uint64_t>`
  writes `["1","2"]`; `[[=json::bytes(hex)]] std::optional<std::vector<std::byte>>` applies to
  the contained value; `[[=json::stringify_keys]] std::vector<std::map<int,int>>` applies to
  each map. A struct element starts a fresh context governed by its own annotations.
- `name` vs `rename_all`: `name` wins. `alias` adds to whichever won.
- `skip` beats everything else on the member; the other annotations are not examined.
- `skip_deserializing` forces the member optional; `required` does not override that.
- `default_value` makes a member optional. Combining it with `required` restores the
  requirement: the key must be present and the default is never applied, so the pairing is
  pointless but harmless.
- `tag` on the member wins over `tag` on the type; the type-level one applies only to
  variant-typed members.
- `describe<T>` annotations are merged with source annotations of the same member; both are
  visible to every rule above.

# Errors

Decoding failures are values, not exceptions: every decode entry point returns a
`vcodec::result<T>` (`std::expected<T, vcodec::error>`), a `vcodec::status`
(`std::expected<void, vcodec::error>`) or a `vcodec::collected<T>`. Encoding cannot fail for
a type that compiles, except for a small set of runtime facts that throw
`vcodec::encode_error` ([below](#encode-side-failures)).

Everything on this page is declared in `<vcodec/error.hpp>` except `render_framed`, which
lives in `<vcodec/json/render_error.hpp>`. Both are included by `<vcodec/json.hpp>`.

## The error type

```cpp
namespace vcodec {

enum class errc : std::uint16_t { /* see below */ };

struct line_col { std::size_t line = 1; std::size_t column = 1; };   // 1-based; column in bytes

struct path_step {
    enum class tag : std::uint8_t { member, index, key_text, key_int };
    tag              kind;
    std::string_view name;    // member: the C++ identifier. key_text: the key
    std::uint64_t    index;   // index / key_int
};

class error {
public:
    explicit error(errc code, std::size_t offset = 0);

    errc                        code()      const noexcept;
    std::span<const path_step>  path()      const noexcept;   // root-first
    std::size_t                 offset()    const noexcept;   // byte offset into the input
    std::optional<line_col>     position()  const noexcept;   // text formats only
    std::string_view            detail()    const noexcept;   // the offending spelling, value or key

    // Used by the renderers; available to callers who render themselves.
    std::size_t                 length()           const noexcept;   // bytes the offending token occupies, 0 if unknown
    std::string_view            expected()         const noexcept;
    std::string_view            found()            const noexcept;
    std::string_view            context()          const noexcept;   // a type or member name
    std::string_view            suggestion()       const noexcept;
    std::span<const std::string> candidates()      const noexcept;   // valid enumerators / alternatives
    std::optional<std::size_t>  container_offset() const noexcept;   // where the enclosing object began
    bool                        path_truncated()   const noexcept;
    bool                        deny_unknown()     const noexcept;
    // ... builders (with_detail, with_expected, push_member, ...) for codecs and readers
};

template<class T> using result = std::expected<T, error>;
using status = std::expected<void, error>;

template<class T>
struct collected {
    T                  value{};
    std::vector<error> errors;
    bool complete() const noexcept;      // errors.empty()
    explicit operator bool() const noexcept;
};

constexpr std::string_view to_string(errc) noexcept;   // the enumerator's name
constexpr bool is_syntax_error(errc) noexcept;

}
```

`error` is a handle to a heap-allocated block, so `result<T>` stays small on the success path;
copies are deep. `position()` is an `std::optional` because binary formats have no line and
column: the JSON reader always sets it, computed from the offset. Which of the extended
accessors are populated depends on the code; the table under
[Renderers](#renderers) says which the framed renderer uses.

### Error codes

```cpp
enum class errc : std::uint16_t {
    // syntax: the input is not JSON
    unexpected_token, unterminated_string, invalid_escape, invalid_utf8,
    invalid_number, trailing_content, depth_exceeded, truncated,
    // schema: valid JSON that does not fit the type
    type_mismatch, missing_field, unknown_field, duplicate_key,
    out_of_range, unknown_enumerator, no_variant_alternative, missing_tag,
    // policy: valid and well-typed, but a rule of the call forbids it
    escape_in_borrowed_string, non_text_key,
};
```

The three groups matter for recovery. `is_syntax_error(c)` is true for the first group; a
syntax error means the reader's position is meaningless and nothing after it can be trusted,
so [`decode_all`](#collect-all-decode_all) stops there. Schema and policy errors leave the
reader positioned at a known value, which is what makes member-granularity recovery possible.

## Paths

The path is built while the failure unwinds: each frame that receives a failed result pushes
its own step, and the public entry points reverse the steps into root-first order. Success
pays nothing for this. The path buffer holds 16 steps; deeper failures keep the 16 steps
nearest the leaf and set `path_truncated()`.

Rendering, shared by both renderers:

| Step | Rendered | Produced by |
|---|---|---|
| root | `$` | every path starts here |
| `member` | `.name` | a struct member, using the **C++ identifier**, not the wire name |
| `index` | `[3]` | array element, tuple element, fixed-array element |
| `key_text` | `.key` if the key is non-empty and only `[A-Za-z0-9_-]`, else `["the key"]` | map entry with a text key; an unknown field; a variant discriminator |
| `key_int` | `[7]` | map entry with an integral key |
| truncation | `$.…[truncated]` prefix (U+2026) | more than 16 steps |

```
$.users[3].name          member users, index 3, member name
$.groups.g[1].n          member groups, key "g", index 1, member n
$.m[2].plain-key["needs quoting"][9]
```

Two rules follow from "member steps use the identifier". A member renamed with `name` or
`rename_all` still appears under its C++ spelling (`host_name`, not `host-name`), because the
path locates a place in *your type*. A flattened member appears directly under its parent
(`$.x`, not `$.inner.x`), because flattening is invisible to traversal. The wire spelling is
in `detail()` when it matters (`missing_field`, `unknown_field`, `duplicate_key`).

A `missing_field` error's path is the object that lacks the member, not the member itself;
the member's wire name is in `detail()` and the object's start in `container_offset()`.

## Renderers

Both are free functions in namespace `vcodec` and neither consults the input except to cut an
excerpt; render however you like from the accessors if these do not suit.

```cpp
std::string render_terse(error const& e);                             // one line
std::string render_framed(error const& e, std::string_view input);    // multi-line with excerpt
```

`render_terse`: `error: <headline> at <path> (byte offset <N>)`.

`render_framed`: the headline, a `-->` line with the path, then code-specific lines:

| Code | Extra lines |
|---|---|
| `missing_field`, `missing_tag` | `object begins at byte offset N` (from `container_offset()`) |
| `unknown_field` | `did you mean 'x'?` when `suggestion()` is set; `(T is declared [[=deny_unknown_fields]])` when `deny_unknown()` |
| `unknown_enumerator`, `no_variant_alternative` | `expected one of: a, b, c` from `candidates()` |
| `out_of_range` | none |
| everything else | a source excerpt with a caret line (only when `input` is non-empty), then `suggestion()` if set |

Pass an empty `input` when the buffer is gone; the excerpt is simply omitted.

### The excerpt window

The excerpt is structural, not a fixed number of bytes: it runs from the start of the
previous sibling value to the end of the next one, at the failing token's own nesting level,
never crossing a newline, and each scan is bounded at 96 bytes. Content cut on either side is
marked with `…` (U+2026). The caret line puts one `^` per byte of `length()` under the token
(at least one). Tabs in the excerpt are shown as spaces and other control bytes as `?`, so
the caret column stays aligned.

That is why the §9.4 example reads the same for compact and pretty-printed input:

```
   |  …"id": 7, "name": 42, "email": "a@b.c"…
   |                    ^^ at byte offset 1284
```

Did-you-mean is Levenshtein distance ≤ 2 against the type's wire names, computed only on the
error path.

## One example per code

Each example gives the C++ type, the input, and the exact `render_framed` output (with the
input passed). All of these are produced by the JSON reader from real input; the first five
are asserted byte for byte by `test/json/decode_errors.cpp`.

### Syntax

**`unexpected_token`** — a byte that cannot start what the grammar (or the expected type)
requires. `detail()` is the byte, `expected()` what was wanted.

```cpp
struct S { std::vector<int> a; };
```
```
input: {"a": tru}

error: unexpected token 't', expected array
  --> $.a
   |
   |  …"a": tru…
   |        ^^^ at byte offset 6
```

Also produced for: a trailing comma (`[1,]`), a missing comma (`[1 2]`), an unquoted key, a
misspelt literal (`trux`), `+1`, form feed used as whitespace, and a raw control character
inside a string (`detail()` is then `control character`, `expected()` is
`escaped control character`).

**`unterminated_string`** — the input ends inside a string. The offset is the opening quote;
`length()` runs to the end of the input.

```cpp
struct Msg { std::string text; };
```
```
input: {"text": "abc

error: unterminated string
  --> $.text
   |
   |  …"text": "abc
   |           ^^^^ at byte offset 9
```

**`invalid_escape`** — a backslash followed by something that is not a JSON escape, a `\u`
without four hex digits, or a lone surrogate. `detail()` is the escape text, or
`lone high surrogate` / `lone low surrogate`.

```
input: {"text": "a\qb"}

error: invalid escape sequence '\q'
  --> $.text
   |
   |  {"text": "a\qb"}
   |             ^^ at byte offset 11
```
```
input: {"text": "\ud800x"}

error: invalid escape sequence 'lone high surrogate'
  --> $.text
   |
   |  {"text": "\ud800x"}
   |            ^^^^^^ at byte offset 10
```

**`invalid_utf8`** — a string's raw bytes are not well-formed UTF-8 (overlong forms, encoded
surrogates and code points above U+10FFFF included). Checked only when
`options::validate_utf8` is true (the default). The offset is the first bad byte.

```
input: {"text": "caf\xe9"}          (the byte 0xE9, Latin-1 é)

error: invalid UTF-8
  --> $.text
   |
   |  {"text": "caf?"}
   |               ^ at byte offset 13
```

(The excerpt shows the raw byte; it is reproduced here as `?` because it is not printable on
its own.)

**`invalid_number`** — a token that starts like a number but is not one: a leading zero, a
bare `-`, a trailing `.`, `1.e5`, and so on. `detail()` is the token.

```cpp
struct S { std::vector<int> a; };
```
```
input: {"a": [01]}

error: invalid number '01'
  --> $.a[0]
   |
   |  …01…
   |   ^^ at byte offset 7
```

**`trailing_content`** — something other than whitespace follows the top-level value.

```cpp
struct Point { int x = 0; int y = 0; };
```
```
input: {"x":1,"y":2} x

error: trailing content after value
  --> $
   |
   |  {"x":1,"y":2} x
   |                ^ at byte offset 14
```

**`depth_exceeded`** — nesting deeper than `options::max_depth` (default 256). `detail()` is
the limit. Applies equally to values being skipped.

```cpp
constexpr vcodec::json::options shallow{ .max_depth = 4 };
using Deep = std::vector<std::vector<std::vector<std::vector<std::vector<int>>>>>;
auto r = vcodec::json::decode<Deep, shallow>("[[[[[1]]]]]");
```
```
error: nesting depth exceeds 4
  --> $[0][0][0][0]
   |
   |  …[1]…
   |   ^ at byte offset 4
```

**`truncated`** — the input ends where more was required. `expected()` says what
(`',' or ']'`, `key`, `':'`, `'true'`, ...). The offset is the end of the input, and the
excerpt caret sits after the last byte.

```cpp
struct S { std::vector<int> a; };
```
```
input: {"a": [1

error: unexpected end of input
  --> $.a
   |
   |  …1
   |    ^ at byte offset 8
```

An empty (or all-whitespace) input is `truncated` at offset 0 with path `$`.

### Schema

**`type_mismatch`** — the value is well-formed JSON of the wrong kind for the type.
`expected()` and `found()` are the two kinds. Besides the obvious cases (`string` vs
`number`, `object` vs `array`, `null` for a non-optional), it covers: a fraction or exponent
where an integer is required (`expected integer, found number`); a fixed array or tuple of
the wrong arity (`expected array of 2 elements, found array of 3 elements`); a `char`
member given more than one character (`expected single-character string, found string of length 2`);
a byte string that does not decode under the selected `json::bytes` encoding
(`expected base64 string, found string`) or that has the wrong length for an
`std::array<std::byte, N>`; a non-numeric key for a `stringify_keys` map
(`expected integer key, found string`); a non-integer string for an `as_string` member.

```cpp
struct User { int id = 0; std::string name; std::string email; };
struct Server { std::vector<User> users; };
```
```
input: {"users":[ …three users… , {     "id": 7, "name": 42, "email": "a@b.c"}]}

error: expected string, found number
  --> $.users[3].name
   |
   |  …"id": 7, "name": 42, "email": "a@b.c"…
   |                    ^^ at byte offset 1284
```

**`missing_field`** — a required member is absent. `detail()` is the wire name; the path and
`container_offset()` identify the object. Also produced when an externally tagged variant
alternative lacks its `"value"` key.

```
input: {"users":[ … , {"id": 7, "name": "n"}]}

error: missing required field 'email'
  --> $.users[3]
   |  object begins at byte offset 1261
```

**`unknown_field`** — only for types declared `[[=vcodec::deny_unknown_fields]]`; otherwise
unknown keys are skipped. `detail()` is the key, `suggestion()` the nearest wire name within
distance 2 if any, `context()` the type.

```cpp
struct [[=vcodec::deny_unknown_fields]] Row { int id = 0; std::string email; };
struct Doc { std::vector<Row> users; };
```
```
input: {"users":[{"id":1,"email":"x"},{"id":2,"email":"x"},{"id":3,"email":"x"},{"id":4,"emial":"x"}]}

error: unknown field 'emial'
  --> $.users[3].emial
   |  did you mean 'email'?
   |  (Row is declared [[=deny_unknown_fields]])
```

**`duplicate_key`** — a struct key appears twice and `options::duplicates` is
`duplicate_key::error`. With the default `last_wins` the later value replaces the earlier;
with `first_wins` the later value is skipped. The offset is the second occurrence.

```cpp
constexpr vcodec::json::options strict{ .duplicates = vcodec::duplicate_key::error };
auto r = vcodec::json::decode<Point, strict>(R"({"x":1,"x":2,"y":0})");
```
```
error: duplicate key 'x'
  --> $.x
   |
   |  …"x":1,"x":2,"y":0…
   |         ^^^ at byte offset 7
```

The policy applies to struct members. Maps (`std::map` and other `map_like` types) are not
subject to it; see [types.md](types.md#maps).

**`out_of_range`** — an integer that does not fit the member's type, in either direction.
`detail()` is the literal, `context()` the type, spelled by its fixed-width name
(`std::uint8_t`, `std::int64_t`; plain `int` stays `int`). Also: an integer literal too large
for 64 bits where an integer is required, a floating-point literal whose magnitude overflows
`double` (`context()` is `double`; underflow to zero is *not* an error), and a `stringify_keys`
key outside the key type.

```cpp
struct Config   { std::uint8_t retries = 0; };
struct Settings { Config config; };
```
```
input: {"config": {"retries": 512}}

error: value 512 out of range for std::uint8_t
  --> $.config.retries
```

**`unknown_enumerator`** — text (or, with `as_integer`, a number) that names no enumerator,
or names one marked `[[=vcodec::skip]]`. `candidates()` lists the non-skipped names.

```cpp
enum class Color { red [[=vcodec::name("bright-red")]], green };
struct Theme  { Color accent = Color::green; };
struct Themed { Theme theme; };
```
```
input: {"theme": {"accent": "BRIGHT_RED"}}

error: 'BRIGHT_RED' is not a valid Color
  --> $.theme.accent
   |  expected one of: bright-red, green
```

**`no_variant_alternative`** — the discriminator names no alternative. The path ends in the
discriminator key; `candidates()` lists the alternative names.

```cpp
struct A { int a = 0; };
struct B { int b = 0; };
struct [[=vcodec::tag("type")]] Ev { std::variant<A, B> payload; };
```
```
input: {"payload": {"type": "C", "a": 1}}

error: 'C' is not an alternative of std::variant<A, B>
  --> $.payload.type
   |  expected one of: A, B
```

**`missing_tag`** — the object for a variant member has no discriminator key at all.

```
input: {"payload": {"a": 1}}

error: missing discriminator 'type'
  --> $.payload
   |  object begins at byte offset 12
```

### Policy

**`escape_in_borrowed_string`** — a `std::string_view` (or `std::u8string_view`) member, or
a `std::string_view` map key, met a string containing escapes. A view can only alias the
input, and unescaping needs storage the library does not own. `context()` is the member
(`S::v`) or, for a top-level or element view, the type.

```cpp
struct S { std::string_view v; };
```
```
input: {"v": "a\nb"}

error: string contains escapes but S::v borrows from the input
  --> $.v
   |
   |  …"v": "a\nb"…
   |        ^^^^^^ at byte offset 6
   |  use std::string instead of std::string_view
```

**`non_text_key`** — a format delivered a non-text map key where a text key was required.
The JSON reader never produces it, since every JSON key is text (integral keys for
`stringify_keys` maps arrive as strings and are parsed); it exists for binary formats whose
readers can hand back integer keys. A reader that produces it sets `found()`; rendered:

```
error: expected text key, found integer
  --> $.by_id
```

## Collect-all: `decode_all`

```cpp
template<class T, vcodec::json::options Opts = {}>
vcodec::collected<T> decode_all(std::string_view input);
```

`decode_all` forces `options::errors = error_mode::collect` and recovers at
**object-member granularity**: when decoding a struct member fails with a schema or policy
error, the error is recorded, the reader rewinds to the start of that member's value, skips
it, and continues with the next key. Missing required members, denied unknown fields and
duplicate keys (under `duplicate_key::error`) are recorded the same way. Recovery happens
only in struct frames; a failure inside an array or map element is attributed to the
enclosing struct member, which is then skipped as a whole. Elements decoded before the
failure remain in the container.

Syntax errors are unrecoverable by nature and end the run; the syntax error is appended last.
Trailing content after the value is appended last as well.

```cpp
struct Coord { int x; int y; };
struct Cfg { std::uint8_t a = 0; std::string b; Coord p{}; int c = 0; std::vector<int> v; };

auto r = vcodec::json::decode_all<Cfg>(R"({"a":300,"b":1,"p":{"x":"no","y":2},"c":5,"v":[1,"x"]})");
// r.complete() == false, r.errors.size() == 4, r.value.c == 5, r.value.p.y == 2, r.value.v == {1}
for (auto const& e : r.errors) std::cout << vcodec::render_terse(e) << '\n';
```
```
error: value 300 out of range for std::uint8_t at $.a (byte offset 5)
error: expected string, found number at $.b (byte offset 13)
error: expected integer, found string at $.p.x (byte offset 24)
error: expected integer, found string at $.v[1] (byte offset 49)
```

A missing member and a syntax error, for contrast:

```cpp
auto m = vcodec::json::decode_all<Coord>(R"({"x":"one"})");
// error: expected integer, found string at $.x (byte offset 5)
// error: missing required field 'y' at $ (byte offset 0)

auto s = vcodec::json::decode_all<Cfg>(R"({"a":300,"b":)");
// error: value 300 out of range for std::uint8_t at $.a (byte offset 5)
// error: unexpected end of input at $.b (byte offset 13)
```

`collected<T>::value` is always a complete object of `T`: members that failed keep their
default-constructed value, members that succeeded hold what was decoded. Check `complete()`
before trusting it.

Setting `.errors = error_mode::collect` on `decode` or `decode_into` directly makes the
reader recover at member granularity, but those entry points can return only one error, so
they return the first one collected (and `decode_into` leaves the successfully decoded
members in place). Use `decode_all` to receive every error.

## Encode-side failures

Sinks do not return errors, and for well-typed input encoding cannot fail. The exceptions are
facts the type system cannot know, thrown as `vcodec::encode_error`
(derived from `std::runtime_error`):

```cpp
class encode_error : public std::runtime_error {
public:
    errc                       code() const noexcept;
    std::span<const path_step> path() const noexcept;    // root-first
    bool                       path_truncated() const noexcept;
};
```

| Cause | `code()` | `what()` |
|---|---|---|
| a `[[=skip]]`ped enumerator value | `unknown_enumerator` | `enumerator 'hidden' of Color is marked [[=vcodec::skip]] and cannot be encoded` |
| an enum value that is not an enumerator | `unknown_enumerator` | `value 42 is not an enumerator of Color` |
| nesting deeper than `max_depth`, typically a cycle through smart pointers | `depth_exceeded` | `nesting depth exceeds max_depth (256); a cycle through smart pointers is the usual cause` |
| a string that is not valid UTF-8, with `validate_utf8` on | `invalid_utf8` | `string contains invalid UTF-8 at byte 4` |
| a `std::variant` that is `valueless_by_exception` | `no_variant_alternative` | `variant is valueless_by_exception` |

The path is built by the same unwind rule as decode errors, so a skipped enumerator inside
`rows[1].c` reports the steps `rows`, `1`, `c`. There is no framed renderer for
`encode_error` (there is no input to excerpt); render the path with the same rules as above
if needed.

## Producing errors from a custom codec

A codec's `decode` receives the reader and can fail with any `errc`. Build the error through
the reader's factory so it carries the offset and position, then add what the renderer will
need:

```cpp
static auto decode(vcodec::core::reader auto& r) -> vcodec::result<my::Rgb> {
    std::size_t off = r.offset();
    auto t = r.expect_text();
    if (!t) return std::unexpected(std::move(t.error()));
    if (!looks_like_colour(t->text))
        return std::unexpected(r.error(vcodec::errc::type_mismatch, off)
            .with_expected("colour like \"#rrggbb\"").with_found("string")
            .with_length(t->text.size() + 2));
    return parse_colour(t->text);
}
```

```
error: expected colour like "#rrggbb", found string
  --> $.colours[0]
   |
   |  …"blue"…
   |   ^^^^^^ at byte offset 12
```

The path step for the codec's member is added by the frame that called it; a codec that
itself iterates a container should `push_index` / `push_key` on the way out, as the test DOM
in `test/support/dom.hpp` does. See [customization.md](customization.md).

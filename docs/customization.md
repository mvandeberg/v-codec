# Customization

Four extension points, from the smallest to the largest:

| You want to | Use |
|---|---|
| change how one member is encoded | [`with<Codec>`](#withcodec) on the member |
| give a type you do not own its own representation | [`codec_for<T, Format>`](#codec_for) |
| annotate a struct you do not own, but still traverse it by reflection | [`describe<T>`](#describet) |
| drive the traversal into something other than a `std::string` of JSON | a [custom sink](#writing-a-sink) or [reader](#writing-a-reader), possibly a [new format](#adding-a-format) |

The first three are declared by `<vcodec/json.hpp>` and `<vcodec/cbor.hpp>` alike. Sinks,
readers and formats need the `core/` headers, all of which `<vcodec/vcodec.hpp>` includes.

## Lookup order

For every value the traversal meets, the first of these that applies wins:

1. `[[=vcodec::with<Codec>]]` on the member being encoded or decoded
2. `vcodec::codec_for<T, Format>` where `Format` is the sink's or reader's format
   (`vcodec::json::format` for JSON, `vcodec::cbor::format` for CBOR)
3. `vcodec::codec_for<T, void>`, the format-independent codec
4. on encode only, the sink's `write_prepared<T>(value)` hook
5. the built-in classification ([types.md](types.md#summary-of-the-classification-order)),
   which ends in reflection over the members, using `describe<T>` if there is one

Steps 1–3 replace everything below them: a type with a codec is never audited or reflected,
and its members need not be encodable themselves. `codec_for` applies wherever `T` appears
(as a member, an element, an optional's payload); `with<Codec>` applies to the one member
and receives that member's whole type.

## Codec shape

A codec is a class with two static member functions. It is the same shape for `codec_for`
and for `with<Codec>`:

```cpp
struct MyCodec {
    static void encode(vcodec::core::sink auto& s, T const& v);

    // either form; if both exist, the status form is used
    static vcodec::result<T> decode(vcodec::core::reader auto& r);
    static vcodec::status    decode(vcodec::core::reader auto& r, T& out);
};
```

`encode` emits model tokens into the sink and cannot fail (throw `vcodec::encode_error` for
the rare value that cannot be represented). `decode` reads from the reader and returns an
error rather than throwing; the frame that called the codec adds the member's path step.
The `status` form decodes in place, which suits `decode_into` and types that are expensive
to move; the `result<T>` form is simpler when construction needs the parsed pieces. A codec
that only encodes leaves out `decode`; the type is then encode-only and decoding it is a
compile error.

### Field-aware codecs

A codec may additionally (or instead) provide **field-aware** overloads that receive the
member's annotations as a `core::field_meta` template parameter:

```cpp
struct MyCodec {
    template<vcodec::core::field_meta F>
    static void encode(vcodec::core::sink auto& s, T const& v);          // F.anns(): the member's annotations

    template<vcodec::core::field_meta F>
    static vcodec::status decode(vcodec::core::reader auto& r, T& out);  // or result<T> decode<F>(r)
};
```

The traversal prefers `encode<F>` / `decode<F>` when they are well-formed for the call and
falls back to the plain forms otherwise; for a value that is not a member (a top-level value,
a container element) `F` is `core::no_field`, whose `anns()` is empty. Query annotations with
`core::has_annotation<A>(F.anns())` and `core::find_annotation<A>(F.anns())`. The built-in
CBOR time-point codec is the model: its `encode<F>` checks for `cbor::tag(0)` on the member
and writes RFC 3339 text instead of epoch seconds (`<vcodec/cbor/tags.hpp>`). Note that the
*tags* around the value are not the codec's business: `format_traits::expected_tags` emits
and verifies them, and the codec writes only the payload.

## codec_for

```cpp
namespace vcodec {
template<class T, class Format = void> struct codec_for;    // primary undefined
}
```

Specialise for the type, per format or generically. A specialisation must be complete
(defined, not just declared) to count. `T` is matched after removing `const`, `volatile`
and references.

```cpp
namespace third_party { struct Rgb { std::uint8_t r = 0, g = 0, b = 0; }; }

template<>
struct vcodec::codec_for<third_party::Rgb, vcodec::json::format> {
    static void encode(vcodec::core::sink auto& s, third_party::Rgb const& v) {
        char buf[8] = { '#' };
        static constexpr char hex[] = "0123456789abcdef";
        std::uint8_t c[3] = { v.r, v.g, v.b };
        for (int i = 0; i < 3; ++i) { buf[1 + 2 * i] = hex[c[i] >> 4]; buf[2 + 2 * i] = hex[c[i] & 15]; }
        s.text(std::string_view(buf, 7));
    }

    static auto decode(vcodec::core::reader auto& r) -> vcodec::result<third_party::Rgb> {
        std::size_t off = r.offset();                 // before consuming, for the error
        auto t = r.expect_text();
        if (!t) return std::unexpected(std::move(t.error()));
        std::string_view s = t->text;
        auto bad = [&] {
            return std::unexpected(r.error(vcodec::errc::type_mismatch, off)
                .with_expected("colour like \"#rrggbb\"").with_found("string")
                .with_length(s.size() + 2));
        };
        if (s.size() != 7 || s[0] != '#') return bad();
        std::uint8_t c[3]{};
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 2; ++k) {
                char ch = s[1 + 2 * i + k];
                int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
                if (d < 0) return bad();
                c[i] = std::uint8_t(c[i] * 16 + d);
            }
        return third_party::Rgb{ c[0], c[1], c[2] };
    }
};

struct Palette { std::vector<third_party::Rgb> colours; };
// {"colours":["#ff0010"]}
```

A failure inside the codec renders like any other error, with the path supplied by the
caller:

```
error: expected colour like "#rrggbb", found string
  --> $.colours[0]
   |
   |  …"blue"…
   |   ^^^^^^ at byte offset 12
```

Two points about `decode` that the reader contract makes possible:

- **Take `r.offset()` before the `expect_*` call.** Every `expect_*` either consumes the item
  or fails without consuming, so on failure the reader is still at the item, but on success
  it has moved past it and the offset for a semantic error must have been saved.
- **Build errors with `r.error(code, offset)`.** The factory fills in the offset and, for
  text formats, line and column. Add `with_expected` / `with_found` / `with_detail` /
  `with_context` / `with_suggestion` / `with_length` as the renderer for that code expects
  ([errors.md](errors.md#renderers)).

### Format-specific vs generic

`codec_for<T, vcodec::json::format>` applies only when the sink or reader declares
`using format = vcodec::json::format;`, and `codec_for<T, vcodec::cbor::format>` under
`vcodec::cbor::format`. `codec_for<T, void>` applies under every format that has no more
specific codec. The distinction exists because a `std::chrono::time_point` wants ISO 8601
text in JSON and a tagged epoch number in CBOR: the library ships the CBOR one
(`codec_for<std::chrono::sys_time<D>, cbor::format>`, tag 1) and leaves the JSON one to
you. Write the generic one when the model tokens you emit are right everywhere (a number, a
string, an array of numbers).

A format-specific codec may use members of that format's reader beyond the `core::reader`
concept. The CBOR reader's `expect_bignum()` (tags 2 and 3) is the case in point; constrain
the overload on the member rather than naming `cbor::reader<>`, so it matches every option
set ([cbor.md](cbor.md#bignums-tags-2-and-3)).

### Recursive and generic types

A `codec_for` may call itself for nested values and may cooperate with the built-in traversal
by calling `vcodec::core::encode_value<vcodec::core::no_field>(s, x)` and
`vcodec::core::decode_value<vcodec::core::no_field>(r, x)` for element types it does not
want to handle itself. The test-only generic JSON value in `test/support/dom.hpp` is a
complete example of a recursive codec written against the reader's cursors, including
pushing `push_index` / `push_key` onto errors so that failures deep inside it still carry a
path.

## with<Codec>

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

`with<Codec>` is a member annotation naming a codec class of the shape above. It applies to
the member's full type and takes precedence over any `codec_for`. Because the member's type
is never audited, it is also how a member whose type has no codec is made encodable without
specialising anything at namespace scope, and how a non-structural default is supplied: give
the member a default member initialiser and the codec runs only when the key is present
([annotations.md](annotations.md#default_value)).

## describe\<T\>

For a struct you do not own, `describe<T>` supplies the annotations that source annotations
would have. Members are still enumerated by reflection; `describe` only adds to them.

```cpp
namespace third_party { struct Point { int x = 0; int y = 0; int cache = 0; }; }

template<>
struct vcodec::describe<third_party::Point> {
    static constexpr auto annotations = vcodec::type_annotations(vcodec::deny_unknown_fields);
    static constexpr auto members = vcodec::members(
        vcodec::member("x", vcodec::name("X")),
        vcodec::member("y", vcodec::name("Y")),
        vcodec::member("cache", vcodec::skip));
};
// third_party::Point{3, 4, 99} encodes as {"X":3,"Y":4}
```

- `annotations` is optional and takes type-level annotations (`rename_all`,
  `deny_unknown_fields`, `transparent`, `tag`, `name`).
- `members` is optional and takes `member("identifier", annotations...)` entries. The
  identifier is the C++ member name; naming a member that does not exist is a compile error
  (`vcodec::describe<Point>: 'z' is not a non-static data member of Point.`).
- At most 8 annotations per `member(...)` and per `type_annotations(...)`.
- Annotations from `describe<T>` are merged with any source annotations on the same member,
  and every rule (requiredness, duplicates, flatten, tag placement) sees the union.
- `describe<E>` on an enum supports `annotations` (`rename_all`, `as_integer`) only; there is
  no out-of-line form for enumerator-level `name`, `alias` or `skip`.
- A type-level `name("...")` given through `describe<A>::annotations` names `A` as a variant
  alternative, which is the way to give a standard-library alternative a sensible tag.

## Writing a sink

A sink receives the encode-side token stream. The concept, from `<vcodec/core/model.hpp>`:

```cpp
template<class S>
concept sink = requires(S& s, std::string_view t, std::span<const std::byte> b,
                        std::optional<std::size_t> n, std::uint64_t u, std::int64_t i,
                        double d, bool flag) {
    { s.null()            } -> std::same_as<void>;
    { s.boolean(flag)     } -> std::same_as<void>;
    { s.uint(u)           } -> std::same_as<void>;
    { s.sint(i)           } -> std::same_as<void>;
    { s.real(d)           } -> std::same_as<void>;
    { s.text(t)           } -> std::same_as<void>;
    { s.bytes(b)          } -> std::same_as<void>;
    { s.begin_array(n)    } -> std::same_as<void>;   // n: definite length, or nullopt
    { s.end_array()       } -> std::same_as<void>;
    { s.begin_map(n)      } -> std::same_as<void>;
    { s.end_map()         } -> std::same_as<void>;
    { s.key(t)            } -> std::same_as<void>;   // text key
    { s.key(u)            } -> std::same_as<void>;   // unsigned integer key
    { s.key(i)            } -> std::same_as<void>;   // signed integer key
    { s.tag(u)            } -> std::same_as<void>;   // semantic tag; JSON drops it
};
```

Sinks return nothing: there is no error path per token. Failures are either compile-time or
resource exhaustion, which propagates as an exception from the buffer. Keys are always
followed by exactly one value; `begin_*` is always matched by `end_*`. The meaning of each
token is in [design/data-model.md](design/data-model.md).

A complete minimal sink, which counts tokens:

```cpp
struct counting_sink {
    std::size_t values = 0, keys = 0, containers = 0;
    void null() { ++values; }
    void boolean(bool) { ++values; }
    void uint(std::uint64_t) { ++values; }
    void sint(std::int64_t) { ++values; }
    void real(double) { ++values; }
    void text(std::string_view) { ++values; }
    void bytes(std::span<const std::byte>) { ++values; }
    void begin_array(std::optional<std::size_t>) { ++containers; }
    void end_array() {}
    void begin_map(std::optional<std::size_t>) { ++containers; }
    void end_map() {}
    void key(std::string_view) { ++keys; }
    void key(std::uint64_t) { ++keys; }
    void key(std::int64_t) { ++keys; }
    void tag(std::uint64_t) {}
};
static_assert(vcodec::core::sink<counting_sink>);

counting_sink cs;
vcodec::json::encode_into(cfg, cs);      // audited under the JSON rules, then driven
```

`vcodec::json::encode_into(v, s)` runs the JSON audit (so `T` must be `encodable` for JSON)
and then drives any sink. `vcodec::core::encode(s, v)` from `<vcodec/core/traverse.hpp>`
drives a sink with no audit at all, which is what a new format uses together with its own
gate.

### Optional hooks

The traversal looks for these and uses them when present; none is required.

| Hook | Effect |
|---|---|
| `using format = F;` | selects `codec_for<T, F>` and `format_traits<F>`; without it the format is `void` |
| `template<vcodec::core::static_string Name> void prepared_key();` | called instead of `key(Name.view())` for struct member keys; `Name` is a compile-time constant, so the wire form can be a `static constexpr` blob appended in one `memcpy`. The JSON writer builds `"name":` (plus a space when pretty) this way. |
| `template<std::int64_t Key> void prepared_key();` | the integer-key counterpart, called for struct members whose `format_traits::integer_key<F>()` is set; the CBOR writer keeps the encoded key as a `static constexpr` blob. |
| `template<vcodec::core::field_meta F> void uint(std::uint64_t);`<br>`… void sint(std::int64_t);`<br>`… void real(double);`<br>`… void text(std::string_view);`<br>`… void bytes(std::span<const std::byte>);` | field-aware overloads, called instead of the plain ones when the value belongs to a member; `F.anns()` gives the member's annotations. The JSON writer implements `json::as_string` and `json::bytes` through `uint`/`sint`/`bytes`; the CBOR writer implements `cbor::float_width` through `real<F>` and `cbor::indefinite` chunking through `text<F>` / `bytes<F>`. |
| `template<vcodec::core::field_meta F, class R> void write_range(R const& r);` | bulk-range hook: when `format_traits::bulk_range<F, R>()` is true for the member, the traversal calls this once with the whole range instead of `begin_array` + elements. The CBOR writer emits an RFC 8746 tag and one byte string. |
| `void begin_map_ordered(std::optional<std::size_t> n);` | called instead of `begin_map(n)` for a struct's map, whose entries arrive in the format's `member_order`; a sink that sorts maps (deterministic CBOR) can skip buffering. Not called when a variant discriminator is spliced in at runtime. |
| `template<class T> void write_prepared(T const& v);` | whole-value fast path: when it is viable for `T`, the traversal calls it instead of visiting `T`'s members. Constrain it (`requires ...`) to the types you handle. Neither shipped writer uses it. |

The field-aware overloads shadow the plain ones inside a class; bring the base versions back
with `using base::uint;` if you derive, as `test/support/recording_sink.hpp` does.

Tags are emitted by the traversal, not by the sink: `format_traits::expected_tags<F, T>()`
lists them for a member and core calls `s.tag(t)` for each before the value (never before a
`null` from an empty optional).

The JSON writer itself, `vcodec::json::writer<Opts>` in `<vcodec/json/write.hpp>`, is a
sink over a `std::string&` and can be used directly, and so is the CBOR writer,
`vcodec::cbor::writer<Opts>` in `<vcodec/cbor/write.hpp>`, over a `std::vector<std::byte>&`:

```cpp
std::string out;
vcodec::json::writer<> w(out);
vcodec::json::encode_into(cfg, w);

std::vector<std::byte> bytes;
vcodec::cbor::writer<> cw(bytes);
vcodec::cbor::encode_into(cfg, cw);
```

Beyond the sink concept, the CBOR writer offers three things a custom codec may need and
the traversal never calls: `negative(n)` writes the integer −1−n (reaching below
`INT64_MIN`, the encode side of `expect_nint`), `simple(v)` writes an unassigned simple
value, and `begin_key()` / `end_key()` bracket exactly one value written as a map key of
any type — deterministic mode sorts it bytewise with the rest. The test DOM in
`test/support/dom.hpp` uses all three.

`test/cross/consistency.cpp` drives the recording sink from `test/support/recording_sink.hpp`
with `using format = vcodec::cbor::format;` added, which makes core key and order the members
as it does for the real CBOR writer while the sink itself only records tokens: a cheap way
to see what a format's traits change without writing a byte.

## Writing a reader

Decoding is type-directed: the expected type drives the reader. The concept requires the
scalar `expect_*` functions, two cursor types, rewinding, an error factory and two policy
constants:

```cpp
template<class C>
concept seq_cursor = requires(C& c) {
    { c.next()      } -> std::same_as<result<bool>>;            // true: positioned at an element
    { c.remaining() } -> std::same_as<std::optional<std::size_t>>;
};

template<class C>
concept map_cursor = requires(C& c) {
    { c.next()      } -> std::same_as<result<bool>>;            // true: positioned at a key
    { c.key_kind()  } -> std::same_as<kind>;                    // text, uint or sint
    { c.key_text()  } -> std::same_as<result<text_ref>>;        // consumes the key, positions at the value
    { c.key_uint()  } -> std::same_as<result<std::uint64_t>>;
    { c.key_sint()  } -> std::same_as<result<std::int64_t>>;
    { c.remaining() } -> std::same_as<std::optional<std::size_t>>;
};

template<class R>
concept reader = requires(R& r, std::size_t pos, errc code) {
    typename R::seq_cursor;   requires seq_cursor<typename R::seq_cursor>;
    typename R::map_cursor;   requires map_cursor<typename R::map_cursor>;
    { r.expect_null()     } -> std::same_as<status>;
    { r.expect_boolean()  } -> std::same_as<result<bool>>;
    { r.expect_uint()     } -> std::same_as<result<std::uint64_t>>;
    { r.expect_sint()     } -> std::same_as<result<std::int64_t>>;
    { r.expect_real()     } -> std::same_as<result<double>>;
    { r.expect_text()     } -> std::same_as<result<text_ref>>;
    { r.expect_bytes()    } -> std::same_as<result<bytes_ref>>;
    { r.expect_array()    } -> std::same_as<result<typename R::seq_cursor>>;
    { r.expect_map()      } -> std::same_as<result<typename R::map_cursor>>;
    { r.peek()            } -> std::same_as<kind>;
    { r.skip_value()      } -> std::same_as<status>;
    { r.offset()          } -> std::same_as<std::size_t>;      // of the next unconsumed item
    { r.save()            } -> std::same_as<std::size_t>;
    { r.restore(pos)      } -> std::same_as<void>;
    { r.error(code, pos)  } -> std::same_as<error>;
    { R::duplicates       } -> std::convertible_to<duplicate_key>;
    { R::errors           } -> std::convertible_to<error_mode>;
};
```

The contract behind the signatures:

- **`expect_*` consumes the item or fails without consuming**, reporting what it found. A
  `type_mismatch` from `expect_text` should carry `with_expected("string")` and
  `with_found(...)` so the headline reads `expected string, found number`. `expect_sint` on a
  non-negative item that fits is fine; `expect_uint` on a negative item is `out_of_range`.
  `expect_real` accepts any number.
- **`text_ref`** is `{ std::string_view text; bool borrowed; std::size_t raw_length; }`.
  `borrowed` means the view aliases the input and lives as long as it does; otherwise it
  aliases reader scratch valid until the next reader call, and the struct decoder refuses to
  bind a `std::string_view` member to it (`escape_in_borrowed_string`). `raw_length` is the
  token's extent in the input, for the caret, or 0.
- **`bytes_ref`** is `{ std::span<const std::byte> bytes; bool borrowed; }`.
- **`peek()`** classifies the next item without consuming it, returning `kind::undefined`
  when there is nothing to classify. Optionals use it to detect `null`.
- **`skip_value()`** consumes one complete value of any shape, validating it. It is what
  unknown fields, `skip_deserializing` members and collect-mode recovery go through.
- **`save()` / `restore()`** rewind to a saved position. Tagged variants use them to find the
  discriminator and then decode from the start of the object; collect mode uses them to skip
  a member whose decode failed part-way. Whole-buffer formats can do this trivially; a
  streaming reader cannot satisfy this concept, which is deliberate for v0.1.
- **`error(code, pos)`** is the error factory. Set the offset and, for text formats,
  `with_position(line_col{...})`.
- **`duplicates`** and **`errors`** are the policies from the options. A reader whose
  `errors` is `error_mode::collect` must also provide `std::vector<error>& collected()`
  (concept `collecting_reader`); the struct decoder appends recovered errors there.

- **`peek()` may return `kind::tag` and `kind::undefined` for real.** The CBOR reader
  returns `kind::tag` for an unexpected tag only when `ignore_unknown_tags` is off (otherwise
  it looks through tags), and `kind::undefined` for `0xf7` only when `undefined_as_null` is
  off (otherwise it reports `null`). Optionals decode `null` from `peek()`, so those two
  options are implemented entirely inside the reader.

### Optional reader hooks

| Hook | Effect |
|---|---|
| `template<field_meta F> result<std::uint64_t> expect_uint();`<br>`… expect_sint();`<br>`… expect_bytes();` | field-aware mirrors of the sink's, consulted for member values so the reader can honour per-member annotations (the JSON reader accepts quoted integers for `json::as_string` members and picks the `json::bytes` alphabet this way). |
| `status expect_tags(std::span<const std::uint64_t> expected);` | called before a member's value when `format_traits::expected_tags<F, T>()` is non-empty; consumes exactly those tags in order or fails with `tag_mismatch` without consuming. Core adds the member name as `context()`. |
| `template<field_meta F, class R> result<bool> read_range(R& out);` | bulk-range hook, tried before element-wise decoding when `format_traits::bulk_range<F, R>()` or `accepts_bulk_range<R>()` is true. Return `true` if the whole range was consumed, `false` (without consuming) to fall back to the element path. The CBOR reader decodes RFC 8746 typed arrays here. |
| `result<bignum_ref> expect_bignum();` (CBOR only) | not consulted by core; a format-specific extension for codecs, see [Format-specific vs generic](#format-specific-vs-generic). |
| `void start();` `status finish();` (CBOR) | not part of the concept either: the CBOR entry points call `start()` to skip a leading self-describe tag and `finish()` to reject trailing bytes. A new format's entry points do the same kind of bracketing around `core::decode`. |

Two implementations to read: `vcodec::json::reader<Opts>` in `<vcodec/json/read.hpp>` (the
real one, whole-buffer, with borrowing and scratch), `vcodec::cbor::reader<Opts>` in
`<vcodec/cbor/read.hpp>` (heads, cursors, a well-formedness validator in `skip_value`, and
every optional hook above), and `test/support/token_reader.hpp`, a reader over a pre-built
token vector that is about a hundred lines and exercises every part of the contract. Once a
reader satisfies the concept, `vcodec::core::decode(r, out)` from `<vcodec/core/build.hpp>`
drives it for any type.

## Adding a format

A format is a tag type plus three things that hang off it: a `format_traits`
specialisation telling core how the format classifies, keys, orders and tags members; a sink
and/or reader that names the format; and codecs that only make sense there. Every hook has
a default in `core::format_traits_defaults` that reproduces JSON's v0.1 behaviour, so a
format specialises only what it cares about.

### The `format_traits` hooks

```cpp
namespace vcodec::core {
struct format_traits_defaults {
    static constexpr std::string_view name = "this format";                        // used in audit messages

    // v0.1: bytes and keys
    template<field_meta F, class T> static consteval bool is_bytes();               // default: std::byte ranges only
    template<field_meta F, class T> static consteval bool can_lower_bytes();        // default: true
    template<field_meta F, class K> static consteval bool can_lower_key();          // default: true

    // v0.2: keys, order, tags, enums, bulk ranges, borrowing, format-specific audits
    template<field_meta F>          static consteval std::optional<std::int64_t> integer_key();   // nullopt: text name
    template<field_meta F>          static consteval bool accepts_text_key();                    // default: true
    template<class T>               static consteval std::vector<std::size_t> member_order(std::size_t n);  // identity
    template<field_meta F, class T> static consteval std::vector<std::uint64_t> expected_tags();  // {}
    static constexpr bool enum_default_integer = false;
    static constexpr bool bytes_borrowable = false;
    template<field_meta F, class R> static consteval bool bulk_range();             // false
    template<class R>               static consteval bool accepts_bulk_range();     // false
    template<field_meta F, class T> static consteval std::string check_field();     // "" = fine
    template<class T>               static consteval std::string check_type();      // "" = fine

    static constexpr std::string_view bytes_hint = "";                                // appended to audit messages
    static constexpr std::string_view key_hint = "";
    static constexpr std::string_view integer_key_spelling = "integer key";           // "via cbor::key(2)" in messages
};
template<class Format> struct format_traits : format_traits_defaults {};
}
```

What each answers, and where core consults it:

| Hook | Question | Consulted by |
|---|---|---|
| `is_bytes<F, T>` | Is this member a byte string? | classification (encode, decode, audit) |
| `can_lower_bytes<F, T>`, `can_lower_key<F, K>` | Can the format represent a byte string / a non-text key here? | the audit; a `false` is the "has no JSON lowering" message with `bytes_hint` / `key_hint` |
| `integer_key<F>` | Which integer key does this struct member use, if any? | `encode_members` (emits `prepared_key<Key>` / `key(int64)`), `decode_struct` (builds a per-format `key_table_of<T, Format>`, dense for small key ranges, sorted otherwise), duplicate detection (the "both map to the … key" message uses `integer_key_spelling`) |
| `accepts_text_key<F>` | May an integer-keyed member also be found by its text name on decode? | `decode_struct` |
| `member_order<T>(n)` | In which order are a struct's `n` members emitted? A permutation of `[0, n)`. | `encode_members`; the sink is told through `begin_map_ordered` |
| `expected_tags<F, T>` | Which tags precede this member's value (outermost first)? `T` is the value type with optionals unwrapped. | `encode_field_value` → `s.tag(t)`; `decode_field_value` → `r.expect_tags(list)` |
| `enum_default_integer` | Do enums encode as integers when neither `as_integer` nor `as_text` applies? | enum encode/decode |
| `bytes_borrowable` | May a `std::span<const std::byte>` be decoded by borrowing? | the decode audit (`span` is encode-only otherwise) and `borrows<T>` |
| `bulk_range<F, R>` | Does this member want `write_range` / `read_range` instead of element-wise traversal? | `encode_field_value`, `decode_field_value` |
| `accepts_bulk_range<R>` | Should `read_range` also be *offered* an unannotated range (CBOR: any numeric range may arrive as a typed array)? | `decode_field_value` |
| `check_field<F, T>`, `check_type<T>` | Format-specific audit failures. Return the message body (the audit prepends the member path and type), or empty. | the audit, on every member and type |

The CBOR specialisation in `<vcodec/cbor/traits.hpp>` sets all of them and is the reference
implementation: `is_bytes` adds `cbor::byte_string`; `integer_key` reads `cbor::key` or
allocates from `cbor::integer_keys`; `member_order` sorts by the encoded key bytes;
`expected_tags` reads `cbor::tag` and defaults a time point to `{1}`; `bulk_range` reads
`cbor::typed_array`; `check_field` / `check_type` produce the CBOR compile-time messages
(`typed_array` on a non-contiguous range, `float_width` on an integer, `indefinite` inside
`deterministic`, `key` on a `transparent` member, nested `self_describe`, `integer_keys`
across `flatten`). JSON's, in `<vcodec/json/traits.hpp>`, sets only the three v0.1 hooks and
the hints.

### Walkthrough: a third format

Suppose MessagePack. The steps, in the order the code needs them:

```cpp
// 1. The tag type and the traits. MessagePack has native bytes and integer keys and no tags,
//    so the defaults are almost right; only the byte-string promotion and borrowing differ.
namespace mp { struct format {}; struct byte_string_t {}; inline constexpr byte_string_t byte_string{}; }

template<>
struct vcodec::core::format_traits<mp::format> : vcodec::core::format_traits_defaults {
    static constexpr std::string_view name = "MessagePack";
    static constexpr bool bytes_borrowable = true;               // bin 8/16/32 are contiguous
    static constexpr bool enum_default_integer = true;
    template<vcodec::core::field_meta F, class T>
    static consteval bool is_bytes() {
        return vcodec::core::byte_like<T>
            || (vcodec::core::contiguous_of_uint8<T> && vcodec::core::has_annotation<mp::byte_string_t>(F.anns()));
    }
};

// 2. A sink. `using format` is what routes codec_for<T, mp::format> and format_traits here.
struct mp_writer {
    using format = mp::format;
    std::vector<std::byte>& out;
    void null(); void boolean(bool); void uint(std::uint64_t); void sint(std::int64_t); void real(double);
    void text(std::string_view); void bytes(std::span<const std::byte>);
    void begin_array(std::optional<std::size_t>); void end_array();
    void begin_map(std::optional<std::size_t>);   void end_map();     // MessagePack has no indefinite lengths:
    void key(std::string_view); void key(std::uint64_t); void key(std::int64_t);   // buffer when n is nullopt, as CBOR does
    void tag(std::uint64_t) {}                                        // no tags: drop, as JSON does
};
static_assert(vcodec::core::sink<mp_writer>);

// 3. A reader satisfying core::reader, returning position() == nullopt from error().
// 4. Gated entry points, one static_assert instead of an instantiation wall.
namespace mp {
template<class T> concept encodable = vcodec::core::audit_of<T, format, vcodec::core::direction::encode>.ok;
template<class T> std::vector<std::byte> encode(T const& v) {
    static_assert(encodable<T>, (vcodec::core::explain<T, format, vcodec::core::direction::encode>()));
    std::vector<std::byte> out; mp_writer w{out};
    if constexpr (encodable<T>) vcodec::core::encode(w, v);
    return out;
}
}
```

Then, in whatever order the format needs: per-format codecs
(`codec_for<std::chrono::sys_time<D>, mp::format>` as an extension type, say); a renderer
if `render_framed` and `render_hex` do not fit; and, if the format has per-member
representation choices, its own annotation namespace read through `F.anns()` in the sink's
field-aware hooks and in `check_field`. `core/` never includes a format header and the two
shipped formats never include each other; `cmake/LayeringCheck.cmake` enforces both at
configure time and as a test, and a third format should add itself to the rule.

`test/cross/consistency.cpp` shows what the library asserts about two formats fed the same
type: the same keys (modulo the format's key form), the same order (modulo `member_order`),
the same skips, and the same error paths.

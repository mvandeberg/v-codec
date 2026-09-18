# Customization

Four extension points, from the smallest to the largest:

| You want to | Use |
|---|---|
| change how one member is encoded | [`with<Codec>`](#withcodec) on the member |
| give a type you do not own its own representation | [`codec_for<T, Format>`](#codec_for) |
| annotate a struct you do not own, but still traverse it by reflection | [`describe<T>`](#describet) |
| drive the traversal into something other than a `std::string` of JSON | a [custom sink](#writing-a-sink) or [reader](#writing-a-reader), possibly a [new format](#adding-a-format) |

The first three are declared in `<vcodec/json.hpp>`. Sinks, readers and formats need the
`core/` headers, all of which `<vcodec/vcodec.hpp>` includes.

## Lookup order

For every value the traversal meets, the first of these that applies wins:

1. `[[=vcodec::with<Codec>]]` on the member being encoded or decoded
2. `vcodec::codec_for<T, Format>` where `Format` is the sink's or reader's format
   (`vcodec::json::format` for JSON)
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
`using format = vcodec::json::format;`. `codec_for<T, void>` applies under every format that
has no more specific codec. The distinction exists because a `std::chrono::time_point` wants
ISO 8601 text in JSON and a tagged epoch number in CBOR; write the generic one when the model
tokens you emit are right everywhere (a number, a string, an array of numbers).

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
| `template<vcodec::core::field_meta F> void uint(std::uint64_t);`<br>`… void sint(std::int64_t);`<br>`… void bytes(std::span<const std::byte>);` | field-aware overloads, called instead of the plain ones when the value belongs to a member; `F.anns()` gives the member's annotations. This is how the JSON writer implements `json::as_string` and `json::bytes`. |
| `template<class T> void write_prepared(T const& v);` | whole-value fast path: when it is viable for `T`, the traversal calls it instead of visiting `T`'s members. Constrain it (`requires ...`) to the types you handle. |

The field-aware overloads shadow the plain ones inside a class; bring the base versions back
with `using base::uint;` if you derive, as `test/support/recording_sink.hpp` does.

The JSON writer itself, `vcodec::json::writer<Opts>` in `<vcodec/json/write.hpp>`, is a
sink over a `std::string&` and can be used directly:

```cpp
std::string out;
vcodec::json::writer<> w(out);
vcodec::json::encode_into(cfg, w);
```

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

Optional hooks, mirrors of the sink's: `template<vcodec::core::field_meta F> result<std::uint64_t> expect_uint()`,
`expect_sint<F>()` and `expect_bytes<F>()`, consulted for member values so the reader can
honour per-member annotations (the JSON reader accepts quoted integers for `json::as_string`
members and picks the `json::bytes` alphabet this way).

Two implementations to read: `vcodec::json::reader<Opts>` in `<vcodec/json/read.hpp>` (the
real one, whole-buffer, with borrowing and scratch), and `test/support/token_reader.hpp`, a
reader over a pre-built token vector that is about a hundred lines and exercises every part
of the contract. Once a reader satisfies the concept, `vcodec::core::decode(r, out)` from
`<vcodec/core/build.hpp>` drives it for any type.

## Adding a format

A format is a tag type plus three things that hang off it:

```cpp
struct my_format {};

// 1. How core classifies and lowers model elements this format cannot represent natively.
template<>
struct vcodec::core::format_traits<my_format> {
    static constexpr std::string_view name = "MyFormat";        // used in §10 messages

    // Which members are byte strings? The default is std::byte ranges only.
    template<vcodec::core::field_meta F, class T>
    static consteval bool is_bytes() { return vcodec::core::byte_like<T>; }

    // Can this member's bytes be represented? (JSON: only with json::bytes.)
    template<vcodec::core::field_meta F, class T>
    static consteval bool can_lower_bytes() { return true; }

    // Can this member's map keys of type K be represented? (JSON: only text, or stringify_keys.)
    template<vcodec::core::field_meta F, class K>
    static consteval bool can_lower_key() { return true; }

    static constexpr std::string_view bytes_hint = "";           // appended to the audit message
    static constexpr std::string_view key_hint   = "";
};

// 2. A sink and/or reader that names the format.
struct my_sink {
    using format = my_format;
    std::string out;
    // ... the sink concept's member functions, appending to `out`
};

// 3. Codecs that only make sense here.
template<> struct vcodec::codec_for<std::chrono::system_clock::time_point, my_format> { /* ... */ };
```

The primary `format_traits` (everything representable natively, as in CBOR) is what a format
gets by not specialising. JSON's specialisation is in `<vcodec/json/traits.hpp>` and is a
useful template: it promotes `uint8_t` ranges to bytes only under `json::bytes`, refuses
bytes without it, and refuses non-text keys without `json::stringify_keys`, with hints that
name the annotation to add.

`core/` never includes a format header; the layering is checked by CMake. A new format's
entry points should gate on the audit the same way JSON's do, so users get one
`static_assert` instead of an instantiation wall:

```cpp
template<class T>
concept my_encodable = vcodec::core::audit_of<T, my_format, vcodec::core::direction::encode>.ok;

template<class T>
std::string my_encode(T const& v) {
    static_assert(my_encodable<T>, (vcodec::core::explain<T, my_format, vcodec::core::direction::encode>()));
    my_sink s;
    if constexpr (my_encodable<T>) vcodec::core::encode(s, v);
    return std::move(s.out);
}
```

The throwaway CBOR sink in `spike/cbor_write.hpp` is a ninety-line worked example of a
second format driven by the same traversal, and `test/cross/consistency.cpp` shows what the
library asserts about two formats fed the same type: same keys, same order, same skips, same
error paths.

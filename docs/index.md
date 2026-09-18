# vcodec — quick start

`vcodec` is a header-only C++26 library that maps C++ structs to and from JSON, driven by
compile-time reflection (P2996) with per-member behaviour declared as annotations (P3394)
on the struct itself. No macros, no registration, no code generation.

```cpp
#include <vcodec/json.hpp>

struct [[=vcodec::rename_all(vcodec::case_::kebab)]] Server {
    std::string                host;
    std::uint16_t              port = 8080;
    [[=vcodec::name("tls")]]   bool secure = false;
    [[=vcodec::skip_if_null]]  std::optional<std::string> cert_path;
    std::vector<std::string>   allowed_origins;
};

std::string text = vcodec::json::encode(cfg);                 // std::string
auto        back = vcodec::json::decode<Server>(text);        // vcodec::result<Server>
```

This page gets you from an empty CMake project to encoding, decoding and rendering an
error. The other documents are reference material:

| Document | Contents |
|---|---|
| [annotations.md](annotations.md) | Every annotation: placement, effect on encode and decode, defaults, interactions |
| [types.md](types.md) | The supported type set, category by category, with the concept that admits each |
| [errors.md](errors.md) | The `error` type, both renderers, one example per error code, collect-all |
| [customization.md](customization.md) | `codec_for`, `describe`, `with<Codec>`, custom sinks and readers, new formats |
| [compatibility.md](compatibility.md) | Toolchain requirements and what v0.1 does and does not promise |
| [design/performance.md](design/performance.md) — what is optimised and the recorded baseline
- [design/data-model.md](design/data-model.md) | The format-neutral data model the traversal speaks |
| [design/m0-spellings.md](design/m0-spellings.md) | Verified reflection spellings and where the implementation departs from the spec |

## Requirements

- GCC 16 with `-std=c++26 -freflection`. v0.1 is exercised only on GCC 16.2.1; see
  [compatibility.md](compatibility.md).
- CMake 3.28 or later.
- No runtime dependencies. Test dependencies (Catch2, the JSONTestSuite corpus) are fetched
  with `FetchContent` and built only when `VCODEC_BUILD_TESTS` is on, which it is by default
  only when vcodec is the top-level project.

## Getting the library

### FetchContent

```cmake
cmake_minimum_required(VERSION 3.28)
project(demo LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(vcodec
    GIT_REPOSITORY https://github.com/mvandeberg/v-codec.git
    GIT_TAG        v0.1.0)
FetchContent_MakeAvailable(vcodec)

add_executable(demo main.cpp)
target_link_libraries(demo PRIVATE vcodec::vcodec)
```

### find_package

Install once (`cmake --install build`), then:

```cmake
find_package(vcodec 0.1 REQUIRED)
target_link_libraries(demo PRIVATE vcodec::vcodec)
```

The `vcodec::vcodec` interface target carries the include directory, `cxx_std_26` and the
`-freflection` flag (`-freflection -fexpansion-statements` when the compiler identifies as
Clang). Nothing is compiled; the target only adds flags to yours.

### Headers

| Header | Provides |
|---|---|
| `<vcodec/json.hpp>` | JSON encode/decode, the annotation vocabulary, `codec_for`, `describe`, the error type and both renderers. Almost every program needs only this. |
| `<vcodec/vcodec.hpp>` | Everything, including the `core/` traversal headers, for writing sinks, readers and formats. |
| `<vcodec/core/annotations.hpp>` | Only the annotation vocabulary, for headers that declare types but never encode them. |

## A worked example

### The type

```cpp
#include <vcodec/json.hpp>

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace js = vcodec::json;

struct [[=vcodec::rename_all(vcodec::case_::kebab)]] Server {
    std::string                host;
    std::uint16_t              port = 8080;
    [[=vcodec::name("tls")]]   bool secure = false;
    [[=vcodec::skip_if_null]]  std::optional<std::string> cert_path;
    std::vector<std::string>   allowed_origins;
};
```

What the annotations do here:

- `rename_all(case_::kebab)` on the type turns `allowed_origins` into the wire name
  `allowed-origins` and `cert_path` into `cert-path`. Single-word identifiers are unchanged.
- `name("tls")` overrides the wire name of `secure`; an explicit `name` always beats
  `rename_all`.
- `skip_if_null` omits `cert_path` from the output when the optional is empty.

Which members are required on decode follows from the type, not from an annotation: a member
is optional if it is optional-like (`std::optional`, `std::unique_ptr`, `std::shared_ptr`),
or has a default member initialiser, or carries `default_value(...)`. Everything else is
required. So `host` and `allowed_origins` are required; `port`, `secure` and `cert_path` are
not. `[[=vcodec::required]]` and `[[=vcodec::optional]]` override the rule
([annotations.md](annotations.md#required-and-optional)).

### Encode

```cpp
Server cfg;
cfg.host = "example.org";
cfg.allowed_origins = { "https://app.example.org" };

std::cout << js::encode(cfg) << '\n';
```

```json
{"host":"example.org","port":8080,"tls":false,"allowed-origins":["https://app.example.org"]}
```

Members appear in declaration order. `cert-path` is absent because it is empty and marked
`skip_if_null`; set it and it appears:

```cpp
cfg.cert_path = "/etc/tls/cert.pem";
std::cout << js::encode(cfg) << '\n';
```

```json
{"host":"example.org","port":8080,"tls":false,"cert-path":"/etc/tls/cert.pem","allowed-origins":["https://app.example.org"]}
```

Formatting is an option of the call, not of the type. Options are a structural non-type
template parameter, so every option resolves at compile time:

```cpp
constexpr js::options pretty{ .pretty = true };       // indent defaults to 2
std::cout << js::encode<pretty>(cfg) << '\n';
```

```json
{
  "host": "example.org",
  "port": 8080,
  "tls": false,
  "cert-path": "/etc/tls/cert.pem",
  "allowed-origins": [
    "https://app.example.org"
  ]
}
```

Encoding cannot fail for a type that compiles: the only runtime failures are facts the type
system cannot see (a `[[=skip]]`ped enumerator value, a cycle through smart pointers that
exhausts `max_depth`, invalid UTF-8 in a string) and those throw `vcodec::encode_error`
([errors.md](errors.md#encode-side-failures)).

### Decode

`decode<T>` returns `vcodec::result<T>`, which is `std::expected<T, vcodec::error>`:

```cpp
std::string_view input = R"({"host":"example.org","allowed-origins":[]})";

auto r = js::decode<Server>(input);
if (r) {
    std::cout << r->host << ':' << r->port << '\n';      // example.org:8080
}
```

Absent optional members keep their default-constructed value (`port` is `8080` from its
initialiser, `secure` is `false`, `cert_path` is empty). Unknown keys are skipped unless the
type is declared `[[=vcodec::deny_unknown_fields]]`. Key order does not matter.

`decode` takes a `std::string_view`. Types containing `std::string_view` members borrow from
that buffer, and passing a temporary `std::string` for such a type is a compile error
([types.md](types.md#borrowing)).

### Rendering an error

An `error` carries a code, a root-first path, a byte offset, a line/column position and
enough detail for two renderers: `render_terse` (one line) and `render_framed` (a source
excerpt with a caret). Both are free functions in namespace `vcodec`.

```cpp
std::string_view bad = R"({"host":"example.org","port":"8080","allowed-origins":[]})";

auto r = js::decode<Server>(bad);
if (!r) {
    std::cerr << vcodec::render_framed(r.error(), bad);
    std::cerr << vcodec::render_terse(r.error()) << '\n';
}
```

```
error: expected integer, found string
  --> $.port
   |
   |  …"host":"example.org","port":"8080","allowed-origins":[]}
   |                               ^ at byte offset 29
error: expected integer, found string at $.port (byte offset 29)
```

A missing required member is reported against the enclosing object:

```cpp
std::string_view missing = R"({"host":"example.org","port":8080})";
auto r = js::decode<Server>(missing);
std::cerr << vcodec::render_framed(r.error(), missing);
```

```
error: missing required field 'allowed-origins'
  --> $
   |  object begins at byte offset 0
```

Range errors name the C++ type the value did not fit:

```
error: value 80800 out of range for std::uint16_t
  --> $.port
```

The path uses the C++ member identifier (`$.port`), not the wire name, and the detail uses
the wire name (`'allowed-origins'`), because the path identifies a place in your type and the
detail identifies a key in the document. [errors.md](errors.md) has one example per code.

### Collecting every error

`decode_all<T>` keeps going after a failed member, records each error, and returns
`vcodec::collected<T>`: a possibly incomplete value plus a `std::vector<error>`.

```cpp
auto all = js::decode_all<Server>(R"({"host":1,"port":"x","allowed-origins":[]})");
if (!all.complete()) {
    for (auto const& e : all.errors) std::cerr << vcodec::render_terse(e) << '\n';
}
```

```
error: expected string, found number at $.host (byte offset 8)
error: expected integer, found string at $.port (byte offset 17)
```

Recovery is at object-member granularity: syntax errors end the run
([errors.md](errors.md#collect-all-decode_all)).

## The API at a glance

All entry points live in `vcodec::json`. `Opts` is a `vcodec::json::options` value; when
omitted it is the default-constructed one.

```cpp
template<options Opts = {}, class T> std::string encode(T const& v);
template<options Opts = {}, class T> void        encode_append(T const& v, std::string& out);
template<options Opts = {}, class T, std::output_iterator<char> It> It encode_to(T const& v, It it);
template<options Opts = {}, class T, core::sink S> void encode_into(T const& v, S& s);

template<class T, options Opts = {}> result<T>    decode(std::string_view input);
template<options Opts = {}, class T> status       decode_into(T& out, std::string_view input);
template<class T, options Opts = {}> collected<T> decode_all(std::string_view input);

template<class T> concept encodable;                  // passes the compile-time audit for encode
template<class T> concept decodable;                  // ... for decode
template<class T> inline constexpr bool borrows;      // T holds std::string_view members
```

Note the parameter order: `Opts` comes first for the encode functions and `decode_into`
(so `T` is deduced), and second for `decode<T>` and `decode_all<T>` (so `T` is written
first). `encode_to` encodes into a temporary `std::string` and copies it through the
iterator. `decode_into` reuses an existing object: containers it decodes into are cleared
first, but members absent from the input keep their current value.

`vcodec::json::options`:

```cpp
struct options {
    bool          pretty        = false;
    unsigned      indent        = 2;
    std::size_t   max_depth     = 256;                        // nesting limit, encode and decode
    bool          validate_utf8 = true;                       // encode as well as decode
    duplicate_key duplicates    = duplicate_key::last_wins;   // or first_wins, error
    error_mode    errors        = error_mode::fail_fast;      // decode_all forces collect
};
```

`errors` is set by `decode_all`. If you set `error_mode::collect` on `decode` or
`decode_into` yourself, the reader still recovers at member granularity but those entry
points can return only one error, so they return the *first* one collected. Use
`decode_all` to see them all.

## Compile-time diagnostics

Every entry point checks the type first. A type that cannot be encoded or decoded produces one
`static_assert` naming the member path and the reason, not a wall of template instantiation
errors:

```cpp
struct Pool     { int size = 0; void* handle; };
struct Database { std::optional<std::vector<Pool>> pool; };
struct Config   { Database database; };

vcodec::json::encode(Config{});
```

```
static assertion failed: Config::database::pool::handle (type void*)
  has no codec. Provide vcodec::codec_for<void*>, mark the member
  [[=vcodec::skip]], or give it [[=vcodec::with<YourCodec>]].
```

The audit is also available as concepts (`vcodec::json::encodable<T>`,
`vcodec::json::decodable<T>`) and as a consteval message
(`vcodec::json::explain_encode<T>()`, `explain_decode<T>()`, each returning an object with
`.view()`), so a library can gate its own entry points the same way.

## Where to go next

- Annotating a type: [annotations.md](annotations.md).
- A member type is rejected, or you need a type you do not own to work:
  [types.md](types.md) then [customization.md](customization.md).
- Reporting errors to users: [errors.md](errors.md).
- What can change between v0.1 and v0.2: [compatibility.md](compatibility.md).

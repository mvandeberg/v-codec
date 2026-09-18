# vcodec

A header-only C++26 library that maps between C++ structs and JSON, driven by compile-time
reflection (P2996) with per-member behaviour declared as annotations (P3394) on the struct.
No registration macros, no code generation, no runtime type information.

```cpp
#include <vcodec/json.hpp>

struct [[=vcodec::rename_all(vcodec::case_::kebab)]] Server {
    std::string                host;
    std::uint16_t              port = 8080;
    [[=vcodec::name("tls")]]   bool secure = false;
    [[=vcodec::skip_if_null]]  std::optional<std::string> cert_path;
    std::vector<std::string>   allowed_origins;
};

Server cfg{ .host = "example.org", .allowed_origins = {"https://a.example"} };

std::string text = vcodec::json::encode(cfg);
// {"host":"example.org","port":8080,"tls":false,"allowed-origins":["https://a.example"]}

auto back = vcodec::json::decode<Server>(text);   // vcodec::result<Server> = std::expected<Server, vcodec::error>
if (!back) std::cerr << vcodec::render_framed(back.error(), text);
```

A wrong type in the input renders like this, with a path in C++ member terms, a source
excerpt and a caret:

```
error: expected string, found number
  --> $.users[3].name
   |
   |  …"id": 7, "name": 42, "email": "a@b.c"…
   |                    ^^ at byte offset 1284
```

A type the library cannot handle fails at compile time with one message, not a template wall:

```
static assertion failed: Config::database::pool::handle (type void*)
  has no codec. Provide vcodec::codec_for<void*>, mark the member
  [[=vcodec::skip]], or give it [[=vcodec::with<YourCodec>]].
```

## Status

**v0.1.** The version number admits that the annotation vocabulary and the format seam are
hypotheses that first contact with real types will revise. Everything else is held to a 1.0
bar: JSONTestSuite conformance on both parsing paths, three fuzz harnesses under ASan+UBSan,
property-based round-trip tests, exact-text tests for every diagnostic, a compile-fail suite
for the compile-time messages, and a recorded benchmark and compile-time baseline. See
[docs/compatibility.md](docs/compatibility.md) for what v0.1 promises and does not.

## Requirements

| | |
|---|---|
| Compiler | GCC 16.1+ with `-std=c++26 -freflection` (developed and tested on 16.2.1) |
| Build | CMake 3.28+ |
| Dependencies | None at runtime. Catch2 v3, google/benchmark, JSONTestSuite and the comparators are test-only, fetched via `FetchContent`, all opt-in |

Bloomberg clang-p2996 is supported best-effort and is not currently exercised; MSVC is not
supported because no reflection implementation exists to support.

## Using it

```cmake
include(FetchContent)
FetchContent_Declare(vcodec GIT_REPOSITORY https://github.com/mvandeberg/v-codec.git GIT_TAG v0.1.0)
FetchContent_MakeAvailable(vcodec)
target_link_libraries(my_app PRIVATE vcodec::vcodec)
```

or install and `find_package(vcodec CONFIG REQUIRED)`. The `vcodec::vcodec` target adds
`-freflection` and `cxx_std_26` for you.

## What is in the box

- Encode and decode for structs (including private and inherited members), enums, and the
  standard-library type set in [docs/types.md](docs/types.md): scalars, strings, optionals,
  smart pointers, tagged variants, tuples, arrays, any range, maps, and byte strings.
- The annotation vocabulary in [docs/annotations.md](docs/annotations.md): `name`, `alias`,
  `skip`, `skip_serializing`, `skip_deserializing`, `skip_if_null`, `skip_if_default`,
  `required`, `optional`, `default_value`, `flatten`, `with<Codec>`, `rename_all`,
  `deny_unknown_fields`, `transparent`, `tag`, `as_integer`, and the JSON lowerings
  `json::bytes`, `json::as_string`, `json::stringify_keys`.
- Structured errors with a member path built only when something fails, two renderers, and
  a collect-all mode that reports every mistake in a config file at once
  ([docs/errors.md](docs/errors.md)).
- Compile-time diagnostics that name the offending member and the fix
  ([docs/design/m0-spellings.md](docs/design/m0-spellings.md) records how they are produced).
- Extension points for types you do not own: `codec_for<T, Format>` and `describe<T>`
  ([docs/customization.md](docs/customization.md)).
- A format-free core: nothing under `include/vcodec/core/` knows JSON exists, and a CBOR
  write spike in `spike/` proves it ([docs/design/seam.md](docs/design/seam.md)).

## Building the tests

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVCODEC_BUILD_SPIKE=ON
ninja -C build
ctest --test-dir build
```

| Option | Default | What it adds |
|---|---|---|
| `VCODEC_BUILD_TESTS` | on when top-level | unit, round-trip, conformance and compile-fail tests |
| `VCODEC_BUILD_SPIKE` | off | the CBOR spike and the cross-format consistency tests |
| `VCODEC_SANITIZE` | off | ASan+UBSan on the test suite |
| `VCODEC_BUILD_FUZZERS` | off | the three fuzz harnesses (`VCODEC_FUZZ_SECONDS` per harness) |
| `VCODEC_BUILD_BENCHMARKS` | off | google/benchmark against Glaze, nlohmann and simdjson, and the compile-time budget |

## Layout

```
include/vcodec/        the library (header-only)
  core/                format-free: schema, model, traversal, diagnostics
  json/                the JSON format: writer, reader, options, renderers
test/                  Catch2 suites by layer, plus conformance, round-trip, compile-fail
fuzz/                  libFuzzer-style harnesses with a standalone driver for GCC
bench/                 benchmarks and the compile-time budget
spike/                 the throwaway CBOR sink; never included by the library
docs/                  reference and design documentation
```

## Licence

MIT. Copyright (c) 2026 Michael Vandeberg. See [LICENSE](LICENSE).

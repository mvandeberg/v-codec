# M0 — verified reflection spellings (GCC 16.2.1, `-std=c++26 -freflection`)

This document is the exit deliverable of milestone 0. Every P2996 / P3394 / P3491 facility
the specification depends on was compiled and executed against GCC 16.2.1 before anything
was built on top. Where the spec's assumed spelling or shape differs from what the compiler
accepts, the difference is recorded here together with the design consequence.

Feature macros present in `<meta>`: `__cpp_lib_reflection`, `__cpp_lib_define_static`,
`__cpp_lib_constexpr_exceptions`.

## Confirmed as written in the spec

| Facility | Spelling | Notes |
|---|---|---|
| Member enumeration | `std::meta::nonstatic_data_members_of(^^T, ctx)` | `ctx = std::meta::access_context::unchecked()` sees private members |
| Base enumeration | `std::meta::bases_of(^^T, ctx)` | `obj.[:base:]` splices to the base subobject |
| Annotations | `std::meta::annotations_of(m)` | also `annotations_of_with_type(m, ^^A)` |
| Annotation value | `std::meta::extract<A>(a)` | |
| Identifier | `std::meta::identifier_of(m)`, `has_identifier(m)` | |
| Default member init | `std::meta::has_default_member_initializer(m)` | |
| Access | `std::meta::is_public(m)`, `is_private`, `is_protected` | |
| Enumerators | `std::meta::enumerators_of(^^E)` | value via `extract<E>(e)` |
| Static storage | `std::define_static_string(sv)` → `const char*`; `std::define_static_array(range)` → `std::span<const T>` | element type must be structural |
| Substitution | `std::meta::substitute(^^tmpl, {args})` | used for consteval recursion over member types |
| Type splice | `using U = [:info:]`, `obj.[:member:]` | |
| Expansion statements | `template for (constexpr auto x : constexpr-range)` | works over `define_static_array` spans held in `inline constexpr` variable templates |
| P2741 messages | `static_assert(cond, obj)` with `obj.data()`/`obj.size()` | works; message is printed verbatim |
| Structural NTTP options | `template<options O = {}>` | works with designated initialisers at the call site |
| `std::meta::display_string_of` | | `std::vector<std::uint8_t>` renders as `std::vector<unsigned char>`; aliases render as `std::string_view {aka std::basic_string_view<char>}` |
| `std::meta::is_structural_type` | | |
| `std::meta::offset_of(m).bytes` | | |
| `std::meta::info` as NTTP | `template<field_meta F>` where `field_meta` holds `info` members | works |

## Differences from the spec, and their consequences

1. **`std::define_static_array` requires a structural element type.** `std::string_view`,
   `std::span`, and `std::optional` all have private members and are not structural, so the
   spec's `field_info` (§6.1) cannot be stored as written. Consequence: every string in a
   schema table is a `core::static_string` (pointer to static storage + length), aliases are
   a pointer + count, and the optional integer key is a `bool` + value pair. The public
   accessors return `std::string_view` / `std::span`, so the difference is internal.

2. **A type with a `std::meta::info` member is consteval-only.** Objects of such a type can
   live in static storage and drive `template for`, but cannot be read at runtime. The spec's
   single `schema_of<T>` table therefore splits in two, indexed identically:
   `core::fields_of<T>` (consteval-only; member paths, types, annotation reflections) and
   `core::schema_of<T>` (runtime-usable; wire names, aliases, flags). The lookup table and
   did-you-mean read the second; traversal iterates the first.

3. **`std::meta::type_of(annotation)` is const-qualified.** Comparing it against `^^name_t`
   fails; every comparison goes through `std::meta::remove_cvref` first. Encapsulated in
   `core/annotations.hpp`.

4. **Enumerator annotations follow the identifier.** The grammar is
   `enumerator: identifier attribute-specifier-seq(opt)`, so the spec's
   `enum class Color { [[=name("bright-red")]] red }` is a syntax error. The accepted form is
   `enum class Color { red [[=vcodec::name("bright-red")]], green };`. Member and type
   annotations are placed as the spec shows.

5. **`std::meta::qualified_name_of` does not exist.** Diagnostics build `Outer::member`
   paths by hand from `identifier_of` along the member path, which is what §10 wants anyway.

6. **GCC prints `In instantiation of` for every template-based diagnostic mechanism**,
   including the spec's own `static_assert` inside `encode<Opts, T>`. Four mechanisms were
   compared (static_assert in the entry point, a throwing consteval default template
   argument, a throwing `requires` clause, a checker class as a defaulted parameter); the
   phrase appears in all of them, and the throwing variants additionally lose the message
   behind "no matching function". Consequence: the §13.5 guard is amended from "never emits
   `in instantiation of`" to **"emits exactly one instantiation frame, naming the public
   entry point, followed by exactly one `static assertion failed` line"** — which is the
   property the spec is actually after (no nested-template wall). The compile-fail harness
   enforces this on every case.

7. **`std::vector<std::uint8_t>` is not byte-like without annotation.** §7.2 says so
   explicitly and §10's sample message uses it as the byte-like example; §7.2 wins. The
   "is byte-like but has no JSON lowering" message is produced for `std::vector<std::byte>`,
   `std::array<std::byte, N>` and `std::span<const std::byte>`; a `std::vector<std::uint8_t>`
   without `json::bytes` is an array of numbers.

8. **`default_value(v)` shape.** Because the payload must be structural and string literals
   cannot be template arguments, `default_value` is an overload set: arithmetic/enum/bool
   values are stored directly; `std::string_view` is interned via `define_static_string`;
   anything else hits a `static_assert` naming `with<Codec>` as the alternative (§5.1.1).

9. **clang-p2996 is not available on the development machine** (system clang 22 has no
   `-freflection`). Secondary-compiler status is "not exercised", recorded in
   `docs/compatibility.md`. Consequence for §13.4: GCC has no libFuzzer, so the fuzz
   harnesses expose `LLVMFuzzerTestOneInput` and ship with a standalone driver that replays
   the seed corpus and mutates it under ASan+UBSan; a true libFuzzer run needs clang.

10. **Catch2 v3.8.1 and google/benchmark v1.9.4 compile under `-freflection`** via
    `FetchContent`. Glaze, nlohmann/json and simdjson likewise (for `bench/`).

11. **`std::meta::type_of(member)` does not preserve the declared alias.** A member declared
    `std::uint8_t` reflects as `unsigned char`, and `^^std::uint8_t` is itself ill-formed
    because libstdc++ brings it in with a using-declaration. The §9.4 message
    `value 512 out of range for std::uint8_t` therefore cannot read the alias back.
    Consequence: diagnostics render integer types other than `int` by their fixed-width
    name (`unsigned char` → `std::uint8_t`, `long` → `std::int64_t`), which states the range
    and matches how most code spells them. `int` stays `int`.

## Things that were tried and worked, worth knowing

- Consteval recursion over member types with a shared visited set: obtain the specialised
  function through `std::meta::substitute(^^fn, {type})`, then
  `std::meta::extract<Fn*>(r)(args...)`. Used by flatten resolution and the audit.
- A `template<auto V> struct default_value_t` annotation is found with
  `template_of(remove_cvref(type_of(a))) == ^^default_value_t`.
- Range-for over a consteval result at runtime requires the range to be `constexpr`
  (`define_static_array` result bound to a `constexpr` variable).

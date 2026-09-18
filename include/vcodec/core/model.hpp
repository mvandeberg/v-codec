// The data model (spec §4): the union of what JSON and CBOR can express. Lowering is the
// format's job. Also the sink and reader concepts and the format hooks core consults.
#ifndef VCODEC_CORE_MODEL_HPP
#define VCODEC_CORE_MODEL_HPP

#include <vcodec/core/compiler.hpp>
#include <vcodec/core/concepts.hpp>
#include <vcodec/core/schema.hpp>
#include <vcodec/error.hpp>
#include <vcodec/options.hpp>

#include <concepts>
#include <cstddef>
#include <vector>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace vcodec::core {

enum class kind : std::uint8_t {
    null, boolean, uint, sint, real,
    text, bytes,                  // bytes: no JSON representation, see §4.1
    array, map,
    tag,                          // no JSON representation
    undefined,                    // no JSON representation
};

constexpr std::string_view kind_name(kind k) noexcept {
    switch (k) {
    case kind::null: return "null";        case kind::boolean: return "boolean";
    case kind::uint: return "uint";        case kind::sint: return "sint";
    case kind::real: return "real";        case kind::text: return "text";
    case kind::bytes: return "bytes";      case kind::array: return "array";
    case kind::map: return "map";          case kind::tag: return "tag";
    case kind::undefined: return "undefined";
    }
    return "?";
}

// Text as the reader saw it. `borrowed` means the view aliases the input buffer and lives
// as long as it does; otherwise it aliases reader scratch, valid until the next reader call.
struct text_ref {
    std::string_view text;
    bool             borrowed = false;
    std::size_t      raw_length = 0;   // bytes the token occupied in the input, 0 if unknown
};

struct bytes_ref {
    std::span<const std::byte> bytes;
    bool                       borrowed = false;
};

// ---- sink (§4.2) -------------------------------------------------------------------------

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
    { s.begin_array(n)    } -> std::same_as<void>;
    { s.end_array()       } -> std::same_as<void>;
    { s.begin_map(n)      } -> std::same_as<void>;
    { s.end_map()         } -> std::same_as<void>;
    { s.key(t)            } -> std::same_as<void>;
    { s.key(u)            } -> std::same_as<void>;
    { s.key(i)            } -> std::same_as<void>;
    { s.tag(u)            } -> std::same_as<void>;
};

// ---- reader (§4.3) -----------------------------------------------------------------------

template<class C>
concept seq_cursor = requires(C& c) {
    { c.next()      } -> std::same_as<result<bool>>;              // true: positioned at an element
    { c.remaining() } -> std::same_as<std::optional<std::size_t>>;
};

template<class C>
concept map_cursor = requires(C& c) {
    { c.next()      } -> std::same_as<result<bool>>;              // true: positioned at a key
    { c.key_kind()  } -> std::same_as<kind>;                      // text, uint or sint
    { c.key_text()  } -> std::same_as<result<text_ref>>;          // consumes key, positions at value
    { c.key_uint()  } -> std::same_as<result<std::uint64_t>>;
    { c.key_sint()  } -> std::same_as<result<std::int64_t>>;
    { c.remaining() } -> std::same_as<std::optional<std::size_t>>;
};

template<class R>
concept reader = requires(R& r, std::size_t pos, errc code) {
    typename R::seq_cursor;
    typename R::map_cursor;
    requires seq_cursor<typename R::seq_cursor>;
    requires map_cursor<typename R::map_cursor>;
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
    { r.offset()          } -> std::same_as<std::size_t>;         // offset of the next unconsumed item
    // Whole-buffer formats can rewind. Used for tagged unions whose tag is not first and
    // for member-granularity recovery in collect mode.
    { r.save()            } -> std::same_as<std::size_t>;
    { r.restore(pos)      } -> std::same_as<void>;
    // Error factory: fills in offset and, for text formats, line/column.
    { r.error(code, pos)  } -> std::same_as<error>;
    { R::duplicates       } -> std::convertible_to<duplicate_key>;
    { R::errors           } -> std::convertible_to<error_mode>;
};

// Readers in collect mode expose the error list.
template<class R>
concept collecting_reader = reader<R> && requires(R& r) {
    { r.collected() } -> std::same_as<std::vector<error>&>;
};

// ---- format hooks -----------------------------------------------------------------------
//
// core/ knows no format. Where a format's annotations change how a member is classified,
// keyed, tagged or ordered, core asks format_traits<Format>. Formats specialise it, inheriting
// format_traits_defaults so that a hook they do not care about keeps the v0.1 behaviour.

struct format_traits_defaults {
    static constexpr std::string_view name = "this format";

    // Is this member a byte string? Default: only std::byte ranges.
    template<field_meta F, class T>
    static consteval bool is_bytes() { return byte_like<T>; }

    // Can this format represent a byte string for this member?
    template<field_meta F, class T>
    static consteval bool can_lower_bytes() { return true; }

    // Can this format represent a non-text map key for this member?
    template<field_meta F, class K>
    static consteval bool can_lower_key() { return true; }

    // Integer key for a struct member, or nullopt for its text name (v0.2).
    template<field_meta F>
    static consteval std::optional<std::int64_t> integer_key() { return std::nullopt; }

    // Emission order of a struct's N members: a permutation of [0, N). Identity by default.
    template<class T>
    static consteval std::vector<std::size_t> member_order(std::size_t n) {
        std::vector<std::size_t> v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = i;
        return v;
    }

    // Tags that must precede this member's value, outermost first. None by default.
    template<field_meta F>
    static consteval std::vector<std::uint64_t> expected_tags() { return {}; }

    // Whether enums encode as integers when the member carries neither as_integer nor as_text.
    static constexpr bool enum_default_integer = false;

    // Whether the format wants the bulk-range hooks (write_range / read_range) tried before
    // element-wise traversal for this member.
    template<field_meta F, class R>
    static consteval bool bulk_range() { return false; }

    // Format-specific audit checks: return a §10 message (without the member path, which the
    // audit prepends) or an empty string when the member / type is fine.
    template<field_meta F, class T>
    static consteval std::string check_field() { return {}; }
    template<class T>
    static consteval std::string check_type() { return {}; }

    // Diagnostic hints appended to §10 messages.
    static constexpr std::string_view bytes_hint = "";
    static constexpr std::string_view key_hint = "";
    static constexpr std::string_view integer_key_spelling = "integer key";
};

template<class Format>
struct format_traits : format_traits_defaults {};

namespace detail {
template<class S> struct format_of_impl { using type = void; };
template<class S> requires requires { typename S::format; }
struct format_of_impl<S> { using type = typename S::format; };
}
template<class S> using format_of = typename detail::format_of_impl<std::remove_cvref_t<S>>::type;

// The "no field" context for top-level values and container elements without a member.
inline constexpr field_meta no_field{};

} // namespace vcodec::core

#endif // VCODEC_CORE_MODEL_HPP

// CBOR-namespace annotations (spec v0.2 §5). They express per-field protocol intent — integer
// keys, tags, determinism — that JSON has no use for and never sees.
#ifndef VCODEC_CBOR_ANNOTATIONS_HPP
#define VCODEC_CBOR_ANNOTATIONS_HPP

#include <cstdint>

namespace vcodec::cbor {

// The format tag used for codec_for<T, cbor::format> and format_traits<cbor::format>.
struct format {};

// ---- field level ------------------------------------------------------------------------

// Integer map key in place of the member's text name. Negative keys are allowed.
struct key_t { std::int64_t key; };
consteval key_t key(std::int64_t k) { return { k }; }

// Also accept the member's wire name as a text key on decode. Never emitted.
struct text_key_alias_t {};   inline constexpr text_key_alias_t text_key_alias{};

// Emit tag n before the value; require it on read. Repeatable, outermost first.
struct tag_t { std::uint64_t tag; };
consteval tag_t tag(std::uint64_t t) { return { t }; }

// Treat a std::uint8_t / unsigned char contiguous range as a byte string (major type 2).
struct byte_string_t {};      inline constexpr byte_string_t byte_string{};

// Encode this array, map or string with indefinite length (never under deterministic).
struct indefinite_t {};       inline constexpr indefinite_t indefinite{};

// Pin the encoded float width instead of preferred serialization.
enum class width : std::uint8_t { preferred, half, single, double_ };
inline constexpr width half    = width::half;
inline constexpr width single  = width::single;
inline constexpr width double_ = width::double_;
struct float_width_t { width w; };
consteval float_width_t float_width(width w) { return { w }; }

// RFC 8746: encode a contiguous numeric range as one tagged byte string.
struct typed_array_t {};      inline constexpr typed_array_t typed_array{};

// ---- type level -------------------------------------------------------------------------

// Every member without an explicit cbor::key gets one, declaration order from 1.
struct integer_keys_t {};     inline constexpr integer_keys_t integer_keys{};

// RFC 8949 §4.2.1 for this type and everything nested in it, in both directions.
struct deterministic_t {};    inline constexpr deterministic_t deterministic{};

// Prefix the top-level value with tag 55799.
struct self_describe_t {};    inline constexpr self_describe_t self_describe{};

inline constexpr std::uint64_t self_describe_tag = 55799;

} // namespace vcodec::cbor

#endif // VCODEC_CBOR_ANNOTATIONS_HPP

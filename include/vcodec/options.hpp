// Shared option types (spec §8). Format-specific option structs live in <format>/options.hpp.
#ifndef VCODEC_OPTIONS_HPP
#define VCODEC_OPTIONS_HPP

#include <cstdint>

namespace vcodec {

enum class duplicate_key : std::uint8_t {
    last_wins,      // the later value replaces the earlier (default)
    first_wins,     // the later value is skipped
    error,          // errc::duplicate_key
};

enum class error_mode : std::uint8_t {
    fail_fast,      // stop at the first error
    collect,        // record and continue at object-member granularity (§9.5)
};

} // namespace vcodec

#endif // VCODEC_OPTIONS_HPP

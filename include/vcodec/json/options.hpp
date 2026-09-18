// JSON options (spec §8). A structural NTTP: every option resolves at compile time.
#ifndef VCODEC_JSON_OPTIONS_HPP
#define VCODEC_JSON_OPTIONS_HPP

#include <vcodec/options.hpp>

#include <cstddef>

namespace vcodec::json {

struct options {
    bool          pretty          = false;
    unsigned      indent          = 2;
    std::size_t   max_depth       = 256;
    bool          validate_utf8   = true;      // on encode as well as decode
    duplicate_key duplicates      = duplicate_key::last_wins;
    error_mode    errors          = error_mode::fail_fast;
};

} // namespace vcodec::json

#endif // VCODEC_JSON_OPTIONS_HPP

// CBOR options (spec v0.2 §8). A structural NTTP, like json::options.
#ifndef VCODEC_CBOR_OPTIONS_HPP
#define VCODEC_CBOR_OPTIONS_HPP

#include <vcodec/cbor/annotations.hpp>
#include <vcodec/options.hpp>

#include <cstddef>

namespace vcodec::cbor {

struct options {
    // encode
    bool          deterministic         = true;    // RFC 8949 §4.2.1 core requirements
    bool          indefinite            = false;   // indefinite lengths where core has no count (only when !deterministic)
    width         floats                = width::preferred;
    bool          self_describe         = false;   // tag 55799 on the top-level value
    // decode
    bool          require_deterministic = false;   // validate §4.2.1 on read
    bool          ignore_unknown_tags   = true;
    bool          undefined_as_null     = true;
    bool          accept_typed_arrays   = true;    // decode RFC 8746 arrays into any numeric range
    // both
    std::size_t   max_depth             = 256;
    bool          validate_utf8         = true;
    duplicate_key duplicates            = duplicate_key::last_wins;
    error_mode    errors                = error_mode::fail_fast;
};

} // namespace vcodec::cbor

#endif // VCODEC_CBOR_OPTIONS_HPP

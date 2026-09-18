// Convenience: everything, core plus JSON.
#ifndef VCODEC_VCODEC_HPP
#define VCODEC_VCODEC_HPP

#include <vcodec/fwd.hpp>
#include <vcodec/error.hpp>
#include <vcodec/options.hpp>
#include <vcodec/core/annotations.hpp>
#include <vcodec/core/codec_for.hpp>
#include <vcodec/core/describe.hpp>
#include <vcodec/core/schema.hpp>
#include <vcodec/core/model.hpp>
#include <vcodec/core/traverse.hpp>
#include <vcodec/core/build.hpp>
#include <vcodec/json.hpp>
#include <vcodec/cbor.hpp>

namespace vcodec {
// §11 rule 2 stated in one line: a type that supports one format and fails at compile time
// under the other.
template<class T> concept cbor_only = cbor::encodable<T> && cbor::decodable<T> && !json::encodable<T>;
template<class T> concept json_only = json::encodable<T> && json::decodable<T> && !cbor::encodable<T>;
}

#endif // VCODEC_VCODEC_HPP

// simdjson adapters for the shared corpus.
//
// Decode uses the On-Demand API through simdjson's tag_invoke customisation point
// (SIMDJSON_SUPPORTS_DESERIALIZATION), so `doc.get<T>(out)` works for every corpus type
// and for std::vector<T> of them. Fields are looked up in declaration order, which is the
// fast path for On-Demand's forward-scanning operator[].
//
// Encode is NOT provided: simdjson v3.13.0 ships no builder API for user-defined types.
// Its only `to_json_string` overloads (ondemand/serialization.h) re-emit an already parsed
// document, and `dom::string_builder` is a private helper for DOM elements. The
// `simdjson::builder::to_json_string(T)` API named in spec §13.7 arrives in a later
// release, so the simdjson column is decode-only in bench_encode/BASELINE.md.
#pragma once

#include "corpus.hpp"

#include <simdjson.h>

#include <string>
#include <vector>

// Same shape as SIMDJSON_TRY, spelled locally so the adapters do not depend on a macro
// that is not part of simdjson's documented surface.
#define BENCH_SIMDJSON_TRY(expr) do { if (auto _err = (expr)) return _err; } while (false)

namespace bench {

template<class V>
simdjson::error_code tag_invoke(simdjson::deserialize_tag, V& val, SmallConfig& out) noexcept {
    simdjson::ondemand::object obj;
    BENCH_SIMDJSON_TRY(val.get_object().get(obj));
    BENCH_SIMDJSON_TRY(obj["name"].get(out.name));
    BENCH_SIMDJSON_TRY(obj["host"].get(out.host));
    BENCH_SIMDJSON_TRY(obj["port"].get(out.port));
    BENCH_SIMDJSON_TRY(obj["timeout_ms"].get(out.timeout_ms));
    BENCH_SIMDJSON_TRY(obj["retries"].get(out.retries));
    BENCH_SIMDJSON_TRY(obj["enable_tls"].get(out.enable_tls));
    BENCH_SIMDJSON_TRY(obj["ratio"].get(out.ratio));
    BENCH_SIMDJSON_TRY(obj["log_level"].get(out.log_level));
    return simdjson::SUCCESS;
}

template<class V>
simdjson::error_code tag_invoke(simdjson::deserialize_tag, V& val, Record& out) noexcept(false) {
    simdjson::ondemand::object obj;
    BENCH_SIMDJSON_TRY(val.get_object().get(obj));
    BENCH_SIMDJSON_TRY(obj["id"].get(out.id));
    BENCH_SIMDJSON_TRY(obj["name"].get(out.name));
    BENCH_SIMDJSON_TRY(obj["email"].get(out.email));
    BENCH_SIMDJSON_TRY(obj["active"].get(out.active));
    BENCH_SIMDJSON_TRY(obj["score"].get(out.score));
    BENCH_SIMDJSON_TRY(obj["tags"].get(out.tags));
    return simdjson::SUCCESS;
}

template<class V>
simdjson::error_code tag_invoke(simdjson::deserialize_tag, V& val, Node& out) noexcept(false) {
    simdjson::ondemand::object obj;
    BENCH_SIMDJSON_TRY(val.get_object().get(obj));
    BENCH_SIMDJSON_TRY(obj["name"].get(out.name));
    BENCH_SIMDJSON_TRY(obj["value"].get(out.value));
    BENCH_SIMDJSON_TRY(obj["children"].get(out.children));
    return simdjson::SUCCESS;
}

template<class V>
simdjson::error_code tag_invoke(simdjson::deserialize_tag, V& val, Document& out) noexcept(false) {
    simdjson::ondemand::object obj;
    BENCH_SIMDJSON_TRY(val.get_object().get(obj));
    BENCH_SIMDJSON_TRY(obj["title"].get(out.title));
    BENCH_SIMDJSON_TRY(obj["body"].get(out.body));
    BENCH_SIMDJSON_TRY(obj["paragraphs"].get(out.paragraphs));
    return simdjson::SUCCESS;
}

// One parser per thread, reused across iterations as simdjson recommends. The input must
// be a padded_string (SIMDJSON_PADDING bytes of slack after the text).
template<class T>
simdjson::error_code simdjson_decode(simdjson::ondemand::parser& parser, simdjson::padded_string const& json, T& out) {
    simdjson::ondemand::document doc;
    BENCH_SIMDJSON_TRY(parser.iterate(json).get(doc));
    return doc.get<T>(out);
}

} // namespace bench

#undef BENCH_SIMDJSON_TRY

// CBOR benchmark corpus (spec §13.7): the four v0.1 shapes from corpus.hpp plus two protocol
// shapes, used by every library under test, with hand-written QCBOR encoders and decoders.
//
// Protocol shapes:
//   CWT claims set — CwtClaims: 7 members with integer keys 1..7 (RFC 8392 claim keys), one
//                    epoch time under tag 1, one byte-string claim id
//   COSE_Key set   — CoseKeySet: 20 EC2 keys with keys 1, 3 and -1..-4 (RFC 9052 §7), three
//                    32-byte byte strings each
//
// Glaze (v8.4.0) reflects struct members to text keys only — its CBOR reader rejects any map
// key that is not a text string — and does not accept a tag in front of a plain integer. It
// therefore benchmarks the *text-key, untagged* twin of each protocol shape (CwtClaimsText,
// CoseKeyText), which carries the same values; to_text/from_text convert so results can
// still be compared value-for-value. vcodec and QCBOR use the integer-key form.
//
// QCBOR is a C library with no reflection: each shape has an encoder written against
// QCBOREncode_* and a decoder written against the spiffy-decode API, which is how QCBOR is
// used in COSE/CWT stacks. Map members are added in RFC 8949 §4.2.1 order so the output is
// the deterministic encoding — QCBOR users sort at authoring time, at zero run-time cost.
#pragma once

#include "corpus.hpp"

#include <vcodec/cbor.hpp>
#include <glaze/cbor.hpp>
#include <qcbor/qcbor_encode.h>
#include <qcbor/qcbor_spiffy_decode.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bench {

// ---- protocol shapes ----

struct CwtClaims {
    [[=vcodec::cbor::key(1)]]                        std::string            iss;
    [[=vcodec::cbor::key(2)]]                        std::string            sub;
    [[=vcodec::cbor::key(3)]]                        std::string            aud;
    [[=vcodec::cbor::key(4), =vcodec::cbor::tag(1)]] std::int64_t           exp = 0;
    [[=vcodec::cbor::key(5)]]                        std::int64_t           nbf = 0;
    [[=vcodec::cbor::key(6)]]                        std::int64_t           iat = 0;
    [[=vcodec::cbor::key(7)]]                        std::vector<std::byte> cti;
    bool operator==(CwtClaims const&) const = default;
};

struct CoseKey {
    [[=vcodec::cbor::key(1)]]  int                    kty = 0;   // 2 = EC2
    [[=vcodec::cbor::key(3)]]  int                    alg = 0;   // -7 = ES256
    [[=vcodec::cbor::key(-1)]] int                    crv = 0;   // 1 = P-256
    [[=vcodec::cbor::key(-2)]] std::vector<std::byte> x;
    [[=vcodec::cbor::key(-3)]] std::vector<std::byte> y;
    [[=vcodec::cbor::key(-4)]] std::vector<std::byte> d;
    bool operator==(CoseKey const&) const = default;
};
using CoseKeySet = std::vector<CoseKey>;

// Runtime map — the one shape whose key order is not known at compile time. Struct members
// are emitted in canonical order straight from core, so this is the only shape on which
// vcodec's deterministic mode has to buffer and sort (spec §13.7: "the cost of buffering
// runtime maps is visible").
using MetricMap = std::unordered_map<std::string, double>;

// Text-key twins for Glaze (see the header comment). No annotations, so vcodec encodes them
// with text keys too; that encoding is Glaze's decode input.
struct CwtClaimsText {
    std::string iss, sub, aud;
    std::int64_t exp = 0, nbf = 0, iat = 0;
    std::vector<std::byte> cti;
};
struct CoseKeyText {
    int kty = 0, alg = 0, crv = 0;
    std::vector<std::byte> x, y, d;
};
using CoseKeySetText = std::vector<CoseKeyText>;

template<class T> T const& to_text(T const& v) { return v; }
template<class T> T const& from_text(T const& v) { return v; }
inline CwtClaimsText to_text(CwtClaims const& c) { return { c.iss, c.sub, c.aud, c.exp, c.nbf, c.iat, c.cti }; }
inline CwtClaims from_text(CwtClaimsText const& c) { return { c.iss, c.sub, c.aud, c.exp, c.nbf, c.iat, c.cti }; }
inline CoseKeySetText to_text(CoseKeySet const& v) {
    CoseKeySetText out;
    out.reserve(v.size());
    for (auto const& k : v) out.push_back({ k.kty, k.alg, k.crv, k.x, k.y, k.d });
    return out;
}
inline CoseKeySet from_text(CoseKeySetText const& v) {
    CoseKeySet out;
    out.reserve(v.size());
    for (auto const& k : v) out.push_back({ k.kty, k.alg, k.crv, k.x, k.y, k.d });
    return out;
}
template<class T> using text_form_t = std::remove_cvref_t<decltype(to_text(std::declval<T const&>()))>;

// ---- value equality for the corpus.hpp shapes (they are plain aggregates without ==) ----

inline bool operator==(SmallConfig const& a, SmallConfig const& b) {
    return a.name == b.name && a.host == b.host && a.port == b.port && a.timeout_ms == b.timeout_ms
        && a.retries == b.retries && a.enable_tls == b.enable_tls && a.ratio == b.ratio && a.log_level == b.log_level;
}
inline bool operator==(Record const& a, Record const& b) {
    return a.id == b.id && a.name == b.name && a.email == b.email && a.active == b.active && a.score == b.score && a.tags == b.tags;
}
inline bool operator==(Node const& a, Node const& b) {
    return a.name == b.name && a.value == b.value && a.children == b.children;
}
inline bool operator==(Document const& a, Document const& b) {
    return a.title == b.title && a.body == b.body && a.paragraphs == b.paragraphs;
}

// ---- generators ----

inline std::vector<std::byte> random_bytes(std::mt19937_64& rng, std::size_t n) {
    std::vector<std::byte> out(n);
    for (auto& b : out) b = static_cast<std::byte>(rng() & 0xFF);
    return out;
}

// RFC 8392 Appendix A.1 values.
inline CwtClaims make_cwt_claims() {
    CwtClaims c;
    c.iss = "coap://as.example.com";
    c.sub = "erikw";
    c.aud = "coap://light.example.com";
    c.exp = 1444064944;
    c.nbf = 1443944944;
    c.iat = 1443944944;
    c.cti = { std::byte{0x0b}, std::byte{0x71} };
    return c;
}

inline CoseKeySet make_cose_keys(std::size_t n = 20) {
    std::mt19937_64 rng(0x5eed'c0de'0004ull);
    CoseKeySet out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        CoseKey k;
        k.kty = 2;
        k.alg = -7;
        k.crv = 1;
        k.x = random_bytes(rng, 32);
        k.y = random_bytes(rng, 32);
        k.d = random_bytes(rng, 32);
        out.push_back(std::move(k));
    }
    return out;
}

inline MetricMap make_metric_map() {
    static constexpr std::string_view kinds[] = {"cpu", "mem", "disk", "net", "gpu", "io", "rpc", "db"};
    static constexpr std::string_view stats[] = {"p50", "p90", "p99", "max", "mean", "rate", "count", "err"};
    std::mt19937_64 rng(0x5eed'c0de'0005ull);
    std::uniform_real_distribution<double> val(0.0, 1000.0);
    MetricMap m;
    for (auto k : kinds)
        for (auto st : stats) m.emplace(std::string(k) + "." + std::string(st), std::round(val(rng) * 1000.0) / 1000.0);
    return m;   // 64 entries
}

// ---- QCBOR ----

namespace qc {

inline UsefulBufC ub(std::string const& s)            { return { s.data(), s.size() }; }
inline UsefulBufC ub(std::vector<std::byte> const& b) { return { b.data(), b.size() }; }
inline void assign(std::string& out, UsefulBufC s) { out.assign(static_cast<char const*>(s.ptr), s.len); }
inline void assign(std::vector<std::byte>& out, UsefulBufC s) {
    auto const* p = static_cast<std::byte const*>(s.ptr);
    out.assign(p, p + s.len);
}

// -- encode. Members are added in deterministic (§4.2.1: bytewise on the encoded key) order.

inline void put(QCBOREncodeContext& c, SmallConfig const& v) {
    QCBOREncode_OpenMap(&c);
    QCBOREncode_AddTextToMapSZ(&c, "host", ub(v.host));
    QCBOREncode_AddTextToMapSZ(&c, "name", ub(v.name));
    QCBOREncode_AddUInt64ToMapSZ(&c, "port", v.port);
    QCBOREncode_AddDoubleToMapSZ(&c, "ratio", v.ratio);
    QCBOREncode_AddInt64ToMapSZ(&c, "retries", v.retries);
    QCBOREncode_AddTextToMapSZ(&c, "log_level", ub(v.log_level));
    QCBOREncode_AddBoolToMapSZ(&c, "enable_tls", v.enable_tls);
    QCBOREncode_AddInt64ToMapSZ(&c, "timeout_ms", v.timeout_ms);
    QCBOREncode_CloseMap(&c);
}

inline void put(QCBOREncodeContext& c, Record const& v) {
    QCBOREncode_OpenMap(&c);
    QCBOREncode_AddUInt64ToMapSZ(&c, "id", v.id);
    QCBOREncode_AddTextToMapSZ(&c, "name", ub(v.name));
    QCBOREncode_OpenArrayInMapSZ(&c, "tags");
    for (auto const& t : v.tags) QCBOREncode_AddText(&c, ub(t));
    QCBOREncode_CloseArray(&c);
    QCBOREncode_AddTextToMapSZ(&c, "email", ub(v.email));
    QCBOREncode_AddDoubleToMapSZ(&c, "score", v.score);
    QCBOREncode_AddBoolToMapSZ(&c, "active", v.active);
    QCBOREncode_CloseMap(&c);
}

inline void put(QCBOREncodeContext& c, Node const& v) {
    QCBOREncode_OpenMap(&c);
    QCBOREncode_AddTextToMapSZ(&c, "name", ub(v.name));
    QCBOREncode_AddInt64ToMapSZ(&c, "value", v.value);
    if (v.children.empty()) {
        // QCBOR tracks at most QCBOR_MAX_ARRAY_NESTING (15) open containers and the depth-8
        // tree's empty leaf arrays sit at level 16. An empty array is the constant byte 0x80,
        // so it is spliced in pre-encoded (QCBOREncode_AddEncoded is the API for that) instead
        // of being opened. The decoder has no such problem: it does not descend into empty
        // definite-length arrays.
        static constexpr unsigned char empty_array = 0x80;
        QCBOREncode_AddEncodedToMapSZ(&c, "children", UsefulBufC{ &empty_array, 1 });
    } else {
        QCBOREncode_OpenArrayInMapSZ(&c, "children");
        for (auto const& ch : v.children) put(c, ch);
        QCBOREncode_CloseArray(&c);
    }
    QCBOREncode_CloseMap(&c);
}

inline void put(QCBOREncodeContext& c, Document const& v) {
    QCBOREncode_OpenMap(&c);
    QCBOREncode_AddTextToMapSZ(&c, "body", ub(v.body));
    QCBOREncode_AddTextToMapSZ(&c, "title", ub(v.title));
    QCBOREncode_OpenArrayInMapSZ(&c, "paragraphs");
    for (auto const& p : v.paragraphs) QCBOREncode_AddText(&c, ub(p));
    QCBOREncode_CloseArray(&c);
    QCBOREncode_CloseMap(&c);
}

inline void put(QCBOREncodeContext& c, CwtClaims const& v) {
    QCBOREncode_OpenMap(&c);
    QCBOREncode_AddTextToMapN(&c, 1, ub(v.iss));
    QCBOREncode_AddTextToMapN(&c, 2, ub(v.sub));
    QCBOREncode_AddTextToMapN(&c, 3, ub(v.aud));
    QCBOREncode_AddTDateEpochToMapN(&c, 4, QCBOR_ENCODE_AS_TAG, v.exp);
    QCBOREncode_AddInt64ToMapN(&c, 5, v.nbf);
    QCBOREncode_AddInt64ToMapN(&c, 6, v.iat);
    QCBOREncode_AddBytesToMapN(&c, 7, ub(v.cti));
    QCBOREncode_CloseMap(&c);
}

inline void put(QCBOREncodeContext& c, CoseKey const& v) {
    QCBOREncode_OpenMap(&c);
    QCBOREncode_AddInt64ToMapN(&c, 1, v.kty);
    QCBOREncode_AddInt64ToMapN(&c, 3, v.alg);
    QCBOREncode_AddInt64ToMapN(&c, -1, v.crv);
    QCBOREncode_AddBytesToMapN(&c, -2, ub(v.x));
    QCBOREncode_AddBytesToMapN(&c, -3, ub(v.y));
    QCBOREncode_AddBytesToMapN(&c, -4, ub(v.d));
    QCBOREncode_CloseMap(&c);
}

inline void put(QCBOREncodeContext& c, MetricMap const& v) {
    // Iteration order: QCBOR v1.5.1 has no map sorting, so for a runtime map this row's output
    // is not the deterministic encoding — it is what a QCBOR user gets without sorting by hand.
    QCBOREncode_OpenMap(&c);
    for (auto const& [k, d] : v) QCBOREncode_AddDoubleToMapSZ(&c, k.c_str(), d);
    QCBOREncode_CloseMap(&c);
}

template<class T>
void put(QCBOREncodeContext& c, std::vector<T> const& v) {
    QCBOREncode_OpenArray(&c);
    for (auto const& e : v) put(c, e);
    QCBOREncode_CloseArray(&c);
}

// QCBOR writes into a caller-supplied buffer of fixed size. encoded_size runs the encoder in
// its size-calculation mode once; encode then writes into `buf` (which must be at least that
// large) and returns the encoded length, 0 on error.
template<class T>
std::size_t encoded_size(T const& v) {
    QCBOREncodeContext c;
    QCBOREncode_Init(&c, SizeCalculateUsefulBuf);
    put(c, v);
    std::size_t n = 0;
    return QCBOREncode_FinishGetSize(&c, &n) == QCBOR_SUCCESS ? n : 0;
}

template<class T>
std::size_t encode(T const& v, std::vector<std::byte>& buf) {
    QCBOREncodeContext c;
    QCBOREncode_Init(&c, UsefulBuf{ buf.data(), buf.size() });
    put(c, v);
    UsefulBufC out{};
    return QCBOREncode_Finish(&c, &out) == QCBOR_SUCCESS ? out.len : 0;
}

// -- decode (spiffy). Every Get* is a no-op once the context is in an error state, so the
// error is checked once per container; string outputs start empty so a failed Get leaves a
// harmless empty copy behind before the error is reported.

inline bool ok(QCBORDecodeContext& c) { return QCBORDecode_GetError(&c) == QCBOR_SUCCESS; }

// The last error a decoder saw before it was reset or before Finish; for diagnosing a
// verification failure, not read on the hot path.
inline thread_local QCBORError last_error = QCBOR_SUCCESS;

// Declared up front: get_items below calls get() on element types that live in namespace
// bench, so argument-dependent lookup does not reach into bench::qc for them.
inline bool get(QCBORDecodeContext& c, SmallConfig& v);
inline bool get(QCBORDecodeContext& c, Record& v);
inline bool get(QCBORDecodeContext& c, Node& v);
inline bool get(QCBORDecodeContext& c, Document& v);
inline bool get(QCBORDecodeContext& c, CwtClaims& v);
inline bool get(QCBORDecodeContext& c, CoseKey& v);
inline bool get(QCBORDecodeContext& c, MetricMap& v);
template<class T> bool get(QCBORDecodeContext& c, std::vector<T>& v);

inline bool get(QCBORDecodeContext& c, SmallConfig& v) {
    QCBORDecode_EnterMap(&c, nullptr);
    if (!ok(c)) return false;
    UsefulBufC s{};
    std::uint64_t port = 0;
    std::int64_t timeout = 0, retries = 0;
    QCBORDecode_GetTextStringInMapSZ(&c, "name", &s);      assign(v.name, s);
    QCBORDecode_GetTextStringInMapSZ(&c, "host", &s);      assign(v.host, s);
    QCBORDecode_GetUInt64InMapSZ(&c, "port", &port);
    QCBORDecode_GetInt64InMapSZ(&c, "timeout_ms", &timeout);
    QCBORDecode_GetInt64InMapSZ(&c, "retries", &retries);
    QCBORDecode_GetBoolInMapSZ(&c, "enable_tls", &v.enable_tls);
    QCBORDecode_GetDoubleInMapSZ(&c, "ratio", &v.ratio);
    QCBORDecode_GetTextStringInMapSZ(&c, "log_level", &s); assign(v.log_level, s);
    QCBORDecode_ExitMap(&c);
    v.port = static_cast<std::uint16_t>(port);
    v.timeout_ms = static_cast<int>(timeout);
    v.retries = static_cast<int>(retries);
    return ok(c);
}

// Reads text strings until the entered array is exhausted.
inline bool get_strings(QCBORDecodeContext& c, char const* label, std::vector<std::string>& out) {
    QCBORDecode_EnterArrayFromMapSZ(&c, label);
    if (!ok(c)) return false;
    for (;;) {
        UsefulBufC s{};
        QCBORDecode_GetTextString(&c, &s);
        if (!ok(c)) break;
        out.emplace_back(static_cast<char const*>(s.ptr), s.len);
    }
    if (QCBORDecode_GetAndResetError(&c) != QCBOR_ERR_NO_MORE_ITEMS) return false;
    QCBORDecode_ExitArray(&c);
    return ok(c);
}

// Reads structs until the entered array is exhausted; the item that fails with
// QCBOR_ERR_NO_MORE_ITEMS is the one past the end, so it is popped again.
template<class T>
bool get_items(QCBORDecodeContext& c, std::vector<T>& out) {
    for (;;) {
        out.emplace_back();
        if (!get(c, out.back())) { out.pop_back(); break; }
    }
    QCBORError const e = QCBORDecode_GetAndResetError(&c);
    if (e == QCBOR_ERR_NO_MORE_ITEMS) return true;
    if (e != QCBOR_SUCCESS) last_error = e;   // keep the first error while the failure unwinds
    return false;
}

inline bool get(QCBORDecodeContext& c, Record& v) {
    QCBORDecode_EnterMap(&c, nullptr);
    if (!ok(c)) return false;
    UsefulBufC s{};
    QCBORDecode_GetUInt64InMapSZ(&c, "id", &v.id);
    QCBORDecode_GetTextStringInMapSZ(&c, "name", &s);  assign(v.name, s);
    QCBORDecode_GetTextStringInMapSZ(&c, "email", &s); assign(v.email, s);
    QCBORDecode_GetBoolInMapSZ(&c, "active", &v.active);
    QCBORDecode_GetDoubleInMapSZ(&c, "score", &v.score);
    if (!get_strings(c, "tags", v.tags)) return false;
    QCBORDecode_ExitMap(&c);
    return ok(c);
}

// The depth-8 tree needs 16 nesting levels under spiffy decode — a bounded map search descends
// one level further than the data — which is one more than QCBOR's compile-time
// QCBOR_MAX_ARRAY_NESTING (15): a depth-7 tree decodes, depth 8 does not. The tree is
// therefore read with QCBOR's basic API, a QCBORDecode_GetNext preorder traversal, which stays
// within the limit. Every other shape uses spiffy decode.
inline bool get(QCBORDecodeContext& c, Node& v) {
    QCBORItem it;
    if (QCBORDecode_GetNext(&c, &it) != QCBOR_SUCCESS || it.uDataType != QCBOR_TYPE_MAP) return false;
    for (std::uint16_t i = 0, n = it.val.uCount; i < n; ++i) {
        if (QCBORDecode_GetNext(&c, &it) != QCBOR_SUCCESS || it.uLabelType != QCBOR_TYPE_TEXT_STRING) return false;
        std::string_view const key(static_cast<char const*>(it.label.string.ptr), it.label.string.len);
        if (key == "name") {
            if (it.uDataType != QCBOR_TYPE_TEXT_STRING) return false;
            assign(v.name, it.val.string);
        } else if (key == "value") {
            if (it.uDataType != QCBOR_TYPE_INT64) return false;
            v.value = static_cast<int>(it.val.int64);
        } else if (key == "children") {
            if (it.uDataType != QCBOR_TYPE_ARRAY) return false;
            v.children.resize(it.val.uCount);
            for (auto& ch : v.children) if (!get(c, ch)) return false;
        } else {
            return false;
        }
    }
    return true;
}

inline bool get(QCBORDecodeContext& c, Document& v) {
    QCBORDecode_EnterMap(&c, nullptr);
    if (!ok(c)) return false;
    UsefulBufC s{};
    QCBORDecode_GetTextStringInMapSZ(&c, "title", &s); assign(v.title, s);
    QCBORDecode_GetTextStringInMapSZ(&c, "body", &s);  assign(v.body, s);
    if (!get_strings(c, "paragraphs", v.paragraphs)) return false;
    QCBORDecode_ExitMap(&c);
    return ok(c);
}

inline bool get(QCBORDecodeContext& c, CwtClaims& v) {
    QCBORDecode_EnterMap(&c, nullptr);
    if (!ok(c)) return false;
    UsefulBufC s{};
    QCBORDecode_GetTextStringInMapN(&c, 1, &s); assign(v.iss, s);
    QCBORDecode_GetTextStringInMapN(&c, 2, &s); assign(v.sub, s);
    QCBORDecode_GetTextStringInMapN(&c, 3, &s); assign(v.aud, s);
    QCBORDecode_GetEpochDateInMapN(&c, 4, QCBOR_TAG_REQUIREMENT_TAG, &v.exp);
    QCBORDecode_GetInt64InMapN(&c, 5, &v.nbf);
    QCBORDecode_GetInt64InMapN(&c, 6, &v.iat);
    QCBORDecode_GetByteStringInMapN(&c, 7, &s);  assign(v.cti, s);
    QCBORDecode_ExitMap(&c);
    return ok(c);
}

inline bool get(QCBORDecodeContext& c, CoseKey& v) {
    QCBORDecode_EnterMap(&c, nullptr);
    if (!ok(c)) return false;
    UsefulBufC s{};
    std::int64_t kty = 0, alg = 0, crv = 0;
    QCBORDecode_GetInt64InMapN(&c, 1, &kty);
    QCBORDecode_GetInt64InMapN(&c, 3, &alg);
    QCBORDecode_GetInt64InMapN(&c, -1, &crv);
    QCBORDecode_GetByteStringInMapN(&c, -2, &s); assign(v.x, s);
    QCBORDecode_GetByteStringInMapN(&c, -3, &s); assign(v.y, s);
    QCBORDecode_GetByteStringInMapN(&c, -4, &s); assign(v.d, s);
    QCBORDecode_ExitMap(&c);
    v.kty = static_cast<int>(kty);
    v.alg = static_cast<int>(alg);
    v.crv = static_cast<int>(crv);
    return ok(c);
}

// A map with unknown keys has no spiffy form; QCBORDecode_GetNext over the entries is the API.
inline bool get(QCBORDecodeContext& c, MetricMap& v) {
    QCBORItem it;
    if (QCBORDecode_GetNext(&c, &it) != QCBOR_SUCCESS || it.uDataType != QCBOR_TYPE_MAP) return false;
    for (std::uint16_t i = 0, n = it.val.uCount; i < n; ++i) {
        if (QCBORDecode_GetNext(&c, &it) != QCBOR_SUCCESS || it.uLabelType != QCBOR_TYPE_TEXT_STRING) return false;
        double d = 0;
        if (it.uDataType == QCBOR_TYPE_DOUBLE) d = it.val.dfnum;
        else if (it.uDataType == QCBOR_TYPE_FLOAT) d = it.val.fnum;
        else if (it.uDataType == QCBOR_TYPE_INT64) d = static_cast<double>(it.val.int64);
        else return false;
        v.emplace(std::string(static_cast<char const*>(it.label.string.ptr), it.label.string.len), d);
    }
    return true;
}

template<class T>
bool get(QCBORDecodeContext& c, std::vector<T>& v) {
    QCBORDecode_EnterArray(&c, nullptr);
    if (!ok(c)) return false;
    if (!get_items(c, v)) return false;
    QCBORDecode_ExitArray(&c);
    return ok(c);
}

template<class T>
bool decode(std::span<std::byte const> in, T& out) {
    QCBORDecodeContext c;
    QCBORDecode_Init(&c, UsefulBufC{ in.data(), in.size() }, QCBOR_DECODE_MODE_NORMAL);
    if (!get(c, out)) { if (!ok(c)) last_error = QCBORDecode_GetError(&c); return false; }
    last_error = QCBORDecode_Finish(&c);
    return last_error == QCBOR_SUCCESS;
}

} // namespace qc
} // namespace bench

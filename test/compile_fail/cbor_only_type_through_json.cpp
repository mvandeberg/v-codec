// A type that is fine in CBOR fails at compile time under JSON (§11 rule 2), with the
// two-format ending.
// expect-error: static assertion failed: Claims::cwt_id (std::vector<std::byte>)
// expect-error: is byte-like but has no JSON lowering.
// expect-error: Add [[=vcodec::json::bytes(vcodec::json::base64)]], or mark it [[=vcodec::skip]].
#include <vcodec/vcodec.hpp>
#include <vector>
struct Claims { [[=vcodec::cbor::key(7)]] std::vector<std::byte> cwt_id; };
static_assert(vcodec::cbor::encodable<Claims>);
int main() { return vcodec::json::encode(Claims{}).size(); }

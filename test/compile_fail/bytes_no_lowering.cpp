// expect-error: static assertion failed: Claims::nonce (std::vector<std::byte>)
// expect-error: is byte-like but has no JSON lowering.
// expect-error: Add [[=vcodec::json::bytes(vcodec::json::base64)]], or mark it [[=vcodec::skip]].
#include <vcodec/json.hpp>
#include <cstddef>
#include <vector>
struct Claims { std::vector<std::byte> nonce; };
int main() { return vcodec::json::encode(Claims{}).size(); }

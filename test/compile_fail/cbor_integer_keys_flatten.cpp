// expect-error: static assertion failed: Claims (declared [[=cbor::integer_keys]]) has a flattened or inherited member Ext::extra;
// expect-error: declaration-order key allocation is not defined across a flatten or a base class.
// expect-error: Give every member an explicit [[=vcodec::cbor::key(n)]].
#include <vcodec/cbor.hpp>
struct Ext { int extra = 0; };
struct [[=vcodec::cbor::integer_keys]] Claims { int a = 0; [[=vcodec::flatten]] Ext ext; };
int main() { return vcodec::cbor::encode(Claims{}).size(); }

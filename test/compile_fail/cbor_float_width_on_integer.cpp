// expect-error: static assertion failed: S::n (int)
// expect-error: is declared [[=cbor::float_width(...)]] but is not a floating type.
#include <vcodec/cbor.hpp>
struct S { [[=vcodec::cbor::float_width(vcodec::cbor::half)]] int n = 0; };
int main() { return vcodec::cbor::encode(S{}).size(); }

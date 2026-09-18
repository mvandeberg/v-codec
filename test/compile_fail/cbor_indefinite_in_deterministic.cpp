// expect-error: static assertion failed: Stream::events (std::vector<int>)
// expect-error: is declared [[=cbor::indefinite]] inside Stream, which is declared [[=cbor::deterministic]];
// expect-error: deterministic encoding forbids indefinite lengths.
#include <vcodec/cbor.hpp>
#include <vector>
struct [[=vcodec::cbor::deterministic]] Stream { [[=vcodec::cbor::indefinite]] std::vector<int> events; };
int main() { return vcodec::cbor::encode(Stream{}).size(); }

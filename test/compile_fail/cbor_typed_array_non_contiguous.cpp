// expect-error: static assertion failed: Frame::samples (std::list<double>)
// expect-error: is declared [[=cbor::typed_array]] but is not a contiguous range of a
// expect-error: fixed-width integer or floating type.
#include <vcodec/cbor.hpp>
#include <list>
struct Frame { [[=vcodec::cbor::typed_array]] std::list<double> samples; };
int main() { return vcodec::cbor::encode(Frame{}).size(); }

// expect-error: static assertion failed: Event::when (std::chrono::time_point<std::chrono::system_clock, std::chrono::duration<long int>>)
// expect-error: carries [[=cbor::tag(1)]] and [[=cbor::tag(0)]]; a time point takes one of the two.
#include <vcodec/cbor.hpp>
#include <chrono>
struct Event { [[=vcodec::cbor::tag(1), =vcodec::cbor::tag(0)]] std::chrono::sys_seconds when; };
int main() { return vcodec::cbor::encode(Event{}).size(); }

// Every entry point is gated, not just encode/decode.
// expect-error: static assertion failed: Cfg::fn (type void (*)())
// expect-error: has no codec.
#include <vcodec/json.hpp>
struct Cfg { void (*fn)() = nullptr; };
int main() { return vcodec::json::decode_all<Cfg>("{}").complete(); }

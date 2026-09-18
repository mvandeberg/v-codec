// expect-error: static assertion failed: Config::handle (type void*)
// expect-error: has no codec. Provide vcodec::codec_for<void*>, mark the member
// expect-error: [[=vcodec::skip]], or give it [[=vcodec::with<YourCodec>]].
#include <vcodec/json.hpp>
struct Config { void* handle; };
int main() { return vcodec::json::encode(Config{}).size(); }

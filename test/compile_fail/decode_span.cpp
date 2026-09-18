// expect-error: static assertion failed: Frame::samples (std::span<const int>)
// expect-error: is encode-only and cannot be decoded into. Use std::vector
#include <vcodec/json.hpp>
#include <span>
struct Frame { std::span<const int> samples; };
int main() { Frame f; return vcodec::json::decode_into(f, "{}").has_value(); }

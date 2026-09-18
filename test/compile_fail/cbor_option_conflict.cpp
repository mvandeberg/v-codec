// The option pair is a static_assert on the writer itself, so the frame names the writer,
// not an entry point.
// guard-exempt
// expect-error: cbor::options: indefinite lengths are not deterministic; pick one
#include <vcodec/cbor.hpp>
int main() {
    constexpr vcodec::cbor::options bad{ .deterministic = true, .indefinite = true };
    return vcodec::cbor::encode<bad>(1).size();
}

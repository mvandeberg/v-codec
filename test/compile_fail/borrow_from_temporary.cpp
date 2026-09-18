// A type with std::string_view members cannot decode from a temporary std::string: the
// buffer dies with the full expression. This is a deleted overload, not a static_assert, so
// the no-cascade guard is exempt; the message still names the cause.
// guard-exempt
// expect-error: this type borrows from the input (it has std::string_view members); pass a buffer that outlives the result
#include <vcodec/json.hpp>
#include <string>
#include <string_view>
struct Header { std::string_view name; };
std::string fetch();
int main() { return vcodec::json::decode<Header>(fetch()).has_value(); }

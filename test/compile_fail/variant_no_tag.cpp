// expect-error: static assertion failed: Event::payload (std::variant<A, B>)
// expect-error: needs a discriminator. Add [[=vcodec::tag("type")]] to Event.
#include <vcodec/json.hpp>
#include <variant>
struct A { int a = 0; };
struct B { int b = 0; };
struct Event { std::variant<A, B> payload; };
int main() { return vcodec::json::decode<Event>("{}").has_value(); }

// expect-error: static assertion failed: Pair is declared [[=vcodec::transparent]] but has 2 members; transparent requires exactly one.
#include <vcodec/json.hpp>
struct [[=vcodec::transparent]] Pair { int a = 0; int b = 0; };
int main() { return vcodec::json::encode(Pair{}).size(); }

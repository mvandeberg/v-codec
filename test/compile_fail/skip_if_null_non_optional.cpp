// expect-error: static assertion failed: Cfg::port (int)
// expect-error: is declared [[=vcodec::skip_if_null]] but is not optional-like
#include <vcodec/json.hpp>
struct Cfg { [[=vcodec::skip_if_null]] int port = 0; };
int main() { return vcodec::json::encode(Cfg{}).size(); }

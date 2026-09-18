// expect-error: static assertion failed: Cfg::port is declared [[=vcodec::flatten]] but its type int is not a struct.
#include <vcodec/json.hpp>
struct Cfg { [[=vcodec::flatten]] int port = 0; };
int main() { return vcodec::json::encode(Cfg{}).size(); }

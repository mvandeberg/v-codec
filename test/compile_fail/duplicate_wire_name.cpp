// expect-error: static assertion failed: Server::host and Server::hostname both map to the wire name "host" (via name("host")).
#include <vcodec/json.hpp>
#include <string>
struct Server { std::string host; [[=vcodec::name("host")]] std::string hostname; };
int main() { return vcodec::json::encode(Server{}).size(); }

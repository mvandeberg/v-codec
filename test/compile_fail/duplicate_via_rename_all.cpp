// expect-error: static assertion failed: Server::host_name and Server::hostName both map to the wire name "host-name" (via rename_all).
#include <vcodec/json.hpp>
#include <string>
struct [[=vcodec::rename_all(vcodec::case_::kebab)]] Server { std::string host_name; std::string hostName; };
int main() { return vcodec::json::encode(Server{}).size(); }

// The §10 example: the path names every member on the way down, and nothing else is
// reported — no cascade through the containers between Config and the handle.
// expect-error: static assertion failed: Config::database::pool::handle (type void*)
// expect-error: has no codec.
// expect-not: std::vector
#include <vcodec/json.hpp>
#include <optional>
#include <vector>
struct Pool { int size = 0; void* handle; };
struct Database { std::optional<std::vector<Pool>> pool; };
struct Config { Database database; };
int main() { return vcodec::json::encode(Config{}).size(); }

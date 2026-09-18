// expect-error: static assertion failed: Index::by_id (std::map<int, std::string>)
// expect-error: has integer keys, which JSON cannot represent.
// expect-error: Add [[=vcodec::json::stringify_keys]], use string keys, or mark it [[=vcodec::skip]].
#include <vcodec/json.hpp>
#include <map>
#include <string>
struct Index { std::map<int, std::string> by_id; };
int main() { return vcodec::json::encode(Index{}).size(); }

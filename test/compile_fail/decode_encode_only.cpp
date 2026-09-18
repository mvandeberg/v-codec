// expect-error: static assertion failed: Row::label (const char*)
// expect-error: is encode-only and cannot be decoded into. Use std::string, or mark it [[=vcodec::skip_deserializing]].
#include <vcodec/json.hpp>
struct Row { const char* label = ""; };
int main() { return vcodec::json::decode<Row>("{}").has_value(); }

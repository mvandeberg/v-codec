// expect-error: static assertion failed: Claims::issuer and Claims::subject both map to the CBOR key 2 (via cbor::key(2)).
#include <vcodec/cbor.hpp>
#include <string>
struct Claims { [[=vcodec::cbor::key(2)]] std::string issuer; [[=vcodec::cbor::key(2)]] std::string subject; };
int main() { return vcodec::cbor::encode(Claims{}).size(); }

#include "MUXRPC.h"
#include <catch2/catch_all.hpp>

using namespace muxrpc;

// Partly to make me feel better about relying on C++ enum numbering.
TEST_CASE("Correctly determines payload type from MUXRPC header", "[MUXRPC]") {
  Header header;
  header.flags = 0;
  REQUIRE(header.bodyType() == BodyType::BINARY);
  header.flags = 1;
  REQUIRE(header.bodyType() == BodyType::UTF8_STRING);
  header.flags = 2;
  REQUIRE(header.bodyType() == BodyType::JSON);
  header.flags = 4;
  REQUIRE(header.bodyType() == BodyType::BINARY);
  header.flags = 5;
  REQUIRE(header.bodyType() == BodyType::UTF8_STRING);
  header.flags = 6;
  REQUIRE(header.bodyType() == BodyType::JSON);
}

#include "MUXRPC.h"
#include <catch2/catch_all.hpp>
#include <cstring>

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

TEST_CASE("Correctly determines body length from MUXRPC header",
          "[MUXRPC][debugging]") {
  unsigned char buffer[9];
  std::memset(buffer, 0, sizeof(buffer));
  buffer[4] = 32;
  Header header;
  REQUIRE(header.readFromBuffer(buffer) == B_OK);
  REQUIRE(header.bodyLength == 32);
  buffer[4] = 0;
  buffer[1] = 0x88;
  REQUIRE(header.readFromBuffer(buffer) == B_OK);
  REQUIRE(header.bodyLength == 0x88000000);
}
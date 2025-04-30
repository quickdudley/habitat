#include "Invite.h"
#include <catch2/catch_all.hpp>

TEST_CASE("Correctly parses room server invite code", "[invites][rooms]") {
  BString example("net:example.com:8008~"
                  "shs:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=:"
                  "SSB+Room+PSK3TLYC2T86EHQCUHBUHASCASE18JBV24=");
  BMessage result;
  REQUIRE(invite::parse(&result, example) == B_OK);
  BString hostname;
  REQUIRE(result.FindString("hostname", &hostname) == B_OK);
  REQUIRE(hostname == "example.com:8008");
  BString cypherkey;
  REQUIRE(result.FindString("cypherkey", &cypherkey) == B_OK);
  REQUIRE(cypherkey == "@AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=.ed25519");
}

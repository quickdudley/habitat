#include "JSON.h"
#include <catch2/catch_all.hpp>

TEST_CASE( "Handles unicode", "[JSON::escapeString]") {
  BString source("🀐");
  BString result = JSON::escapeString(source);
  REQUIRE(result == "\"🀐\"");
}
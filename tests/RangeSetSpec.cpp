#include "DatePage.h"
#include <catch2/catch_all.hpp>

TEST_CASE("Successfully inserts and queries single range", "[range_set]") {
  RangeSet op;
  op.set(1, 2, true);
  REQUIRE(op[0] == false);
  REQUIRE(op[1] == true);
  REQUIRE(op[2] == true);
  REQUIRE(op[3] == false);
}

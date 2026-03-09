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

TEST_CASE("Merges two adjacent ranges", "[range_set]") {
  RangeSet op;
  op.set(1, 2, true);
  op.set(3, 4, true);
  for (int64 i = 1; i <= 4; i++)
    REQUIRE(op[i] == true);
  REQUIRE(op[0] == false);
  REQUIRE(op[5] == false);
  auto i = op.ranges().begin();
  REQUIRE(i->first == 1);
  REQUIRE(i->second == 4);
  i++;
  REQUIRE(i == op.ranges().end());
}

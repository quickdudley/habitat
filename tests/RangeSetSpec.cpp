#include "DatePage.h"
#include <catch2/catch_all.hpp>

TEST_CASE("RangeSet successfully inserts and queries single range",
          "[range_set]") {
  RangeSet op;
  op.set(1, 2, true);
  REQUIRE(op[0] == false);
  REQUIRE(op[1] == true);
  REQUIRE(op[2] == true);
  REQUIRE(op[3] == false);
}

TEST_CASE("RangeSet merges two adjacent ranges", "[range_set]") {
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

TEST_CASE("RangeSet merges two overlapping ranges", "[range_set]") {
  RangeSet op;
  op.set(1, 2, true);
  op.set(2, 3, true);
  for (int64 i = 1; i <= 3; i++)
    REQUIRE(op[i] == true);
  REQUIRE(op[0] == false);
  REQUIRE(op[4] == false);
  auto i = op.ranges().begin();
  REQUIRE(i->first == 1);
  REQUIRE(i->second == 3);
  i++;
  REQUIRE(i == op.ranges().end());
}

TEST_CASE("RangeSet merges two overlapping ranges (reverse order)",
          "[range_set]") {
  RangeSet op;
  op.set(2, 3, true);
  op.set(1, 2, true);
  for (int64 i = 1; i <= 3; i++)
    REQUIRE(op[i] == true);
  REQUIRE(op[0] == false);
  REQUIRE(op[4] == false);
  auto i = op.ranges().begin();
  REQUIRE(i->first == 1);
  REQUIRE(i->second == 3);
  i++;
  REQUIRE(i == op.ranges().end());
}

TEST_CASE("RangeSet splits larger range when middle is unset", "[range_set]") {
  RangeSet op;
  op.set(1, 5, true);
  op.set(3, 3, false);
  for (int64 i = 1; i <= 5; i++)
    REQUIRE(op[i] == (i != 3));
  auto i = op.ranges().begin();
  REQUIRE(i->first == 1);
  REQUIRE(i->second == 2);
  i++;
  REQUIRE(i->first == 4);
  REQUIRE(i->second == 5);
  i++;
  REQUIRE(i == op.ranges().end());
}

TEST_CASE("RangeSet clips range when beginning is unset", "[range_set]") {
  RangeSet op;
  op.set(1, 4, true);
  op.set(1, 2, false);
  for (int64 i = 1; i <= 4; i++)
    REQUIRE(op[i] == (i > 2));
  auto i = op.ranges().begin();
  REQUIRE(i->first == 3);
  REQUIRE(i->second == 4);
  i++;
  REQUIRE(i == op.ranges().end());
}

TEST_CASE("RangeSet clips range when ending is unset", "[range_set]") {
  RangeSet op;
  op.set(1, 4, true);
  op.set(3, 5, false);
  for (int64 i = 1; i <= 4; i++)
    REQUIRE(op[i] == (i < 3));
  auto i = op.ranges().begin();
  REQUIRE(i->first == 1);
  REQUIRE(i->second == 2);
  i++;
  REQUIRE(i == op.ranges().end());
}

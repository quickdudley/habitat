#include "EBT.h"
#include <catch2/catch_all.hpp>

using namespace ebt;

TEST_CASE("Correctly decodes EBT note values", "[EBT]") {
  Note note = decodeNote(-1);
  REQUIRE(note.replicate == false);
  REQUIRE(note.receive == false);
  REQUIRE(note.sequence == 0);
  note = decodeNote(0);
  REQUIRE(note.replicate == true);
  REQUIRE(note.receive == true);
  REQUIRE(note.sequence == 0);
  note = decodeNote(1);
  REQUIRE(note.replicate == true);
  REQUIRE(note.receive == false);
  REQUIRE(note.sequence == 0);
  note = decodeNote(2);
  REQUIRE(note.replicate == true);
  REQUIRE(note.receive == true);
  REQUIRE(note.sequence == 1);
  note = decodeNote(3);
  REQUIRE(note.replicate == true);
  REQUIRE(note.receive == false);
  REQUIRE(note.sequence == 1);
}
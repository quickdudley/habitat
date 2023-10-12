#include "EBT.h"
#include <catch2/catch_all.hpp>

using namespace ebt;

TEST_CASE("Correctly decodes EBT note values", "[EBT]") {
  Note note;
#define EX(nv, rep, rcv, sq)                                                   \
  note = decodeNote(nv);                                                       \
  REQUIRE(note.replicate == rep);                                              \
  REQUIRE(note.receive == rcv);                                                \
  REQUIRE(note.sequence == sq);                                                \
  REQUIRE(encodeNote(note) == nv)
  EX(-1, false, false, 0);
  EX(0, true, true, 0);
  EX(1, true, false, 0);
  EX(2, true, true, 1);
  EX(3, true, false, 1);
#undef EX
}
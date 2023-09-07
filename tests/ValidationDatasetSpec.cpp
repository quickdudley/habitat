#include "BJSON.h"
#include "SignJSON.h"
#include <File.h>
#include <catch2/catch_all.hpp>

// The dataset used here is from
// https://github.com/fraction/ssb-validation-dataset

TEST_CASE("Validation matches examples", "") {
  REQUIRE(sodium_init() >= 0);
  BMessage examples;

  {
    BFile input("tests/data.json", B_READ_ONLY);
    REQUIRE(JSON::parse(new JSON::BMessageDocSink(&examples), &input) == B_OK);
  }
  examples.PrintToStream();
  REQUIRE(false);
}

#include "BJSON.h"
#include "Post.h"
#include "SignJSON.h"
#include <File.h>
#include <catch2/catch_all.hpp>
#include <iostream>

// The dataset used here is from
// https://github.com/fraction/ssb-validation-dataset

static inline BString showNumber(int n) {
  BString result;
  result << n;
  return result;
}

TEST_CASE("Validation matches examples", "") {
  REQUIRE(sodium_init() >= 0);
  BMessage examples;
  {
    BFile input("tests/data.json", B_READ_ONLY);
    REQUIRE(JSON::parse(new JSON::BMessageDocSink(&examples), &input) == B_OK);
  }
  int i = 0;
  BMessage sample;
  while (examples.FindMessage(showNumber(i).String(), &sample) == B_OK) {
	DYNAMIC_SECTION("Example " << i) {
	  std::cout << "-- Example " << i << " --" << std::endl;
	  sample.PrintToStream();
	  
	}
	i++;
  }
}

#include "BJSON.h"
#include "Post.h"
#include "SignJSON.h"
#include <File.h>
#include <catch2/catch_all.hpp>

// The dataset used here is from
// https://github.com/fraction/ssb-validation-dataset

static inline BString showNumber(int n) {
  BString result;
  result << n;
  return result;
}

TEST_CASE("Validation matches examples", "[message][validation]") {
  REQUIRE(sodium_init() >= 0);
  BMessage examples;
  {
    BFile input("tests/data.json", B_READ_ONLY);
    REQUIRE(JSON::parse(std::make_unique<JSON::BMessageDocSink>(&examples),
                        &input) == B_OK);
  }
  int i = 0;
  BMessage sample;
  while (examples.FindMessage(showNumber(i).String(), &sample) == B_OK) {
    DYNAMIC_SECTION("Example " << i) {
      bool valid;
      REQUIRE(sample.FindBool("valid", &valid) == B_OK);
      bool foundValid;
      double lastSequence = -1;
      BString lastID;
      BString hmacKey;
      bool useHMac = false;
      if (sample.FindString("hmacKey", &hmacKey) == B_OK) {
        useHMac = true;
      }
      BString expectedID;
      sample.FindString("id", &expectedID);
      BMessage state;
      if (sample.FindMessage("state", &state) == B_OK) {
        REQUIRE(state.FindDouble("sequence", &lastSequence) == B_OK);
        REQUIRE(state.FindString("id", &lastID) == B_OK);
      }
      BMessage message;
      if (sample.FindMessage("message", &message) == B_OK) {
        foundValid = post::validate(&message, lastSequence, lastID, useHMac,
                                    hmacKey) == B_OK;
      } else {
        foundValid = false;
      }
      if (valid != foundValid)
        sample.PrintToStream();
      REQUIRE(valid == foundValid);
    }
    i++;
  }
}

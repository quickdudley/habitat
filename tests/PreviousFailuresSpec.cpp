#include "BJSON.h"
#include "Base64.h"
#include "Post.h"
#include "SignJSON.h"
#include "failures.h"
#include <catch2/catch_all.hpp>

static inline BString showNumber(int n) {
  BString result;
  result << n;
  return result;
}

TEST_CASE("Validation failures found in the wild are fixed", "[message][validation][wild]") {
  BMessage examples;
  {
    status_t parseStatus = B_OK;
    {
      JSON::Parser parser(std::make_unique<JSON::BMessageDocSink>(&examples));
      for (int i = 0; parseStatus == B_OK &&
           i < tests_failures_json_len;
           i++) {
        parseStatus =
            parser.nextChar(tests_failures_json[i]);
      }
    }
    REQUIRE(parseStatus == B_OK);
  }
  int i = 0;
  BMessage sample;
  while (examples.FindMessage(showNumber(i).String(), &sample) == B_OK) {
    DYNAMIC_SECTION("Example " << i) {
      BString lastID;
      sample.FindString("previous", &lastID);
      double sequence;
      sample.FindDouble("sequence", &sequence);
      BString hmacKey;
      REQUIRE(
            post::validate(&sample, sequence - 1, lastID, false, hmacKey) ==
                B_OK);
    }
    i++;
  }
}

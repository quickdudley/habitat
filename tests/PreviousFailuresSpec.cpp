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

TEST_CASE("Validation failures found in the wild are fixed",
          "[message][validation][wild]") {
  BMessage examples;
  {
    status_t parseStatus = B_OK;
    {
      JSON::Parser parser(std::make_unique<JSON::BMessageDocSink>(&examples));
      for (int i = 0; parseStatus == B_OK && i < tests_failures_json_len; i++)
        parseStatus = parser.nextChar(tests_failures_json[i]);
    }
    REQUIRE(parseStatus == B_OK);
  }
  int i = 0;
  BMessage sample;
  while (examples.FindMessage(showNumber(i).String(), &sample) == B_OK) {
    DYNAMIC_SECTION("Example " << i) {
      if (i == 8)
        SKIP("Old test message, not known for certain to be valid.");
      BString lastID;
      sample.FindString("previous", &lastID);
      double sequence;
      sample.FindDouble("sequence", &sequence);
      BString hmacKey;
      REQUIRE(post::validate(&sample, sequence - 1, lastID, false, hmacKey) ==
              B_OK);
    }
    i++;
  }
  SECTION("Known hash") {
    REQUIRE(examples.FindMessage("9", &sample) == B_OK);
    BString lastID;
    BString hmacKey;
    post::validate(&sample, 0, lastID, false, hmacKey);
    BString expected("%ZGEQGbV0AugQWONqVd0FsFrbAONGhEQlckP1D8h0LZY=.sha256");
    BString computed("%");
    unsigned char hash[crypto_hash_sha256_BYTES];
    {
      JSON::RootSink rootSink(std::make_unique<JSON::Hash>(hash));
      JSON::fromBMessage(&rootSink, &sample);
    }
    computed.Append(
        base64::encode(hash, crypto_hash_sha256_BYTES, base64::STANDARD));
    computed.Append(".sha256");
    REQUIRE(computed == expected);
  }
}

#include "BJSON.h"
#include "Base64.h"
#include "Post.h"
#include "SignJSON.h"
#include "ssb_validation_dataset.h"
#include <catch2/catch_all.hpp>

// The dataset used here is from
// https://github.com/fraction/ssb-validation-dataset

static inline BString showNumber(int n) {
  BString result;
  result << n;
  return result;
}

TEST_CASE("Validation matches examples", "[message][validation]") {
  BMessage examples;
  {
    status_t parseStatus = B_OK;
    {
      JSON::Parser parser(std::make_unique<JSON::BMessageDocSink>(&examples));
      for (int i = 0; parseStatus == B_OK &&
           i < generated_test_ssb_validation_dataset_json_len;
           i++) {
        parseStatus =
            parser.nextChar(generated_test_ssb_validation_dataset_json[i]);
      }
    }
    REQUIRE(parseStatus == B_OK);
  }
  int i = 0;
  BMessage sample;
  while (examples.FindMessage(showNumber(i).String(), &sample) == B_OK) {
    DYNAMIC_SECTION("Example " << i) {
      bool valid;
      REQUIRE(sample.FindBool("valid", &valid) == B_OK);
      bool foundValid = true;
      double lastSequence = -1;
      BString lastID;
      BString hmacKey;
      bool useHMac = false;
      if (sample.FindString("hmacKey", &hmacKey) == B_OK) {
        useHMac = true;
        if (!base64::isCanonical(hmacKey))
          foundValid = false;
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
        {
          unsigned char hash[crypto_hash_sha256_BYTES];
          JSON::RootSink rootSink(std::make_unique<JSON::Hash>(hash));
          JSON::fromBMessage(&rootSink, &message);
          rootSink.closeNode();
          BString computedID("%");
          computedID.Append(
              base64::encode(hash, crypto_hash_sha256_BYTES, base64::STANDARD));
          computedID.Append(".sha256");
          foundValid = foundValid && computedID == expectedID;
        }
        foundValid = foundValid &&
            post::validate(&message, lastSequence, lastID, useHMac, hmacKey) ==
                B_OK;
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

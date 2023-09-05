#include "BJSON.h"
#include "Base64.h"
#include "SignJSON.h"
#include <catch2/catch_all.hpp>

TEST_CASE("Correctly handles test message",
          "[JSON::Hash, JSON::VerifySignature]") {
  REQUIRE(sodium_init() >= 0);
  BMessage outer;
  outer.AddString("previous",
                  "%fkYNze1TmuWuQvpdowh7f+BR7nlrE8k3dVc3F1+21Nc=.sha256");
  outer.AddDouble("sequence", 7866.0);
  outer.AddString("author",
                  "@0W1ekqmNIN4e1IsAWBM1Kft6w5w86BMUdPO2M0SO83I=.ed25519");
  outer.AddDouble("timestamp", 1693523614894.0);
  outer.AddString("hash", "sha256");
  BMessage content;
  content.AddString("text",
                    "This post is to serve as a reference for testing message "
                    "hashing in Habitat. It includes the 🀐 character to ensure "
                    "all the edge cases are covered.");
  content.AddString("type", "post");
  content.AddString("root",
                    "%7ktVW113fuZBHkzhPRq0vteBb1gk0H+yL0MXeD90zXU=.sha256");
  BMessage mentions('JSAR');
  content.AddMessage("mentions", &mentions);
  content.AddString("branch",
                    "%7ktVW113fuZBHkzhPRq0vteBb1gk0H+yL0MXeD90zXU=.sha256");
  content.AddString("fork",
                    "%R17AcZh5Ab8LlPmVpnThpSa6qYSoGyc8ufQ6BOAZBGg=.sha256");
  outer.AddMessage("content", &content);
  outer.AddString("signature",
                  "H9U8Q2HbePwtNhEPOI1SLLRH99uQf9dQqTA3JqWJAGSM/nC/"
                  "zIJFkJZ8MjgFev/We/rR/0g4jafdjX7oHu9fDw==.sig.ed25519");
  SECTION("Same message ID as in Manyverse") {
    unsigned char rawHash[crypto_hash_sha256_BYTES];
    {
      JSON::RootSink rootSink(new JSON::Hash(rawHash));
      BString blank;
      rootSink.beginObject(blank);
      JSON::fromBMessage(&rootSink, &outer);
      rootSink.closeNode();
    }
    BString hash("%");
    hash.Append(
        base64::encode(rawHash, crypto_hash_sha256_BYTES, base64::STANDARD));
    hash.Append(".sha256");
    REQUIRE(hash == "%JFOLfCUuZz0AFMvo0iN0J3/cWV0nRF6aDKrRS6Bxz8c=.sha256");
  }
  SECTION("Signature successfully validated") {
    bool valid;
    {
      JSON::RootSink rootSink(new JSON::VerifySignature(&valid));
      BString blank;
      rootSink.beginObject(blank);
      JSON::fromBMessage(&rootSink, &outer);
      rootSink.closeNode();
    }
    REQUIRE(valid == true);
  }
}

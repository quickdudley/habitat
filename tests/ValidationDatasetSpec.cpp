#include "BJSON.h"
#include "SignJSON.h"
#include <File.h>
#include <catch2/catch_all.hpp>

// The dataset used here is from
// https://github.com/fraction/ssb-validation-dataset

TEST_CASE("Validation matches examples",
          "[JSON::Hash][JSON::VerifySignature][other]") {
  REQUIRE(sodium_init() >= 0);
  BMessage examples;
  {
  	JSON::Parser parser(new JSON::BMessageDocSink(&examples));
  	BFile input("tests/data.json", B_READ_ONLY);
  	char buffer[1024];
  	ssize_t readBytes;
  	while(input.Read(buffer, sizeof(buffer)) > 0) {
  	  for(int i = 0; i < readBytes; i++) {
  	  	parser.nextChar(buffer[i]);
  	  }
  	}
  }
  examples.PrintToStream();
  REQUIRE(false);
}


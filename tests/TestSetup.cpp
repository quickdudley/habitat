#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <sodium.h>
#include <stdexcept>

class TestSetup : public Catch::EventListenerBase {
public:
  using Catch::EventListenerBase::EventListenerBase;

  void testRunStarting(Catch::TestRunInfo const &) override {
    if (sodium_init() < 0)
      throw std::runtime_error("Failed to initialize libsodium");
  }
};

CATCH_REGISTER_LISTENER(TestSetup)

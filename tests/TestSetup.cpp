#include <Application.h>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <sodium.h>
#include <stdexcept>

static BApplication *testApp = NULL;
static thread_id testAppThread;

static int32 runApp(void *ignored) {
  testApp = new BApplication("application/x-vnd.habitat-test-suite");
  testApp->Run();
  delete testApp;
  return 0;
}

class TestSetup : public Catch::EventListenerBase {
public:
  using Catch::EventListenerBase::EventListenerBase;

  void testRunStarting(Catch::TestRunInfo const &) override {
    if (sodium_init() < 0)
      throw std::runtime_error("Failed to initialize libsodium");
    testAppThread = spawn_thread(runApp, "Test BApplication thread",
                                 B_NORMAL_PRIORITY, NULL);
    resume_thread(testAppThread);
  }

  void testRunEnded(Catch::TestRunStats const &) override {
    if (testApp != NULL) {
      testApp->Lock();
      testApp->Quit();
      testApp->Unlock();
      status_t exitValue;
      wait_for_thread(testAppThread, &exitValue);
    }
  }
};

CATCH_REGISTER_LISTENER(TestSetup)

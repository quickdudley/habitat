#include "Room.h"

namespace rooms2 {
namespace {
class MetadataHook : public muxrpc::ConnectionHook {
public:
  void call(muxrpc::Connection *rpc) override;
};

void MetadataHook::call(muxrpc::Connection *rpc) {}
} // namespace

void installClient(muxrpc::MethodSuite *suite) {
  suite->registerConnectionHook(std::make_shared<MetadataHook>());
}
} // namespace rooms2

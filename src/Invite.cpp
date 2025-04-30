#include "Invite.h"
#include "Base64.h"
#include <StringList.h>
#include <sodium.h>

namespace invite {

status_t parse(BMessage *result, const BString &input) {
  BStringList outermost;
  input.Split("~", false, outermost);
  if (outermost.CountStrings() < 2)
    return B_BAD_DATA;
  BString routeString = outermost.StringAt(0);
  BString keyString = outermost.StringAt(1);
  BStringList route;
  BStringList key;
  routeString.Split(":", false, route);
  keyString.Split(":", false, key);
  if (route.StringAt(0) != "net")
    return B_UNSUPPORTED;
  if (key.StringAt(0) != "shs")
    return B_UNSUPPORTED;
  if (route.CountStrings() != 3)
    return B_BAD_DATA;
  if (base64::decode(key.StringAt(1)).size() !=
      crypto_sign_ed25519_PUBLICKEYBYTES) {
    return B_BAD_DATA;
  }
  BString hostname = route.StringAt(1) << ":" << route.StringAt(2);
  BString cypherkey = BString("@") << key.StringAt(1) << ".ed25519";
  result->AddString("hostname", hostname);
  result->AddString("cypherkey", cypherkey);
  return B_OK;
}
} // namespace invite

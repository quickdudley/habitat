#ifndef INVITE_H
#define INVITE_H

#include <Message.h>
#include <String.h>

namespace invite {
status_t parse(BMessage *result, const BString &input);
}

#endif

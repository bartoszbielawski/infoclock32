#pragma once
// Host shim: TLS-flavoured client — identical to the plain one (curl carries
// the TLS in the HTTP shim). Exists so http_utils' __has_include() takes the
// HTTPS-supported path, like on the device.
#include "WiFiClient.h"

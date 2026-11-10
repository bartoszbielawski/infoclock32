#ifndef HTTP_UTILS_HPP
#define HTTP_UTILS_HPP

#include <Arduino.h>
#include <vector>
#include <utility>
#include <functional>

namespace HttpUtils
{

int httpGet(const String &url, String &outBody, bool insecure = true,
            const std::vector<std::pair<String, String>> &headers = {});

// Like httpGet, but a 200 body is handed to `onBody` as a stream instead of
// being buffered in one String. Use it for large responses: getString() needs
// the whole body in one contiguous heap block and silently returns "" when the
// heap is too fragmented for it (seen with the ~21 KB Novae menu). The
// request uses HTTP/1.0, so the stream is the plain body (never chunked).
int httpGetStream(const String &url, const std::function<void(Stream &)> &onBody,
                  bool insecure = true,
                  const std::vector<std::pair<String, String>> &headers = {});

}
#endif // HTTP_UTILS_HPP
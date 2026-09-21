#ifndef HTTP_UTILS_HPP
#define HTTP_UTILS_HPP

#include <Arduino.h>
#include <vector>
#include <utility>

namespace HttpUtils
{

int httpGet(const String &url, String &outBody, bool insecure = true,
            const std::vector<std::pair<String, String>> &headers = {});

}
#endif // HTTP_UTILS_HPP
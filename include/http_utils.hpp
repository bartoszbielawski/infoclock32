#ifndef HTTP_UTILS_HPP
#define HTTP_UTILS_HPP

#include <Arduino.h>

namespace HttpUtils 
{
    
int httpGet(const String &url, String &outBody, bool insecure = true);

}
#endif // HTTP_UTILS_HPP
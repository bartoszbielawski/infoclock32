// Canned responses for --offline mode: deterministic bodies keyed by URL
// patterns, shaped exactly like the real APIs (OWM JSON, LHC RSS HTML,
// Novae menu JSON).
#include <WiFiClient.h>

#include <cstring>
#include <string>

namespace
{
const char kOwmWeather[] =
    R"({"main":{"temp":21.4},"weather":[{"id":800,"icon":"01d"}]})";

const char kOwmForecast[] =
    R"({"city":{"name":"Prototype"},"list":[
        {"main":{"temp":19.2}},
        {"main":{"temp":20.5}},
        {"main":{"temp":18.7},"weather":[{"description":"light rain"}]}
    ]})";

// The LHC parser reads <title>Key: Value</title> lines.
const char kLhcRss[] =
    "<rss><channel>\n"
    "<title>LhcMachineMode: STABLE</title>\n"
    "<title>LhcBeamMode: PROTON PHYSICS</title>\n"
    "<title>BeamEnergy: 6800</title>\n"
    "<title>LhcPage1: Beam 1: 6800 GeV, Beam 2: 6800 GeV</title>\n"
    "</channel></rss>\n";

// Novae API: array of menu items, midi service only.
const char kNovaeMenu[] =
    R"([
      {"model":{"service":"midi"},"title":{"en":"Pasta arrabiata\nside salad","fr":"Pates arrabiata"}},
      {"model":{"service":"soir"},"title":{"en":"Evening dish","fr":"Plat du soir"}},
      {"model":{"service":"midi"},"title":{"en":"Grilled chicken\nrice"}}
    ])";
}  // namespace

int host_http_canned(const char* url, String& outBody)
{
    if (strstr(url, "/data/2.5/weather"))
        outBody = String(kOwmWeather);
    else if (strstr(url, "/data/2.5/forecast"))
        outBody = String(kOwmForecast);
    else if (strstr(url, "rss.xml") || strstr(url, "alicedcs"))
        outBody = String(kLhcRss);
    else if (strstr(url, "mynovae"))
        outBody = String(kNovaeMenu);
    else
        return 404;
    return 200;
}

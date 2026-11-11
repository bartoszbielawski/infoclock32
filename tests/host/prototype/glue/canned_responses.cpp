// Canned responses for --offline mode: deterministic bodies keyed by URL
// patterns, shaped exactly like the real APIs (OWM JSON, LHC Page 1 JSON,
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

// LHC Page 1 /history?channels=status,comments (webfd.app.cern.ch). Two
// entries per channel: the task must use the last (latest) one.
const char kLhcPage1[] =
    R"({"channels":{"status":[
        {"_id":1,"mode_disp":"SETUP","state_disp":"NO BEAM","energy_gev":0.0,"fill_number":9999.0},
        {"_id":2,"mode_disp":"PROTON PHYSICS","state_disp":"STABLE BEAMS","energy_gev":6799.6,"fill_number":10000.0,"beam_stable":true}],
      "comments":[
        {"_id":1,"comment":"old comment"},
        {"_id":3,"comment":"<center>  <br>Stable beams&nbsp;&#8211; fill 10000<br>Let&#8217;s go!<br></center>"}]},
      "current_id":4677,"total_events":4})";

// Novae API: array of menu items, midi service only. Long enough that a
// real-world menu joined into one string outlasts the display's minimum-hold
// slice (display_min_hold_s) — that is the truncation this fixture guards.
const char kNovaeMenu[] =
    R"([
      {"model":{"service":"midi"},"title":{"en":"**MENU TABLE DU CHEF CE VENDREDI**"}},
      {"model":{"service":"midi"},"title":{"en":"Roasted chicken with rosemary potatoes\nmixed green salad","fr":"Poulet rôti avec pommes de terre romarin"}},
      {"model":{"service":"soir"},"title":{"en":"Evening dish","fr":"Plat du soir"}},
      {"model":{"service":"midi","price":{"chf":12.5},"allergens":["milk","celery"]},"title":{"en":"Mushroom risotto with parmesan and grilled zucchini"},"image":{"url":"https://example.invalid/r.jpg","w":640,"h":480},"nutrition":{"kcal":640,"protein":22}},
      {"model":{"service":"midi"},"title":{"en":"Baked cod filet with lemon butter sauce\nseasonal vegetables"}},
      {"model":{"service":"midi"},"title":{"en":"Vegetarian lasagna with tomato basil sauce"}},
      {"model":{"service":"midi"},"title":{"en":"Pasta arrabiata\nside salad","fr":"Pates arrabiata"}}
    ])";
}  // namespace

int host_http_canned(const char* url, String& outBody)
{
    if (strstr(url, "/data/2.5/weather"))
        outBody = String(kOwmWeather);
    else if (strstr(url, "/data/2.5/forecast"))
        outBody = String(kOwmForecast);
    else if (strstr(url, "webfd.app.cern.ch"))
        outBody = String(kLhcPage1);
    else if (strstr(url, "mynovae"))
        outBody = String(kNovaeMenu);
    else
        return 404;
    return 200;
}

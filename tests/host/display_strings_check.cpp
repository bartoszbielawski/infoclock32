// Host-side tests for include/display_strings.hpp.
#include <display_strings.hpp>
#include <cstdio>
#include <cstring>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static void check_fold(const char* in, const char* want)
{
    std::string got = ascii_fold(in);
    CHECK(got == want, "fold '%s' -> '%s' (want '%s')", in, got.c_str(), want);
}

int main()
{
    // parse_lang: all seven codes, unknown/empty/wrong case fall back to English
    const char* codes[] = {"en","fr","pl","it","es","pt","de"};
    Lang langs[] = {Lang::En,Lang::Fr,Lang::Pl,Lang::It,Lang::Es,Lang::Pt,Lang::De};
    for (int i = 0; i < 7; i++) {
        CHECK(parse_lang(codes[i]) == langs[i], "parse_lang(%s)", codes[i]);
        CHECK(strcmp(owm_lang(langs[i]), codes[i]) == 0, "owm_lang(%s)", codes[i]);
    }
    CHECK(parse_lang("") == Lang::En && parse_lang("xx") == Lang::En && parse_lang("DE") == Lang::En,
          "unknown codes fall back to en");

    // every string is present, ASCII-only and fits the clock's date layout
    for (Lang l : langs) {
        const DisplayStrings& s = display_strings(l);
        const char* all[] = {s.sunrise, s.sunset, s.polar_day, s.polar_night,
                             s.rebooting, s.ap_connect, s.ap_browse};
        for (const char* t : all) {
            CHECK(t && *t, "%s: empty string", owm_lang(l));
            for (const char* p = t; p && *p; p++)
                CHECK((unsigned char)*p < 0x80, "%s: non-ASCII in '%s'", owm_lang(l), t);
        }
        for (int d = 0; d < 7; d++) {
            CHECK(s.days[d] && strlen(s.days[d]) >= 2 && strlen(s.days[d]) <= 3,
                  "%s: day %d '%s'", owm_lang(l), d, s.days[d]);
            for (int e = d + 1; e < 7; e++)
                CHECK(strcmp(s.days[d], s.days[e]) != 0, "%s: duplicate day name %s",
                      owm_lang(l), s.days[d]);
        }
    }
    CHECK(strcmp(display_strings(Lang::En).days[0], "Sun") == 0, "en Sunday");
    CHECK(strcmp(display_strings(Lang::De).days[1], "Mo") == 0, "de Monday");
    CHECK(strcmp(display_strings(Lang::Pl).days[3], "Sro") == 0, "pl Wednesday unchanged");

    // ascii_fold
    check_fold("plain ASCII", "plain ASCII");
    check_fold("", "");
    check_fold("\xC3\xA9t\xC3\xA9 \xC3\xA7" "a", "ete ca");
    check_fold("Gr\xC3\xBC\xC3\x9F" "e \xC3\x84rger", "Grusse Arger");
    check_fold("\xC3\x9F", "ss");
    check_fold("\xC3\xB1" "o \xC3\x91", "no N");
    check_fold("\xC3\xA3o \xC3\xB5" "es", "ao oes");
    check_fold("\xC3\xA6 \xC3\xB8", "ae o");
    check_fold("\xC5\x82\xC3\xB3" "d\xC5\xBA \xC5\x9A" "wi\xC4\x99" "ty", "lodz Swiety");
    check_fold("\xC4\x85\xC4\x87\xC5\x84\xC5\xBC", "acnz");
    check_fold("\xC5\x93uf", "oeuf");
    check_fold("l\xE2\x80\x99" "eau", "l'eau");
    check_fold("\xC3", "\xC3");
    check_fold("\xE2\x82\xAC", "\xE2\x82\xAC");

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("display_strings_check passed\n");
    return 0;
}

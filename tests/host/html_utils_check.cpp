// Host-side tests for include/html_utils.hpp.
#include <html_utils.hpp>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

int main()
{
    CHECK(escapeHtml("") == "", "empty");
    CHECK(escapeHtml("Home WiFi 5G") == "Home WiFi 5G", "plain text untouched");
    CHECK(escapeHtml("caf\xc3\xa9") == "caf\xc3\xa9", "UTF-8 bytes untouched");

    // The SSID that would break out of onclick="pick('...')"
    CHECK(escapeHtml("');alert(1);//") == "&#39;);alert(1);//", "apostrophe escaped");
    CHECK(escapeHtml("a\"b") == "a&quot;b", "double quote escaped");
    CHECK(escapeHtml("<script>x</script>") == "&lt;script&gt;x&lt;/script&gt;", "tags escaped");
    CHECK(escapeHtml("AT&T") == "AT&amp;T", "ampersand escaped");
    // '&' must be handled first/once: no double escaping of the entities we emit
    CHECK(escapeHtml("&lt;") == "&amp;lt;", "existing entity text is escaped, not trusted");

    // Result never contains a raw metacharacter
    std::string nasty = "\"'<>&\"'<>&";
    std::string e = escapeHtml(nasty);
    CHECK(e.find_first_of("\"'<>") == std::string::npos, "no raw metacharacters: %s", e.c_str());

    if (failures == 0) printf("html_utils: all checks passed\n");
    return failures ? 1 : 0;
}

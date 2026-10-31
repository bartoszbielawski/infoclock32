// Host-side tests for include/config_parse.hpp.
#include <config_parse.hpp>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

// Mirrors DataStore::load_from_file(): the result replaces the map wholesale.
static std::string get(const std::map<std::string, std::string>& m, const std::string& k)
{
    auto it = m.find(k);
    return it == m.end() ? "<default>" : it->second;
}

int main()
{
    auto a = parseConfigText("hostname=clock\nbrightness=7\n# comment\n\nmsg1_text=a=b\n");
    CHECK(a.size() == 3, "three keys, got %zu", a.size());
    CHECK(get(a, "hostname") == "clock", "plain key");
    CHECK(get(a, "msg1_text") == "a=b", "'=' inside a value kept");

    // The bug this replaces: a key deleted from the file must be gone after a reload
    auto b = parseConfigText("hostname=clock\n");
    CHECK(get(b, "brightness") == "<default>", "removed key falls back to default");
    CHECK(get(b, "hostname") == "clock", "remaining key still there");

    // CRLF files
    auto c = parseConfigText("a=1\r\nb=2\r\n");
    CHECK(get(c, "a") == "1" && get(c, "b") == "2", "CRLF stripped");

    // No trailing newline, blank file, junk lines
    CHECK(get(parseConfigText("x=1"), "x") == "1", "last line without newline");
    CHECK(parseConfigText("").empty(), "empty text");
    CHECK(parseConfigText("novalue\n#k=v\n").empty(), "no '=' and comments ignored");
    CHECK(get(parseConfigText("k=\n"), "k") == "", "empty value is kept as empty");

    // Over-long lines are dropped whole and never produce a second key
    std::string longVal(200, 'x');
    std::vector<std::string> skipped;
    auto d = parseConfigText("ok=1\nlong=" + longVal + "\nweb_password=oops\n", &skipped);
    CHECK(get(d, "long") == "<default>", "long line ignored");
    CHECK(get(d, "ok") == "1" && get(d, "web_password") == "oops", "neighbours unaffected");
    CHECK(skipped.size() == 1, "reported one long line, got %zu", skipped.size());
    std::string edge = "k=" + std::string(CONFIG_MAX_LINE - 2, 'y');
    CHECK(get(parseConfigText(edge + "\n"), "k").size() == CONFIG_MAX_LINE - 2, "line at the limit is kept");
    CHECK(get(parseConfigText(edge + "z\n"), "k") == "<default>", "one past the limit is dropped");

    if (failures == 0) printf("config_parse: all checks passed\n");
    return failures ? 1 : 0;
}

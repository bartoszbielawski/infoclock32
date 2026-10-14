// Host-side tests for include/message_utils.hpp.
#include <message_utils.hpp>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

int main()
{
    CHECK(clampMessage("hello") == "hello", "short unchanged");
    CHECK(clampMessage("") == "", "empty unchanged");
    CHECK(clampMessage(std::string(256, 'a')).size() == 256, "exactly the limit is kept");
    CHECK(clampMessage(std::string(257, 'a')).size() == 256, "one over is cut");
    CHECK(clampMessage(std::string(100000, 'a')).size() == 256, "huge is cut to the limit");
    CHECK(clampMessage("abcdef", 3) == "abc", "custom limit");

    // UTF-8: "é" is 2 bytes (C3 A9); never leave a lone lead byte
    std::string e = "a\xC3\xA9";                 // a é  (3 bytes)
    CHECK(clampMessage(e, 2) == "a", "cut inside é backs off to 'a'");
    CHECK(clampMessage(e, 3) == e, "whole é kept");
    std::string euro = "ab\xE2\x82\xAC";         // a b € (5 bytes)
    CHECK(clampMessage(euro, 3) == "ab", "cut inside 3-byte char backs off");
    CHECK(clampMessage(euro, 4) == "ab", "still inside the 3-byte char");
    CHECK(clampMessage(euro, 5) == euro, "whole char kept");

    // a 256-char message must fit int16 canvas width at 6 px per byte
    CHECK(kMaxDisplayMessage * 6 < 32767, "canvas width stays inside int16");

    if (failures) { printf("%d message util check(s) FAILED\n", failures); return 1; }
    printf("all message util checks passed\n");
    return 0;
}

// Host-side tests for include/scroll_timing.hpp.
#include <scroll_timing.hpp>
#include <cstdio>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

int main()
{
    // Fits the 64 px display (<= 10 chars): centered for 100*speed.
    CHECK(scroll_duration_ms(10, 30) == 3000, "fit: %u", scroll_duration_ms(10, 30));
    CHECK(scroll_duration_ms(0, 30) == 3000, "empty: %u", scroll_duration_ms(0, 30));

    // 11 chars = 66 px: last = 3, frames = 4 -> (110 + 4) * 30.
    CHECK(scroll_duration_ms(11, 30) == 114 * 30, "11ch: %u", scroll_duration_ms(11, 30));

    // 40 chars = 240 px: last = 177, frames = 178 -> 288 * 30 = 8640 ms.
    CHECK(scroll_duration_ms(40, 30) == 8640, "40ch: %u", scroll_duration_ms(40, 30));

    // Linear in speed.
    CHECK(scroll_duration_ms(40, 10) * 3 == scroll_duration_ms(40, 30), "linear");

    // steps = 2 halves the frame count (rounded up).
    CHECK(scroll_duration_ms(40, 30, 64, 2) == (110 + 90) * 30, "steps2: %u",
          scroll_duration_ms(40, 30, 64, 2));

    // Guard rails.
    CHECK(scroll_duration_ms(40, -5) == 0, "negative speed");
    CHECK(scroll_duration_ms(40, 30, 64, 0) == scroll_duration_ms(40, 30), "steps 0 -> 1");

    // Slice: floor, margin, cap.
    CHECK(scroll_slice_ms(3000, 10000) == 10000, "floor");
    CHECK(scroll_slice_ms(12000, 10000) == 13000, "margin");
    CHECK(scroll_slice_ms(60000, 10000) == 30000, "cap");
    CHECK(scroll_slice_ms(60000, 40000) == 40000, "floor above cap wins");
    CHECK(scroll_slice_ms(0, 0) == 1000, "zero floor keeps margin");

    if (failures) { printf("scroll_timing_check: %d failure(s)\n", failures); return 1; }
    printf("scroll_timing_check: all passed\n");
    return 0;
}

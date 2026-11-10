// Host-side tests for include/tick_utils.hpp.
#include <tick_utils.hpp>
#include <cstdio>
#include <initializer_list>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

// Run a clock face that takes `drawUs` per frame, starting at `startUs`, and
// count displayed seconds that differ from the previous frame's by != 1.
static int irregularFrames(int64_t startUs, int64_t drawUs, int frames)
{
    int64_t t = startUs;
    int64_t lastShown = -1;
    int bad = 0;
    for (int i = 0; i < frames; i++)
    {
        const int64_t frameStart = t;
        const int64_t shown = frameStart / 1000000;      // the face reads the time first
        if (lastShown >= 0 && shown - lastShown != 1) bad++;
        lastShown = shown;
        t += drawUs;                                     // draw
        t += (int64_t)msToNextFrame(frameStart, t) * 1000;  // sleep
    }
    return bad;
}

int main()
{
    // basic arithmetic (10 ms margin)
    CHECK(msToNextFrame(5000000, 5000000) == 1010, "frame at the boundary (%u)", msToNextFrame(5000000, 5000000));
    CHECK(msToNextFrame(5500000, 5540000) == 470, "half way, 40 ms drawing (%u)", msToNextFrame(5500000, 5540000));
    CHECK(msToNextFrame(5990000, 6030000) == 0, "drawing crossed the boundary: no sleep (%u)", msToNextFrame(5990000, 6030000));
    CHECK(msToNextFrame(5990000, 6005000) == 5, "late frame wakes 10 ms into the next second (%u)", msToNextFrame(5990000, 6005000));

    // no skipped or repeated second, wherever the first frame starts and
    // however long drawing takes (up to most of a second)
    for (int64_t start : {3000000LL, 3500000LL, 3990000LL, 3999999LL})
        for (int64_t draw : {1000LL, 40000LL, 300000LL, 900000LL})
            CHECK(irregularFrames(start, draw, 600) == 0,
                  "start %lld us, draw %lld us: every frame shows the next second", (long long)start, (long long)draw);
    // the old flat 1000 ms sleep did skip seconds (sanity check of the harness)
    {
        int64_t t = 3000000, last = -1; int bad = 0;
        for (int i = 0; i < 600; i++) { int64_t s = t / 1000000; if (last >= 0 && s - last != 1) bad++; last = s; t += 40000 + 1000000; }
        CHECK(bad > 0, "harness detects the old drift (%d)", bad);
    }

    DeferredAction d;
    CHECK(!d.takeDue(100) && !d.pending(), "nothing pending at first");
    d.touch(100, 5); d.touch(101, 5); d.touch(103, 5);    // a slider burst
    CHECK(!d.takeDue(107), "not due while changes keep coming");
    CHECK(d.takeDue(108), "due 5 s after the last change");
    CHECK(!d.takeDue(200), "fires once");

    if (failures) { printf("%d tick util check(s) FAILED\n", failures); return 1; }
    printf("all tick util checks passed\n");
    return 0;
}

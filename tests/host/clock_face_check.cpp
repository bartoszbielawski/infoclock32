// Host-side tests for include/clock_face.hpp.
// Covers 7-seg digit masks, rendered 7-seg/BCD faces on a fake pixel grid,
// and the seconds-sweep mapping.
#include <clock_face.hpp>
#include <cstdio>
#include <cstring>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

// Fake display: 64x8 grid, LMDS-compatible setPixel.
struct FakeDisp
{
    bool px[64][8] = {};
    void setPixel(int x, int y, bool v)
    {
        if (x >= 0 && x < 64 && y >= 0 && y < 8) px[x][y] = v;
    }
    bool get(int x, int y) const { return px[x][y]; }
    void clear() { memset(px, 0, sizeof(px)); }
};

static int litCount(const FakeDisp& d)
{
    int n = 0;
    for (int x = 0; x < 64; x++)
        for (int y = 0; y < 8; y++)
            n += d.get(x, y);
    return n;
}

static void expect_row(int digit, int row, uint8_t want)
{
    CHECK(kBoldDigits[digit][row] == want, "bold[%d][%d]=%02X want %02X",
          digit, row, kBoldDigits[digit][row], want);
}

int main()
{
    // Bold digit table: every digit fully defined (8 non-empty rows) and a
    // few spot values
    for (int digit = 0; digit < 10; digit++)
        for (int row = 0; row < 8; row++)
            CHECK(kBoldDigits[digit][row] != 0, "bold[%d][%d] empty", digit, row);
    expect_row(0, 0, 0x1E);
    expect_row(0, 1, 0x3F);
    expect_row(1, 1, 0x1C);
    expect_row(8, 3, 0x1E);
    expect_row(8, 4, 0x33);
    expect_row(7, 0, 0x1E);
    expect_row(2, 7, 0x1E);

    // Rendered bold face at 21:07, origin centered (17, 0) on 64 px
    // (30 px face: digits at 17, 24, colon 31-32, digits 34, 41)
    {
        FakeDisp d;
        drawBoldTime(d, 21, 7, true, 17, 0);

        // '2' at x 17-22: bowl rows 0-1, diagonal to the lower left, full base
        CHECK(d.get(18, 0) && d.get(21, 0), "2: bowl top");
        CHECK(d.get(22, 1) && d.get(22, 2), "2: right side");
        CHECK(d.get(18, 5) && d.get(19, 5) && d.get(17, 6), "2: diagonal");
        CHECK(d.get(18, 7) && d.get(21, 7), "2: rounded foot");
        CHECK(!d.get(17, 7) && !d.get(22, 7), "2: foot corners cut");
        CHECK(!d.get(17, 2), "2: left side dark below bowl");

        // '1' at x 24-29: plain 2 px stem rows 0-7, left flag at row 1
        for (int y = 0; y <= 7; y++) CHECK(d.get(26, y) && d.get(27, y), "1: stem row %d", y);
        CHECK(d.get(25, 1), "1: flag");
        CHECK(!d.get(25, 0) && !d.get(25, 2) && !d.get(28, 4), "1: flag only on row 1");

        // Colon at x 31-32, 2x2 blocks on rows 2-3 and 5-6
        CHECK(d.get(31, 2) && d.get(32, 3) && d.get(31, 5) && d.get(32, 6), "colon on");
        CHECK(!d.get(31, 4) && !d.get(32, 0) && !d.get(31, 7), "colon no bleed");

        // '0' at x 34-39: rounded stadium, hollow center
        CHECK(d.get(35, 0) && d.get(38, 0), "0: rounded top");
        CHECK(d.get(35, 7) && d.get(38, 7), "0: rounded bottom");
        for (int y = 1; y <= 6; y++)
            CHECK(d.get(34, y) && d.get(35, y) && d.get(38, y) && d.get(39, y), "0: sides row %d", y);
        CHECK(!d.get(36, 3) && !d.get(37, 5), "0: hollow");

        // '7' at x 41-46: rounded top bar, diagonal into a straight stem
        CHECK(d.get(42, 0) && d.get(45, 0), "7: rounded top bar");
        CHECK(!d.get(41, 0) && !d.get(46, 0), "7: bar corners cut");
        CHECK(d.get(45, 1) && d.get(45, 3) && d.get(43, 6), "7: stem");
        CHECK(!d.get(41, 1) && !d.get(41, 7), "7: nothing below/after");

        CHECK(!d.get(63, 7), "face does not touch the WiFi pixel");

        // Every digit renders non-empty on the grid
        for (int digit = 0; digit < 10; digit++)
        {
            FakeDisp t;
            drawBoldTime(t, digit * 11, digit, true, 17, 0);
            CHECK(litCount(t) > 0, "digit %d renders", digit);
        }
    }

    // Colon off clears both blocks; midnight "00:00" renders four full zeros
    {
        FakeDisp d;
        drawBoldTime(d, 0, 0, false, 17, 0);
        CHECK(!d.get(31, 2) && !d.get(32, 6), "colon off");
        int zeros = litCount(d);
        CHECK(zeros == 4 * 36, "four zeros = 144 px, got %d", zeros);
    }

    // Bold face fits the display: 30 px + centered offset leaves margins
    {
        FakeDisp d;
        drawBoldTime(d, 88, 88, true, (64 - kBoldFaceWidth) / 2, 0);
        CHECK(litCount(d) == 4 * 32 + 8, "88:88 = 136 px, got %d", litCount(d));
        CHECK(!d.get(0, 0) && !d.get(63, 7), "within bounds");
    }

    // BCD face at 21:07: HT=1 (bit1), HU=1 (bit0), MT=0 (nothing), MU=7 (bits 0-2)
    {
        FakeDisp d;
        drawBCDTime(d, 21, 7, true, 25, 0);
        CHECK(d.get(25, 6), "bcd HT=2 -> bit1 (row 6)");
        CHECK(!d.get(25, 7), "bcd HT: no bit0");
        CHECK(d.get(28, 7), "bcd HU=1 -> bit0 (row 7)");
        CHECK(!d.get(28, 6) && !d.get(28, 5) && !d.get(28, 4), "bcd HU: only bit0");
        CHECK(d.get(31, 2) && d.get(31, 4), "bcd colon on");
        CHECK(!d.get(34, 7) && !d.get(34, 6) && !d.get(34, 5), "bcd MT=0 -> dark");
        CHECK(d.get(37, 7) && d.get(37, 6) && d.get(37, 5) && !d.get(37, 4), "bcd MU=7 -> bits 0-2");
    }

    // BCD bit table: value 9 with 4 bits -> bits 0 and 3
    {
        uint8_t m = bcdColumnBits(9, 4);
        CHECK(m == 0x09, "bcd(9,4)=%02X want 09", m);
        CHECK(bcdColumnBits(5, 3) == 0x05, "bcd(5,3)");
        CHECK(bcdColumnBits(7, 3) == 0x07, "bcd(7,3)");
        CHECK(bcdColumnBits(9, 3) == 0x01, "bcd(9,3) truncates to 3 bits");
    }

    // Sweep mapping: endpoints, monotonicity, clamping
    {
        CHECK(sweepX(0, 62) == 0, "sweep 0 -> left edge");
        CHECK(sweepX(59, 62) == 62, "sweep 59 -> maxX");
        CHECK(sweepX(30, 62) == 31, "sweep 30 -> mid");
        int last = -1;
        for (int s = 0; s < 60; s++)
        {
            int x = sweepX(s, 62);
            CHECK(x >= last && x <= 62, "sweep monotonic at %d", s);
            last = x;
        }
        CHECK(sweepX(-5, 62) == 0 && sweepX(120, 62) == 62, "sweep clamps");
    }

    if (failures == 0)
        printf("all clock face tests passed\n");
    return failures == 0 ? 0 : 1;
}

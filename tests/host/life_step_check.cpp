// Host-side tests for include/life.hpp (Conway B3/S23 with x-wrap, 8 rows).
#include <life.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

typedef std::vector<uint8_t> Grid;

static Grid make(int width, std::initializer_list<const char*> rows)
{
    Grid g((size_t)width * 8, 0);
    int y = 0;
    for (const char* r : rows) {
        for (int x = 0; x < width && r[x]; x++)
            g[y * width + x] = (r[x] == 'O') ? 1 : 0;
        y++;
    }
    return g;
}

static std::string str(const Grid& g, int width)
{
    std::string s;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < width; x++) s += g[y*width+x] ? 'O' : '.';
        if (y < 7) s += '/';
    }
    return s;
}

static Grid stepN(Grid g, int width, int n)
{
    Grid nxt(g.size());
    while (n--) { life_step(g.data(), nxt.data(), width); g = nxt; }
    return g;
}

int main()
{
    const int W = 12;

    // Block: 2x2 square is a still life.
    {
        Grid g = make(W, {"......", "......", "..OO..", "..OO..", "......", "......", "......", "......"});
        Grid nxt((size_t)W * 8);
        CHECK(life_step(g.data(), nxt.data(), W) == 4, "block pop 4");
        CHECK(str(nxt, W) == str(g, W), "block stable");
    }

    // Blinker: horizontal bar oscillates with period 2.
    {
        Grid h = make(W, {"......", "......", "..OOO.", "......", "......", "......", "......", "......"});
        Grid v = stepN(h, W, 1);
        Grid back = stepN(v, W, 1);
        CHECK(str(back, W) == str(h, W), "blinker period 2");
        CHECK(v[1*W+3] && v[2*W+3] && v[3*W+3] && !v[2*W+2] && !v[2*W+4], "blinker turns vertical");
    }

    // Glider: the classic 5-cell pattern translates (+1, +1) every 4 gens.
    {
        Grid glider = make(W, {"............", "............",
                               ".....O......", "......O.....", "....OOO.....",
                               "............", "............", "............"});
        int px = 5, py = 2;                       // (x,y) of the topmost cell
        int q = 0;
        for (int i = 0; i < W * 8; i++) if (glider[i]) q++;
        Grid moved = stepN(glider, W, 4);
        int mx = -1, my = -1, mq = 0;
        for (int y = 0; y < 8; y++) for (int x = 0; x < W; x++)
            if (moved[y*W+x]) { if (my < 0 || y < my || (y == my && x < mx)) { my = y; mx = x; } mq++; }
        CHECK(mq == q, "glider population stays 5, got %d", mq);
        CHECK(mx == px + 1 && my == py + 1, "glider moved (+1,+1) in 4 gens, got (%d,%d)", mx, my);
    }

    // Empty field stays empty (population 0).
    {
        Grid g((size_t)W * 8, 0);
        Grid nxt((size_t)W * 8, 1);
        CHECK(life_step(g.data(), nxt.data(), W) == 0, "empty -> 0");
    }

    // Horizontal wrap: a row-edge blinker keeps oscillating across the seam.
    {
        const int W2 = 6; // narrow to force wrap: cells at cols 5,0,1 in row 3
        Grid g = make(W2, {"......", "......", "......", "OO...O", "......", "......", "......", "......"});
        Grid back = stepN(g, W2, 2);
        CHECK(str(back, W2) == str(g, W2), "wrapped blinker period 2");
    }


    // ── LifeCycleDetector ───────────────────────────────────────────────────

    // Still life: the block repeats the previous state -> period 1.
    {
        Grid g = make(W, {"......", "......", "..OO..", "..OO..", "......", "......", "......", "......"});
        LifeCycleDetector d;
        CHECK(d.observe(g.data(), g.size()) == 0, "first state is never a repeat");
        Grid nxt((size_t)W * 8);
        life_step(g.data(), nxt.data(), W);
        CHECK(d.observe(nxt.data(), nxt.size()) == 1, "block reported as period 1");
    }

    // Blinker: back-to-back states differ, the state two gens back repeats.
    {
        Grid g = make(W, {"......", "......", "..OOO.", "......", "......", "......", "......", "......"});
        LifeCycleDetector d;
        d.observe(g.data(), g.size());
        Grid a = stepN(g, W, 1);
        CHECK(d.observe(a.data(), a.size()) == 0, "blinker gen 1 is new");
        Grid b = stepN(g, W, 2);
        CHECK(d.observe(b.data(), b.size()) == 2, "blinker reported as period 2");
    }

    // A travelling glider keeps producing new states for the whole window.
    {
        Grid g = make(W, {"............", "............",
                          ".....O......", "......O.....", "....OOO.....",
                          "............", "............", "............"});
        LifeCycleDetector d;
        d.observe(g.data(), g.size());
        bool flagged = false;
        Grid cur = g, nxt((size_t)W * 8);
        for (int i = 0; i < 6; i++) {          // 6 gens: glider still travelling
            life_step(cur.data(), nxt.data(), W);
            cur = nxt;
            if (d.observe(cur.data(), cur.size())) flagged = true;
        }
        CHECK(!flagged, "travelling glider is not reported as settled");
    }

    // reset() forgets the history, so a reseeded board starts clean.
    {
        Grid g = make(W, {"......", "......", "..OO..", "..OO..", "......", "......", "......", "......"});
        LifeCycleDetector d;
        d.observe(g.data(), g.size());
        CHECK(d.observe(g.data(), g.size()) == 1, "repeat seen before reset");
        d.reset();
        CHECK(d.observe(g.data(), g.size()) == 0, "history cleared by reset");
    }

    // The ring only remembers kLifeCycleWindow generations: a repeat older
    // than the window is not reported.
    {
        Grid a = make(W, {"......", "......", "..OO..", "..OO..", "......", "......", "......", "......"});
        Grid b = make(W, {"......", "......", "......", "......", "..OO..", "..OO..", "......", "......"});
        LifeCycleDetector d;
        d.observe(a.data(), a.size());
        for (int i = 0; i < kLifeCycleWindow; i++)   // push `a` out of the ring
            d.observe(b.data(), b.size());
        CHECK(d.observe(a.data(), a.size()) == 0, "repeat older than the window is ignored");
    }

    printf(failures ? "%d test(s) FAILED\n" : "all life tests passed\n", failures);
    return failures != 0;
}

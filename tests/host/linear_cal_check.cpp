// Host-side tests for include/linear_cal.hpp.
#include <linear_cal.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)
static bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main()
{
    const char* why = nullptr;

    // defaults: identity
    LinearCal d = makeLinearCal(NAN, NAN, &why);
    CHECK(!d.active() && !*why && applyLinearCal(d, 21.37f) == 21.37f, "unset = identity");

    // offset only, slope only, both
    CHECK(near(applyLinearCal(makeLinearCal(NAN, -0.3f), 21.0f), 20.7f), "offset only");
    CHECK(near(applyLinearCal(makeLinearCal(1.02f, NAN), 25.0f), 25.5f), "slope only");
    LinearCal c = makeLinearCal(0.98f, 0.2f, &why);
    CHECK(c.active() && !*why && near(applyLinearCal(c, 20.0f), 19.8f), "slope + offset");

    // the README recipe: two reference points -> coefficients that hit both
    LinearCal t;
    CHECK(linearCalFromTwoPoints(0.4f, 0.0f, 25.8f, 25.0f, t), "two points accepted");
    CHECK(near(applyLinearCal(t, 0.4f), 0.0f) && near(applyLinearCal(t, 25.8f), 25.0f),
          "derived slope/offset map both points (slope %.4f offset %.4f)", t.slope, t.offset);
    LinearCal tight;
    CHECK(!linearCalFromTwoPoints(20.0f, 20.0f, 20.5f, 21.0f, tight), "points < 1 degC apart refused");

    // typos are refused and leave readings unchanged
    LinearCal s = makeLinearCal(10.0f, 0.0f, &why);
    CHECK(!s.active() && strstr(why, "slope"), "slope 10 refused");
    LinearCal neg = makeLinearCal(-1.0f, 0.0f, &why);
    CHECK(!neg.active() && strstr(why, "slope"), "negative slope refused");
    LinearCal o = makeLinearCal(1.0f, 50.0f, &why);
    CHECK(!o.active() && strstr(why, "offset") && applyLinearCal(o, 21.0f) == 21.0f, "offset 50 refused");

    if (failures) { printf("%d linear cal check(s) FAILED\n", failures); return 1; }
    printf("all linear cal checks passed\n");
    return 0;
}

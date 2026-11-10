// Host-side tests for include/menu_filter.hpp.
#include <menu_filter.hpp>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

int main()
{
    // headers seen in the real R3 menu and variants
    CHECK(isMenuHeader("**MENU TABLE DU CHEF CE VENDREDI**"), "wrapped all-caps header");
    CHECK(isMenuHeader("MENU TABLE DU CHEF CE VENDREDI"), "all-caps header");
    CHECK(isMenuHeader("*Plat du jour*"), "wrapped mixed-case header");
    CHECK(isMenuHeader("  **SPECIAL**  "), "surrounding spaces");
    CHECK(isMenuHeader("VEGETARIEN"), "single all-caps word");

    // real dishes stay
    CHECK(!isMenuHeader("Beetroot gnocchi gratin"), "normal dish");
    CHECK(!isMenuHeader("Pike quenelles, Nantua sauce"), "dish with capitalised name");
    CHECK(!isMenuHeader("Rice"), "short dish");
    CHECK(!isMenuHeader("BBQ ribs"), "abbreviation inside a dish");
    CHECK(!isMenuHeader("BBQ"), "short all-caps name");
    CHECK(!isMenuHeader("Seared pheasant fillet, red fruit sauce with dark chocolate"), "long dish");
    CHECK(!isMenuHeader(""), "empty");
    CHECK(!isMenuHeader("   "), "blank");
    CHECK(!isMenuHeader("100% beef burger"), "digits and lowercase");

    if (failures) { printf("%d menu filter check(s) FAILED\n", failures); return 1; }
    printf("all menu filter checks passed\n");
    return 0;
}

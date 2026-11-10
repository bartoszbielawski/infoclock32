#pragma once

// Recognises section headers in restaurant menus (pure, host-tested in
// tests/host/menu_filter_check.cpp).
//
// The Novae API sometimes lists a heading as if it were a lunch dish, e.g.
// "**MENU TABLE DU CHEF CE VENDREDI**". Dishes are written in normal mixed
// case, so an entry is a header when it is wrapped in '*' or has letters but
// no lowercase ones. Short all-caps names (2-3 letters, like "BBQ") alone are
// not enough to count.

#include <cctype>
#include <string>

inline bool isMenuHeader(const std::string& dish)
{
    // trim spaces for the wrapper check
    size_t b = dish.find_first_not_of(' ');
    size_t e = dish.find_last_not_of(' ');
    if (b == std::string::npos) return false;
    if (dish[b] == '*' || dish[e] == '*') return true;

    int letters = 0, lower = 0;
    for (size_t i = b; i <= e; i++)
    {
        unsigned char c = (unsigned char)dish[i];
        if (std::isalpha(c)) { letters++; if (std::islower(c)) lower++; }
    }
    return letters >= 4 && lower == 0;
}

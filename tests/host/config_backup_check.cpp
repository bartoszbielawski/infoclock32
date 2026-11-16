// Host-side checks for include/config_backup.hpp (config download file name).
#include <config_backup.hpp>
#include <cstdio>
#include <cstdlib>

static int failures = 0;

#define CHECK_EQ(got, want) do { std::string g = (got); if (g != (want)) { \
    printf("FAIL: got '%s', want '%s'  [%s:%d]\n", g.c_str(), (want), __FILE__, __LINE__); \
    failures++; } } while (0)

int main()
{
    setenv("TZ", "UTC0", 1);
    tzset();
    const time_t t = 1794787200;   // 2026-11-16 00:00:00 UTC

    CHECK_EQ(configBackupFilename("infoclock32", t, true),  "infoclock32-config-2026-11-16.txt");
    CHECK_EQ(configBackupFilename("kitchen", t, false),     "kitchen-config.txt");
    CHECK_EQ(configBackupFilename("", t, true),             "infoclock32-config-2026-11-16.txt");
    CHECK_EQ(configBackupFilename("a\"b;c/d\\e f", 0, false), "abcdef-config.txt");
    CHECK_EQ(configBackupFilename("\"\r\n", 0, false),      "infoclock32-config.txt");
    CHECK_EQ(configBackupFilename(std::string(40, 'x'), 0, false),
             (std::string(32, 'x') + "-config.txt").c_str());

    // Local time decides the date: 23:30 UTC is already the next day in CET.
    setenv("TZ", "CET-1", 1);
    tzset();
    CHECK_EQ(configBackupFilename("c", t - 1800, true), "c-config-2026-11-16.txt");

    if (failures) { printf("%d config backup check(s) failed\n", failures); return 1; }
    printf("config_backup_check: all passed\n");
    return 0;
}

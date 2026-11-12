// The firmware's build stamp (see version.hpp). Kept alone in this file:
// build_stamp.py deletes its object before every build, so this is the only
// file recompiled just to refresh the stamp.
#include <version.hpp>

const char kBuildDate[] = __DATE__;
const char kBuildTime[] = __TIME__;

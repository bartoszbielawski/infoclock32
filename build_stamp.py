# PlatformIO pre-script: make src/build_stamp.cpp recompile on every build so
# BUILD_DATE / BUILD_TIME (version.hpp) show when the firmware was built.
# SCons rebuilds a missing target, so deleting the object is enough; only
# this one file is recompiled and the firmware relinked.
import os

Import("env")

obj = os.path.join(env.subst("$BUILD_DIR"), "src", "build_stamp.cpp.o")
if os.path.exists(obj):
    os.remove(obj)

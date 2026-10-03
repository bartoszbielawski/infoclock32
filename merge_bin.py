# Post-build: produce firmware-merged.bin (bootloader + partitions + boot_app0 + app)
# that can be flashed at offset 0x0 by web flashers such as espboards.dev.
Import("env")
import os, sys

def merge(source, target, env):
    mcu = env.BoardConfig().get("build.mcu", "esp32")
    boot_off = "0x1000" if mcu in ("esp32", "esp32s2") else "0x0"
    build = env.subst("$BUILD_DIR")
    fw = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    out = os.path.join(build, "firmware-merged.bin")
    flash_size = env.BoardConfig().get("upload.flash_size", "4MB")
    cmd = [env.subst("$PYTHONEXE"), esptool, "--chip", mcu, "merge_bin", "-o", out,
           "--flash_mode", "dio", "--flash_freq", env.BoardConfig().get("build.f_flash", "80000000L")[:-7] + "m",
           "--flash_size", flash_size,
           boot_off, os.path.join(build, "bootloader.bin"),
           "0x8000", os.path.join(build, "partitions.bin"),
           "0xe000", os.path.join(fw, "tools", "partitions", "boot_app0.bin"),
           "0x10000", os.path.join(build, "firmware.bin")]
    env.Execute(env.VerboseAction(" ".join('"%s"' % c for c in cmd), "Merging " + out))

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)

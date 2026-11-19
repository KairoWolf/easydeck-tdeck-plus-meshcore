# Post-build: produce both an app-only image and a merged (full) image, and copy
# them to ../dist with friendly names.
#  - EasyDeck-<ver>-app.bin     : flash at 0x10000, or update an existing install
#  - EasyDeck-<ver>-full.bin    : flash at 0x0 over USB, or put on the SD card for the Launcher
#                                 (the Launcher reads the partition table from it and
#                                 creates our "edfs" storage partition)
import os, shutil, subprocess
Import("env")

def after_build(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    proj_dir = env.subst("$PROJECT_DIR")
    ver = env.GetProjectOption("custom_version", "dev")
    dist = os.path.join(proj_dir, "..", "dist")
    os.makedirs(dist, exist_ok=True)
    app = os.path.join(build_dir, "firmware.bin")
    images = []
    for item in env.get("FLASH_EXTRA_IMAGES", []):
        images += [env.subst(item[0]), env.subst(item[1])]
    images += [env.subst("$ESP32_APP_OFFSET"), app]
    full = os.path.join(build_dir, "firmware-full.bin")
    cmd = [env.subst("$PYTHONEXE"), env.subst("$OBJCOPY"), "--chip", "esp32s3", "merge_bin",
           "-o", full, "--flash_mode", "qio", "--flash_freq", "80m", "--flash_size", "16MB"] + images
    subprocess.check_call(cmd)
    shutil.copy(app, os.path.join(dist, f"EasyDeck-{ver}-app.bin"))
    shutil.copy(full, os.path.join(dist, f"EasyDeck-{ver}-full.bin"))
    print(f"EasyDeck: wrote dist/EasyDeck-{ver}-app.bin and dist/EasyDeck-{ver}-full.bin")

env.AddPostAction("$BUILD_DIR/firmware.bin", after_build)

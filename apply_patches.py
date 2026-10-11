from os.path import join, isfile

Import("env")

FRAMEWORK_DIR = join(
    env["PROJECT_DIR"],
    ".pio", "libdeps", "esp32doit-devkit-v1", "TFT_eSPI"
)

patchflag_path = join(FRAMEWORK_DIR, ".patching-done")

# Patch file only if we didn't do it before
if not isfile(patchflag_path):
    original_file = join(FRAMEWORK_DIR, "User_Setup.h")
    patched_file = join(env["PROJECT_DIR"], "patches", "1-setup_screen_pins.patch")

    assert isfile(original_file) and isfile(patched_file)

    env.Execute("patch %s %s" % (original_file, patched_file))

    def _touch(path):
        with open(path, "w") as fp:
            fp.write("")

    env.Execute(lambda *args, **kwargs: _touch(patchflag_path))

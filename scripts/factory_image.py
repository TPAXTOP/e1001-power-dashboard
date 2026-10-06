# PlatformIO extra script: `pio run -e e1001 -t factory` merges bootloader,
# partition table, otadata (boot_app0) and the app into one image that flashes
# at offset 0 (esptool write-flash 0x0 factory.bin, or a web flasher).
#
# Offsets and files come from the same FLASH_EXTRA_IMAGES list the upload
# target uses, so the result always matches a USB `-t upload`.
Import("env")  # noqa: F821 (provided by PlatformIO)

from pathlib import Path


def merge_factory(target, source, env):
    board = env.BoardConfig()
    images = [(off, env.subst(path)) for off, path in env.get("FLASH_EXTRA_IMAGES", [])]
    images.append((env.subst("$ESP32_APP_OFFSET"), env.subst("$BUILD_DIR/${PROGNAME}.bin")))
    for _, path in images:
        if not Path(path).exists():
            raise SystemExit(f"factory: missing {path} - run `pio run -e {env['PIOENV']}` first")

    out = env.subst("$BUILD_DIR/factory.bin")
    args = [
        "$UPLOADER",
        "--chip", board.get("build.mcu"),
        "merge-bin",
        "-o", f'"{out}"',
        "--flash-mode", "${__get_board_flash_mode(__env__)}",
        "--flash-freq", "${__get_board_f_image(__env__)}",
        "--flash-size", board.get("upload.flash_size", "detect"),
    ]
    for off, path in sorted(images, key=lambda i: int(i[0], 16)):
        args += [off, f'"{path}"']
    return env.Execute(env.VerboseAction(" ".join(args), f"Merging {out}"))


env.AddCustomTarget(
    name="factory",
    # The build-dir images (bootloader, partitions) are SCons targets too, so a
    # clean `-t factory` builds them first.
    dependencies=["$BUILD_DIR/${PROGNAME}.bin"]
    + [
        env.subst(path)
        for _, path in env.get("FLASH_EXTRA_IMAGES", [])
        if Path(env.subst(path)).parent == Path(env.subst("$BUILD_DIR"))
    ],
    actions=merge_factory,
    title="Factory image",
    description="Merge bootloader, partitions, otadata and app into factory.bin",
)

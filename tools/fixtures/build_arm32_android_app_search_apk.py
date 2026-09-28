#!/usr/bin/env python3
import pathlib
import sys
import zipfile

ENTRY = "lib/armeabi-v7a/libfixture_app_child.so"

if len(sys.argv) != 3:
    raise SystemExit(f"usage: {sys.argv[0]} <child-so> <output-apk>")

child = pathlib.Path(sys.argv[1])
output = pathlib.Path(sys.argv[2])
data = child.read_bytes()
if not data:
    raise SystemExit("child DSO is empty")

output.parent.mkdir(parents=True, exist_ok=True)
info = zipfile.ZipInfo(ENTRY, date_time=(1980, 1, 1, 0, 0, 0))
info.create_system = 3
info.external_attr = 0o100644 << 16
info.compress_type = zipfile.ZIP_DEFLATED

with zipfile.ZipFile(
    output,
    mode="w",
    compression=zipfile.ZIP_DEFLATED,
    compresslevel=9,
    strict_timestamps=True,
) as archive:
    archive.writestr(
        info,
        data,
        compress_type=zipfile.ZIP_DEFLATED,
        compresslevel=9,
    )

with zipfile.ZipFile(output, mode="r") as archive:
    entries = archive.infolist()
    if len(entries) != 1:
        raise SystemExit("fixture APK did not contain exactly one entry")
    entry = entries[0]
    if entry.filename != ENTRY or entry.compress_type != zipfile.ZIP_DEFLATED:
        raise SystemExit("fixture APK entry metadata was not deterministic DEFLATE")
    if archive.read(ENTRY) != data:
        raise SystemExit("fixture APK entry bytes did not round-trip")

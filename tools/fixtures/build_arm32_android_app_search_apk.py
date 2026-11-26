#!/usr/bin/env python3
import pathlib
import sys
import zipfile

ROOT_ENTRY = "lib/armeabi-v7a/libfixture_app_root.so"
CHILD_ENTRY = "lib/armeabi-v7a/libfixture_app_child.so"

if len(sys.argv) != 4:
    raise SystemExit(
        f"usage: {sys.argv[0]} <root-so> <child-so> <output-apk>"
    )

root = pathlib.Path(sys.argv[1])
child = pathlib.Path(sys.argv[2])
output = pathlib.Path(sys.argv[3])
entries = (
    (ROOT_ENTRY, root.read_bytes()),
    (CHILD_ENTRY, child.read_bytes()),
)
if any(not data for _, data in entries):
    raise SystemExit("root or child DSO is empty")

output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(
    output,
    mode="w",
    compression=zipfile.ZIP_DEFLATED,
    compresslevel=9,
    strict_timestamps=True,
) as archive:
    for name, data in entries:
        info = zipfile.ZipInfo(
            name, date_time=(1980, 1, 1, 0, 0, 0)
        )
        info.create_system = 3
        info.external_attr = 0o100644 << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(
            info,
            data,
            compress_type=zipfile.ZIP_DEFLATED,
            compresslevel=9,
        )

with zipfile.ZipFile(output, mode="r") as archive:
    infos = archive.infolist()
    if [entry.filename for entry in infos] != [
        ROOT_ENTRY,
        CHILD_ENTRY,
    ]:
        raise SystemExit("fixture APK entry order was not deterministic")
    for name, data in entries:
        entry = archive.getinfo(name)
        if entry.compress_type != zipfile.ZIP_DEFLATED:
            raise SystemExit(
                f"fixture APK entry was not DEFLATE: {name}"
            )
        if archive.read(name) != data:
            raise SystemExit(
                f"fixture APK entry bytes did not round-trip: {name}"
            )

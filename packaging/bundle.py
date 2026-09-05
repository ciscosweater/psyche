"""Build a relocatable Linux bundle inside the release container."""

from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys
import zipfile

root = Path(sys.argv[1])
qt = Path("/usr/lib/x86_64-linux-gnu/qt6")
cmake = Path("CMakeLists.txt").read_text()
match = re.search(r"project\(Psyche VERSION ([0-9.]+)", cmake)
if not match:
    raise SystemExit("Could not read VERSION from CMakeLists.txt")
version = match.group(1)


def copy_files(source: Path, destination: Path, names: list[str]) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for name in names:
        item = source / name
        if item.is_file():
            shutil.copy2(item, destination / name)


(root / "lib").mkdir(exist_ok=True)

imports = json.loads(
    subprocess.check_output(
        [
            "/usr/lib/qt6/libexec/qmlimportscanner",
            "-rootPath",
            "qml",
            "-importPath",
            str(qt / "qml"),
        ]
    )
)
for module in imports:
    if not module.get("path") or not module.get("relativePath"):
        continue
    source = Path(module["path"])
    target = root / "qml" / module["relativePath"]
    target.mkdir(parents=True, exist_ok=True)
    for item in source.iterdir():
        if item.is_file() and item.suffix != ".qmltypes":
            shutil.copy2(item, target / item.name)

for style in ("Fusion", "Material", "Imagine", "Universal"):
    shutil.rmtree(root / "qml" / "QtQuick" / "Controls" / style, ignore_errors=True)
for types in (root / "qml").rglob("*.qmltypes"):
    types.unlink()

copy_files(qt / "plugins" / "platforms", root / "plugins" / "platforms", [
    "libqxcb.so",
    "libqwayland-generic.so",
])
copy_files(qt / "plugins" / "imageformats", root / "plugins" / "imageformats", [
    "libqjpeg.so",
    "libqsvg.so",
])
copy_files(qt / "plugins" / "tls", root / "plugins" / "tls", ["libqopensslbackend.so"])
copy_files(qt / "plugins" / "iconengines", root / "plugins" / "iconengines", ["libqsvgicon.so"])
copy_files(
    qt / "plugins" / "wayland-shell-integration",
    root / "plugins" / "wayland-shell-integration",
    ["libxdg-shell.so"],
)
copy_files(
    qt / "plugins" / "wayland-decoration-client",
    root / "plugins" / "wayland-decoration-client",
    ["libbradient.so"],
)
copy_files(
    qt / "plugins" / "wayland-graphics-integration-client",
    root / "plugins" / "wayland-graphics-integration-client",
    ["libshm-emulation-server.so"],
)

# Host C runtime and GL dispatcher stay on the system.
excluded = re.compile(
    r"^(ld-linux.*|lib(c|m|dl|pthread|rt|resolv|util|nss_.*)\.so.*"
    r"|lib(GL|EGL|GLX|OpenGL|GLdispatch)\.so.*)$"
)
queue = [p for p in root.rglob("*") if p.is_file() and p.read_bytes()[:4] == b"\x7fELF"]
seen = set()
while queue:
    binary = queue.pop()
    if binary in seen:
        continue
    seen.add(binary)
    output = subprocess.check_output(["ldd", str(binary)], text=True)
    if "not found" in output:
        raise RuntimeError(output)
    for name, path in re.findall(r"^\s*(\S+) => (/\S+)", output, re.M):
        if excluded.match(name):
            continue
        target = root / "lib" / name
        if not target.exists():
            shutil.copy2(path, target)
            queue.append(target)

for binary in list(seen) + list((root / "lib").glob("*")):
    if binary.is_file():
        subprocess.run(["strip", "--strip-unneeded", str(binary)], check=False, capture_output=True)

(root / "fontconfig").mkdir(exist_ok=True)
shutil.copy2("packaging/fonts.conf", root / "fontconfig/fonts.conf")
(root / "bin/qt.conf").write_text(
    "[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nQmlImports=qml\n"
)
(root / "psyche").write_text(
    """#!/bin/sh
set -eu
bundle_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$bundle_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export FONTCONFIG_PATH="$bundle_dir/fontconfig"
export FONTCONFIG_FILE="$bundle_dir/fontconfig/fonts.conf"
export PSYCHE_FONTCONFIG_FILE="$FONTCONFIG_FILE"
export QT_PLUGIN_PATH="$bundle_dir/plugins"
export QML_IMPORT_PATH="$bundle_dir/qml"
export QML2_IMPORT_PATH="$bundle_dir/qml"
exec "$bundle_dir/bin/psyche" "$@"
"""
)
(root / "psyche").chmod(0o755)
shutil.copy2("packaging/install.sh", root / "install.sh")
(root / "install.sh").chmod(0o755)
shutil.copy2("qml/icons/psyche.svg", root / "psyche.svg")
if Path("LICENSE").is_file():
    shutil.copy2("LICENSE", root / "LICENSE")

(root / "licenses").mkdir(exist_ok=True)
shutil.copy2("qml/fonts/OFL.txt", root / "licenses/PixelifySans-OFL.txt")
(root / "licenses" / "THIRD_PARTY.md").write_text(
    """This bundle ships Qt 6, libarchive, yaml-cpp, OpenSSL, ICU, and related
shared libraries from Debian 13. Shared objects in `lib/` can be replaced
with compatible builds. Psyche itself is MIT (`LICENSE`). Pixelify Sans is
SIL OFL (`PixelifySans-OFL.txt`).
"""
)

(root / "INSTALL.md").write_text(
    f"""# psyche {version}

```sh
./psyche
./install.sh   # then ~/.local/share/psyche/bin/psyche
```

Needs this whole directory, not just `bin/psyche`. Linux x86_64, glibc 2.41+.
Software renderer by default; set `QT_QUICK_BACKEND` to override.
"""
)

archive_path = root.parent / (root.name + "-setup.zip")
with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
    for file in sorted(root.rglob("*")):
        if file.is_file():
            archive.write(file, file.relative_to(root))
with zipfile.ZipFile(archive_path) as archive:
    assert archive.testzip() is None
archive_path.with_suffix(".zip.sha256").write_text(
    hashlib.sha256(archive_path.read_bytes()).hexdigest() + "  " + archive_path.name + "\n"
)

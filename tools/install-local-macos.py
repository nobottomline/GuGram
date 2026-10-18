#!/usr/bin/env python3
"""Install the local GuGram Desktop build without replacing another Telegram."""

import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import plistlib
import shlex
import subprocess
import sys
import tempfile


LAUNCHER_MARKER = "# GuGram Desktop local launcher"


def launcher_text(app: Path) -> str:
    return f'''#!/bin/sh
{LAUNCHER_MARKER}
app={shlex.quote(str(app))}
if [ ! -d "$app" ]; then
    echo "GuGram Desktop is not installed at $app" >&2
    exit 1
fi
case "${{1-}}" in
    --version)
        exec /usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist"
        ;;
    --path)
        printf '%s\\n' "$app"
        exit 0
        ;;
    --help|-h)
        printf '%s\\n' 'Usage: tg [Telegram arguments or tg:// URL]' '       tg --version | --path'
        exit 0
        ;;
esac
exec /usr/bin/open -a "$app" --args "$@"
'''


def install(source: Path, app_dir: Path, bin_dir: Path) -> Path:
    repo = Path(__file__).resolve().parents[1]
    executable = source / "Contents/MacOS/Telegram"
    if not executable.is_file():
        raise ValueError(f"Build Telegram first: missing {executable}")
    target = app_dir / "GuGram Desktop.app"
    command = bin_dir / "tg"
    if target.is_symlink() or command.is_symlink():
        raise ValueError("Refusing to replace a symbolic link at the install destination.")
    if command.exists() and LAUNCHER_MARKER not in command.read_text():
        raise ValueError(f"An unrelated command already exists: {command}")
    if target.exists():
        with (target / "Contents/Info.plist").open("rb") as file:
            if plistlib.load(file).get("CFBundleIdentifier") != "com.gugram.desktop":
                raise ValueError(f"An unrelated application already exists: {target}")
    app_dir.mkdir(parents=True, exist_ok=True)
    bin_dir.mkdir(parents=True, exist_ok=True)
    backup = None
    with tempfile.TemporaryDirectory(prefix=".gugram-install-", dir=app_dir) as temporary:
        staged = Path(temporary) / target.name
        subprocess.run(["/usr/bin/ditto", str(source), str(staged)], check=True)
        info_path = staged / "Contents/Info.plist"
        with info_path.open("rb") as file:
            info = plistlib.load(file)
        info.update({
            "CFBundleIdentifier": "com.gugram.desktop",
            "CFBundleName": "GuGram Desktop",
            "CFBundleDisplayName": "GuGram Desktop",
            "CFBundleGetInfoString": "GuGram Desktop local custom Telegram client",
            "LSMinimumSystemVersion": "15.0",
        })
        info.pop("CFBundleURLTypes", None)
        info.pop("CFBundleDocumentTypes", None)
        with info_path.open("wb") as file:
            plistlib.dump(info, file)
        subprocess.run([
            "/usr/bin/codesign", "--force", "--sign", "-", "--entitlements",
            str(repo / "Telegram/Telegram/Telegram.entitlements"), str(staged),
        ], check=True)
        subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(staged)], check=True)
        if target.exists():
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
            backup = app_dir / f".GuGram Desktop.previous-{stamp}.app"
            target.rename(backup)
        try:
            staged.rename(target)
        except OSError:
            if backup:
                backup.rename(target)
            raise
    descriptor, temporary = tempfile.mkstemp(prefix=".tg-", dir=bin_dir)
    try:
        with os.fdopen(descriptor, "w") as file:
            file.write(launcher_text(target))
        os.chmod(temporary, 0o755)
        os.replace(temporary, command)
    finally:
        Path(temporary).unlink(missing_ok=True)
    print(f"Installed application: {target}")
    print(f"Installed command: {command}")
    if backup:
        print(f"Previous application retained: {backup}")
    return target


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path,
                        default=Path(__file__).resolve().parents[1] / "out/Telegram.app")
    parser.add_argument("--app-dir", type=Path, default=Path.home() / "Applications")
    parser.add_argument("--bin-dir", type=Path, default=Path.home() / ".local/bin")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("This installer requires macOS.")
    install(args.source.resolve(), args.app_dir.absolute(), args.bin_dir.absolute())


if __name__ == "__main__":
    main()

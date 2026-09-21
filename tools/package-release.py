#!/usr/bin/env python3
"""Assemble a local-generation Proton package without PowerShell."""
from __future__ import annotations

import argparse
import json
import shutil
import stat
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BLOCKED_SUFFIXES = {".cmd", ".bat", ".exe", ".ps1", ".vbs", ".lnk", ".msi"}


def require_file(path: Path, description: str) -> Path:
    path = path.expanduser().resolve()
    if not path.is_file() or path.is_symlink():
        raise SystemExit(f"Missing {description}: {path}")
    return path


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def stage_package(args: argparse.Namespace) -> Path:
    output = args.output_directory.expanduser().resolve()
    if output.exists():
        raise SystemExit(f"Choose a new output directory; it already exists: {output}")

    plugin = require_file(args.native_plugin_path, "native REFramework plug-in")
    helper = require_file(args.native_helper_path, "Linux HID helper")
    portaudio = require_file(args.portaudio_path, "PortAudio runtime")
    generator = args.generator_directory.expanduser().resolve()
    generator_files = [
        require_file(generator / name, f"asset generator file {name}")
        for name in (
            "OnimushaDualSense.dll",
            "OnimushaDualSense.deps.json",
            "OnimushaDualSense.runtimeconfig.json",
        )
    ]
    bundled_files = [
        require_file(ROOT / "distribution" / name, f"bundled generator file {name}")
        for name in ("defense_haptics.json",)
    ]

    game_plugins = output / "reframework/plugins"
    generator_destination = output / "asset-generator"
    copy_file(portaudio, game_plugins / "libportaudio64bit.dll")
    copy_file(plugin, game_plugins / "OnimushaDualSense.dll")
    copy_file(helper, output / "onimusha_hidrelay")
    copy_file(ROOT / "tools/start-hidrelay.sh", output / "start-hidrelay.sh")
    copy_file(ROOT / "tools/prepare-assets.sh", output / "prepare-assets.sh")
    copy_file(ROOT / "tools/steam-launch.sh", output / "steam-launch.sh")
    for source in [*generator_files, *bundled_files]:
        copy_file(source, generator_destination / source.name)
    copy_file(ROOT / "distribution/LICENSE", output / "LICENSE")
    copy_file(ROOT / "distribution/THIRD_PARTY_NOTICES.txt", output / "THIRD_PARTY_NOTICES.txt")
    metadata = {
        "assets": "generated locally by prepare-assets.sh",
        "format": 3,
        "version": args.version,
    }
    (output / "RELEASE-METADATA.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    files = sorted(path for path in output.rglob("*") if path.is_file())
    blocked = [path for path in files if path.suffix.lower() in BLOCKED_SUFFIXES]
    if blocked:
        names = ", ".join(str(path.relative_to(output)) for path in blocked)
        raise SystemExit(f"Distribution contains forbidden Windows files: {names}")
    return output


def write_archive(stage: Path, archive: Path) -> None:
    archive = archive.expanduser().resolve()
    if archive.exists():
        raise SystemExit(f"Choose a new archive path; it already exists: {archive}")
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as stream:
        for path in sorted(path for path in stage.rglob("*") if path.is_file()):
            relative = path.relative_to(stage).as_posix()
            info = zipfile.ZipInfo(relative, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (stat.S_IMODE(path.stat().st_mode) & 0o777) << 16
            stream.writestr(info, path.read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-plugin-path", type=Path, required=True)
    parser.add_argument("--native-helper-path", type=Path, required=True)
    parser.add_argument("--generator-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--portaudio-path", type=Path, default=ROOT / "distribution/libportaudio64bit.dll")
    parser.add_argument("--archive", type=Path, help="also write a deterministic ZIP archive")
    parser.add_argument("--version", default="1.2.0-proton.1")
    args = parser.parse_args()
    stage = stage_package(args)
    if args.archive:
        write_archive(stage, args.archive)
    print(stage)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

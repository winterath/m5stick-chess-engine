from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def find_boot_app0(build_dir: Path) -> Path | None:
    candidates = [build_dir / "boot_app0.bin"]
    candidates.extend(ROOT.glob(".pio/**/boot_app0.bin"))
    candidates.extend((Path.home() / ".platformio").glob("packages/**/boot_app0.bin"))
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def find_esptool() -> tuple[Path, Path] | None:
    tool = Path.home() / ".platformio" / "packages" / "tool-esptoolpy" / "esptool.py"
    pio_python = Path.home() / ".platformio" / "penv" / "Scripts" / "python.exe"
    if not pio_python.exists():
        pio_python = Path.home() / ".platformio" / "penv" / "bin" / "python"
    if tool.exists() and pio_python.exists():
        return pio_python, tool
    return None


def copy_artifact(source: Path, destination: Path) -> None:
    if not source.exists():
        raise FileNotFoundError(f"Missing build artifact: {source}")
    shutil.copy2(source, destination)
    print(f"wrote {destination.relative_to(ROOT)}")


def build_manifest(env_name: str, entries: list[tuple[str, str]]) -> dict:
    return {
        "environment": env_name,
        "flash_files": [
            {
                "address": address,
                "file": filename,
            }
            for address, filename in entries
        ],
    }


def try_merge(env_name: str, entries: list[tuple[str, Path]], export_dir: Path) -> None:
    esptool = find_esptool()
    if esptool is None:
        print("merge skipped: PlatformIO esptool was not found")
        return

    python_exe, esptool_py = esptool
    merged = export_dir / f"m5_chess_engine_{env_name}_merged.bin"
    command = [
        str(python_exe),
        str(esptool_py),
        "--chip",
        "esp32",
        "merge_bin",
        "-o",
        str(merged),
        "--flash_mode",
        "dio",
        "--flash_freq",
        "40m",
        "--flash_size",
        "4MB",
    ]
    for address, file_path in entries:
        command.extend([address, str(file_path)])

    try:
        subprocess.run(command, cwd=ROOT, check=True)
    except subprocess.CalledProcessError as exc:
        print(f"merge skipped: esptool failed with exit code {exc.returncode}")
        return

    print(f"wrote {merged.relative_to(ROOT)}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Package PlatformIO output for M5Burner.")
    parser.add_argument("env", nargs="?", default="m5stack-core-esp32")
    parser.add_argument("--no-merge", action="store_true", help="Do not create a single merged firmware bin.")
    args = parser.parse_args()

    build_dir = ROOT / ".pio" / "build" / args.env
    if not build_dir.exists():
        print(f"Build folder not found: {build_dir}")
        print(f"Run: pio run -e {args.env}")
        return 1

    bootloader = build_dir / "bootloader.bin"
    partitions = build_dir / "partitions.bin"
    app = build_dir / "firmware.bin"
    boot_app0 = find_boot_app0(build_dir)
    if boot_app0 is None:
        print("boot_app0.bin was not found. Re-run PlatformIO after the ESP32 Arduino platform is installed.")
        return 1

    firmware_dir = ROOT / "firmware" / args.env
    export_dir = ROOT / "exports"
    firmware_dir.mkdir(parents=True, exist_ok=True)
    export_dir.mkdir(parents=True, exist_ok=True)

    for old_file in firmware_dir.glob("*.bin"):
        old_file.unlink()

    named_entries = [
        ("0x1000", bootloader, "bootloader_0x1000.bin"),
        ("0x8000", partitions, "partitions_0x8000.bin"),
        ("0xe000", boot_app0, "boot_app0_0xe000.bin"),
        ("0x10000", app, "m5_chess_engine_0x10000.bin"),
    ]

    for _, source, filename in named_entries:
        copy_artifact(source, firmware_dir / filename)

    manifest_entries = [(address, filename) for address, _, filename in named_entries]
    manifest_path = export_dir / f"{args.env}_flash_manifest.json"
    manifest_path.write_text(
        json.dumps(build_manifest(args.env, manifest_entries), indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {manifest_path.relative_to(ROOT)}")

    if not args.no_merge:
        merge_entries = [(address, firmware_dir / filename) for address, _, filename in named_entries]
        try_merge(args.env, merge_entries, export_dir)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

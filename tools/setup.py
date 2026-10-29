#!/usr/bin/env python3
"""Fetch pinned public source dependencies; keep downloaded files out of Git."""

from pathlib import Path, PurePosixPath
import hashlib
import json
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = [
    (
        "lua",
        "5.1.1",
        "https://www.lua.org/ftp/lua-5.1.1.tar.gz",
        "c5daeed0a75d8e4dd2328b7c7a69888247868154acbda69110e97d4a6e17d1f0",
    ),
    (
        "freetype",
        "2.15.0",
        "https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz",
        "93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5",
    ),
]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def prepare(name, version, url, expected):
    directory = ROOT / "engine" / ("lua-runtime" if name == "lua" else "font-runtime")
    receipt = directory / "build/source-receipt.json"
    try:
        prior = json.loads(receipt.read_text())

        def source(row):
            return (
                directory / row["path"]
                if name == "lua"
                else directory / "build/public-source" / row["path"]
            )

        if prior["archive_sha256"] == expected and all(
            digest(source(row).read_bytes()) == row["sha256"] for row in prior["files"]
        ):
            print(f"{name} {version}: sources ready")
            return
    except (FileNotFoundError, KeyError, ValueError):
        pass
    cache = ROOT / ".local/dependencies" / url.rsplit("/", 1)[-1]
    cache.parent.mkdir(parents=True, exist_ok=True)
    if not cache.is_file() or digest(cache.read_bytes()) != expected:
        with urllib.request.urlopen(url, timeout=120) as response:
            content = response.read(32 * 1024 * 1024)
        if digest(content) != expected:
            raise ValueError(f"{name}: source archive checksum mismatch")
        cache.write_bytes(content)
    rows, total = [], 0
    with tarfile.open(cache) as archive:
        for entry in archive:
            path = PurePosixPath(entry.name)
            if path.is_absolute() or ".." in path.parts or entry.issym() or entry.islnk():
                raise ValueError("Unsafe dependency archive path")
            relative = PurePosixPath(*path.parts[1:])
            if not entry.isfile() or not relative.parts:
                continue
            if name == "lua":
                if relative.parts[0] != "src" and relative.name != "COPYRIGHT":
                    continue
                output = directory / "tools" / f"lua-{version}" / str(relative)
            else:
                if relative.parts[0] not in ("src", "include"):
                    continue
                output = directory / "build/public-source" / str(relative)
            total += entry.size
            if entry.size > 2 * 1024 * 1024 or total > 24 * 1024 * 1024:
                raise ValueError("Dependency source capacity exceeded")
            data = archive.extractfile(entry).read()
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(data)
            rows.append(
                {
                    "path": str(output.relative_to(directory)) if name == "lua" else str(relative),
                    "sha256": digest(data),
                }
            )
    receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_text(
        json.dumps(
            {"version": version, "url": url, "archive_sha256": expected, "files": rows}, indent=2
        )
        + "\n"
    )
    print(f"{name} {version}: prepared {len(rows)} source files")


if __name__ == "__main__":
    for dependency in DEPENDENCIES:
        prepare(*dependency)

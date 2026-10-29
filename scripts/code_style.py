#!/usr/bin/env python3
"""Format or lint authored sources without traversing private client artifacts."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

from repository_policy import ROOT, inventory


def executable(name: str) -> str:
    """Prefer the repository virtual environment, then an installed tool."""
    local = ROOT / ".venv" / ("Scripts" if os.name == "nt" else "bin") / name
    if local.is_file():
        return str(local)
    found = shutil.which(name)
    if found:
        return found
    raise SystemExit(f"Missing {name}. Create .venv and install requirements-dev.txt first.")


def node_tool(name: str, entry: str) -> list[str]:
    path = ROOT / "node_modules" / name / entry
    if not path.is_file():
        raise SystemExit(f"Missing {name}. Run npm install first.")
    return [executable("node"), str(path)]


def batches(paths: list[str], size: int = 200):
    for start in range(0, len(paths), size):
        yield paths[start : start + size]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["format", "format-check", "lint"])
    args = parser.parse_args()
    paths = [row["path"] for row in inventory()]
    python = [p for p in paths if Path(p).suffix == ".py"]
    c = [p for p in paths if Path(p).suffix in {".c", ".h", ".inc"}]
    prettier = [
        p for p in paths if Path(p).suffix in {".js", ".mjs", ".html", ".md", ".json", ".yml"}
    ]
    javascript = [p for p in prettier if Path(p).suffix in {".js", ".mjs"}]
    commands = []
    if args.action == "lint":
        commands.append((node_tool("eslint", "bin/eslint.js"), javascript))
        commands.append(([executable("ruff"), "check", "--output-format", "concise"], python))
        commands.append(([executable("clang-format"), "--dry-run", "--Werror"], c))
    else:
        check = args.action == "format-check"
        commands.append(
            (
                node_tool("prettier", "bin/prettier.cjs")
                + ["--check" if check else "--write", "--log-level", "warn"],
                prettier,
            )
        )
        commands.append(([executable("ruff"), "format"] + (["--check"] if check else []), python))
        commands.append(
            ([executable("clang-format")] + (["--dry-run", "--Werror"] if check else ["-i"]), c)
        )
    status = 0
    for command, selected in commands:
        for batch in batches(selected):
            result = subprocess.run(command + batch, cwd=ROOT, check=False)
            status = max(status, result.returncode)
    return status


if __name__ == "__main__":
    sys.exit(main())

"""Select only distributable brWoWser source; never traverse local client data."""

from pathlib import Path
import hashlib
import os
import re

ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = {
    "README.md",
    "CONTRIBUTING.md",
    ".gitignore",
    ".editorconfig",
    ".clang-format",
    ".prettierignore",
    ".prettierrc.json",
    "eslint.config.mjs",
    "package.json",
    "package-lock.json",
    "pyproject.toml",
    "requirements-dev.txt",
}
SCRIPTS = {
    "build_auth.mjs",
    "network_bridge.mjs",
    "x86_toolchain.mjs",
    "client_profile.py",
    "dev.mjs",
    "restart_dev.mjs",
    "code_style.py",
    "repository_policy.py",
}
DOCS = {
    "ARCHITECTURE.md",
    "DEPLOYMENT.md",
    "STATUS.md",
    "TODO.md",
    "KNOWN_ISSUES.md",
    "THIRD_PARTY.md",
}
ROOTS = {".github", "runtime", "engine", "app", "tools", "config", "scripts", "docs"}
PRIVATE_PARTS = {
    "build",
    "cache",
    "captures",
    "runs",
    "tools",
    "__pycache__",
    "node_modules",
    ".git",
}
EXCLUDED_ENGINE = {
    "simplehtml-trace",
    "login-ui",
    "screen-barrier",
    "login-material-passes",
    "login-soundscape",
}
SOURCE_SUFFIXES = {".c", ".h", ".inc", ".mjs", ".py", ".json", ".html", ".css", ".md"}
SECRET = re.compile(
    rb"(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----)"
)


def selected(name):
    p = Path(name)
    if p.is_absolute() or ".." in p.parts:
        return False
    if len(p.parts) == 1:
        return name in ROOT_FILES
    if p.parts[0] == ".github":
        return name == ".github/ISSUE_TEMPLATE/bug_report.md"
    if p.parts[0] not in ROOTS or any(part in PRIVATE_PARTS for part in p.parts[1:-1]):
        return False
    if name in {
        "engine/archive-selection/browser-reader.mjs",
        "runtime/io/wasm_memory.mjs",
        "runtime/scene/particles_motion.mjs",
    }:
        return False
    if p.parts[0] == "scripts":
        return len(p.parts) == 2 and p.name in SCRIPTS
    if p.parts[0] == "docs":
        return len(p.parts) == 2 and p.name in DOCS
    if p.parts[0] == "runtime" and p.suffix not in {".mjs", ".c", ".h", ".inc", ".json"}:
        return False
    if p.parts[0] == "engine" and p.parts[1] in EXCLUDED_ENGINE:
        return False
    if p.name.startswith(("check_", "test_", "probe", "gpu_", "pass_gpu_")):
        return False
    return p.suffix in SOURCE_SUFFIXES


def validate(name, data):
    if not selected(name):
        raise ValueError("Private or unsupported source path: " + name)
    if len(data) > 4 * 1024 * 1024 or b"\0" in data:
        raise ValueError("Unexpected source payload: " + name)
    data.decode("utf-8")
    if SECRET.search(data):
        raise ValueError("Possible credential in " + name)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inventory(root=ROOT):
    result = []
    for directory, dirs, files in os.walk(root, followlinks=False):
        base = Path(directory)
        relative = base.relative_to(root)
        dirs[:] = [
            d
            for d in dirs
            if not (base / d).is_symlink()
            and (d in ROOTS if relative == Path(".") else d not in PRIVATE_PARTS)
        ]
        for filename in files:
            p = base / filename
            name = p.relative_to(root).as_posix()
            if not selected(name):
                continue
            if p.is_symlink():
                raise ValueError("Symlink in source selection: " + name)
            data = p.read_bytes()
            validate(name, data)
            result.append({"path": name, "bytes": len(data), "sha256": digest(data)})
    for name in ROOT_FILES:
        p = root / name
        if p.is_file():
            data = p.read_bytes()
            validate(name, data)
            result.append({"path": name, "bytes": len(data), "sha256": digest(data)})
    return sorted({row["path"]: row for row in result}.values(), key=lambda row: row["path"])

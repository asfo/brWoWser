"""Discover a local locale and loose cinematic without renaming client files."""

from pathlib import Path
import re


def discover(root, locale=None, resolution=1024):
    data = root / "Data"
    available = sorted(
        p.name
        for p in data.iterdir()
        if p.is_dir()
        and re.fullmatch("[a-z]{2}[A-Z]{2}", p.name)
        and any(f.is_file() and f.suffix.lower() == ".mpq" for f in p.iterdir())
    )
    if not locale:
        cfg = root / "WTF/Config.wtf"
        matches = re.findall(
            r'^SET\s+(?:locale|textLocale)\s+"([a-z]{2}[A-Z]{2})"',
            cfg.read_text(errors="replace") if cfg.is_file() else "",
            re.M,
        )
        configured = [x for x in matches if x in available]
        if configured:
            locale = configured[0]
        elif len(available) == 1:
            locale = available[0]
        else:
            raise ValueError("Cannot choose client locale unambiguously: " + ", ".join(available))
    if locale not in available:
        raise ValueError("Locale not installed: " + locale)
    name = f"WOW_Intro_LK_{resolution}.avi"
    candidates = [
        p
        for p in (data / locale).rglob("*")
        if p.name.lower() == name.lower() and p.is_file() and not p.is_symlink()
    ]
    if len(candidates) != 1:
        raise ValueError(
            "Expected one regular cinematic "
            + name
            + " in Data/"
            + locale
            + ", found "
            + str(len(candidates))
        )
    return {"locale": locale, "cinematic_path": candidates[0].relative_to(data).as_posix()}


def cinematic_catalogue(root, locale):
    """Admit only the original Glue MovieList basenames, never arbitrary paths."""
    data = root / "Data"
    folder = data / locale
    logical = []
    for stem in ("Logo", "WOW_Intro", "WOW_Intro_BC", "WOW_Intro_LK"):
        for resolution in (800, 1024):
            name = f"{stem}_{resolution}"
            candidates = [
                p
                for p in folder.rglob("*")
                if p.is_file() and not p.is_symlink() and p.name.lower() == (name + ".avi").lower()
            ]
            if len(candidates) > 1:
                raise ValueError("Ambiguous cinematic " + name)
            if candidates:
                logical.append(
                    {
                        "name": name,
                        "logical_path": "Interface\\Cinematics\\" + name,
                        "source_path": candidates[0].relative_to(data).as_posix(),
                    }
                )
    return logical

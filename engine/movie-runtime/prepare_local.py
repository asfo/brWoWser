#!/usr/bin/env python3
# Local research modules require the path bootstrap before sibling imports.
# ruff: noqa: E402
"""Convert an owner-supplied loose cinematic into a private browser compatibility cache."""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "scripts"))
from client_profile import discover, cinematic_catalogue


def sha(p):
    h = hashlib.sha256()
    with p.open("rb") as f:
        for b in iter(lambda: f.read(1024 * 1024), b""):
            h.update(b)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--locale", default=None)
    ap.add_argument("--resolution", type=int, choices=[800, 1024], default=1024)
    ap.add_argument("--movie", default=None)
    ap.add_argument("--catalogue", action="store_true")
    args = ap.parse_args()
    profile = discover(ROOT, args.locale, args.resolution)
    args.locale = profile["locale"]
    catalogue = cinematic_catalogue(ROOT, args.locale)
    if args.catalogue:
        for movie in catalogue:
            movie["source_sha256"] = sha(ROOT / "Data" / movie["source_path"])
        print(json.dumps({"locale": args.locale, "movies": catalogue}))
        return
    chosen = (
        next((row for row in catalogue if row["name"] == args.movie), None)
        if args.movie
        else next(
            (row for row in catalogue if row["name"] == f"WOW_Intro_LK_{args.resolution}"), None
        )
    )
    if not chosen:
        raise ValueError("Cinematic not available in original local catalogue")
    profile["cinematic_path"] = chosen["source_path"]
    source = ROOT / "Data" / profile["cinematic_path"]
    if not source.is_file() or source.is_symlink():
        raise ValueError("missing regular owner cinematic")
    ffmpeg = shutil.which("ffmpeg")
    probe = shutil.which("ffprobe")
    if not ffmpeg or not probe:
        raise ValueError("ffmpeg and ffprobe required; no download attempted")
    out = HERE / "build"
    out.mkdir(exist_ok=True)
    target = out / (
        f"intro-lk-{args.locale}-{args.resolution}.private.webm"
        if not args.movie
        else f"{chosen['name']}-{args.locale}.private.webm"
    )
    receipt = target.with_suffix(".json")
    digest = sha(source)
    if receipt.exists() and target.exists():
        prior = json.loads(receipt.read_text())
        if (
            prior.get("source_path") == profile["cinematic_path"]
            and prior.get("source_sha256") == digest
            and prior.get("output_sha256") == sha(target)
        ):
            print(json.dumps(prior))
            return
    metadata = json.loads(
        subprocess.check_output(
            [
                probe,
                "-v",
                "error",
                "-show_entries",
                "format=duration,size:stream=codec_name,codec_tag_string,width,height,sample_rate,channels,r_frame_rate",
                "-of",
                "json",
                str(source),
            ]
        )
    )
    temp = target.with_suffix(".partial.webm")
    command = [
        ffmpeg,
        "-nostdin",
        "-v",
        "error",
        "-y",
        "-i",
        str(source),
        "-map",
        "0:v:0",
        "-map",
        "0:a:0",
        "-c:v",
        "libvpx-vp9",
        "-b:v",
        "0",
        "-crf",
        "28",
        "-deadline",
        "realtime",
        "-cpu-used",
        "8",
        "-row-mt",
        "1",
        "-threads",
        "4",
        "-c:a",
        "libopus",
        "-b:a",
        "128k",
        str(temp),
    ]
    try:
        subprocess.run(command, check=True)
        temp.replace(target)
    finally:
        temp.unlink(missing_ok=True)
    result = {
        "status": "PREPARED_PRIVATE_BROWSER_CINEMATIC",
        "logical_path": chosen["logical_path"],
        "locale": args.locale,
        "source_path": profile["cinematic_path"],
        "source_sha256": digest,
        "source_metadata": metadata,
        "output": str(target.relative_to(ROOT)),
        "output_sha256": sha(target),
        "media_type": 'video/webm; codecs="vp9, opus"',
        "conversion": "local public FFmpeg MPEG4-part2/MP3 to VP9/Opus; lossy compatibility conversion",
        "scope": "Owner-local cache only. No original playback timing, volume, subtitles or native frame/pixel equivalence.",
    }
    receipt.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()

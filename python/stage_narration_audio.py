#!/usr/bin/env python3
"""Normalize Storybook narrator recordings without touching image assets."""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_INPUT = REPO_ROOT / "assets_src" / "audio_narration"
DEFAULT_OUTPUT = REPO_ROOT / "spiffs_root" / "assets" / "audio"

NARRATION_FILES = (
    "intro_narrator.mp3",
    "scene_a_narrator_01.mp3",
    "scene_a_narrator_02.mp3",
    "scene_b1_narrator_01.mp3",
    "scene_b1_1_narrator_01.mp3",
    "scene_b1_1_narrator_02.mp3",
    "scene_b1_1_1_narrator_01.mp3",
    "scene_d1_narrator_01.mp3",
    "outro_narrator.mp3",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    return parser.parse_args()


def find_source(directory: Path, output_name: str) -> Path:
    stem = Path(output_name).stem
    source_stems = {stem, stem.replace("_", "-")}
    matches = sorted(
        path
        for source_stem in source_stems
        for path in directory.glob(f"{source_stem}.*")
        if path.is_file()
    )
    if len(matches) != 1:
        raise RuntimeError(
            f"expected exactly one source for {output_name}; found {len(matches)}"
        )
    return matches[0]


def normalize(ffmpeg: str, source: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            ffmpeg,
            "-v",
            "error",
            "-y",
            "-i",
            str(source),
            "-map_metadata",
            "-1",
            "-vn",
            "-af",
            "loudnorm=I=-18:TP=-1.5:LRA=7,aresample=16000",
            "-ar",
            "16000",
            "-ac",
            "1",
            "-c:a",
            "libmp3lame",
            "-b:a",
            "64k",
            "-write_xing",
            "0",
            "-id3v2_version",
            "0",
            "-write_id3v1",
            "0",
            str(output),
        ],
        check=True,
    )


def main() -> None:
    args = parse_args()
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg is required")

    for output_name in NARRATION_FILES:
        normalize(ffmpeg, find_source(args.input, output_name), args.output / output_name)


if __name__ == "__main__":
    main()

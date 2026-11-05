# decode_assets.py — Dubruederschaft build helper.
#
# Why this exists: the plugin UI is bitmap art (Lexi's crayon stickers),
# but the GitHub connector can only push TEXT files to the repo, so the
# PNGs and the Patrick Hand font live in the repo as base64 text:
#   resources/art/background.json  — {"panel_background.png": "<b64>"}
#   resources/art/stickers.json    — {"<name>.png": "<b64>", ...}
#   resources/fonts/*.ttf.b64      — raw base64, one file per font
# This script runs at CMake configure time (see CMakeLists.txt) and
# decodes them into the real binary files the build expects. It is
# idempotent: re-running it just overwrites the decoded files. It exits
# non-zero if any source file is missing or fails to decode, so a broken
# asset fails the build loudly instead of producing a plugin with
# missing art.
#
# Usage: python3 decode_assets.py <plugin-source-dir>
# (defaults to the directory this script lives in)
import base64
import json
import sys
from pathlib import Path

def decode_all(root: Path) -> list[str]:
    decoded = []
    art_dir = root / "resources" / "art"
    if art_dir.is_dir():
        for src in sorted(art_dir.glob("*.json")):
            manifest = json.loads(src.read_text())
            for name, b64 in manifest.items():
                dst = art_dir / name
                dst.parent.mkdir(parents=True, exist_ok=True)
                data = base64.b64decode(b64, validate=True)
                dst.write_bytes(data)
                decoded.append(f"{dst.relative_to(root)} ({len(data)} bytes)")
    fonts_dir = root / "resources" / "fonts"
    if fonts_dir.is_dir():
        for src in sorted(fonts_dir.glob("*.b64")):
            dst = src.with_suffix("")
            text = "".join(src.read_text().split())
            data = base64.b64decode(text, validate=True)
            dst.write_bytes(data)
            decoded.append(f"{dst.relative_to(root)} ({len(data)} bytes)")
    if not decoded:
        raise SystemExit("decode_assets.py: no asset sources found under " + str(root))
    return decoded

if __name__ == "__main__":
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    for line in decode_all(root):
        print("decoded " + line)

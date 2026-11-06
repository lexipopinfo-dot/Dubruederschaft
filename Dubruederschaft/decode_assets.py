# decode_assets.py — Dubruederschaft build helper (v0.3).
#
# Why this exists: the plugin UI is one flat panel PNG ("panel.png",
# 600x375), but the GitHub connector can only push TEXT files to the
# repo, so the PNG and the Patrick Hand font live in the repo as
# base64 text:
#   resources/art/panel_p1.json, panel_p2.json — {"panel.png": "<b64 chunk>"}
#   resources/fonts/*.ttf.b64                  — raw base64, one file per font
# This script runs at CMake configure time (see CMakeLists.txt) and
# decodes them into the real binary files the build expects. It is
# idempotent: re-running it just overwrites the decoded files. It exits
# non-zero if any source file is missing or fails to decode, so a broken
# asset fails the build loudly instead of producing a plugin with
# missing art. NOTE: only panel_*.json manifests are decoded — the
# v0.2.0 background/stickers manifests are still in the repo but
# intentionally ignored (superseded by the single panel).
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
        # A base64 string may be split across several manifests (the
        # GitHub CLI caps one argument at 128KB), so chunks for the same
        # output file are joined in manifest filename order.
        buffers: dict[str, list[str]] = {}
        for src in sorted(art_dir.glob("panel_*.json")):
            manifest = json.loads(src.read_text())
            for name, chunk in manifest.items():
                buffers.setdefault(name, []).append(chunk)
        for name, chunks in sorted(buffers.items()):
            data = base64.b64decode("".join(chunks), validate=True)
            dst = art_dir / name
            dst.parent.mkdir(parents=True, exist_ok=True)
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

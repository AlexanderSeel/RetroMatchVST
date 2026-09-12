"""Render the editor preview and promote its primary snapshot into README assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", type=Path, required=True, help="RetroMatchEditorPreview executable")
    parser.add_argument("--output", type=Path, default=Path("build-validation/readme-ui-preview"))
    args = parser.parse_args()

    output = (root / args.output).resolve() if not args.output.is_absolute() else args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(args.preview.resolve()), str(output)], check=False)
    if result.returncode:
        raise SystemExit(f"Editor visual preview failed with exit code {result.returncode}")

    source = output / "01-synth-mint.png"
    if not source.is_file():
        raise SystemExit(f"Expected preview image is missing: {source}")
    destination = root / "Assets/screenshots/retromatch-editor-example.png"
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    manifest = destination.with_suffix(".json")
    manifest.write_text(json.dumps({"source": "EditorPreview/01-synth-mint.png", "sha256": digest}, indent=2) + "\n", encoding="utf-8")
    print(f"Updated {destination} ({digest[:12]})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

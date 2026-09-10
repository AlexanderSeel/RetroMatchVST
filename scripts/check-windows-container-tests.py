#!/usr/bin/env python3
"""Ensure every CTest executable registered in CMake is built by the Windows container."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
build_script = (root / "scripts" / "build-container-windows.cmd").read_text(encoding="utf-8")

registered_targets = re.findall(r"add_test\s*\(\s*NAME\s+\S+\s+COMMAND\s+([A-Za-z0-9_]+)", cmake, flags=re.IGNORECASE)
missing = [target for target in registered_targets if target not in build_script]

if missing:
    print("Windows container does not build registered CTest target(s): " + ", ".join(missing), file=sys.stderr)
    sys.exit(1)

print("Windows container builds all registered CTest targets: " + ", ".join(registered_targets))

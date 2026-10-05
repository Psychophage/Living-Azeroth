#!/usr/bin/env python3
"""Compile/run the production population policy without a realm or API credentials."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="playerbots-population-") as directory:
    binary = str(Path(directory) / "population-tests")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "src/Bot/Population"), str(root / "src/Bot/Population/PopulationPolicy.cpp"),
                    str(Path(__file__).with_name("PopulationPolicyTest.cpp")), "-o", binary], check=True)
    subprocess.run([binary], check=True)

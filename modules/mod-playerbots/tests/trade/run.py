#!/usr/bin/env python3
"""Compile/run the trade sharing rules without a realm."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="playerbots-trade-") as directory:
    binary = str(Path(directory) / "trade-tests")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "src/Bot/Trade"), str(root / "src/Bot/Trade/TradeSharesLedger.cpp"),
                    str(Path(__file__).with_name("TradeSharesTest.cpp")), "-o", binary], check=True)
    subprocess.run([binary], check=True)

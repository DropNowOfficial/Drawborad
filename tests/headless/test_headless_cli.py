#!/usr/bin/env python3
"""Behavioral contract for the Linux headless executable."""
import subprocess
import sys

exe = sys.argv[1]
result = subprocess.run([exe, "--help"], text=True, capture_output=True, timeout=5)
assert result.returncode == 0, result.stderr
assert "run" in result.stdout
assert "smoke" in result.stdout

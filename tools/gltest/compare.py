"""Compares the CPU and GPU renders of each preview scene: mean difference
(0-255) and the share of pixels that differ by more than 24 in a channel."""
import sys
from pathlib import Path

from PIL import Image, ImageChops

out = Path(sys.argv[1] if len(sys.argv) > 1 else "tools/gltest/build")
worst = []
for cpu in sorted((out / "cpu").glob("*.ppm")):
    gpu = out / "gpu" / cpu.name
    a, b = Image.open(cpu).convert("RGB"), Image.open(gpu).convert("RGB")
    diff = ImageChops.difference(a, b).convert("L")
    hist = diff.histogram()
    total = sum(hist)
    mean = sum(i * n for i, n in enumerate(hist)) / total
    big = sum(hist[25:]) / total * 100
    worst.append((big, mean, cpu.stem))
for big, mean, name in sorted(worst, reverse=True):
    print(f"{name:34s} mean {mean:5.2f}  pixels off {big:5.2f}%")

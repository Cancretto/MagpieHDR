"""Extract production foreground decisions; substitute only native endpoints.

Generates a CPU harness. Does not install a hook or change native foreground.
"""
from pathlib import Path
import sys

repo = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
source = (repo / "src/Magpie.Core/NvidiaForegroundCompatibility.cpp").read_text(encoding="utf-8-sig")
markers = ("bool Matches(", "bool SourceForeground(", "unsigned CallerIndex(",
           "HWND WINAPI DetourForegroundWindow(", "bool InstallHook(",
           "HWND NvidiaForegroundCompatibility::RealForegroundWindow(", "void NvidiaForegroundCompatibility::FinishRetire(")
blocks = []
for marker in markers:
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    blocks.append(source[start:end])
template = (repo / "tests/NvidiaForegroundDecisionTests.cpp").read_text(encoding="utf-8-sig")
assert template.count("/* PRODUCTION_FOREGROUND_DECISIONS */") == 1
(out / "NvidiaForegroundDecisionTests.cpp").write_text(
    template.replace("/* PRODUCTION_FOREGROUND_DECISIONS */", "\n".join(blocks)), encoding="utf-8")
print("Generated production foreground decision fixture (not compiled or executed).")

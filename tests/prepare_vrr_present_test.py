"""Extract the production Present helper; replace only the COM endpoint type.

Generation is not C++ execution or hardware validation.
"""
from pathlib import Path
import sys

repo = Path(__file__).resolve().parents[1]
output = Path(sys.argv[1])
output.mkdir(parents=True, exist_ok=True)
source = (repo / "src/Magpie.Core/PresenterBase.cpp").read_text(encoding="utf-8-sig")
start = source.index("HRESULT PresenterBase::_PresentWithVRR(")
brace = source.index("{", start)
end, depth = brace + 1, 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
production = source[start:end].replace("IDXGISwapChain4*", "FakeSwapChain*")
template = (repo / "tests/VRRPresentTests.cpp").read_text(encoding="utf-8-sig")
assert template.count("/* PRODUCTION_PRESENT */") == 1
(output / "VRRPresentTests.cpp").write_text(template.replace("/* PRODUCTION_PRESENT */", production), encoding="utf-8")
print("Generated production VRR Present fixture (not compiled or executed).")

"""RTSS source/UI/resource contracts. No compilation or external profile writes."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

repo = Path(__file__).resolve().parents[1]
checks = 0


def check(condition, message):
    global checks
    checks += 1
    assert condition, message


def read(path):
    return (repo / path).read_text(encoding="utf-8-sig")


x = "{http://schemas.microsoft.com/winfx/2006/xaml}"
root = ET.fromstring(read("src/Magpie/ProfilePage.xaml"))
nodes = list(root.iter())
uids = {n.get(x + "Uid"): n for n in nodes if n.get(x + "Uid")}
group = uids["FrameRefresh"]
children = list(group)
check(children.index(uids["Rtss_TargetLimit"]) < children.index(uids["FrameRefresh_Content"]), "RTSS placement")
rtss = uids["Rtss_TargetLimit"]
scope = uids["Rtss_Scope_Input"]
check([n.get(x + "Uid") for n in scope] == ["Rtss_Scope_None", "Rtss_Scope_Global", "Rtss_Scope_Target"], "scope order")
check(uids["Rtss_Rate_Sync"].get("IsEnabled") == "{x:Bind ViewModel.IsRtssSyncAvailable, Mode=OneWay}", "follow-source disables sync")
check(uids["Rtss_CustomRate_Input"].get("Minimum") == "15" and uids["Rtss_CustomRate_Input"].get("Maximum") == "360", "integer editor range")
check("IntegerFormatter" in uids["Rtss_CustomRate_Input"].get("NumberFormatter"), "integer formatter")
check(uids["Rtss_Advanced"].get("IsExpanded") == "False", "advanced collapsed")
for uid in ("Rtss_RateMode", "Rtss_CustomRate", "Rtss_Advanced"):
    check(uids[uid].get("IsEnabled") == "{x:Bind ViewModel.IsRtssManaged, Mode=OneWay}", uid + " unmanaged state")
status = uids["Rtss_Status"]
check("IsEnabled" not in status.attrib, "status/launcher remain available")
check(uids["Rtss_Tools"] in list(status.iter()), "launcher in status row")
check(len([n for n in status.iter() if n.tag.endswith("MenuFlyoutItem")]) == 2, "two distinct launch actions")
check(all(n.get("IsWrapEnabled") == "True" for n in rtss.iter() if n.tag.endswith("SettingsCard") or n.tag.endswith("SettingsExpander")), "wrap all settings rows")

idl = read("src/Magpie/ProfileViewModel.idl")
header = read("src/Magpie/ProfileViewModel.h")
vm = read("src/Magpie/ProfileViewModel.cpp")
for name in ("RtssStatusText", "RtssSyncText", "RtssLaunchText", "AfterburnerLaunchText"):
    getter = vm.split(f"hstring ProfileViewModel::{name}() const {{", 1)[1].split("\n}", 1)[0]
    check("ResourceLoader::GetForCurrentView(CommonSharedConstants::APP_RESOURCE_MAP_ID)" in getter,
          "native profile resource map: " + name)
    check("ResourceLoader::GetForCurrentView()" not in getter, "no missing default resource map: " + name)
members = set(re.findall(r"ViewModel\.(\w+)", ET.tostring(rtss, encoding="unicode")))
for member in members:
    check(re.search(r"\b" + member + r"\b", idl) is not None, "IDL: " + member)
    check(re.search(r"\b" + member + r"\(", header) is not None, "header: " + member)
    check("ProfileViewModel::" + member + "(" in vm, "implementation: " + member)

resource_maps = []
for path in sorted((repo / "src/Magpie").glob("Resources.language-*.resw")):
    data = ET.parse(path).getroot().findall("data")
    resources = {n.get("name"): n.findtext("value") for n in data}
    check(len(resources) == len(data), "duplicate resources in " + path.name)
    rtss_resources = {key: value for key, value in resources.items() if key.startswith("Rtss_")}
    check(len(rtss_resources) >= 59 and all(rtss_resources.values()), "RTSS resources in " + path.name)
    resource_maps.append(rtss_resources)
check(all(set(r) == set(resource_maps[0]) for r in resource_maps), "same resource keys in all languages")
for uid in (n.get(x + "Uid") for n in rtss.iter() if n.get(x + "Uid")):
    check(any(key.startswith(uid + ".") for key in resource_maps[0]), "resource for " + uid)
for key in set(re.findall(r'L"(Rtss_[A-Za-z0-9_]+)"', vm + read("src/Magpie/RtssService.cpp"))):
    check(key in resource_maps[0], "dynamic resource " + key)

settings = read("src/Magpie/AppSettings.cpp")
profile = read("src/Magpie/Profile.h")
service = read("src/Magpie/RtssService.cpp")
access = read("src/Magpie/RtssProfileAccess.cpp")
scaling = read("src/Magpie/ScalingService.cpp")
renderer = read("src/Magpie.Core/Renderer.cpp")
check(settings.count("ReadProfileRtss(") == 2, "default and application load")
check("WriteProfileRtss(writer, profile.rtss)" in settings, "save preferences")
check("rtss = other.rtss" in profile, "copy preferences")
check("ContentTargetChanged(rtssSession, source, fixed, baseRate, constrained)" in scaling, "generation-tagged content target")
check("options.contentTargetChanged(ScalingWindow::Get().SrcTracker().Handle(), fixedContent, _baseFrameRateLimit, constrained)" in renderer, "actual resolved base target")
check("rebound->SameProcess(*session->initialTarget)" in service and "std::atomic<std::shared_ptr<TargetIdentity>>" in service, "same-process HWND rebuild preserves the session baseline")
check("RestoreOriginal" in read("src/Magpie/RtssSettings.h"), "restore default")
check("stillValid()" in access and access.index("if (!stillValid())") < access.index("_CallProfile(_save, profile.c_str())"), "cancel check before save")
check("saveAttempted = true" in access and "else if (saveAttempted &&" in service, "failed readback retains cleanup")
check("readbackComplete && RtssProfileAccess::SameRate(before, written)" in service, "verified unchanged failed writes do not fabricate ownership")
check("current.contents != lastWritten.contents" in access, "new profile deletion ownership")
check("disk.contents != current.contents" in access, "restore concurrent edit guard")
check("_Set(\"AppDetectionLevel\", 1)" in access, "new profile Low")
check("before.exists && (!NonRateEqual" in access, "existing non-rate preservation check")
check("std::chrono::milliseconds(750)" in service and "_thread.detach()" in service, "bounded final shutdown")
check("Recheck(false)" in service and "retry && Status().state != RtssState::Conflict" in service, "launch readiness does not retry failures")
check("session->cancelled = true" in service and "session->finished = true" in service, "stop cancellation and one cleanup")
check("WritePrivateProfile" not in access, "no silent file write fallback")
check('GetProcAddress(_module, "EnumProfiles")' not in access and "GetFileAttributesW(path.c_str())" in access, "reliable existence check without unsafe enumeration")
check("GuardProfile" in access and "GuardProperty" in access and "GuardReload" in access and "_faulted" in access, "native faults quarantined")
check("catch (...) { error = L\"WriteException\"; }" in service, "write exceptions retain save-attempt cleanup")
check("original.fields.contains(key) != after.fields.contains(key)" in access, "restore does not claim a changed expression is identical")
check("timeout && !selectedRunning" in service and "Rtss_Failure_StartTimeout" in service, "launch timeout is a reported failure")

ns = {"msb": "http://schemas.microsoft.com/developer/msbuild/2003"}
project = ET.fromstring(read("src/Magpie/Magpie.vcxproj"))
filters = ET.fromstring(read("src/Magpie/Magpie.vcxproj.filters"))
for tree in (project, filters):
    registered = [n.get("Include") for n in tree.findall(".//msb:ClCompile", ns) + tree.findall(".//msb:ClInclude", ns)]
    for name in ("RtssSettings.h", "ProfileRtss.h", "RtssProfileAccess.h", "RtssProfileAccess.cpp", "RtssService.h", "RtssService.cpp"):
        check(registered.count(name) == 1, "project registration: " + name)

print(f"RTSS source/UI/resource contracts: {checks} checks passed across {len(resource_maps)} languages.")
print("This checks structure only; C++ compilation, API writes, native UI and target FPS remain separate acceptance.")

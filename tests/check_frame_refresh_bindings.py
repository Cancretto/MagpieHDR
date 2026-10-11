"""Check saved-model bindings, localization and frontend scheduling boundaries.

This is a source contract check, not a native UI or GPU acceptance test.
"""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

repo = Path(__file__).resolve().parents[1]
def read(path):
    return (repo / path).read_text(encoding="utf-8-sig")

x = "{http://schemas.microsoft.com/winfx/2006/xaml}"
local = "{using:Magpie}"
profile = ET.fromstring(read("src/Magpie/ProfilePage.xaml"))
group = next(n for n in profile.iter() if n.get(x + "Uid") == "FrameRefresh")
# RTSS has separate source/UI/resource contracts. Keep the original refresh
# contracts scoped to the original controls, including their notifications.
rtss = next((n for n in group if n.get(x + "Uid") == "Rtss_TargetLimit"), None)
if rtss is not None:
    group.remove(rtss)
uids = [n.get(x + "Uid", "") for n in group.iter()]
assert uids.index("FrameRefresh_Content") < uids.index("FrameRefresh_Pacing") < uids.index("FrameRefresh_ContentRate")
assert not any(n.get(x + "Uid") == "FrameRefresh_Advanced" for n in group.iter())
simple = [n for n in group if n.get("Visibility") == "{x:Bind ViewModel.IsSimpleFrameRefresh, Mode=OneWay}"]
assert [n.get(x + "Name") for n in simple] == ["SimpleContentCard", "SimpleIdleCard"]
# The RTSS card was removed above only to scope the existing resource checks.
assert rtss is not None and rtss.get("Visibility") == "{x:Bind ViewModel.IsAdvancedFrameRefresh, Mode=OneWay}"
for uid in ("FrameRefresh_Content", "FrameRefresh_Cursor", "FrameRefresh_VRR", "Home_Advanced_DeveloperOptions_DuplicateFrameDetection", "FrameRefresh_Idle"):
    assert any(n.get(x + "Uid") == uid and n.get("Visibility") == "{x:Bind ViewModel.IsAdvancedFrameRefresh, Mode=OneWay}" for n in group), uid
for n in group.iter():
    if n.get(x + "Uid") == "FrameRefresh_Content_Input":
        assert [c.get(x + "Uid") for c in n] == ["FrameRefresh_Source", "FrameRefresh_Custom"]
assert next(n for n in group.iter() if n.get(x + "Uid") == "FrameRefresh_Pacing_Input").get("IsEnabled") == "{x:Bind ViewModel.IsContentPacingEnabled, Mode=OneWay}"
for uid, property_name in {
    "FrameRefresh_ContentRate": "ShowContentFrameRate",
    "FrameRefresh_CursorSupplement": "ShowCursorSupplement",
    "FrameRefresh_CursorRate": "ShowCursorSupplementRate",
    "FrameRefresh_IdleRate": "ShowIdleRedrawRate",
}.items():
    node = next(n for n in group.iter() if n.get(x + "Uid") == uid)
    assert node.get("Visibility") == f"{{x:Bind ViewModel.{property_name}, Mode=OneWay}}"
assert all(n.get("IsWrapEnabled") == "True" for n in group.iter(local + "SettingsCard"))
for n in group.iter("{using:Microsoft.UI.Xaml.Controls}NumberBox"):
    assert n.get("Minimum") == "15" and n.get("Maximum") == "360"
    assert n.get("SmallChange") == "1"
    assert n.get("NumberFormatter") == "{x:Bind local:App.IntegerFormatter, Mode=OneTime}"
home = read("src/Magpie/HomePage.xaml")
assert "MinFrameRateIndex" not in home and "DefaultFrameRefreshSummary" in home and "EditDefaultFrameRefresh" in home
assert "Profile_Cursor_Refresh" not in read("src/Magpie/ProfilePage.xaml")
assert "Profile_Performance_FrameRateLimiter" not in read("src/Magpie/ProfilePage.xaml")
idl = read("src/Magpie/ProfileViewModel.idl")
header = read("src/Magpie/ProfileViewModel.h")
vm = read("src/Magpie/ProfileViewModel.cpp")
names = set(re.findall(r"ViewModel\.([A-Za-z]+)", ET.tostring(group, encoding="unicode")))
for name in names:
    assert name in idl and name in header and "ProfileViewModel::" + name + "(" in vm, name
    if name != "ResetFrameRefresh":
        assert f'RaisePropertyChanged(L"{name}")' in vm, name
fallback_root = ET.fromstring(read("src/Magpie/Resources.language-en-US.resw"))
fallback = {n.get("name"): n.findtext("value") for n in fallback_root.findall("data")}
for file in (repo / "src/Magpie").glob("Resources.language-*.resw"):
    lang = file.stem.removeprefix("Resources.language-")
    root = ET.fromstring(read(f"src/Magpie/Resources.language-{lang}.resw"))
    resources = {n.get("name"): n.findtext("value") for n in root.findall("data")}
    assert len(resources) == len(root.findall("data")), lang
    assert all(k in resources for k in ("FrameRefresh_Mode.Header", "FrameRefresh_Mode.Description", "FrameRefresh_Mode_Simple.Content", "FrameRefresh_Mode_Advanced.Content"))
    resources = fallback | resources
    for n in group.iter():
        uid = n.get(x + "Uid", "")
        if not uid:
            continue
        if n.tag in (local + "SettingsCard", local + "SettingsExpander", local + "SettingsGroup"):
            assert resources[uid + ".Header"] and resources[uid + ".Description"], (lang, uid)
        elif n.tag.endswith("ToggleSwitch"):
            assert resources[uid + ".OffContent"] and resources[uid + ".OnContent"], (lang, uid)
            assert resources[uid + ".[using:Windows.UI.Xaml.Automation]AutomationProperties.Name"], (lang, uid)
        elif n.tag.endswith(("NumberBox", "ComboBox")):
            assert resources[uid + "_Input.[using:Windows.UI.Xaml.Automation]AutomationProperties.Name" if not uid.endswith("_Input") else uid + ".[using:Windows.UI.Xaml.Automation]AutomationProperties.Name"], (lang, uid)
        else:
            assert resources[uid + ".Content"], (lang, uid)
    for name in ("Home_FrameRefresh_Summary", "Home_FrameRefresh_Edit.Content", "FrameRefresh_RestartNotice", "FrameRefresh_LegacyNotice"):
        assert resources[name], (lang, name)
renderer = read("src/Magpie.Core/Renderer.cpp")
assert "const bool paced = !stableBaseOnly &&" in renderer
assert "else if (!stableBaseOnly) {\n\t\t_frontEdgeClock.Reset();" in renderer
assert "if (submitted && contentFrame && ActiveFrameSyncBackend() == FrameSyncBackend::FrontEdge" in renderer
assert "_frontendPresentedBaseValid && overlayPending &&" in renderer
window = read("src/Magpie.Core/ScalingWindow.cpp")
service = read("src/Magpie/ScalingService.cpp")
assert "ApplyFrameRefreshSettings(_options, frameRefresh, _options.isSimpleFrameRefresh)" in window
assert "ApplyFrameRefreshSettings(options, profile.frameRefresh, options.isSimpleFrameRefresh)" in service
settings = read("src/Magpie/AppSettings.cpp")
assert "_LoadProfile(scaleProfilesArray[0].GetObj(), _defaultProfile, true, legacyParameterFocusSwitching, legacyFrameSync, legacyIdle, migrateLegacyRefresh)" in settings
assert "_LoadProfile(scaleProfilesArray[i].GetObj(), rule, false, legacyParameterFocusSwitching, legacyFrameSync, legacyIdle, migrateLegacyRefresh)" in settings
assert "MergeFrameRefreshSettings(mergedFrameRefresh, request.previousFrameRefresh, request.frameRefresh)" in service
assert "FindProfileByIdentity" in service and "scalingRunId" in service
overlay = read("src/Magpie.Core/OverlayDrawer.cpp")
assert "FrameRefresh_Mode" not in overlay, "Toolbar must not expose the global mode selector"
assert 'L"FrameRefresh_Source/Content", L"FrameRefresh_Custom/Content"' in overlay
assert "const bool simpleRefresh = ScalingWindow::Get().Options().isSimpleFrameRefresh;" in overlay
assert "_parameterSessionSnapshot.simpleFrameRefresh != options.isSimpleFrameRefresh" in overlay
assert "sessionOptions.isSimpleFrameRefresh != settings.IsSimpleFrameRefresh()" in service
assert "_StartScale(request.hwndSource, *frameSyncProfile, sessionOptions.IsWindowedMode(), true);" in service
assert "FrameRefreshModeChanged(auto_revoke" in service
print("Unified refresh UI/model/localization/frontend contracts passed (not native UI/GPU acceptance).")

#include <windows.h>
#undef GetCurrentTime
#include <winrt/Windows.ApplicationModel.Resources.h>
#include <winrt/Windows.ApplicationModel.Resources.Core.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <fmt/xchar.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "CommonSharedConstants.h"
#include "RtssSettings.h"

using namespace winrt;
using namespace Windows::Foundation::Collections;
using namespace Magpie;
namespace Resources = Windows::ApplicationModel::Resources;

// Headless factory adapter: preserve the production overload and map argument,
// but use the native view-independent factory so no full Magpie XAML metadata
// provider is needed. This validates PRI/map selection, not page navigation.
struct ResourceLoader {
	static auto GetForCurrentView() { return Resources::ResourceLoader::GetForViewIndependentUse(); }
	static auto GetForCurrentView(const wchar_t* map) { return Resources::ResourceLoader::GetForViewIndependentUse(map); }
};

// Status inputs are fixtures; the extracted getter bodies resolve strings from
// the real product PRI. No service, process launch or profile write runs.
struct RtssToolInfo { std::wstring executable, version; bool running = false; };
struct RtssService {
	bool pending = false;
	static RtssService& Get() { static RtssService service; return service; }
	bool IsInitializingMagpie() const { return pending; }
};
#include "RtssResourceCases.g.h"
struct ProfileData {
	RtssSettings rtss;
	std::shared_ptr<const uint8_t> runtimeIdentity = std::make_shared<const uint8_t>(uint8_t(0));
};
struct ProfileViewModel {
	ProfileData data;
	ProfileData* _data = &data;
	RtssStatus status;
	bool syncAvailable = true;
	RtssStatus _RtssStatus() const { return status; }
	bool IsRtssManaged() const { return data.rtss.scope != RtssScope::None; }
	bool IsRtssSyncAvailable() const { return syncAvailable; }
	bool ShowRtssGlobalNotice() const { return data.rtss.scope == RtssScope::Global; }
	IVector<hstring> RtssStatusSegments() const;
	hstring RtssStatusText() const;
	hstring RtssSyncText() const;
	hstring RtssLaunchText() const;
	hstring AfterburnerLaunchText() const;
	hstring RtssInitializeText() const;
};
#include "RtssResourceGetters.g.h"
static unsigned checks = 0;
static void Require(bool value, const char* message) {
	++checks;
	if (!value) throw std::runtime_error(message);
}

int main() {
	try {
		std::cout << std::unitbuf;
		init_apartment(apartment_type::multi_threaded);
		bool reproduced = false;
		try { auto incorrect = ResourceLoader::GetForCurrentView(); (void)incorrect; }
		catch (const hresult_error& e) {
			reproduced = uint32_t(e.code().value) == 0x80073B1F;
			std::cout << "Default map HRESULT: 0x" << std::hex << uint32_t(e.code().value) << std::dec << '\n';
		}
		Require(reproduced, "The old default-map call did not reproduce the reported HRESULT");
		for (auto language : {L"en-US", L"zh-Hans", L"zh-Hant"}) {
			Resources::Core::ResourceContext::SetGlobalQualifierValue(L"Language", language);
			auto resources = ResourceLoader::GetForCurrentView(CommonSharedConstants::APP_RESOURCE_MAP_ID);
			for (auto key : ResourceKeys) Require(!resources.GetString(key).empty(), "Empty RTSS resource");
#ifdef RTSS_SOURCE_RESOURCES
			const bool chinese = std::wstring_view(language) == L"zh-Hans";
			Require(resources.GetString(L"Rtss_Rate_Sync/Content") == (chinese ? L"与内容帧率同步" : L"Sync with content frame rate"), "Sync option wording");
			Require(resources.GetString(L"Rtss_Status_Written") == (chinese ? L"配置已更新。" : L"Profile updated."), "Written result wording");
			Require(resources.GetString(L"Rtss_GlobalNotice/Text") == (chinese ? L"使用全局配置的应用共用此上限。" : L"Applications using the global profile share this limit."), "Global factual hint");
			Require(resources.GetString(L"Rtss_TargetOverridesGlobal") == (chinese ? L"目标使用独立配置。" : L"The target uses its own profile."), "Target factual hint");
			Require(resources.GetString(L"Rtss_Failure_Set") == (chinese ? L"设置帧率失败。" : L"Could not set the frame rate."), "Setting failure wording");
			Require(resources.GetString(L"Rtss_Failure_Verify") == (chinese ? L"核对配置失败。" : L"Could not verify the profile."), "Verification failure wording");
			Require(resources.GetString(L"Rtss_Failure_Reload") == (chinese ? L"重载配置失败。" : L"Could not reload the profile."), "Reload failure wording");
			Require(resources.GetString(L"Rtss_Failure_TimeMode") == (chinese ? L"无法切换帧时间模式。" : L"Could not switch from frame time mode."), "Time mode failure wording");
#endif
			ProfileViewModel vm;
			for (bool pending : { false, true }) {
				RtssService::Get().pending = pending;
				Require(vm.RtssInitializeText() == resources.GetString(pending ? L"Rtss_InitializingMagpie" : L"Rtss_InitializeMagpie"), "Initialization button uses native map and busy text");
			}
			for (auto state : States) for (auto scope : {RtssScope::None, RtssScope::Global, RtssScope::Target}) {
				vm.data.rtss.scope = scope;
				vm.status.state = state;
				Require(!vm.RtssStatusText().empty(), "Empty status text");
			}
			vm.status.state = RtssState::Read;
			vm.data.rtss.scope = RtssScope::Global;
			vm.status.rtss.executable = L"RTSS.exe";
			vm.status.profile = L"Global"; vm.status.rtss.version = L"7.3.5.28314";
			vm.status.rateKnown = true; vm.status.currentRate = 60;
			vm.status.failureStage = L"Rtss_Failure_Read";
			const auto segments = vm.RtssStatusSegments();
			Require(segments.GetAt(0) == L"Global" && segments.GetAt(1) == L"\u00a0-\u00a0RTSS 7.3.5.28314", "Profile and version lead the status");
			Require(segments.GetAt(3) == L"\u00a0-\u00a060 FPS" && segments.GetAt(4) == hstring(L"\u00a0-\u00a0" + std::wstring(resources.GetString(L"Rtss_Failure_Read"))), "Rate precedes specific error");
			Require(std::wstring_view(vm.RtssStatusText()).find(L'\n') == std::wstring_view::npos, "Status stays one paragraph");
			vm.status.profile.clear(); vm.status.rtss.version.clear(); vm.status.rateKnown = false; vm.status.failureStage.clear();
			vm.data.rtss.scope = RtssScope::Target;
			Require(vm.RtssStatusSegments().Size() == 1, "Missing object, version and rate are omitted");
			vm.status.profile = L"Global"; vm.status.rtss.executable.clear();
			Require(vm.RtssStatusSegments().Size() == 1, "Uninstalled tool does not fabricate an object");
			vm.status.profile.clear();
			vm.status.rateKnown = true;
			vm.status.detectionDisabled = vm.status.targetOverridesGlobal = true;
			vm.status.failureStage = L"Rtss_Failure_Read";
			for (auto rate : {0., 60., 119.88}) for (bool timeLimit : {false, true}) {
				vm.status.currentRate = rate; vm.status.timeLimit = timeLimit;
				Require(!vm.RtssStatusText().empty(), "Empty rate status");
			}
			vm.syncAvailable = false;
			Require(vm.RtssSyncText() == resources.GetString(L"Rtss_SyncUnavailable"), "Unavailable sync text");
			vm.syncAvailable = true;
			Require(vm.RtssSyncText() == resources.GetString(L"Rtss_CustomAtStart"), "Custom rate text");
			vm.data.rtss.rateMode = RtssRateMode::ContentSync;
			Require(vm.RtssSyncText() == resources.GetString(L"Rtss_SyncAtStart"), "Pending sync text");
			vm.status.profileIdentity = vm.data.runtimeIdentity; vm.status.desiredRate = 60;
			vm.status.contentConstrained = true;
			Require(vm.RtssSyncText() == resources.GetString(L"Rtss_SyncAtStart"), "Active sync description does not repeat the numeric row");
			for (auto toolState : {0, 1, 2}) {
				vm.status.rtss.executable = vm.status.afterburner.executable = toolState ? L"tool.exe" : L"";
				vm.status.rtss.running = vm.status.afterburner.running = toolState == 2;
				const wchar_t* rtssKeys[] = {L"Rtss_LaunchRtssMissing", L"Rtss_LaunchRtss", L"Rtss_LaunchRtssRunning"};
				const wchar_t* abKeys[] = {L"Rtss_LaunchAfterburnerMissing", L"Rtss_LaunchAfterburner", L"Rtss_LaunchAfterburnerRunning"};
				Require(vm.RtssLaunchText() == resources.GetString(rtssKeys[toolState]), "RTSS launch text");
				Require(vm.AfterburnerLaunchText() == resources.GetString(abKeys[toolState]), "Afterburner launch text");
			}
		}
		std::cout << "RTSS native resource text: " << checks << " checks passed across 3 languages.\n";
		return 0;
	} catch (const hresult_error& e) {
		std::cerr << "HRESULT: 0x" << std::hex << uint32_t(e.code().value) << " " << to_string(e.message()) << '\n';
	} catch (const std::exception& e) { std::cerr << e.what() << '\n'; }
	return 1;
}

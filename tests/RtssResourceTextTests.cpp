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
	hstring RtssStatusText() const;
	hstring RtssSyncText() const;
	hstring RtssLaunchText() const;
	hstring AfterburnerLaunchText() const;
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
			ProfileViewModel vm;
			for (auto state : States) for (auto scope : {RtssScope::None, RtssScope::Global, RtssScope::Target}) {
				vm.data.rtss.scope = scope;
				vm.status.state = state;
				Require(!vm.RtssStatusText().empty(), "Empty status text");
			}
			vm.status.state = RtssState::Read;
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
			Require(vm.RtssSyncText() == L"60 FPS\n" + resources.GetString(L"Rtss_ContentConstrained"), "Active sync text");
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

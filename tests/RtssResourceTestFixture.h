#pragma once
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

// Separate hosts validate actual PRI text and real XAML collection binding.
// Neither adapter loads the product App.xbf or starts Magpie/RTSS services.
struct ResourceLoader {
#ifdef RTSS_ITEMS_SOURCE_TEST
	hstring GetString(std::wstring_view key) const { return hstring(key); }
	static ResourceLoader GetForCurrentView(const wchar_t*) { return {}; }
#else
	static auto GetForCurrentView() { return Resources::ResourceLoader::GetForViewIndependentUse(); }
	static auto GetForCurrentView(const wchar_t* map) { return Resources::ResourceLoader::GetForViewIndependentUse(map); }
#endif
};

// Service/status fixtures exercise extracted production getter bodies.
struct RtssToolInfo { std::wstring executable, version; bool running = false; };
#include "RtssResourceCases.g.h"
struct RtssService {
	RtssStatus status;
	bool pending = false, launching = false;
	static RtssService& Get() { static RtssService service; return service; }
	bool IsInitializingMagpie() const { return pending; }
	bool IsLaunching() const { return launching; }
	RtssStatus Status() const { return status; }
};
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
struct StatusViewModel {
	ProfileData data; ProfileData* _data = &data;
	RtssStatus _rtssPreview;
	RtssStatus _RtssStatus() const;
	bool CanInitializeMagpie() const;
};
#include "RtssResourceGetters.g.h"

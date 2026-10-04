// Filled with the production helper by prepare_vrr_present_test.py.
#include "VRRPresentationPolicy.h"
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace Magpie;
struct FakeSwapChain {
	std::vector<HRESULT> results;
	std::vector<UINT> flags;
	HRESULT Present(UINT interval, UINT presentFlags) {
		if (interval != 0) throw std::runtime_error("VRR sync interval must be zero");
		flags.push_back(presentFlags);
		if (flags.size() > results.size()) throw std::runtime_error("Unexpected extra Present retry");
		return results[flags.size() - 1];
	}
};
struct Logger {
	static Logger& Get() { static Logger logger; return logger; }
	void ComWarn(const char*, HRESULT) {}
};
struct PresenterBase {
	VRRPresentationPolicy _vrr;
	unsigned probes = 0, transitions = 0;
	void _RefreshVRREnvironment() { ++probes; }
	void _LogVRRState(const char*) { ++transitions; }
	HRESULT _PresentWithVRR(FakeSwapChain*, bool) noexcept;
	PresenterBase() {
		_vrr.Configure(true, true);
		_vrr.PathAvailable(true); _vrr.Environment(VRRUnavailableReason::None);
		_vrr.Created(_vrr.CreationFlags()); _vrr.AltEnterDisabled(true);
	}
};
/* PRODUCTION_PRESENT */
static void Check(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}
int main() {
	PresenterBase ordinary;
	FakeSwapChain rejected{{DXGI_ERROR_INVALID_CALL, S_OK, S_OK}, {}};
	Check(ordinary._PresentWithVRR(&rejected, true) == S_OK, "Ordinary fallback did not submit once without tearing");
	Check(rejected.flags == std::vector<UINT>{DXGI_PRESENT_ALLOW_TEARING, 0}, "Ordinary fallback flags/order wrong");
	Check(ordinary.transitions == 1 && !ordinary._vrr.PresentFlags(), "Rejection didn't latch for this session");
	Check(ordinary._PresentWithVRR(&rejected, true) == S_OK && rejected.flags.back() == 0,
		"Next frame retried rejected tearing contract");
	PresenterBase sdk;
	FakeSwapChain sdkRejected{{DXGI_ERROR_INVALID_CALL, S_OK}, {}};
	Check(sdk._PresentWithVRR(&sdkRejected, false) == DXGI_ERROR_INVALID_CALL && sdkRejected.flags.size() == 1,
		"SDK proxy double-submitted tagged FG input");
	Check(sdk._PresentWithVRR(&sdkRejected, false) == S_OK && sdkRejected.flags.back() == 0,
		"SDK next-frame fallback failed");
	for (HRESULT failure : {DXGI_ERROR_DEVICE_REMOVED, DXGI_ERROR_DEVICE_RESET, E_OUTOFMEMORY}) {
		PresenterBase device;
		FakeSwapChain lost{{failure}, {}};
		Check(device._PresentWithVRR(&lost, true) == failure && lost.flags.size() == 1 && device.transitions == 0,
			"Non-contract failure incorrectly retried or downgraded as VRR rejection");
	}
	PresenterBase off; off._vrr.Configure(false, true);
	FakeSwapChain noTearing{{DXGI_ERROR_INVALID_CALL}, {}};
	Check(off._PresentWithVRR(&noTearing, true) == DXGI_ERROR_INVALID_CALL && noTearing.flags.size() == 1,
		"Non-tearing contract error was retried");
	PresenterBase occluded;
	FakeSwapChain hidden{{DXGI_STATUS_OCCLUDED}, {}};
	Check(occluded._PresentWithVRR(&hidden, true) == DXGI_STATUS_OCCLUDED && occluded.transitions == 0,
		"Hidden first-frame status was treated as VRR contract failure");
	std::cout << "Production VRR Present helper: bounded fallback, SDK single submission, device-loss and occlusion cases passed\n";
}

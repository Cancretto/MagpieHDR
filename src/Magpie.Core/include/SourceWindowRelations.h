#pragma once
#include <windows.h>

namespace Magpie {

inline bool IsWindowInOwnerChain(HWND window, HWND owner) noexcept {
	for (unsigned i = 0; window && i < 64; ++i, window = GetWindow(window, GW_OWNER)) {
		if (window == owner) return true;
	}
	return false;
}

inline HWND EnabledSourcePopup(HWND source, HWND excluded = nullptr) noexcept {
	auto eligible = [&](HWND popup) {
		return popup && popup != source && IsWindowVisible(popup) && IsWindowEnabled(popup) &&
			!(excluded && IsWindowInOwnerChain(popup, excluded));
	};
	const HWND popup = GetWindow(source, GW_ENABLEDPOPUP);
	if (eligible(popup)) return popup;
	// NativeOwned outputs/input helpers are themselves owned popups. Ignore
	// that branch when finding a real application popup; do not confuse self
	// (the documented no-popup result) with a modal window.
	struct Search { HWND source, excluded, found; } search{ source, excluded, nullptr };
	EnumWindows([](HWND window, LPARAM data) -> BOOL {
		auto& s = *reinterpret_cast<Search*>(data);
		if (GetWindow(window, GW_OWNER) == s.source && IsWindowVisible(window) && IsWindowEnabled(window) &&
			!(s.excluded && IsWindowInOwnerChain(window, s.excluded))) {
			s.found = window;
			return FALSE;
		}
		return TRUE;
	}, reinterpret_cast<LPARAM>(&search));
	return search.found;
}

}

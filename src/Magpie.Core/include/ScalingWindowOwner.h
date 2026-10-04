#pragma once
#include <windows.h>

namespace Magpie {

enum class ScalingWindowOwnerStatus {
	Applied,
	Rejected,
	InvalidWindow,
	InvalidOwner,
	ChildWindow,
	UnexpectedOwner
};

struct ScalingWindowOwnerResult {
	ScalingWindowOwnerStatus status;
	DWORD systemError;
	HWND actualOwner = nullptr;

	bool IsApplied() const noexcept { return status == ScalingWindowOwnerStatus::Applied; }
	bool CanContinueUnowned() const noexcept { return status == ScalingWindowOwnerStatus::Rejected; }
	const char* StatusName() const noexcept {
		switch (status) {
		case ScalingWindowOwnerStatus::Applied: return "applied";
		case ScalingWindowOwnerStatus::Rejected: return "rejected";
		case ScalingWindowOwnerStatus::InvalidWindow: return "invalid-output";
		case ScalingWindowOwnerStatus::InvalidOwner: return "invalid-source";
		case ScalingWindowOwnerStatus::ChildWindow: return "child-output";
		default: return "unexpected-owner";
		}
	}
};

inline ScalingWindowOwnerResult TrySetScalingWindowOwner(HWND window, HWND owner) noexcept {
	if (!IsWindow(window)) {
		return { ScalingWindowOwnerStatus::InvalidWindow, ERROR_INVALID_WINDOW_HANDLE };
	}
	// GWLP_HWNDPARENT on a child changes its parent, not its owner.
	if (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) {
		return { ScalingWindowOwnerStatus::ChildWindow, ERROR_INVALID_PARAMETER };
	}
	if (owner && !IsWindow(owner)) {
		return { ScalingWindowOwnerStatus::InvalidOwner, ERROR_INVALID_WINDOW_HANDLE };
	}

	SetLastError(ERROR_SUCCESS);
	SetWindowLongPtrW(window, GWLP_HWNDPARENT,
		reinterpret_cast<LONG_PTR>(owner));
	const DWORD systemError = GetLastError();
	const HWND actualOwner = GetWindow(window, GW_OWNER);
	// A null readback also occurs for a destroyed HWND. Do not mistake that for
	// successful detachment or a supported unowned session.
	if (!IsWindow(window)) {
		return { ScalingWindowOwnerStatus::InvalidWindow,
			systemError ? systemError : ERROR_INVALID_WINDOW_HANDLE, actualOwner };
	}
	if (owner && !IsWindow(owner)) {
		return { ScalingWindowOwnerStatus::InvalidOwner,
			systemError ? systemError : ERROR_INVALID_WINDOW_HANDLE, actualOwner };
	}
	if (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) {
		return { ScalingWindowOwnerStatus::ChildWindow, ERROR_INVALID_PARAMETER, actualOwner };
	}
	if (actualOwner == owner) {
		return { ScalingWindowOwnerStatus::Applied, systemError, actualOwner };
	}
	if (owner && !actualOwner) {
		// UWP/XAML sources can reject this optional relationship even when both
		// windows are valid. Capture does not require an owner relationship.
		return { ScalingWindowOwnerStatus::Rejected, systemError, actualOwner };
	}
	return { ScalingWindowOwnerStatus::UnexpectedOwner, systemError, actualOwner };
}

inline bool SetScalingWindowOwner(HWND window, HWND owner) noexcept {
	const auto result = TrySetScalingWindowOwner(window, owner);
	SetLastError(result.IsApplied() ? ERROR_SUCCESS : result.systemError);
	return result.IsApplied();
}

}

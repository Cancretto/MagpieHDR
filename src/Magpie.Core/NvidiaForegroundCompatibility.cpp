#include "pch.h"
#include "NvidiaForegroundCompatibility.h"
#include "Logger.h"
#include "StrHelper.h"
#include <tlhelp32.h>
#include <intrin.h>
#if defined(_M_X64) || defined(_M_IX86)
#include <MinHook.h>
#endif

namespace Magpie {
namespace {
using ForegroundFunction = HWND(WINAPI*)();
std::atomic<ForegroundFunction> originalForeground = &::GetForegroundWindow;
thread_local unsigned realQueryDepth = 0;
PublishedForegroundState published;
std::atomic<ForegroundQueryPhase> queryPhase = ForegroundQueryPhase::Preparing;
std::atomic<uint64_t> targetQueries = 0, appliedQueries = 0;
std::array<std::atomic<uint32_t>, size_t(ForegroundQueryPhase::Count)> observedPhases{};

struct QueryRecord {
	uint64_t epoch = 0;
	uintptr_t caller = 0, real = 0, result = 0;
	DWORD thread = 0;
	ForegroundQueryPhase phase{};
	bool target = false;
};
struct QuerySlot {
	// Empty -> writer -> ready -> consumer -> empty; no payload data races.
	std::atomic<unsigned> state = 0;
	QueryRecord value{};
};
std::array<QuerySlot, 64> records;

// Frontend-only control. The detour must never read this object.
struct Control {
	ForegroundCompatibilitySnapshot snapshot{};
	bool requested = false, standardVRR = false, windowed = false;
	bool deviceReady = false, nvidia = false, frameGeneration = false;
	bool potential = false, present = false, suspended = false, retired = true;
	const char* reason = "disabled";
	uint64_t nextPoll = 0, nextModules = 0;
	HWND lastForeground = nullptr;
	bool lastArmed = false;
	uint64_t lastApplied = 0, lastTargets = 0;
	bool referenceActive = false, referenceRestore = false;
	HWND referenceReturn = nullptr;
	uint64_t referenceDeadline = 0;
} control;
bool hookEnabled = false;
#if defined(_M_X64) || defined(_M_IX86)
void* hookTarget = nullptr;
bool hookCreated = false, hookInitialized = false;
#endif

ForegroundWindowIdentity Identity(HWND window) noexcept {
	ForegroundWindowIdentity result;
	result.window = uintptr_t(window);
	result.tid = GetWindowThreadProcessId(window, &result.pid);
	result.atom = GetClassLongPtrW(window, GCW_ATOM);
	return result;
}
bool Matches(const ForegroundWindowIdentity& value) noexcept {
	const HWND window = HWND(value.window);
	DWORD pid = 0;
	return window && IsWindow(window) && value.tid &&
		GetWindowThreadProcessId(window, &pid) == value.tid && pid == value.pid &&
		GetClassLongPtrW(window, GCW_ATOM) == value.atom;
}
bool SourceForeground(const ForegroundCompatibilitySnapshot& value, HWND foreground) noexcept {
	// Capture and root can have different PIDs (UWP). A source popup or our
	// parameter host deliberately suspends the first prototype.
	return foreground && (uintptr_t(foreground) == value.source.window || uintptr_t(foreground) == value.root.window) &&
		Matches(value.source) && Matches(value.root) &&
		GetAncestor(HWND(value.source.window), GA_ROOT) == HWND(value.root.window) &&
		IsWindowVisible(HWND(value.root.window)) && !IsIconic(HWND(value.root.window)) &&
		IsWindowEnabled(HWND(value.root.window));
}
#if defined(_M_X64) || defined(_M_IX86)
unsigned CallerIndex(const ForegroundCompatibilitySnapshot& value, uintptr_t caller) noexcept {
	for (unsigned i = 0; i < value.callers.size(); ++i)
		if (value.callers[i].size && caller >= value.callers[i].begin &&
			caller - value.callers[i].begin < value.callers[i].size) return i;
	return unsigned(value.callers.size());
}
QuerySlot* Record(const ForegroundCompatibilitySnapshot& value, uintptr_t caller, unsigned index, HWND real) noexcept {
	const auto phase = queryPhase.load(std::memory_order_relaxed);
	const uint32_t bit = uint32_t(1) << index;
	if (observedPhases[size_t(phase)].fetch_or(bit, std::memory_order_relaxed) & bit) return nullptr;
	for (auto& slot : records) {
		unsigned empty = 0;
		if (!slot.state.compare_exchange_strong(empty, 1, std::memory_order_acquire)) continue;
		slot.value = { value.epoch, caller, uintptr_t(real), 0, GetCurrentThreadId(), phase,
			index < value.callers.size() };
		return &slot;
	}
	return nullptr;
}
HWND WINAPI DetourForegroundWindow() noexcept {
	const HWND real = originalForeground.load(std::memory_order_acquire)();
	const DWORD error = GetLastError();
	if (realQueryDepth) return real;
	++realQueryDepth;
	ForegroundCompatibilitySnapshot value;
	uint64_t version = 0;
	HWND result = real;
	if (published.Read(value, version) && value.mode != ForegroundCompatibilityMode::Off) {
		const uintptr_t caller = uintptr_t(_ReturnAddress());
		const unsigned index = CallerIndex(value, caller);
		const bool target = index < value.callers.size();
		if (target) targetQueries.fetch_add(1, std::memory_order_relaxed);
		if (value.mode == ForegroundCompatibilityMode::Query && value.armed && target &&
			SourceForeground(value, real) && Matches(value.output) && published.Current(version))
			result = HWND(value.output.window);
		QuerySlot* record = Record(value, caller, index, real);
		// A retiring/rebuilding frontend invalidates an in-flight decision.
		if (!published.Current(version) || (result != real && originalForeground.load(std::memory_order_acquire)() != real))
			result = real;
		if (result != real) appliedQueries.fetch_add(1, std::memory_order_relaxed);
		if (record) {
			record->value.result = uintptr_t(result);
			record->state.store(2, std::memory_order_release);
		}
	}
	--realQueryDepth;
	SetLastError(error);
	return result;
}
#endif
bool InstallHook() noexcept {
#if defined(_M_X64) || defined(_M_IX86)
	MH_STATUS status = MH_OK;
	if (!hookInitialized) {
		status = MH_Initialize();
		if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
			Logger::Get().Warn(fmt::format("G-SYNC foreground: initialize hook failed: {}", MH_StatusToString(status)));
			return false;
		}
		hookInitialized = true;
	}
	if (!hookCreated) {
		void* original = nullptr;
		status = MH_CreateHookApiEx(L"user32.dll", "GetForegroundWindow",
			reinterpret_cast<void*>(&DetourForegroundWindow), &original, &hookTarget);
		if (status != MH_OK) {
			Logger::Get().Warn(fmt::format("G-SYNC foreground: create hook failed: {}", MH_StatusToString(status)));
			return false;
		}
		originalForeground.store(reinterpret_cast<ForegroundFunction>(original), std::memory_order_release);
		hookCreated = true;
	}
	if (!hookEnabled) {
		status = MH_EnableHook(hookTarget);
		if (status != MH_OK) {
			Logger::Get().Warn(fmt::format("G-SYNC foreground: enable hook failed: {}", MH_StatusToString(status)));
			return false;
		}
		hookEnabled = true;
	}
	return true;
#else
	Logger::Get().Warn("G-SYNC foreground: API hook unavailable on this architecture");
	return false;
#endif
}
void RefreshModules() noexcept {
	control.nextModules = GetTickCount64() + 1000;
	// Exact candidate modules, never a filename prefix or all foreign callers.
	// Each actual API hit is reported; membership alone is not driver proof.
	static constexpr std::array names{ L"nvwgf2umx.dll", L"nvwgf2um.dll", L"nvldumdx.dll", L"dxgi.dll", L"d3d11.dll" };
	control.snapshot.callers = {};
	const HANDLE rawSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, 0);
	if (rawSnapshot == INVALID_HANDLE_VALUE) return;
	wil::unique_handle snapshot(rawSnapshot);
	MODULEENTRY32W module{};
	module.dwSize = sizeof(module);
	for (BOOL found = Module32FirstW(snapshot.get(), &module); found; found = Module32NextW(snapshot.get(), &module)) {
		for (size_t i = 0; i < names.size(); ++i) {
			if (_wcsicmp(module.szModule, names[i])) continue;
			HMODULE pinned = nullptr;
			// Prevent a stale address range from identifying a replacement DLL.
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
				reinterpret_cast<LPCWSTR>(module.modBaseAddr), &pinned))
				control.snapshot.callers[i] = { uintptr_t(module.modBaseAddr), module.modBaseSize };
		}
	}
}
void Publish() noexcept {
	control.snapshot.armed = control.snapshot.mode != ForegroundCompatibilityMode::Off && control.standardVRR &&
		!control.retired && !control.suspended && control.deviceReady && control.nvidia &&
		!control.windowed && !control.frameGeneration && control.potential &&
		Matches(control.snapshot.source) && Matches(control.snapshot.root) && Matches(control.snapshot.output);
	published.Publish(control.snapshot);
}
void DrainRecords() noexcept {
	for (auto& slot : records) {
		unsigned ready = 2;
		if (!slot.state.compare_exchange_strong(ready, 3, std::memory_order_acquire)) continue;
		const auto record = slot.value;
		slot.state.store(0, std::memory_order_release);
		HMODULE module = nullptr;
		wchar_t path[MAX_PATH]{};
		std::wstring_view name = L"unknown";
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(record.caller), &module) && GetModuleFileNameW(module, path, MAX_PATH)) {
			name = path;
			if (const auto slash = name.find_last_of(L"\\/"); slash != name.npos) name.remove_prefix(slash + 1);
		}
		Logger::Get().Info(fmt::format(
			"G-SYNC foreground query: epoch={} phase={} module={} offset=0x{:x} thread={} target={} real=0x{:x} returned=0x{:x}",
			record.epoch, uint32_t(record.phase), StrHelper::UTF16ToUTF8(name), record.caller - uintptr_t(module),
			record.thread, record.target, record.real, record.result));
	}
}
void LogState(const char* event) noexcept {
	const HWND real = NvidiaForegroundCompatibility::RealForegroundWindow();
	const auto targets = targetQueries.load(std::memory_order_relaxed), applied = appliedQueries.load(std::memory_order_relaxed);
	DWORD pid = 0;
	GetWindowThreadProcessId(real, &pid);
	Logger::Get().Info(fmt::format(
		"G-SYNC foreground: event={} epoch={} mode={} requested={} installed={} armed={} appliedQueries={} targetQueries={} "
		"active=unknown reason={} real=0x{:x} realPid={} source=0x{:x} root=0x{:x} output=0x{:x} sourceForeground={} "
		"outputOwner=0x{:x} outputExStyle=0x{:x}", event, control.snapshot.epoch, uint32_t(control.snapshot.mode),
		control.requested, hookEnabled, control.snapshot.armed, applied, targets, control.reason, uintptr_t(real), pid,
		control.snapshot.source.window, control.snapshot.root.window, control.snapshot.output.window,
		SourceForeground(control.snapshot, real), uintptr_t(GetWindow(HWND(control.snapshot.output.window), GW_OWNER)),
		uintptr_t(GetWindowLongPtrW(HWND(control.snapshot.output.window), GWL_EXSTYLE))));
	control.lastForeground = real;
	control.lastArmed = control.snapshot.armed;
	control.lastApplied = applied;
	control.lastTargets = targets;
}
void EndReference(bool restore) noexcept {
	const HWND output = HWND(control.snapshot.output.window);
	control.referenceActive = false;
	KillTimer(output, NvidiaForegroundCompatibility::ReferenceTimer);
	if (restore && NvidiaForegroundCompatibility::RealForegroundWindow() == output &&
		SourceForeground(control.snapshot, control.referenceReturn))
		SetForegroundWindow(control.referenceReturn);
	control.referenceRestore = false;
	control.referenceReturn = nullptr;
	control.reason = "reference-ended";
	LogState("reference-end");
}
}

HWND NvidiaForegroundCompatibility::RealForegroundWindow() noexcept {
	++realQueryDepth;
	const HWND result = originalForeground.load(std::memory_order_acquire)();
	--realQueryDepth;
	return result;
}
void NvidiaForegroundCompatibility::Prepare(const SourceWindowIdentity& source, HWND output, uint64_t epoch,
	bool standardVRR, bool requested, bool windowed) noexcept {
	control = {};
	control.retired = false;
	control.requested = requested;
	control.standardVRR = standardVRR;
	control.windowed = windowed;
	control.snapshot.epoch = epoch;
	control.snapshot.source = { uintptr_t(source.capture), source.capturePid, source.captureTid, source.captureClass };
	control.snapshot.root = { uintptr_t(source.root), source.rootPid, source.rootTid, source.rootClass };
	control.snapshot.output = Identity(output);
	control.snapshot.mode = requested ? ForegroundCompatibilityMode::Query : ForegroundCompatibilityMode::Off;
	wchar_t diagnostic[32]{};
	const DWORD length = GetEnvironmentVariableW(L"MAGPIE_GSYNC_DIAGNOSTIC", diagnostic, DWORD(std::size(diagnostic)));
	if (length && length < std::size(diagnostic)) {
		if (!_wcsicmp(diagnostic, L"trace")) control.snapshot.mode = ForegroundCompatibilityMode::Trace;
		else if (!_wcsicmp(diagnostic, L"reference")) control.snapshot.mode = ForegroundCompatibilityMode::Reference;
	}
	control.requested = control.snapshot.mode != ForegroundCompatibilityMode::Off;
	control.reason = control.requested ? "initializing-device" : "disabled";
	if (!standardVRR) {
		control.snapshot.mode = ForegroundCompatibilityMode::Off;
		control.reason = "standard-vrr-disabled";
	} else if (windowed) {
		control.snapshot.mode = ForegroundCompatibilityMode::Off;
		control.reason = "windowed-outside-prototype";
	}
	targetQueries.store(0); appliedQueries.store(0);
	for (auto& mask : observedPhases) mask.store(0);
	Phase(ForegroundQueryPhase::Preparing);
	if (!control.requested || control.snapshot.mode == ForegroundCompatibilityMode::Off) {
		published.Publish(control.snapshot);
		if (control.requested) LogState("prepare");
		return;
	}
	if (control.snapshot.mode != ForegroundCompatibilityMode::Reference) RefreshModules();
	published.Publish(control.snapshot);
	if ((control.snapshot.mode == ForegroundCompatibilityMode::Trace || control.snapshot.mode == ForegroundCompatibilityMode::Query) &&
		!InstallHook()) {
		control.snapshot.mode = ForegroundCompatibilityMode::Off;
		control.reason = "hook-failed";
		published.Publish(control.snapshot);
	}
	if (control.requested) LogState("prepare");
}
void NvidiaForegroundCompatibility::DeviceInitialized(uint32_t vendor, bool hasFrameGeneration) noexcept {
	if (control.retired || !control.requested || control.snapshot.mode == ForegroundCompatibilityMode::Off) return;
	control.deviceReady = true;
	control.nvidia = vendor == 0x10de;
	control.frameGeneration = hasFrameGeneration;
	control.reason = !control.nvidia ? "non-nvidia-output" : hasFrameGeneration ? "fg-outside-prototype" : "initializing-swap-chain";
	if (control.snapshot.mode != ForegroundCompatibilityMode::Reference) RefreshModules();
	Publish();
	LogState("device-initialized");
}
void NvidiaForegroundCompatibility::PresentationState(bool canCreateTearing, bool canPresentTearing) noexcept {
	if (control.retired || !control.requested || control.snapshot.mode == ForegroundCompatibilityMode::Off) return;
	const bool changed = control.potential != canCreateTearing || control.present != canPresentTearing;
	control.potential = canCreateTearing;
	control.present = canPresentTearing;
	if (control.nvidia && !control.frameGeneration && control.snapshot.mode != ForegroundCompatibilityMode::Off)
		control.reason = canCreateTearing ? "candidate-queries-only" : "vrr-path-unavailable";
	if (changed) { Publish(); LogState("presentation-state"); }
}
void NvidiaForegroundCompatibility::Reject(const char* reason) noexcept {
	if (control.retired || !control.requested || control.snapshot.mode == ForegroundCompatibilityMode::Off) return;
	control.snapshot.mode = ForegroundCompatibilityMode::Off;
	control.snapshot.armed = false;
	control.reason = reason;
	published.Publish(control.snapshot);
	LogState("session-rejected");
}
void NvidiaForegroundCompatibility::Phase(ForegroundQueryPhase phase) noexcept {
	if (!control.retired && control.snapshot.mode != ForegroundCompatibilityMode::Off)
		queryPhase.store(phase, std::memory_order_relaxed);
}
void NvidiaForegroundCompatibility::FrontendSuspended(bool suspended) noexcept {
	if (control.retired || !control.requested || control.suspended == suspended) return;
	control.suspended = suspended;
	Publish();
	LogState(suspended ? "frontend-suspend" : "frontend-resume");
}
void NvidiaForegroundCompatibility::OutputShown() noexcept {
	if (!control.requested || control.retired) return;
	LogState("first-output-shown");
	if (control.snapshot.mode != ForegroundCompatibilityMode::Reference || !control.snapshot.armed || !control.present) return;
	const HWND real = RealForegroundWindow();
	if (!SourceForeground(control.snapshot, real)) return;
	control.referenceReturn = real;
	control.referenceDeadline = GetTickCount64() + 10000;
	control.referenceActive = true;
	const HWND output = HWND(control.snapshot.output.window);
	if (!SetTimer(output, ReferenceTimer, 100, nullptr) || !SetForegroundWindow(output)) {
		EndReference(true);
		return;
	}
	LogState("reference-begin-10s");
}
bool NvidiaForegroundCompatibility::ReferenceOwnsForeground() noexcept {
	return control.referenceActive && GetTickCount64() < control.referenceDeadline &&
		Matches(control.snapshot.output) && RealForegroundWindow() == HWND(control.snapshot.output.window);
}
void NvidiaForegroundCompatibility::Poll() noexcept {
	if (!control.requested || control.retired || control.snapshot.mode == ForegroundCompatibilityMode::Off) return;
	if (control.referenceActive && (!ReferenceOwnsForeground() || !control.snapshot.armed)) EndReference(true);
	const uint64_t now = GetTickCount64();
	if (now < control.nextPoll) return;
	control.nextPoll = now + 1000;
	if (now >= control.nextModules && control.snapshot.mode != ForegroundCompatibilityMode::Reference) { RefreshModules(); Publish(); }
	DrainRecords();
	if (RealForegroundWindow() != control.lastForeground || control.lastArmed != control.snapshot.armed ||
		appliedQueries.load() != control.lastApplied || targetQueries.load() != control.lastTargets) LogState("sample");
}
void NvidiaForegroundCompatibility::Retire() noexcept {
	if (control.retired) return;
	control.retired = true;
	control.snapshot.mode = ForegroundCompatibilityMode::Off;
	control.snapshot.armed = false;
	control.referenceRestore = control.referenceActive;
	control.referenceActive = false;
	control.reason = "retired";
	published.Publish(control.snapshot);
}
void NvidiaForegroundCompatibility::ReleaseReferenceFocus() noexcept {
	if (control.referenceRestore) EndReference(true);
}
void NvidiaForegroundCompatibility::FinishRetire() noexcept {
	if (control.requested) { DrainRecords(); LogState("retire-complete"); }
#if defined(_M_X64) || defined(_M_IX86)
	if (hookEnabled) {
		const MH_STATUS status = MH_DisableHook(hookTarget);
		if (status == MH_OK) hookEnabled = false;
		else Logger::Get().Warn(fmt::format("G-SYNC foreground: disable hook failed, passthrough retained: {}", MH_StatusToString(status)));
	}
#endif
	// Keep one disabled trampoline until process exit. UI queries or late UMD
	// workers may still hold it; removing/freeing it here would be unsafe.
}

}

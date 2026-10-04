// Filled with production decisions by prepare_nvidia_foreground_test.py.
// Native metadata and API endpoints are doubles; no live focus or hook changes.
#define NOMINMAX
#include <windows.h>
#include "ForegroundCompatibilityState.h"
#include <cassert>
#include <iostream>
#include <string_view>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <utility>

namespace Magpie {
static const HWND capture = HWND(uintptr_t(1)), root = HWND(uintptr_t(2)), output = HWND(uintptr_t(3)), other = HWND(uintptr_t(4));
static const HWND mainUI = HWND(uintptr_t(5)), parameter = HWND(uintptr_t(6)), popup = HWND(uintptr_t(7));
struct NativeWindow { DWORD pid, tid; uintptr_t atom; HWND root; bool visible = true, enabled = true, iconic = false; HWND owner = nullptr; };
static std::unordered_map<HWND, NativeWindow> native{
	{capture, {10, 11, 101, root}}, {root, {20, 21, 102, root}}, {output, {77, 78, 103, output}},
	{other, {99, 100, 104, other}}, {mainUI, {77, 79, 105, mainUI}},
	{parameter, {77, 78, 106, parameter, true, true, false, output}},
	{popup, {20, 21, 107, popup, true, true, false, root}}
};
static std::atomic<HWND> fakeForeground = root;
static std::atomic<bool> changeDuringIdentity = false;
static bool retireDuringRecord = false;
static thread_local uintptr_t fakeCaller = 0x1010;
static BOOL FakeIsWindow(HWND window) { return native.contains(window); }
static DWORD FakeThread(HWND window, DWORD* pid) {
	const auto it = native.find(window);
	SetLastError(1234);
	if (it == native.end()) return 0;
	if (pid) *pid = it->second.pid;
	return it->second.tid;
}
static ULONG_PTR FakeAtom(HWND window, int) {
	if (window == output && changeDuringIdentity.exchange(false)) fakeForeground = other;
	return native.at(window).atom;
}
static HWND FakeAncestor(HWND window, UINT) { return native.at(window).root; }
static BOOL FakeVisible(HWND window) { return native.at(window).visible; }
static BOOL FakeEnabled(HWND window) { return native.at(window).enabled; }
static BOOL FakeIconic(HWND window) { return native.at(window).iconic; }
static HWND WINAPI FakeForeground() { SetLastError(73); return fakeForeground.load(); }
static void* FakeCaller() { return reinterpret_cast<void*>(fakeCaller); }
static HWND FakeOwner(HWND window, UINT relation) {
	const auto it = native.find(window);
	return relation == GW_OWNER && it != native.end() ? it->second.owner : nullptr;
}
static std::wstring fakeDiagnostic;
static DWORD FakeEnvironment(const wchar_t*, wchar_t* buffer, DWORD size) {
	if (fakeDiagnostic.size() >= size) return DWORD(fakeDiagnostic.size() + 1);
	std::copy(fakeDiagnostic.begin(), fakeDiagnostic.end(), buffer);
	buffer[fakeDiagnostic.size()] = 0;
	return DWORD(fakeDiagnostic.size());
}

#define IsWindow FakeIsWindow
#define GetWindowThreadProcessId FakeThread
#define GetClassLongPtrW FakeAtom
#define GetAncestor FakeAncestor
#define IsWindowVisible FakeVisible
#define IsWindowEnabled FakeEnabled
#define IsIconic FakeIconic
#define _ReturnAddress FakeCaller
#define GetWindow FakeOwner
#define GetCurrentProcessId() 77
#define GetEnvironmentVariableW FakeEnvironment
}
#include "include/SourceWindowRelations.h"
namespace Magpie {
struct SourceWindowIdentity { HWND capture, root; DWORD capturePid, captureTid; uintptr_t captureClass;
	DWORD rootPid, rootTid; uintptr_t rootClass; };
using ForegroundFunction = HWND(WINAPI*)();
static std::atomic<ForegroundFunction> originalForeground = &FakeForeground;
static thread_local unsigned realQueryDepth = 0;
static PublishedForegroundState published;
static std::atomic<uint64_t> targetQueries = 0, appliedQueries = 0;
static std::atomic<ForegroundQueryPhase> queryPhase = ForegroundQueryPhase::Preparing;
static std::array<std::atomic<uint32_t>, size_t(ForegroundQueryPhase::Count)> observedPhases{};
/* PRODUCTION_QUERY_RECORDS */
QuerySlot* RecordCandidate(const ForegroundCompatibilitySnapshot&, uintptr_t, unsigned, HWND) noexcept;
static QuerySlot* Record(const ForegroundCompatibilitySnapshot& value, uintptr_t caller, unsigned index, HWND real) {
	if (retireDuringRecord) { published.Publish({}); retireDuringRecord = false; }
	return RecordCandidate(value, caller, index, real);
}
struct Logger { static Logger& Get() { static Logger value; return value; } void Warn(std::string_view) {} };
namespace fmt { template<class... T> const char* format(const char* value, T...) { return value; } }
enum MH_STATUS { MH_OK, MH_ERROR_ALREADY_INITIALIZED, MH_ERROR_ALREADY_CREATED, MH_ERROR_MEMORY_PROTECT };
static MH_STATUS initializeResult = MH_OK, createResult = MH_OK, enableResult = MH_OK, disableResult = MH_OK;
static unsigned initializeCalls = 0, createCalls = 0, enableCalls = 0, disableCalls = 0;
static void* hookTarget = nullptr;
static bool hookCreated = false, hookEnabled = false, hookInitialized = false;
static MH_STATUS MH_Initialize() { ++initializeCalls; return initializeResult; }
static MH_STATUS MH_CreateHookApiEx(const wchar_t*, const char*, void*, void** original, void** target) {
	++createCalls; *original = reinterpret_cast<void*>(&FakeForeground); *target = reinterpret_cast<void*>(uintptr_t(0x100));
	return createResult;
}
static MH_STATUS MH_EnableHook(void* target) { assert(target == hookTarget && target); ++enableCalls; return enableResult; }
static MH_STATUS MH_DisableHook(void* target) { assert(target == hookTarget && target); ++disableCalls; return disableResult; }
static const char* MH_StatusToString(MH_STATUS) { return "fake-status"; }
/* PRODUCTION_FOREGROUND_CONTROL */
static void RefreshModules() { control.snapshot.callers[0] = {0x1000, 0x100}; }
static void DrainRecords() {}
static void LogState(const char*) {}
struct NvidiaForegroundCompatibility {
	static HWND RealForegroundWindow() noexcept;
	static void Prepare(const SourceWindowIdentity&, HWND, uint64_t, bool, bool, const SessionWindowIdentity&) noexcept;
	static void DeviceInitialized(uint32_t, uint32_t, int32_t, ForegroundPresentationPath, uint32_t) noexcept;
	static bool BindPresentationWindow(HWND) noexcept;
	static void PresentationState(bool, bool) noexcept;
	static void Reject(const char*) noexcept;
	static void FrontendSuspended(bool) noexcept;
	static void MainUIChanged(const SessionWindowIdentity&) noexcept;
	static void Phase(ForegroundQueryPhase) noexcept;
	static void Retire() noexcept;
	static void FinishRetire() noexcept;
};
/* PRODUCTION_FOREGROUND_DECISIONS */

static ForegroundCompatibilitySnapshot Session() {
	ForegroundCompatibilitySnapshot value;
	value.epoch = 1;
	value.mode = ForegroundCompatibilityMode::Query;
	value.armed = true;
	value.source = {uintptr_t(capture), 10, 11, 101};
	value.root = {uintptr_t(root), 20, 21, 102};
	value.output = {uintptr_t(output), 77, 78, 103};
	value.outputRoot = value.output;
	value.mainUI = {uintptr_t(mainUI), 77, 79, 105};
	value.parameterHost = {uintptr_t(parameter), 77, 78, 106};
	value.callers[0] = {0x1000, 0x100};
	return value;
}
}
int main() {
	using namespace Magpie;
	initializeResult = MH_ERROR_MEMORY_PROTECT;
	assert(!InstallHook() && !hookInitialized && !createCalls && !enableCalls);
	initializeResult = MH_ERROR_ALREADY_INITIALIZED; createResult = MH_ERROR_ALREADY_CREATED;
	assert(!InstallHook() && hookInitialized && !hookCreated && !enableCalls);
	createResult = MH_OK; enableResult = MH_ERROR_MEMORY_PROTECT;
	assert(!InstallHook() && hookCreated && !hookEnabled);
	const auto previousCreateCalls = createCalls;
	enableResult = MH_OK;
	assert(InstallHook() && hookEnabled && createCalls == previousCreateCalls);
	const auto previousEnableCalls = enableCalls;
	assert(InstallHook() && enableCalls == previousEnableCalls);
	disableResult = MH_ERROR_MEMORY_PROTECT;
	NvidiaForegroundCompatibility::FinishRetire(); assert(hookEnabled);
	disableResult = MH_OK;
	NvidiaForegroundCompatibility::FinishRetire(); assert(!hookEnabled && hookCreated);
	assert(NvidiaForegroundCompatibility::RealForegroundWindow() == root);
	const SourceWindowIdentity source{capture, root, 10, 11, 101, 20, 21, 102};
	const auto ui = RegisterSessionWindow(mainUI);
	uint64_t epoch = 10;
	auto start = [&](ForegroundPresentationPath path, bool windowed = false, uint32_t vendor = 0x10de) {
		fakeDiagnostic.clear();
		NvidiaForegroundCompatibility::Prepare(source, output, ++epoch, true, windowed, ui);
		assert(!control.snapshot.armed);
		NvidiaForegroundCompatibility::DeviceInitialized(vendor, 123, -4, path,
			path == ForegroundPresentationPath::DLSSFrontendDXGI ? 4u : path == ForegroundPresentationPath::XeSSProxy ? 2u : 1u);
		NvidiaForegroundCompatibility::PresentationState(true, false);
	};
	for (auto path : {ForegroundPresentationPath::OrdinaryDXGI, ForegroundPresentationPath::DLSSFrontendDXGI,
		ForegroundPresentationPath::XeSSProxy}) for (bool windowed : {false, true}) {
		start(path, windowed);
		assert(control.snapshot.armed && control.path == path && control.luidLow == 123 && control.luidHigh == -4);
		assert(DetourForegroundWindow() == output);
		assert(NvidiaForegroundCompatibility::BindPresentationWindow(output));
		NvidiaForegroundCompatibility::PresentationState(true, true);
		assert(control.snapshot.armed && control.present);
		NvidiaForegroundCompatibility::FrontendSuspended(true);
		assert(!control.snapshot.armed && DetourForegroundWindow() == root);
		NvidiaForegroundCompatibility::FrontendSuspended(false);
		assert(control.snapshot.armed && DetourForegroundWindow() == output);
		for (HWND foreground : {mainUI, parameter, popup, other, HWND(nullptr)}) {
			fakeForeground = foreground;
			assert(DetourForegroundWindow() == foreground);
		}
		fakeForeground = root;
		assert(DetourForegroundWindow() == output);
		NvidiaForegroundCompatibility::PresentationState(false, false);
		assert(!control.snapshot.armed && DetourForegroundWindow() == root);
		NvidiaForegroundCompatibility::PresentationState(true, true);
		assert(control.snapshot.armed && DetourForegroundWindow() == output);
		NvidiaForegroundCompatibility::Retire();
		assert(DetourForegroundWindow() == root);
	}
	start(ForegroundPresentationPath::XeSSProxy, false, 0x8086);
	assert(!control.snapshot.armed && DetourForegroundWindow() == root);
	for (auto path : {ForegroundPresentationPath::Unknown, ForegroundPresentationPath::Composition}) {
		start(path); assert(!control.snapshot.armed && DetourForegroundWindow() == root);
	}
	start(ForegroundPresentationPath::XeSSProxy);
	NvidiaForegroundCompatibility::MainUIChanged({});
	assert(!control.snapshot.mainUI.window && control.snapshot.armed);
	NvidiaForegroundCompatibility::MainUIChanged(ui);
	assert(control.snapshot.mainUI.window == uintptr_t(mainUI) && control.snapshot.armed);
	auto staleUI = ui; ++staleUI.atom;
	NvidiaForegroundCompatibility::MainUIChanged(staleUI);
	assert(!control.snapshot.mainUI.window);
	NvidiaForegroundCompatibility::MainUIChanged(RegisterSessionWindow(other));
	assert(!control.snapshot.mainUI.window);
	NvidiaForegroundCompatibility::MainUIChanged(ui);
	assert(!NvidiaForegroundCompatibility::BindPresentationWindow(mainUI));
	assert(control.snapshot.mode == ForegroundCompatibilityMode::Off && DetourForegroundWindow() == root);
	for (const wchar_t* diagnostic : {L"standard", L"trace", L"reference"}) {
		fakeDiagnostic = diagnostic;
		NvidiaForegroundCompatibility::Prepare(source, output, ++epoch, true, false, ui);
		NvidiaForegroundCompatibility::DeviceInitialized(0x10de, 0, 0, ForegroundPresentationPath::XeSSProxy, 2);
		NvidiaForegroundCompatibility::PresentationState(true, true);
		assert(DetourForegroundWindow() == root);
	}
	fakeDiagnostic.clear();
	NvidiaForegroundCompatibility::Prepare(source, output, ++epoch, false, false, ui);
	NvidiaForegroundCompatibility::DeviceInitialized(0x10de, 0, 0, ForegroundPresentationPath::DLSSFrontendDXGI, 4);
	assert(control.path == ForegroundPresentationPath::DLSSFrontendDXGI && control.multiplier == 4);
	assert(!control.snapshot.armed && DetourForegroundWindow() == root);
	NvidiaForegroundCompatibility::Retire();
	auto value = Session(); published.Publish(value);
	for (auto& slot : records) slot.state = 0;
	for (auto& slot : unknownQueries) { slot.page = 0; slot.phases = 0; }
	queryPhase = ForegroundQueryPhase::Present;
	const auto unknownIndex = unsigned(value.callers.size());
	auto firstUnknown = RecordCandidate(value, 0x9000, unknownIndex, root);
	assert(firstUnknown && !firstUnknown->value.target && std::string_view(firstUnknown->value.role) == "source");
	firstUnknown->state = 2;
	assert(!RecordCandidate(value, 0x9008, unknownIndex, root));
	auto secondUnknown = RecordCandidate(value, 0xA000, unknownIndex, root);
	assert(secondUnknown && !secondUnknown->value.target); secondUnknown->state = 2;
	queryPhase = ForegroundQueryPhase::FirstPresent;
	auto nextPhase = RecordCandidate(value, 0x9000, unknownIndex, root);
	assert(nextPhase); nextPhase->state = 2;
	for (auto [foreground, role] : {std::pair{root, "source"}, {output, "output"}, {mainUI, "magpie-main-ui"},
		{parameter, "parameter-host"}, {popup, "source-popup"}, {other, "external"}, {HWND(nullptr), "none"}})
		assert(std::string_view(ForegroundRole(value, foreground)) == role);
	assert(DetourForegroundWindow() == output && GetLastError() == 73);
	assert(NvidiaForegroundCompatibility::RealForegroundWindow() == root && GetLastError() == 73);
	fakeCaller = 0x9999;
	assert(DetourForegroundWindow() == root);
	fakeCaller = 0x1010;
	for (HWND window : {capture, root}) { fakeForeground = window; assert(DetourForegroundWindow() == output); }
	for (HWND window : {other, HWND(nullptr), output}) { fakeForeground = window; assert(DetourForegroundWindow() == window); }
	fakeForeground = root;
	for (auto mode : {ForegroundCompatibilityMode::Off, ForegroundCompatibilityMode::Trace, ForegroundCompatibilityMode::Reference}) {
		value.mode = mode; published.Publish(value); assert(DetourForegroundWindow() == root);
	}
	value = Session(); value.armed = false; published.Publish(value);
	assert(DetourForegroundWindow() == root);
	value = Session(); published.Publish(value);
	// Startup may be hidden, but shown outputs must pause during hiding/minimization.
	native[output].visible = false;
	assert(DetourForegroundWindow() == output);
	value.outputShown = true; published.Publish(value);
	assert(DetourForegroundWindow() == root);
	native[output].visible = true; native[output].iconic = true;
	assert(DetourForegroundWindow() == root);
	native[output].iconic = false;
	assert(DetourForegroundWindow() == output);
	value = Session(); published.Publish(value);
	const auto saved = native[output];
	native[output].pid = 88; assert(DetourForegroundWindow() == root); native[output] = saved;
	native[output].tid = 89; assert(DetourForegroundWindow() == root); native[output] = saved;
	native[output].atom = 90; assert(DetourForegroundWindow() == root); native[output] = saved;
	native[root].iconic = true; assert(DetourForegroundWindow() == root); native[root].iconic = false;
	native[root].enabled = false; assert(DetourForegroundWindow() == root); native[root].enabled = true;
	native[capture].root = other; assert(DetourForegroundWindow() == root); native[capture].root = root;
	changeDuringIdentity = true;
	assert(DetourForegroundWindow() == root && fakeForeground == other);
	fakeForeground = root; retireDuringRecord = true;
	assert(DetourForegroundWindow() == root);
	published.Publish(Session());
	appliedQueries = 0;
	std::vector<std::thread> threads;
	for (int i = 0; i < 4; ++i) threads.emplace_back([] {
		for (int j = 0; j < 20000; ++j) assert(DetourForegroundWindow() == output);
	});
	for (auto& thread : threads) thread.join();
	assert(appliedQueries == 80000);

	// Exercise the actual publication implementation with concurrent readers.
	PublishedForegroundState concurrent;
	std::atomic<bool> finished = false;
	std::atomic<unsigned> accepted = 0;
	threads.clear();
	for (int i = 0; i < 4; ++i) threads.emplace_back([&] {
		while (!finished.load()) {
			ForegroundCompatibilitySnapshot current; uint64_t version;
			if (!concurrent.Read(current, version) || !current.epoch) continue;
			assert(current.source.window == current.epoch * 3 && current.output.window == current.epoch * 7);
			assert(current.armed == bool(current.epoch & 1));
			++accepted;
		}
	});
	for (uint64_t epoch = 1; epoch < 10000; ++epoch) {
		ForegroundCompatibilitySnapshot current;
		current.epoch = epoch; current.source.window = uintptr_t(epoch * 3); current.output.window = uintptr_t(epoch * 7);
		current.armed = bool(epoch & 1); concurrent.Publish(current);
	}
	finished = true;
	for (auto& thread : threads) thread.join();
	ForegroundCompatibilitySnapshot final; uint64_t version;
	assert(concurrent.Read(final, version) && final.epoch == 9999);
	concurrent.Publish({}); assert(!concurrent.Current(version));
	std::cout << "Production foreground decisions: ordinary/DLSS/XeSS qualification, windowed mode, focus roles, proxy binding, diagnostics, retirement, HWND reuse, LastError, concurrent readers passed\n";
}

// Filled with production decisions by prepare_nvidia_foreground_test.py.
// Native metadata and API endpoints are doubles; no live focus or hook changes.
#define NOMINMAX
#include <windows.h>
#include "ForegroundCompatibilityState.h"
#include <cassert>
#include <iostream>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Magpie {
static const HWND capture = HWND(uintptr_t(1)), root = HWND(uintptr_t(2)), output = HWND(uintptr_t(3)), other = HWND(uintptr_t(4));
struct NativeWindow { DWORD pid, tid; uintptr_t atom; HWND root; bool visible = true, enabled = true, iconic = false; };
static std::unordered_map<HWND, NativeWindow> native{
	{capture, {10, 11, 101, root}}, {root, {20, 21, 102, root}}, {output, {77, 78, 103, output}},
	{other, {99, 100, 104, other}}
};
static std::atomic<HWND> foreground = root;
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
	if (window == output && changeDuringIdentity.exchange(false)) foreground = other;
	return native.at(window).atom;
}
static HWND FakeAncestor(HWND window, UINT) { return native.at(window).root; }
static BOOL FakeVisible(HWND window) { return native.at(window).visible; }
static BOOL FakeEnabled(HWND window) { return native.at(window).enabled; }
static BOOL FakeIconic(HWND window) { return native.at(window).iconic; }
static HWND WINAPI FakeForeground() { SetLastError(73); return foreground.load(); }
static void* FakeCaller() { return reinterpret_cast<void*>(fakeCaller); }

#define IsWindow FakeIsWindow
#define GetWindowThreadProcessId FakeThread
#define GetClassLongPtrW FakeAtom
#define GetAncestor FakeAncestor
#define IsWindowVisible FakeVisible
#define IsWindowEnabled FakeEnabled
#define IsIconic FakeIconic
#define _ReturnAddress FakeCaller
using ForegroundFunction = HWND(WINAPI*)();
static std::atomic<ForegroundFunction> originalForeground = &FakeForeground;
static thread_local unsigned realQueryDepth = 0;
static PublishedForegroundState published;
static std::atomic<uint64_t> targetQueries = 0, appliedQueries = 0;
struct QuerySlot { struct Value { uintptr_t result = 0; } value; std::atomic<unsigned> state = 1; };
static QuerySlot* Record(const ForegroundCompatibilitySnapshot&, uintptr_t, unsigned, HWND) {
	if (retireDuringRecord) { published.Publish({}); retireDuringRecord = false; }
	return nullptr;
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
static struct Control { bool requested = false; } control;
static void DrainRecords() {}
static void LogState(const char*) {}
struct NvidiaForegroundCompatibility {
	static HWND RealForegroundWindow() noexcept;
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
	auto value = Session(); published.Publish(value);
	assert(DetourForegroundWindow() == output && GetLastError() == 73);
	assert(NvidiaForegroundCompatibility::RealForegroundWindow() == root && GetLastError() == 73);
	fakeCaller = 0x9999;
	assert(DetourForegroundWindow() == root);
	fakeCaller = 0x1010;
	for (HWND window : {capture, root}) { foreground = window; assert(DetourForegroundWindow() == output); }
	for (HWND window : {other, HWND(nullptr), output}) { foreground = window; assert(DetourForegroundWindow() == window); }
	foreground = root;
	for (auto mode : {ForegroundCompatibilityMode::Off, ForegroundCompatibilityMode::Trace, ForegroundCompatibilityMode::Reference}) {
		value.mode = mode; published.Publish(value); assert(DetourForegroundWindow() == root);
	}
	value = Session(); value.armed = false; published.Publish(value);
	assert(DetourForegroundWindow() == root);
	value = Session(); published.Publish(value);
	const auto saved = native[output];
	native[output].pid = 88; assert(DetourForegroundWindow() == root); native[output] = saved;
	native[output].tid = 89; assert(DetourForegroundWindow() == root); native[output] = saved;
	native[output].atom = 90; assert(DetourForegroundWindow() == root); native[output] = saved;
	native[root].iconic = true; assert(DetourForegroundWindow() == root); native[root].iconic = false;
	native[root].enabled = false; assert(DetourForegroundWindow() == root); native[root].enabled = true;
	native[capture].root = other; assert(DetourForegroundWindow() == root); native[capture].root = root;
	changeDuringIdentity = true;
	assert(DetourForegroundWindow() == root && foreground == other);
	foreground = root; retireDuringRecord = true;
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
	std::cout << "Production foreground decisions/publication/hook failures: bypass, module scope, UWP root/capture, retirement, HWND reuse, LastError, concurrent readers passed\n";
}

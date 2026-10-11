#include "pch.h"
#include "DlssgSm86Runtime.h"
#include "DlssgSm86Policy.h"
#include "DlssgSm86Package.h"
#include "Logger.h"
#include "Win32Helper.h"
#include "StrHelper.h"
#include <mutex>
#include <shlobj.h>
#include <psapi.h>

namespace Magpie::DlssgSm86Runtime {

#ifdef MP_ENABLE_DLSS_FRAME_GENERATION
namespace {

std::mutex runtimeMutex;
bool attempted = false;
HMODULE proxy = nullptr;
LUID physicalAdapter{};
// Intentionally retained until process exit. Unloading the proxy is not safe.
HANDLE proxyFile = INVALID_HANDLE_VALUE;
HANDLE configFile = INVALID_HANDLE_VALUE;

bool QueryArchitecture(const LUID& wanted, int& major, int& minor, int& driver) {
	// Load only the installed driver. No CUDA context, SDK runtime or toolkit
	// deployment is required for these stable CUDA Driver API entry points.
	wil::unique_hmodule cuda(LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
	if (!cuda) return false;
	using Init = int(WINAPI*)(unsigned int);
	using GetCount = int(WINAPI*)(int*);
	using GetDevice = int(WINAPI*)(int*, int);
	using GetLuid = int(WINAPI*)(char*, unsigned int*, int);
	using GetAttribute = int(WINAPI*)(int*, int, int);
	auto init = reinterpret_cast<Init>(GetProcAddress(cuda.get(), "cuInit"));
	auto count = reinterpret_cast<GetCount>(GetProcAddress(cuda.get(), "cuDeviceGetCount"));
	auto device = reinterpret_cast<GetDevice>(GetProcAddress(cuda.get(), "cuDeviceGet"));
	auto luid = reinterpret_cast<GetLuid>(GetProcAddress(cuda.get(), "cuDeviceGetLuid"));
	auto attribute = reinterpret_cast<GetAttribute>(GetProcAddress(cuda.get(), "cuDeviceGetAttribute"));
	auto driverVersion = reinterpret_cast<GetCount>(GetProcAddress(cuda.get(), "cuDriverGetVersion"));
	if (!init || !count || !device || !luid || !attribute || !driverVersion || init(0) != 0) return false;
	int n = 0;
	if (count(&n) != 0) return false;
	for (int ordinal = 0; ordinal < n; ++ordinal) {
		int dev = 0;
		LUID candidate{};
		unsigned int mask = 0;
		if (device(&dev, ordinal) != 0 ||
			luid(reinterpret_cast<char*>(&candidate), &mask, dev) != 0 ||
			!mask || candidate.HighPart != wanted.HighPart || candidate.LowPart != wanted.LowPart) continue;
		// CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR/MINOR (75/76).
		return attribute(&major, 75, dev) == 0 && attribute(&minor, 76, dev) == 0 && driverVersion(&driver) == 0;
	}
	return false;
}

bool CacheFile(const std::filesystem::path& source, const std::filesystem::path& destination,
	const DlssgSm86Package::File& expected) {
	if (PinnedFile::Verify(destination, expected.size, expected.sha256)) return true;
	// Per-process temporary + atomic rename. Never overwrite an in-use proxy.
	auto temporary = destination;
	temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
	if (!CopyFileW(source.c_str(), temporary.c_str(), FALSE)) return false;
	const bool ok = PinnedFile::Verify(temporary, expected.size, expected.sha256) &&
		MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	DeleteFileW(temporary.c_str());
	// Another instance can have installed the same immutable pair meanwhile.
	return ok || PinnedFile::Verify(destination, expected.size, expected.sha256);
}

bool PrepareImpl(IDXGIAdapter* adapter) {
	DXGI_ADAPTER_DESC desc{};
	if (!adapter || FAILED(adapter->GetDesc(&desc)) || desc.VendorId != 0x10de) return false;
	std::lock_guard lock(runtimeMutex);
	if (attempted) {
		if (!proxy) return false;
		if (desc.AdapterLuid.HighPart == physicalAdapter.HighPart &&
			desc.AdapterLuid.LowPart == physicalAdapter.LowPart) return true;
		Logger::Get().Error("DLSSFG rendering adapter changed after process-wide proxy load; restart Magpie before using DLSSFG");
		return false;
	}
	int major = 0, minor = 0, driver = 0;
	if (!QueryArchitecture(desc.AdapterLuid, major, minor, driver)) {
		Logger::Get().Error("DLSSFG cannot match rendering adapter LUID to a CUDA device; community FG unavailable");
		return false;
	}
	const auto route = DlssgSm86Policy::SelectRoute(major, minor);
	if (route == DlssgSm86Policy::Route::Unsupported) {
		Logger::Get().Warn(fmt::format("DLSSFG has no validated routing policy for physical SM {}.{}", major, minor));
		return false;
	}
	attempted = true;
	physicalAdapter = desc.AdapterLuid;
	std::array<HMODULE, 1024> loaded{};
	DWORD needed = 0;
	if (!K32EnumProcessModules(GetCurrentProcess(), loaded.data(), static_cast<DWORD>(sizeof(loaded)), &needed) ||
		needed > sizeof(loaded)) return false;
	for (size_t i = 0; i < needed / sizeof(HMODULE); ++i) {
		if (GetProcAddress(loaded[i], "DlssgProxy_Name")) {
			Logger::Get().Error("Another DLSSG proxy loaded before the managed runtime; refusing ambiguous configuration");
			return false;
		}
	}
	const auto root = Win32Helper::GetExePath().parent_path();
	if (!DlssgSm86Package::Verify(root)) {
		Logger::Get().Error("DLSSFG community package missing or checksum mismatch; reinstall the complete matching package");
		return false;
	}
	wil::unique_cotaskmem_string local;
	const HRESULT localResult = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, local.put());
	if (FAILED(localResult)) {
		Logger::Get().ComError("DLSSFG cache directory unavailable", localResult);
		return false;
	}
	const bool forward = route == DlssgSm86Policy::Route::ForwardJit;
	// Include proxy + configuration identities. An update never edits loaded
	// code/configuration, and a read-only installation remains supported.
	const auto& ini = DlssgSm86Package::Files[forward ? 2 : 1];
	const auto& binary = DlssgSm86Package::Files[0];
	const auto cache = std::filesystem::path(local.get()) / L"Magpie/DlssgSm86" /
		binary.sha256 / ini.sha256;
	std::filesystem::create_directories(cache);
	const auto source = root / DlssgSm86Package::Directory;
	if (!CacheFile(source / binary.name, cache / binary.name, binary) ||
		!CacheFile(source / ini.name, cache / L"dlssg_sm86.ini", ini)) {
		Logger::Get().Error("DLSSFG cache copy/integrity check failed; cached proxy was not loaded");
		return false;
	}
	proxyFile = PinnedFile::Open(cache / binary.name);
	configFile = PinnedFile::Open(cache / L"dlssg_sm86.ini");
	if (!PinnedFile::Verify(proxyFile, binary.size, binary.sha256) ||
		!PinnedFile::Verify(configFile, ini.size, ini.sha256)) {
		Logger::Get().Error("DLSSFG cache cannot be locked and verified before loading");
		return false;
	}
	proxy = LoadLibraryExW((cache / binary.name).c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!proxy) {
		Logger::Get().Win32Error("DLSSFG community proxy load failed; no native FG fallback");
		return false;
	}
	Logger::Get().Info(fmt::format(
		"DLSSFG proxy loaded: package={} revision={} sha256={} PCI={:04x}:{:04x} "
		"LUID={:08x}:{:08x} physicalSM={}.{} cudaDriverApi={} policy={} "
		"Optimized=1 maxGenerated=3 SpoofArchToGame=0 path={} "
		"(kernel execution not yet verified; inspect loader/backend logs)",
		DlssgSm86Package::Version, DlssgSm86Package::Revision, binary.sha256,
		desc.VendorId, desc.DeviceId, static_cast<uint32_t>(desc.AdapterLuid.HighPart), desc.AdapterLuid.LowPart,
		major, minor, driver, forward ? "forward-PTX" : "native-family",
		StrHelper::UTF16ToUTF8(cache.wstring())));
	return true;
}

} // namespace

bool Prepare(IDXGIAdapter* adapter) noexcept {
	try { return PrepareImpl(adapter); }
	catch (...) {
		Logger::Get().Error("DLSSFG community proxy preparation failed; restart after repairing the package/cache");
		return false;
	}
}

bool VerifyLoadedPair() noexcept {
	try {
		std::lock_guard lock(runtimeMutex);
		if (!proxy) return false;
		std::array<HMODULE, 1024> modules{};
		DWORD needed = 0;
		if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(),
			static_cast<DWORD>(sizeof(modules)), &needed) || needed > sizeof(modules)) return false;
		bool foundRuntime = false, foundBackend = false;
		for (size_t i = 0; i < needed / sizeof(HMODULE); ++i) {
			std::wstring path(32768, L'\0');
			const DWORD length = GetModuleFileNameW(modules[i], path.data(), static_cast<DWORD>(path.size()));
			if (!length || length >= path.size()) continue;
			path.resize(length);
			const auto name = std::filesystem::path(path).filename().wstring();
			const bool isRuntime = _wcsnicmp(name.c_str(), L"nvngx_dlssg", 11) == 0;
			const bool isBackend = _wcsnicmp(name.c_str(), L"sm86_backend", 12) == 0;
			if (!isRuntime && !isBackend) continue;
			const auto& expected = isRuntime ? DlssgSm86Package::Runtime : DlssgSm86Package::Backend;
			if (!PinnedFile::Verify(path, expected.size, expected.sha256)) {
				Logger::Get().Error(fmt::format("DLSSFG loaded an unexpected runtime/backend: {}", StrHelper::UTF16ToUTF8(path)));
				return false;
			}
			foundRuntime |= isRuntime;
			foundBackend |= isBackend;
			Logger::Get().Info(fmt::format("DLSSFG module identity verified: path={} sha256={}",
				StrHelper::UTF16ToUTF8(path), expected.sha256));
		}
		if (!foundRuntime || !foundBackend) {
			Logger::Get().Error("DLSSFG bundled runtime/backend pair not loaded; refusing unverified/native fallback");
			return false;
		}
		return true;
	} catch (...) { return false; }
}
#else
bool Prepare(IDXGIAdapter*) noexcept { return false; }
bool VerifyLoadedPair() noexcept { return false; }
#endif

}

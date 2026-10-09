#include "pch.h"
#include "RtssProfileAccess.h"
#include "Win32Helper.h"
#include <charconv>
#include <sstream>
#include <memory>
#include <tlhelp32.h>
#include <shellapi.h>
#include <ShlObj.h>
#pragma comment(lib, "Version.lib")

namespace Magpie {
namespace {

constexpr std::array<const char*, 4> RATE_KEYS{
	"framerate/limit", "framerate/limitdenominator", "framerate/limittime", "framerate/limittimedenominator" };

// Keep SEH boundaries in POD-only functions, separate from C++ transactions.
bool GuardProfile(void (*function)(LPCSTR), LPCSTR profile, bool& faulted) noexcept {
	__try { function(profile); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { faulted = true; return false; }
}

BOOL GuardProperty(BOOL (*function)(LPCSTR, LPBYTE, DWORD), LPCSTR name,
	LPBYTE value, DWORD size, bool& faulted) noexcept {
	__try { return function(name, value, size); }
	__except (EXCEPTION_EXECUTE_HANDLER) { faulted = true; return FALSE; }
}

bool GuardReload(void (*function)(), bool& faulted) noexcept {
	__try { function(); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { faulted = true; return false; }
}

std::string Lower(std::string value) {
	for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
	return value;
}

std::string Trim(std::string value) {
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return {};
	return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

bool NonRateEqual(const RtssProfileSnapshot& a, const RtssProfileSnapshot& b) {
	auto stripped = [](auto fields) {
		for (const auto key : RATE_KEYS) fields.erase(key);
		fields.erase("info/timestamp");
		return fields;
	};
	return stripped(a.fields) == stripped(b.fields);
}

bool IsRunning(const std::filesystem::path& executable) {
	if (executable.empty()) return false;
	wil::unique_handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
	if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE) return false;
	PROCESSENTRY32W entry{ .dwSize = sizeof(entry) };
	if (!Process32FirstW(snapshot.get(), &entry)) return false;
	do {
		if (_wcsicmp(entry.szExeFile, executable.filename().c_str()) != 0) continue;
		wil::unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
		std::wstring path(32768, L'\0');
		DWORD size = DWORD(path.size());
		if (process && QueryFullProcessImageNameW(process.get(), 0, path.data(), &size)) {
			path.resize(size);
			if (_wcsicmp(path.c_str(), executable.c_str()) == 0) return true;
		}
	} while (Process32NextW(snapshot.get(), &entry));
	return false;
}

std::wstring ReadToolVersion(const std::filesystem::path& path) {
	DWORD ignored = 0;
	const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
	if (!size || size > 1024 * 1024) return {};
	std::vector<uint8_t> bytes(size);
	if (!GetFileVersionInfoW(path.c_str(), 0, size, bytes.data())) return {};
	VS_FIXEDFILEINFO* info = nullptr;
	UINT infoSize = 0;
	if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) ||
		infoSize < sizeof(VS_FIXEDFILEINFO)) return {};
	return fmt::format(L"{}.{}.{}.{}", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
		HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
}

std::filesystem::path RegistryInstall(bool afterburner) {
	// Search both registry views, including per-user installations.
	const auto key = afterburner ? L"SOFTWARE\\Unwinder\\MSI Afterburner" : L"SOFTWARE\\Unwinder\\RTSS";
	for (HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER }) {
		for (DWORD view : { KEY_WOW64_32KEY, KEY_WOW64_64KEY }) {
			wil::unique_hkey handle;
			if (RegOpenKeyExW(root, key, 0, KEY_QUERY_VALUE | view, handle.put()) != ERROR_SUCCESS) continue;
			for (const auto name : { L"InstallPath", L"InstallDir" }) {
				wchar_t value[32768]{};
				DWORD size = sizeof(value), type = 0;
				if (RegQueryValueExW(handle.get(), name, nullptr, &type, reinterpret_cast<BYTE*>(value), &size) != ERROR_SUCCESS ||
					(type != REG_SZ && type != REG_EXPAND_SZ) || size >= sizeof(value)) continue;
				std::wstring resolved(value);
				if (type == REG_EXPAND_SZ) {
					std::wstring expanded(32768, L'\0');
					const DWORD count = ExpandEnvironmentStringsW(value, expanded.data(), DWORD(expanded.size()));
					if (!count || count > expanded.size()) continue;
					expanded.resize(count - 1); resolved = std::move(expanded);
				}
				std::filesystem::path path(resolved);
				if (_wcsicmp(path.extension().c_str(), L".exe") != 0) path /= afterburner ? L"MSIAfterburner.exe" : L"RTSS.exe";
				if (Win32Helper::FileExists(path.c_str())) return path;
			}
		}
	}
	return {};
}

}

RtssToolInfo RtssProfileAccess::DetectTool(bool afterburner) {
	RtssToolInfo result;
	result.executable = RegistryInstall(afterburner);
	if (result.executable.empty()) {
		for (const auto folder : { FOLDERID_ProgramFilesX86, FOLDERID_ProgramFiles }) {
			wil::unique_cotaskmem_string path;
			if (FAILED(SHGetKnownFolderPath(folder, 0, nullptr, path.put()))) continue;
			const auto candidate = std::filesystem::path(path.get()) /
				(afterburner ? L"MSI Afterburner\\MSIAfterburner.exe" : L"RivaTuner Statistics Server\\RTSS.exe");
			if (Win32Helper::FileExists(candidate.c_str())) { result.executable = candidate; break; }
		}
	}
	if (!result.executable.empty()) {
		result.version = ReadToolVersion(result.executable);
		result.running = IsRunning(result.executable);
	}
	return result;
}

bool RtssProfileAccess::LaunchTool(const RtssToolInfo& tool, std::wstring& error) {
	if (tool.executable.empty()) { error = L"ToolNotInstalled"; return false; }
	if (IsRunning(tool.executable)) return true;
	const auto folder = tool.executable.parent_path();
	SHELLEXECUTEINFOW info{ .cbSize = sizeof(info), .fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC,
		.lpVerb = L"open", .lpFile = tool.executable.c_str(), .lpDirectory = folder.c_str(), .nShow = SW_SHOWMINNOACTIVE };
	if (!ShellExecuteExW(&info)) { error = std::to_wstring(GetLastError()); return false; }
	if (info.hProcess) CloseHandle(info.hProcess);
	return true;
}

RtssProfileAccess::~RtssProfileAccess() { _Unload(); }

void RtssProfileAccess::_Unload() {
	// Quarantine a faulted module until process exit rather than invoke its DllMain again.
	if (_module && !_faulted) FreeLibrary(_module);
	_module = nullptr;
	_load = nullptr; _save = nullptr; _delete = nullptr;
	_get = nullptr; _set = nullptr; _update = nullptr;
	_directory.clear();
}

bool RtssProfileAccess::Initialize(const RtssToolInfo& tool, std::wstring& error) {
	_version = tool.version;
	_ResetFailure("");
	if (_faulted) { error = L"InterfaceFault"; return false; }
	if (tool.executable.empty()) { error = L"NotInstalled"; return false; }
	if (_module && _directory == tool.executable.parent_path()) return true;
	_Unload();
#if defined(_M_ARM64)
	error = L"UnsupportedArchitecture";
	return false;
#else
	_directory = tool.executable.parent_path();
#if defined(_WIN64)
	const auto dll = _directory / L"RTSSHooks64.dll";
#else
	const auto dll = _directory / L"RTSSHooks.dll";
#endif
	if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
		const DWORD code = GetLastError();
		error = code == ERROR_ACCESS_DENIED ? L"AccessDenied" :
			(code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) ? L"LibraryMissing" : L"LoadFailed: " + std::to_wstring(code);
		_Unload(); return false;
	}
	_module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!_module) {
		const DWORD code = GetLastError();
		error = code == ERROR_BAD_EXE_FORMAT ? L"UnsupportedArchitecture" : code == ERROR_ACCESS_DENIED ?
			L"AccessDenied" : L"LoadFailed: " + std::to_wstring(code);
		_Unload(); return false;
	}
	_load = reinterpret_cast<ProfileFn>(GetProcAddress(_module, "LoadProfile"));
	_save = reinterpret_cast<ProfileFn>(GetProcAddress(_module, "SaveProfile"));
	_delete = reinterpret_cast<ProfileFn>(GetProcAddress(_module, "DeleteProfile"));
	_get = reinterpret_cast<PropertyFn>(GetProcAddress(_module, "GetProfileProperty"));
	_set = reinterpret_cast<PropertyFn>(GetProcAddress(_module, "SetProfileProperty"));
	_update = reinterpret_cast<void (*)()>(GetProcAddress(_module, "UpdateProfiles"));
	if (!_load || !_save || !_delete || !_get || !_set || !_update) {
		error = L"UnsupportedInterface"; _Unload(); return false;
	}
	if (!_CallProfile(_load, "")) { error = L"InterfaceFault"; return false; }
	uint32_t value = 0;
	if (!_Get("FramerateLimit", value) || !_Get("FramerateLimitDenominator", value) ||
		!_Get("FrametimeLimit", value) || !_Get("AppDetectionLevel", value)) {
		error = _faulted ? L"InterfaceFault" : L"UnsupportedInterface"; _Unload(); return false;
	}
	return true;
#endif
}

bool RtssProfileAccess::_Get(const char* property, uint32_t& value) {
	const bool result = !_faulted && GuardProperty(_get, property, reinterpret_cast<BYTE*>(&value), sizeof(value), _faulted) != FALSE;
	_failure.nativeFault = _faulted;
	if (!result) { _failure.property = property; _failure.observedKnown = false; }
	return result;
}

bool RtssProfileAccess::_Set(const char* property, uint32_t value) {
	return !_faulted && GuardProperty(_set, property, reinterpret_cast<BYTE*>(&value), sizeof(value), _faulted) != FALSE;
}

void RtssProfileAccess::_ResetFailure(const std::string& profile) {
	_failure = {};
	_failure.profile = profile;
	_failure.version = _version;
	_failure.nativeFault = _faulted;
}

bool RtssProfileAccess::_Fail(RtssFailureStage stage, const wchar_t* reason, std::wstring& error) {
	_failure.stage = stage;
	_failure.nativeFault = _faulted;
	error = _faulted ? L"InterfaceFault" : reason;
	return false;
}

bool RtssProfileAccess::_SetIfChanged(const char* property, uint32_t value, std::wstring& error) {
	_failure.property = property;
	_failure.requested = value;
	_failure.observedKnown = _Get(property, _failure.observed);
	if (!_failure.observedKnown) return _Fail(RtssFailureStage::Setting, L"ReadPropertyFailed", error);
	// Get can return values the setter rejects, including FrametimeLimit=0 in 7.3.5.
	if (_failure.observed != value) {
		if (!_Set(property, value)) return _Fail(RtssFailureStage::Setting, L"SetPropertyFailed", error);
		_failure.observedKnown = _Get(property, _failure.observed);
		if (!_failure.observedKnown || _failure.observed != value) {
			return _Fail(RtssFailureStage::Setting, L"SetReadbackFailed", error);
		}
	}
	_failure.property.clear();
	_failure.observedKnown = false;
	_failure.requested = _failure.observed = 0;
	return true;
}

bool RtssProfileAccess::_VerifyProperty(const char* property, uint32_t expected, uint32_t actual, std::wstring& error) {
	if (expected == actual) return true;
	_failure.property = property;
	_failure.requested = expected;
	_failure.observed = actual;
	_failure.observedKnown = true;
	return _Fail(RtssFailureStage::Verify, L"VerifyFailed", error);
}

bool RtssProfileAccess::_CallProfile(ProfileFn function, const char* profile) {
	const bool result = !_faulted && GuardProfile(function, profile, _faulted);
	_failure.nativeFault = _faulted;
	return result;
}

bool RtssProfileAccess::_Reload() {
	return !_faulted && GuardReload(_update, _faulted);
}

std::filesystem::path RtssProfileAccess::_ProfilePath(const std::string& profile) const {
	// RTSS profile names are ANSI. The caller rejects lossy process-name conversion.
	const int count = MultiByteToWideChar(CP_ACP, 0, profile.c_str(), -1, nullptr, 0);
	if (!count) return {};
	std::wstring name(size_t(count), L'\0');
	MultiByteToWideChar(CP_ACP, 0, profile.c_str(), -1, name.data(), count);
	name.resize(size_t(count) - 1);
	return _directory / L"Profiles" / (profile.empty() ? L"Global" : name + L".cfg");
}

bool RtssProfileAccess::_Exists(const std::string& profile, bool& exists, std::wstring& error) {
	// Do not call EnumProfiles: a read-only probe of this installation's x64
	// export raised an access violation. RTSS stores both presets and user
	// profiles in Profiles/<exe>.cfg; existence is distinct from Load fallback.
	const auto path = _ProfilePath(profile);
	if (path.empty()) { error = L"InvalidProfileName"; return false; }
	const DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes != INVALID_FILE_ATTRIBUTES) {
		if (attributes & FILE_ATTRIBUTE_DIRECTORY) { error = L"InvalidProfileFile"; return false; }
		exists = true; return true;
	}
	const DWORD code = GetLastError();
	if (code == ERROR_FILE_NOT_FOUND) { exists = false; return true; }
	_failure.fileError = code; error = L"ReadFailed: " + std::to_wstring(code);
	return false;
}

bool RtssProfileAccess::_ReadFile(const std::string& profile, RtssProfileSnapshot& snapshot, std::wstring& error) {
	const auto path = _ProfilePath(profile);
	if (path.empty()) { error = L"InvalidProfileName"; return false; }
	const HANDLE raw = CreateFileW(path.c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (raw == INVALID_HANDLE_VALUE) {
		_failure.fileError = GetLastError(); error = L"ReadFailed: " + std::to_wstring(_failure.fileError); return false;
	}
	const std::unique_ptr<void, decltype(&CloseHandle)> file(raw, CloseHandle);
	LARGE_INTEGER size{};
	if (!GetFileSizeEx(file.get(), &size)) {
		_failure.fileError = GetLastError(); error = L"ReadFailed: " + std::to_wstring(_failure.fileError); return false;
	}
	if (size.QuadPart < 0 || size.QuadPart > 1024 * 1024) { error = L"ProfileTooLarge"; return false; }
	std::string bytes(size_t(size.QuadPart), '\0');
	DWORD count = 0;
	if (!ReadFile(file.get(), bytes.data(), DWORD(bytes.size()), &count, nullptr)) {
		_failure.fileError = GetLastError(); error = L"ReadFailed: " + std::to_wstring(_failure.fileError); return false;
	}
	if (count != bytes.size()) { error = L"ProfileChangedWhileReading"; return false; }
	LARGE_INTEGER afterSize{};
	if (!GetFileSizeEx(file.get(), &afterSize)) {
		_failure.fileError = GetLastError(); error = L"ReadFailed: " + std::to_wstring(_failure.fileError); return false;
	}
	if (afterSize.QuadPart != size.QuadPart) { error = L"ProfileChangedWhileReading"; return false; }
	snapshot.contents = bytes;
	std::stringstream lines(bytes);
	std::string line, section;
	snapshot.fields.clear();
	while (std::getline(lines, line)) {
		line = Trim(line);
		if (line.empty() || line[0] == ';' || line[0] == '#') continue;
		if (line.front() == '[' && line.back() == ']') { section = Lower(Trim(line.substr(1, line.size() - 2))); continue; }
		const auto split = line.find('=');
		if (split == std::string::npos) continue;
		const auto key = section + '/' + Lower(Trim(line.substr(0, split)));
		if (!snapshot.fields.emplace(key, Trim(line.substr(split + 1))).second) {
			error = L"DuplicateProfileField"; return false;
		}
	}
	snapshot.rate = { 0, 1, 0, 1 };
	for (size_t i = 0; i < RATE_KEYS.size(); ++i) {
		const auto it = snapshot.fields.find(RATE_KEYS[i]);
		if (it == snapshot.fields.end()) continue;
		uint32_t value = 0;
		const auto& text = it->second;
		const auto [end, result] = std::from_chars(text.data(), text.data() + text.size(), value);
		if (result != std::errc{} || end != text.data() + text.size() || ((i == 1 || i == 3) && !value)) {
			error = L"InvalidRateExpression"; return false;
		}
		snapshot.rate[i] = value;
	}
	return true;
}

bool RtssProfileAccess::Read(const std::string& profile, RtssProfileSnapshot& snapshot, std::wstring& error) {
	_ResetFailure(profile);
	_failure.stage = RtssFailureStage::Read;
	const bool success = _Read(profile, snapshot, error);
	if (success) _failure.stage = RtssFailureStage::None;
	return success;
}

bool RtssProfileAccess::_Read(const std::string& profile, RtssProfileSnapshot& snapshot, std::wstring& error) {
	snapshot = {};
	if (!_module) { error = L"NotInitialized"; return false; }
	if (!_Exists(profile, snapshot.exists, error)) return false;
	if (profile.empty() && !snapshot.exists) { error = L"GlobalProfileMissing"; return false; }
	if (!_CallProfile(_load, snapshot.exists ? profile.c_str() : "")) { error = L"InterfaceFault"; return false; }
	if (!_ReadFile(snapshot.exists ? profile : "", snapshot, error)) return false;
	uint32_t limit = 0, denominator = 0, time = 0;
	if (!_Get("FramerateLimit", limit) || !_Get("FramerateLimitDenominator", denominator) ||
		!_Get("FrametimeLimit", time) || !_Get("AppDetectionLevel", snapshot.detectionLevel)) {
		error = _faulted ? L"InterfaceFault" : L"ReadPropertyFailed"; _failure.nativeFault = _faulted; return false;
	}
	const std::array<uint32_t, 3> properties{ limit, denominator, time };
	if (!denominator) { error = L"InvalidRateExpression"; return false; }
	for (size_t i = 0; i < properties.size(); ++i) {
		if (snapshot.fields.contains(RATE_KEYS[i]) && snapshot.rate[i] != properties[i]) {
			error = L"ProfileChangedWhileReading"; return false;
		}
		// Partial profiles may omit defaults; use the interface's effective value.
		snapshot.rate[i] = properties[i];
	}
	return true;
}

bool RtssProfileAccess::_CheckWriteAccess(const std::string& profile, bool exists, std::wstring& error) {
	// Open only existing objects: this checks the caller's token without changing
	// a profile or creating a test file in the user's RTSS directory.
	const auto path = exists ? _ProfilePath(profile) : _directory / L"Profiles";
	const HANDLE handle = CreateFileW(path.c_str(), exists ? GENERIC_WRITE : FILE_ADD_FILE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		exists ? FILE_ATTRIBUTE_NORMAL : FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); return true; }
	_failure.fileError = GetLastError();
	return _Fail(RtssFailureStage::Save, L"WriteAccessFailed", error);
}

bool RtssProfileAccess::Apply(const std::string& profile, uint32_t rate, const RtssProfileSnapshot& before,
	RtssProfileSnapshot& written, bool& saveAttempted, bool& readbackComplete,
	std::wstring& error, const std::function<bool()>& stillValid) {
	_ResetFailure(profile);
	written = {};
	saveAttempted = false; readbackComplete = false;
	_failure.stage = RtssFailureStage::Read;
	RtssProfileSnapshot current;
	if (!_Read(profile, current, error)) return false;
	if (current != before) return _Fail(RtssFailureStage::Conflict, L"Conflict", error);
	if (!rate || rate > 1000) return _Fail(RtssFailureStage::Setting, L"InvalidRate", error);
	if (!_CheckWriteAccess(profile, before.exists, error)) return false;
	if (!_SetIfChanged("FrametimeLimit", 0, error)) {
		// A positive FPS property does not prove that RTSS has left time mode.
		if (error == L"SetPropertyFailed" && current.rate[2]) error = L"TimeModeUnsupported";
		return false;
	}
	if (!_SetIfChanged("FramerateLimitDenominator", 1, error) ||
		!_SetIfChanged("FramerateLimit", rate, error) ||
		(!before.exists && !_SetIfChanged("AppDetectionLevel", 1, error))) return false;
	_failure.stage = RtssFailureStage::Read;
	RtssProfileSnapshot disk;
	if (!_ReadFile(before.exists ? profile : "", disk, error)) return false;
	if (disk.contents != before.contents) return _Fail(RtssFailureStage::Conflict, L"Conflict", error);
	if (!before.exists) {
		bool exists = false;
		if (!_Exists(profile, exists, error)) return false;
		if (exists) return _Fail(RtssFailureStage::Conflict, L"Conflict", error);
	}
	if (!stillValid()) return _Fail(RtssFailureStage::Cancelled, L"Cancelled", error);
	saveAttempted = _failure.saveAttempted = true;
	if (!_CallProfile(_save, profile.c_str())) return _Fail(RtssFailureStage::Save, L"SaveFailed", error);
	// SaveProfile is void. Capture the actual state even when verification fails.
	_failure.stage = RtssFailureStage::Verify;
	if (!_Read(profile, written, error)) { _failure.nativeFault = _faulted; return false; }
	readbackComplete = _failure.readbackComplete = true;
	if (!written.exists) return _Fail(RtssFailureStage::Save, L"SaveFailed", error);
	if (written == before && (before.rate[0] != rate || before.rate[1] != 1 || before.rate[2] != 0)) {
		return _Fail(RtssFailureStage::Save, L"SaveFailed", error);
	}
	if (!_VerifyProperty("FramerateLimit", rate, written.rate[0], error) ||
		!_VerifyProperty("FramerateLimitDenominator", 1, written.rate[1], error) ||
		!_VerifyProperty("FrametimeLimit", 0, written.rate[2], error) ||
		!_VerifyProperty("AppDetectionLevel", before.exists ? before.detectionLevel : 1, written.detectionLevel, error)) return false;
	if (before.exists && !NonRateEqual(before, written)) {
		return _Fail(RtssFailureStage::Verify, L"VerifyFailed", error);
	}
	if (!_Reload()) return _Fail(RtssFailureStage::Reload, L"ReloadFailed", error);
	_failure.stage = RtssFailureStage::None;
	return true;
}

bool RtssProfileAccess::Restore(const std::string& profile, const RtssProfileSnapshot& original,
	const RtssProfileSnapshot& lastWritten, bool created, bool& conflict, std::wstring& error) {
	_ResetFailure(profile);
	_failure.stage = RtssFailureStage::Restore;
	conflict = false;
	RtssProfileSnapshot current;
	if (!_Read(profile, current, error)) { _failure.nativeFault = _faulted; return false; }
	if (created && !current.exists) { _failure.stage = RtssFailureStage::None; return true; }
	if (!SameRate(current, lastWritten) || (created && (current.contents != lastWritten.contents || !NonRateEqual(current, lastWritten) ||
		current.detectionLevel != lastWritten.detectionLevel))) {
		conflict = true; return _Fail(RtssFailureStage::Conflict, L"Conflict", error);
	}
	if (created) {
		RtssProfileSnapshot confirmed;
		if (!_Read(profile, confirmed, error)) return false;
		if (!confirmed.exists || confirmed.contents != lastWritten.contents) {
			conflict = true; return _Fail(RtssFailureStage::Conflict, L"Conflict", error);
		}
		_failure.saveAttempted = true;
		if (!_CallProfile(_delete, profile.c_str())) return _Fail(RtssFailureStage::Restore, L"DeleteFailed", error);
		RtssProfileSnapshot after;
		_failure.stage = RtssFailureStage::Verify;
		if (!_Read(profile, after, error)) return false;
		_failure.readbackComplete = true;
		if (after.exists) return _Fail(RtssFailureStage::Restore, L"DeleteFailed", error);
		if (!_Reload()) return _Fail(RtssFailureStage::Reload, L"ReloadFailed", error);
		_failure.stage = RtssFailureStage::None;
		return true;
	}
	// Preserve current non-rate fields. No silent file fallback for unsupported expressions.
	if (current.rate[3] != original.rate[3]) return _Fail(RtssFailureStage::Restore, L"TimeDenominatorChanged", error);
	if (!_CheckWriteAccess(profile, true, error)) return false;
	if (!_SetIfChanged("FramerateLimitDenominator", original.rate[1], error) ||
		!_SetIfChanged("FrametimeLimit", original.rate[2], error) ||
		!_SetIfChanged("FramerateLimit", original.rate[0], error)) return false;
	_failure.stage = RtssFailureStage::Restore;
	RtssProfileSnapshot disk;
	if (!_ReadFile(profile, disk, error)) return false;
	if (disk.contents != current.contents) { conflict = true; return _Fail(RtssFailureStage::Conflict, L"Conflict", error); }
	_failure.saveAttempted = true;
	if (!_CallProfile(_save, profile.c_str())) return _Fail(RtssFailureStage::Save, L"RestoreSaveFailed", error);
	RtssProfileSnapshot after;
	_failure.stage = RtssFailureStage::Verify;
	if (!_Read(profile, after, error)) return false;
	_failure.readbackComplete = true;
	if (!after.exists || !NonRateEqual(current, after)) {
		return _Fail(RtssFailureStage::Verify, L"RestoreVerifyFailed", error);
	}
	constexpr std::array<const char*, 4> properties{
		"FramerateLimit", "FramerateLimitDenominator", "FrametimeLimit", "LimitTimeDenominator" };
	for (size_t i = 0; i < properties.size(); ++i) {
		if (!_VerifyProperty(properties[i], original.rate[i], after.rate[i], error)) return false;
	}
	if (!_VerifyProperty("AppDetectionLevel", current.detectionLevel, after.detectionLevel, error)) return false;
	for (const auto key : RATE_KEYS) {
		if (original.fields.contains(key) != after.fields.contains(key)) {
			return _Fail(RtssFailureStage::Verify, L"RestoreExpressionChanged", error);
		}
	}
	if (!_Reload()) return _Fail(RtssFailureStage::Reload, L"ReloadFailed", error);
	_failure.stage = RtssFailureStage::None;
	return true;
}

}

#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Magpie {

struct RtssToolInfo {
	std::filesystem::path executable;
	std::wstring version;
	bool running = false;
};

struct RtssProfileSnapshot {
	// Complete rate expression, including the time denominator not exposed by this SDK.
	std::array<uint32_t, 4> rate{ 0, 1, 0, 1 };
	std::map<std::string, std::string> fields;
	std::string contents;
	bool exists = false;
	uint32_t detectionLevel = 0;
	bool operator==(const RtssProfileSnapshot&) const = default;
};

// Worker-thread only. The DLL maintains a current loaded profile, so callers
// must serialize complete transactions, not just individual exported calls.
class RtssProfileAccess {
public:
	~RtssProfileAccess();
	RtssProfileAccess() = default;
	RtssProfileAccess(const RtssProfileAccess&) = delete;
	RtssProfileAccess& operator=(const RtssProfileAccess&) = delete;
	static RtssToolInfo DetectTool(bool afterburner);
	static bool LaunchTool(const RtssToolInfo& tool, std::wstring& error);
	bool Initialize(const RtssToolInfo& tool, std::wstring& error);
	bool Read(const std::string& profile, RtssProfileSnapshot& snapshot, std::wstring& error);
	bool Apply(const std::string& profile, uint32_t rate, const RtssProfileSnapshot& before,
		RtssProfileSnapshot& written, bool& saveAttempted, bool& readbackComplete,
		std::wstring& error, const std::function<bool()>& stillValid);
	bool Restore(const std::string& profile, const RtssProfileSnapshot& original,
		const RtssProfileSnapshot& lastWritten, bool created, bool& conflict, std::wstring& error);
	static bool SameRate(const RtssProfileSnapshot& a, const RtssProfileSnapshot& b) noexcept {
		return a.exists == b.exists && a.rate == b.rate;
	}

private:
	using ProfileFn = void (*)(LPCSTR);
	using PropertyFn = BOOL (*)(LPCSTR, LPBYTE, DWORD);
	std::filesystem::path _ProfilePath(const std::string& profile) const;
	bool _Exists(const std::string& profile, bool& exists, std::wstring& error);
	bool _ReadFile(const std::string& profile, RtssProfileSnapshot& snapshot, std::wstring& error);
	bool _Get(const char* property, uint32_t& value);
	bool _Set(const char* property, uint32_t value);
	bool _CallProfile(ProfileFn function, const char* profile);
	bool _Reload();
	void _Unload();
	HMODULE _module = nullptr;
	std::filesystem::path _directory;
	ProfileFn _load = nullptr, _save = nullptr, _delete = nullptr;
	PropertyFn _get = nullptr, _set = nullptr;
	void (*_update)() = nullptr;
	bool _faulted = false;
};

}

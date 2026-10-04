#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Magpie {

enum class ForegroundCompatibilityMode : uint32_t { Off, Trace, Query, Reference };
enum class ForegroundQueryPhase : uint32_t { Preparing, Device, SwapChain, FirstPresent, Present, Count };

struct ForegroundWindowIdentity {
	uintptr_t window = 0;
	uint32_t pid = 0, tid = 0;
	uintptr_t atom = 0;
};
struct ForegroundCallerRange { uintptr_t begin = 0, size = 0; };
struct ForegroundCompatibilitySnapshot {
	uint64_t epoch = 0;
	ForegroundCompatibilityMode mode = ForegroundCompatibilityMode::Off;
	bool armed = false;
	ForegroundWindowIdentity source, root, output;
	std::array<ForegroundCallerRange, 8> callers{};
};

// One frontend publisher; arbitrary driver/API readers. Atomic words avoid the
// data race of a conventional seqlock over a plain C++ struct. A reader never
// waits for a suspended publisher and fails closed on a changing generation.
class PublishedForegroundState {
public:
	void Publish(const ForegroundCompatibilitySnapshot& value) noexcept {
		static_assert(std::is_trivially_copyable_v<ForegroundCompatibilitySnapshot>);
		std::array<uintptr_t, WordCount> words{};
		std::memcpy(words.data(), &value, sizeof(value));
		_version.fetch_add(1);
		for (size_t i = 0; i < WordCount; ++i) _words[i].store(words[i]);
		_version.fetch_add(1);
	}
	bool Read(ForegroundCompatibilitySnapshot& value, uint64_t& version) const noexcept {
		version = _version.load();
		if (version & 1) return false;
		std::array<uintptr_t, WordCount> words{};
		for (size_t i = 0; i < WordCount; ++i) words[i] = _words[i].load();
		if (!Current(version)) return false;
		std::memcpy(&value, words.data(), sizeof(value));
		return true;
	}
	bool Current(uint64_t version) const noexcept { return !(version & 1) && _version.load() == version; }

private:
	static constexpr size_t WordCount = (sizeof(ForegroundCompatibilitySnapshot) + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);
	std::atomic<uint64_t> _version = 0;
	std::array<std::atomic<uintptr_t>, WordCount> _words{};
};

}

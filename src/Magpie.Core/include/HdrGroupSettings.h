#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Magpie {

enum class HdrGroupInput : uint8_t { ExistingChain, Sdr, Hdr };
enum class HdrGroupOutput : uint8_t { Auto, RestorePair, NativeWhite, RtxHdr, Sdr };
enum class HdrManagedRole : uint8_t { None, InputMapping, PairRestore, Output, Identity };

struct HdrGroupSettings {
	bool managed = false;
	HdrGroupInput input = HdrGroupInput::ExistingChain;
	HdrGroupOutput output = HdrGroupOutput::Auto;
	float whiteNits = 203;
	float peakNits = 1000;
	float exposure = 1;
	float shoulder = 1;
	float nativeWhiteNits = 203;
	// Adopting a legacy chain must retain its metadata as well as its pixels.
	bool nativeLegacy = false;
	float legacyWhiteNits = 203;
	float legacyExposure = 1;
	float legacyPeakNits = 1000;
	float rtxPeakNits = 1000;
	float rtxContrast = 100;
	float rtxSaturation = 100;
	float rtxMiddleGray = 50;
	std::optional<bool> CaptureIntent() const noexcept {
		if (input == HdrGroupInput::ExistingChain) return std::nullopt;
		return input == HdrGroupInput::Hdr;
	}
	bool IsValid() const noexcept {
		auto range = [](float value, float low, float high) { return std::isfinite(value) && value >= low && value <= high; };
		auto integer = [&](float value, float low, float high) { return range(value, low, high) && value == std::floor(value); };
		return uint8_t(input) <= 2 && uint8_t(output) <= 4 && (!managed || input != HdrGroupInput::ExistingChain) &&
			range(whiteNits,80,400) && range(peakNits,400,4000) && range(exposure,.1f,4) && range(shoulder,.1f,4) &&
			range(nativeWhiteNits,8,4000) && range(legacyWhiteNits,80,400) && range(legacyExposure,.1f,4) && range(legacyPeakNits,400,4000) &&
			integer(rtxPeakNits,400,2000) && integer(rtxContrast,0,200) && integer(rtxSaturation,0,200) && integer(rtxMiddleGray,10,100);
	}
	bool operator==(const HdrGroupSettings&) const noexcept = default;
};
}

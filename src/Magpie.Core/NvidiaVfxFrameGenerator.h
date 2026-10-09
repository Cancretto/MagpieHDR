#pragma once
#include "NvidiaVfxParameters.h"
#include <functional>
#include <memory>

namespace Magpie {
class DeviceResources;

struct NvidiaVfxFrameIdentity {
	uint64_t frameId = 0, captureSequence = 0, resourceGeneration = 0, historyRevision = 0;
};

// Publish consumes/copies a single output before the shared SDK buffer is reused.
// First A is published once. Subsequent pairs publish 1..M-1, then real B.
class NvidiaVfxFrameGenerator {
public:
	using Publish = std::function<bool(ID3D11Texture2D*, uint32_t, uint32_t)>;
	NvidiaVfxFrameGenerator();
	~NvidiaVfxFrameGenerator();
	bool Initialize(DeviceResources& resources, ID3D11Texture2D* input,
		const NvidiaVfxFrameGenerationSettings& settings) noexcept;
	bool Draw(ID3D11Texture2D* input, NvidiaVfxFrameIdentity identity,
		bool newCapture, const Publish& publish) noexcept;
	bool ApplyModel(DeviceResources& resources, ID3D11Texture2D* input, uint32_t model) noexcept;
	void RequestHistoryReset() noexcept;
	bool Drain() noexcept;
	uint32_t Multiplier() const noexcept { return _settings.multiplier; }
	const NvidiaVfxFrameGenerationSettings& Settings() const noexcept { return _settings; }
private:
	struct Impl;
	[[maybe_unused]] std::unique_ptr<Impl> _impl;
	NvidiaVfxFrameGenerationSettings _settings{};
};
}

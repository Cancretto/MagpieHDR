#include "pch.h"
#include "NvidiaVfxImageBackend.h"
#include "NvidiaVfxRuntime.h"
#include "NvidiaVfxParameters.h"
#include "DeviceResources.h"
#include "DirectXHelper.h"
#include "BackendDescriptorStore.h"
#include "HdrSurfaceAdapter.h"
#include "Logger.h"
#include "EffectCompiler.h"
#include "EffectDrawer.h"
#include "EffectsProfiler.h"
#ifdef MP_ENABLE_NVIDIA_VFX
#include <nvVFXUpscale.h>
#include <nvVFXTrueHDR.h>
#endif

namespace Magpie {
#ifdef MP_ENABLE_NVIDIA_VFX

struct NvidiaVfxImageBackend::Impl {
	DeviceResources* resources = nullptr;
	NvidiaVfxSession session;
	BackendDescriptorStore descriptors;
	HdrSurfaceAdapter adapter;
	winrt::com_ptr<ID3D11Texture2D> packedHdr;
	winrt::com_ptr<ID3D11Texture2D> nativeUpscale;
	EffectDrawer adaptation;
	EffectsProfiler adaptationProfiler;
	NvidiaVfxImageKind kind = NvidiaVfxImageKind::Vsr;
	EffectOption settings;
	NvVFX_Handle CreateCandidate(const EffectOption& option) noexcept {
		const auto value = [&](const char* key, float fallback) { const auto it = option.parameters.find(key); return it == option.parameters.end() ? fallback : it->second; };
		NvVFX_Handle effect = nullptr;
		const char* selector = kind == NvidiaVfxImageKind::Vsr ? "VideoSuperRes" :
			kind == NvidiaVfxImageKind::Upscale ? NVVFX_FX_SR_UPSCALE : NVVFX_FX_TRUE_HDR;
		if (!NvidiaVfxSucceeded(NvVFX_CreateEffect(selector, &effect), "CreateEffect")) return nullptr;
		auto cleanup = wil::scope_exit([&] { if (effect) NvVFX_DestroyEffect(effect); });
		if (!NvidiaVfxSucceeded(NvVFX_SetCudaStream(effect, NVVFX_CUDA_STREAM, session.stream), "SetCudaStream") ||
			!NvidiaVfxSucceeded(NvVFX_SetImage(effect, NVVFX_INPUT_IMAGE, &session.input), "SetImage(input)") ||
			!NvidiaVfxSucceeded(NvVFX_SetImage(effect, NVVFX_OUTPUT_IMAGE, &session.output), "SetImage(output)")) return nullptr;
		if (kind == NvidiaVfxImageKind::Vsr) {
			const float categoryValue = value("category", 0.f);
			const int category = NvidiaVfxCategory(categoryValue);
			const float tier = value("strength", 1.f);
			if (categoryValue != float(category) || !NvidiaVfxTierValid(category, tier) ||
				(category == 2 && session.Context().ComputeMajor() < 8)) return nullptr;
			if (!NvidiaVfxSucceeded(NvVFX_SetU32(effect, "QualityLevel", NvidiaVfxQualityLevel(category, int(tier))), "SetQualityLevel")) return nullptr;
			// The deployed strength key remains a tier. Vendor continuous Strength
			// is independent and intentionally has no third first-level control.
			float enhancement = 0.f;
			if (NvVFX_GetF32(effect, NVVFX_STRENGTH, &enhancement) == NVCV_SUCCESS) {
				if (!std::isfinite(enhancement) || enhancement < 0 || enhancement > 1 ||
					!NvidiaVfxSucceeded(NvVFX_SetF32(effect, NVVFX_STRENGTH, enhancement), "SetVsrStrength")) return nullptr;
				Logger::Get().Info(fmt::format("Nvidia VFX VSR vendor default Strength={}", enhancement));
			}
		} else if (kind == NvidiaVfxImageKind::Upscale) {
			const float strength = value("sharpness", .4f);
			if (!std::isfinite(strength) || strength < 0 || strength > 1 ||
				!NvidiaVfxSucceeded(NvVFX_SetF32(effect, NVVFX_STRENGTH, strength), "SetStrength")) return nullptr;
		} else {
			if (BuildHdrComponentPlan(std::span(&option, 1)).error != HdrComponentError::None) return nullptr;
			const std::pair<const char*, uint32_t> tunables[] = {
				{ NVVFXTRUEHDR_CONTRAST, uint32_t(value("contrast", 100)) },
				{ NVVFXTRUEHDR_SATURATION, uint32_t(value("saturation", 100)) },
				{ NVVFXTRUEHDR_MIDDLE_GRAY, uint32_t(value("middleGray", 50)) },
				{ NVVFXTRUEHDR_LUMINANCE, uint32_t(value("peakNits", 1000)) },
				{ NVVFXTRUEHDR_DEBANDING_OFF, value("debanding", 1) == 0 ? 1u : 0u }
			};
			for (const auto& [key, val] : tunables)
				if (!NvidiaVfxSucceeded(NvVFX_SetU32(effect, key, val), "SetTrueHdrTunable")) return nullptr;
		}
		if (!NvidiaVfxSucceeded(NvVFX_Load(effect), "Load candidate")) return nullptr;
		return std::exchange(effect, nullptr);
	}
};
#else
struct NvidiaVfxImageBackend::Impl {};
#endif

NvidiaVfxImageBackend::NvidiaVfxImageBackend() = default;
NvidiaVfxImageBackend::~NvidiaVfxImageBackend() = default;
bool NvidiaVfxImageBackend::Initialize(DeviceResources& resources, ID3D11Texture2D* input,
	ID3D11Texture2D* output, const EffectOption& option, NvidiaVfxImageKind kind) noexcept {
#ifdef MP_ENABLE_NVIDIA_VFX
	if (!input || !output) return false;
	D3D11_TEXTURE2D_DESC in{}, out{};
	input->GetDesc(&in); output->GetDesc(&out);
	const auto sdr = [](DXGI_FORMAT format) { return format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM; };
	if (!sdr(in.Format) || in.SampleDesc.Count != 1 || out.SampleDesc.Count != 1 ||
		(kind == NvidiaVfxImageKind::TrueHdr ? out.Format != DXGI_FORMAT_R16G16B16A16_FLOAT : !sdr(out.Format))) return false;
	const auto read = [&](const char* key, float fallback) { const auto it = option.parameters.find(key); return it == option.parameters.end() ? fallback : it->second; };
	if ((kind == NvidiaVfxImageKind::TrueHdr || (kind == NvidiaVfxImageKind::Vsr && NvidiaVfxSameSize(NvidiaVfxCategory(read("category", 0))))) &&
		(in.Width != out.Width || in.Height != out.Height)) return false;
	NvidiaVfxUpscalePlan upscale;
	if (kind == NvidiaVfxImageKind::Upscale) {
		const float mode = read("upscaleMode", 0);
		if (!std::isfinite(mode) || mode < 0 || mode > 5 || std::floor(mode) != mode) return false;
		upscale = PlanNvidiaVfxUpscale({in.Width, in.Height}, {out.Width, out.Height}, int(mode));
		if (!upscale.valid || (mode != 0 && (upscale.native.width != out.Width || upscale.native.height != out.Height))) return false;
	}
	const wchar_t* library = kind == NvidiaVfxImageKind::Vsr ? L"nvVFXVideoSuperRes.dll" :
		kind == NvidiaVfxImageKind::Upscale ? L"nvVFXUpscale.dll" : L"nvVFXTrueHDR.dll";
	// NvCVImage's C++ default constructor already calls the proxy Alloc API.
	// Pin the matched local modules before constructing any image descriptors.
	if (!LoadNvidiaVfxRuntime(library, true)) return false;
	auto candidate = std::make_unique<Impl>();
	candidate->resources = &resources;
	candidate->kind = kind;
	if (!candidate->session.Initialize(resources, library)) return false;
	NvidiaVfxContextScope scope(candidate->session.Context());
	if (!scope || !NvidiaVfxSucceeded(NvCVImage_Alloc(&candidate->session.input, in.Width, in.Height,
		NVCV_RGBA, NVCV_U8, NVCV_INTERLEAVED, NVCV_GPU, 32), "Alloc(input)") ||
		!NvidiaVfxSucceeded(NvCVImage_Alloc(&candidate->session.output,
			kind == NvidiaVfxImageKind::Upscale ? upscale.native.width : out.Width,
			kind == NvidiaVfxImageKind::Upscale ? upscale.native.height : out.Height,
			kind == NvidiaVfxImageKind::TrueHdr ? NVCV_RGB10A2 : NVCV_RGBA,
			kind == NvidiaVfxImageKind::TrueHdr ? NVCV_P32 : NVCV_U8,
			NVCV_INTERLEAVED, NVCV_GPU, 32), "Alloc(output)")) return false;
	if (kind == NvidiaVfxImageKind::TrueHdr) {
		candidate->packedHdr = DirectXHelper::CreateTexture2D(resources.GetD3DDevice(), DXGI_FORMAT_R10G10B10A2_UNORM,
			out.Width, out.Height, D3D11_BIND_SHADER_RESOURCE);
		candidate->descriptors.Initialize(resources.GetD3DDevice());
		if (!candidate->packedHdr || !candidate->adapter.Initialize(resources, candidate->descriptors)) return false;
	}
	if (kind == NvidiaVfxImageKind::Upscale && upscale.adaptation != NvidiaVfxAdaptation::None) {
		candidate->nativeUpscale = DirectXHelper::CreateTexture2D(resources.GetD3DDevice(), out.Format,
			upscale.native.width, upscale.native.height, D3D11_BIND_SHADER_RESOURCE);
		candidate->descriptors.Initialize(resources.GetD3DDevice());
		EffectOption resize{ .name = "Bicubic", .parameters{{"paramB", 0.f}, {"paramC", .5f}},
			.scalingType = ScalingType::Absolute, .scale = {float(out.Width), float(out.Height)} };
		EffectDesc desc{ .name = "Bicubic" };
		if (!candidate->nativeUpscale || EffectCompiler::Compile(desc, EffectCompilerFlags::InlineParams, &resize.parameters)) return false;
		ID3D11Texture2D* handoff = candidate->nativeUpscale.get();
		if (!candidate->adaptation.Initialize(desc, resize, resources, candidate->descriptors, &handoff)) return false;
	}
	candidate->session.effect = candidate->CreateCandidate(option);
	if (!candidate->session.effect) return false;
	candidate->settings = option;
	_impl = std::move(candidate); _option = option; _kind = kind;
	return true;
#else
	(void)resources; (void)input; (void)output; (void)option; (void)kind;
	Logger::Get().Error("Nvidia VFX suite requires a build with matched VFX 1.3 development headers and runtime");
	return false;
#endif
}
bool NvidiaVfxImageBackend::Resize(DeviceResources& resources, ID3D11Texture2D* input, ID3D11Texture2D* output) noexcept {
	return Initialize(resources, input, output, _option, _kind);
}
bool NvidiaVfxImageBackend::Drain() noexcept {
#ifdef MP_ENABLE_NVIDIA_VFX
	if (!_impl) return true;
	NvidiaVfxContextScope scope(_impl->session.Context());
	return scope && _impl->session.Synchronize();
#else
	return true;
#endif
}
bool NvidiaVfxImageBackend::Draw(const NativeEffectDrawContext& context) noexcept {
#ifdef MP_ENABLE_NVIDIA_VFX
	if (!_impl) return false;
	NvidiaVfxContextScope scope(_impl->session.Context());
	if (!scope || !_impl->session.Import(context.input, _impl->session.input) ||
		!NvidiaVfxSucceeded(NvVFX_Run(_impl->session.effect, 0), "Run image effect")) return false;
	if (_kind != NvidiaVfxImageKind::TrueHdr) {
		if (!_impl->nativeUpscale) return _impl->session.Export(_impl->session.output, context.output);
		if (!_impl->session.Export(_impl->session.output, _impl->nativeUpscale.get()) ||
			!_impl->adaptation.Draw(_impl->adaptationProfiler)) return false;
		_impl->resources->GetD3DDC()->ClearState();
		_impl->resources->GetD3DDC()->CopyResource(context.output, _impl->adaptation.GetOutputTexture());
		return true;
	}
	if (!_impl->session.Export(_impl->session.output, _impl->packedHdr.get())) return false;
	HdrTransformParameters parameters;
	const auto peak = _option.parameters.find("peakNits");
	parameters.hdrPeakNits = peak == _option.parameters.end() ? 1000.f : peak->second;
	// Decode PQ and BT.2020 explicitly into the 80-nit canonical scRGB domain.
	return _impl->adapter.ConvertHdr10ToCanonical(_impl->packedHdr.get(), context.output, parameters);
#else
	(void)context; return false;
#endif
}
EffectParameterApplyMode NvidiaVfxImageBackend::GetParameterApplyMode(std::string_view name) const noexcept {
	if (_kind == NvidiaVfxImageKind::TrueHdr) return name == "contrast" || name == "saturation" || name == "middleGray" || name == "peakNits" || name == "debanding"
		? EffectParameterApplyMode::Live : EffectParameterApplyMode::Unavailable;
	if (_kind == NvidiaVfxImageKind::Upscale) return name == "sharpness" ? EffectParameterApplyMode::Live : EffectParameterApplyMode::RestartRequired;
	return name == "strength" ? EffectParameterApplyMode::Live : EffectParameterApplyMode::RestartRequired;
}
bool NvidiaVfxImageBackend::ApplyParameters(const EffectOption& option, std::span<const std::string> names) noexcept {
#ifdef MP_ENABLE_NVIDIA_VFX
	if (!_impl || std::ranges::any_of(names, [&](const auto& name) { return GetParameterApplyMode(name) != EffectParameterApplyMode::Live; })) return false;
	NvidiaVfxContextScope scope(_impl->session.Context());
	if (!scope || !_impl->session.Synchronize()) return false;
	// Load the complete candidate transactionally: a rejected SDK setting must
	// not leave a partially applied HDR tuple or replace a working VSR model.
	NvVFX_Handle candidate = _impl->CreateCandidate(option);
	if (!candidate) return false;
	std::swap(candidate, _impl->session.effect);
	NvVFX_DestroyEffect(candidate);
	_impl->settings = option; _option = option;
	return true;
#else
	(void)option; (void)names; return false;
#endif
}
} // namespace Magpie

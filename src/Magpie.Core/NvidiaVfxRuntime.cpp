#include "pch.h"
#include "NvidiaVfxRuntime.h"
#include "DeviceResources.h"
#include "Logger.h"
#include "StrHelper.h"
#include "Win32Helper.h"

#if defined(MP_ENABLE_RTX_VIDEO_DENOISE) || defined(MP_ENABLE_NVIDIA_VFX)
// Used by the SDK's proxy loader; the application path is selected explicitly.
char* g_nvVFXSDKPath = nullptr;
namespace Magpie {

bool NvidiaVfxSucceeded(NvCV_Status status, std::string_view operation) noexcept {
	if (status == NVCV_SUCCESS) return true;
	const char* message = NvCV_GetErrorStringFromCode(status);
	Logger::Get().Error(fmt::format("Nvidia VFX {} failed: {} ({})", operation, message ? message : "unknown", int(status)));
	return false;
}
static HMODULE LoadVfxModule(const std::filesystem::path& directory, const wchar_t* name) noexcept {
	const auto expected = directory / name;
	HMODULE module = GetModuleHandleW(name);
	if (!module) module = LoadLibraryExW(expected.c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!module) { Logger::Get().Win32Error(fmt::format("Nvidia VFX component missing or cannot load: {}", StrHelper::UTF16ToUTF8(expected.native()))); return nullptr; }
	wchar_t path[32768]{};
	const auto size = GetModuleFileNameW(module, path, ARRAYSIZE(path));
	if (!size || size >= ARRAYSIZE(path) || _wcsicmp(path, expected.c_str()) != 0) {
		Logger::Get().Error(fmt::format("Nvidia VFX DLL path mismatch: {}", StrHelper::UTF16ToUTF8(name)));
		return nullptr;
	}
	// Proxy functions cache addresses for process lifetime. Keep verified module
	// references for the same lifetime rather than unloading under cached pointers.
	return module;
}
bool LoadNvidiaVfxRuntime(const wchar_t* featureLibrary, bool requireVersion13) noexcept {
	const auto directory = Win32Helper::GetExePath().parent_path();
	SetEnvironmentVariableW(L"NV_VIDEO_EFFECTS_PATH", L"USE_APP_PATH");
	if ((!GetModuleHandleW(L"nvcuda.dll") && !LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) ||
		!LoadVfxModule(directory, L"nvngxruntime.dll") || !LoadVfxModule(directory, L"NVCVImage.dll") ||
		!LoadVfxModule(directory, L"NVVideoEffects.dll")) return false;
	if (_wcsicmp(featureLibrary, L"nvVFXVideoSuperRes.dll") == 0 && !LoadVfxModule(directory, L"nvngx_vsr.dll")) return false;
	if (_wcsicmp(featureLibrary, L"nvVFXTrueHDR.dll") == 0 && !LoadVfxModule(directory, L"nvngx_truehdr.dll")) return false;
	if (!LoadVfxModule(directory, featureLibrary)) return false;
	unsigned version = 0;
	if (!NvidiaVfxSucceeded(NvVFX_GetVersion(&version), "GetVersion")) return false;
	if (requireVersion13 && version != 0x01030000u) {
		Logger::Get().Error(fmt::format("Nvidia VFX requires matched Core 1.3.0.0; loaded 0x{:08X}", version));
		return false;
	}
	Logger::Get().Info(fmt::format("Nvidia VFX runtime: version=0x{:08X} feature={}", version, StrHelper::UTF16ToUTF8(featureLibrary)));
	return true;
}

template<class Fn> static Fn CudaFunction(const char* name) noexcept {
	return reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"nvcuda.dll"), name));
}
using CudaInitFn = int(WINAPI*)(unsigned);
using CudaD3DDeviceFn = int(WINAPI*)(int*, IDXGIAdapter*);
using CudaRetainFn = int(WINAPI*)(void**, int);
using CudaReleaseFn = int(WINAPI*)(int);
using CudaPushFn = int(WINAPI*)(void*);
using CudaPopFn = int(WINAPI*)(void**);
using CudaAttributeFn = int(WINAPI*)(int*, int, int);

bool NvidiaVfxCudaContext::Initialize(DeviceResources& resources) noexcept {
	if (_context) return false;
	const auto init = CudaFunction<CudaInitFn>("cuInit");
	const auto device = CudaFunction<CudaD3DDeviceFn>("cuD3D11GetDevice");
	const auto retain = CudaFunction<CudaRetainFn>("cuDevicePrimaryCtxRetain");
	const auto attribute = CudaFunction<CudaAttributeFn>("cuDeviceGetAttribute");
	if (!init || !device || !retain || !attribute || init(0) ||
		device(&_device, resources.GetGraphicsAdapter()) ||
		attribute(&_major, 75, _device) || attribute(&_minor, 76, _device) || retain(&_context, _device)) {
		Logger::Get().Error("Nvidia VFX cannot select the CUDA device matching the D3D11 renderer");
		return false;
	}
	Logger::Get().Info(fmt::format("Nvidia VFX selected CUDA device={} compute={}.{}", _device, _major, _minor));
	return true;
}
NvidiaVfxCudaContext::~NvidiaVfxCudaContext() {
	if (_context) {
		const auto release = CudaFunction<CudaReleaseFn>("cuDevicePrimaryCtxRelease_v2");
		if (!release || release(_device)) Logger::Get().Error("Nvidia VFX CUDA primary context release failed");
	}
}
bool NvidiaVfxCudaContext::Push() const noexcept {
	const auto push = CudaFunction<CudaPushFn>("cuCtxPushCurrent_v2");
	return _context && push && push(_context) == 0;
}
void NvidiaVfxCudaContext::Pop() noexcept {
	void* context = nullptr;
	const auto pop = CudaFunction<CudaPopFn>("cuCtxPopCurrent_v2");
	if (!pop || pop(&context)) Logger::Get().Error("Nvidia VFX CUDA context pop failed");
}
bool NvidiaVfxSession::Initialize(DeviceResources& resources, const wchar_t* library) noexcept {
	if (!LoadNvidiaVfxRuntime(library, true) || !_context.Initialize(resources)) return false;
	NvidiaVfxContextScope scope(_context);
	_synchronize = reinterpret_cast<SynchronizeFn>(GetProcAddress(GetModuleHandleW(L"NVVideoEffects.dll"), "NvVFX_CudaStreamSynchronize"));
	return scope && _synchronize && NvidiaVfxSucceeded(NvVFX_CudaStreamCreate(&stream), "CudaStreamCreate");
}
NvidiaVfxSession::~NvidiaVfxSession() {
	NvidiaVfxContextScope scope(_context);
	if (!scope) return;
	if (stream) Synchronize();
	if (effect) { NvVFX_DestroyEffect(effect); effect = nullptr; }
	NvCVImage_Dealloc(&input); NvCVImage_Dealloc(&previous); NvCVImage_Dealloc(&output);
	NvCVImage_Dealloc(&_inputD3D); NvCVImage_Dealloc(&_outputD3D); NvCVImage_Dealloc(&_temporary);
	if (stream) { NvVFX_CudaStreamDestroy(stream); stream = nullptr; }
}
bool NvidiaVfxSession::Synchronize() noexcept {
	return _synchronize && NvidiaVfxSucceeded(_synchronize(stream), "CudaStreamSynchronize");
}
bool NvidiaVfxSession::_Transfer(ID3D11Texture2D* texture, NvCVImage& image, bool importing) noexcept {
	if (!texture) return false;
	auto& wrapper = importing ? _inputD3D : _outputD3D;
	auto& retained = importing ? _inputTexture : _outputTexture;
	if (retained.get() != texture) {
		if (!Synchronize()) return false;
		NvCVImage_Dealloc(&wrapper);
		retained = nullptr;
		if (!NvidiaVfxSucceeded(NvCVImage_InitFromD3D11Texture(&wrapper, texture), "InitFromD3D11Texture")) return false;
		retained.copy_from(texture);
	}
	if (!NvidiaVfxSucceeded(NvCVImage_MapResource(&wrapper, stream), "MapResource")) return false;
	const bool transferred = NvidiaVfxSucceeded(importing
		? NvCVImage_Transfer(&wrapper, &image, 1.f, stream, &_temporary)
		: NvCVImage_Transfer(&image, &wrapper, 1.f, stream, &_temporary), "Transfer");
	const bool unmapped = NvidiaVfxSucceeded(NvCVImage_UnmapResource(&wrapper, stream), "UnmapResource");
	return transferred && unmapped && Synchronize();
}
bool NvidiaVfxSession::Import(ID3D11Texture2D* texture, NvCVImage& image) noexcept { return _Transfer(texture, image, true); }
bool NvidiaVfxSession::Export(NvCVImage& image, ID3D11Texture2D* texture) noexcept { return _Transfer(texture, image, false); }

} // namespace Magpie
#endif

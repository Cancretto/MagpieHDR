#include "pch.h"
#include "PresenterBase.h"
#include "DeviceResources.h"
#include "Logger.h"
#include "ScalingWindow.h"

namespace Magpie {

bool PresenterBase::Initialize(HWND hwndAttach, const DeviceResources& deviceResources) noexcept {
	_deviceResources = &deviceResources;
	_vrrWindow = hwndAttach;
	_vrrMonitor = MonitorFromWindow(hwndAttach, MONITOR_DEFAULTTONULL);
	_vrr.Configure(ScalingWindow::Get().Options().isVRREnabled, deviceResources.IsTearingSupported());
	_RefreshVRREnvironment(true);

	HRESULT hr = deviceResources.GetD3DDevice()->CreateFence(
		_fenceValue, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
	if (FAILED(hr)) {
		// GH#979
		// 这个错误会在某些很旧的显卡上出现，似乎是驱动的 bug。文档中提到 ID3D11Device5::CreateFence 
		// 和 ID3D12Device::CreateFence 等价，但支持 DX12 的显卡也有失败的可能，如 GH#1013
		Logger::Get().ComError("CreateFence 失败", hr);
		return false;
	}

	if (!_fenceEvent.try_create(wil::EventOptions::None, nullptr)) {
		Logger::Get().Win32Error("CreateEvent 失败");
		return false;
	}

	const bool initialized = _Initialize(hwndAttach);
	_LogVRRState(initialized ? "initialize" : "initialize-failed");
	return initialized;
}

VRRUnavailableReason PresenterBase::_QualifyVRROutput() const noexcept {
	if (GetSystemMetrics(SM_REMOTESESSION)) return VRRUnavailableReason::RemoteSession;
	if (!_deviceResources->GetDXGIFactory()->IsCurrent()) return VRRUnavailableReason::StaleDisplayTopology;
	DXGI_ADAPTER_DESC1 adapterDesc{};
	IDXGIAdapter4* adapter = _deviceResources->GetGraphicsAdapter();
	if (!adapter || FAILED(adapter->GetDesc1(&adapterDesc))) return VRRUnavailableReason::OutputUnknown;
	if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) return VRRUnavailableReason::SoftwareAdapter;
	RECT windowRect{};
	if (!GetWindowRect(_vrrWindow, &windowRect) || IsRectEmpty(&windowRect))
		return VRRUnavailableReason::OutputUnknown;
	const HMONITOR monitor = MonitorFromRect(&windowRect, MONITOR_DEFAULTTONULL);
	MONITORINFO monitorInfo{};
	monitorInfo.cbSize = sizeof(monitorInfo);
	if (!monitor || !GetMonitorInfo(monitor, &monitorInfo)) return VRRUnavailableReason::OutputUnknown;
	RECT intersection{};
	if (!IntersectRect(&intersection, &windowRect, &monitorInfo.rcMonitor) ||
		!EqualRect(&intersection, &windowRect)) return VRRUnavailableReason::SpanningOutputs;
	for (UINT index = 0; ; ++index) {
		winrt::com_ptr<IDXGIOutput> output;
		const HRESULT hr = adapter->EnumOutputs(index, output.put());
		if (hr == DXGI_ERROR_NOT_FOUND) break;
		if (FAILED(hr)) return VRRUnavailableReason::OutputUnknown;
		DXGI_OUTPUT_DESC desc{};
		if (FAILED(output->GetDesc(&desc))) return VRRUnavailableReason::OutputUnknown;
		if (desc.AttachedToDesktop && desc.Monitor == monitor) return VRRUnavailableReason::None;
	}
	return VRRUnavailableReason::CrossAdapter;
}

void PresenterBase::_RefreshVRREnvironment(bool force) noexcept {
	if (!_vrr.Requested() || !_vrr.TearingSupported()) return;
	const auto now = std::chrono::steady_clock::now();
	if (!force && now < _vrrNextProbe) return;
	_vrrNextProbe = now + std::chrono::milliseconds(500);
	_vrrMonitor = MonitorFromWindow(_vrrWindow, MONITOR_DEFAULTTONULL);
	_vrr.Environment(_QualifyVRROutput());
	_LogVRRState("output-probe");
}

void PresenterBase::_VRRPathAvailable(bool available) noexcept {
	_vrr.PathAvailable(available);
	_vrrMonitor = MonitorFromWindow(_vrrWindow, MONITOR_DEFAULTTONULL);
	_RefreshVRREnvironment(true);
	_LogVRRState("path-change");
}

void PresenterBase::_LogVRRState(std::string_view event) noexcept {
	if (_lastLoggedVRR && *_lastLoggedVRR == _vrr && _lastLoggedVRRMonitor == _vrrMonitor) return;
	_lastLoggedVRR = _vrr;
	_lastLoggedVRRMonitor = _vrrMonitor;
	const char* reason = "none";
	switch (_vrr.Reason()) {
	case VRRUnavailableReason::None: break;
	case VRRUnavailableReason::Disabled: reason = "disabled"; break;
	case VRRUnavailableReason::TearingUnsupported: reason = "tearing-unsupported"; break;
	case VRRUnavailableReason::PathUnavailable: reason = "non-DXGI-path"; break;
	case VRRUnavailableReason::OutputUnknown: reason = "output-unknown"; break;
	case VRRUnavailableReason::SpanningOutputs: reason = "spanning-or-outside-output"; break;
	case VRRUnavailableReason::SoftwareAdapter: reason = "software-adapter"; break;
	case VRRUnavailableReason::RemoteSession: reason = "remote-session"; break;
	case VRRUnavailableReason::CrossAdapter: reason = "cross-adapter-output"; break;
	case VRRUnavailableReason::CreationRejected: reason = "creation-rejected"; break;
	case VRRUnavailableReason::AltEnterUnsafe: reason = "alt-enter-guard-failed"; break;
	case VRRUnavailableReason::PresentRejected: reason = "present-contract-rejected"; break;
	case VRRUnavailableReason::SwapChainWithoutTearing: reason = "swap-chain-without-tearing-restart-required"; break;
	case VRRUnavailableReason::StaleDisplayTopology: reason = "display-topology-stale-restart-required"; break;
	}
	DXGI_ADAPTER_DESC1 adapterDesc{};
	if (auto adapter = _deviceResources->GetGraphicsAdapter()) adapter->GetDesc1(&adapterDesc);
	Logger::Get().Info(fmt::format(
		"VRR: event={} requested={} tearingSupported={} eligible={} creationFlags=0x{:x} "
		"syncInterval=0 presentFlags=0x{:x} active=unknown vendorCompatibility=false "
		"reason={} hwnd=0x{:x} monitor=0x{:x} adapterVendor=0x{:x} adapterLuid={:x}:{:x}",
		event, _vrr.Requested(), _vrr.TearingSupported(),
		_vrr.Reason() == VRRUnavailableReason::None, _vrr.ResizeFlags(), _vrr.PresentFlags(),
		reason, reinterpret_cast<uintptr_t>(_vrrWindow), reinterpret_cast<uintptr_t>(_vrrMonitor),
		adapterDesc.VendorId, uint32_t(adapterDesc.AdapterLuid.HighPart), adapterDesc.AdapterLuid.LowPart));
}

HRESULT PresenterBase::_PresentWithVRR(IDXGISwapChain4* swapChain, bool retryWithoutTearing) noexcept {
	_RefreshVRREnvironment();
	const UINT flags = _vrr.PresentFlags();
	HRESULT hr = swapChain->Present(0, flags);
	if (flags && hr == DXGI_ERROR_INVALID_CALL) {
		// Device loss is deliberately excluded. Latch for this swap-chain session;
		// ResizeBuffers must still retain its original immutable creation flags.
		_vrr.RejectPresent();
		Logger::Get().ComWarn("VRR Present contract rejected; disabling tearing submissions for this session", hr);
		_LogVRRState("present-rejected");
		// An SDK proxy may already have consumed its tagged FG input. Only the
		// ordinary DXGI endpoint can safely retry the same image once here.
		if (retryWithoutTearing) hr = swapChain->Present(0, 0);
	}
	return hr;
}

uint32_t PresenterBase::_CalcBufferCount() noexcept {
	// 缓冲区数量取决于 ScalingRuntime::_ScalingThreadProc 中检查光标移动的频率
	return ScalingWindow::Get().Options().Is3DGameMode() ? 4 : 8;
}

void PresenterBase::_WaitForGpu() noexcept {
	ID3D11DeviceContext4* d3dDC = _deviceResources->GetD3DDC();

	// 等待渲染完成
	HRESULT hr = d3dDC->Signal(_fence.get(), ++_fenceValue);
	if (FAILED(hr)) {
		return;
	}

	hr = _fence->SetEventOnCompletion(_fenceValue, _fenceEvent.get());
	if (FAILED(hr)) {
		return;
	}

	d3dDC->Flush();

	WaitForSingleObject(_fenceEvent.get(), 1000);
}

}

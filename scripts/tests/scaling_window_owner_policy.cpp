#include <windows.h>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <fmt/format.h>

// Real hidden HWNDs, with only the owner API intercepted to model UWP refusal,
// ambiguous last-error state, and destruction while the call is in progress.
enum class OwnerApiMode { Native, Reject, AppliedWithError, Unexpected, DestroyOutput, DestroySource };
static OwnerApiMode ownerApiMode;
static HWND alternateOwner;
static int ownerCalls;
static LONG_PTR SetOwnerForTest(HWND window, int index, LONG_PTR value) {
    ++ownerCalls;
    switch (ownerApiMode) {
    case OwnerApiMode::Reject:
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    case OwnerApiMode::AppliedWithError: {
        const LONG_PTR previous = SetWindowLongPtrW(window, index, value);
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return previous;
    }
    case OwnerApiMode::Unexpected:
        SetWindowLongPtrW(window, index, reinterpret_cast<LONG_PTR>(alternateOwner));
        SetLastError(ERROR_ACCESS_DENIED);
        return 0;
    case OwnerApiMode::DestroyOutput:
        assert(DestroyWindow(window));
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    case OwnerApiMode::DestroySource:
        assert(DestroyWindow(reinterpret_cast<HWND>(value)));
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    default:
        return SetWindowLongPtrW(window, index, value);
    }
}
static HWND ReadOwnerForTest(HWND window, UINT relation) {
    const HWND owner = GetWindow(window, relation);
    // Verification must not replace the owner API's original error code.
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return owner;
}
#define SetWindowLongPtrW SetOwnerForTest
#define GetWindow ReadOwnerForTest
#include "ScalingWindowOwner.h"
#undef GetWindow
#undef SetWindowLongPtrW
using namespace Magpie;

enum class ScalingError { NoError, ScalingWindowCreationFailed, SourceWindowClosed,
    SourceWindowUnresponsive, CaptureFailed };
static ScalingError rendererError;
static std::function<void()> initializeAction;
static bool sourceHung;
static int detachCalls;
static bool stopped;
static int reports;
static std::string reportedContext;
static uint32_t reportedSystemError;

struct Logger {
    struct DiagnosticCapture {
        std::string details;
        uint32_t systemError = 0;
        static inline DiagnosticCapture* active = nullptr;
        DiagnosticCapture() { assert(!active); active = this; }
        ~DiagnosticCapture() { active = nullptr; }
        const std::string& Details() const { return details; }
        uint32_t SystemError() const { return systemError; }
    };
    int warnings = 0;
    static Logger& Get() { static Logger logger; return logger; }
    void Info(std::string_view) {}
    void Warn(std::string_view) { ++warnings; }
    void Win32Warn(std::string_view text) { Warn(text); }
    void Error(std::string_view text) {
        if (auto* capture = DiagnosticCapture::active) capture->details.append(text);
    }
    void Win32Error(std::string_view text) {
        if (auto* capture = DiagnosticCapture::active) capture->systemError = GetLastError();
        Error(text);
    }
};
namespace Win32Helper {
static bool IsWindowHung(HWND) { return sourceHung; }
static std::wstring GetWindowClassName(HWND window) {
    wchar_t name[256]{};
    GetClassNameW(window, name, 256);
    return name;
}
}
namespace StrHelper {
static std::string UTF16ToUTF8(const std::wstring& name) {
    std::string result;
    for (const wchar_t ch : name) { assert(ch < 128); result += static_cast<char>(ch); }
    return result;
}
}
static BOOL DetachForTest(DWORD, DWORD, BOOL attach) {
    assert(!attach);
    ++detachCalls;
    return TRUE;
}
class Renderer {
public:
    ScalingError Initialize(HWND, int) {
        if (initializeAction) initializeAction();
        return rendererError;
    }
    const std::string& InitializationContext() const {
        static const std::string context = "capture backend diagnostic";
        return context;
    }
    uint32_t InitializationSystemError() const { return 1234; }
};
class CursorManager {};
struct Options {
    int overlayOptions = 0;
    bool IsTouchSupportEnabled() const { return false; }
    std::function<void(HWND, ScalingError, std::string_view, uint32_t)> reportErrorDetails;
    std::function<void(HWND, ScalingError)> showError;
};
static Options _options;
static std::unique_ptr<Renderer> _renderer;
static std::unique_ptr<CursorManager> _cursorManager;
static HWND outputWindow;
static HWND _hwndRenderer;
static HWND Handle() { return outputWindow; }
static void _UpdateTouchHoleWindows(bool) { assert(false); }
static void Stop() { stopped = true; }
#define AttachThreadInput DetachForTest
#include "ScalingWindowOwnerStartup.inc"
#undef AttachThreadInput

static HWND HiddenWindow(HWND parent = nullptr) {
    HWND window = CreateWindowExW(0, L"STATIC", L"Magpie owner policy test",
        parent ? WS_CHILD : WS_POPUP, 0, 0, 32, 32, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(window);
    return window;
}
static void Close(HWND window) { if (IsWindow(window)) assert(DestroyWindow(window)); }
static void Prepare(OwnerApiMode mode) {
    ownerApiMode = mode;
    ownerCalls = detachCalls = reports = 0;
    stopped = sourceHung = false;
    rendererError = ScalingError::NoError;
    initializeAction = {};
    Logger::Get().warnings = 0;
    reportedContext.clear();
    reportedSystemError = 0;
    _options.reportErrorDetails = [](HWND, ScalingError, std::string_view context, uint32_t system) {
        ++reports; reportedContext = context; reportedSystemError = system;
    };
    _options.showError = [](HWND, ScalingError) { ++reports; };
    outputWindow = HiddenWindow();
    _hwndRenderer = outputWindow;
}

int main() {
    HWND source = HiddenWindow();
    alternateOwner = HiddenWindow();
    Prepare(OwnerApiMode::Native);
    auto result = TrySetScalingWindowOwner(outputWindow, source);
    assert(result.IsApplied() && result.actualOwner == source);
    assert(result.systemError == ERROR_SUCCESS);
    assert(TrySetScalingWindowOwner(outputWindow, source).IsApplied());
    assert(TrySetScalingWindowOwner(outputWindow, nullptr).IsApplied());
    assert(TrySetScalingWindowOwner(outputWindow, nullptr).IsApplied());
    assert(!TrySetScalingWindowOwner(nullptr, nullptr).IsApplied());
    const HWND destroyed = HiddenWindow();
    assert(DestroyWindow(destroyed));
    assert(TrySetScalingWindowOwner(outputWindow, destroyed).status == ScalingWindowOwnerStatus::InvalidOwner);
    HWND child = HiddenWindow(outputWindow);
    const int before = ownerCalls;
    assert(TrySetScalingWindowOwner(child, nullptr).status == ScalingWindowOwnerStatus::ChildWindow);
    assert(ownerCalls == before && GetParent(child) == outputWindow);
    Close(child);
    Close(outputWindow);

    Prepare(OwnerApiMode::AppliedWithError);
    result = TrySetScalingWindowOwner(outputWindow, source);
    assert(result.IsApplied() && result.systemError == ERROR_INVALID_WINDOW_HANDLE);
    result = TrySetScalingWindowOwner(outputWindow, nullptr);
    assert(result.IsApplied() && result.systemError == ERROR_INVALID_WINDOW_HANDLE);
    assert(SetScalingWindowOwner(outputWindow, nullptr) && GetLastError() == ERROR_SUCCESS);
    Close(outputWindow);

    Prepare(OwnerApiMode::Reject);
    result = TrySetScalingWindowOwner(outputWindow, source);
    assert(result.CanContinueUnowned() && result.systemError == ERROR_INVALID_PARAMETER);
    assert(GetWindow(outputWindow, GW_OWNER) == nullptr);
    Start(source);
    assert(!stopped && !reports && detachCalls == 0 && Logger::Get().warnings == 1);
    assert(IsWindow(outputWindow) && _renderer);
    Close(outputWindow);

    Prepare(OwnerApiMode::Native);
    Start(source);
    assert(!stopped && !reports && detachCalls == 1 && Logger::Get().warnings == 0);
    Close(outputWindow);

    Prepare(OwnerApiMode::Native);
    _options.reportErrorDetails = nullptr;
    rendererError = ScalingError::CaptureFailed;
    Start(source);
    assert(stopped && reports == 1 && ownerCalls == 0);
    Close(outputWindow);

    Prepare(OwnerApiMode::Native);
    _hwndRenderer = nullptr;
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    assert(CheckChildCreated() == ScalingError::ScalingWindowCreationFailed);
    Close(outputWindow);
    outputWindow = nullptr;
    assert(CheckOutputCreated() == ScalingError::ScalingWindowCreationFailed);

    Prepare(OwnerApiMode::Unexpected);
    bool rendererInitializationFailed = true;
    assert(_StartImpl(source, rendererInitializationFailed) == ScalingError::ScalingWindowCreationFailed);
    assert(!rendererInitializationFailed);
    Start(source);
    assert(stopped && reports == 1 && detachCalls == 0);
    assert(reportedSystemError == ERROR_ACCESS_DENIED);
    assert(reportedContext.find("step=initialize") != std::string::npos);
    assert(reportedContext.find("capture backend diagnostic") == std::string::npos);
    Close(outputWindow);

    Prepare(OwnerApiMode::Native);
    rendererError = ScalingError::CaptureFailed;
    Start(source);
    assert(stopped && reports == 1 && ownerCalls == 0);
    assert(reportedContext == "capture backend diagnostic" && reportedSystemError == 1234);
    Close(outputWindow);

    Prepare(OwnerApiMode::Reject);
    sourceHung = true;
    assert(_StartImpl(source, rendererInitializationFailed) == ScalingError::SourceWindowUnresponsive);
    assert(!rendererInitializationFailed && ownerCalls == 0);
    Close(outputWindow);

    Prepare(OwnerApiMode::Reject);
    HWND dyingSource = HiddenWindow();
    initializeAction = [=] { assert(DestroyWindow(dyingSource)); };
    assert(_StartImpl(dyingSource, rendererInitializationFailed) == ScalingError::SourceWindowClosed);
    assert(ownerCalls == 0);
    Close(outputWindow);

    Prepare(OwnerApiMode::DestroyOutput);
    result = TrySetScalingWindowOwner(outputWindow, nullptr);
    assert(result.status == ScalingWindowOwnerStatus::InvalidWindow && !result.IsApplied());
    Prepare(OwnerApiMode::DestroyOutput);
    Start(source);
    assert(stopped && reports == 1 && reportedSystemError == ERROR_INVALID_WINDOW_HANDLE);
    assert(!IsWindow(outputWindow));

    Prepare(OwnerApiMode::DestroySource);
    dyingSource = HiddenWindow();
    assert(_StartImpl(dyingSource, rendererInitializationFailed) == ScalingError::SourceWindowClosed);
    assert(!rendererInitializationFailed && detachCalls == 0);
    Close(outputWindow);

    Prepare(OwnerApiMode::Unexpected);
    result = TrySetScalingWindowOwner(outputWindow, nullptr);
    assert(result.status == ScalingWindowOwnerStatus::UnexpectedOwner && !result.IsApplied());
    assert(!result.CanContinueUnowned() && result.actualOwner == alternateOwner);
    Close(outputWindow);
    Close(alternateOwner);
    Close(source);
    std::cout << "PASS: hidden HWND owner readback, refusal, detach, invalid/child windows, destruction races; "
        "production startup continuation and stage diagnostics\n";
}

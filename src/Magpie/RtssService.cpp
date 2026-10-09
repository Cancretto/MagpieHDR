#include "pch.h"
#include "RtssService.h"
#include "Logger.h"
#include "Win32Helper.h"
#include "StrHelper.h"
#include <utility>

namespace Magpie {
namespace {

struct TargetIdentity {
	HWND source = nullptr, processWindow = nullptr;
	wil::unique_handle process;
	DWORD processId = 0;
	FILETIME creation{};
	std::string profile;
	std::wstring exe;
	bool Valid() const {
		if (!process || !IsWindow(source) || !IsWindow(processWindow)) return false;
		if (source != processWindow && !IsChild(source, processWindow)) return false;
		DWORD pid = 0, exitCode = 0;
		GetWindowThreadProcessId(processWindow, &pid);
		FILETIME created{}, exited{}, kernel{}, user{};
		return pid == processId && GetExitCodeProcess(process.get(), &exitCode) && exitCode == STILL_ACTIVE &&
			GetProcessTimes(process.get(), &created, &exited, &kernel, &user) &&
			created.dwHighDateTime == creation.dwHighDateTime && created.dwLowDateTime == creation.dwLowDateTime;
	}
	bool SameProcess(const TargetIdentity& other) const noexcept {
		return processId == other.processId && creation.dwHighDateTime == other.creation.dwHighDateTime &&
			creation.dwLowDateTime == other.creation.dwLowDateTime && _stricmp(profile.c_str(), other.profile.c_str()) == 0;
	}
};

std::string ProfileName(const std::wstring& name) {
	if (name.empty() || name.find_first_of(L"\\/:,\r\n*?\t") != std::wstring::npos ||
		_wcsicmp(std::filesystem::path(name).extension().c_str(), L".exe") != 0) return {};
	BOOL lossy = FALSE;
	const bool utf8 = GetACP() == CP_UTF8;
	const DWORD flags = utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
	BOOL* const loss = utf8 ? nullptr : &lossy;
	const int size = WideCharToMultiByte(CP_ACP, flags, name.c_str(), int(name.size()),
		nullptr, 0, nullptr, loss);
	if (!size || lossy) return {};
	std::string result(size_t(size), '\0');
	WideCharToMultiByte(CP_ACP, flags, name.c_str(), int(name.size()), result.data(), size, nullptr, loss);
	return lossy ? std::string{} : result;
}

std::shared_ptr<TargetIdentity> ResolveTarget(HWND source) {
	if (!IsWindow(source)) return {};
	HWND actual = source;
	const auto sourceName = Win32Helper::GetWindowExeName(source);
	if (_wcsicmp(sourceName.c_str(), L"ApplicationFrameHost.exe") == 0) {
		struct Children { HWND actual = nullptr; DWORD pid = 0; bool ambiguous = false; } children;
		EnumChildWindows(source, [](HWND child, LPARAM context) -> BOOL {
			auto& found = *reinterpret_cast<Children*>(context);
			if (Win32Helper::GetWindowClassName(child) != L"Windows.UI.Core.CoreWindow") return TRUE;
			DWORD pid = 0;
			GetWindowThreadProcessId(child, &pid);
			if (!pid || pid == GetCurrentProcessId()) return TRUE;
			if (found.pid && found.pid != pid) found.ambiguous = true;
			found.pid = pid; found.actual = child;
			return TRUE;
		}, reinterpret_cast<LPARAM>(&children));
		if (!children.actual || children.ambiguous) return {};
		actual = children.actual;
	}
	auto result = std::make_shared<TargetIdentity>();
	result->source = source; result->processWindow = actual;
	GetWindowThreadProcessId(actual, &result->processId);
	if (!result->processId || result->processId == GetCurrentProcessId()) return {};
	result->process.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, result->processId));
	FILETIME exited{}, kernel{}, user{};
	if (!result->process || !GetProcessTimes(result->process.get(), &result->creation, &exited, &kernel, &user)) return {};
	result->exe = Win32Helper::GetWindowExeName(actual);
	if (_wcsicmp(result->exe.c_str(), L"ApplicationFrameHost.exe") == 0 ||
		_wcsicmp(result->exe.c_str(), L"Magpie.exe") == 0) return {};
	result->profile = ProfileName(result->exe);
	return result->profile.empty() || !result->Valid() ? nullptr : result;
}

bool Incompatible(const std::wstring& error) {
	return error == L"UnsupportedInterface" || error == L"UnsupportedArchitecture";
}

void LogFailure(const char* stage, const std::wstring& error) {
	Logger::Get().Warn(fmt::format("RTSS {}: {}", stage, StrHelper::UTF16ToUTF8(error)));
}

const wchar_t* ProfileFailureText(const RtssProfileFailure& failure, const std::wstring& error, bool restoring) {
	if (failure.nativeFault || error == L"InterfaceFault") return L"Rtss_Failure_Interface";
	if (failure.fileError == ERROR_ACCESS_DENIED || failure.fileError == ERROR_PRIVILEGE_NOT_HELD) return L"Rtss_Failure_Permission";
	if (error == L"TimeModeUnsupported") return L"Rtss_Failure_TimeMode";
	switch (failure.stage) {
	case RtssFailureStage::Read: return L"Rtss_Failure_Read";
	case RtssFailureStage::Setting: return L"Rtss_Failure_Set";
	case RtssFailureStage::Save: return L"Rtss_Failure_Write";
	case RtssFailureStage::Verify: return L"Rtss_Failure_Verify";
	case RtssFailureStage::Reload: return L"Rtss_Failure_Reload";
	default: return restoring ? L"Rtss_Failure_Restore" : L"Rtss_Failure_Write";
	}
}

void LogProfileFailure(const char* operation, const std::wstring& error, const RtssProfileFailure& failure) {
	Logger::Get().Warn(fmt::format(
		"RTSS {}: {} stage={} profile={} version={} property={} requested={} observed={} observedKnown={} "
		"saveAttempted={} readbackComplete={} nativeFault={} fileError={}",
		operation, StrHelper::UTF16ToUTF8(error), int(failure.stage), failure.profile.empty() ? "Global" : failure.profile,
		StrHelper::UTF16ToUTF8(failure.version), failure.property, failure.requested, failure.observed, failure.observedKnown,
		failure.saveAttempted, failure.readbackComplete, failure.nativeFault, failure.fileError));
}

}

struct RtssService::Session {
	uint64_t generation = 0;
	RtssSettings settings;
	std::shared_ptr<TargetIdentity> initialTarget;
	std::atomic<std::shared_ptr<TargetIdentity>> target;
	std::weak_ptr<const uint8_t> identity;
	std::atomic<bool> cancelled = false, updateQueued = false;
	std::atomic<uint32_t> desiredRate = 0;
	std::atomic<bool> contentConstrained = false;
	std::string profile;
	RtssProfileSnapshot original, lastWritten;
	bool modified = false, created = false, suspended = false, finished = false;
};

RtssService& RtssService::Get() {
	// Process lifetime: a stalled third-party DLL must not force UI shutdown to
	// wait forever or outlive a freed service after the bounded cleanup deadline.
	static auto* instance = new RtssService;
	return *instance;
}
RtssService::~RtssService() { Uninitialize(); }

void RtssService::Initialize(winrt::DispatcherQueue dispatcher) {
	if (_thread.joinable()) return;
	_dispatcher = std::move(dispatcher);
	_closing = false; _notify = true;
	_thread = std::thread([this] { _Worker(); });
	Recheck(false);
}

void RtssService::Uninitialize() {
	if (!_thread.joinable()) return;
	_notify = false;
	if (_launchTimer) _launchTimer.Stop();
	End();
	{
		std::lock_guard lock(_mutex);
		_closing = true;
	}
	_wake.notify_one();
	// Give one cleanup attempt a bounded exit window. Its state remains alive
	// if the DLL stalls; process exit can abandon it without blocking shutdown.
	std::unique_lock lock(_mutex);
	const bool completed = _done.wait_for(lock, std::chrono::milliseconds(750), [this] { return _exited; });
	lock.unlock();
	if (completed) _thread.join(); else _thread.detach();
}

void RtssService::_Queue(std::function<void()> task) {
	{
		std::lock_guard lock(_mutex);
		if (_closing) return;
		_tasks.push_back(std::move(task));
	}
	_wake.notify_one();
}

void RtssService::_Worker() {
	winrt::init_apartment(winrt::apartment_type::multi_threaded);
	while (true) {
		std::function<void()> task;
		{
			std::unique_lock lock(_mutex);
			_wake.wait(lock, [this] { return _closing || !_tasks.empty(); });
			if (_tasks.empty()) break;
			task = std::move(_tasks.front()); _tasks.pop_front();
		}
		try { task(); }
		catch (...) {
			Logger::Get().Error("RTSS operation failed with an exception");
			auto status = Status(); status.state = RtssState::Failed; _Publish(std::move(status));
		}
	}
	winrt::uninit_apartment();
	{
		std::lock_guard lock(_mutex); _exited = true;
	}
	_done.notify_one();
}

RtssStatus RtssService::Status() const {
	std::lock_guard lock(_mutex); return _status;
}

void RtssService::_Publish(RtssStatus status) {
	{
		std::lock_guard lock(_mutex); _status = std::move(status);
	}
	if (_notify) _dispatcher.TryEnqueue([this] { if (_notify) StatusChanged.Invoke(); });
}

bool RtssService::_Prepare(RtssStatus& status) {
	status.rtss = RtssProfileAccess::DetectTool(false);
	status.afterburner = RtssProfileAccess::DetectTool(true);
	if (status.rtss.executable.empty()) { status.state = RtssState::NotInstalled; return false; }
	if (!status.rtss.running) { status.state = RtssState::NotRunning; return false; }
	std::wstring error;
	if (!_access.Initialize(status.rtss, error)) {
		status.state = Incompatible(error) ? RtssState::Incompatible : RtssState::Failed;
		status.failureStage = error == L"InterfaceFault" ? L"Rtss_Failure_Interface" : error == L"LibraryMissing" ?
			L"Rtss_Failure_LibraryMissing" : error == L"AccessDenied" ? L"Rtss_Failure_Permission" : L"Rtss_Failure_Access";
		LogProfileFailure("check", error, _access.Failure()); return false;
	}
	return true;
}

uint64_t RtssService::Begin(HWND target, std::weak_ptr<const uint8_t> identity, RtssSettings settings) {
	End();
	if (!settings.IsValid() || settings.scope == RtssScope::None) return ++_generation;
	auto session = std::make_shared<Session>();
	session->generation = ++_generation; session->identity = std::move(identity);
	session->settings = settings.IsValid() ? settings : RtssSettings{};
	session->initialTarget = ResolveTarget(target);
	session->target = session->initialTarget;
	if (settings.scope == RtssScope::Target && session->initialTarget) session->profile = session->initialTarget->profile;
	if (settings.rateMode == RtssRateMode::Custom) session->desiredRate = settings.customRate;
	_uiSession = session;
	_Queue([this, session] { _workerSession = session; _TryApply(session); });
	return session->generation;
}

void RtssService::ContentTargetChanged(uint64_t sessionId, HWND source, bool fixed, double baseRate, bool constrained) {
	const auto session = _uiSession;
	if (!session || session->generation != sessionId || session->cancelled) return;
	const auto previousTarget = session->target.load();
	if (!previousTarget || previousTarget->source != source || !previousTarget->Valid()) {
		const auto rebound = ResolveTarget(source);
		if (!rebound || !session->initialTarget || !rebound->SameProcess(*session->initialTarget)) {
			End();
			_Queue([this, identity = session->identity] {
				auto status = Status(); status.profileIdentity = identity;
				status.state = RtssState::Failed; status.failureStage = L"Rtss_Failure_Target";
				_Publish(std::move(status));
			});
			return;
		}
		// A new HWND in the same process is an internal rebuild, not a new baseline.
		session->target = rebound;
	}
	if (session->settings.rateMode == RtssRateMode::ContentSync) {
		const auto rate = session->settings.ResolveRate(fixed, baseRate).value_or(0);
		if (session->desiredRate.exchange(rate) != rate && _notify) {
			_dispatcher.TryEnqueue([this] { if (_notify) StatusChanged.Invoke(); });
		}
		session->contentConstrained = constrained;
	}
	if (session->updateQueued.exchange(true)) return;
	_Queue([this, session] {
		session->updateQueued = false;
		_TryApply(session);
	});
}

void RtssService::End() {
	if (!_uiSession) return;
	auto session = std::exchange(_uiSession, nullptr);
	session->cancelled = true;
	// Clear the display even if nothing was written and _Finish publishes no result.
	if (_notify) _dispatcher.TryEnqueue([this] { if (_notify) StatusChanged.Invoke(); });
	_Queue([this, session] {
		_Finish(session);
		if (_workerSession == session) _workerSession.reset();
	});
}

uint32_t RtssService::ContentSyncRate(std::weak_ptr<const uint8_t> identity) const noexcept {
	const auto session = _uiSession;
	const auto profile = identity.lock();
	if (!session || session->cancelled || !profile || profile != session->identity.lock() ||
		session->settings.rateMode != RtssRateMode::ContentSync) return 0;
	return session->desiredRate.load();
}

void RtssService::ReleaseProfile(std::weak_ptr<const uint8_t> identity) {
	if (_uiSession && identity.lock() == _uiSession->identity.lock()) End();
}

void RtssService::_TryApply(const std::shared_ptr<Session>& session) {
	if (session->cancelled || session->suspended || session->finished || session->settings.scope == RtssScope::None) return;
	RtssStatus status;
	auto target = session->target.load();
	status.profileIdentity = session->identity;
	status.profile = session->settings.scope == RtssScope::Global ? L"Global" : target ? target->exe : std::wstring{};
	status.desiredRate = session->desiredRate.load();
	status.contentConstrained = session->contentConstrained;
	if (!_Prepare(status)) { _Publish(std::move(status)); return; }
	if (target && !target->Valid()) {
		const auto rebound = ResolveTarget(target->source);
		if (rebound && session->initialTarget && rebound->SameProcess(*session->initialTarget)) {
			target = rebound; session->target = rebound;
		}
	}
	if (!target || !target->Valid()) {
		status.state = RtssState::Failed; status.failureStage = L"Rtss_Failure_Target"; _Publish(std::move(status)); return;
	}
	if (!status.desiredRate) { status.state = RtssState::WaitingContent; _Publish(std::move(status)); return; }
	RtssProfileSnapshot before;
	std::wstring error;
	status.state = RtssState::Applying; _Publish(status);
	if (!_access.Read(session->profile, before, error)) {
		LogProfileFailure("read", error, _access.Failure());
		status.state = RtssState::Failed; status.failureStage = ProfileFailureText(_access.Failure(), error, false);
		_Publish(std::move(status)); return;
	}
	if (session->modified && (!RtssProfileAccess::SameRate(before, session->lastWritten) ||
		(session->created && (before.contents != session->lastWritten.contents || before.detectionLevel != session->lastWritten.detectionLevel)))) {
		session->suspended = true; status.state = RtssState::Conflict; _Publish(std::move(status)); return;
	}
	if (session->modified && before.rate[0] == status.desiredRate && before.rate[1] == 1 && before.rate[2] == 0) {
		status.state = RtssState::Written; status.currentRate = status.desiredRate;
		status.rateKnown = true;
		status.detectionDisabled = !before.detectionLevel; _Publish(std::move(status)); return;
	}
	if (session->cancelled || !target->Valid()) return;
	if (!session->modified) { session->original = before; session->created = !before.exists; }
	RtssProfileSnapshot written;
	bool saveAttempted = false, readbackComplete = false;
	bool success = false;
	try {
		success = _access.Apply(session->profile, status.desiredRate, before, written, saveAttempted, readbackComplete, error,
			[session, target, rate = status.desiredRate] { return !session->cancelled && target->Valid() && session->desiredRate == rate; });
	} catch (...) { error = L"WriteException"; }
	if (written.exists && written.rate[0] == status.desiredRate && written.rate[1] == 1 && written.rate[2] == 0) {
		session->modified = true; session->lastWritten = written;
	} else if (saveAttempted && !(readbackComplete && RtssProfileAccess::SameRate(before, written))) {
		// Save is void: even failed readback may follow a real external write.
		// Retain the original and try guarded cleanup once. A new profile with
		// unverified content cannot satisfy deletion ownership checks.
		session->modified = true;
		session->lastWritten = before;
		if (!before.exists) session->lastWritten.contents.clear();
		session->lastWritten.exists = true;
		session->lastWritten.rate = { status.desiredRate, 1, 0, before.rate[3] };
	}
	if (!success) {
		if (error == L"Cancelled") return;
		LogProfileFailure("write", error, _access.Failure()); status.state = error == L"Conflict" ? RtssState::Conflict : RtssState::Failed;
		status.failureStage = error == L"Conflict" ? L"" : ProfileFailureText(_access.Failure(), error, false);
		session->suspended = true; _Publish(std::move(status)); return;
	}
	status.state = RtssState::Written; status.currentRate = status.desiredRate;
	status.rateKnown = true;
	status.detectionDisabled = !written.detectionLevel;
	if (session->profile.empty()) {
		RtssProfileSnapshot targetProfile;
		if (_access.Read(target->profile, targetProfile, error)) status.targetOverridesGlobal = targetProfile.exists;
	}
	_Publish(std::move(status));
}

void RtssService::_Finish(const std::shared_ptr<Session>& session) {
	if (session->finished) return;
	session->finished = true;
	if (!session->modified) return;
	auto status = Status();
	status.rateKnown = false;
	status.failureStage.clear();
	status.profileIdentity = session->identity;
	status.profile = session->profile.empty() ? L"Global" : session->initialTarget->exe;
	if (session->settings.exitPolicy == RtssExitPolicy::KeepChanges) {
		RtssProfileSnapshot current;
		std::wstring error;
		if (_access.Read(session->profile, current, error)) {
			if (!RtssProfileAccess::SameRate(current, session->lastWritten)) {
				status.state = RtssState::Conflict; _Publish(std::move(status)); return;
			}
			status.currentRate = double(current.rate[0]) / current.rate[1];
			status.timeLimit = current.rate[2] > 0; status.rateKnown = true;
			status.detectionDisabled = !current.detectionLevel;
		}
		status.state = RtssState::Kept; _Publish(std::move(status)); return;
	}
	std::wstring error;
	bool conflict = false;
	bool restored = false;
	try { restored = _access.Restore(session->profile, session->original, session->lastWritten, session->created, conflict, error); }
	catch (...) { error = L"RestoreException"; }
	if (!restored) {
		LogProfileFailure("restore", error, _access.Failure()); status.state = conflict ? RtssState::Conflict : RtssState::RestoreFailed;
		status.failureStage = conflict ? L"" : ProfileFailureText(_access.Failure(), error, true);
	} else status.state = RtssState::Restored;
	_Publish(std::move(status));
}

void RtssService::Recheck(bool retry) {
	_Queue([this, retry] {
		if (_workerSession && !_workerSession->cancelled) {
			// A conflict stays suspended; a failed write may be retried explicitly.
			if (retry && Status().state != RtssState::Conflict) _workerSession->suspended = false;
			_TryApply(_workerSession); return;
		}
		RtssStatus status;
		if (_Prepare(status)) status.state = RtssState::WaitingTarget;
		_Publish(std::move(status));
	});
}

void RtssService::Launch(bool afterburner) {
	if (_launchPending) return;
	_launchPending = true;
	_Queue([this, afterburner] {
		auto status = Status(); status.state = RtssState::StartingTool;
		status.profile.clear(); status.failureStage.clear(); _Publish(status);
		std::wstring error;
		const auto tool = RtssProfileAccess::DetectTool(afterburner);
		const bool success = RtssProfileAccess::LaunchTool(tool, error);
		if (!success) { LogFailure("launch", error); status.state = RtssState::ToolStartFailed; _Publish(status); }
		if (_notify) _dispatcher.TryEnqueue([this, success, afterburner] {
			if (!success || !_notify) { _launchPending = false; return; }
			// One bounded readiness window after an explicit user launch. Checking
			// every 500 ms for 10 s only queries running state; it never writes per frame.
			_launchTimer = _dispatcher.CreateTimer();
			_launchTimer.Interval(std::chrono::milliseconds(500));
			_launchTimer.Tick([this, afterburner, attempts = 0](winrt::DispatcherQueueTimer const& timer, winrt::IInspectable const&) mutable {
				const auto s = Status();
				const bool selectedRunning = afterburner ? s.afterburner.running : s.rtss.running;
				const bool timeout = ++attempts >= 20;
				if (!_notify || (selectedRunning && s.rtss.running) || timeout) {
					timer.Stop(); _launchPending = false;
					StatusChanged.Invoke();
				}
				if (_notify && timeout && !selectedRunning) {
					_Queue([this, afterburner] {
						const auto tool = RtssProfileAccess::DetectTool(afterburner);
						if (tool.running) { Recheck(false); return; }
						auto status = Status(); status.state = RtssState::ToolStartFailed;
						status.profile = afterburner ? L"MSI Afterburner" : L"RTSS";
						status.failureStage = L"Rtss_Failure_StartTimeout";
						_Publish(std::move(status));
					});
					return;
				}
				if (_notify) Recheck(false);
			});
			_launchTimer.Start();
		});
	});
}

void RtssService::Preview(RtssSettings settings, std::wstring executable, std::function<void(RtssStatus)> completed) {
	_Queue([this, settings, executable = std::move(executable), completed = std::move(completed)] {
		RtssStatus status;
		if (_Prepare(status)) {
			std::string profile;
			if (settings.scope == RtssScope::Target) profile = ProfileName(std::filesystem::path(executable).filename().native());
			if (settings.scope == RtssScope::Target && profile.empty()) status.state = RtssState::WaitingTarget;
			else {
				RtssProfileSnapshot snapshot;
				std::wstring error;
				status.profile = profile.empty() ? L"Global" : std::filesystem::path(executable).filename().native();
				if (_access.Read(profile, snapshot, error)) {
					status.state = snapshot.exists ? RtssState::Read : RtssState::MissingProfile;
					status.currentRate = double(snapshot.rate[0]) / snapshot.rate[1];
					status.rateKnown = true;
					status.timeLimit = snapshot.rate[2] > 0;
					status.detectionDisabled = !snapshot.detectionLevel;
				} else {
					status.state = RtssState::Failed; status.failureStage = ProfileFailureText(_access.Failure(), error, false);
					LogProfileFailure("preview", error, _access.Failure());
				}
			}
		}
		if (_notify) _dispatcher.TryEnqueue([this, status = std::move(status), completed] {
			if (_notify) completed(status);
		});
	});
}

}

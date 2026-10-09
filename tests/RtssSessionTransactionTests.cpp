// Reuse the faithful profile-export fixture with the actual service transaction bodies.
#define wmain RtssProfileUnitMain
#include "RtssProfileTransactionTests.cpp"
#undef wmain
#include "RtssSettings.h"
#include <atomic>
#include <memory>

namespace Magpie {
#include "RtssSessionCases.g.h"
namespace {
struct TargetIdentity {
	std::string profile = "game.exe";
	std::wstring exe = L"game.exe";
	HWND source = nullptr;
	bool valid = true;
	bool Valid() const { return valid; }
	bool SameProcess(const TargetIdentity&) const { return true; }
};
std::shared_ptr<TargetIdentity> ResolveTarget(HWND) { return {}; }
RtssProfileFailure loggedFailure;
void LogProfileFailure(const char*, const std::wstring&, const RtssProfileFailure& failure) { loggedFailure = failure; }
}
class RtssService {
public:
	struct Session;
	RtssProfileAccess& _access;
	RtssStatus _status;
	unsigned publications = 0;
	explicit RtssService(RtssProfileAccess& access) : _access(access) {}
	bool _Prepare(RtssStatus&) { return true; }
	void _Publish(RtssStatus value) { _status = std::move(value); ++publications; }
	RtssStatus Status() const { return _status; }
	void _TryApply(const std::shared_ptr<Session>& session);
	void _Finish(const std::shared_ptr<Session>& session);
};
#include "RtssSessionTransactions.g.h"
}

int wmain(int argc, wchar_t** argv) {
	try {
		if (argc != 2) throw std::runtime_error("Expected output directory");
		const auto output = std::filesystem::path(argv[1]) / ("session-run-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
		auto session = [](RtssScope scope, RtssExitPolicy exit) {
			auto s = std::make_shared<RtssService::Session>();
			s->settings.scope = scope; s->settings.exitPolicy = exit;
			s->initialTarget = std::make_shared<TargetIdentity>(); s->target = s->initialTarget;
			s->profile = scope == RtssScope::Global ? "" : "game.exe";
			s->desiredRate = 60;
			return s;
		};
		for (const auto scope : {RtssScope::Global, RtssScope::Target}) for (const bool existing : {false, true})
			for (const auto exit : {RtssExitPolicy::KeepChanges, RtssExitPolicy::RestoreOriginal}) {
				Fixture f(output);
				WriteBytes(f.Path(""), Profile(30));
				if (existing) WriteBytes(f.Path("game.exe"), Profile(30));
				RtssService service(f.access);
				const auto s = session(scope, exit);
				service._TryApply(s);
				Require(s->modified && service._status.state == RtssState::Written && s->original.rate[0] == 30, "Session actual write");
				s->settings.rateMode = RtssRateMode::ContentSync; s->desiredRate = 72;
				service._TryApply(s);
				Require(service._status.state == RtssState::Written && s->original.rate[0] == 30 && s->lastWritten.rate[0] == 72, "Sync update preserves first baseline");
				s->cancelled = true;
				service._Finish(s);
				const auto saves = f.saves, deletes = f.deletes, publications = service.publications;
				service._Finish(s);
				Require(s->finished && f.saves == saves && f.deletes == deletes && service.publications == publications, "One cleanup attempt only");
				const auto actual = f.Snapshot(s->profile);
				if (exit == RtssExitPolicy::KeepChanges) Require(service._status.state == RtssState::Kept && actual.exists && actual.rate[0] == 72, "Keep actual saved state");
				else Require(service._status.state == RtssState::Restored && actual.exists == (scope == RtssScope::Global || existing) && actual.rate[0] == 30, "Restore actual baseline/inheritance");
			}
		for (int kind = 0; kind < 7; ++kind) {
			Fixture f(output);
			RtssService service(f.access);
			const auto s = session(kind == 2 ? RtssScope::Target : RtssScope::Global, RtssExitPolicy::RestoreOriginal);
			if (kind == 0) s->cancelled = true;
			if (kind == 1) f.saveMode = SaveMode::Noop;
			if (kind == 2) f.saveMode = SaveMode::Partial;
			if (kind == 3) f.saveMode = SaveMode::BadFile;
			if (kind == 4) f.saveMode = SaveMode::Fault;
			if (kind == 5) f.reject = "FramerateLimit";
			if (kind == 6) f.reloadFault = true;
			service._TryApply(s);
			if (kind == 0) Require(f.saves == 0 && !s->modified, "Cancelled session never writes");
			if (kind == 1) Require(!s->modified && service._status.failureStage == L"Rtss_Failure_Write", "Void unchanged save has no fabricated ownership");
			if (kind == 2) Require(s->modified && service._status.failureStage == L"Rtss_Failure_Verify", "Partial new save retains cleanup ownership");
			if (kind == 3 || kind == 4 || kind == 6) Require(s->modified, "Ambiguous write retains guarded cleanup");
			if (kind == 5) Require(!s->modified && service._status.failureStage == L"Rtss_Failure_Set" && loggedFailure.property == "FramerateLimit", "Service setter failure mapped and logged");
			service._Finish(s);
			if (kind == 2) Require(service._status.state == RtssState::Restored && !f.Snapshot("game.exe").exists, "Owned partial new profile removed");
			if (kind == 3 || kind == 4 || kind == 6) Require(service._status.state == RtssState::RestoreFailed, "Unverifiable/faulted cleanup reports failure");
			const auto saves = f.saves, deletes = f.deletes;
			service._Finish(s);
			Require(f.saves == saves && f.deletes == deletes, "Failed cleanup is not retried");
		}
		{
			Fixture f(output);
			RtssService service(f.access);
			const auto s = session(RtssScope::Global, RtssExitPolicy::RestoreOriginal);
			service._TryApply(s);
			Require(SetFileAttributesW(f.Path("").c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "Session restore permission fixture");
			service._Finish(s);
			SetFileAttributesW(f.Path("").c_str(), FILE_ATTRIBUTE_NORMAL);
			Require(service._status.state == RtssState::RestoreFailed && service._status.failureStage == L"Rtss_Failure_Permission" && f.saves == 1,
				"Restore permission failure is precise and performs no save");
			service._Finish(s);
			Require(f.saves == 1 && s->finished, "Permission cleanup is not retried");
		}
		{
			Fixture f(output);
			RtssService service(f.access);
			const auto s = session(RtssScope::Global, RtssExitPolicy::RestoreOriginal);
			service._TryApply(s);
			WriteBytes(f.Path(""), Profile(59));
			service._Finish(s);
			Require(service._status.state == RtssState::Conflict && f.Snapshot("").rate[0] == 59 && f.saves == 1, "Service restore conflict preserves external value");
		}
		std::cout << "RTSS production session transactions: " << checks << " checks passed.\n";
		return 0;
	} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

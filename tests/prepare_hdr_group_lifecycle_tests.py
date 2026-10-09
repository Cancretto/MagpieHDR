"""Extract production HDR switch scheduling/rebuilds; mock dispatcher/windows only."""
from pathlib import Path
import sys

repo = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
runtime = (repo / 'src/Magpie.Core/ScalingRuntime.cpp').read_text(encoding='utf-8-sig')
runtime = runtime[runtime.index('void ScalingRuntime::UpdateHdrGroupFromSettings('):runtime.index('\nstatic std::optional<bool> IsSrcRepositioning(')]
window = (repo / 'src/Magpie.Core/ScalingWindow.cpp').read_text(encoding='utf-8-sig')
window = window[window.index('void ScalingWindow::RestartWithHdrGroup('):window.index('\nbool ScalingWindow::QueueEffectParameterRestart(')]
prefix = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>
using HWND=int;
bool sourceAvailable=true;
bool IsWindow(HWND hwnd) { return hwnd && sourceAvailable; }
namespace Magpie {
enum class ScalingState {Idle,Starting,Scaling,Stopping,Waiting};
struct EffectOption {std::string name;};
struct EffectParameterSessionState {
 std::vector<EffectOption> effects;
 EffectParameterSessionState(std::vector<EffectOption> value,int):effects(std::move(value)) {}
};
struct ScalingOptions {
 uint32_t scalingModeIdx=3;
 std::wstring scalingModeName=L"Test";
 uint64_t hdrGroupRevision=1;
 std::optional<bool> hdrCaptureIntent=true;
 std::vector<EffectOption> effects{{"old"}};
 int frameRefresh=8;
 std::shared_ptr<EffectParameterSessionState> parameterSession=std::make_shared<EffectParameterSessionState>(effects,frameRefresh);
};
std::vector<std::string> trace;
struct Renderer {
 int CaptureOverlayState() const {return 42;}
 void RestoreOverlayState(int state) { assert(state==42); trace.push_back("restore-overlay"); }
};
struct Logger { static Logger& Get(){static Logger v;return v;} void Info(const char*){} };
struct ScalingWindow {
 struct HdrGroupRestart {std::vector<EffectOption> effects;std::optional<bool> captureIntent;uint64_t revision;};
 static ScalingWindow& Get(){static ScalingWindow v;return v;}
 ScalingOptions _options;
 struct Tracker {HWND Handle() const {return 7;}} _srcTracker;
 std::unique_ptr<Renderer> _renderer=std::make_unique<Renderer>();
 bool active=true,held=false,waiting=false,_isSrcRepositioning=false;
 int _restartOverlayState=42;
 std::optional<int> _pendingManualParameterRestart;
 std::optional<HdrGroupRestart> _pendingHdrGroupRestart;
 std::function<void()> duringDestroy;
 HWND Handle()const{return active?100:0;}
 explicit operator bool() const{return active;}
 bool HasHeldParameterInput()const{return held;}
 bool IsWaitingForParameterRestart()const{return waiting;}
 bool IsSrcRepositioning()const{return _isSrcRepositioning;}
 const ScalingOptions& Options()const{return _options;}
 void _CancelParameterRestart(){waiting=false;trace.push_back("cancel-parameters");}
 void Destroy(){
  assert(_isSrcRepositioning && _options.hdrGroupRevision==1);
  trace.push_back("joined-old-backends");active=false;_renderer.reset();if(duringDestroy)duringDestroy();
 }
 void Start(HWND source,ScalingOptions&&){
  assert(source==7 && _options.hdrGroupRevision>=2 && !_options.parameterSession->effects.empty());
  trace.push_back("start-new-color-contract");active=true;_renderer=std::make_unique<Renderer>();
 }
 void Stop(){trace.push_back("stop");active=false;waiting=false;_pendingHdrGroupRestart.reset();}
 void RestartWithHdrGroup(std::vector<EffectOption>&&,std::optional<bool>,uint64_t)noexcept;
 void Reset(){
  _options=ScalingOptions{};_renderer=std::make_unique<Renderer>();active=true;held=false;waiting=false;
  _isSrcRepositioning=false;_pendingHdrGroupRestart.reset();_pendingManualParameterRestart.reset();
  duringDestroy={};sourceAvailable=true;trace.clear();
 }
};
struct Queue {
 std::deque<std::function<void()>> work;
 void TryEnqueue(std::function<void()> value){work.push_back(std::move(value));}
 void Run(){while(!work.empty()){auto value=std::move(work.front());work.pop_front();value();}}
};
struct ScalingRuntime {
 std::atomic<uint64_t> _commandGeneration=1,_hdrGroupGeneration=0;
 ScalingState state=ScalingState::Scaling;
 Queue queue;
 Queue& _Dispatcher(){return queue;}
 ScalingState State()const{return state;}
 void _State(ScalingState value){state=value;}
 void UpdateHdrGroupFromSettings(uint32_t,std::wstring,uint64_t,std::vector<EffectOption>,std::optional<bool>);
};
'''
tests = r'''
int Run() {
 auto& w=ScalingWindow::Get();
 w.Reset(); auto old=w._options.parameterSession;
 ScalingRuntime first;
 first.UpdateHdrGroupFromSettings(3,L"Test",2,{{"native-1500"}},false);
 first.queue.Run();
 assert(w._options.hdrGroupRevision==2 && w._options.hdrCaptureIntent==false && w._options.effects[0].name=="native-1500");
 assert(w._options.parameterSession!=old && old->effects[0].name=="old");
 assert(trace==std::vector<std::string>({"cancel-parameters","joined-old-backends","start-new-color-contract","restore-overlay"}));
 w.Reset(); ScalingRuntime latest;
 latest.UpdateHdrGroupFromSettings(3,L"Test",2,{{"superseded"}},false);
 latest.UpdateHdrGroupFromSettings(3,L"Test",3,{{"latest-rtx"}},true); latest.queue.Run();
 assert(w._options.hdrGroupRevision==3 && w._options.effects[0].name=="latest-rtx");
 w.Reset(); ScalingRuntime stopped;
 stopped.UpdateHdrGroupFromSettings(3,L"Test",2,{{"cancelled"}},false);
 ++stopped._commandGeneration; stopped.state=ScalingState::Stopping; stopped.queue.Run();
 assert(trace.empty() && w._options.hdrGroupRevision==1);
 w.Reset(); ScalingRuntime other;
 other.UpdateHdrGroupFromSettings(4,L"Other",2,{{"wrong-mode"}},false); other.queue.Run(); assert(trace.empty());
 other.UpdateHdrGroupFromSettings(3,L"Test",1,{{"old-revision"}},false); other.queue.Run(); assert(trace.empty());
 w.Reset(); w.held=true; ScalingRuntime held;
 held.UpdateHdrGroupFromSettings(3,L"Test",2,{{"held-2"}},false); held.queue.Run();
 held.UpdateHdrGroupFromSettings(3,L"Test",3,{{"held-3"}},true); held.queue.Run();
 assert(trace.empty() && w._pendingHdrGroupRestart && w._pendingHdrGroupRestart->revision==3);
 w.held=false; auto pending=std::move(*w._pendingHdrGroupRestart);
 w.RestartWithHdrGroup(std::move(pending.effects),pending.captureIntent,pending.revision);
 assert(w._options.hdrGroupRevision==3 && !w._pendingHdrGroupRestart);
 w.Reset(); w.active=false; w.waiting=true; w._renderer.reset(); ScalingRuntime waiting;
 waiting.state=ScalingState::Starting;
 waiting.UpdateHdrGroupFromSettings(3,L"Test",2,{{"new-during-wait"}},false); waiting.queue.Run();
 assert(w.active && !w.waiting && waiting.State()==ScalingState::Scaling);
 assert(trace==std::vector<std::string>({"cancel-parameters","start-new-color-contract","restore-overlay"}));
 w.Reset(); ScalingRuntime interrupted;
 w.duringDestroy=[&]{++interrupted._commandGeneration;interrupted.state=ScalingState::Stopping;};
 interrupted.UpdateHdrGroupFromSettings(3,L"Test",2,{{"interrupted"}},false); interrupted.queue.Run();
 assert(!w.active && interrupted.State()==ScalingState::Stopping && trace.back()=="stop");
 w.Reset(); sourceAvailable=false; ScalingRuntime unavailable;
 unavailable.UpdateHdrGroupFromSettings(3,L"Test",2,{{"missing-source"}},false); unavailable.queue.Run();
 assert(trace.empty() && w._options.hdrGroupRevision==1);
 std::cout<<"PASS: production HDR switch dispatch/rebuild order, new sessions, latest update, cancellation, identity/revision, held input, paused restart and unavailable sources\n";
 return 0;
}
}
int main(){return Magpie::Run();}
'''
(out / 'HdrGroupLifecycleTests.cpp').write_text(prefix+window+runtime+tests,encoding='utf-8')

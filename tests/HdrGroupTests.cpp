#include "HdrGroupPlan.h"
#include <cassert>
#include <iostream>
#include <limits>

using namespace Magpie;
template<class String> struct Effect {
	String name;
	std::map<String,float> parameters;
	int scalingType = 0;
	std::pair<float,float> scale{1,1};
	HdrManagedRole hdrManagedRole = HdrManagedRole::None;
	bool HasScale() const { return scalingType != 0 || scale != std::pair<float,float>{1,1}; }
};
using FX = Effect<std::string>;
FX fx(std::string name) { return {.name=std::move(name)}; }
HdrComponentPlan stages(const HdrGroupPlan<FX>& plan, const HdrGroupSettings& settings) {
	assert(plan.error == HdrGroupError::None);
	const auto color = BuildHdrComponentPlan(plan.effects,settings.CaptureIntent());
	assert(color.error == HdrComponentError::None);
	return color;
}
int main() {
	HdrGroupSettings s; s.managed=true; s.input=HdrGroupInput::Hdr;
	const auto direct = BuildHdrGroupPlan(std::vector{fx("Bicubic"),fx("FSR2\\FSR2_SR")},s);
	assert(direct.effects.size()==2 && direct.keepsHdr && direct.sdrSegments==0);
	assert(stages(direct,s).captureHdr && stages(direct,s).outputHdr);
	const auto grouped = BuildHdrGroupPlan(std::vector{fx("CAS\\CAS"),fx("CRT\\CRT_Easymode")},s);
	assert(grouped.effects.size()==4 && grouped.sdrSegments==1 && grouped.restoresPair);
	const auto paired = stages(grouped,s);
	assert(paired.stages.back().pairIndex==0);
	const auto separated = BuildHdrGroupPlan(std::vector{fx("CAS\\CAS"),fx("ACNet"),fx("CAS\\CAS")},s);
	assert(separated.sdrSegments==3 && stages(separated,s).outputHdr);
	const auto rebuild = BuildHdrGroupPlan(grouped.effects,s);
	assert(rebuild.effects.size()==grouped.effects.size());
	assert(HdrGroupPlansEquivalent(grouped.effects,s.CaptureIntent(),rebuild.effects,s.CaptureIntent()));
	auto shared = separated.effects;
	shared.front().parameters["whiteNits"]=257.25f;
	SynchronizeHdrGroupParameterChanges(separated.effects,shared);
	for(const auto& effect:shared) if(effect.hdrManagedRole==HdrManagedRole::InputMapping) assert(effect.parameters.at("whiteNits")==257.25f);
	CaptureHdrGroupParameters(shared,s); assert(s.whiteNits==257.25f);

	s.output=HdrGroupOutput::Sdr;
	const auto display = BuildHdrGroupPlan(grouped.effects,s);
	assert(!stages(display,s).outputHdr && !display.restoresPair);
	assert(display.effects.front().parameters.at("mode")==1);
	s.output=HdrGroupOutput::RtxHdr; s.rtxPeakNits=1500;
	const auto rtx = BuildHdrGroupPlan(display.effects,s);
	assert(rtx.effects.back().name=="RTXVideo\\RTXVideo_HDR" && stages(rtx,s).outputHdr);
	s.output=HdrGroupOutput::NativeWhite; s.nativeWhiteNits=4000;
	const auto native = BuildHdrGroupPlan(rtx.effects,s);
	assert(native.effects.back().name=="Color\\SDR_to_HDR" && stages(native,s).stages.back().whiteNits==4000);
	assert(s.rtxPeakNits==1500); // inactive method preference remains independent
	s.output=HdrGroupOutput::RestorePair;
	assert(BuildHdrGroupPlan(std::vector{fx("Bicubic")},s).error==HdrGroupError::MissingPair);
	s.output=HdrGroupOutput::Auto;
	const auto unknown = std::vector{fx("Custom\\Mystery")};
	assert(BuildHdrGroupPlan(unknown,s).error==HdrGroupError::UnknownEffect);
	s.input=HdrGroupInput::Sdr;
	assert(stages(BuildHdrGroupPlan(unknown,s),s).outputHdr);
	s.input=HdrGroupInput::Hdr;
	assert(BuildHdrGroupPlan(std::vector{fx("DLSSFG\\DLSS_FrameGeneration"),fx("CAS\\CAS")},s).error==HdrGroupError::FrameGenerationPosition);
	const auto fg = BuildHdrGroupPlan(std::vector{fx("CAS\\CAS"),fx("DLSSFG\\DLSS_FrameGeneration")},s);
	assert(fg.effects.back().name=="DLSSFG\\DLSS_FrameGeneration" && stages(fg,s).outputHdr);
	auto forged=grouped.effects; forged.front().name="CAS\\CAS";
	assert(BuildHdrGroupPlan(forged,s).error==HdrGroupError::InvalidOwnership);
	s.nativeWhiteNits=std::numeric_limits<float>::quiet_NaN();
	assert(BuildHdrGroupPlan(grouped.effects,s).error==HdrGroupError::InvalidSettings);

	std::vector<FX> legacy{fx("Color\\HDR_to_SDR"),fx("CAS\\CAS"),fx("Color\\SDR_to_HDR")};
	legacy.front().parameters={{"whiteNits",203.375f},{"exposure",1.337f}};
	const auto adoption=TryAdoptHdrGroup(legacy); assert(adoption);
	assert(HdrGroupPlansEquivalent(legacy,std::nullopt,adoption->second,adoption->first.CaptureIntent()));
	legacy.insert(legacy.end(),fx("Color\\HDR_to_SDR"));
	assert(!TryAdoptHdrGroup(legacy));
	std::vector<FX> low{fx("CAS\\CAS"),fx("Color\\SDR_to_HDR")};
	low.back().parameters={{"whiteNits",80},{"exposure",.1f},{"peakNits",1500}};
	const auto lowAdoption=TryAdoptHdrGroup(low); assert(lowAdoption);
	assert(lowAdoption->first.nativeLegacy && lowAdoption->first.nativeWhiteNits==8 && lowAdoption->first.legacyPeakNits==1500);
	assert(HdrGroupPlansEquivalent(low,std::nullopt,lowAdoption->second,lowAdoption->first.CaptureIntent()));
	std::vector<FX> needless{fx("Color\\HDR_to_SDR"),fx("Bicubic"),fx("Color\\SDR_to_HDR")};
	assert(!TryAdoptHdrGroup(needless)); // native HDR plan removes lossy boundaries: not an equivalent adoption
	HdrGroupSettings wide; wide.managed=true; wide.input=HdrGroupInput::Hdr;
	const auto unicode=BuildHdrGroupPlan(std::vector<Effect<std::wstring>>{{.name=L"CAS\\CAS"}},wide);
	assert(unicode.error==HdrGroupError::None && unicode.effects.front().name==L"Color\\HDR_to_SDR");
	std::cout << "PASS: explicit HDR domain, safe SDR spans/barriers, ownership/idempotence, shared snapshots, methods, FG, unknown effects, strict adoption and legacy pixels/metadata\n";
}

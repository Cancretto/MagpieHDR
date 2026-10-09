#include "NvidiaVfxParameterPolicy.h"
#include "NvidiaVfxExecutionPlan.h"
#include <cassert>
#include <limits>
#include <map>
#include <vector>

struct StoredEffect {
	std::string name;
	std::map<std::string,float> parameters;
	int scalingType = 1;
	std::pair<float,float> scale{1.f,1.f};
};
int main() {
	using namespace Magpie;
	const int expected[5][4]{{1,2,3,4},{16,17,18,19},{-1,21,-1,23},{8,9,10,11},{12,13,14,15}};
	for (int family=0; family<5; ++family) for(int tier=0; tier<4; ++tier) {
		assert(NvidiaVfxTierValid(family,float(tier)) == (expected[family][tier] != -1));
		if(expected[family][tier] != -1) assert(NvidiaVfxQualityLevel(family,tier) == uint32_t(expected[family][tier]));
	}
	for (uint32_t reserved : {0u,5u,6u,7u,20u,22u,24u}) assert(!NvidiaVfxQualityValid(reserved));
	StoredEffect vsr{std::string(NVIDIA_VFX_VSR_ID),{{"strength",2.f}},1,{.9f,.9f}};
	ChangeNvidiaVfxVsrCategory(vsr,2); assert(vsr.parameters.at("strength") == 1.f);
	vsr.parameters["strength"]=3.f;
	ChangeNvidiaVfxVsrCategory(vsr,3); assert(vsr.scale.first == 1.f && vsr.scalingType == 0);
	ChangeNvidiaVfxVsrCategory(vsr,0); assert(vsr.parameters.at("strength") == 2.f && vsr.scale.first == .9f);
	ChangeNvidiaVfxVsrCategory(vsr,2); assert(vsr.parameters.at("strength") == 3.f);
	const auto exact=PlanNvidiaVfxUpscale({1280,720},{1920,1080},0);
	assert(exact.valid && exact.ratioIndex==1 && exact.adaptation==NvidiaVfxAdaptation::None);
	const auto down=PlanNvidiaVfxUpscale({1280,720},{2304,1296},0);
	assert(down.valid && down.ratioIndex==2 && down.native.width==2560 && down.adaptation==NvidiaVfxAdaptation::Downsample);
	const auto beyond=PlanNvidiaVfxUpscale({640,360},{3840,2160},0);
	assert(beyond.valid && beyond.ratioIndex==4 && beyond.native.width==2560 && beyond.adaptation==NvidiaVfxAdaptation::Upsample);
	assert(!PlanNvidiaVfxUpscale({1282,720},{1920,1080},1).valid);
	assert(!PlanNvidiaVfxUpscale({4200,2400},{16000,9000},0).valid);
	assert(!PlanNvidiaVfxUpscale({0,720},{1920,1080},0).valid);
	for (uint32_t m=2; m<=8; ++m) {
		std::vector<StoredEffect> effects{{std::string(NVIDIA_VFX_VFG_ID),{{"multiplier",float(m)}}},{std::string(NVIDIA_VFX_TRUE_HDR_ID)},{"Bicubic"}};
		const auto plan=PlanNvidiaVfxExecution(effects,BuildHdrComponentPlan(effects));
		assert(plan.enabled && plan.error==NvidiaVfxPlanError::None && plan.settings.multiplier==m && plan.postStart==1);
		// Moving only the selector does not reorder the image effects or turn
		// VFG into a processor of already-HDR colours.
		std::swap(effects[0],effects[1]);
		assert(PlanNvidiaVfxExecution(effects,BuildHdrComponentPlan(effects)).postStart==0);
	}
	std::vector<StoredEffect> unsafe{{std::string(NVIDIA_VFX_VFG_ID)},{std::string(NVIDIA_VFX_TRUE_HDR_ID)},{"DLSSNR\\DLSSNR_AI_Filter"}};
	assert(PlanNvidiaVfxExecution(unsafe,BuildHdrComponentPlan(unsafe)).error==NvidiaVfxPlanError::UnsupportedPostEffect);
	std::vector<StoredEffect> hdr{{"Color\\HDR_to_SDR"},{std::string(NVIDIA_VFX_VFG_ID)}};
	assert(PlanNvidiaVfxExecution(hdr,BuildHdrComponentPlan(hdr)).error==NvidiaVfxPlanError::None);
	hdr={{"Color\\SDR_to_HDR"},{std::string(NVIDIA_VFX_VFG_ID)}};
	assert(PlanNvidiaVfxExecution(hdr,BuildHdrComponentPlan(hdr)).error==NvidiaVfxPlanError::ExpectedSdr);
	unsafe[0].parameters["multiplier"]=std::numeric_limits<float>::quiet_NaN();
	assert(PlanNvidiaVfxExecution(unsafe,BuildHdrComponentPlan(unsafe)).error==NvidiaVfxPlanError::InvalidSettings);
}

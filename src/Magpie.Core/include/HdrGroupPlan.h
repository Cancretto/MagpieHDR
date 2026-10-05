#pragma once
#include "HdrGroupSettings.h"
#include "HdrEffectCapabilities.h"
#include "HdrComponents.h"
#include <type_traits>
#include <map>
#include <vector>

namespace Magpie {
enum class HdrGroupError { None, InvalidSettings, ManualComponents, UnknownEffect, InvalidOwnership, FrameGenerationPosition, MissingPair };

template<class String>
String HdrGroupString(std::string_view value) {
	String result;
	for (char c : value) result.push_back(typename String::value_type(c));
	return result;
}

template<class Effect>
auto HdrGroupKey(std::string_view key) {
	using Key = typename std::remove_cvref_t<decltype(std::declval<Effect>().parameters)>::key_type;
	return HdrGroupString<Key>(key);
}

template<class Effect>
float HdrGroupValue(const Effect& effect, std::string_view key, float fallback) {
	const auto it = effect.parameters.find(HdrGroupKey<Effect>(key));
	return it == effect.parameters.end() ? fallback : it->second;
}

template<class Effect>
bool HdrManagedRoleMatches(const Effect& effect) {
	const auto kind = ClassifyHdrComponent(HdrEffectAsciiName(effect.name));
	switch (effect.hdrManagedRole) {
	case HdrManagedRole::None: return true;
	case HdrManagedRole::InputMapping: return kind == HdrComponentKind::HdrToSdr && !effect.HasScale();
	case HdrManagedRole::PairRestore: return kind == HdrComponentKind::SdrToHdr && !effect.HasScale() && HdrGroupValue(effect,"mode",0) == 2;
	case HdrManagedRole::Output: return (kind == HdrComponentKind::SdrToHdr || kind == HdrComponentKind::RtxVideoHdr) && !effect.HasScale();
	case HdrManagedRole::Identity: return HdrEffectAsciiName(effect.name) == "Bicubic" && !effect.HasScale() && effect.parameters.empty();
	default: return false;
	}
}

template<class Effect>
void CaptureHdrGroupParameters(const std::vector<Effect>& effects, HdrGroupSettings& settings) {
	for (const auto& effect : effects) {
		if (effect.hdrManagedRole == HdrManagedRole::InputMapping) {
			settings.whiteNits = HdrGroupValue(effect,"whiteNits",203);
			settings.peakNits = HdrGroupValue(effect,"peakNits",1000);
			settings.exposure = HdrGroupValue(effect,"exposure",1);
			settings.shoulder = HdrGroupValue(effect,"shoulder",1);
			break;
		}
	}
	for (const auto& effect : effects) if (effect.hdrManagedRole == HdrManagedRole::Output) {
		if (ClassifyHdrComponent(HdrEffectAsciiName(effect.name)) == HdrComponentKind::RtxVideoHdr) {
			settings.rtxPeakNits = HdrGroupValue(effect,"peakNits",1000);
			settings.rtxContrast = HdrGroupValue(effect,"contrast",100);
			settings.rtxSaturation = HdrGroupValue(effect,"saturation",100);
			settings.rtxMiddleGray = HdrGroupValue(effect,"middleGray",50);
		} else {
			settings.nativeLegacy = !effect.parameters.contains(HdrGroupKey<Effect>("nativeWhiteNits"));
			settings.legacyWhiteNits = HdrGroupValue(effect,"whiteNits",203);
			settings.legacyExposure = HdrGroupValue(effect,"exposure",1);
			settings.legacyPeakNits = HdrGroupValue(effect,"peakNits",1000);
			settings.nativeWhiteNits = HdrGroupValue(effect,"nativeWhiteNits",settings.legacyWhiteNits * settings.legacyExposure);
		}
	}
}

// Shared input parameters change as one transaction. Generated decode ends
// still use the upstream immutable component snapshot, never their own copy.
template<class Effect>
void SynchronizeHdrGroupParameterChanges(const std::vector<Effect>& before, std::vector<Effect>& after) {
	if (before.size() != after.size()) return;
	for (const auto key : {"whiteNits","peakNits","exposure","shoulder"}) {
		std::optional<float> edited;
		for (size_t i = 0; i < after.size(); ++i) if (after[i].hdrManagedRole == HdrManagedRole::InputMapping) {
			const auto name = HdrGroupKey<Effect>(key);
			const auto old = before[i].parameters.find(name);
			const auto value = after[i].parameters.find(name);
			if (value != after[i].parameters.end() && (old == before[i].parameters.end() || old->second != value->second)) { edited = value->second; break; }
		}
		if (edited) for (auto& effect : after) if (effect.hdrManagedRole == HdrManagedRole::InputMapping)
			effect.parameters[HdrGroupKey<Effect>(key)] = *edited;
	}
}

template<class Effect>
struct HdrGroupPlan {
	std::vector<Effect> effects;
	HdrGroupError error = HdrGroupError::None;
	size_t errorIndex = 0;
	size_t sdrSegments = 0;
	bool restoresPair = false;
	bool keepsHdr = false;
};

template<class Effect>
HdrGroupPlan<Effect> BuildHdrGroupPlan(const std::vector<Effect>& existing, const HdrGroupSettings& settings) {
	HdrGroupPlan<Effect> result;
	auto fail = [&](HdrGroupError error, size_t index = 0) {
		result.effects = existing; result.error = error; result.errorIndex = index;
	};
	if (!settings.IsValid()) { fail(HdrGroupError::InvalidSettings); return result; }
	if (!settings.managed) { result.effects = existing; return result; }
	std::vector<Effect> body;
	for (size_t i = 0; i < existing.size(); ++i) {
		const auto& effect = existing[i];
		if (!HdrManagedRoleMatches(effect)) { fail(HdrGroupError::InvalidOwnership,i); return result; }
		if (effect.hdrManagedRole != HdrManagedRole::None) continue;
		if (ClassifyHdrComponent(HdrEffectAsciiName(effect.name)) != HdrComponentKind::None) { fail(HdrGroupError::ManualComponents,i); return result; }
		const auto capability = GetHdrEffectCapability(effect.name);
		if (capability.capability == HdrEffectCapability::Unknown && settings.input == HdrGroupInput::Hdr) { fail(HdrGroupError::UnknownEffect,i); return result; }
		body.push_back(effect);
	}
	using Name = std::remove_cvref_t<decltype(std::declval<Effect>().name)>;
	auto make = [&](std::string_view name, HdrManagedRole role) {
		Effect effect{}; effect.name = HdrGroupString<Name>(name); effect.hdrManagedRole = role; return effect;
	};
	if (body.empty()) body.push_back(make("Bicubic", HdrManagedRole::Identity));
	// The terminal FG marker is preserved in place after the generated output.
	// A marker in the middle needs manual ownership, not converter hoisting.
	std::optional<Effect> terminal;
	for (size_t i = 0; i < body.size(); ++i) if (GetHdrEffectCapability(body[i].name).capability == HdrEffectCapability::FrameGeneration) {
		if (i + 1 != body.size() || terminal) { fail(HdrGroupError::FrameGenerationPosition,i); return result; }
		terminal = body[i];
	}
	if (terminal) body.pop_back();
	bool hdr = settings.input == HdrGroupInput::Hdr;
	bool pair = false;
	bool hadPair = false;
	auto enterSdr = [&] {
		if (!hdr) return;
		auto effect = make("Color\\HDR_to_SDR", HdrManagedRole::InputMapping);
		effect.parameters[HdrGroupKey<Effect>("mode")] = settings.output == HdrGroupOutput::Sdr ? 1.f : 0.f;
		effect.parameters[HdrGroupKey<Effect>("whiteNits")] = settings.whiteNits;
		effect.parameters[HdrGroupKey<Effect>("peakNits")] = settings.peakNits;
		effect.parameters[HdrGroupKey<Effect>("exposure")] = settings.exposure;
		effect.parameters[HdrGroupKey<Effect>("shoulder")] = settings.shoulder;
		result.effects.push_back(std::move(effect));
		hdr = false; pair = settings.output != HdrGroupOutput::Sdr;
		hadPair |= pair; ++result.sdrSegments;
	};
	auto restore = [&] {
		if (!pair) return;
		auto effect = make("Color\\SDR_to_HDR", HdrManagedRole::PairRestore);
		effect.parameters[HdrGroupKey<Effect>("mode")] = 2;
		result.effects.push_back(std::move(effect));
		hdr = true; pair = false; result.restoresPair = true;
	};
	for (size_t i = 0; i < body.size(); ++i) {
		const auto& effect = body[i];
		const auto capability = GetHdrEffectCapability(effect.name);
		const bool barrier = capability.IsBarrier() || effect.HasScale();
		if (capability.capability == HdrEffectCapability::RequiresSdr) {
			if (barrier) restore();
			enterSdr();
			result.effects.push_back(effect);
			const bool finalSdrOutput = i + 1 == body.size() &&
				(settings.output == HdrGroupOutput::NativeWhite || settings.output == HdrGroupOutput::RtxHdr);
			if (barrier && !finalSdrOutput) restore();
		} else {
			if (settings.output != HdrGroupOutput::Sdr) restore();
			result.effects.push_back(effect);
		}
	}
	if (settings.output == HdrGroupOutput::RestorePair && !hadPair) { fail(HdrGroupError::MissingPair); return result; }
	if (settings.output == HdrGroupOutput::Sdr) enterSdr();
	else if (settings.output == HdrGroupOutput::RtxHdr) {
		enterSdr();
		auto effect = make("RTXVideo\\RTXVideo_HDR", HdrManagedRole::Output);
		effect.parameters[HdrGroupKey<Effect>("peakNits")] = settings.rtxPeakNits;
		effect.parameters[HdrGroupKey<Effect>("contrast")] = settings.rtxContrast;
		effect.parameters[HdrGroupKey<Effect>("saturation")] = settings.rtxSaturation;
		effect.parameters[HdrGroupKey<Effect>("middleGray")] = settings.rtxMiddleGray;
		result.effects.push_back(std::move(effect)); hdr = true;
	} else if (settings.output == HdrGroupOutput::NativeWhite || (settings.input == HdrGroupInput::Sdr && settings.output == HdrGroupOutput::Auto)) {
		enterSdr();
		auto effect = make("Color\\SDR_to_HDR", HdrManagedRole::Output);
		effect.parameters[HdrGroupKey<Effect>("mode")] = settings.output == HdrGroupOutput::Auto ? 0.f : 1.f;
		if (settings.nativeLegacy) {
			effect.parameters[HdrGroupKey<Effect>("whiteNits")] = settings.legacyWhiteNits;
			effect.parameters[HdrGroupKey<Effect>("exposure")] = settings.legacyExposure;
			effect.parameters[HdrGroupKey<Effect>("peakNits")] = settings.legacyPeakNits;
		} else effect.parameters[HdrGroupKey<Effect>("nativeWhiteNits")] = settings.nativeWhiteNits;
		result.effects.push_back(std::move(effect)); hdr = true;
	} else {
		restore(); result.keepsHdr = !result.restoresPair;
	}
	if (terminal) result.effects.push_back(std::move(*terminal));
	return result;
}

// Comparison uses the executed color stages, including legacy metadata. Raw
// Auto/Restore mode numbers may differ while resolving to the same upstream
// immutable pair. Ordinary effects and their scaling/parameters stay exact.
template<class Effect>
bool HdrGroupPlansEquivalent(const std::vector<Effect>& before, std::optional<bool> beforeIntent,
	const std::vector<Effect>& after, std::optional<bool> afterIntent) {
	if (before.size() != after.size()) return false;
	struct ColorEffect { std::string name; std::map<std::string,float> parameters; };
	auto colors = [](const std::vector<Effect>& effects) {
		std::vector<ColorEffect> result;
		for (const auto& effect : effects) {
			ColorEffect color{ HdrEffectAsciiName(effect.name) };
			for (const auto& [key,value] : effect.parameters) color.parameters.emplace(HdrEffectAsciiName(key), value);
			result.push_back(std::move(color));
		}
		return result;
	};
	const auto a = BuildHdrComponentPlan(colors(before), beforeIntent);
	const auto b = BuildHdrComponentPlan(colors(after), afterIntent);
	if (a.error != HdrComponentError::None || b.error != HdrComponentError::None ||
		a.captureHdr != b.captureHdr || a.outputHdr != b.outputHdr) return false;
	// A legacy SDR-only plan has no stages. Explicit SDR intent is equivalent
	// only if the materialized list itself is unchanged and still has SDR output.
	for (size_t i = 0; i < before.size(); ++i) {
		if (before[i].name != after[i].name || before[i].scalingType != after[i].scalingType || before[i].scale != after[i].scale) return false;
		if (ClassifyHdrComponent(HdrEffectAsciiName(before[i].name)) == HdrComponentKind::None) {
			if (before[i].parameters != after[i].parameters) return false;
			if (!a.stages.empty() && (a.stages[i].inputHdr != b.stages[i].inputHdr || a.stages[i].outputHdr != b.stages[i].outputHdr)) return false;
			continue;
		}
		const auto& x = a.stages[i]; const auto& y = b.stages[i];
		if (x.inputHdr != y.inputHdr || x.outputHdr != y.outputHdr || x.pairIndex != y.pairIndex) return false;
		if (x.kind == HdrComponentKind::HdrToSdr && x.mode != y.mode) return false;
		if (x.kind == HdrComponentKind::SdrToHdr && (x.pairIndex != size_t(-1))) continue;
		if (x.whiteNits != y.whiteNits || x.peakNits != y.peakNits || x.exposure != y.exposure || x.shoulder != y.shoulder) return false;
		if (x.kind == HdrComponentKind::RtxVideoHdr) for (const auto key : {"contrast","saturation","middleGray"})
			if (HdrGroupValue(before[i],key,key == std::string_view("middleGray") ? 50.f : 100.f) !=
				HdrGroupValue(after[i],key,key == std::string_view("middleGray") ? 50.f : 100.f)) return false;
	}
	return true;
}

template<class Effect>
std::optional<std::pair<HdrGroupSettings,std::vector<Effect>>> TryAdoptHdrGroup(
	const std::vector<Effect>& existing, std::optional<bool> intent = std::nullopt) {
	if (existing.empty()) return std::nullopt;
	HdrGroupSettings settings; settings.managed = true;
	std::vector<Effect> tagged = existing;
	size_t converters = 0;
	const size_t tail = GetHdrEffectCapability(existing.back().name).capability == HdrEffectCapability::FrameGeneration ? existing.size()-1 : existing.size();
	for (size_t i = 0; i < tagged.size(); ++i) {
		auto& effect = tagged[i];
		if (effect.hdrManagedRole != HdrManagedRole::None || effect.HasScale()) {
			if (ClassifyHdrComponent(HdrEffectAsciiName(effect.name)) != HdrComponentKind::None || effect.hdrManagedRole != HdrManagedRole::None) return std::nullopt;
		}
		const auto kind = ClassifyHdrComponent(HdrEffectAsciiName(effect.name));
		if (kind == HdrComponentKind::None) continue;
		++converters;
		if (kind == HdrComponentKind::HdrToSdr) {
			if (i != 0) return std::nullopt;
			settings.input = HdrGroupInput::Hdr;
			settings.output = HdrGroupValue(effect,"mode",0) == 1 ? HdrGroupOutput::Sdr : HdrGroupOutput::Auto;
			effect.hdrManagedRole = HdrManagedRole::InputMapping;
		} else {
			if (i + 1 != tail) return std::nullopt;
			if (settings.input == HdrGroupInput::ExistingChain) settings.input = HdrGroupInput::Sdr;
			if (kind == HdrComponentKind::RtxVideoHdr) settings.output = HdrGroupOutput::RtxHdr;
			else if (settings.input == HdrGroupInput::Hdr && HdrGroupValue(effect,"mode",0) != 1)
				settings.output = HdrGroupValue(effect,"mode",0) == 2 ? HdrGroupOutput::RestorePair : HdrGroupOutput::Auto;
			else settings.output = HdrGroupValue(effect,"mode",0) == 0 ? HdrGroupOutput::Auto : HdrGroupOutput::NativeWhite;
			effect.hdrManagedRole = settings.output == HdrGroupOutput::RestorePair ||
				(settings.output == HdrGroupOutput::Auto && settings.input == HdrGroupInput::Hdr) ? HdrManagedRole::PairRestore : HdrManagedRole::Output;
		}
	}
	if (converters > 2) return std::nullopt;
	if (!converters) {
		settings.input = intent.value_or(false) ? HdrGroupInput::Hdr : HdrGroupInput::Sdr;
		settings.output = settings.input == HdrGroupInput::Hdr ? HdrGroupOutput::Auto : HdrGroupOutput::Sdr;
	}
	CaptureHdrGroupParameters(tagged, settings);
	// Legacy Auto pair nodes use mode 0, which is proven equivalent to mode 2
	// below. Do not reject them as malformed generated ownership before checking.
	for (auto& effect : tagged) if (effect.hdrManagedRole == HdrManagedRole::PairRestore)
		effect.parameters[HdrGroupKey<Effect>("mode")] = 2;
	const auto candidate = BuildHdrGroupPlan(tagged, settings);
	if (candidate.error != HdrGroupError::None || !HdrGroupPlansEquivalent(existing,intent,candidate.effects,settings.CaptureIntent())) return std::nullopt;
	return std::pair{settings,candidate.effects};
}
}

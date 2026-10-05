#pragma once
#include "HdrGroupSettings.h"
#include "HdrGroupPlan.h"
#include <array>
#include <rapidjson/document.h>

namespace Magpie {
inline constexpr std::pair<const char*,float HdrGroupSettings::*> HDR_GROUP_FLOAT_FIELDS[] = {
	{"whiteNits",&HdrGroupSettings::whiteNits}, {"peakNits",&HdrGroupSettings::peakNits},
	{"exposure",&HdrGroupSettings::exposure}, {"shoulder",&HdrGroupSettings::shoulder},
	{"nativeWhiteNits",&HdrGroupSettings::nativeWhiteNits}, {"legacyWhiteNits",&HdrGroupSettings::legacyWhiteNits},
	{"legacyExposure",&HdrGroupSettings::legacyExposure}, {"legacyPeakNits",&HdrGroupSettings::legacyPeakNits},
	{"rtxPeakNits",&HdrGroupSettings::rtxPeakNits}, {"rtxContrast",&HdrGroupSettings::rtxContrast},
	{"rtxSaturation",&HdrGroupSettings::rtxSaturation}, {"rtxMiddleGray",&HdrGroupSettings::rtxMiddleGray}
};

template<class Writer> void WriteHdrGroupJson(Writer& writer,const HdrGroupSettings& value) {
	writer.StartObject();
	writer.Key("version"); writer.Uint(1);
	writer.Key("managed"); writer.Bool(value.managed);
	writer.Key("input"); writer.Uint(uint8_t(value.input));
	writer.Key("output"); writer.Uint(uint8_t(value.output));
	writer.Key("nativeLegacy"); writer.Bool(value.nativeLegacy);
	for (const auto& [name,member] : HDR_GROUP_FLOAT_FIELDS) { writer.Key(name); writer.Double(value.*member); }
	writer.EndObject();
}

inline bool ReadHdrGroupJson(const rapidjson::Value& value,HdrGroupSettings& destination) {
	if (!value.IsObject() || !value.HasMember("version") || !value["version"].IsUint() || value["version"].GetUint()!=1) return false;
	for (const char* name : {"input","output"}) if (!value.HasMember(name) || !value[name].IsUint()) return false;
	if (value["input"].GetUint()>2 || value["output"].GetUint()>4) return false;
	for (const char* name : {"managed","nativeLegacy"}) if (!value.HasMember(name) || !value[name].IsBool()) return false;
	HdrGroupSettings parsed;
	parsed.managed=value["managed"].GetBool();
	parsed.input=HdrGroupInput(value["input"].GetUint()); parsed.output=HdrGroupOutput(value["output"].GetUint());
	parsed.nativeLegacy=value["nativeLegacy"].GetBool();
	for (const auto& [name,member] : HDR_GROUP_FLOAT_FIELDS) {
		if (!value.HasMember(name) || !value[name].IsNumber()) return false;
		parsed.*member=value[name].GetFloat();
	}
	if (!parsed.IsValid()) return false;
	destination=parsed; return true;
}

template<class Effect> bool ValidateSavedHdrGroup(const std::vector<Effect>& effects,const HdrGroupSettings& settings) {
	if (!settings.IsValid()) return false;
	if (!settings.managed) return std::ranges::all_of(effects,[](const auto& effect) { return effect.hdrManagedRole==HdrManagedRole::None; });
	const auto canonical=BuildHdrGroupPlan(effects,settings);
	return canonical.error==HdrGroupError::None && HdrGroupPlansEquivalent(effects,settings.CaptureIntent(),canonical.effects,settings.CaptureIntent());
}
}

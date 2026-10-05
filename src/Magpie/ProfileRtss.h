#pragma once
#include "RtssSettings.h"
#include <rapidjson/document.h>
#include <type_traits>

namespace Magpie {

inline RtssSettings ReadProfileRtss(const rapidjson::GenericObject<true, rapidjson::Value>& object) noexcept {
	RtssSettings result;
	const auto member = object.FindMember("rtss");
	if (member == object.MemberEnd() || !member->value.IsObject()) return result;
	const auto fields = member->value.GetObj();
	bool valid = true;
	auto enumeration = [&](const char* key, auto& target, uint32_t last) {
		const auto value = fields.FindMember(key);
		if (value == fields.MemberEnd()) return;
		if (!value->value.IsUint() || value->value.GetUint() > last) { valid = false; return; }
		target = static_cast<std::remove_reference_t<decltype(target)>>(value->value.GetUint());
	};
	enumeration("scope", result.scope, 2);
	enumeration("rateMode", result.rateMode, 1);
	enumeration("exitPolicy", result.exitPolicy, 1);
	if (const auto value = fields.FindMember("customRate"); value != fields.MemberEnd()) {
		if (value->value.IsUint() && value->value.GetUint() >= RtssSettings::MinimumRate &&
			value->value.GetUint() <= RtssSettings::MaximumRate) result.customRate = value->value.GetUint();
		else valid = false;
	}
	// Do not interpret damaged/forward-version preferences as permission to write externally.
	if (!valid) result.scope = RtssScope::None;
	return result;
}

template<class Writer>
void WriteProfileRtss(Writer& writer, const RtssSettings& settings) {
	const auto s = settings.IsValid() ? settings : RtssSettings{};
	writer.Key("rtss"); writer.StartObject();
	writer.Key("scope"); writer.Uint(uint32_t(s.scope));
	writer.Key("rateMode"); writer.Uint(uint32_t(s.rateMode));
	writer.Key("customRate"); writer.Uint(s.customRate);
	writer.Key("exitPolicy"); writer.Uint(uint32_t(s.exitPolicy));
	writer.EndObject();
}

}

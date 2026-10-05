#include "HdrGroupJson.h"
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <cassert>
#include <iostream>

using namespace Magpie;
int main() {
	HdrGroupSettings original;
	original.managed=true; original.input=HdrGroupInput::Sdr; original.output=HdrGroupOutput::NativeWhite;
	original.nativeLegacy=true; original.nativeWhiteNits=8; original.legacyWhiteNits=80; original.legacyExposure=.1f;
	original.legacyPeakNits=1500; original.rtxPeakNits=1701; original.rtxContrast=133;
	original.exposure=1.337f; original.whiteNits=203.125f;
	rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	WriteHdrGroupJson(writer,original);
	rapidjson::Document json; json.Parse(buffer.GetString());
	HdrGroupSettings reloaded; assert(ReadHdrGroupJson(json,reloaded)); assert(reloaded==original);
	for (const auto* name : {"version","managed","nativeWhiteNits","rtxPeakNits"}) {
		rapidjson::Document invalid; invalid.Parse(buffer.GetString()); invalid.RemoveMember(name);
		const auto before=reloaded; assert(!ReadHdrGroupJson(invalid,reloaded)); assert(reloaded==before);
	}
	json["nativeWhiteNits"].SetInt(4001); assert(!ReadHdrGroupJson(json,reloaded)); assert(reloaded==original);
	json["nativeWhiteNits"].SetInt(1500); json["rtxPeakNits"].SetDouble(1000.5); assert(!ReadHdrGroupJson(json,reloaded));
	json["rtxPeakNits"].SetInt(1000); json["version"].SetUint(2); assert(!ReadHdrGroupJson(json,reloaded));
	std::cout << "PASS: versioned HDR settings roundtrip, precise and low legacy values, inactive method preferences, missing/range/integer errors and transactional read\n";
}

"""Test the production group writer/loader and JsonHelper without an app build."""
from pathlib import Path
import sys

repo = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
source = (repo / 'src/Magpie/ScalingModesService.cpp').read_text(encoding='utf-8-sig')
writer = source[source.index('static void WriteScalingMode('):source.index('\nvoid ScalingModesService::Export(')]
loader = source[source.index('static bool LoadScalingMode('):source.index('\nstruct V065NormalizationStats')]
helper = (repo / 'src/Magpie/JsonHelper.cpp').read_text(encoding='utf-8-sig')
helper = helper[helper.index('bool JsonHelper::ReadBool('):helper.rfind('\n}')]
prefix = r'''
#include <windows.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include "HdrGroupJson.h"
#include "JsonHelper.h"
#include <cassert>
#include <iostream>
#include <map>
struct StrHelper {
 static std::wstring UTF8ToUTF16(std::string_view text) {
  std::wstring output(MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0),L'\0');
  MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),output.data(),int(output.size())); return output;
 }
 static std::string UTF16ToUTF8(std::wstring_view text) {
  std::string output(WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr),'\0');
  WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),output.data(),int(output.size()),nullptr,nullptr); return output;
 }
};
namespace Magpie {
enum class ScalingType : uint32_t { Normal,Fit,Absolute,Fill };
struct ParameterMap : std::map<std::wstring,float> { using std::map<std::wstring,float>::map; void reserve(size_t) {} };
struct EffectItem {
 std::wstring name;
 ParameterMap parameters;
 ScalingType scalingType=ScalingType::Normal;
 std::pair<float,float> scale{1.f,1.f};
 bool isRecoveryInvalid=false;
 std::string recoveryOriginal;
 HdrManagedRole hdrManagedRole=HdrManagedRole::None;
 bool HasScale() const { return scalingType!=ScalingType::Normal || scale!=std::pair<float,float>{1.f,1.f}; }
};
struct ScalingMode {
 std::wstring name;
 std::vector<EffectItem> effects;
 HdrGroupSettings hdr;
 bool hdrInvalid=false;
 std::string hdrRecoveryOriginal;
};
struct ScalingModeNames { static std::wstring_view Trim(std::wstring_view value) { return value; } };
'''
tests = r'''
std::string Save(const ScalingMode& value) {
 rapidjson::StringBuffer buffer; rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
 WriteScalingMode(writer,value); return std::string(buffer.GetString(),buffer.GetSize());
}
ScalingMode Load(std::string_view text) {
 rapidjson::Document json; json.Parse(text.data(),text.size()); assert(!json.HasParseError());
 ScalingMode result; assert(LoadScalingMode(std::as_const(json).GetObj(),result,true)); return result;
}
int main() {
 ScalingMode legacy; legacy.name=L"旧参数 α";
 legacy.effects={{.name=L"CAS\\CAS"},{.name=L"Color\\SDR_to_HDR",.parameters={{L"whiteNits",80.f},{L"exposure",.1f}}}};
 const auto original=Save(legacy); auto reloaded=Load(original);
 assert(Save(reloaded)==original && reloaded.hdr==HdrGroupSettings{} && !reloaded.hdrInvalid);
 assert(!reloaded.effects.back().parameters.contains(L"nativeWhiteNits"));
 const auto adopted=TryAdoptHdrGroup(legacy.effects); assert(adopted);
 ScalingMode grouped; grouped.name=L"HDR"; grouped.hdr=adopted->first; grouped.effects=adopted->second;
 grouped.hdr.rtxPeakNits=1701; grouped.hdr.rtxContrast=133;
 const auto serialized=Save(grouped); const auto decoded=Load(serialized);
 assert(!decoded.hdrInvalid && decoded.hdr==grouped.hdr && Save(decoded)==serialized);
 assert(decoded.hdr.nativeWhiteNits==8 && decoded.hdr.nativeLegacy);
 assert(decoded.effects.back().hdrManagedRole==HdrManagedRole::Output);
 rapidjson::Document wrong; wrong.Parse(serialized.c_str()); wrong["hdr"]["version"].SetUint(2);
 auto encode=[](const auto& json) { rapidjson::StringBuffer b; rapidjson::Writer<rapidjson::StringBuffer> w(b); json.Accept(w); return std::string(b.GetString()); };
 const auto retained=Load(encode(wrong)); assert(retained.hdrInvalid && !retained.hdrRecoveryOriginal.empty());
 const auto retainedAgain=Load(Save(retained)); assert(retainedAgain.hdrInvalid && retainedAgain.hdrRecoveryOriginal==retained.hdrRecoveryOriginal);
 assert(retainedAgain.effects.size()==grouped.effects.size());
 wrong.Parse(serialized.c_str()); wrong["effects"][1]["hdrManagedRole"].SetUint(256);
 const auto forged=Load(encode(wrong)); assert(forged.hdrInvalid && forged.effects[1].isRecoveryInvalid);
 assert(forged.effects[1].recoveryOriginal.find("256")!=std::string::npos);
 const auto forgedAgain=Load(Save(forged)); assert(forgedAgain.hdrInvalid && forgedAgain.effects[1].recoveryOriginal==forged.effects[1].recoveryOriginal);
 wrong.Parse(serialized.c_str()); wrong["hdr"]["nativeWhiteNits"].SetInt(1500); wrong["hdr"]["nativeLegacy"].SetBool(false);
 const auto mismatch=Load(encode(wrong)); assert(mismatch.hdrInvalid);
 assert(!mismatch.effects.back().parameters.contains(L"nativeWhiteNits")); // no silent canonical rewrite
 wrong.Parse(serialized.c_str()); wrong["effects"][0].SetInt(37);
 const auto malformed=Load(encode(wrong)); assert(malformed.hdrInvalid && malformed.effects.size()==grouped.effects.size());
 assert(malformed.effects[0].isRecoveryInvalid && malformed.effects[0].recoveryOriginal=="37");
 assert(Load(Save(malformed)).effects[0].recoveryOriginal=="37");
 wrong.Parse(serialized.c_str()); wrong["effects"][0].AddMember("parameters",rapidjson::Value(rapidjson::kObjectType),wrong.GetAllocator());
 wrong["effects"][0]["parameters"].AddMember("arbitrary",rapidjson::Value("invalid",wrong.GetAllocator()),wrong.GetAllocator());
 const auto invalidParameter=Load(encode(wrong)); assert(invalidParameter.hdrInvalid && invalidParameter.effects[0].isRecoveryInvalid);
 std::cout << "PASS: production group JSON writer/loader, Unicode, legacy load/open/save, inactive preferences, roles, malformed metadata and tampered ownership recovery\n";
 return 0;
}
}
int main() { return Magpie::main(); }
'''
(out / 'HdrGroupPersistenceTests.cpp').write_text(prefix + helper + writer + loader + tests,encoding='utf-8')

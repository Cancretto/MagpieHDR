#include "pch.h"
#include "DLSSNRDetailParameters.h"
#include "ScalingModeItem.h"
#if __has_include("ScalingModeItem.g.cpp")
#include "ScalingModeItem.g.cpp"
#endif
#include "ScalingMode.h"
#include "StrHelper.h"
#include "XamlHelper.h"
#include "AppSettings.h"
#include "EffectsService.h"
#include "EffectHelper.h"
#include "CommonSharedConstants.h"
#include "App.h"
#include "ScalingModeEffectItem.h"
#include "ScalingOptions.h"
#include "RootPage.h"
#include "HdrGroupPlan.h"
#include "EffectParameterLocalization.h"
#include "ScalingService.h"
#include "Win32Helper.h"

using namespace ::Magpie;

namespace winrt::Magpie::implementation {

static ScalingModeEffectItem& GetEffectItemImpl(const IInspectable& item) noexcept {
	return *get_self<ScalingModeEffectItem>(item.try_as<winrt::Magpie::ScalingModeEffectItem>());
}

ScalingModeItem::ScalingModeItem(
	uint32_t index,
	bool isInitialExpanded,
	bool shouldAutoRename)
	: _index(index),
	  _isInitialExpanded(isInitialExpanded),
	  _shouldAutoRename(shouldAutoRename)
{
	{
		std::vector<IInspectable> linkedProfiles;
		const Profile& defaultProfile = AppSettings::Get().DefaultProfile();
		if (defaultProfile.scalingMode == (int)index) {
			hstring defaults = ResourceLoader::GetForCurrentView(CommonSharedConstants::APP_RESOURCE_MAP_ID)
				.GetString(L"Root_Defaults/Content");
			linkedProfiles.push_back(box_value(defaults));
		}
		for (const Profile& profile : AppSettings::Get().Profiles()) {
			if (profile.scalingMode == (int)index) {
				linkedProfiles.push_back(box_value(profile.name));
			}
		}
		_linkedProfiles = single_threaded_vector(std::move(linkedProfiles));
	}

	_scalingModeAddedRevoker = ScalingModesService::Get().ScalingModeAdded(
		auto_revoke, std::bind_front(&ScalingModeItem::_ScalingModesService_Added, this));
	_scalingModeMovedRevoker = ScalingModesService::Get().ScalingModeMoved(
		auto_revoke, std::bind_front(&ScalingModeItem::_ScalingModesService_Moved, this));
	_scalingModeNamesChangedRevoker = ScalingModesService::Get().ScalingModeNamesChanged(
		auto_revoke, std::bind_front(&ScalingModeItem::_ScalingModesService_NamesChanged, this));

	ScalingMode& data = _Data();
	{
		std::vector<IInspectable> effects;
		effects.reserve(data.effects.size());
		for (uint32_t i = 0; i < data.effects.size(); ++i) {
			effects.push_back(*_CreateScalingModeEffectItem(_index, i));
		}
		_effects = single_threaded_observable_vector(std::move(effects));
	}
	_effectsChangedRevoker = _effects.VectorChanged(
		auto_revoke, { this, &ScalingModeItem::_Effects_VectorChanged });
	_hdrStructureChangedRevoker=ScalingModesService::Get().HdrGroupStructureChanged(auto_revoke,
		std::bind_front(&ScalingModeItem::_HdrStructureChanged,this));
	_hdrParametersChangedRevoker=ScalingModesService::Get().EffectParametersChanged(auto_revoke,
		[this](uint32_t mode,uint32_t) { if (!_IsRemoved() && mode==_index) _RefreshHdrProperties(); });
}

void ScalingModeItem::_Index(uint32_t value) noexcept {
	_index = value;
	for (const IInspectable& item : _effects) {
		GetEffectItemImpl(item).ScalingModeIdx(value);
	}
}

// 效果被删除后 ScalingModeItem 不会立刻析构，而且 WinUI 可能会更新绑定！我们要
// 确保被删除后 ScalingModeItem 依然处于合法的状态，调用任何方法都不会崩溃。我知
// 道老是检查显得啰嗦但别无他法。
bool ScalingModeItem::_IsRemoved() const noexcept {
	return _index == std::numeric_limits<uint32_t>::max();
}

void ScalingModeItem::_ScalingModesService_Added(EffectAddedWay) {
	RaisePropertyChanged(L"CanDrag");
}

void ScalingModeItem::_ScalingModesService_Moved(uint32_t fromIndex, uint32_t toIndex) {
	if (_index == fromIndex) {
		_Index(toIndex);
	} else if (fromIndex < toIndex && _index > fromIndex && _index <= toIndex) {
		_Index(_index - 1);
	} else if (toIndex < fromIndex && _index >= toIndex && _index < fromIndex) {
		_Index(_index + 1);
	}
}

void ScalingModeItem::PrepareForRemoval(uint32_t index) noexcept {
	if (_IsRemoved()) return;
	if (_index == index) Detach();
	else if (_index > index) _Index(_index - 1);
}

void ScalingModeItem::RefreshAfterRemoval() {
	if (_IsRemoved()) return;
	RaisePropertyChanged(L"CanDrag");
	_ScalingModesService_NamesChanged();
}

void ScalingModeItem::_ScalingModesService_NamesChanged() {
	if (_IsRemoved()) return;
	RaisePropertyChanged(L"Name");
	RaisePropertyChanged(L"HasNameConflict");
	RenameText(_renameText);
}

void ScalingModeItem::_Effects_VectorChanged(IObservableVector<IInspectable> const&, IVectorChangedEventArgs const& args) {
	if (!_isMovingEffects) {
		RaisePropertyChanged(L"Description");
		RaisePropertyChanged(L"CanReorderEffects");
		return;
	}
	
	// 移动元素时先删除再插入
	if (args.CollectionChange() == CollectionChange::ItemRemoved) {
		_movingFromIdx = args.Index();
		return;
	}
	if (args.CollectionChange() != CollectionChange::ItemInserted ||
		_movingFromIdx == std::numeric_limits<uint32_t>::max()) {
		return;
	}

	const uint32_t movingFromIdx = std::exchange(
		_movingFromIdx,
		std::numeric_limits<uint32_t>::max());
	const uint32_t movingToIdx = args.Index();

	std::vector<EffectItem>& effects = _Data().effects;
	if (movingFromIdx >= effects.size() || movingToIdx >= effects.size()) {
		return;
	}

	const auto before=effects;
	EffectItem removedEffect = std::move(effects[movingFromIdx]);
	effects.erase(effects.begin() + movingFromIdx);
	effects.emplace(effects.begin() + movingToIdx, std::move(removedEffect));
	if (_Data().hdr.managed) {
		const auto candidate=effects;
		effects=before;
		_ApplyHdrSettings(_Data().hdr,candidate);
		return;
	}

	uint32_t minIdx, maxIdx;
	if (movingFromIdx < movingToIdx) {
		minIdx = movingFromIdx;
		maxIdx = movingToIdx;
	} else {
		minIdx = movingToIdx;
		maxIdx = movingFromIdx;
	}
	
	for (uint32_t i = minIdx; i <= maxIdx; ++i) {
		GetEffectItemImpl(_effects.GetAt(i)).EffectIdx(i);
	}

	RaisePropertyChanged(L"Description");
	AppSettings::Get().SaveAsync();
}

void ScalingModeItem::_ScalingModeEffectItem_Removed(uint32_t index) {
	if (_IsRemoved()) {
		return;
	}

	std::vector<EffectItem>& effects = _Data().effects;
	if (index>=effects.size() || effects[index].hdrManagedRole!=HdrManagedRole::None) return;
	if (_Data().hdr.managed) {
		auto candidate=effects; candidate.erase(candidate.begin()+index);
		_ApplyHdrSettings(_Data().hdr,std::move(candidate)); return;
	}
	effects.erase(effects.begin() + index);

	_isMovingEffects = false;
	// 标记已被删除
	GetEffectItemImpl(_effects.GetAt(index)).EffectIdx(std::numeric_limits<uint32_t>::max());
	_effects.RemoveAt(index);
	_isMovingEffects = true;

	for (uint32_t i = index; i < effects.size(); ++i) {
		GetEffectItemImpl(_effects.GetAt(i)).EffectIdx(i);
	}

	_RefreshEffectDragState();

	RaisePropertyChanged(L"HasUnkownEffects");

	AppSettings::Get().SaveAsync();
}

com_ptr<ScalingModeEffectItem> ScalingModeItem::_CreateScalingModeEffectItem(uint32_t scalingModeIdx, uint32_t effectIdx) {
	auto item = make_self<ScalingModeEffectItem>(scalingModeIdx, effectIdx);
	item->Removed(std::bind_front(&ScalingModeItem::_ScalingModeEffectItem_Removed, this));
	return item;
}

void ScalingModeItem::_RefreshEffectDragState() {
	for (const IInspectable& item : _effects) {
		GetEffectItemImpl(item).RefreshDragState();
	}
}

void ScalingModeItem::AddEffect(const hstring& fullName) {
	if (!CanAddEffect(fullName)) {
		return;
	}

	const EffectInfo* effectInfo = EffectsService::Get().GetEffect(fullName);
	if (!effectInfo) return;
	if (_Data().hdr.managed) {
		auto candidate=_Data().effects;
		EffectItem effect; effect.name=fullName;
		if (effectInfo->CanScale()) effect.scalingType=::Magpie::ScalingType::Fit;
		if (fullName==L"DLSSNR\\DLSSNR_AI_Filter") NormalizeDLSSNRDetailParameters(effect.parameters);
		auto insertion=candidate.end();
		if (!candidate.empty() && GetHdrEffectCapability(candidate.back().name).capability==HdrEffectCapability::FrameGeneration) --insertion;
		candidate.insert(insertion,std::move(effect));
		_ApplyHdrSettings(_Data().hdr,std::move(candidate)); return;
	}
	EffectItem& effect = _Data().effects.emplace_back();
	effect.name = fullName;
	if (fullName == L"DLSSNR\\DLSSNR_AI_Filter")
		NormalizeDLSSNRDetailParameters(effect.parameters);
	if (effectInfo->CanScale()) {
		// 支持缩放的效果默认等比缩放到充满屏幕
		effect.scalingType = ::Magpie::ScalingType::Fit;
	}

	auto item = _CreateScalingModeEffectItem(_index, (uint32_t)_Data().effects.size() - 1);
	_isMovingEffects = false;
	_effects.Append(*item);
	_isMovingEffects = true;

	_RefreshEffectDragState();

	AppSettings::Get().SaveAsync();
}

bool ScalingModeItem::CanAddEffect(const hstring& fullName) const noexcept {
	if (_IsRemoved() || _Data().hdrInvalid || !EffectsService::Get().GetEffect(fullName)) {
		return false;
	}
	if (_Data().hdr.managed) {
		auto candidate=_Data().effects;
		EffectItem effect; effect.name=fullName;
		if (EffectsService::Get().GetEffect(fullName)->CanScale()) effect.scalingType=::Magpie::ScalingType::Fit;
		auto insertion=candidate.end();
		if (!candidate.empty() && GetHdrEffectCapability(candidate.back().name).capability==HdrEffectCapability::FrameGeneration) --insertion;
		candidate.insert(insertion,std::move(effect));
		if (BuildHdrGroupPlan(candidate,_Data().hdr).error!=HdrGroupError::None) return false;
	}

	const FrameGenerationEffectKind candidate =
		ClassifyFrameGenerationEffect(std::wstring_view(fullName));
	if (candidate == FrameGenerationEffectKind::None) {
		return true;
	}

	return !ValidateFrameGenerationChain(_Data().effects).HasFrameGeneration();
}

hstring ScalingModeItem::EffectAddProblem(const hstring& fullName) const {
	if (_IsRemoved()) return L"请重新打开要编辑的效果组，再添加效果器。";
	if (_Data().hdrInvalid) return HdrProblem();
	if (!EffectsService::Get().GetEffect(fullName)) return L"请恢复对应效果器文件并重启 Magpie，再尝试添加。";
	if (CanAddEffect(fullName)) return {};
	if (_Data().hdr.managed && ClassifyFrameGenerationEffect(std::wstring_view(fullName))==FrameGenerationEffectKind::None)
		return to_hstring(EffectParameterLocalization::Resource(L"HdrGroup_AddProblem",
			"This effect cannot be added to the managed HDR plan. Use advanced manual editing for converters or unverified HDR effects."));
	for (const auto& effect : _Data().effects) {
		if (ClassifyFrameGenerationEffect(effect.name) != FrameGenerationEffectKind::None) {
			return hstring(fmt::format(L"当前组已使用 {}；如需改用 {}，请先移除现有补帧项。",
				EffectHelper::GetDisplayName(effect.name), EffectHelper::GetDisplayName(fullName)));
		}
	}
	return L"请重新打开当前效果组后再添加。";
}

hstring ScalingModeItem::Name() const noexcept {
	if (_IsRemoved()) {
		return {};
	}

	return hstring(_Data().name);
}

void ScalingModeItem::Name(const hstring& value) noexcept {
	if (_IsRemoved()) {
		return;
	}

	if (!ScalingModesService::Get().RenameScalingMode(_index, value)) RenameText(value);
}

hstring ScalingModeItem::Description() const noexcept {
	if (_IsRemoved()) {
		return {};
	}

	std::wstring result;
	for (const EffectItem& effect : _Data().effects) {
		if (effect.hdrManagedRole!=HdrManagedRole::None) continue;
		if (!result.empty()) {
			result.append(L" > ");
		}

		if (EffectsService::Get().GetEffect(effect.name) != nullptr) {
			result += EffectHelper::GetDisplayName(effect.name);
		} else {
			ResourceLoader resourceLoader =
				ResourceLoader::GetForCurrentView(CommonSharedConstants::APP_RESOURCE_MAP_ID);
			result += L'(';
			result += resourceLoader.GetString(L"ScalingModes_Description_UnknownEffect");
			result += L')';
		}
	}
	return hstring(result);
}

bool ScalingModeItem::HasUnkownEffects() const noexcept {
	if (_IsRemoved()) {
		return false;
	}

	for (const EffectItem& effect : _Data().effects) {
		if (!EffectsService::Get().GetEffect(effect.name)) {
			return true;
		}
	}

	return false;
}

bool ScalingModeItem::HasNameConflict() const noexcept {
	return !_IsRemoved() && ScalingModesService::Get().HasNameConflict(_index);
}

void ScalingModeItem::RenameText(const hstring& value) noexcept {
	if (_IsRemoved()) {
		return;
	}

	_renameText = value;
	RaisePropertyChanged(L"RenameText");

	_trimedRenameText = value;
	StrHelper::Trim(_trimedRenameText);
	const auto loader = ResourceLoader::GetForCurrentView(CommonSharedConstants::APP_RESOURCE_MAP_ID);
	if (_trimedRenameText.empty()) _renameProblem = loader.GetString(L"ScalingModes_NameRequired");
	else if (!ScalingModesService::Get().CanUseName(_trimedRenameText, _index))
		_renameProblem = loader.GetString(L"ScalingModes_NameAlreadyExists");
	else _renameProblem = {};
	RaisePropertyChanged(L"RenameProblem");
	RaisePropertyChanged(L"HasRenameProblem");
	bool newEnabled = _renameProblem.empty() && _trimedRenameText != _Data().name;
	if (_isRenameButtonEnabled != newEnabled) {
		_isRenameButtonEnabled = newEnabled;
		RaisePropertyChanged(L"IsRenameButtonEnabled");
	}
}

void ScalingModeItem::RenameFlyout_Opening() {
	if (_IsRemoved()) {
		return;
	}

	RenameText(hstring(_Data().name));
	RaisePropertyChanged(L"RenameTextBoxSelectionStart");
}

void ScalingModeItem::RenameTextBox_KeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& args) {
	if (_IsRemoved()) {
		return;
	}

	if (args.Key() == VirtualKey::Enter) {
		RenameButton_Click();
	}
}

void ScalingModeItem::RenameButton_Click() {
	if (_IsRemoved() || !_isRenameButtonEnabled) {
		return;
	}
	auto lifetime = get_strong();
	if (!ScalingModesService::Get().RenameScalingMode(_index, _trimedRenameText)) {
		RenameText(_renameText);
		return;
	}

	// Flyout 没有 IsOpen 可供绑定，只能用变通方法关闭
	XamlHelper::ClosePopups(App::Get().RootPage()->XamlRoot());

}

bool ScalingModeItem::TakeAutoRenameRequest() noexcept {
	return std::exchange(_shouldAutoRename, false);
}

bool ScalingModeItem::CanDrag() const noexcept {
	return !_IsRemoved() &&
		ScalingModesService::Get().GetScalingModeCount() > 1;
}

void ScalingModeItem::Duplicate() {
	if (_IsRemoved()) {
		return;
	}

	ResourceLoader resourceLoader =
		ResourceLoader::GetForCurrentView(CommonSharedConstants::APP_RESOURCE_MAP_ID);
	std::wstring name = _Data().name;
	name += resourceLoader.GetString(L"ScalingModes_DuplicateSuffix");
	ScalingModesService::Get().AddScalingMode(name, (int)_index);
}

bool ScalingModeItem::CanReorderEffects() const noexcept {
	if (_IsRemoved()) {
		return false;
	}

	return std::ranges::count_if(_Data().effects,[](const auto& effect) { return effect.hdrManagedRole==HdrManagedRole::None; })>1;
}

void ScalingModeItem::Remove() {
	if (_IsRemoved()) {
		return;
	}
	auto lifetime = get_strong();
	const uint32_t index = _index;
	// RemoveAt can synchronously re-enter bindings and release the last UI reference.
	Detach();
	ScalingModesService::Get().RemoveScalingMode(index);
}

void ScalingModeItem::Detach() noexcept {
	if (_IsRemoved()) return;
	_Index(std::numeric_limits<uint32_t>::max());

	// 被删除后不会立刻析构，因此手动清理事件订阅
	_effectsChangedRevoker.revoke();
	_scalingModeAddedRevoker.Revoke();
	_scalingModeMovedRevoker.Revoke();
	_scalingModeNamesChangedRevoker.Revoke();
	_hdrStructureChangedRevoker.Revoke();
	_hdrParametersChangedRevoker.Revoke();
}

static hstring HdrText(std::wstring_view key,std::string_view fallback) {
	return to_hstring(EffectParameterLocalization::Resource(key,fallback));
}

IObservableVector<hstring> ScalingModeItem::HdrInputChoices() {
	if (!_hdrInputChoices) _hdrInputChoices=MakeEffectChoiceItems({
		HdrText(L"HdrGroup_InputManual","Keep existing chain / advanced manual"),
		HdrText(L"HdrGroup_InputSdr","SDR input"), HdrText(L"HdrGroup_InputHdr","HDR input")});
	return _hdrInputChoices;
}

IObservableVector<hstring> ScalingModeItem::HdrOutputChoices() {
	if (!_hdrOutputChoices) _hdrOutputChoices=MakeEffectChoiceItems({
		HdrText(L"HdrGroup_OutputAuto","Auto"), HdrText(L"HdrGroup_OutputRestore","Restore compatibility pair"),
		HdrText(L"HdrGroup_OutputNative","Native SDR white mapping"), HdrText(L"HdrGroup_OutputRtx","RTX HDR"),
		HdrText(L"HdrGroup_OutputSdr","SDR output")});
	return _hdrOutputChoices;
}

IObservableVector<hstring> ScalingModeItem::HdrManualCaptureChoices() {
	if (!_hdrManualCaptureChoices) _hdrManualCaptureChoices=MakeEffectChoiceItems({
		HdrText(L"HdrGroup_CaptureFromChain","From existing converters"),
		HdrText(L"HdrGroup_InputSdr","SDR input"),HdrText(L"HdrGroup_InputHdr","HDR input")});
	return _hdrManualCaptureChoices;
}
int ScalingModeItem::HdrManualCaptureIndex() const noexcept { return _IsRemoved() ? 0 : int(_Data().hdr.input); }
void ScalingModeItem::HdrManualCaptureIndex(int value) {
	if (!CanEditHdr() || IsHdrManaged() || value<0 || value>2 || value==HdrManualCaptureIndex()) return;
	auto settings=_Data().hdr; settings.input=HdrGroupInput(value);
	_ApplyHdrSettings(settings,_Data().effects);
}

bool ScalingModeItem::IsHdrManaged() const noexcept { return !_IsRemoved() && _Data().hdr.managed; }
bool ScalingModeItem::CanEditHdr() const noexcept { return !_IsRemoved() && !_Data().hdrInvalid; }
int ScalingModeItem::HdrInputIndex() const noexcept { return IsHdrManaged() ? int(_Data().hdr.input) : 0; }
int ScalingModeItem::HdrOutputIndex() const noexcept { return _IsRemoved() ? 0 : int(_Data().hdr.output); }

void ScalingModeItem::HdrInputIndex(int value) {
	if (!CanEditHdr() || value<0 || value>2 || value==HdrInputIndex()) return;
	auto settings=_Data().hdr;
	auto effects=_Data().effects;
	if (!value) {
		CaptureHdrGroupParameters(effects,settings);
		settings.managed=false; // retain explicit capture intent when exposing a pure HDR chain
		for (auto& effect:effects) effect.hdrManagedRole=HdrManagedRole::None;
	} else {
		if (!settings.managed && std::ranges::any_of(effects,[](const auto& effect) {
			return ClassifyHdrComponent(HdrEffectAsciiName(effect.name))!=HdrComponentKind::None;
		})) {
			const auto adopted=TryAdoptHdrGroup(effects,settings.CaptureIntent());
			if (!adopted) {
				_hdrProblem=HdrText(L"HdrGroup_AdoptionProblem","This manual chain cannot be represented equivalently. Keep advanced manual editing; its order and values are preserved.");
				_RefreshHdrProperties(); return;
			}
			settings=adopted->first; effects=adopted->second;
		}
		settings.managed=true; settings.input=HdrGroupInput(value);
	}
	_ApplyHdrSettings(settings,std::move(effects));
}

void ScalingModeItem::HdrOutputIndex(int value) {
	if (!IsHdrManaged() || !CanEditHdr() || value<0 || value>4 || value==HdrOutputIndex()) return;
	auto settings=_Data().hdr; settings.output=HdrGroupOutput(value);
	_ApplyHdrSettings(settings,_Data().effects);
}

bool ScalingModeItem::_ApplyHdrSettings(HdrGroupSettings settings,std::vector<EffectItem> effects) {
	if (!CanEditHdr()) return false;
	CaptureHdrGroupParameters(effects,settings);
	const auto plan=BuildHdrGroupPlan(effects,settings);
	if (plan.error!=HdrGroupError::None) {
		const wchar_t* key=L"HdrGroup_InvalidPlan";
		if (plan.error==HdrGroupError::MissingPair) key=L"HdrGroup_MissingPair";
		if (plan.error==HdrGroupError::UnknownEffect) key=L"HdrGroup_UnknownEffect";
		if (plan.error==HdrGroupError::FrameGenerationPosition) key=L"HdrGroup_FrameGenerationPosition";
		_hdrProblem=HdrText(key,"The requested HDR plan is unavailable. The existing chain has been retained.");
		_RebuildEffectViews(); _RefreshHdrProperties(); return false;
	}
	_Data().hdr=settings; _Data().effects=plan.effects; ++_Data().hdrRevision;
	_hdrProblem={};
	_RebuildEffectViews(); _RefreshHdrProperties();
	AppSettings::Get().SaveAsync(); ScalingService::Get().HdrGroupEdited(_index);
	return true;
}

void ScalingModeItem::_HdrStructureChanged(uint32_t mode) {
	if (!_IsRemoved() && mode==_index && IsHdrManaged()) _ApplyHdrSettings(_Data().hdr,_Data().effects);
}

void ScalingModeItem::_RebuildEffectViews() {
	for (const auto& effect:_effects) GetEffectItemImpl(effect).EffectIdx(std::numeric_limits<uint32_t>::max());
	std::vector<IInspectable> views;
	for (uint32_t i=0; i<_Data().effects.size(); ++i) views.push_back(*_CreateScalingModeEffectItem(_index,i));
	const bool moving=std::exchange(_isMovingEffects,false);
	auto restore=wil::scope_exit([&] { _isMovingEffects=moving; });
	_effects.ReplaceAll(views); _movingFromIdx=std::numeric_limits<uint32_t>::max();
	RaisePropertyChanged(L"Description"); RaisePropertyChanged(L"HasUnkownEffects"); _RefreshEffectDragState();
}

void ScalingModeItem::_RefreshHdrProperties() {
	for (const wchar_t* property:{L"HdrInputIndex",L"HdrOutputIndex",L"HdrManualCaptureIndex",L"IsHdrManual",L"IsHdrManaged",L"CanEditHdr",L"HdrSummary",L"HdrOutputDescription",L"HdrProblem",
		L"HasHdrProblem",L"HasHdrInputParameters",L"HasHdrOutputParameters",L"HdrInputParameters",L"HdrOutputParameters",L"CanReorderEffects"}) RaisePropertyChanged(property);
}

winrt::Magpie::EffectParametersViewModel ScalingModeItem::_HdrParameters(HdrManagedRole role) const noexcept {
	if (!IsHdrManaged()) return nullptr;
	for (uint32_t i=0; i<_Data().effects.size() && i<_effects.Size(); ++i)
		if (_Data().effects[i].hdrManagedRole==role) return GetEffectItemImpl(_effects.GetAt(i)).Parameters();
	return nullptr;
}
winrt::Magpie::EffectParametersViewModel ScalingModeItem::HdrInputParameters() const noexcept { return _HdrParameters(HdrManagedRole::InputMapping); }
winrt::Magpie::EffectParametersViewModel ScalingModeItem::HdrOutputParameters() const noexcept { return _HdrParameters(HdrManagedRole::Output); }
bool ScalingModeItem::HasHdrInputParameters() const noexcept { return bool(HdrInputParameters()); }
bool ScalingModeItem::HasHdrOutputParameters() const noexcept { return bool(HdrOutputParameters()); }
bool ScalingModeItem::HasHdrProblem() const noexcept { return !_IsRemoved() && (_Data().hdrInvalid || !_hdrProblem.empty()); }
hstring ScalingModeItem::HdrProblem() const {
	if (_IsRemoved()) return {};
	return _Data().hdrInvalid ? HdrText(L"HdrGroup_RecoveryProblem","Saved HDR settings are invalid and have been retained for recovery. Repair this group before starting.") : _hdrProblem;
}

hstring ScalingModeItem::HdrSummary() const {
	if (_IsRemoved()) return {};
	if (!IsHdrManaged()) {
		const auto intent=_Data().hdr.CaptureIntent();
		return HdrText(intent ? (*intent ? L"HdrGroup_ManualHdrSummary" : L"HdrGroup_ManualSdrSummary") : L"HdrGroup_ManualSummary",
			"Advanced manual chain: existing converters and explicit input intent determine capture and output.");
	}
	const auto plan=BuildHdrGroupPlan(_Data().effects,_Data().hdr);
	return to_hstring(fmt::format(fmt::runtime(EffectParameterLocalization::Resource(L"HdrGroup_Summary","{} input → {} SDR span(s) → {} output")),
		_Data().hdr.input==HdrGroupInput::Hdr ? "HDR" : "SDR",plan.sdrSegments,_Data().hdr.output==HdrGroupOutput::Sdr ? "SDR" : "HDR"));
}

hstring ScalingModeItem::HdrOutputDescription() const {
	if (!IsHdrManaged()) return HdrText(L"HdrGroup_ManualOutput","Edit the existing conversion components in advanced manual mode.");
	const auto& settings=_Data().hdr;
	if (settings.output==HdrGroupOutput::RtxHdr) {
		const auto directory=Win32Helper::GetExePath().parent_path();
		const bool ready=Win32Helper::FileExists((directory/L"Magpie.RtxVideo.dll").c_str()) && Win32Helper::FileExists((directory/L"nvngx_truehdr.dll").c_str());
		return HdrText(ready ? L"HdrGroup_RtxLibrariesReady" : L"HdrGroup_RtxLibrariesMissing",
			"RTX HDR requires its runtime libraries. GPU and driver support are verified when starting; HDR input is reinterpreted through SDR.");
	}
	if (settings.output==HdrGroupOutput::Sdr) return HdrText(L"HdrGroup_SdrDescription","HDR input uses display tone mapping; output stays SDR.");
	if (settings.output==HdrGroupOutput::NativeWhite || (settings.output==HdrGroupOutput::Auto && settings.input==HdrGroupInput::Sdr)) {
		auto description=fmt::format(fmt::runtime(EffectParameterLocalization::Resource(settings.input==HdrGroupInput::Hdr ? L"HdrGroup_NativeHdrDescription" : L"HdrGroup_NativeDescription",
			"Actual method: native white mapping, {:.7g} nits. Increasing this value raises white, gray and highlights together.")),settings.nativeWhiteNits);
		if (settings.nativeWhiteNits<80) description+="\n"+EffectParameterLocalization::Resource(L"HdrGroup_LegacyDimDescription",
			"A legacy dim value is preserved. New edits use the 80–4000-nit range.");
		return to_hstring(description);
	}
	const auto plan=BuildHdrGroupPlan(_Data().effects,settings);
	return plan.restoresPair ? HdrText(L"HdrGroup_RestoreDescription","Actual method: restore the shared compatibility pair. Quantization, clipping and effect changes cannot be undone.") :
		HdrText(L"HdrGroup_KeepHdrDescription","Actual method: keep HDR throughout effects with confirmed direct HDR support.");
}

ScalingMode& ScalingModeItem::_Data() noexcept {
	return ScalingModesService::Get().GetScalingMode(_index);
}

const ScalingMode& ScalingModeItem::_Data() const noexcept {
	return ScalingModesService::Get().GetScalingMode(_index);
}

}

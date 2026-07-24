#include "pch.h"
#include "WaterSurfaceParameterPanel.h"

// WaterSurfaceParameterPanel のエディタ設定シリアライズ実装。
// UI 描画（WaterSurfaceParameterPanel.cpp）と分離し、WaterSettingsSection から
// 自動保存・復元のために呼ばれる。適用は Draw と同じ経路
// （WaterPlaneObject の setter / facade の Apply）を使う。

#ifdef USE_IMGUI

#include "WaterSurfaceRuntimeController.h"
#include "Utility/JsonManager/JsonManager.h"

using namespace CoreEngine;

void WaterSurfaceParameterPanel::SerializeSettings(json& out, const WaterEditorFacade& editorFacade) const {
	// ===== 見た目 =====
	out["baseColor"] = json::array({
		appearanceParameters_.baseColor[0], appearanceParameters_.baseColor[1],
		appearanceParameters_.baseColor[2], appearanceParameters_.baseColor[3] });
	out["roughness"] = appearanceParameters_.roughness;
	out["metallic"] = appearanceParameters_.metallic;
	out["iblEnabled"] = appearanceParameters_.iblEnabled;
	out["fresnelReflectanceScale"] = appearanceParameters_.fresnelReflectanceScale;
	out["fresnelBaseReflectance"] = appearanceParameters_.fresnelBaseReflectance;

	// ===== 透過 / 水質（ベース値＋濁度。実効係数は復元時に再合成する） =====
	out["scrollSpeed"] = json::array({ surfaceParameters_.scrollSpeed[0], surfaceParameters_.scrollSpeed[1] });
	out["uvTiling"] = json::array({ surfaceParameters_.uvTiling[0], surfaceParameters_.uvTiling[1] });
	out["depthFadeEnabled"] = surfaceParameters_.depthFadeEnabled;
	out["absorptionCoeff"] = json::array({
		surfaceParameters_.absorptionCoeff[0], surfaceParameters_.absorptionCoeff[1], surfaceParameters_.absorptionCoeff[2] });
	out["scatteringCoeff"] = json::array({
		surfaceParameters_.scatteringCoeff[0], surfaceParameters_.scatteringCoeff[1], surfaceParameters_.scatteringCoeff[2] });
	out["jerlovPreset"] = surfaceParameters_.jerlovPreset;
	out["turbidity"] = surfaceParameters_.turbidity;

	// ===== DXR 屈折 =====
	out["rtRefractionOffsetPixels"] = rtRefractionOffsetPixels_;

	// ===== FFT Ocean（facade 経由の実体値。デバッグ・状態フラグは保存しない） =====
	const WaterEditorFFTSettings fft = editorFacade.GetFFTSettings();
	out["fftEnabled"] = fft.enabled;
	out["fftResolution"] = fft.resolution;
	out["fftPatchLength"] = fft.patchLength;
	out["fftAmplitudeScale"] = fft.amplitudeScale;
	out["fftWindDirection"] = json::array({ fft.windDirection[0], fft.windDirection[1] });
	out["fftWindSpeed"] = fft.windSpeed;
	out["fftChoppiness"] = fft.choppiness;
	out["fftActiveComponentCount"] = fft.activeComponentCount;
	out["fftGravity"] = fft.gravity;

	// ===== コースティクス（デバッグ表示系は保存しない） =====
	const WaterEditorCausticsSettings caustics = editorFacade.GetCausticsSettings();
	out["causticsIntensity"] = caustics.intensity;
	out["causticsDepthAttenuation"] = caustics.depthAttenuation;
	out["causticsCurvatureScale"] = caustics.curvatureScale;
	out["causticsSurfaceSampleRadius"] = caustics.surfaceSampleRadius;
	out["causticsRefractiveIndex"] = caustics.refractiveIndex;
	out["causticsReceiverNormalStrength"] = caustics.receiverNormalStrength;
	out["causticsAlignmentPower"] = caustics.alignmentPower;
}

void WaterSurfaceParameterPanel::DeserializeSettings(const json& in,
	WaterSurfaceRuntimeController& runtimeController, WaterEditorFacade& editorFacade) {
	// ===== UI キャッシュの復元（欠損キーは現在値＝Initialize 後のプリセット値を維持） =====
	const Vector4 baseColor = JsonManager::SafeGetVector4(in, "baseColor", {
		appearanceParameters_.baseColor[0], appearanceParameters_.baseColor[1],
		appearanceParameters_.baseColor[2], appearanceParameters_.baseColor[3] });
	appearanceParameters_.baseColor[0] = baseColor.x;
	appearanceParameters_.baseColor[1] = baseColor.y;
	appearanceParameters_.baseColor[2] = baseColor.z;
	appearanceParameters_.baseColor[3] = baseColor.w;
	appearanceParameters_.roughness = JsonManager::SafeGet(in, "roughness", appearanceParameters_.roughness);
	appearanceParameters_.metallic = JsonManager::SafeGet(in, "metallic", appearanceParameters_.metallic);
	appearanceParameters_.iblEnabled = JsonManager::SafeGet(in, "iblEnabled", appearanceParameters_.iblEnabled);
	appearanceParameters_.fresnelReflectanceScale =
		JsonManager::SafeGet(in, "fresnelReflectanceScale", appearanceParameters_.fresnelReflectanceScale);
	appearanceParameters_.fresnelBaseReflectance =
		JsonManager::SafeGet(in, "fresnelBaseReflectance", appearanceParameters_.fresnelBaseReflectance);

	const Vector3 scroll = JsonManager::SafeGetVector3(in, "scrollSpeed",
		{ surfaceParameters_.scrollSpeed[0], surfaceParameters_.scrollSpeed[1], 0.0f });
	surfaceParameters_.scrollSpeed[0] = scroll.x;
	surfaceParameters_.scrollSpeed[1] = scroll.y;
	const Vector3 tiling = JsonManager::SafeGetVector3(in, "uvTiling",
		{ surfaceParameters_.uvTiling[0], surfaceParameters_.uvTiling[1], 0.0f });
	surfaceParameters_.uvTiling[0] = tiling.x;
	surfaceParameters_.uvTiling[1] = tiling.y;
	surfaceParameters_.depthFadeEnabled = JsonManager::SafeGet(in, "depthFadeEnabled", surfaceParameters_.depthFadeEnabled);
	const Vector3 absorption = JsonManager::SafeGetVector3(in, "absorptionCoeff", {
		surfaceParameters_.absorptionCoeff[0], surfaceParameters_.absorptionCoeff[1], surfaceParameters_.absorptionCoeff[2] });
	surfaceParameters_.absorptionCoeff[0] = absorption.x;
	surfaceParameters_.absorptionCoeff[1] = absorption.y;
	surfaceParameters_.absorptionCoeff[2] = absorption.z;
	const Vector3 scattering = JsonManager::SafeGetVector3(in, "scatteringCoeff", {
		surfaceParameters_.scatteringCoeff[0], surfaceParameters_.scatteringCoeff[1], surfaceParameters_.scatteringCoeff[2] });
	surfaceParameters_.scatteringCoeff[0] = scattering.x;
	surfaceParameters_.scatteringCoeff[1] = scattering.y;
	surfaceParameters_.scatteringCoeff[2] = scattering.z;
	// 範囲外の値は UI 側（Jerlov コンボ表示前）でクランプされる
	surfaceParameters_.jerlovPreset = JsonManager::SafeGet(in, "jerlovPreset", surfaceParameters_.jerlovPreset);
	surfaceParameters_.turbidity = JsonManager::SafeGet(in, "turbidity", surfaceParameters_.turbidity);

	rtRefractionOffsetPixels_ = JsonManager::SafeGet(in, "rtRefractionOffsetPixels", rtRefractionOffsetPixels_);

	// ===== 水面へ適用（Draw の各ウィジェットと同じ経路） =====
	if (WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane()) {
		waterPlane->SetBaseColor({
			appearanceParameters_.baseColor[0], appearanceParameters_.baseColor[1],
			appearanceParameters_.baseColor[2], appearanceParameters_.baseColor[3] });
		waterPlane->SetRoughness(appearanceParameters_.roughness);
		waterPlane->SetMetallic(appearanceParameters_.metallic);
		waterPlane->SetIBLEnabled(appearanceParameters_.iblEnabled);
		waterPlane->SetFresnelParameters(
			appearanceParameters_.fresnelReflectanceScale,
			appearanceParameters_.fresnelBaseReflectance);
		waterPlane->SetDepthFade(surfaceParameters_.depthFadeEnabled);
		waterPlane->SetScrollSpeed({ surfaceParameters_.scrollSpeed[0], surfaceParameters_.scrollSpeed[1] });
		waterPlane->SetUVTiling({ surfaceParameters_.uvTiling[0], surfaceParameters_.uvTiling[1] });
		ApplyEffectiveOpticalCoefficients(waterPlane);
	}

	// ===== FFT Ocean（Apply が解像度変更時の再初期化まで面倒を見る） =====
	WaterEditorFFTSettings fft = editorFacade.GetFFTSettings();
	fft.enabled = JsonManager::SafeGet(in, "fftEnabled", fft.enabled);
	fft.resolution = JsonManager::SafeGet(in, "fftResolution", fft.resolution);
	fft.patchLength = JsonManager::SafeGet(in, "fftPatchLength", fft.patchLength);
	fft.amplitudeScale = JsonManager::SafeGet(in, "fftAmplitudeScale", fft.amplitudeScale);
	const Vector3 wind = JsonManager::SafeGetVector3(in, "fftWindDirection",
		{ fft.windDirection[0], fft.windDirection[1], 0.0f });
	fft.windDirection[0] = wind.x;
	fft.windDirection[1] = wind.y;
	fft.windSpeed = JsonManager::SafeGet(in, "fftWindSpeed", fft.windSpeed);
	fft.choppiness = JsonManager::SafeGet(in, "fftChoppiness", fft.choppiness);
	fft.activeComponentCount = JsonManager::SafeGet(in, "fftActiveComponentCount", fft.activeComponentCount);
	fft.gravity = JsonManager::SafeGet(in, "fftGravity", fft.gravity);
	editorFacade.ApplyFFTSettings(fft);

	// ===== DXR 屈折 =====
	WaterEditorRayTracingSettings rt = editorFacade.GetRayTracingSettings();
	rt.maxRefractionOffsetPixels = rtRefractionOffsetPixels_;
	editorFacade.ApplyRayTracingSettings(rt);

	// ===== コースティクス =====
	WaterEditorCausticsSettings caustics = editorFacade.GetCausticsSettings();
	caustics.intensity = JsonManager::SafeGet(in, "causticsIntensity", caustics.intensity);
	caustics.depthAttenuation = JsonManager::SafeGet(in, "causticsDepthAttenuation", caustics.depthAttenuation);
	caustics.curvatureScale = JsonManager::SafeGet(in, "causticsCurvatureScale", caustics.curvatureScale);
	caustics.surfaceSampleRadius = JsonManager::SafeGet(in, "causticsSurfaceSampleRadius", caustics.surfaceSampleRadius);
	caustics.refractiveIndex = JsonManager::SafeGet(in, "causticsRefractiveIndex", caustics.refractiveIndex);
	caustics.receiverNormalStrength = JsonManager::SafeGet(in, "causticsReceiverNormalStrength", caustics.receiverNormalStrength);
	caustics.alignmentPower = JsonManager::SafeGet(in, "causticsAlignmentPower", caustics.alignmentPower);
	editorFacade.ApplyCausticsSettings(caustics);
}

#endif // USE_IMGUI

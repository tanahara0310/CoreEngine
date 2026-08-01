#include "pch.h"
#include "PostEffectPresetManager.h"

// PostEffectPresetManager のシリアライズ実装（CaptureToJson / ApplyFromJson）。
// プリセットの明示保存（SavePreset / LoadPreset）とエディタ設定の自動保存
// （PostEffectSettingsSection）の両方から共用される。
// ファイル IO・プリセット UI は PostEffectPresetManager.cpp 側に分離している。

#include "PostEffectManager.h"
#include "Blur/Blur.h"
#include "RadialBlur/RadialBlur.h"
#include "ColorGrading/ColorGrading.h"
#include "ChromaticAberration/ChromaticAberration.h"
#include "Shockwave/Shockwave.h"
#include "Random/Random.h"
#include "RasterScroll/RasterScroll.h"
#include "FadeEffect/FadeEffect.h"
#include "Bloom/Bloom.h"
#include "ToneMapping/ToneMapping.h"
#include "LensFlare/LensFlare.h"
#include "Outline/Outline.h"
#include "Dissolve/Dissolve.h"

namespace CoreEngine
{
json PostEffectPresetManager::CaptureToJson(const PostEffectManager* postEffectManager)
{
    json presetData;

    // 各エフェクトの有効/無効状態を保存
    json enabledStates;
    enabledStates["GrayScale"] = postEffectManager->IsEffectEnabled("GrayScale");
    enabledStates["Blur"] = postEffectManager->IsEffectEnabled("Blur");
    enabledStates["RadialBlur"] = postEffectManager->IsEffectEnabled("RadialBlur");
    enabledStates["Shockwave"] = postEffectManager->IsEffectEnabled("Shockwave");
    enabledStates["Vignette"] = postEffectManager->IsEffectEnabled("Vignette");
    enabledStates["ColorGrading"] = postEffectManager->IsEffectEnabled("ColorGrading");
    enabledStates["ChromaticAberration"] = postEffectManager->IsEffectEnabled("ChromaticAberration");
    enabledStates["Sepia"] = postEffectManager->IsEffectEnabled("Sepia");
    enabledStates["Invert"] = postEffectManager->IsEffectEnabled("Invert");
    enabledStates["Random"] = postEffectManager->IsEffectEnabled("Random");
    enabledStates["RasterScroll"] = postEffectManager->IsEffectEnabled("RasterScroll");
    enabledStates["FadeEffect"] = postEffectManager->IsEffectEnabled("FadeEffect");
    enabledStates["Bloom"] = postEffectManager->IsEffectEnabled("Bloom");
    enabledStates["LensFlare"] = postEffectManager->IsEffectEnabled("LensFlare");
    enabledStates["Outline"] = postEffectManager->IsEffectEnabled("Outline");
    enabledStates["Dissolve"] = postEffectManager->IsEffectEnabled("Dissolve");
    presetData["enabledStates"] = enabledStates;

    // Blurのパラメータ保存
    if (auto* blur = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<Blur>("Blur")) {
        auto params = blur->GetParams();
        json blurJson;
        blurJson["intensity"] = params.intensity;
        blurJson["kernelSize"] = params.kernelSize;
        presetData["blur"] = blurJson;
    }

    // RadialBlurのパラメータ保存
    if (auto* radialBlur = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<RadialBlur>("RadialBlur")) {
        auto params = radialBlur->GetParams();
        json radialBlurJson;
        radialBlurJson["intensity"] = params.intensity;
        radialBlurJson["sampleCount"] = params.sampleCount;
        radialBlurJson["centerX"] = params.centerX;
        radialBlurJson["centerY"] = params.centerY;
        presetData["radialBlur"] = radialBlurJson;
    }

    // Vignette のパラメータは CVar（"r.Vignette.*"）へ移行済みのため、ここでは扱わない。
    // 保存・復元は CVarSettingsSection（CVars.json）が一括で行う。
    // enabled 状態のみ従来どおり enabledStates 経由で保存される

    // ColorGradingのパラメータ保存
    if (auto* colorGrading = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<ColorGrading>("ColorGrading")) {
        auto params = colorGrading->GetParams();
        json colorGradingJson;
        colorGradingJson["hue"] = params.hue;
        colorGradingJson["saturation"] = params.saturation;
        colorGradingJson["value"] = params.value;
        colorGradingJson["contrast"] = params.contrast;
        colorGradingJson["gamma"] = params.gamma;
        colorGradingJson["temperature"] = params.temperature;
        colorGradingJson["tint"] = params.tint;
        colorGradingJson["exposure"] = params.exposure;
        colorGradingJson["shadowLift"] = json::array({params.shadowLift[0], params.shadowLift[1], params.shadowLift[2]});
        colorGradingJson["midtoneGamma"] = json::array({params.midtoneGamma[0], params.midtoneGamma[1], params.midtoneGamma[2]});
        colorGradingJson["highlightGain"] = json::array({params.highlightGain[0], params.highlightGain[1], params.highlightGain[2]});
        presetData["colorGrading"] = colorGradingJson;
    }

    // ChromaticAberrationのパラメータ保存
    if (auto* chromaticAberration = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<ChromaticAberration>("ChromaticAberration")) {
        auto params = chromaticAberration->GetParams();
        json chromaticJson;
        chromaticJson["intensity"] = params.intensity;
        chromaticJson["radialFactor"] = params.radialFactor;
        chromaticJson["centerX"] = params.centerX;
        chromaticJson["centerY"] = params.centerY;
        chromaticJson["distortionScale"] = params.distortionScale;
        chromaticJson["falloff"] = params.falloff;
        presetData["chromaticAberration"] = chromaticJson;
    }

    // Shockwaveのパラメータ保存
    if (auto* shockwave = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<Shockwave>("Shockwave")) {
        auto params = shockwave->GetParams();
        json shockwaveJson;
        shockwaveJson["centerX"] = params.center[0];
        shockwaveJson["centerY"] = params.center[1];
        shockwaveJson["strength"] = params.strength;
        shockwaveJson["thickness"] = params.thickness;
        shockwaveJson["speed"] = params.speed;
        presetData["shockwave"] = shockwaveJson;
    }

    // Randomのパラメータ保存
    if (auto* random = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<Random>("Random")) {
        auto params = random->GetParams();
        json randomJson;
        randomJson["intensity"] = params.intensity;
        randomJson["blend"] = params.blend;
        randomJson["speed"] = params.speed;
        randomJson["grainScale"] = params.grainScale;
        randomJson["luminanceInfluence"] = params.luminanceInfluence;
        randomJson["chromaAmount"] = params.chromaAmount;
        presetData["random"] = randomJson;
    }

    // RasterScrollのパラメータ保存
    if (auto* rasterScroll = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<RasterScroll>("RasterScroll")) {
        auto params = rasterScroll->GetParams();
        json rasterScrollJson;
        rasterScrollJson["scrollSpeed"] = params.scrollSpeed;
        rasterScrollJson["lineHeight"] = params.lineHeight;
        rasterScrollJson["amplitude"] = params.amplitude;
        rasterScrollJson["frequency"] = params.frequency;
        rasterScrollJson["lineOffset"] = params.lineOffset;
        rasterScrollJson["distortionStrength"] = params.distortionStrength;
        presetData["rasterScroll"] = rasterScrollJson;
    }

    // FadeEffectのパラメータ保存
    if (auto* fadeEffect = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<FadeEffect>("FadeEffect")) {
        auto params = fadeEffect->GetParams();
        json fadeJson;
        fadeJson["fadeAlpha"] = params.fadeAlpha;
        fadeJson["fadeType"] = params.fadeType;
        fadeJson["spiralPower"] = params.spiralPower;
        fadeJson["rippleFreq"] = params.rippleFreq;
        fadeJson["glitchIntensity"] = params.glitchIntensity;
        fadeJson["portalSize"] = params.portalSize;
        fadeJson["colorShift"] = params.colorShift;
        presetData["fadeEffect"] = fadeJson;
    }

    // Bloomのパラメータ保存
    if (auto* bloom = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<Bloom>("Bloom")) {
        auto params = bloom->GetParams();
        json bloomJson;
        bloomJson["threshold"] = params.threshold;
        bloomJson["intensity"] = params.intensity;
        bloomJson["blurRadius"] = params.blurRadius;
        bloomJson["softKnee"] = params.softKnee;
        presetData["bloom"] = bloomJson;
    }

    // ToneMappingのパラメータ保存（常時有効のため enabled は保存しない）
    if (auto* toneMapping = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<ToneMapping>("ToneMapping")) {
        json toneMappingJson;
        toneMappingJson["exposureEV"] = toneMapping->GetExposureEV();
        toneMappingJson["autoExposureEnabled"] = toneMapping->IsAutoExposureEnabled();
        toneMappingJson["preserveSceneBrightness"] = toneMapping->GetPreserveSceneBrightness();
        toneMappingJson["adaptationSpeed"] = toneMapping->GetAdaptationSpeed();
        toneMappingJson["keyValue"] = toneMapping->GetKeyValue();
        toneMappingJson["minAutoEV"] = toneMapping->GetMinAutoEV();
        toneMappingJson["maxAutoEV"] = toneMapping->GetMaxAutoEV();
        toneMappingJson["referenceLuminance"] = toneMapping->GetReferenceLuminance();
        presetData["toneMapping"] = toneMappingJson;
    }

    // LensFlareのパラメータ保存（実行時に自動設定されるフィールドは保存しない）
    if (auto* lensFlare = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<LensFlare>("LensFlare")) {
        const auto& params = lensFlare->GetParams();
        json lensFlareJson;
        lensFlareJson["threshold"] = params.threshold;
        lensFlareJson["softKnee"] = params.softKnee;
        lensFlareJson["ghostDispersal"] = params.ghostDispersal;
        lensFlareJson["ghostIntensity"] = params.ghostIntensity;
        lensFlareJson["haloWidth"] = params.haloWidth;
        lensFlareJson["haloIntensity"] = params.haloIntensity;
        lensFlareJson["chromaDistortion"] = params.chromaDistortion;
        lensFlareJson["starburstIntensity"] = params.starburstIntensity;
        lensFlareJson["intensity"] = params.intensity;
        lensFlareJson["ghostCount"] = params.ghostCount;
        lensFlareJson["maxBrightness"] = params.maxBrightness;
        lensFlareJson["blurSigma"] = params.blurSigma;
        lensFlareJson["tint"] = json::array({ params.tint[0], params.tint[1], params.tint[2], params.tint[3] });
        lensFlareJson["apertureBladeCount"] = params.apertureBladeCount;
        lensFlareJson["apertureRotationRad"] = params.apertureRotationRad;
        lensFlareJson["ghostRadius"] = params.ghostRadius;
        lensFlareJson["ghostPolygonMix"] = params.ghostPolygonMix;
        lensFlareJson["sunMaskRadius"] = params.sunMaskRadius;
        presetData["lensFlare"] = lensFlareJson;
    }

    // Outlineのパラメータ保存（クリップ距離はカメラから毎フレーム設定されるため保存しない）
    if (auto* outline = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<Outline>("Outline")) {
        const auto& params = outline->GetParams();
        json outlineJson;
        outlineJson["outlineColor"] = json::array({
            params.outlineColor[0], params.outlineColor[1], params.outlineColor[2], params.outlineColor[3] });
        outlineJson["depthThreshold"] = params.depthThreshold;
        outlineJson["depthStrength"] = params.depthStrength;
        outlineJson["outlineWidth"] = params.outlineWidth;
        presetData["outline"] = outlineJson;
    }

    // Dissolveのパラメータ保存
    if (auto* dissolve = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<Dissolve>("Dissolve")) {
        const auto& params = dissolve->GetParams();
        json dissolveJson;
        dissolveJson["threshold"] = params.threshold;
        dissolveJson["edgeWidth"] = params.edgeWidth;
        dissolveJson["edgeColor"] = json::array({ params.edgeColorR, params.edgeColorG, params.edgeColorB });
        presetData["dissolve"] = dissolveJson;
    }

    presetData["version"] = "1.0";
    return presetData;
}

void PostEffectPresetManager::ApplyFromJson(PostEffectManager* postEffectManager, const json& presetData)
{
    // 各エフェクトの有効/無効状態を読み込み（欠損キーは現在の状態を維持する。
    // 旧プリセットに存在しない後発エフェクトのキーが「強制無効」にならないようにするため）
    if (presetData.contains("enabledStates")) {
        auto enabledStates = presetData["enabledStates"];
        const auto applyEnabled = [&](const char* name) {
            postEffectManager->SetEffectEnabled(name,
                JsonManager::SafeGet(enabledStates, name, postEffectManager->IsEffectEnabled(name)));
        };
        applyEnabled("GrayScale");
        applyEnabled("Blur");
        applyEnabled("RadialBlur");
        applyEnabled("Shockwave");
        applyEnabled("Vignette");
        applyEnabled("ColorGrading");
        applyEnabled("ChromaticAberration");
        applyEnabled("Sepia");
        applyEnabled("Invert");
        applyEnabled("Random");
        applyEnabled("RasterScroll");
        applyEnabled("FadeEffect");
        applyEnabled("Bloom");
        applyEnabled("LensFlare");
        applyEnabled("Outline");
        applyEnabled("Dissolve");
    }

    // Blurのパラメータ読み込み
    if (presetData.contains("blur")) {
        auto blurJson = presetData["blur"];
        if (auto* blur = postEffectManager->GetEffect<Blur>("Blur")) {
            Blur::BlurParams params;
            params.intensity = JsonManager::SafeGet(blurJson, "intensity", 1.0f);
            params.kernelSize = JsonManager::SafeGet(blurJson, "kernelSize", 1.0f);
            blur->SetParams(params);
        }
    }

    // RadialBlurのパラメータ読み込み
    if (presetData.contains("radialBlur")) {
        auto radialBlurJson = presetData["radialBlur"];
        if (auto* radialBlur = postEffectManager->GetEffect<RadialBlur>("RadialBlur")) {
            RadialBlur::RadialBlurParams params;
            params.intensity = JsonManager::SafeGet(radialBlurJson, "intensity", 0.5f);
            params.sampleCount = JsonManager::SafeGet(radialBlurJson, "sampleCount", 8.0f);
            params.centerX = JsonManager::SafeGet(radialBlurJson, "centerX", 0.5f);
            params.centerY = JsonManager::SafeGet(radialBlurJson, "centerY", 0.5f);
            radialBlur->SetParams(params);
        }
    }

    // Vignette のパラメータは CVar（"r.Vignette.*"）が唯一の保持者。
    // 旧形式の "vignette" キーが残っているファイルは、読み飛ばして CVars.json 側を優先する

    // ColorGradingのパラメータ読み込み
    if (presetData.contains("colorGrading")) {
        auto colorGradingJson = presetData["colorGrading"];
        if (auto* colorGrading = postEffectManager->GetEffect<ColorGrading>("ColorGrading")) {
            ColorGrading::ColorGradingParams params;
            params.hue = JsonManager::SafeGet(colorGradingJson, "hue", 0.0f);
            params.saturation = JsonManager::SafeGet(colorGradingJson, "saturation", 1.0f);
            params.value = JsonManager::SafeGet(colorGradingJson, "value", 1.0f);
            params.contrast = JsonManager::SafeGet(colorGradingJson, "contrast", 1.0f);
            params.gamma = JsonManager::SafeGet(colorGradingJson, "gamma", 1.0f);
            params.temperature = JsonManager::SafeGet(colorGradingJson, "temperature", 0.0f);
            params.tint = JsonManager::SafeGet(colorGradingJson, "tint", 0.0f);
            params.exposure = JsonManager::SafeGet(colorGradingJson, "exposure", 0.0f);

            if (colorGradingJson.contains("shadowLift") && colorGradingJson["shadowLift"].is_array()) {
                auto arr = colorGradingJson["shadowLift"];
                if (arr.size() >= 3) {
                    params.shadowLift[0] = arr[0].get<float>();
                    params.shadowLift[1] = arr[1].get<float>();
                    params.shadowLift[2] = arr[2].get<float>();
                }
            }

            if (colorGradingJson.contains("midtoneGamma") && colorGradingJson["midtoneGamma"].is_array()) {
                auto arr = colorGradingJson["midtoneGamma"];
                if (arr.size() >= 3) {
                    params.midtoneGamma[0] = arr[0].get<float>();
                    params.midtoneGamma[1] = arr[1].get<float>();
                    params.midtoneGamma[2] = arr[2].get<float>();
                }
            }

            if (colorGradingJson.contains("highlightGain") && colorGradingJson["highlightGain"].is_array()) {
                auto arr = colorGradingJson["highlightGain"];
                if (arr.size() >= 3) {
                    params.highlightGain[0] = arr[0].get<float>();
                    params.highlightGain[1] = arr[1].get<float>();
                    params.highlightGain[2] = arr[2].get<float>();
                }
            }

            colorGrading->SetParams(params);
        }
    }

    // ChromaticAberrationのパラメータ読み込み
    if (presetData.contains("chromaticAberration")) {
        auto chromaticJson = presetData["chromaticAberration"];
        if (auto* chromaticAberration = postEffectManager->GetEffect<ChromaticAberration>("ChromaticAberration")) {
            ChromaticAberration::ChromaticAberrationParams params;
            params.intensity = JsonManager::SafeGet(chromaticJson, "intensity", 3.0f);
            params.radialFactor = JsonManager::SafeGet(chromaticJson, "radialFactor", 1.0f);
            params.centerX = JsonManager::SafeGet(chromaticJson, "centerX", 0.5f);
            params.centerY = JsonManager::SafeGet(chromaticJson, "centerY", 0.5f);
            params.distortionScale = JsonManager::SafeGet(chromaticJson, "distortionScale", 1.0f);
            params.falloff = JsonManager::SafeGet(chromaticJson, "falloff", 1.5f);
            chromaticAberration->SetParams(params);
        }
    }

    // Shockwaveのパラメータ読み込み
    if (presetData.contains("shockwave")) {
        auto shockwaveJson = presetData["shockwave"];
        if (auto* shockwave = postEffectManager->GetEffect<Shockwave>("Shockwave")) {
            Shockwave::ShockwaveParams params;
            params.center[0] = JsonManager::SafeGet(shockwaveJson, "centerX", 0.5f);
            params.center[1] = JsonManager::SafeGet(shockwaveJson, "centerY", 0.5f);
            params.strength = JsonManager::SafeGet(shockwaveJson, "strength", 0.1f);
            params.thickness = JsonManager::SafeGet(shockwaveJson, "thickness", 0.1f);
            params.speed = JsonManager::SafeGet(shockwaveJson, "speed", 1.0f);
            params.time = 0.0f;
            shockwave->SetParams(params);
        }
    }

    // Randomのパラメータ読み込み
    if (presetData.contains("random")) {
        auto randomJson = presetData["random"];
        if (auto* random = postEffectManager->GetEffect<Random>("Random")) {
            Random::RandomParams params;
            params.intensity = JsonManager::SafeGet(randomJson, "intensity", 0.15f);
            params.blend = JsonManager::SafeGet(randomJson, "blend", 0.35f);
            params.speed = JsonManager::SafeGet(randomJson, "speed", 1.0f);
            params.grainScale = JsonManager::SafeGet(randomJson, "grainScale", 1.0f);
            params.luminanceInfluence = JsonManager::SafeGet(randomJson, "luminanceInfluence", 0.25f);
            params.chromaAmount = JsonManager::SafeGet(randomJson, "chromaAmount", 0.15f);
            params.time = 0.0f;
            random->SetParams(params);
        }
    }

    // RasterScrollのパラメータ読み込み
    if (presetData.contains("rasterScroll")) {
        auto rasterScrollJson = presetData["rasterScroll"];
        if (auto* rasterScroll = postEffectManager->GetEffect<RasterScroll>("RasterScroll")) {
            RasterScroll::RasterScrollParams params;
            params.scrollSpeed = JsonManager::SafeGet(rasterScrollJson, "scrollSpeed", 1.0f);
            params.lineHeight = JsonManager::SafeGet(rasterScrollJson, "lineHeight", 10.0f);
            params.amplitude = JsonManager::SafeGet(rasterScrollJson, "amplitude", 0.02f);
            params.frequency = JsonManager::SafeGet(rasterScrollJson, "frequency", 1.5f);
            params.lineOffset = JsonManager::SafeGet(rasterScrollJson, "lineOffset", 0.0f);
            params.distortionStrength = JsonManager::SafeGet(rasterScrollJson, "distortionStrength", 1.0f);
            params.time = 0.0f;
            rasterScroll->SetParams(params);
        }
    }

    // FadeEffectのパラメータ読み込み
    if (presetData.contains("fadeEffect")) {
        auto fadeJson = presetData["fadeEffect"];
        if (auto* fadeEffect = postEffectManager->GetEffect<FadeEffect>("FadeEffect")) {
            fadeEffect->SetFadeAlpha(JsonManager::SafeGet(fadeJson, "fadeAlpha", 0.0f));

            float fadeType = JsonManager::SafeGet(fadeJson, "fadeType", 0.0f);
            fadeEffect->SetFadeType(static_cast<FadeEffect::FadeType>(static_cast<int>(fadeType)));

            fadeEffect->SetSpiralPower(JsonManager::SafeGet(fadeJson, "spiralPower", 5.0f));
            fadeEffect->SetRippleFrequency(JsonManager::SafeGet(fadeJson, "rippleFreq", 10.0f));
            fadeEffect->SetGlitchIntensity(JsonManager::SafeGet(fadeJson, "glitchIntensity", 0.5f));
            fadeEffect->SetPortalSize(JsonManager::SafeGet(fadeJson, "portalSize", 0.3f));
            fadeEffect->SetColorShift(JsonManager::SafeGet(fadeJson, "colorShift", 0.0f));
        }
    }

    // Bloomのパラメータ読み込み
    if (presetData.contains("bloom")) {
        auto bloomJson = presetData["bloom"];
        if (auto* bloom = postEffectManager->GetEffect<Bloom>("Bloom")) {
            Bloom::BloomParams params;
            params.threshold = JsonManager::SafeGet(bloomJson, "threshold", 1.0f);
            params.intensity = JsonManager::SafeGet(bloomJson, "intensity", 1.0f);
            params.blurRadius = JsonManager::SafeGet(bloomJson, "blurRadius", 1.0f);
            params.softKnee = JsonManager::SafeGet(bloomJson, "softKnee", 1.0f);
            bloom->SetParams(params);
        }
    }

    // ToneMappingのパラメータ読み込み
    if (presetData.contains("toneMapping")) {
        auto toneMappingJson = presetData["toneMapping"];
        if (auto* toneMapping = postEffectManager->GetEffect<ToneMapping>("ToneMapping")) {
            toneMapping->SetExposureEV(JsonManager::SafeGet(toneMappingJson, "exposureEV", toneMapping->GetExposureEV()));
            toneMapping->SetAutoExposureEnabled(
                JsonManager::SafeGet(toneMappingJson, "autoExposureEnabled", toneMapping->IsAutoExposureEnabled()));
            toneMapping->SetPreserveSceneBrightness(
                JsonManager::SafeGet(toneMappingJson, "preserveSceneBrightness", toneMapping->GetPreserveSceneBrightness()));
            toneMapping->SetAdaptationSpeed(
                JsonManager::SafeGet(toneMappingJson, "adaptationSpeed", toneMapping->GetAdaptationSpeed()));
            toneMapping->SetKeyValue(JsonManager::SafeGet(toneMappingJson, "keyValue", toneMapping->GetKeyValue()));
            toneMapping->SetAutoEVRange(
                JsonManager::SafeGet(toneMappingJson, "minAutoEV", toneMapping->GetMinAutoEV()),
                JsonManager::SafeGet(toneMappingJson, "maxAutoEV", toneMapping->GetMaxAutoEV()));
            toneMapping->SetReferenceLuminance(
                JsonManager::SafeGet(toneMappingJson, "referenceLuminance", toneMapping->GetReferenceLuminance()));
        }
    }

    // LensFlareのパラメータ読み込み（現在値を起点に、保存対象のフィールドだけ上書きする）
    if (presetData.contains("lensFlare")) {
        auto lensFlareJson = presetData["lensFlare"];
        if (auto* lensFlare = postEffectManager->GetEffect<LensFlare>("LensFlare")) {
            LensFlare::LensFlareConstants params = lensFlare->GetParams();
            params.threshold = JsonManager::SafeGet(lensFlareJson, "threshold", params.threshold);
            params.softKnee = JsonManager::SafeGet(lensFlareJson, "softKnee", params.softKnee);
            params.ghostDispersal = JsonManager::SafeGet(lensFlareJson, "ghostDispersal", params.ghostDispersal);
            params.ghostIntensity = JsonManager::SafeGet(lensFlareJson, "ghostIntensity", params.ghostIntensity);
            params.haloWidth = JsonManager::SafeGet(lensFlareJson, "haloWidth", params.haloWidth);
            params.haloIntensity = JsonManager::SafeGet(lensFlareJson, "haloIntensity", params.haloIntensity);
            params.chromaDistortion = JsonManager::SafeGet(lensFlareJson, "chromaDistortion", params.chromaDistortion);
            params.starburstIntensity = JsonManager::SafeGet(lensFlareJson, "starburstIntensity", params.starburstIntensity);
            params.intensity = JsonManager::SafeGet(lensFlareJson, "intensity", params.intensity);
            params.ghostCount = JsonManager::SafeGet(lensFlareJson, "ghostCount", params.ghostCount);
            params.maxBrightness = JsonManager::SafeGet(lensFlareJson, "maxBrightness", params.maxBrightness);
            params.blurSigma = JsonManager::SafeGet(lensFlareJson, "blurSigma", params.blurSigma);
            if (lensFlareJson.contains("tint") && lensFlareJson["tint"].is_array() && lensFlareJson["tint"].size() >= 4) {
                params.tint[0] = lensFlareJson["tint"][0].get<float>();
                params.tint[1] = lensFlareJson["tint"][1].get<float>();
                params.tint[2] = lensFlareJson["tint"][2].get<float>();
                params.tint[3] = lensFlareJson["tint"][3].get<float>();
            }
            params.apertureBladeCount = JsonManager::SafeGet(lensFlareJson, "apertureBladeCount", params.apertureBladeCount);
            params.apertureRotationRad = JsonManager::SafeGet(lensFlareJson, "apertureRotationRad", params.apertureRotationRad);
            params.ghostRadius = JsonManager::SafeGet(lensFlareJson, "ghostRadius", params.ghostRadius);
            params.ghostPolygonMix = JsonManager::SafeGet(lensFlareJson, "ghostPolygonMix", params.ghostPolygonMix);
            params.sunMaskRadius = JsonManager::SafeGet(lensFlareJson, "sunMaskRadius", params.sunMaskRadius);
            lensFlare->SetParams(params);
        }
    }

    // Outlineのパラメータ読み込み
    if (presetData.contains("outline")) {
        auto outlineJson = presetData["outline"];
        if (auto* outline = postEffectManager->GetEffect<Outline>("Outline")) {
            Outline::OutlineParams params = outline->GetParams();
            if (outlineJson.contains("outlineColor") && outlineJson["outlineColor"].is_array() &&
                outlineJson["outlineColor"].size() >= 4) {
                params.outlineColor[0] = outlineJson["outlineColor"][0].get<float>();
                params.outlineColor[1] = outlineJson["outlineColor"][1].get<float>();
                params.outlineColor[2] = outlineJson["outlineColor"][2].get<float>();
                params.outlineColor[3] = outlineJson["outlineColor"][3].get<float>();
            }
            params.depthThreshold = JsonManager::SafeGet(outlineJson, "depthThreshold", params.depthThreshold);
            params.depthStrength = JsonManager::SafeGet(outlineJson, "depthStrength", params.depthStrength);
            params.outlineWidth = JsonManager::SafeGet(outlineJson, "outlineWidth", params.outlineWidth);
            outline->SetParams(params);
        }
    }

    // Dissolveのパラメータ読み込み
    if (presetData.contains("dissolve")) {
        auto dissolveJson = presetData["dissolve"];
        if (auto* dissolve = postEffectManager->GetEffect<Dissolve>("Dissolve")) {
            Dissolve::DissolveParams params = dissolve->GetParams();
            params.threshold = JsonManager::SafeGet(dissolveJson, "threshold", params.threshold);
            params.edgeWidth = JsonManager::SafeGet(dissolveJson, "edgeWidth", params.edgeWidth);
            if (dissolveJson.contains("edgeColor") && dissolveJson["edgeColor"].is_array() &&
                dissolveJson["edgeColor"].size() >= 3) {
                params.edgeColorR = dissolveJson["edgeColor"][0].get<float>();
                params.edgeColorG = dissolveJson["edgeColor"][1].get<float>();
                params.edgeColorB = dissolveJson["edgeColor"][2].get<float>();
            }
            dissolve->SetParams(params);
        }
    }
}
}

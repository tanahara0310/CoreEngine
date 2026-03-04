#include "PostEffectPresetManager.h"
#include "PostEffectManager.h"
#include "Effect/Blur.h"
#include "Effect/RadialBlur.h"
#include "Effect/Vignette.h"
#include "Effect/ColorGrading.h"
#include "Effect/ChromaticAberration.h"
#include "Effect/Shockwave.h"
#include "Effect/RasterScroll.h"
#include "Effect/FadeEffect.h"
#include "Effect/Bloom.h"
#include <filesystem>
#include <iostream>

#ifdef _DEBUG
#include "Utility/Debug/ImGui/ImguiManager.h"
#endif


namespace CoreEngine
{
bool PostEffectPresetManager::SavePreset(const PostEffectManager* postEffectManager, const std::string& filePath)
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
    enabledStates["RasterScroll"] = postEffectManager->IsEffectEnabled("RasterScroll");
    enabledStates["FadeEffect"] = postEffectManager->IsEffectEnabled("FadeEffect");
    enabledStates["Bloom"] = postEffectManager->IsEffectEnabled("Bloom");
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

    // Vignetteのパラメータ保存
    if (auto* vignette = const_cast<PostEffectManager*>(postEffectManager)->GetEffect<Vignette>("Vignette")) {
        auto params = vignette->GetParams();
        json vignetteJson;
        vignetteJson["intensity"] = params.intensity;
        vignetteJson["smoothness"] = params.smoothness;
        vignetteJson["size"] = params.size;
        presetData["vignette"] = vignetteJson;
    }

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

    presetData["version"] = "1.0";

    bool success = JsonManager::GetInstance().SaveJson(filePath, presetData);
    if (success) {
        std::cout << "PostEffect preset saved: " << filePath << std::endl;
        needUpdateFileList_ = true;
        
        currentPresetPath_ = filePath;
        currentPresetName_ = GetFileNameWithoutExtension(std::filesystem::path(filePath).filename().string());
    } else {
        std::cerr << "Failed to save PostEffect preset: " << filePath << std::endl;
    }

    return success;
}

bool PostEffectPresetManager::LoadPreset(PostEffectManager* postEffectManager, const std::string& filePath)
{
    if (!JsonManager::GetInstance().FileExists(filePath)) {
        std::cerr << "PostEffect preset file not found: " << filePath << std::endl;
        return false;
    }

    json presetData = JsonManager::GetInstance().LoadJson(filePath);
    if (presetData.empty()) {
        std::cerr << "Failed to load PostEffect preset: " << filePath << std::endl;
        return false;
    }

    // 各エフェクトの有効/無効状態を読み込み
    if (presetData.contains("enabledStates")) {
        auto enabledStates = presetData["enabledStates"];
        postEffectManager->SetEffectEnabled("GrayScale", JsonManager::SafeGet(enabledStates, "GrayScale", false));
        postEffectManager->SetEffectEnabled("Blur", JsonManager::SafeGet(enabledStates, "Blur", false));
        postEffectManager->SetEffectEnabled("RadialBlur", JsonManager::SafeGet(enabledStates, "RadialBlur", false));
        postEffectManager->SetEffectEnabled("Shockwave", JsonManager::SafeGet(enabledStates, "Shockwave", false));
        postEffectManager->SetEffectEnabled("Vignette", JsonManager::SafeGet(enabledStates, "Vignette", false));
        postEffectManager->SetEffectEnabled("ColorGrading", JsonManager::SafeGet(enabledStates, "ColorGrading", false));
        postEffectManager->SetEffectEnabled("ChromaticAberration", JsonManager::SafeGet(enabledStates, "ChromaticAberration", false));
        postEffectManager->SetEffectEnabled("Sepia", JsonManager::SafeGet(enabledStates, "Sepia", false));
        postEffectManager->SetEffectEnabled("Invert", JsonManager::SafeGet(enabledStates, "Invert", false));
        postEffectManager->SetEffectEnabled("RasterScroll", JsonManager::SafeGet(enabledStates, "RasterScroll", false));
        postEffectManager->SetEffectEnabled("FadeEffect", JsonManager::SafeGet(enabledStates, "FadeEffect", true));
        postEffectManager->SetEffectEnabled("Bloom", JsonManager::SafeGet(enabledStates, "Bloom", false));
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

    // Vignetteのパラメータ読み込み
    if (presetData.contains("vignette")) {
        auto vignetteJson = presetData["vignette"];
        if (auto* vignette = postEffectManager->GetEffect<Vignette>("Vignette")) {
            Vignette::VignetteParams params;
            params.intensity = JsonManager::SafeGet(vignetteJson, "intensity", 0.8f);
            params.smoothness = JsonManager::SafeGet(vignetteJson, "smoothness", 0.8f);
            params.size = JsonManager::SafeGet(vignetteJson, "size", 16.0f);
            vignette->SetParams(params);
        }
    }

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

    currentPresetPath_ = filePath;
    currentPresetName_ = GetFileNameWithoutExtension(std::filesystem::path(filePath).filename().string());

    std::cout << "PostEffect preset loaded: " << filePath << std::endl;
    return true;
}

std::vector<std::string> PostEffectPresetManager::GetPresetList(const std::string& directory)
{
    std::vector<std::string> fileList;

    try {
        if (!std::filesystem::exists(directory)) {
            return fileList;
        }

        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                fileList.push_back(entry.path().filename().string());
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error listing PostEffect preset files: " << e.what() << std::endl;
    }

    return fileList;
}

void PostEffectPresetManager::ShowImGui(PostEffectManager* postEffectManager)
{
#ifdef _DEBUG
    if (ImGui::CollapsingHeader("プリセット管理")) {
        // キーボードショートカット: Ctrl+S で上書き保存
        if (!currentPresetPath_.empty() && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S)) {
            if (SaveCurrentPreset(postEffectManager)) {
                ImGui::OpenPopup("上書き保存成功");
            } else {
                ImGui::OpenPopup("上書き保存失敗");
            }
        }

        // 現在読み込まれているプリセット情報を表示
        if (!currentPresetPath_.empty()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "現在のプリセット: %s", currentPresetName_.c_str());
            ImGui::Text("パス: %s", currentPresetPath_.c_str());
            
            // 上書き保存ボタン
            if (ImGui::Button("上書き保存 (Ctrl+S)", ImVec2(200, 0))) {
                if (SaveCurrentPreset(postEffectManager)) {
                    ImGui::OpenPopup("上書き保存成功");
                } else {
                    ImGui::OpenPopup("上書き保存失敗");
                }
            }
            
            ImGui::SameLine();
            
            // プリセットをクリア
            if (ImGui::Button("プリセットをクリア", ImVec2(150, 0))) {
                currentPresetPath_.clear();
                currentPresetName_.clear();
            }
            
            ImGui::Separator();
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "現在のプリセット: なし");
            ImGui::Separator();
        }

        // 上書き保存成功ポップアップ
        if (ImGui::BeginPopupModal("上書き保存成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("プリセットを上書き保存しました。");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // 上書き保存失敗ポップアップ
        if (ImGui::BeginPopupModal("上書き保存失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("プリセットの上書き保存に失敗しました。");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ディレクトリパス設定
        ImGui::Text("保存先ディレクトリ");
        if (ImGui::InputText("##Directory", directoryPathBuffer_, sizeof(directoryPathBuffer_))) {
            needUpdateFileList_ = true;
        }

        ImGui::Separator();

        // 保存セクション
        ImGui::Text("=== 保存 ===");
        ImGui::InputText("ファイル名", saveFileNameBuffer_, sizeof(saveFileNameBuffer_));
        
        if (ImGui::Button("プリセットを保存", ImVec2(200, 0))) {
            std::string fileName = std::string(saveFileNameBuffer_);
            if (!fileName.empty()) {
                // 拡張子がない場合は追加
                if (fileName.find(".json") == std::string::npos) {
                    fileName += ".json";
                }
                
                std::string fullPath = std::string(directoryPathBuffer_) + fileName;
                
                // ディレクトリが存在しない場合は作成
                try {
                    std::filesystem::create_directories(directoryPathBuffer_);
                } catch (const std::exception& e) {
                    std::cerr << "Failed to create directory: " << e.what() << std::endl;
                }
                
                if (SavePreset(postEffectManager, fullPath)) {
                    ImGui::OpenPopup("保存成功");
                } else {
                    ImGui::OpenPopup("保存失敗");
                }
            }
        }

        // 保存成功ポップアップ
        if (ImGui::BeginPopupModal("保存成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("プリセットを保存しました。");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // 保存失敗ポップアップ
        if (ImGui::BeginPopupModal("保存失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("プリセットの保存に失敗しました。");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();

        // 読み込みセクション
        ImGui::Text("=== 読み込み ===");

        // ファイルリスト更新ボタン
        if (ImGui::Button("リストを更新") || needUpdateFileList_) {
            UpdatePresetFileList();
            needUpdateFileList_ = false;
        }

        // プリセットファイルリスト表示
        if (!presetFileList_.empty()) {
            ImGui::BeginChild("PresetList", ImVec2(0, 150), true);
            for (size_t i = 0; i < presetFileList_.size(); ++i) {
                bool isSelected = (selectedPresetIndex_ == static_cast<int>(i));
                if (ImGui::Selectable(GetFileNameWithoutExtension(presetFileList_[i]).c_str(), isSelected)) {
                    selectedPresetIndex_ = static_cast<int>(i);
                }
            }
            ImGui::EndChild();

            // 選択中のファイルを表示
            if (selectedPresetIndex_ >= 0 && selectedPresetIndex_ < static_cast<int>(presetFileList_.size())) {
                ImGui::Text("選択: %s", presetFileList_[selectedPresetIndex_].c_str());
                
                if (ImGui::Button("プリセットを読み込み", ImVec2(200, 0))) {
                    std::string fullPath = std::string(directoryPathBuffer_) + presetFileList_[selectedPresetIndex_];
                    if (LoadPreset(postEffectManager, fullPath)) {
                        ImGui::OpenPopup("読み込み成功");
                    } else {
                        ImGui::OpenPopup("読み込み失敗");
                    }
                }
            }
        } else {
            ImGui::Text("プリセットファイルが見つかりません。");
        }

        // 読み込み成功ポップアップ
        if (ImGui::BeginPopupModal("読み込み成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("プリセットを読み込みました。");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // 読み込み失敗ポップアップ
        if (ImGui::BeginPopupModal("読み込み失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("プリセットの読み込みに失敗しました。");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
#else
    (void)postEffectManager; // 未使用警告を抑制
#endif
    (void)postEffectManager;
}

void PostEffectPresetManager::UpdatePresetFileList()
{
    presetFileList_ = GetPresetList(directoryPathBuffer_);
    selectedPresetIndex_ = -1;
}

std::string PostEffectPresetManager::GetFileNameWithoutExtension(const std::string& filename)
{
    size_t lastDot = filename.find_last_of('.');
    if (lastDot != std::string::npos) {
        return filename.substr(0, lastDot);
    }
    return filename;
}

bool PostEffectPresetManager::SaveCurrentPreset(PostEffectManager* postEffectManager)
{
    if (currentPresetPath_.empty()) {
        std::cerr << "No preset is currently loaded" << std::endl;
        return false;
    }

    return SavePreset(postEffectManager, currentPresetPath_);
}
}

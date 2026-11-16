#include "pch.h"
#include "ParticlePresetManager.h"
#include "IParticleSystem.h"
#include "Modules/MainModule.h"
#include "Modules/EmissionModule.h"
#include "Modules/ShapeModule.h"
#include "Modules/VelocityModule.h"
#include "Modules/ColorModule.h"
#include "Modules/ForceModule.h"
#include "Modules/SizeModule.h"
#include "Modules/RotationModule.h"
#include "Modules/NoiseModule.h"
#include "Modules/CollisionModule.h"
#include "Utility/JsonManager/JsonFields.h"
#include "Utility/Logger/Logger.h"
#include <filesystem>

#ifdef CORE_EDITOR
#include "Editor/ImGui/ImguiManager.h"
#endif

// Windows.hのマクロ干渉を回避
#ifdef CreateDirectory
#undef CreateDirectory
#endif

namespace CoreEngine
{
namespace
{
    /// @brief 書き出すプリセットの版（放出位置は持たない）
    constexpr const char* kPresetVersion = "2.2";

    /// @brief モジュール 1 つの設定と有効を、プリセットの key の欄へ書く
    /// @param fields 設定の項目の並び（JsonFields の形）
    template <class TModule, class Fields>
    void SaveModule(json& presetData, const char* key, const TModule& module, Fields&& fields)
    {
        json moduleJson;
        JsonFields::WriteFields(moduleJson, module.GetData(), fields);
        moduleJson["enabled"] = module.IsEnabled();
        presetData[key] = moduleJson;
    }

    /// @brief プリセットの key の欄から、モジュール 1 つの設定と有効を読む（欄が無ければ何もしない）
    /// @param defaultEnabled 欄に有効が無いときの有効
    /// @param fields 設定の項目の並び（既定の値の設定へ欄の値を読む）
    template <class TModule, class Fields>
    void LoadModule(const json& presetData, const char* key, TModule& module, bool defaultEnabled, Fields&& fields)
    {
        if (!presetData.contains(key)) {
            return;
        }
        const json& moduleJson = presetData[key];
        typename TModule::Data data;
        JsonFields::ReadFields(moduleJson, data, fields);
        module.SetData(data);
        module.SetEnabled(JsonManager::SafeGet(moduleJson, "enabled", defaultEnabled));
    }

    /// @brief MainModule の設定の項目（シミュレーション空間は欄が無ければローカル）
    constexpr auto kMainFields = [](auto& data, auto&& field) {
        field("duration", data.duration);
        field("looping", data.looping);
        field("playOnAwake", data.playOnAwake);
        field("maxParticles", data.maxParticles);
        field("simulationSpace", data.simulationSpace, JsonFields::IfMissing(MainModule::SimulationSpace::Local));
        field("startLifetime", data.startLifetime);
        field("startLifetimeRandomness", data.startLifetimeRandomness);
        field("startSpeed", data.startSpeed);
        field("startSpeedRandomness", data.startSpeedRandomness);
        field("startSize", data.startSize);
        field("startSizeRandomness", data.startSizeRandomness);
        field("startRotation", data.startRotation);
        field("startRotationRandomness", data.startRotationRandomness);
        field("startColor", data.startColor);
        field("startColorRandomness", data.startColorRandomness);
        field("gravityModifier", data.gravityModifier);
    };

    /// @brief EmissionModule の設定の項目
    constexpr auto kEmissionFields = [](auto& data, auto&& field) {
        field("rateOverTime", data.rateOverTime);
        field("burstCount", data.burstCount);
        field("burstTime", data.burstTime);
    };

    /// @brief ShapeModule の設定の項目
    constexpr auto kShapeFields = [](auto& data, auto&& field) {
        field("shapeType", data.shapeType);
        field("scale", data.scale);
        field("radius", data.radius);
        field("innerRadius", data.innerRadius);
        field("height", data.height);
        field("angle", data.angle);
        field("randomPositionRange", data.randomPositionRange);
        field("emitFromSurface", data.emitFromSurface);
        field("emissionDirection", data.emissionDirection);
        field("circlePlane", data.circlePlane);
    };

    /// @brief VelocityModule の設定の項目
    constexpr auto kVelocityFields = [](auto& data, auto&& field) {
        field("startSpeed", data.startSpeed);
        field("randomSpeedRange", data.randomSpeedRange);
        field("useRandomDirection", data.useRandomDirection);
    };

    /// @brief ColorModule の設定の項目
    constexpr auto kColorFields = [](auto& data, auto&& field) {
        field("endColor", data.endColor);
        field("useGradient", data.useGradient);
        field("startRatio", data.startRatio);
    };

    /// @brief ForceModule の設定の項目
    constexpr auto kForceFields = [](auto& data, auto&& field) {
        field("gravity", data.gravity);
        field("wind", data.wind);
        field("drag", data.drag);
        field("acceleration", data.acceleration);
        field("areaMin", data.area.min);
        field("areaMax", data.area.max);
        field("useAccelerationField", data.useAccelerationField);
    };

    /// @brief SizeModule の設定の項目
    constexpr auto kSizeFields = [](auto& data, auto&& field) {
        field("endSize", data.endSize);
        field("sizeOverLifetime", data.sizeOverLifetime);
        field("endSize3D", data.endSize3D);
        field("use3DSize", data.use3DSize);
        field("sizeCurve", data.sizeCurve);
        field("minSize", data.minSize);
        field("maxSize", data.maxSize);
        field("uniformScaling", data.uniformScaling);
    };

    /// @brief RotationModule の設定の項目
    constexpr auto kRotationFields = [](auto& data, auto&& field) {
        field("rotationSpeed", data.rotationSpeed);
        field("rotationSpeedRandomness", data.rotationSpeedRandomness);
        field("use2DRotation", data.use2DRotation);
        field("rotation2DSpeed", data.rotation2DSpeed);
        field("rotation2DSpeedRandomness", data.rotation2DSpeedRandomness);
        field("rotationDirection", data.rotationDirection);
        field("rotationOverLifetime", data.rotationOverLifetime);
        field("startRotationSpeedMultiplier", data.startRotationSpeedMultiplier);
        field("endRotationSpeedMultiplier", data.endRotationSpeedMultiplier);
        field("limitRotationRange", data.limitRotationRange);
        field("minRotation", data.minRotation);
        field("maxRotation", data.maxRotation);
        field("alignToVelocity", data.alignToVelocity);
        field("velocityAlignmentStrength", data.velocityAlignmentStrength);
    };

    /// @brief NoiseModule の設定の項目
    constexpr auto kNoiseFields = [](auto& data, auto&& field) {
        field("strength", data.strength);
        field("frequency", data.frequency);
        field("scrollSpeed", data.scrollSpeed);
        field("damping", data.damping);
        field("positionAmount", data.positionAmount);
    };

    /// @brief CollisionModule の設定の項目
    constexpr auto kCollisionFields = [](auto& data, auto&& field) {
        field("planeHeight", data.planeHeight);
        field("contactOffset", data.contactOffset);
        field("bounce", data.bounce);
        field("friction", data.friction);
        field("restSpeed", data.restSpeed);
        field("roll", data.roll);
        field("rollRadius", data.rollRadius);
        field("useParticleScale", data.useParticleScale);
    };
}

json ParticlePresetManager::ToJson(IParticleSystem& particleSystem)
{
    json presetData;

    // ビルボードタイプとブレンドモードの保存
    presetData["billboardType"] = static_cast<int>(particleSystem.GetBillboardType());
    presetData["blendMode"] = static_cast<int>(particleSystem.GetBlendMode());

    // 各モジュールの保存
    SaveModule(presetData, "main", particleSystem.GetMainModule(), kMainFields);
    SaveModule(presetData, "emission", particleSystem.GetEmissionModule(), kEmissionFields);
    SaveModule(presetData, "shape", particleSystem.GetShapeModule(), kShapeFields);
    SaveModule(presetData, "velocity", particleSystem.GetVelocityModule(), kVelocityFields);
    SaveModule(presetData, "color", particleSystem.GetColorModule(), kColorFields);
    SaveModule(presetData, "force", particleSystem.GetForceModule(), kForceFields);
    SaveModule(presetData, "size", particleSystem.GetSizeModule(), kSizeFields);
    SaveModule(presetData, "rotation", particleSystem.GetRotationModule(), kRotationFields);
    SaveModule(presetData, "noise", particleSystem.GetNoiseModule(), kNoiseFields);

    // CollisionModuleの保存（床との当たり判定。CPU版だけが持つ）
    if (CollisionModule* const collision = particleSystem.GetCollisionModule()) {
        SaveModule(presetData, "collision", *collision, kCollisionFields);
    }

    // メタデータ
    presetData["version"] = kPresetVersion;
    return presetData;
}

bool ParticlePresetManager::SavePreset(IParticleSystem* particleSystem, const std::string& filePath)
{
    const bool success = particleSystem && JsonManager::GetInstance().SaveJson(filePath, ToJson(*particleSystem));
    if (success) {
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
            "ParticlePresetManager: プリセットを保存しました: {}", filePath);
        needUpdateFileList_ = true;

        // 保存したファイルを現在のプリセットとして設定
        currentPresetPath_ = filePath;
        currentPresetName_ = GetFileNameWithoutExtension(std::filesystem::path(filePath).filename().string());
    } else {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
            "ParticlePresetManager: プリセットを保存できませんでした: {}", filePath);
    }

    return success;
}

void ParticlePresetManager::FromJson(IParticleSystem& particleSystem, const json& presetData)
{
    if (!presetData.is_object()) {
        return;
    }

    // ビルボードタイプとブレンドモードの読み込み（範囲外の値は無視する）
    if (const int billboard = JsonManager::SafeGet(presetData, "billboardType", -1);
        billboard >= 0 && billboard <= static_cast<int>(BillboardType::ScreenAligned)) {
        particleSystem.SetBillboardType(static_cast<BillboardType>(billboard));
    }
    if (const int blend = JsonManager::SafeGet(presetData, "blendMode", -1);
        blend >= 0 && blend < static_cast<int>(kBlendModeCount)) {
        particleSystem.SetBlendMode(static_cast<BlendMode>(blend));
    }

    // 各モジュールの読み込み
    LoadModule(presetData, "main", particleSystem.GetMainModule(), true, kMainFields);
    LoadModule(presetData, "emission", particleSystem.GetEmissionModule(), true, kEmissionFields);
    LoadModule(presetData, "shape", particleSystem.GetShapeModule(), true, kShapeFields);
    LoadModule(presetData, "velocity", particleSystem.GetVelocityModule(), true, kVelocityFields);
    LoadModule(presetData, "color", particleSystem.GetColorModule(), true, kColorFields);
    LoadModule(presetData, "force", particleSystem.GetForceModule(), true, kForceFields);
    LoadModule(presetData, "size", particleSystem.GetSizeModule(), true, kSizeFields);
    LoadModule(presetData, "rotation", particleSystem.GetRotationModule(), true, kRotationFields);
    LoadModule(presetData, "noise", particleSystem.GetNoiseModule(), true, kNoiseFields);

    // CollisionModuleの読み込み（床との当たり判定。CPU版だけが持つ）
    // 既定は無効なので、collision セクションが無いプリセットは触らない
    if (CollisionModule* const collision = particleSystem.GetCollisionModule()) {
        LoadModule(presetData, "collision", *collision, false, kCollisionFields);
    }
}

bool ParticlePresetManager::LoadPreset(IParticleSystem* particleSystem, const std::string& filePath)
{
    auto& jsonManager = JsonManager::GetInstance();
    if (!particleSystem || !jsonManager.FileExists(filePath)) {
        Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
            "ParticlePresetManager: プリセットが見つかりません: {}", filePath);
        return false;
    }

    const json presetData = jsonManager.LoadJson(filePath);
    if (presetData.empty()) {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
            "ParticlePresetManager: プリセットを読めませんでした: {}", filePath);
        return false;
    }
    FromJson(*particleSystem, presetData);

    // 現在のプリセット情報を保存
    currentPresetPath_ = filePath;
    currentPresetName_ = GetFileNameWithoutExtension(std::filesystem::path(filePath).filename().string());

    Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Resource,
        "ParticlePresetManager: プリセット（版 {}）を読み込みました: {}",
        JsonManager::SafeGet<std::string>(presetData, "version", "1.0"), filePath);
    return true;
}

std::vector<std::string> ParticlePresetManager::GetPresetList(const std::string& directory)
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
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
            "ParticlePresetManager: プリセットの一覧を作れませんでした: {}", e.what());
    }

    return fileList;
}

void ParticlePresetManager::ShowImGui(IParticleSystem* particleSystem)
{
#ifdef CORE_EDITOR
    ImGui::PushID(this);

    // ── ツールバー行: プリセット名 + 保存/読み込みボタン ──
    if (currentPresetPath_.empty()) {
        ImGui::TextDisabled("プリセット: なし");
    } else {
        ImGui::Text("プリセット:");
        UI::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "%s", currentPresetName_.c_str());
        UI::Tooltip(currentPresetPath_.c_str());
    }

    // 上書き保存（プリセット未読み込み時は無効）
    {
        UI::Scope::DisabledScope ds(currentPresetPath_.empty());
        if (ImGui::Button("上書き保存")) {
            const bool ok = SaveCurrentPreset(particleSystem);
            SetStatus(ok ? "上書き保存しました: " + currentPresetName_
                         : "上書き保存に失敗しました", !ok);
        }
        UI::Tooltip("現在のプリセットファイルに保存します");
    }

    UI::SameLine();
    if (ImGui::Button("別名で保存...")) {
        ImGui::OpenPopup("PresetSaveAsPopup");
    }

    UI::SameLine();
    if (ImGui::Button("読み込む...")) {
        UpdatePresetFileList();
        needUpdateFileList_ = false;
        ImGui::OpenPopup("PresetLoadPopup");
    }

    // ── 別名保存ポップアップ ──
    if (auto popup = UI::Scope::PopupScope("PresetSaveAsPopup")) {
        ImGui::Text("プリセットを保存");
        UI::Separator();

        UI::InputTextWithHint("ファイル名", "例: Fire01", saveFileNameBuffer_, sizeof(saveFileNameBuffer_));
        if (UI::InputText("保存先", directoryPathBuffer_, sizeof(directoryPathBuffer_))) {
            needUpdateFileList_ = true;
        }

        const bool nameEmpty = (saveFileNameBuffer_[0] == '\0');
        {
            UI::Scope::DisabledScope ds(nameEmpty);
            if (ImGui::Button("保存", ImVec2(120, 0))) {
                std::string fileName = saveFileNameBuffer_;
                if (fileName.find(".json") == std::string::npos) {
                    fileName += ".json";
                }

                // ディレクトリが存在しない場合は作成
                JsonManager::GetInstance().CreateJsonDirectory(directoryPathBuffer_);

                const std::string fullPath = BuildPresetPath(fileName);
                const bool ok = SavePreset(particleSystem, fullPath);
                SetStatus(ok ? "保存しました: " + fileName
                             : "保存に失敗しました: " + fileName, !ok);
                ImGui::CloseCurrentPopup();
            }
        }
        UI::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
    }

    // ── 読み込みポップアップ ──
    if (auto popup = UI::Scope::PopupScope("PresetLoadPopup")) {
        ImGui::Text("プリセットを読み込み");
        UI::Separator();

        if (UI::InputText("フォルダ", directoryPathBuffer_, sizeof(directoryPathBuffer_))) {
            needUpdateFileList_ = true;
        }
        UI::SameLine();
        if (ImGui::Button("更新") || needUpdateFileList_) {
            UpdatePresetFileList();
            needUpdateFileList_ = false;
        }

        if (presetFileList_.empty()) {
            UI::Hint("プリセットファイルが見つかりません");
        } else {
            if (auto child = UI::Scope::ChildScope("PresetList", ImVec2(320, 180), ImGuiChildFlags_Border)) {
                for (size_t i = 0; i < presetFileList_.size(); ++i) {
                    const bool isSelected = (selectedPresetIndex_ == static_cast<int>(i));
                    if (ImGui::Selectable(GetFileNameWithoutExtension(presetFileList_[i]).c_str(), isSelected,
                        ImGuiSelectableFlags_AllowDoubleClick)) {
                        selectedPresetIndex_ = static_cast<int>(i);

                        // ダブルクリックで即読み込み
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            const std::string fullPath = BuildPresetPath(presetFileList_[i]);
                            const bool ok = LoadPreset(particleSystem, fullPath);
                            SetStatus(ok ? "読み込みました: " + presetFileList_[i]
                                         : "読み込みに失敗しました: " + presetFileList_[i], !ok);
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
            }
            UI::Hint("ダブルクリックで読み込み");

            const bool hasSelection =
                (selectedPresetIndex_ >= 0 && selectedPresetIndex_ < static_cast<int>(presetFileList_.size()));
            {
                UI::Scope::DisabledScope ds(!hasSelection);
                if (ImGui::Button("読み込む", ImVec2(120, 0)) && hasSelection) {
                    const std::string& fileName = presetFileList_[selectedPresetIndex_];
                    const std::string fullPath = BuildPresetPath(fileName);
                    const bool ok = LoadPreset(particleSystem, fullPath);
                    SetStatus(ok ? "読み込みました: " + fileName
                                 : "読み込みに失敗しました: " + fileName, !ok);
                    ImGui::CloseCurrentPopup();
                }
            }
            UI::SameLine();
            if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
        }
    }

    // ── 操作結果の一時表示 ──
    if (!statusMessage_.empty() && ImGui::GetTime() < statusExpireTime_) {
        const ImVec4 color = statusIsError_
            ? ImVec4(0.95f, 0.4f, 0.35f, 1.0f)
            : ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
        ImGui::TextColored(color, "%s", statusMessage_.c_str());
    }

    ImGui::PopID();
#else
    (void)particleSystem; // 未使用警告を抑制
#endif // CORE_EDITOR
}

void ParticlePresetManager::SetStatus(const std::string& message, bool isError)
{
#ifdef CORE_EDITOR
    statusMessage_ = message;
    statusIsError_ = isError;
    statusExpireTime_ = ImGui::GetTime() + 4.0; // 4秒間表示
#else
    (void)message; (void)isError;
#endif
}

std::string ParticlePresetManager::BuildPresetPath(const std::string& fileName) const
{
    std::string dir = directoryPathBuffer_;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir += '/';
    }
    return dir + fileName;
}

void ParticlePresetManager::UpdatePresetFileList()
{
    presetFileList_ = GetPresetList(directoryPathBuffer_);
    selectedPresetIndex_ = -1;
}

std::string ParticlePresetManager::GetFileNameWithoutExtension(const std::string& filename)
{
    size_t lastDot = filename.find_last_of('.');
    if (lastDot != std::string::npos) {
        return filename.substr(0, lastDot);
    }
    return filename;
}

bool ParticlePresetManager::SaveCurrentPreset(IParticleSystem* particleSystem)
{
    if (currentPresetPath_.empty()) {
        return false;
    }

    // 現在読み込まれているプリセットファイルに上書き保存
    return SavePreset(particleSystem, currentPresetPath_);
}
}

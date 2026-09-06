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
#include "ParticleSystem.h"   // 床との当たり判定は CPU 版だけが持つ
#include <filesystem>
#include <iostream>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImguiManager.h"
#endif

// Windows.hのマクロ干渉を回避
#ifdef CreateDirectory
#undef CreateDirectory
#endif

// TODO: MainModule対応のため、Save/Load機能は一時的に無効化
// 後でMainModuleからデータを取得・設定するように修正する必要があります


namespace CoreEngine
{
bool ParticlePresetManager::SavePreset(IParticleSystem* particleSystem, const std::string& filePath)
{
    json presetData;

    // エミッター位置の保存
    presetData["emitterPosition"] = JsonManager::Vector3ToJson(particleSystem->GetEmitterPosition());

    // ビルボードタイプとブレンドモードの保存
    presetData["billboardType"] = static_cast<int>(particleSystem->GetBillboardType());
    presetData["blendMode"] = static_cast<int>(particleSystem->GetBlendMode());

    // MainModuleの保存
    auto& mainModule = particleSystem->GetMainModule();
    auto mainData = mainModule.GetMainData();
    json mainJson;
    mainJson["duration"] = mainData.duration;
    mainJson["looping"] = mainData.looping;
    mainJson["playOnAwake"] = mainData.playOnAwake;
    mainJson["maxParticles"] = mainData.maxParticles;
    mainJson["simulationSpace"] = static_cast<int>(mainData.simulationSpace);
    mainJson["startLifetime"] = mainData.startLifetime;
    mainJson["startLifetimeRandomness"] = mainData.startLifetimeRandomness;
    mainJson["startSpeed"] = mainData.startSpeed;
    mainJson["startSpeedRandomness"] = mainData.startSpeedRandomness;
    mainJson["startSize"] = JsonManager::Vector3ToJson(mainData.startSize);
    mainJson["startSizeRandomness"] = mainData.startSizeRandomness;
    mainJson["startRotation"] = JsonManager::Vector3ToJson(mainData.startRotation);
    mainJson["startRotationRandomness"] = mainData.startRotationRandomness;
    mainJson["startColor"] = JsonManager::Vector4ToJson(mainData.startColor);
    mainJson["startColorRandomness"] = mainData.startColorRandomness;
    mainJson["gravityModifier"] = mainData.gravityModifier;
    mainJson["enabled"] = mainModule.IsEnabled();
    presetData["main"] = mainJson;

    // EmissionModuleの保存
    auto& emissionModule = particleSystem->GetEmissionModule();
    auto emissionData = emissionModule.GetEmissionData();
    json emissionJson;
    emissionJson["rateOverTime"] = emissionData.rateOverTime;
    emissionJson["burstCount"] = emissionData.burstCount;
    emissionJson["burstTime"] = emissionData.burstTime;
    emissionJson["enabled"] = emissionModule.IsEnabled();
    presetData["emission"] = emissionJson;

    // ShapeModuleの保存
    auto& shapeModule = particleSystem->GetShapeModule();
    auto shapeData = shapeModule.GetShapeData();
    json shapeJson;
    shapeJson["shapeType"] = static_cast<int>(shapeData.shapeType);
    shapeJson["scale"] = JsonManager::Vector3ToJson(shapeData.scale);
    shapeJson["radius"] = shapeData.radius;
    shapeJson["innerRadius"] = shapeData.innerRadius;
    shapeJson["height"] = shapeData.height;
    shapeJson["angle"] = shapeData.angle;
    shapeJson["randomPositionRange"] = shapeData.randomPositionRange;
    shapeJson["emitFromSurface"] = shapeData.emitFromSurface;
    shapeJson["emissionDirection"] = JsonManager::Vector3ToJson(shapeData.emissionDirection);
    shapeJson["circlePlane"] = static_cast<int>(shapeData.circlePlane);
    shapeJson["enabled"] = shapeModule.IsEnabled();
    presetData["shape"] = shapeJson;

    // VelocityModuleの保存
    auto& velocityModule = particleSystem->GetVelocityModule();
    auto velocityData = velocityModule.GetVelocityData();
    json velocityJson;
    velocityJson["startSpeed"] = JsonManager::Vector3ToJson(velocityData.startSpeed);
    velocityJson["randomSpeedRange"] = JsonManager::Vector3ToJson(velocityData.randomSpeedRange);
    velocityJson["useRandomDirection"] = velocityData.useRandomDirection;
    velocityJson["enabled"] = velocityModule.IsEnabled();
    presetData["velocity"] = velocityJson;

    // ColorModuleの保存
    auto& colorModule = particleSystem->GetColorModule();
    auto colorData = colorModule.GetColorData();
    json colorJson;
    colorJson["endColor"] = JsonManager::Vector4ToJson(colorData.endColor);
    colorJson["useGradient"] = colorData.useGradient;
    colorJson["startRatio"] = colorData.startRatio;
    colorJson["enabled"] = colorModule.IsEnabled();
    presetData["color"] = colorJson;

    // ForceModuleの保存
    auto& forceModule = particleSystem->GetForceModule();
    auto forceData = forceModule.GetForceData();
    json forceJson;
    forceJson["gravity"] = JsonManager::Vector3ToJson(forceData.gravity);
    forceJson["wind"] = JsonManager::Vector3ToJson(forceData.wind);
    forceJson["drag"] = forceData.drag;
    forceJson["acceleration"] = JsonManager::Vector3ToJson(forceData.acceleration);
    forceJson["areaMin"] = JsonManager::Vector3ToJson(forceData.area.min);
    forceJson["areaMax"] = JsonManager::Vector3ToJson(forceData.area.max);
    forceJson["useAccelerationField"] = forceData.useAccelerationField;
    forceJson["enabled"] = forceModule.IsEnabled();
    presetData["force"] = forceJson;

    // SizeModuleの保存
    auto& sizeModule = particleSystem->GetSizeModule();
    auto sizeData = sizeModule.GetSizeData();
    json sizeJson;
    sizeJson["endSize"] = sizeData.endSize;
    sizeJson["sizeOverLifetime"] = sizeData.sizeOverLifetime;
    sizeJson["endSize3D"] = JsonManager::Vector3ToJson(sizeData.endSize3D);
    sizeJson["use3DSize"] = sizeData.use3DSize;
    sizeJson["sizeCurve"] = static_cast<int>(sizeData.sizeCurve);
    sizeJson["minSize"] = sizeData.minSize;
    sizeJson["maxSize"] = sizeData.maxSize;
    sizeJson["uniformScaling"] = sizeData.uniformScaling;
    sizeJson["enabled"] = sizeModule.IsEnabled();
    presetData["size"] = sizeJson;

    // RotationModuleの保存
    auto& rotationModule = particleSystem->GetRotationModule();
    auto rotationData = rotationModule.GetRotationData();
    json rotationJson;
    rotationJson["rotationSpeed"] = JsonManager::Vector3ToJson(rotationData.rotationSpeed);
    rotationJson["rotationSpeedRandomness"] = JsonManager::Vector3ToJson(rotationData.rotationSpeedRandomness);
    rotationJson["use2DRotation"] = rotationData.use2DRotation;
    rotationJson["rotation2DSpeed"] = rotationData.rotation2DSpeed;
    rotationJson["rotation2DSpeedRandomness"] = rotationData.rotation2DSpeedRandomness;
    rotationJson["rotationDirection"] = static_cast<int>(rotationData.rotationDirection);
    rotationJson["rotationOverLifetime"] = rotationData.rotationOverLifetime;
    rotationJson["startRotationSpeedMultiplier"] = rotationData.startRotationSpeedMultiplier;
    rotationJson["endRotationSpeedMultiplier"] = rotationData.endRotationSpeedMultiplier;
    rotationJson["limitRotationRange"] = rotationData.limitRotationRange;
    rotationJson["minRotation"] = JsonManager::Vector3ToJson(rotationData.minRotation);
    rotationJson["maxRotation"] = JsonManager::Vector3ToJson(rotationData.maxRotation);
    rotationJson["alignToVelocity"] = rotationData.alignToVelocity;
    rotationJson["velocityAlignmentStrength"] = rotationData.velocityAlignmentStrength;
    rotationJson["enabled"] = rotationModule.IsEnabled();
    presetData["rotation"] = rotationJson;

    // NoiseModuleの保存
    auto& noiseModule = particleSystem->GetNoiseModule();
    auto noiseData = noiseModule.GetNoiseData();
    json noiseJson;
    noiseJson["strength"] = noiseData.strength;
    noiseJson["frequency"] = noiseData.frequency;
    noiseJson["scrollSpeed"] = noiseData.scrollSpeed;
    noiseJson["damping"] = noiseData.damping;
    noiseJson["positionAmount"] = JsonManager::Vector3ToJson(noiseData.positionAmount);
    noiseJson["enabled"] = noiseModule.IsEnabled();
    presetData["noise"] = noiseJson;

    // CollisionModuleの保存（床との当たり判定。CPU版だけが持つ）
    if (auto* cpuSystem = dynamic_cast<ParticleSystem*>(particleSystem)) {
        auto& collisionModule = cpuSystem->GetCollisionModule();
        auto collisionData = collisionModule.GetCollisionData();
        json collisionJson;
        collisionJson["planeHeight"] = collisionData.planeHeight;
        collisionJson["contactOffset"] = collisionData.contactOffset;
        collisionJson["bounce"] = collisionData.bounce;
        collisionJson["friction"] = collisionData.friction;
        collisionJson["restSpeed"] = collisionData.restSpeed;
        collisionJson["roll"] = collisionData.roll;
        collisionJson["rollRadius"] = collisionData.rollRadius;
        collisionJson["useParticleScale"] = collisionData.useParticleScale;
        collisionJson["enabled"] = collisionModule.IsEnabled();
        presetData["collision"] = collisionJson;
    }

    // メタデータ
    presetData["version"] = "2.1";  // モジュールの有効フラグ・床との当たり判定に対応

    // ファイルに保存
    bool success = JsonManager::GetInstance().SaveJson(filePath, presetData);
    if (success) {
        std::cout << "Preset saved (v2.1): " << filePath << std::endl;
        needUpdateFileList_ = true;

        // 保存したファイルを現在のプリセットとして設定
        currentPresetPath_ = filePath;
        currentPresetName_ = GetFileNameWithoutExtension(std::filesystem::path(filePath).filename().string());
    } else {
        std::cerr << "Failed to save preset: " << filePath << std::endl;
    }

    return success;
}

bool ParticlePresetManager::LoadPreset(IParticleSystem* particleSystem, const std::string& filePath)
{
    // ファイルが存在するかチェック
    if (!JsonManager::GetInstance().FileExists(filePath)) {
        std::cerr << "Preset file not found: " << filePath << std::endl;
        return false;
    }

    // ファイルを読み込み
    json presetData = JsonManager::GetInstance().LoadJson(filePath);
    if (presetData.empty()) {
        std::cerr << "Failed to load preset: " << filePath << std::endl;
        return false;
    }

    // バージョンチェック
    std::string version = "1.0";
if (presetData.contains("version")) {
        version = presetData["version"].get<std::string>();
    }
    std::cout << "Loading preset version: " << version << std::endl;

    // エミッター位置の読み込み
    if (presetData.contains("emitterPosition")) {
        Vector3 position = JsonManager::JsonToVector3(presetData["emitterPosition"]);
        particleSystem->SetEmitterPosition(position);
    }

    // ビルボードタイプとブレンドモードの読み込み
    if (presetData.contains("billboardType")) {
        particleSystem->SetBillboardType(static_cast<BillboardType>(presetData["billboardType"].get<int>()));
    }
    if (presetData.contains("blendMode")) {
        particleSystem->SetBlendMode(static_cast<BlendMode>(presetData["blendMode"].get<int>()));
    }

    // MainModuleの読み込み
    if (presetData.contains("main")) {
        auto mainJson = presetData["main"];
        auto& mainModule = particleSystem->GetMainModule();
        auto& mainData = mainModule.GetMainData();

        mainData.duration = JsonManager::SafeGet(mainJson, "duration", 5.0f);
        mainData.looping = JsonManager::SafeGet(mainJson, "looping", true);
        mainData.playOnAwake = JsonManager::SafeGet(mainJson, "playOnAwake", true);
        mainData.maxParticles = JsonManager::SafeGet(mainJson, "maxParticles", 1000u);
        mainData.simulationSpace = static_cast<MainModule::SimulationSpace>(
            JsonManager::SafeGet(mainJson, "simulationSpace", 0));

        mainData.startLifetime = JsonManager::SafeGet(mainJson, "startLifetime", 1.0f);
        mainData.startLifetimeRandomness = JsonManager::SafeGet(mainJson, "startLifetimeRandomness", 0.0f);

        mainData.startSpeed = JsonManager::SafeGet(mainJson, "startSpeed", 5.0f);
        mainData.startSpeedRandomness = JsonManager::SafeGet(mainJson, "startSpeedRandomness", 0.0f);

        mainData.startSize = JsonManager::SafeGetVector3(mainJson, "startSize", { 1.0f, 1.0f, 1.0f });
        mainData.startSizeRandomness = JsonManager::SafeGet(mainJson, "startSizeRandomness", 0.0f);

        mainData.startRotation = JsonManager::SafeGetVector3(mainJson, "startRotation", { 0.0f, 0.0f, 0.0f });
        mainData.startRotationRandomness = JsonManager::SafeGet(mainJson, "startRotationRandomness", 0.0f);

        mainData.startColor = JsonManager::SafeGetVector4(mainJson, "startColor", { 1.0f, 1.0f, 1.0f, 1.0f });
        mainData.startColorRandomness = JsonManager::SafeGet(mainJson, "startColorRandomness", 0.0f);

        mainData.gravityModifier = JsonManager::SafeGet(mainJson, "gravityModifier", 0.0f);
        mainModule.SetEnabled(JsonManager::SafeGet(mainJson, "enabled", true));
    }

    // EmissionModuleの読み込み
    if (presetData.contains("emission")) {
        auto emissionJson = presetData["emission"];
        EmissionModule::EmissionData emissionData;
        emissionData.rateOverTime = JsonManager::SafeGet(emissionJson, "rateOverTime", 10u);
        emissionData.burstCount = JsonManager::SafeGet(emissionJson, "burstCount", 0u);
        emissionData.burstTime = JsonManager::SafeGet(emissionJson, "burstTime", 0.0f);
        particleSystem->GetEmissionModule().SetEmissionData(emissionData);
        particleSystem->GetEmissionModule().SetEnabled(JsonManager::SafeGet(emissionJson, "enabled", true));
    }

    // ShapeModuleの読み込み
    if (presetData.contains("shape")) {
        auto shapeJson = presetData["shape"];
        ShapeModule::ShapeData shapeData;
        shapeData.shapeType = static_cast<ShapeModule::ShapeType>(
            JsonManager::SafeGet(shapeJson, "shapeType", 0));
        shapeData.scale = JsonManager::SafeGetVector3(shapeJson, "scale", { 1.0f, 1.0f, 1.0f });
        shapeData.radius = JsonManager::SafeGet(shapeJson, "radius", 1.0f);
        shapeData.innerRadius = JsonManager::SafeGet(shapeJson, "innerRadius", 0.5f);
        shapeData.height = JsonManager::SafeGet(shapeJson, "height", 2.0f);
        shapeData.angle = JsonManager::SafeGet(shapeJson, "angle", 25.0f);
        shapeData.randomPositionRange = JsonManager::SafeGet(shapeJson, "randomPositionRange", 0.0f);
        shapeData.emitFromSurface = JsonManager::SafeGet(shapeJson, "emitFromSurface", false);
        shapeData.emissionDirection = JsonManager::SafeGetVector3(shapeJson, "emissionDirection", { 0.0f, 1.0f, 0.0f });
        shapeData.circlePlane = static_cast<ShapeModule::CirclePlane>(
            JsonManager::SafeGet(shapeJson, "circlePlane", 0));
        particleSystem->GetShapeModule().SetShapeData(shapeData);
        particleSystem->GetShapeModule().SetEnabled(JsonManager::SafeGet(shapeJson, "enabled", true));
    }

    // VelocityModuleの読み込み
    if (presetData.contains("velocity")) {
        auto velocityJson = presetData["velocity"];
        VelocityModule::VelocityData velocityData;
        velocityData.startSpeed = JsonManager::SafeGetVector3(velocityJson, "startSpeed", { 0.0f, 1.0f, 0.0f });
        velocityData.randomSpeedRange = JsonManager::SafeGetVector3(velocityJson, "randomSpeedRange", { 1.0f, 1.0f, 1.0f });
        velocityData.useRandomDirection = JsonManager::SafeGet(velocityJson, "useRandomDirection", true);
        particleSystem->GetVelocityModule().SetVelocityData(velocityData);
        particleSystem->GetVelocityModule().SetEnabled(JsonManager::SafeGet(velocityJson, "enabled", true));
    }

    // ColorModuleの読み込み
    if (presetData.contains("color")) {
        auto colorJson = presetData["color"];
        ColorModule::ColorOverLifetime colorData;
        colorData.endColor = JsonManager::SafeGetVector4(colorJson, "endColor", { 1.0f, 1.0f, 1.0f, 0.0f });
        colorData.useGradient = JsonManager::SafeGet(colorJson, "useGradient", true);
        colorData.startRatio = JsonManager::SafeGet(colorJson, "startRatio", 0.0f);
        particleSystem->GetColorModule().SetColorData(colorData);
        particleSystem->GetColorModule().SetEnabled(JsonManager::SafeGet(colorJson, "enabled", true));
    }

    // ForceModuleの読み込み
    if (presetData.contains("force")) {
        auto forceJson = presetData["force"];
        ForceModule::ForceData forceData;
        forceData.gravity = JsonManager::SafeGetVector3(forceJson, "gravity", { 0.0f, -9.8f, 0.0f });
        forceData.wind = JsonManager::SafeGetVector3(forceJson, "wind", { 0.0f, 0.0f, 0.0f });
        forceData.drag = JsonManager::SafeGet(forceJson, "drag", 0.0f);
        forceData.acceleration = JsonManager::SafeGetVector3(forceJson, "acceleration", { 0.0f, 0.0f, 0.0f });
        forceData.area.min = JsonManager::SafeGetVector3(forceJson, "areaMin", { -1.0f, -1.0f, -1.0f });
        forceData.area.max = JsonManager::SafeGetVector3(forceJson, "areaMax", { 1.0f, 1.0f, 1.0f });
        forceData.useAccelerationField = JsonManager::SafeGet(forceJson, "useAccelerationField", false);
        particleSystem->GetForceModule().SetForceData(forceData);
        particleSystem->GetForceModule().SetEnabled(JsonManager::SafeGet(forceJson, "enabled", true));
    }

    // SizeModuleの読み込み
    if (presetData.contains("size")) {
        auto sizeJson = presetData["size"];
        SizeModule::SizeData sizeData;
        sizeData.endSize = JsonManager::SafeGet(sizeJson, "endSize", 0.0f);
        sizeData.sizeOverLifetime = JsonManager::SafeGet(sizeJson, "sizeOverLifetime", true);
        sizeData.endSize3D = JsonManager::SafeGetVector3(sizeJson, "endSize3D", { 0.0f, 0.0f, 0.0f });
        sizeData.use3DSize = JsonManager::SafeGet(sizeJson, "use3DSize", false);
        sizeData.sizeCurve = static_cast<SizeModule::SizeData::SizeCurve>(
            JsonManager::SafeGet(sizeJson, "sizeCurve", 0));
        sizeData.minSize = JsonManager::SafeGet(sizeJson, "minSize", 0.01f);
        sizeData.maxSize = JsonManager::SafeGet(sizeJson, "maxSize", 10.0f);
        sizeData.uniformScaling = JsonManager::SafeGet(sizeJson, "uniformScaling", true);
        particleSystem->GetSizeModule().SetSizeData(sizeData);
        particleSystem->GetSizeModule().SetEnabled(JsonManager::SafeGet(sizeJson, "enabled", true));
    }

    // RotationModuleの読み込み
    if (presetData.contains("rotation")) {
        auto rotationJson = presetData["rotation"];
        RotationModule::RotationData rotationData;
        rotationData.rotationSpeed = JsonManager::SafeGetVector3(rotationJson, "rotationSpeed", { 0.0f, 0.0f, 0.0f });
        rotationData.rotationSpeedRandomness = JsonManager::SafeGetVector3(rotationJson, "rotationSpeedRandomness", { 0.0f, 0.0f, 0.0f });
        rotationData.use2DRotation = JsonManager::SafeGet(rotationJson, "use2DRotation", true);
        rotationData.rotation2DSpeed = JsonManager::SafeGet(rotationJson, "rotation2DSpeed", 0.0f);
        rotationData.rotation2DSpeedRandomness = JsonManager::SafeGet(rotationJson, "rotation2DSpeedRandomness", 0.0f);
        rotationData.rotationDirection = static_cast<RotationModule::RotationData::RotationDirection>(
            JsonManager::SafeGet(rotationJson, "rotationDirection", 2));
        rotationData.rotationOverLifetime = JsonManager::SafeGet(rotationJson, "rotationOverLifetime", false);
        rotationData.startRotationSpeedMultiplier = JsonManager::SafeGet(rotationJson, "startRotationSpeedMultiplier", 1.0f);
        rotationData.endRotationSpeedMultiplier = JsonManager::SafeGet(rotationJson, "endRotationSpeedMultiplier", 1.0f);
        rotationData.limitRotationRange = JsonManager::SafeGet(rotationJson, "limitRotationRange", false);
        rotationData.minRotation = JsonManager::SafeGetVector3(rotationJson, "minRotation", { -180.0f, -180.0f, -180.0f });
        rotationData.maxRotation = JsonManager::SafeGetVector3(rotationJson, "maxRotation", { 180.0f, 180.0f, 180.0f });
        rotationData.alignToVelocity = JsonManager::SafeGet(rotationJson, "alignToVelocity", false);
        rotationData.velocityAlignmentStrength = JsonManager::SafeGet(rotationJson, "velocityAlignmentStrength", 1.0f);
        particleSystem->GetRotationModule().SetRotationData(rotationData);
        particleSystem->GetRotationModule().SetEnabled(JsonManager::SafeGet(rotationJson, "enabled", true));
    }

    // NoiseModuleの読み込み
    if (presetData.contains("noise")) {
        auto noiseJson = presetData["noise"];
        NoiseModule::NoiseData noiseData;
        noiseData.strength = JsonManager::SafeGet(noiseJson, "strength", 1.0f);
        noiseData.frequency = JsonManager::SafeGet(noiseJson, "frequency", 1.0f);
        noiseData.scrollSpeed = JsonManager::SafeGet(noiseJson, "scrollSpeed", 0.0f);
        noiseData.damping = JsonManager::SafeGet(noiseJson, "damping", true);
        noiseData.positionAmount = JsonManager::SafeGetVector3(noiseJson, "positionAmount", { 1.0f, 1.0f, 1.0f });
        particleSystem->GetNoiseModule().SetNoiseData(noiseData);
        particleSystem->GetNoiseModule().SetEnabled(JsonManager::SafeGet(noiseJson, "enabled", true));
    }

    // CollisionModuleの読み込み（床との当たり判定。CPU版だけが持つ）
    if (auto* cpuSystem = dynamic_cast<ParticleSystem*>(particleSystem)) {
        if (presetData.contains("collision")) {
            auto collisionJson = presetData["collision"];
            CollisionModule::CollisionData collisionData;
            collisionData.planeHeight = JsonManager::SafeGet(collisionJson, "planeHeight", 0.0f);
            collisionData.contactOffset = JsonManager::SafeGet(collisionJson, "contactOffset", 0.0f);
            collisionData.bounce = JsonManager::SafeGet(collisionJson, "bounce", 0.3f);
            collisionData.friction = JsonManager::SafeGet(collisionJson, "friction", 3.0f);
            collisionData.restSpeed = JsonManager::SafeGet(collisionJson, "restSpeed", 0.6f);
            collisionData.roll = JsonManager::SafeGet(collisionJson, "roll", true);
            collisionData.rollRadius = JsonManager::SafeGet(collisionJson, "rollRadius", 0.2f);
            collisionData.useParticleScale = JsonManager::SafeGet(collisionJson, "useParticleScale", true);
            cpuSystem->GetCollisionModule().SetCollisionData(collisionData);
            // 既定は無効なので、collision セクションが無いプリセットは触らない
            cpuSystem->GetCollisionModule().SetEnabled(
                JsonManager::SafeGet(collisionJson, "enabled", false));
        }
    }

    // 現在のプリセット情報を保存
    currentPresetPath_ = filePath;
    currentPresetName_ = GetFileNameWithoutExtension(std::filesystem::path(filePath).filename().string());

    std::cout << "Preset loaded (v" << version << "): " << filePath << std::endl;
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
        std::cerr << "Error listing preset files: " << e.what() << std::endl;
    }

    return fileList;
}

void ParticlePresetManager::ShowImGui(IParticleSystem* particleSystem)
{
#ifdef USE_IMGUI
    ImGui::PushID(this);

    // キーボードショートカット: Ctrl+S で上書き保存（ウィンドウがフォーカスされている時のみ）
    if (!currentPresetPath_.empty() &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        const bool ok = SaveCurrentPreset(particleSystem);
        SetStatus(ok ? "上書き保存しました: " + currentPresetName_
                     : "上書き保存に失敗しました", !ok);
    }

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
        UI::Tooltip("現在のプリセットファイルに保存します（Ctrl+S）");
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
#endif // USE_IMGUI
}

void ParticlePresetManager::SetStatus(const std::string& message, bool isError)
{
#ifdef USE_IMGUI
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

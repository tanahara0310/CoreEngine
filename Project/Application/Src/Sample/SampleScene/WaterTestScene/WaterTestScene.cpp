#include "pch.h"
#include "WaterTestScene.h"

#include "Graphics/IBL/IBLSystem.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Camera/CameraManager.h"
#include "Scene/SceneManager.h"
#include "Sample/TestGameObject/SkyBox/SkyBoxObject.h"
#include "Sample/TestGameObject/Primitive/WaterPlaneObject.h"
#include "Sample/TestGameObject/Model/ModelObject.h"
#include "Utility/Random/RandomGenerator.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

using namespace CoreEngine;

namespace {
constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;

Vector2 NormalizeDirection(const Vector2& direction) {
    const float length = std::sqrtf(direction.x * direction.x + direction.y * direction.y);
    if (length <= 1.0e-5f) {
        return { 1.0f, 0.0f };
    }
    return { direction.x / length, direction.y / length };
}

Vector2 RotateDirection(const Vector2& direction, float radians) {
    const float c = std::cosf(radians);
    const float s = std::sinf(radians);
    return NormalizeDirection({
        direction.x * c - direction.y * s,
        direction.x * s + direction.y * c,
    });
}

uint32_t ClampWaveCountToRange(int count) {
    return static_cast<uint32_t>(std::clamp(count, 1, static_cast<int>(kMaxWaterWaveCount)));
}
}

#ifdef USE_IMGUI
void WaterTestScene::RestoreRecommendedWaveCount(WaterPresetType preset) {
    const WaterPresetData& p = GetWaterPresetData(preset);
    imguiActiveWaveCount_ = static_cast<int>(p.recommendedWaveCount);
    waterPlane_->SetActiveWaveCount(p.recommendedWaveCount);
}

void WaterTestScene::RegenerateLayeredWaves(WaterPresetType preset, uint32_t activeWaveCount) {
    if (!waterPlane_) {
        return;
    }

    const WaterPresetData& p = GetWaterPresetData(preset);
    auto& rng = RandomGenerator::GetInstance();

    uint32_t layerCounts[kWaterWaveLayerCount] = {};
    uint32_t assignedCount = 0;
    for (uint32_t layerIndex = 0; layerIndex < kWaterWaveLayerCount; ++layerIndex) {
        layerCounts[layerIndex] = std::min(p.layers[layerIndex].count, activeWaveCount - assignedCount);
        assignedCount += layerCounts[layerIndex];
        if (assignedCount >= activeWaveCount) {
            break;
        }
    }

    uint32_t fillLayer = 0;
    while (assignedCount < activeWaveCount) {
        ++layerCounts[fillLayer % kWaterWaveLayerCount];
        ++assignedCount;
        ++fillLayer;
    }

    const Vector2 dominantDirection = NormalizeDirection(p.dominantDirection);
    uint32_t waveIndex = 0;
    for (uint32_t layerIndex = 0; layerIndex < kWaterWaveLayerCount && waveIndex < activeWaveCount; ++layerIndex) {
        const WaterWaveLayerConfig& layer = p.layers[layerIndex];
        const uint32_t countInLayer = layerCounts[layerIndex];
        for (uint32_t layerWaveIndex = 0; layerWaveIndex < countInLayer && waveIndex < activeWaveCount; ++layerWaveIndex, ++waveIndex) {
            const float baseAngle = (countInLayer > 1)
                ? (-0.5f + static_cast<float>(layerWaveIndex) / static_cast<float>(countInLayer - 1)) * layer.directionSpreadRadians
                : 0.0f;
            const float jitter = rng.GetFloat(-p.directionJitterRadians, p.directionJitterRadians);
            const Vector2 direction = RotateDirection(dominantDirection, baseAngle + jitter);

            WaveParams wave{};
            wave.direction = direction;
            wave.amplitude = rng.GetFloat(layer.amplitudeMin, layer.amplitudeMax);
            wave.wavelength = rng.GetFloat(layer.wavelengthMin, layer.wavelengthMax);
            wave.speed = rng.GetFloat(layer.speedMin, layer.speedMax);
            wave.steepness = rng.GetFloat(layer.steepnessMin, layer.steepnessMax);
            wave.phaseOffset = rng.GetFloat(0.0f, kTwoPi);
            waterPlane_->SetWave(waveIndex, wave);
        }
    }

    for (; waveIndex < kMaxWaterWaveCount; ++waveIndex) {
        WaveParams wave{};
        wave.direction = dominantDirection;
        waterPlane_->SetWave(waveIndex, wave);
    }

    waterPlane_->SetActiveWaveCount(activeWaveCount);
}
#endif

void WaterTestScene::OnInitialize() {
    SetSceneName("WaterTestScene");

    // ===== IBL セットアップ =====
    auto iblSystem = engine_->GetComponent<IBLSystem>();
    if (!iblSystem) {
        return;
    }

    auto& textureManager = TextureManager::GetInstance();
    auto environmentMapTexture = textureManager.Load("kloppenheim_06_puresky_4k.hdr");

    IBLSystem::SetupParams iblParams;
    iblParams.environmentMap = environmentMapTexture.texture.Get();
    iblParams.environmentMapSRV = environmentMapTexture.gpuHandle;
    iblParams.environmentKey = "kloppenheim_06_puresky_4k.hdr";
    iblParams.irradianceSize = 128;
    iblParams.prefilteredSize = 256;
    iblParams.brdfLUTSize = 512;
    iblSystem->Setup(iblParams);

    // ===== SkyBox =====
    auto skyBox = CreateObject<SkyBoxObject>();
    skyBox->SetTexture(environmentMapTexture);
    skyBox->SetActive(true);

    // ===== 水面グリッドメッシュ =====
    // サイズ 100 × 100、64×64 分割
    // 分割数が多いほど後のステップ（Gerstner Wave）で波の表現が細かくなる
    waterPlane_ = CreateObject<WaterPlaneObject>(100.0f, 64);
    waterPlane_->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
    waterPlane_->GetTransform().scale = { 1.0f, 1.0f, 1.0f };

    // GBuffer パスをバイパスし、水面専用 VS（Gerstner Wave）が適用される
    // Forward パスで描画させるためアルファブレンドに設定する
    waterPlane_->SetBlendMode(BlendMode::kBlendModeNormal);

    if (auto* mat = waterPlane_->GetModel()->GetMaterial()) {
        // 湖らしい深い青緑をベースカラーとして設定する
        // テクスチャが無くてもアルベドカラーで水の色感を出す
        mat->SetColor({ 0.04f, 0.18f, 0.28f, 0.85f }); // 深い青緑
        mat->SetMetallic(0.0f);
        mat->SetRoughness(0.04f);  // 鏡面に近い（0.0 に近いほど鏡面反射が強くなる）
        mat->SetLightingEnabled(true);
        mat->SetIBLEnabled(true);
        mat->SetNormalMapEnabled(false);
        mat->SetMetallicMapEnabled(false);
        mat->SetRoughnessMapEnabled(false);
        mat->SetAOMapEnabled(false);
    }

    // UV スクロール速度とタイリングを設定
    // scrollSpeed: U方向に 0.03 UV/秒、V方向に 0.01 UV/秒 でゆっくり流れる
    waterPlane_->SetScrollSpeed({ 0.03f, 0.01f });
    waterPlane_->SetUVTiling({ 4.0f, 4.0f });
    waterPlane_->SetActive(true);

    // ===== 地面モデル =====
    groundObject_ = CreateObject<ModelObject>("pool.gltf");
    groundObject_->GetTransform().translate = { 0.0f, -0.1f, 0.0f };
    groundObject_->GetTransform().scale = { 1.0f,  1.0f, 1.0f };
    // PBRテクスチャマップを有効化（gltfに埋め込まれたテクスチャを使用）
    groundObject_->SetPBRTextureMapsEnabled(true, true, true, true);
    groundObject_->SetIBLEnabled(true);
    groundObject_->SetActive(true);

#ifdef USE_IMGUI
    ApplyWaterPreset(static_cast<WaterPresetType>(imguiPreset_));

#endif
}

void WaterTestScene::OnUpdate() {
    // フレーム時間を取得して UV スクロールを更新
    auto* frameRate = engine_->GetComponent<FrameRateController>();
    if (waterPlane_ && frameRate) {
        waterPlane_->UpdateUVScroll(frameRate->GetDeltaTime());
    }

#ifdef USE_IMGUI
    DrawWaterImGui();
#endif

    // ImGui 等で変更されたフレーム定数を GPU バッファに書き込む
    if (waterPlane_) {
        waterPlane_->UpdateFrameConstants();
    }
}

void WaterTestScene::Draw() {
    // ReflectionView の実行は Engine 側 RenderGraph に委譲し、ここでは通常描画だけを行う。
    BaseScene::Draw();
}

ReflectionViewRequest WaterTestScene::GetReflectionViewRequest() const
{
    ReflectionViewRequest request{};
    request.isEnabled = (waterPlane_ != nullptr);
    request.planeHeight = waterPlane_ ? waterPlane_->GetTransform().translate.y : 0.0f;
    return request;
}

void WaterTestScene::SetupReflectionView(ICamera* mainCamera, float planeHeight)
{
    reflectionPass_.SetupReflectionCamera(mainCamera, planeHeight);
    if (waterPlane_) {
        waterPlane_->SetClipPlane(reflectionPass_.GetClipPlane(), true);
        waterPlane_->UpdateFrameConstants();
    }
}

void WaterTestScene::RestoreReflectionView(ICamera* mainCamera)
{
    reflectionPass_.RestoreMainCamera(mainCamera);
}

void WaterTestScene::ApplyReflectionViewResult(const ReflectionViewResult& result)
{
    if (!waterPlane_) {
        return;
    }

    // Engine 側で生成した ReflectionColor と共有 Scene 入力を、水面描画用の結果へ変換する。
    const ReflectionViewResult reflectionResult = reflectionPass_.BuildResult(
        result.reflectionSrv,
        result.sceneDepthSrv,
        result.sceneColorSrv);
    waterPlane_->ApplyWaterReflectionResult(reflectionResult);
    waterPlane_->UpdateFrameConstants();
}

void WaterTestScene::Finalize() {
    BaseScene::Finalize();
}

#ifdef USE_IMGUI
void WaterTestScene::ApplyWaterPreset(WaterPresetType preset) {
    if (!waterPlane_) { return; }

    const WaterPresetData& p = GetWaterPresetData(preset);

    // ---- マテリアル ----
    imguiColor_[0] = p.baseColor.x;
    imguiColor_[1] = p.baseColor.y;
    imguiColor_[2] = p.baseColor.z;
    imguiColor_[3] = p.baseColor.w;
    imguiRoughness_ = p.roughness;
    imguiMetallic_  = p.metallic;
    waterPlane_->SetBaseColor(p.baseColor);
    waterPlane_->SetRoughness(p.roughness);
    waterPlane_->SetMetallic(p.metallic);

    // ---- Depth Fade / 水色 ----
    imguiAbsorptionCoeff_ = p.absorptionCoeff;
    imguiShallowColor_[0] = p.shallowColor.x;
    imguiShallowColor_[1] = p.shallowColor.y;
    imguiShallowColor_[2] = p.shallowColor.z;
    imguiDeepColor_[0]    = p.deepColor.x;
    imguiDeepColor_[1]    = p.deepColor.y;
    imguiDeepColor_[2]    = p.deepColor.z;
    waterPlane_->SetDepthFade(p.absorptionCoeff, imguiDepthFadeEnabled_);
    waterPlane_->SetWaterColors(p.shallowColor, p.deepColor);

    // ---- Fresnel ----
    imguiFresnelReflectanceScale_ = p.fresnelReflectanceScale;
    imguiFresnelBaseReflectance_ = p.fresnelBaseReflectance;
    waterPlane_->SetFresnelParameters(p.fresnelReflectanceScale, p.fresnelBaseReflectance);

    // ---- Gerstner Wave ----
    if (imguiAutoRestoreRecommendedWaveCount_ || imguiLockRecommendedWaveCount_) {
        RestoreRecommendedWaveCount(preset);
    }
    if (imguiLockRecommendedWaveCount_) {
        RestoreRecommendedWaveCount(preset);
    }
    RegenerateLayeredWaves(preset, ClampWaveCountToRange(imguiActiveWaveCount_));

    // ---- UV ----
    imguiScrollSpeed_[0] = p.scrollSpeed.x;
    imguiScrollSpeed_[1] = p.scrollSpeed.y;
    imguiUVTiling_[0]    = p.uvTiling.x;
    imguiUVTiling_[1]    = p.uvTiling.y;
    waterPlane_->GetScrollSpeed() = p.scrollSpeed;
    waterPlane_->GetUVTiling()    = p.uvTiling;
}

void WaterTestScene::DrawWaterImGui() {
    if (!waterPlane_) { return; }

    ImGui::SetNextWindowSize(ImVec2(440, 720), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Water Parameters")) {
        ImGui::End();
        return;
    }

    const WaterPresetType currentPreset = static_cast<WaterPresetType>(imguiPreset_);
    const WaterPresetData& preset = GetWaterPresetData(currentPreset);
    const bool reflectionEnabled = (waterPlane_->GetFrameConstants().reflectionEnabled != 0);

    if (ImGui::CollapsingHeader("プリセット / クイック操作", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("現在の水面実装で有効な設定だけをまとめています");
        ImGui::Spacing();

        static const char* kPresetNames[] = {
            "湖（静かな水）",
            "海（波が立つ水）",
            "池・プール",
            "雨水・水たまり",
        };

        int prevPreset = imguiPreset_;
        for (int i = 0; i < 4; ++i) {
            if (ImGui::RadioButton(kPresetNames[i], &imguiPreset_, i)) {
                // ラジオボタンが押された直後にプリセット適用
            }
            if (i < 3) { ImGui::SameLine(); }
        }

        if (imguiPreset_ != prevPreset) {
            ApplyWaterPreset(currentPreset);
        }

        ImGui::Spacing();
        if (ImGui::Button("プリセットを再適用", ImVec2(-1.0f, 0.0f))) {
            ApplyWaterPreset(currentPreset);
        }

        if (ImGui::Checkbox("推奨波本数に固定", &imguiLockRecommendedWaveCount_)) {
            if (imguiLockRecommendedWaveCount_) {
                RestoreRecommendedWaveCount(currentPreset);
                RegenerateLayeredWaves(currentPreset, ClampWaveCountToRange(imguiActiveWaveCount_));
            }
        }
        ImGui::Checkbox("プリセット切替時に推奨本数へ戻す", &imguiAutoRestoreRecommendedWaveCount_);
        ImGui::Checkbox("波本数増加時に自動再生成", &imguiAutoGenerateOnWaveCountIncrease_);
        if (ImGui::Button("レイヤー波を再生成", ImVec2(-1.0f, 0.0f))) {
            RegenerateLayeredWaves(currentPreset, ClampWaveCountToRange(imguiActiveWaveCount_));
        }

        ImGui::Spacing();
        ImGui::SeparatorText("現在の状態");
        ImGui::Text("プリセット推奨波本数: %u", preset.recommendedWaveCount);
        ImGui::Text("反射 RTT: %s", reflectionEnabled ? "有効" : "未接続");
        ImGui::Text("透過ソース: SceneColor");
        ImGui::Text("波本数: %d / %u", imguiActiveWaveCount_, kMaxWaterWaveCount);

        ImGui::SeparatorText("サーフェス入力");
        ImGui::TextDisabled("水面はテクスチャを使わず、手続き波面とマテリアル値のみで生成します");
    }

    if (ImGui::CollapsingHeader("見た目", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("サーフェス");
        if (ImGui::ColorEdit4("ベースカラー", imguiColor_)) {
            waterPlane_->SetBaseColor({ imguiColor_[0], imguiColor_[1], imguiColor_[2], imguiColor_[3] });
        }
        if (ImGui::SliderFloat("粗さ (Roughness)", &imguiRoughness_, 0.0f, 1.0f)) {
            waterPlane_->SetRoughness(imguiRoughness_);
        }
        if (ImGui::SliderFloat("金属度 (Metallic)", &imguiMetallic_, 0.0f, 1.0f)) {
            waterPlane_->SetMetallic(imguiMetallic_);
        }
        if (ImGui::Checkbox("IBL 有効", &imguiIBLEnabled_)) {
            waterPlane_->SetIBLEnabled(imguiIBLEnabled_);
        }

        ImGui::Spacing();
        ImGui::SeparatorText("反射 / 透過");
        ImGui::TextDisabled("透過は SceneColor、反射は planar reflection RTT を使用します");
        bool fresnelChanged = false;
        fresnelChanged |= ImGui::SliderFloat("反射率スケール", &imguiFresnelReflectanceScale_, 0.0f, 2.0f, "%.3f");
        fresnelChanged |= ImGui::SliderFloat("F0（正面反射率）", &imguiFresnelBaseReflectance_, 0.0f, 0.10f, "%.4f");
        if (fresnelChanged) {
            waterPlane_->SetFresnelParameters(imguiFresnelReflectanceScale_, imguiFresnelBaseReflectance_);
        }

        ImGui::Spacing();
        ImGui::SeparatorText("水の色と吸収");
        ImGui::TextDisabled("浅瀬は背景が見えやすく、深場は吸収で水色が強くなります");
        bool depthChanged = false;
        depthChanged |= ImGui::Checkbox("Depth Fade 有効", &imguiDepthFadeEnabled_);
        depthChanged |= ImGui::SliderFloat("吸収係数", &imguiAbsorptionCoeff_, 0.0f, 8.0f, "%.3f");
        if (depthChanged) {
            waterPlane_->SetDepthFade(imguiAbsorptionCoeff_, imguiDepthFadeEnabled_);
        }

        bool colorChanged = false;
        colorChanged |= ImGui::ColorEdit3("浅瀬の色", imguiShallowColor_);
        colorChanged |= ImGui::ColorEdit3("深場の色", imguiDeepColor_);
        if (colorChanged) {
            waterPlane_->SetWaterColors(
                { imguiShallowColor_[0], imguiShallowColor_[1], imguiShallowColor_[2] },
                { imguiDeepColor_[0],    imguiDeepColor_[1],    imguiDeepColor_[2] });
        }

        if (ImGui::TreeNode("Depth Fade デバッグ")) {
            bool debugChanged = false;
            debugChanged |= ImGui::Checkbox("デバッグ表示", &imguiDepthFadeDebugEnabled_);
            debugChanged |= ImGui::SliderFloat("表示倍率", &imguiDepthFadeDebugScale_, 0.1f, 8.0f, "%.2f");
            static const char* kDebugViewNames[] = {
                "なし",
                "Raw Depth",
                "Linear Depth",
                "Depth Delta",
                "Screen UV",
                "Scene Color",
                "Reflection",
                "Fresnel",
            };
            debugChanged |= ImGui::Combo("可視化モード", &imguiDepthDebugViewMode_, kDebugViewNames, IM_ARRAYSIZE(kDebugViewNames));
            if (debugChanged) {
                waterPlane_->SetDepthFadeDebug(imguiDepthFadeDebugEnabled_, imguiDepthFadeDebugScale_);
                waterPlane_->SetDepthDebugViewMode(static_cast<WaterDebugViewMode>(imguiDepthDebugViewMode_));
            }
            ImGui::TreePop();
        }
    }

    if (ImGui::CollapsingHeader("波とアニメーション", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("UV");
        if (ImGui::DragFloat2("スクロール速度 (U, V)", imguiScrollSpeed_, 0.001f, -1.0f, 1.0f, "%.4f")) {
            waterPlane_->GetScrollSpeed() = { imguiScrollSpeed_[0], imguiScrollSpeed_[1] };
        }
        if (ImGui::DragFloat2("タイリング (U, V)", imguiUVTiling_, 0.1f, 0.1f, 32.0f, "%.2f")) {
            waterPlane_->GetUVTiling() = { imguiUVTiling_[0], imguiUVTiling_[1] };
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Gerstner 波");
        const int prevActiveWaveCount = imguiActiveWaveCount_;
        if (ImGui::SliderInt("有効波本数", &imguiActiveWaveCount_, 1, static_cast<int>(kMaxWaterWaveCount))) {
            if (imguiLockRecommendedWaveCount_) {
                RestoreRecommendedWaveCount(currentPreset);
            } else {
                waterPlane_->SetActiveWaveCount(static_cast<uint32_t>(imguiActiveWaveCount_));
                if (imguiAutoGenerateOnWaveCountIncrease_ && imguiActiveWaveCount_ > prevActiveWaveCount) {
                    RegenerateLayeredWaves(currentPreset, ClampWaveCountToRange(imguiActiveWaveCount_));
                }
            }
        }

        ImGui::TextDisabled("推奨本数: %u", preset.recommendedWaveCount);
        const char* layerNames[kWaterWaveLayerCount] = { "大波", "中波", "小波" };
        for (uint32_t layerIndex = 0; layerIndex < kWaterWaveLayerCount; ++layerIndex) {
            const WaterWaveLayerConfig& layer = preset.layers[layerIndex];
            ImGui::BulletText("%s: %u本 / 振幅 %.3f-%.3f / 波長 %.1f-%.1f",
                layerNames[layerIndex],
                layer.count,
                layer.amplitudeMin,
                layer.amplitudeMax,
                layer.wavelengthMin,
                layer.wavelengthMax);
        }

        if (ImGui::TreeNode("個別波の詳細編集")) {
            WaveParams* waves = waterPlane_->GetWaves();
            for (uint32_t i = 0; i < kMaxWaterWaveCount; ++i) {
                char label[32];
                std::snprintf(label, sizeof(label), "波 %u", i);
                if (ImGui::TreeNode(label)) {
                    float dir[2] = { waves[i].direction.x, waves[i].direction.y };
                    if (ImGui::DragFloat2("進行方向 (XZ)", dir, 0.01f, -1.0f, 1.0f)) {
                        float len = std::sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);
                        if (len > 1e-4f) { dir[0] /= len; dir[1] /= len; }
                        waves[i].direction = { dir[0], dir[1] };
                    }
                    ImGui::DragFloat("振幅 (Amplitude)", &waves[i].amplitude, 0.01f, 0.0f, 5.0f);
                    ImGui::DragFloat("波長 (Wavelength)", &waves[i].wavelength, 0.1f, 0.1f, 50.0f);
                    ImGui::DragFloat("位相速度 (Speed)", &waves[i].speed, 0.05f, 0.0f, 10.0f);
                    ImGui::DragFloat("急峻度 (Steepness)", &waves[i].steepness, 0.01f, 0.0f, 1.0f);
                    ImGui::DragFloat("位相オフセット (Phase)", &waves[i].phaseOffset, 0.05f, -12.56f, 12.56f);
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }

    ImGui::End();
}
#endif

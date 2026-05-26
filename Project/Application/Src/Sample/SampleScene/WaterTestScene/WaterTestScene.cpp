#include "pch.h"
#include "WaterTestScene.h"

#include "Graphics/IBL/IBLSystem.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderManager.h"
#include "Camera/CameraManager.h"
#include "Scene/SceneManager.h"
#include "Sample/TestGameObject/SkyBox/SkyBoxObject.h"
#include "Sample/TestGameObject/Primitive/WaterPlaneObject.h"
#include "Sample/TestGameObject/Model/ModelObject.h"
#include "Utility/FrameRate/FrameRateController.h"
#include <cmath>
#include <cstdio>

using namespace CoreEngine;

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
    // アルベドテクスチャ名だけ登録しておく（初期状態は非表示 = テクスチャなし）
    // ImGui の「テクスチャモード」から「アルベド + ノーマルマップ」を選ぶと有効化される
    // 空文字を渡すと white1x1.png がフォールバックされ、ベースカラーのみで描画される
    waterPlane_ = CreateObject<WaterPlaneObject>(100.0f, 64, "waterAlbedo.jpg");
    waterPlane_->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
    waterPlane_->GetTransform().scale = { 1.0f, 1.0f, 1.0f };

    // GBuffer パスをバイパスし、水面専用 VS（Gerstner Wave）が適用される
    // Forward パスで描画させるためアルファブレンドに設定する
    waterPlane_->SetBlendMode(BlendMode::kBlendModeNormal);

    if (auto* mat = waterPlane_->GetModel()->GetMaterial()) {
        // 湖らしい深い青緑をベースカラーとして設定する
        // テクスチャが無くてもアルベドカラーで水の色感を出す
        mat->SetColor({ 0.04f, 0.18f, 0.28f, 1.0f }); // 深い青緑
        mat->SetMetallic(0.0f);
        mat->SetRoughness(0.04f);  // 鏡面に近い（0.0 に近いほど鏡面反射が強くなる）
        mat->SetLightingEnabled(true);
        mat->SetIBLEnabled(true);
    }

    // ノーマルマップを設定（ImGui の "NormalMap Enable" で有効/無効を切り替え可能）
    waterPlane_->SetNormalMapTextureName("waterNormal.jpg");

    // UV スクロール速度とタイリングを設定
    // scrollSpeed: U方向に 0.03 UV/秒、V方向に 0.01 UV/秒 でゆっくり流れる
    waterPlane_->SetScrollSpeed({ 0.03f, 0.01f });
    waterPlane_->SetUVTiling({ 4.0f, 4.0f });
    waterPlane_->SetActive(true);

    // ===== 地面モデル =====
    groundObject_ = CreateObject<ModelObject>("ground.gltf");
    groundObject_->GetTransform().translate = { 0.0f, -0.1f, 0.0f };
    groundObject_->GetTransform().scale = { 1.0f,  1.0f, 1.0f };
    // PBRテクスチャマップを有効化（gltfに埋め込まれたテクスチャを使用）
    groundObject_->SetPBRTextureMapsEnabled(true, true, true, true);
    groundObject_->SetIBLEnabled(true);
    groundObject_->SetActive(true);

    // ===== Step 4: 反射パス初期化 =====
    auto* dxCommon = engine_->GetComponent<DirectXCommon>();
    if (dxCommon) {
        reflectionPass_.Initialize(dxCommon, 2);
    }

#ifdef USE_IMGUI
    // 初期テクスチャモードを反映する（デフォルト: ノーマルマップのみ）
    // アルベドテクスチャは PrimitiveGameObject::Initialize() でロードされるが
    // ここで texture_.gpuHandle をクリアしてモード 1（ノーマルマップのみ）にそろえる
    waterPlane_->SetAlbedoTextureEnabled(imguiTextureMode_ == 2);
    waterPlane_->SetNormalMapEnabled(imguiTextureMode_ >= 1);
#endif
}

void WaterTestScene::OnUpdate() {
    // フレーム時間を取得して UV スクロールを更新
    auto* frameRate = engine_->GetComponent<FrameRateController>();
    if (waterPlane_ && frameRate) {
        waterPlane_->UpdateUVScroll(frameRate->GetDeltaTime());
    }

    // ===== 水面をカメラ XZ に追従させて「無限遠」に見せる =====
    // メッシュ自体をカメラと同じ XZ 座標に移動するだけで
    // 常にカメラの真下に広大な水面が広がっているように見える
    auto* cameraManager = engine_->GetComponent<CameraManager>();
    if (waterPlane_ && cameraManager) {
        auto* cam = cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (cam) {
            auto camPos = cam->GetPosition();
            auto& t = waterPlane_->GetTransform();
            t.translate.x = camPos.x;
            t.translate.z = camPos.z;
            // Y は水面高さのまま維持する
        }
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
    // ---- Step 4: 反射パスを先に実行する ----
    // PrepareRender() は BaseScene::Draw() の前に呼ばれるため描画キューは既に積まれている
    if (waterPlane_) {
        auto* dxCommon = engine_->GetComponent<DirectXCommon>();
        auto* renderManager = engine_->GetComponent<RenderManager>();
        auto* cameraManager = engine_->GetComponent<CameraManager>();

        if (dxCommon && renderManager && cameraManager) {
            auto* cmdList = dxCommon->GetCommandList();
            auto* mainCamera = cameraManager->GetActiveCamera(CameraType::Camera3D);
            constexpr float kWaterHeight = 0.0f; // 水面の Y 座標

            // クリップ平面を水面オブジェクトに伝える（反射パス中は水面自身をクリップ）
            waterPlane_->SetClipPlane(reflectionPass_.GetClipPlane(), false);
            waterPlane_->UpdateFrameConstants();

            // 反射シーンをオフスクリーン RTT に描画する
            reflectionPass_.Render(cmdList, renderManager, mainCamera, kWaterHeight);

            // 反射テクスチャを水面オブジェクトに渡す
            waterPlane_->SetReflectionTexture(reflectionPass_.GetReflectionSRV());

            // シーン深度 SRV を水面オブジェクトに渡す（Depth Fade 用）
            // GBuffer パス完了後の深度ステンシルバッファの SRV を使用する
            waterPlane_->SetSceneDepthSRV(dxCommon->GetDepthStencilSRV());

            // reflectionEnabled と depthFadeEnabled を含む最終状態を GPU バッファに反映する
            // （SetReflectionTexture が reflectionEnabled を更新するため、ここで再書き込みが必要）
            waterPlane_->UpdateFrameConstants();
        }
    }

    // ---- 通常描画 ----
    BaseScene::Draw();
}

void WaterTestScene::Finalize() {
    BaseScene::Finalize();
}

#ifdef USE_IMGUI
void WaterTestScene::DrawWaterImGui() {
    if (!waterPlane_) { return; }

    ImGui::SetNextWindowSize(ImVec2(440, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Water Parameters")) {
        ImGui::End();
        return;
    }

    // ===== テクスチャモード =====
    if (ImGui::CollapsingHeader("テクスチャモード", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("水面のテクスチャ使用方法を切り替えます");
        ImGui::Spacing();

        const int prevMode = imguiTextureMode_;
        ImGui::RadioButton("テクスチャなし（ベースカラーのみ）", &imguiTextureMode_, 0);
        ImGui::RadioButton("ノーマルマップのみ", &imguiTextureMode_, 1);
        ImGui::RadioButton("アルベド + ノーマルマップ", &imguiTextureMode_, 2);

        if (imguiTextureMode_ != prevMode) {
            // アルベドテクスチャ: モード 2 のときのみ有効
            // （WaterPlaneObject の albedoTextureName_ が設定されていれば読み込む）
            waterPlane_->SetAlbedoTextureEnabled(imguiTextureMode_ == 2);
            // ノーマルマップ: モード 1 / 2 のときに有効
            waterPlane_->SetNormalMapEnabled(imguiTextureMode_ >= 1);
        }
    }

    // ===== マテリアル =====
    if (ImGui::CollapsingHeader("マテリアル", ImGuiTreeNodeFlags_DefaultOpen)) {
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
    }

    // ===== UV =====
    if (ImGui::CollapsingHeader("UV スクロール / タイリング", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::DragFloat2("スクロール速度 (U, V)", imguiScrollSpeed_, 0.001f, -1.0f, 1.0f, "%.4f")) {
            waterPlane_->GetScrollSpeed() = { imguiScrollSpeed_[0], imguiScrollSpeed_[1] };
        }
        if (ImGui::DragFloat2("タイリング (U, V)", imguiUVTiling_, 0.1f, 0.1f, 32.0f, "%.2f")) {
            waterPlane_->GetUVTiling() = { imguiUVTiling_[0], imguiUVTiling_[1] };
        }
    }

    // ===== Gerstner 波 =====
    if (ImGui::CollapsingHeader("Gerstner 波パラメータ", ImGuiTreeNodeFlags_DefaultOpen)) {
        WaveParams* waves = waterPlane_->GetWaves();
        for (int i = 0; i < 4; ++i) {
            char label[32];
            std::snprintf(label, sizeof(label), "波 %d", i);
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
                ImGui::TreePop();
            }
        }
    }

    // ===== Depth Fade =====
    if (ImGui::CollapsingHeader("Depth Fade（水深透過）", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("水面より深い場所ほど光が吸収され不透明な水色になります");
        ImGui::Spacing();
        bool dfChanged = false;
        dfChanged |= ImGui::Checkbox("Depth Fade 有効", &imguiDepthFadeEnabled_);
        dfChanged |= ImGui::SliderFloat("吸収係数", &imguiAbsorptionCoeff_, 0.0f, 5.0f, "%.3f");
        if (dfChanged) {
            waterPlane_->SetDepthFade(imguiAbsorptionCoeff_, imguiDepthFadeEnabled_);
        }
        ImGui::Spacing();
        bool colorChanged = false;
        colorChanged |= ImGui::ColorEdit3("浅瀬の色", imguiShallowColor_);
        colorChanged |= ImGui::ColorEdit3("深場の色", imguiDeepColor_);
        if (colorChanged) {
            waterPlane_->SetWaterColors(
                { imguiShallowColor_[0], imguiShallowColor_[1], imguiShallowColor_[2] },
                { imguiDeepColor_[0],    imguiDeepColor_[1],    imguiDeepColor_[2] });
        }
    }

    // ===== Fresnel =====
    if (ImGui::CollapsingHeader("Fresnel 透過設定", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("視線が水面に対して垂直(真上)ほど透明に、斜めほど不透明になります");
        ImGui::Spacing();
        bool changed = false;
        changed |= ImGui::SliderFloat("最小 Alpha（真上から）", &imguiFresnelMinAlpha_, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::SliderFloat("最大 Alpha（斜めから）", &imguiFresnelMaxAlpha_, 0.0f, 1.0f, "%.3f");
        if (changed) {
            waterPlane_->SetFresnelAlpha(imguiFresnelMinAlpha_, imguiFresnelMaxAlpha_);
        }
    }

    // ===== 反射 =====
    if (ImGui::CollapsingHeader("反射 / IBL")) {
        // reflectionEnabled は SetReflectionTexture() が自動制御するため読み取り専用表示
        const bool reflEnabled = (waterPlane_->GetFrameConstants().reflectionEnabled != 0);
        ImGui::BeginDisabled();
        bool tmp = reflEnabled;
        ImGui::Checkbox("反射テクスチャ 有効（自動制御）", &tmp);
        ImGui::EndDisabled();
        ImGui::TextDisabled("WaterReflectionPass から自動で設定されます");
    }

    ImGui::End();
}
#endif

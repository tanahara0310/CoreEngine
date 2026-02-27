#include "SneakWalkModelObject.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/Model/ModelManager.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Engine/Camera/ICamera.h"

#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif

using namespace CoreEngine;


void SneakWalkModelObject::Initialize() {
   auto engine = GetEngineSystem();

   // ModelManagerを取得
   auto modelManager = engine->GetComponent<ModelManager>();
   if (!modelManager) {
      return;
   }

   // アニメーションを事前に読み込む
   AnimationLoadInfo animInfo;
   animInfo.directory = "SampleAssets/human";
   animInfo.modelFilename = "sneakWalk.gltf";
   animInfo.animationName = "sneakWalkAnimation";
   animInfo.animationFilename = "sneakWalk.gltf";
   modelManager->LoadAnimation(animInfo);

   // スケルトンアニメーションモデルとして作成
   model_ = modelManager->CreateSkeletonModel(
      "SampleAssets/human/sneakWalk.gltf",
      "sneakWalkAnimation",
      true
   );

   // Transformの初期化
   auto dxCommon = engine->GetComponent<DirectXCommon>();
   if (dxCommon) {
      transform_.Initialize(dxCommon->GetDevice());
   }

   // 初期位置・スケール設定（中央に配置）
   transform_.translate = { 0.0f, 0.0f, 0.0f };
   transform_.scale = { 1.0f, 1.0f, 1.0f };
   transform_.rotate = { 0.0f, 0.0f, 0.0f };

   // テクスチャを初期化時に読み込む
   uvCheckerTexture_ = CoreEngine::TextureManager::GetInstance().Load("SampleAssets/human/white.png");

   // アクティブ状態に設定
   SetActive(true);
}

void SneakWalkModelObject::Update() {
   if (!IsActive() || !model_) {
      return;
   }

   auto engine = GetEngineSystem();

   // Transformの更新
   transform_.TransferMatrix();

   // FrameRateControllerから1フレームあたりの時間を取得
   auto frameRateController = engine->GetComponent<FrameRateController>();
   if (!frameRateController) {
      return;
   }

   float deltaTime = frameRateController->GetDeltaTime();

   // アニメーションの更新（コントローラー経由で自動的にスケルトンも更新される）
   if (model_->HasAnimationController()) {
      model_->UpdateAnimation(deltaTime);
   }
}

void SneakWalkModelObject::Draw(const CoreEngine::ICamera* camera) {
    if (!camera || !model_) return;
    
    // モデルの描画
    model_->Draw(transform_, camera, uvCheckerTexture_.gpuHandle);
}

#ifdef _DEBUG
bool SneakWalkModelObject::DrawImGuiExtended() {
    if (!model_) return false;
    
    bool changed = false;
    
    if (ImGui::TreeNode("SneakWalk Model Material")) {
        auto* materialManager = model_->GetMaterialManager();
        if (materialManager) {
            auto* constants = materialManager->GetConstants();
            
            // 色の設定
            float colorArray[4] = { constants->color.x, constants->color.y, constants->color.z, constants->color.w };
            if (ImGui::ColorEdit4("Color", colorArray)) {
                constants->color = { colorArray[0], colorArray[1], colorArray[2], colorArray[3] };
                changed = true;
            }
            
            // ライティング有効/無効
            bool enableLighting = constants->enableLighting != 0;
            if (ImGui::Checkbox("Enable Lighting", &enableLighting)) {
                constants->enableLighting = enableLighting ? 1 : 0;
                changed = true;
            }
            
            // シェーディングモード
            const char* shadingModes[] = { "None", "Lambert", "Half-Lambert", "Toon" };
            if (ImGui::Combo("Shading Mode", &constants->shadingMode, shadingModes, 4)) {
                changed = true;
            }
            
            ImGui::Separator();
            
            // 環境マップ設定
            bool enableEnvironmentMap = constants->enableEnvironmentMap != 0;
            if (ImGui::Checkbox("Enable Environment Map", &enableEnvironmentMap)) {
                constants->enableEnvironmentMap = enableEnvironmentMap ? 1 : 0;
                changed = true;
            }
            
            if (enableEnvironmentMap) {
                if (ImGui::SliderFloat("Environment Map Intensity", &constants->environmentMapIntensity, 0.0f, 1.0f)) {
                    changed = true;
                }
            }
        }
        ImGui::TreePop();
    }
    
    
    return changed;
}
#endif


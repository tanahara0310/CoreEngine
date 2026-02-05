#include "SkeletonModelObject.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/Model/ModelManager.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Engine/Graphics/Material/MaterialManager.h"
#include "Engine/Camera/ICamera.h"

#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif


namespace CoreEngine
{
void SkeletonModelObject::Initialize() {
   auto engine = GetEngineSystem();

   // ModelManagerを取得
   auto modelManager = engine->GetComponent<ModelManager>();
   if (!modelManager) {
      return;
   }

   // アニメーションを事前に読み込む
   AnimationLoadInfo animInfo;
   animInfo.directory = "SampleAssets/simpleSkin";
   animInfo.modelFilename = "simpleSkin.gltf";
   animInfo.animationName = "simpleSkinAnimation";
   animInfo.animationFilename = "simpleSkin.gltf";
   modelManager->LoadAnimation(animInfo);

   // スケルトンアニメーションモデルとして作成
   model_ = modelManager->CreateSkeletonModel(
      "SampleAssets/simpleSkin/simpleSkin.gltf",
      "simpleSkinAnimation",  // アニメーション名
      true   // ループ再生
   );

   // Transformの初期化
   auto dxCommon = engine->GetComponent<DirectXCommon>();
   if (dxCommon) {
      transform_.Initialize(dxCommon->GetDevice());
   }

   // 初期位置・スケール設定
   transform_.translate = { -3.0f, 0.0f, 0.0f };  // 左側に配置
   transform_.scale = { 1.0f, 1.0f, 1.0f };
   transform_.rotate = { 0.0f, 0.0f, 0.0f };

   // テクスチャを初期化時に読み込む
   uvCheckerTexture_ = TextureManager::GetInstance().Load("SampleAssets/simpleSkin/uvChecker.png");

   // アクティブ状態に設定
   SetActive(true);
}

void SkeletonModelObject::Update() {
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

void SkeletonModelObject::Draw(const ICamera* camera) {
    if (!camera || !model_) return;
    
    // モデルの描画
    model_->Draw(transform_, camera, uvCheckerTexture_.gpuHandle);
}

#ifdef _DEBUG
bool SkeletonModelObject::DrawImGuiExtended() {
    if (!model_) return false;
    
    bool changed = false;
    
    if (ImGui::TreeNode("Skeleton Model Material")) {
        auto* materialManager = model_->GetMaterialManager();
        if (materialManager) {
            // 色の設定
            Vector4 color = materialManager->GetColor();
            float colorArray[4] = { color.x, color.y, color.z, color.w };
            if (ImGui::ColorEdit4("Color", colorArray)) {
                materialManager->SetColor({ colorArray[0], colorArray[1], colorArray[2], colorArray[3] });
                changed = true;
            }
            
            // ライティング有効/無効
            bool enableLighting = materialManager->GetMaterialData()->enableLighting != 0;
            if (ImGui::Checkbox("Enable Lighting", &enableLighting)) {
                materialManager->SetEnableLighting(enableLighting);
                changed = true;
            }
            
            // シェーディングモード
            const char* shadingModes[] = { "None", "Lambert", "Half-Lambert", "Toon" };
            int shadingMode = materialManager->GetShadingMode();
            if (ImGui::Combo("Shading Mode", &shadingMode, shadingModes, 4)) {
                materialManager->SetShadingMode(shadingMode);
                changed = true;
            }
            
            // トゥーンシェーディング設定
            if (shadingMode == 3) {
                float toonThreshold = materialManager->GetToonThreshold();
                if (ImGui::SliderFloat("Toon Threshold", &toonThreshold, 0.0f, 1.0f)) {
                    materialManager->SetToonThreshold(toonThreshold);
                    changed = true;
                }
                
                float toonSmoothness = materialManager->GetToonSmoothness();
                if (ImGui::SliderFloat("Toon Smoothness", &toonSmoothness, 0.0f, 0.5f)) {
                    materialManager->SetToonSmoothness(toonSmoothness);
                    changed = true;
                }
            }
            
            // ディザリング設定
            bool enableDithering = materialManager->IsEnableDithering();
            if (ImGui::Checkbox("Enable Dithering", &enableDithering)) {
                materialManager->SetEnableDithering(enableDithering);
                changed = true;
            }
            
            if (enableDithering) {
                float ditheringScale = materialManager->GetDitheringScale();
                if (ImGui::SliderFloat("Dithering Scale", &ditheringScale, 0.1f, 5.0f)) {
                    materialManager->SetDitheringScale(ditheringScale);
                    changed = true;
                }
            }
            
            ImGui::Separator();
            
            // 環境マップ設定
            bool enableEnvironmentMap = materialManager->IsEnableEnvironmentMap();
            if (ImGui::Checkbox("Enable Environment Map", &enableEnvironmentMap)) {
                materialManager->SetEnableEnvironmentMap(enableEnvironmentMap);
                changed = true;
            }
            
            if (enableEnvironmentMap) {
                float envMapIntensity = materialManager->GetEnvironmentMapIntensity();
                if (ImGui::SliderFloat("Environment Map Intensity", &envMapIntensity, 0.0f, 1.0f)) {
                    materialManager->SetEnvironmentMapIntensity(envMapIntensity);
                    changed = true;
                }
            }
        }
        ImGui::TreePop();
    }
    
    return changed;
}
#endif
}

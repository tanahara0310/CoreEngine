#include "ModelObject.h"
#include <EngineSystem.h>
#include "Engine/Camera/ICamera.h"

#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif

namespace CoreEngine
{
    void ModelObject::Initialize(const std::string& modelPath) {
        // 必須コンポーネントの取得
        auto engine = GetEngineSystem();

        auto dxCommon = engine->GetComponent<DirectXCommon>();
        auto modelManager = engine->GetComponent<ModelManager>();

        if (!dxCommon || !modelManager) {
            return;
        }

        // 静的モデルとして作成
        model_ = modelManager->CreateStaticModel(modelPath);

        // トランスフォームの初期化
        transform_.Initialize(dxCommon->GetDevice());

        // アクティブ状態に設定
        SetActive(true);
    }

    void ModelObject::Update() {
        if (!IsActive() || !model_) {
            return;
        }

        // トランスフォームの更新
        transform_.TransferMatrix();
    }

    void ModelObject::Draw(const ICamera* camera) {
        if (!camera || !model_) return;

        // モデルの描画（テクスチャハンドルを省略してモデル組み込みテクスチャを使用）
        model_->Draw(transform_, camera);
    }

    void ModelObject::SetPBRParameters(float metallic, float roughness, float ao) {
        if (!model_) return;

        auto* materialManager = model_->GetMaterialManager();
        if (materialManager) {
            auto* constants = materialManager->GetConstants();
            constants->metallic = metallic;
            constants->roughness = roughness;
            constants->ao = ao;
        }
    }

    void ModelObject::SetPBREnabled(bool enable) {
        if (!model_) return;

        auto* materialManager = model_->GetMaterialManager();
        if (materialManager) {
            materialManager->GetConstants()->enablePBR = enable ? 1 : 0;
        }
    }

    void ModelObject::SetPBRTextureMapsEnabled(bool useNormal, bool useMetallic,
        bool useRoughness, bool useAO) {
        if (!model_) return;

        auto* materialManager = model_->GetMaterialManager();
        if (materialManager) {
            auto* constants = materialManager->GetConstants();
            constants->useNormalMap = useNormal ? 1 : 0;
            constants->useMetallicMap = useMetallic ? 1 : 0;
            constants->useRoughnessMap = useRoughness ? 1 : 0;
            constants->useAOMap = useAO ? 1 : 0;
        }
    }

    void ModelObject::SetEnvironmentMapEnabled(bool enable) {
        if (!model_) return;

        auto* materialManager = model_->GetMaterialManager();
        if (materialManager) {
            materialManager->GetConstants()->enableEnvironmentMap = enable ? 1 : 0;
        }
    }

    void ModelObject::SetEnvironmentMapIntensity(float intensity) {
        if (!model_) return;

        auto* materialManager = model_->GetMaterialManager();
        if (materialManager) {
            materialManager->GetConstants()->environmentMapIntensity = intensity;
        }
    }

    void ModelObject::SetMaterialColor(const Vector4& color) {
        if (!model_) return;

        auto* materialManager = model_->GetMaterialManager();
        if (materialManager) {
            materialManager->GetConstants()->color = color;
        }
    }

    void ModelObject::SetIBLEnabled(bool enable) {
        if (!model_) return;

        auto* materialManager = model_->GetMaterialManager();
        if (materialManager) {
            materialManager->GetConstants()->enableIBL = enable ? 1 : 0;
        }
    }

    void ModelObject::SetIBLIntensity(float intensity) {
        if (!model_) return;

        auto* materialManager = model_->GetMaterialManager();
        if (materialManager) {
            materialManager->GetConstants()->iblIntensity = intensity;
        }
    }

    void ModelObject::SetEnvironmentRotationY(float rotationY) {
        if (!model_) return;

        auto* materialManager = model_->GetMaterialManager();
        if (materialManager) {
            materialManager->GetConstants()->environmentRotationY = rotationY;
        }
    }

#ifdef _DEBUG
    bool ModelObject::DrawImGuiExtended() {

        auto* materialManager = model_->GetMaterialManager();
        auto* constants = materialManager->GetConstants();

        // ===== PBR Settings =====
        if (ImGui::CollapsingHeader("PBR Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool enablePBR = constants->enablePBR != 0;
            if (ImGui::Checkbox("Enable PBR", &enablePBR)) {
                constants->enablePBR = enablePBR ? 1 : 0;
            }

            if (enablePBR) {
                ImGui::Separator();
                ImGui::Text("PBR Texture Maps");

                bool useNormalMap = constants->useNormalMap != 0;
                bool useMetallicMap = constants->useMetallicMap != 0;
                bool useRoughnessMap = constants->useRoughnessMap != 0;
                bool useAOMap = constants->useAOMap != 0;

                if (ImGui::Checkbox("Use Normal Map", &useNormalMap)) {
                    constants->useNormalMap = useNormalMap ? 1 : 0;
                }
                if (ImGui::Checkbox("Use Metallic Map", &useMetallicMap)) {
                    constants->useMetallicMap = useMetallicMap ? 1 : 0;
                }
                if (ImGui::Checkbox("Use Roughness Map", &useRoughnessMap)) {
                    constants->useRoughnessMap = useRoughnessMap ? 1 : 0;
                }
                if (ImGui::Checkbox("Use AO Map", &useAOMap)) {
                    constants->useAOMap = useAOMap ? 1 : 0;
                }

                ImGui::Separator();
                ImGui::Text("PBR Parameters (used when maps are disabled)");
                ImGui::SliderFloat("Metallic", &constants->metallic, 0.0f, 1.0f);
                ImGui::SliderFloat("Roughness", &constants->roughness, 0.0f, 1.0f);
                ImGui::SliderFloat("AO", &constants->ao, 0.0f, 1.0f);
            }
        }

        // ===== Material Settings =====
        if (ImGui::CollapsingHeader("Material Settings")) {
            ImGui::ColorEdit4("Material Color", &constants->color.x);
            ImGui::SliderFloat("Shininess", &constants->shininess, 1.0f, 128.0f);
        }

        // ===== Environment Map Settings =====
        if (ImGui::CollapsingHeader("Environment Map")) {
            bool enableEnvMap = constants->enableEnvironmentMap != 0;
            if (ImGui::Checkbox("Enable Environment Map", &enableEnvMap)) {
                constants->enableEnvironmentMap = enableEnvMap ? 1 : 0;
            }

            if (enableEnvMap) {
                ImGui::SliderFloat("Environment Intensity", &constants->environmentMapIntensity, 0.0f, 1.0f);
                float rotationDeg = constants->environmentRotationY * 57.2958f; // ラジアン→度
                if (ImGui::SliderFloat("Environment Rotation (deg)", &rotationDeg, 0.0f, 360.0f)) {
                    constants->environmentRotationY = rotationDeg / 57.2958f; // 度→ラジアン
                }
            }
        }

        // ===== IBL Settings =====
        if (ImGui::CollapsingHeader("IBL (Image-Based Lighting)")) {
            bool enableIBL = constants->enableIBL != 0;
            if (ImGui::Checkbox("Enable IBL", &enableIBL)) {
                constants->enableIBL = enableIBL ? 1 : 0;
            }

            if (enableIBL) {
                ImGui::SliderFloat("IBL Intensity", &constants->iblIntensity, 0.0f, 2.0f);
            }
        }

        // ===== Lighting Settings =====
        if (ImGui::CollapsingHeader("Lighting")) {
            bool enableLighting = constants->enableLighting != 0;
            if (ImGui::Checkbox("Enable Lighting", &enableLighting)) {
                constants->enableLighting = enableLighting ? 1 : 0;
            }

            const char* shadingModes[] = { "None", "Lambert", "Half-Lambert", "Toon" };
            ImGui::Combo("Shading Mode", &constants->shadingMode, shadingModes, 4);

            if (constants->shadingMode == 3) { // Toon
                ImGui::SliderFloat("Toon Threshold", &constants->toonThreshold, 0.0f, 1.0f);
                ImGui::SliderFloat("Toon Smoothness", &constants->toonSmoothness, 0.0f, 0.5f);
            }
        }

        // ===== Dithering =====
        if (ImGui::CollapsingHeader("Dithering")) {
            bool enableDithering = constants->enableDithering != 0;
            if (ImGui::Checkbox("Enable Dithering", &enableDithering)) {
                constants->enableDithering = enableDithering ? 1 : 0;
            }

            if (enableDithering) {
                ImGui::SliderFloat("Dithering Scale", &constants->ditheringScale, 0.1f, 5.0f);
            }
        }
        
        return false; // パラメータ変更は常に適用されるため、特に戻り値は使わない
    }
#endif
}

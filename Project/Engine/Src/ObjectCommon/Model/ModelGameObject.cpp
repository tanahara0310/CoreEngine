#include "ModelGameObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Camera/ICamera.h"

#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif

namespace CoreEngine
{
    void ModelGameObject::Initialize() {
        auto* engine = GetEngineSystem();
        auto* dxCommon = engine->GetComponent<DirectXCommon>();
        auto* modelMgr = engine->GetComponent<ModelManager>();

        if (dxCommon) {
            transform_.Initialize(dxCommon->GetDevice());
        }

        const std::string modelPath = GetModelPath();
        if (!modelPath.empty() && modelMgr) {
            model_ = modelMgr->CreateStaticModel(modelPath);
        }

        const std::string texPath = GetTexturePath();
        if (!texPath.empty()) {
            texture_ = TextureManager::GetInstance().Load(texPath);
        }

        OnInitialize();
        SetActive(true);
    }

    void ModelGameObject::Update() {
        if (!IsActive()) return;
        transform_.TransferMatrix();
        OnUpdate();
    }

    void ModelGameObject::Draw(const ICamera* camera) {
        if (!IsVisible() || !model_ || !camera) return;
        model_->Draw(transform_, camera, texture_.gpuHandle);
        OnDraw(camera);
    }

    void ModelGameObject::DrawShadow(ID3D12GraphicsCommandList* cmdList) {
        if (model_ && model_->IsInitialized()) {
            model_->DrawShadow(transform_, cmdList);
        }
    }

#ifdef _DEBUG
    bool ModelGameObject::DrawImGui() {
        bool changed = false;

        const char* displayName = GetName().empty() ? GetObjectName() : GetName().c_str();
        char headerLabel[256];
        snprintf(headerLabel, sizeof(headerLabel), "%s##%p", displayName, (void*)this);

        if (ImGui::CollapsingHeader(headerLabel)) {
            ImGui::PushID(this);

            // Active（Undo/Redo コールバックあり）
            bool prevActive = isActive_;
            if (ImGui::Checkbox("Active", &isActive_)) {
                changed = true;
                if (onEditCommitted_) {
                    onEditCommitted_(this,
                        transform_.translate, transform_.rotate, transform_.scale,
                        prevActive);
                }
            }

            if (ImGui::Checkbox("Visible", &isVisible_)) { changed = true; }

            bool autoUpdate = autoUpdate_;
            if (ImGui::Checkbox("Auto Update", &autoUpdate)) {
                autoUpdate_ = autoUpdate;
                changed = true;
            }

            ImGui::Separator();

            if (ImGui::TreeNode("Transform")) {
                Vector3& pos = transform_.translate;
                Vector3& rot = transform_.rotate;
                Vector3& scale = transform_.scale;

                changed |= ImGui::DragFloat3("Position", &pos.x, 0.1f);
                if (ImGui::IsItemActivated()) {
                    imguiSnapTranslate_ = transform_.translate;
                    imguiSnapRotate_ = transform_.rotate;
                    imguiSnapScale_ = transform_.scale;
                    imguiSnapActive_ = isActive_;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && onEditCommitted_) {
                    onEditCommitted_(this, imguiSnapTranslate_, imguiSnapRotate_, imguiSnapScale_, imguiSnapActive_);
                }

                changed |= ImGui::DragFloat3("Rotation", &rot.x, 0.01f);
                if (ImGui::IsItemActivated()) {
                    imguiSnapTranslate_ = transform_.translate;
                    imguiSnapRotate_ = transform_.rotate;
                    imguiSnapScale_ = transform_.scale;
                    imguiSnapActive_ = isActive_;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && onEditCommitted_) {
                    onEditCommitted_(this, imguiSnapTranslate_, imguiSnapRotate_, imguiSnapScale_, imguiSnapActive_);
                }

                changed |= ImGui::DragFloat3("Scale", &scale.x, 0.01f);
                if (ImGui::IsItemActivated()) {
                    imguiSnapTranslate_ = transform_.translate;
                    imguiSnapRotate_ = transform_.rotate;
                    imguiSnapScale_ = transform_.scale;
                    imguiSnapActive_ = isActive_;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && onEditCommitted_) {
                    onEditCommitted_(this, imguiSnapTranslate_, imguiSnapRotate_, imguiSnapScale_, imguiSnapActive_);
                }

                ImGui::TreePop();
            }

            changed |= DrawMaterialImGui();
            changed |= DrawImGuiExtended();
            DrawSaveButton();

            ImGui::PopID();
        }

        return changed;
    }

    bool ModelGameObject::DrawMaterialImGui() {
        if (!model_ || !model_->GetMaterial()) return false;
        if (!materialDebugUI_) materialDebugUI_ = std::make_unique<MaterialDebugUI>();
        return materialDebugUI_->Draw(model_.get());
    }
#endif

}  // namespace CoreEngine

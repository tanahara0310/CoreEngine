#include "ModelGameObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Camera/ICamera.h"
#include "Utility/JsonManager/JsonManager.h"

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

    json ModelGameObject::OnSerialize() const {
        json j;
        j["active"] = IsActive();
        j["transform"]["translate"] = JsonManager::Vector3ToJson(transform_.translate);
        j["transform"]["rotate"]    = JsonManager::Vector3ToJson(transform_.rotate);
        j["transform"]["scale"]     = JsonManager::Vector3ToJson(transform_.scale);
        return j;
    }

    void ModelGameObject::OnDeserialize(const json& j) {
        if (j.contains("active")) {
            SetActive(j["active"].get<bool>());
        }
        if (j.contains("transform")) {
            const json& t = j["transform"];
            transform_.translate = JsonManager::SafeGetVector3(t, "translate", transform_.translate);
            transform_.rotate    = JsonManager::SafeGetVector3(t, "rotate",    transform_.rotate);
            transform_.scale     = JsonManager::SafeGetVector3(t, "scale",     transform_.scale);
        }
    }

#ifdef _DEBUG
    void ModelGameObject::OnImGuiActiveChanged(bool prevActive) {
        if (onEditCommitted_) {
            onEditCommitted_(this,
                transform_.translate, transform_.rotate, transform_.scale,
                prevActive);
        }
    }

    bool ModelGameObject::DrawImGuiExtended() {
        bool changed = false;

        if (ImGui::TreeNode("Transform")) {
            Vector3& pos   = transform_.translate;
            Vector3& rot   = transform_.rotate;
            Vector3& scale = transform_.scale;

            changed |= ImGui::DragFloat3("Position", &pos.x, 0.1f);
            if (ImGui::IsItemActivated()) {
                imguiSnapTranslate_ = transform_.translate;
                imguiSnapRotate_    = transform_.rotate;
                imguiSnapScale_     = transform_.scale;
                imguiSnapActive_    = isActive_;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && onEditCommitted_) {
                onEditCommitted_(this, imguiSnapTranslate_, imguiSnapRotate_, imguiSnapScale_, imguiSnapActive_);
            }

            changed |= ImGui::DragFloat3("Rotation", &rot.x, 0.01f);
            if (ImGui::IsItemActivated()) {
                imguiSnapTranslate_ = transform_.translate;
                imguiSnapRotate_    = transform_.rotate;
                imguiSnapScale_     = transform_.scale;
                imguiSnapActive_    = isActive_;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && onEditCommitted_) {
                onEditCommitted_(this, imguiSnapTranslate_, imguiSnapRotate_, imguiSnapScale_, imguiSnapActive_);
            }

            changed |= ImGui::DragFloat3("Scale", &scale.x, 0.01f);
            if (ImGui::IsItemActivated()) {
                imguiSnapTranslate_ = transform_.translate;
                imguiSnapRotate_    = transform_.rotate;
                imguiSnapScale_     = transform_.scale;
                imguiSnapActive_    = isActive_;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && onEditCommitted_) {
                onEditCommitted_(this, imguiSnapTranslate_, imguiSnapRotate_, imguiSnapScale_, imguiSnapActive_);
            }

            ImGui::TreePop();
        }

        changed |= DrawMaterialImGui();
        return changed;
    }

    bool ModelGameObject::DrawMaterialImGui() {
        if (!model_ || !model_->GetMaterial()) return false;
        if (!materialDebugUI_) materialDebugUI_ = std::make_unique<MaterialDebugUI>();
        return materialDebugUI_->Draw(model_.get());
    }
#endif

}  // namespace CoreEngine

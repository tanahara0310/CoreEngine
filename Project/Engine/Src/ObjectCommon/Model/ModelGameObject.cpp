#include "ModelGameObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Camera/ICamera.h"
#include "Utility/JsonManager/JsonManager.h"

#ifdef USE_IMGUI
#include "Utility/Debug/ImGui/ImGuiAll.h"
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
        if (!model_ || !camera) return;

        // 視錐台カリング: ワールドAABBが視錐台の外側なら描画をスキップ
        BoundingBox worldAABB = GetWorldBoundingBox();
        if (worldAABB.IsValid()) {
            Frustum frustum = camera->GetFrustum();
            if (frustum.IsOutside(worldAABB)) {
                return;
            }
        }

        model_->Draw(transform_, camera, texture_.gpuHandle);
        OnDraw(camera);
    }

    void ModelGameObject::DrawShadow(ID3D12GraphicsCommandList* cmdList) {
        if (model_ && model_->IsInitialized()) {
            model_->DrawShadow(transform_, cmdList);
        }
    }

    BoundingBox ModelGameObject::GetWorldBoundingBox() const {
        if (!model_ || !model_->GetModelResource()) {
            return BoundingBox(); // 無効なAABBを返す
        }

        const BoundingBox& localAABB = model_->GetModelResource()->GetLocalBoundingBox();
        if (!localAABB.IsValid()) {
            return BoundingBox();
        }

        // ローカルAABBの8頂点をワールド変換し、新しいAABBを構築
        const Matrix4x4& world = transform_.GetWorldMatrix();
        BoundingBox worldAABB;

        for (int i = 0; i < 8; ++i) {
            Vector3 corner = {
                (i & 1) ? localAABB.max.x : localAABB.min.x,
                (i & 2) ? localAABB.max.y : localAABB.min.y,
                (i & 4) ? localAABB.max.z : localAABB.min.z
            };

            // ワールド行列で変換
            Vector3 transformed = {
                corner.x * world.m[0][0] + corner.y * world.m[1][0] + corner.z * world.m[2][0] + world.m[3][0],
                corner.x * world.m[0][1] + corner.y * world.m[1][1] + corner.z * world.m[2][1] + world.m[3][1],
                corner.x * world.m[0][2] + corner.y * world.m[1][2] + corner.z * world.m[2][2] + world.m[3][2]
            };

            if (transformed.x < worldAABB.min.x) worldAABB.min.x = transformed.x;
            if (transformed.y < worldAABB.min.y) worldAABB.min.y = transformed.y;
            if (transformed.z < worldAABB.min.z) worldAABB.min.z = transformed.z;
            if (transformed.x > worldAABB.max.x) worldAABB.max.x = transformed.x;
            if (transformed.y > worldAABB.max.y) worldAABB.max.y = transformed.y;
            if (transformed.z > worldAABB.max.z) worldAABB.max.z = transformed.z;
        }

        return worldAABB;
    }

    json ModelGameObject::OnSerialize() const {
        json j;
        j["active"] = IsActive();
        j["transform"]["translate"] = JsonManager::Vector3ToJson(transform_.translate);
        j["transform"]["rotate"] = JsonManager::Vector3ToJson(transform_.rotate);
        j["transform"]["scale"] = JsonManager::Vector3ToJson(transform_.scale);

        if (model_ && model_->GetMaterial()) {
            const MaterialInstance* mat = model_->GetMaterial();
            json& m = j["material"];
            m["color"] = JsonManager::Vector4ToJson(mat->GetColor());
            m["lighting"] = mat->IsLightingEnabled();
            m["metallic"] = mat->GetMetallic();
            m["roughness"] = mat->GetRoughness();
            m["ao"] = mat->GetAO();
            m["normalMap"] = mat->IsNormalMapEnabled();
            m["metallicMap"] = mat->IsMetallicMapEnabled();
            m["roughnessMap"] = mat->IsRoughnessMapEnabled();
            m["aoMap"] = mat->IsAOMapEnabled();
            m["dithering"] = mat->IsDitheringEnabled();
            m["ditheringScale"] = mat->GetDitheringScale();
            m["ibl"] = mat->IsIBLEnabled();
            m["iblIntensity"] = mat->GetIBLIntensity();
        }

        return j;
    }

    void ModelGameObject::OnDeserialize(const json& j) {
        if (j.contains("active")) {
            SetActive(j["active"].get<bool>());
        }
        if (j.contains("transform")) {
            const json& t = j["transform"];
            transform_.translate = JsonManager::SafeGetVector3(t, "translate", transform_.translate);
            transform_.rotate = JsonManager::SafeGetVector3(t, "rotate", transform_.rotate);
            transform_.scale = JsonManager::SafeGetVector3(t, "scale", transform_.scale);
        }
        if (j.contains("material") && model_ && model_->GetMaterial()) {
            MaterialInstance* mat = model_->GetMaterial();
            const json& m = j["material"];
            if (m.contains("color"))
                mat->SetColor(JsonManager::JsonToVector4(m["color"]));
            mat->SetLightingEnabled(JsonManager::SafeGet<bool>(m, "lighting", mat->IsLightingEnabled()));
            mat->SetMetallic(JsonManager::SafeGet<float>(m, "metallic", mat->GetMetallic()));
            mat->SetRoughness(JsonManager::SafeGet<float>(m, "roughness", mat->GetRoughness()));
            mat->SetAO(JsonManager::SafeGet<float>(m, "ao", mat->GetAO()));
            mat->SetNormalMapEnabled(JsonManager::SafeGet<bool>(m, "normalMap", mat->IsNormalMapEnabled()));
            mat->SetMetallicMapEnabled(JsonManager::SafeGet<bool>(m, "metallicMap", mat->IsMetallicMapEnabled()));
            mat->SetRoughnessMapEnabled(JsonManager::SafeGet<bool>(m, "roughnessMap", mat->IsRoughnessMapEnabled()));
            mat->SetAOMapEnabled(JsonManager::SafeGet<bool>(m, "aoMap", mat->IsAOMapEnabled()));
            mat->SetDitheringEnabled(JsonManager::SafeGet<bool>(m, "dithering", mat->IsDitheringEnabled()));
            mat->SetDitheringScale(JsonManager::SafeGet<float>(m, "ditheringScale", mat->GetDitheringScale()));
            mat->SetIBLEnabled(JsonManager::SafeGet<bool>(m, "ibl", mat->IsIBLEnabled()));
            mat->SetIBLIntensity(JsonManager::SafeGet<float>(m, "iblIntensity", mat->GetIBLIntensity()));
        }
    }

#ifdef USE_IMGUI
    void ModelGameObject::OnImGuiActiveChanged(bool prevActive) {
        if (onEditCommitted_) {
            onEditCommitted_(this,
                transform_.translate, transform_.rotate, transform_.scale,
                prevActive);
        }
    }

    bool ModelGameObject::DrawImGuiExtended() {
        bool changed = false;

        if (auto transformTree = UI::Scope::TreeScope("Transform")) {
            Vector3& pos   = transform_.translate;
            Vector3& rot   = transform_.rotate;
            Vector3& scale = transform_.scale;

            changed |= UI::DragVec3("Position", pos, 0.1f);
            if (ImGui::IsItemActivated()) {
                imguiSnapTranslate_ = transform_.translate;
                imguiSnapRotate_ = transform_.rotate;
                imguiSnapScale_ = transform_.scale;
                imguiSnapActive_ = isActive_;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && onEditCommitted_) {
                onEditCommitted_(this, imguiSnapTranslate_, imguiSnapRotate_, imguiSnapScale_, imguiSnapActive_);
            }

            changed |= UI::DragVec3("Rotation", rot, 0.01f);
            if (ImGui::IsItemActivated()) {
                imguiSnapTranslate_ = transform_.translate;
                imguiSnapRotate_ = transform_.rotate;
                imguiSnapScale_ = transform_.scale;
                imguiSnapActive_ = isActive_;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && onEditCommitted_) {
                onEditCommitted_(this, imguiSnapTranslate_, imguiSnapRotate_, imguiSnapScale_, imguiSnapActive_);
            }

            changed |= UI::DragVec3("Scale", scale, 0.01f);
            if (ImGui::IsItemActivated()) {
                imguiSnapTranslate_ = transform_.translate;
                imguiSnapRotate_ = transform_.rotate;
                imguiSnapScale_ = transform_.scale;
                imguiSnapActive_ = isActive_;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && onEditCommitted_) {
                onEditCommitted_(this, imguiSnapTranslate_, imguiSnapRotate_, imguiSnapScale_, imguiSnapActive_);
            }
        }

        changed |= DrawMaterialImGui();
        return changed;
    }

    bool ModelGameObject::DrawMaterialImGui() {
        if (!model_ || !model_->GetMaterial()) return false;
        if (!materialDebugUI_) materialDebugUI_ = std::make_unique<MaterialDebugUI>();
        return materialDebugUI_->Draw(model_.get());
    }
#endif // USE_IMGUI

}  // namespace CoreEngine

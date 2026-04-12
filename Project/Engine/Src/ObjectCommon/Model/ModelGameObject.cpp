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
        // ── アイコンテクスチャの初回ロード ───────────────────────────────
        static D3D12_GPU_DESCRIPTOR_HANDLE sIconWorldHandle{};
        static D3D12_GPU_DESCRIPTOR_HANDLE sIconMaterialHandle{};
        static D3D12_GPU_DESCRIPTOR_HANDLE sIconSceneHandle{};
        static bool sIconsLoaded = false;
        if (!sIconsLoaded && TextureManager::GetInstance().IsInitialized()) {
            sIconWorldHandle = TextureManager::GetInstance().Load("object_data.png").gpuHandle;
            sIconMaterialHandle = TextureManager::GetInstance().Load("material.png").gpuHandle;
            sIconSceneHandle = TextureManager::GetInstance().Load("scene.png").gpuHandle;
            sIconsLoaded = true;
        }

        // ── タブ定義（Blender プロパティパネル風） ──────────────────────
        struct TabInfo {
            ImTextureID texId;
            const char* tip;
            ImVec4 tint;       // アイコン色
            ImVec4 selBg;      // 選択時の背景色
        };

        const TabInfo kTabs[] = {
            { (ImTextureID)sIconWorldHandle.ptr,
              "Object Properties",
              ImVec4(0.96f, 0.65f, 0.14f, 1.0f),   // Blender 風オレンジ
              ImVec4(0.96f, 0.65f, 0.14f, 0.25f) },
            { (ImTextureID)sIconMaterialHandle.ptr,
              "Material Properties",
              ImVec4(0.90f, 0.30f, 0.40f, 1.0f),   // Blender 風レッド/ピンク
              ImVec4(0.90f, 0.30f, 0.40f, 0.25f) },
            { (ImTextureID)sIconSceneHandle.ptr,
              "Render Properties",
              ImVec4(0.34f, 0.67f, 0.88f, 1.0f),   // Blender 風ブルー
              ImVec4(0.34f, 0.67f, 0.88f, 0.25f) },
        };
        constexpr int   kTabCount = 3;
        constexpr float kStripW = 28.0f;  // タブストリップ幅
        constexpr float kIconSize = 16.0f;  // アイコン描画サイズ
        constexpr float kBtnPad = 4.0f;   // ボタン内パディング

        // ── 左側タブストリップ ──────────────────
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.04f, 0.04f, 0.04f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(3.0f, 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);

            UI::Scope::ChildScope tabStrip("##PropTabs", ImVec2(kStripW, 0.0f),
                ImGuiChildFlags_Border,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);

            for (int i = 0; i < kTabCount; ++i) {
                const bool sel = (inspectorTab_ == i);

                // 選択時 : 薄い色付き背景、非選択時 : 透明
                if (sel) {
                    ImGui::PushStyleColor(ImGuiCol_Button, kTabs[i].selBg);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                }
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.18f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kBtnPad, kBtnPad));

                char btnId[32];
                snprintf(btnId, sizeof(btnId), "##proptab%d", i);
                if (ImGui::ImageButton(btnId, kTabs[i].texId,
                    ImVec2(kIconSize, kIconSize),
                    ImVec2(0, 0), ImVec2(1, 1),
                    ImVec4(0, 0, 0, 0),  // 背景はスタイルで制御
                    kTabs[i].tint))
                {
                    inspectorTab_ = i;
                }

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("%s", kTabs[i].tip);
                }
            }
        }

        ImGui::SameLine(0.0f, 2.0f);

        // ── 右コンテンツエリア ─────────────────────────────────────────
        bool changed = false;
        {
            UI::Scope::ChildScope content("##PropContent", ImVec2(0.0f, 0.0f));
            switch (inspectorTab_) {
            case 0: changed |= DrawTransformSection(); break;
            case 1: changed |= DrawMaterialImGui();    break;
            case 2: changed |= DrawRenderSection();    break;
            default: break;
            }
        }

        return changed;
    }

    bool ModelGameObject::DrawTransformSection() {
        bool changed = false;

        // Blender風: ドラッグ中のカーソル非表示＆復帰用
        static ImVec2 sDragOrigin = {};
        static bool   sDragActive = false;

        Vector3& pos = transform_.translate;
        Vector3& rot = transform_.rotate;
        Vector3& sc = transform_.scale;

        // ── フィールドスタイル ───────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.14f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.26f, 0.26f, 0.36f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));

        // 右揃えラベル + DragFloat 行を描画するヘルパー
        auto row = [&](const char* label, float* val, float speed) {
            constexpr float kLabelCol = 82.0f;
            const float textW = ImGui::CalcTextSize(label).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (kLabelCol - textW));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::SetNextItemWidth(-FLT_MIN);
            char id[64];
            snprintf(id, sizeof(id), "##%s%p", label, static_cast<void*>(val));
            changed |= ImGui::DragFloat(id, val, speed, 0.0f, 0.0f, "%.3f");
            if (ImGui::IsItemActivated()) {
                sDragOrigin = ImGui::GetIO().MousePos;
                sDragActive = true;
                imguiSnapTranslate_ = transform_.translate;
                imguiSnapRotate_ = transform_.rotate;
                imguiSnapScale_ = transform_.scale;
                imguiSnapActive_ = isActive_;
            }
            // Blender風: ドラッグ中はカーソルを非表示にする
            if (sDragActive && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            }
            // ドラッグ終了時にカーソルをクリック開始位置へ戻す
            if (sDragActive && ImGui::IsItemDeactivated()) {
                ImGuiIO& io = ImGui::GetIO();
                io.MousePos = sDragOrigin;
                io.WantSetMousePos = true;
                sDragActive = false;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && onEditCommitted_) {
                onEditCommitted_(this,
                    imguiSnapTranslate_, imguiSnapRotate_,
                    imguiSnapScale_, imguiSnapActive_);
            }
            };

        UI::SectionHeader("Location");
        row("Location X", &pos.x, 0.1f);
        row("Y", &pos.y, 0.1f);
        row("Z", &pos.z, 0.1f);

        UI::SectionHeader("Rotation");
        row("Rotation X", &rot.x, 0.01f);
        row("Y", &rot.y, 0.01f);
        row("Z", &rot.z, 0.01f);

        UI::SectionHeader("Scale");
        row("Scale X", &sc.x, 0.01f);
        row("Y", &sc.y, 0.01f);
        row("Z", &sc.z, 0.01f);

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        return changed;
    }

    bool ModelGameObject::DrawRenderSection() {
        bool changed = false;

        constexpr float kLabelCol = 82.0f;

        // Blender 風フィールドスタイル
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.14f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.26f, 0.26f, 0.36f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));

        // 右揃えラベルを描画するヘルパー
        auto label = [&](const char* text) {
            const float tw = ImGui::CalcTextSize(text).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (kLabelCol - tw));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(text);
            ImGui::SameLine(0.0f, 6.0f);
            };

        // ── Render Pass (読み取り専用) ─────────────────────────
        UI::SectionHeader("Render Pass");
        {
            static const char* kPassNames[] = {
                "Shadow Map", "Model", "Skinned Model", "Sky Box",
                "Model Particle", "Line", "Particle", "Sprite"
            };
            const int passIdx = static_cast<int>(GetRenderPassType());
            const char* passName = (passIdx >= 0 && passIdx < 8) ? kPassNames[passIdx] : "Unknown";
            label("Pass Type");
            ImGui::TextUnformatted(passName);
        }

        // ── Blend Mode ─────────────────────────────────────────
        UI::SectionHeader("Blend Mode");
        {
            static const char* kBlendNames[] = {
                "None", "Normal", "Add", "Subtract", "Multiply", "Screen"
            };
            int blendIdx = static_cast<int>(blendMode_);
            label("Mode");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##blendMode", &blendIdx, kBlendNames, 6)) {
                blendMode_ = static_cast<BlendMode>(blendIdx);
                changed = true;
            }
        }

        // ── Render Order ──────────────────────────────────────
        UI::SectionHeader("Render Order");
        {
            bool hasOverride = renderOrder_.has_value();
            label("Override");
            if (ImGui::Checkbox("##renderOrderEnable", &hasOverride)) {
                if (hasOverride) {
                    SetRenderOrder(0);
                } else {
                    ResetRenderOrder();
                }
                changed = true;
            }
            if (hasOverride) {
                int orderVal = renderOrder_.value_or(0);
                label("Order");
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragInt("##renderOrder", &orderVal, 1.0f)) {
                    SetRenderOrder(orderVal);
                    changed = true;
                }
            }
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        return changed;
    }

    bool ModelGameObject::DrawMaterialImGui() {
        if (!model_ || !model_->GetMaterial()) return false;
        if (!materialDebugUI_) materialDebugUI_ = std::make_unique<MaterialDebugUI>();
        return materialDebugUI_->Draw(model_.get());
    }
#endif // USE_IMGUI

}  // namespace CoreEngine

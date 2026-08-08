#include "pch.h"
#include "ModelGameObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Render/Culling/ModelVisibility.h"
#include "Camera/Camera.h"
#include "Camera/View/ViewBuilder.h"
#include "Utility/JsonManager/JsonManager.h"
#include <cmath>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include "Collision/Debug/ColliderInspector.h"
#endif

namespace CoreEngine
{
    void ModelGameObject::Initialize() {
        // トランスフォームの GPU バッファは TransformComponent::Awake() が確保済み。
        // ここではフック（GetModelPath / GetTexturePath）の内容をコンポーネントへ流し込む。
        const std::string modelPath = GetModelPath();
        if (!modelPath.empty()) {
            meshRenderer_->SetModelFile(modelPath);
        }

        const std::string texPath = GetTexturePath();
        if (!texPath.empty()) {
            meshRenderer_->SetTexture(texPath);
        }

        OnInitialize();

        // OnInitialize() で SetCustomShaderProvider() が呼ばれた場合も含めてロードする
        meshRenderer_->ReloadFromSpec();

        SetActive(true);
    }

    void ModelGameObject::BuildCustomShaderPipelineIfNeeded(ID3D12Device* device, ModelManager* modelMgr)
    {
        // 実体は MeshRendererComponent が持つ（device / modelMgr はそちらが自分で引く）
        (void)device;
        (void)modelMgr;
        meshRenderer_->RebuildCustomShaderPipeline();
    }

    void ModelGameObject::Update() {
        if (!IsActive()) return;
        // TransferMatrix() は TransformComponent::Update() が行う。
        // GameObjectManager が「コンポーネントの Update → GameObject::Update」の順で
        // 呼ぶので、従来の `TransferMatrix() → OnUpdate()` と同じ順序が保たれる。
        OnUpdate();
    }

    void ModelGameObject::Draw(const Camera* camera) {
        // RenderGraph を経由しない直接呼び出し（レガシー経路）。
        // ViewInfo を持たないため、その場で 1 つ組み立てて本経路へ合流させる。
        if (!camera) return;
        const ViewInfo legacyView = ViewBuilder::Build(camera, RenderViewType::GameView);
        DrawViewInfo view{};
        view.view = &legacyView;
        Draw(view);
    }

    void ModelGameObject::Draw(const DrawViewInfo& view) {
        // カリング判定 → model_->Draw までは MeshRendererComponent が持つ。
        // カリングで落ちた場合は OnDraw も呼ばない（従来と同じ）。
        if (!meshRenderer_->DrawIfVisible(view)) {
            return;
        }
        OnDraw(view.GetCamera());
    }

    Vector3 ModelGameObject::GetWorldScale() const {
        return transformComponent_->GetWorldScale();
    }

    bool ModelGameObject::TryApplyCollisionPush(const Vector3& delta) {
        return transformComponent_->ApplyWorldDelta(delta);
    }

    BoundingBox ModelGameObject::GetWorldBoundingBox() const {
        return meshRenderer_->GetWorldBoundingBox();
    }

    json ModelGameObject::OnSerialize() const {
        json j;
        j["active"] = IsActive();
        if (!name_.empty()) {
            j["name"] = name_;
        }
        j["transform"]["translate"] = JsonManager::Vector3ToJson(transform_.translate);
        j["transform"]["rotate"] = JsonManager::Vector3ToJson(transform_.rotate);
        j["transform"]["scale"] = JsonManager::Vector3ToJson(transform_.scale);

        // コピー・Undo Redo 用にモデルパスを保存する
        const std::string modelPath = GetModelPath();
        if (!modelPath.empty()) {
            j["modelPath"] = modelPath;
        }

        if (!textureName_.empty()) {
            j["texture"] = textureName_;
        }

        // マテリアルスロットごとにシリアライズ（マルチマテリアル対応）
        if (model_ && model_->GetMaterialCount() > 0) {
            json materials = json::array();
            for (size_t i = 0; i < model_->GetMaterialCount(); ++i) {
                if (const MaterialInstance* mat = model_->GetMaterial(i)) {
                    materials.push_back(mat->ToJson());
                }
            }
            j["materials"] = std::move(materials);
        }

        return j;
    }

    void ModelGameObject::OnDeserialize(const json& j) {
        if (j.contains("name")) {
            name_ = j["name"].get<std::string>();
        }
        if (j.contains("active")) {
            SetActive(j["active"].get<bool>());
        }
        if (j.contains("transform")) {
            const json& t = j["transform"];
            transform_.translate = JsonManager::SafeGetVector3(t, "translate", transform_.translate);
            transform_.rotate = JsonManager::SafeGetVector3(t, "rotate", transform_.rotate);
            transform_.scale = JsonManager::SafeGetVector3(t, "scale", transform_.scale);
        }
        if (j.contains("texture")) {
            textureName_ = j["texture"].get<std::string>();
            if (!textureName_.empty()) {
                texture_ = TextureManager::GetInstance().Load(textureName_);
            }
        }
        if (model_) {
            if (j.contains("materials") && j["materials"].is_array()) {
                // 新フォーマット: マテリアルスロットごとの配列
                // 保存済みシーンは全モデルが毎回フルの materials 配列を書き出すため、
                // 素朴に FromJson すると未オーバーライドのモデルまで毎回 materialize してしまい
                // ModelResource 側の Copy-on-Write（同一モデル複数配置のバッチ統合）が意味を失う。
                // リソース既定値と一致する場合はスキップして共有デフォルトのままにする。
                const json& materials = j["materials"];
                const ModelResource* modelRes = model_->GetModelResource();
                for (size_t i = 0; i < materials.size() && i < model_->GetMaterialCount(); ++i) {
                    const MaterialInstance* def = modelRes
                        ? modelRes->GetDefaultMaterial(static_cast<uint32_t>(i)) : nullptr;
                    if (def && def->ToJson() == materials[i]) {
                        continue;
                    }
                    if (MaterialInstance* mat = model_->GetMaterial(i)) {
                        mat->FromJson(materials[i]);
                    }
                }
            } else if (j.contains("material")) {
                // 旧フォーマット: 単一マテリアル（全スロットへ適用 = 旧動作と同じ）
                const json& m = j["material"];
                model_->ForEachMaterial([&m](MaterialInstance* mat) {
                    mat->FromJson(m);
                });
            }
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

    int ModelGameObject::GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const {
        if (maxTabs < 5) return 0;
        outTabs[0] = { "scene.png",      "描画設定",     {0.34f,0.67f,0.88f,1.0f}, {0.34f,0.67f,0.88f,0.25f} };
        outTabs[1] = { "object_data.png", "トランスフォーム", {0.96f,0.65f,0.14f,1.0f}, {0.96f,0.65f,0.14f,0.25f} };
        outTabs[2] = { "material.png",    "マテリアル",   {0.90f,0.30f,0.40f,1.0f}, {0.90f,0.30f,0.40f,0.25f} };
        outTabs[3] = { "imagePlane.png",  "テクスチャ",   {0.60f,0.40f,0.80f,1.0f}, {0.60f,0.40f,0.80f,0.25f} };
        outTabs[4] = { "obj.png",         "コライダー",   {0.35f,0.85f,0.45f,1.0f}, {0.35f,0.85f,0.45f,0.25f} };
        return 5;
    }

    bool ModelGameObject::DrawInspectorTabContent(int tabIndex) {
        switch (tabIndex) {
        case 0: return DrawRenderSection();
        case 1: return DrawTransformSection();
        case 2: return DrawMaterialImGui();
        case 3: return DrawTextureSection();
        case 4: return ColliderInspector::Draw(*this);
        default: return false;
        }
    }

    bool ModelGameObject::DrawTransformSection() {
        bool changed = false;

        // ドラッグ中のカーソル非表示＆復帰用
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
            if (sDragActive && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            }
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

        UI::SectionHeader("位置");
        row("位置 X", &pos.x, 0.1f);
        row("Y", &pos.y, 0.1f);
        row("Z", &pos.z, 0.1f);

        UI::SectionHeader("回転");
        row("回転 X", &rot.x, 0.01f);
        row("Y", &rot.y, 0.01f);
        row("Z", &rot.z, 0.01f);

        UI::SectionHeader("スケール");
        row("スケール X", &sc.x, 0.01f);
        row("Y", &sc.y, 0.01f);
        row("Z", &sc.z, 0.01f);

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        return changed;
    }

    bool ModelGameObject::DrawRenderSection() {
        bool changed = false;

        constexpr float kLabelCol = 82.0f;

        // フィールドスタイル
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

        // ── 描画パス（読み取り専用） ─────────────────────────
        UI::SectionHeader("描画パス");
        {
            static const char* kPassNames[] = {
                "Shadow Map", "Model", "Skinned Model", "Sky Box",
                "Model Particle", "Line", "Particle", "Sprite"
            };
            const int passIdx = static_cast<int>(GetRenderPassType());
            const char* passName = (passIdx >= 0 && passIdx < 8) ? kPassNames[passIdx] : "Unknown";
            label("パス種別");
            ImGui::TextUnformatted(passName);
        }

        // ── 描画順序 ──────────────────────────────────────────
        UI::SectionHeader("描画順序");
        {
            bool hasOverride = renderOrder_.has_value();
            label("オーバーライド");
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
                label("順序値");
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
        bool changed = false;

        // ── ブレンドモード（マテリアルの描画設定として管理） ────────
        {
            constexpr float kLabelCol = 82.0f;

            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.14f, 0.14f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.26f, 0.26f, 0.36f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));

            UI::SectionHeader("ブレンドモード");
            static const char* kBlendNames[] = {
                "なし", "通常", "加算", "減算", "乗算", "スクリーン"
            };
            int blendIdx = static_cast<int>(blendMode_);
            const float tw = ImGui::CalcTextSize("モード").x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (kLabelCol - tw));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("モード");
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##blendMode", &blendIdx, kBlendNames, 6)) {
                blendMode_ = static_cast<BlendMode>(blendIdx);
                changed = true;
            }

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
        }

        // ── Material Properties ─────────────────────────────────
        if (model_ && model_->GetMaterial()) {
            if (!materialDebugUI_) materialDebugUI_ = std::make_unique<MaterialDebugUI>();
            changed |= materialDebugUI_->Draw(model_.get());
        }

        return changed;
    }

    bool ModelGameObject::DrawTextureSection() {
        bool changed = false;

        // ── テクスチャプレビュー ─────────────────────────────────────
        UI::SectionHeader("ベーステクスチャ");

        // 現在のテクスチャのプレビュー表示
        constexpr float kPreviewSize = 96.0f;
        const bool hasTexture = (texture_.gpuHandle.ptr != 0);

        if (hasTexture) {
            // テクスチャサムネイルを中央揃えで描画
            const float availW = ImGui::GetContentRegionAvail().x;
            if (availW > kPreviewSize) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - kPreviewSize) * 0.5f);
            }
            ImGui::Image((ImTextureID)texture_.gpuHandle.ptr,
                ImVec2(kPreviewSize, kPreviewSize));
        }

        // ── ドロップターゲットエリア ─────────────────────────────────
        {
            // 枠線スタイルを設定
            const bool isDragHovering = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
            ImVec4 borderCol = isDragHovering
                ? ImVec4(0.60f, 0.40f, 0.80f, 0.8f)
                : ImVec4(0.30f, 0.30f, 0.30f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.18f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

            // ドロップ受付用のボタン領域
            const float areaW = ImGui::GetContentRegionAvail().x;
            constexpr float kAreaH = 40.0f;
            ImGui::Button(hasTexture ? textureName_.c_str() : "テクスチャをドロップ", ImVec2(areaW, kAreaH));

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);

            // ドロップターゲット: プロジェクトビューからのテクスチャファイルを受け付ける
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_FILE")) {
                    const char* droppedFilename = static_cast<const char*>(payload->Data);
                    texture_ = TextureManager::GetInstance().Load(droppedFilename);
                    textureName_ = droppedFilename;
                    changed = true;
                }
                ImGui::EndDragDropTarget();
            }
        }

        // ── テクスチャ解除ボタン ─────────────────────────────────────
        if (hasTexture) {
            ImGui::Spacing();
            if (ImGui::Button("テクスチャ解除", ImVec2(-FLT_MIN, 0.0f))) {
                texture_ = {};
                textureName_.clear();
                changed = true;
            }
        }

        return changed;
    }

    bool ModelGameObject::DrawImGui() {
        // 基底クラスの共通インスペクターを使用
        return GameObject::DrawImGui();
    }
#endif // USE_IMGUI

}  // namespace CoreEngine

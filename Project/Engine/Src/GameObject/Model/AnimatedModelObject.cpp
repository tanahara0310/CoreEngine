#include "pch.h"
#include "AnimatedModelObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Model/Animation/AnimationPlayer.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Texture/TextureManager.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

namespace CoreEngine
{
    void AnimatedModelObject::Initialize() {
        auto* engine = GetEngineSystem();
        auto* dxCommon = engine->GetService<DirectXCommon>();
        auto* modelMgr = engine->GetService<ModelManager>();

        // トランスフォームの初期化（GPU 定数バッファを確保）
        if (dxCommon) {
            transform_.Initialize(dxCommon->GetDevice());
        }

        // アニメーションを先にロード（スケルトン生成前に必要）
        const std::string modelPath = GetModelPath();
        if (!modelPath.empty() && modelMgr) {
            // 初期クリップ
            AnimationLoadInfo info;
            info.modelFile = modelPath;
            info.animationName = GetAnimationName();
            info.animationFile = GetAnimationFile();
            info.sourceAnimationName = GetSourceAnimationName();
            modelMgr->LoadAnimation(info);

            // 追加クリップ（同じ ModelResource へ名前付きで登録される）
            for (const AnimationClipDesc& clip : GetAdditionalAnimationClips()) {
                AnimationLoadInfo extra;
                extra.modelFile = modelPath;
                extra.animationName = clip.name;
                extra.animationFile = clip.file.empty() ? GetAnimationFile() : clip.file;
                extra.sourceAnimationName = clip.sourceName;
                modelMgr->LoadAnimation(extra);
            }

            // スケルトンアニメーションモデルとして生成
            model_ = modelMgr->CreateSkeletonModel(modelPath, GetAnimationName(), true);
            animator_->SetCurrentClipName(GetAnimationName());
        }

        // テクスチャのロード（パスが空でなければ）
        const std::string texPath = GetTexturePath();
        if (!texPath.empty()) {
            texture_ = TextureManager::GetInstance().Load(texPath);
            textureName_ = texPath;
        }

        // 派生クラスの追加初期化（座標・コライダー設定など）
        OnInitialize();
        SetActive(true);
    }

    void AnimatedModelObject::OnUpdate() {
        // アニメーションの前進と骨のデバッグ描画は AnimatorComponent::Update() が行う。
        // それは GameObject::Update()（＝この OnUpdate の呼び出し元）より前に走るので、
        // ここに来た時点でスケルトンは今フレームの姿勢になっている。
        if (model_ && model_->GetAnimationPlayer()) {
            OnAnimationUpdated();
        }
    }

#ifdef USE_IMGUI
    int AnimatedModelObject::GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const {
        const int baseCount = ModelGameObject::GetInspectorTabs(outTabs, maxTabs);
        if (baseCount == 0 || maxTabs < baseCount + 1) {
            return baseCount;
        }

        outTabs[baseCount] = { "deltaTime.png", "アニメーション",
            {0.30f, 0.85f, 0.55f, 1.0f}, {0.30f, 0.85f, 0.55f, 0.25f} };
        return baseCount + 1;
    }

    bool AnimatedModelObject::DrawInspectorTabContent(int tabIndex) {
        // 基底のタブ数は基底クラスに問い合わせる（インデックスの直書きを避ける）
        InspectorTabDef scratch[16]{};
        const int baseCount = ModelGameObject::GetInspectorTabs(scratch, 16);

        if (tabIndex < baseCount) {
            return ModelGameObject::DrawInspectorTabContent(tabIndex);
        }
        return DrawAnimationSection();
    }

    bool AnimatedModelObject::DrawAnimationSection() {
        bool changed = false;

        const AnimationPlayer* player = model_ ? model_->GetAnimationPlayer() : nullptr;
        if (!player) {
            ImGui::TextDisabled("アニメーションを持たないモデルです");
            return false;
        }

        ImGui::Text("再生時刻: %.3f s", player->GetTime());

        if (const Skeleton* skeleton = GetSkeleton()) {
            ImGui::Text("ジョイント数: %d", static_cast<int>(skeleton->joints.size()));
        }

        // ===== クリップ切り替え（アニメーションブレンド） =====
        const std::vector<std::string> clipNames = GetAnimationClipNames();
        const std::string& currentClip = GetCurrentClipName();
        if (clipNames.size() > 1) {
            ImGui::Separator();
            ImGui::Text("現在のクリップ: %s", currentClip.c_str());
            if (player->IsBlending()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "(ブレンド中)");
            }

            ImGui::SliderFloat("ブレンド時間", &imguiBlendDuration_, 0.0f, 2.0f, "%.2f s");

            for (const std::string& clipName : clipNames) {
                if (clipName == currentClip) {
                    continue; // 同じクリップへのブレンドは意味がない
                }
                if (ImGui::Button(clipName.c_str())) {
                    SwitchAnimationWithBlend(clipName, imguiBlendDuration_);
                    changed = true;
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }

        ImGui::Separator();

        bool debugDraw = IsSkeletonDebugDrawEnabled();
        if (ImGui::Checkbox("骨のデバッグ表示", &debugDraw)) {
            SetSkeletonDebugDrawEnabled(debugDraw);
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("スケルトンの親子関係を線で、ジョイント位置を十字で描画します");
        }

        return changed;
    }
#endif // USE_IMGUI

}  // namespace CoreEngine

#include "AnimatedCubeObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Model/Animation/Animator.h"
#include "Camera/ICamera.h"

#ifdef _DEBUG
#include "Utility/Debug/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

void AnimatedCubeObject::Initialize() {
    // 必須コンポーネントの取得
    auto engine = GetEngineSystem();

    auto dxCommon = engine->GetComponent<DirectXCommon>();
    auto modelManager = engine->GetComponent<ModelManager>();

    if (!dxCommon || !modelManager) {
        return;
    }

    // アニメーションを事前に読み込む（ファイル名のみ指定、ディレクトリは AssetDatabase が解決）
    AnimationLoadInfo animLoadInfo{
        .modelFile = "AnimatedCube.gltf",
        .animationName = "default"
    };
    modelManager->LoadAnimation(animLoadInfo);

    // キーフレームアニメーションモデルとして作成（ファイル名のみ指定）
    model_ = modelManager->CreateKeyframeModel(
        "AnimatedCube.gltf",
        "default",
        true
    );

    // トランスフォームの初期化
    transform_.Initialize(dxCommon->GetDevice());
    transform_.translate = { 5.0f, 0.0f, 0.0f };  // 他のオブジェクトと被らない位置
    transform_.scale = { 1.0f, 1.0f, 1.0f };
    transform_.rotate = { 0.0f, 0.0f, 0.0f };

    // テクスチャの読み込み
    auto& textureManager = CoreEngine::TextureManager::GetInstance();
    texture_ = textureManager.Load("SampleAssets/AnimatedCube/AnimatedCube_BaseColor.png");
}

void AnimatedCubeObject::Update() {
    // アニメーションの更新
    if (model_ && model_->HasAnimationController()) {
        model_->UpdateAnimation(deltaTime_);
    }

    // トランスフォームの更新
    transform_.TransferMatrix();
}

void AnimatedCubeObject::Draw(const CoreEngine::ICamera* camera) {
    if (!camera || !model_) return;

    // モデルの描画
    model_->Draw(transform_, camera, texture_.gpuHandle);
}

#ifdef _DEBUG
int AnimatedCubeObject::GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const {
    // 親クラス（ModelGameObject）のタブを取得
    int count = ModelGameObject::GetInspectorTabs(outTabs, maxTabs);

    // アニメーションタブを追加
    if (count < maxTabs && model_ && model_->HasAnimationController()) {
        outTabs[count] = { "obj.png", "アニメーション", {0.40f,0.80f,0.40f,1.0f}, {0.40f,0.80f,0.40f,0.25f} };
        ++count;
    }
    return count;
}

bool AnimatedCubeObject::DrawInspectorTabContent(int tabIndex) {
    // 親クラスのタブ数を取得（0〜3 は ModelGameObject のタブ）
    constexpr int kParentTabCount = 4;
    if (tabIndex < kParentTabCount) {
        return ModelGameObject::DrawInspectorTabContent(tabIndex);
    }

    // ── アニメーション ───────────────────────────────────
    bool changed = false;
    if (model_ && model_->HasAnimationController()) {
        UI::SectionHeader("再生制御");

        float animSpeed = GetAnimationSpeed();
        if (UI::SliderFloat("速度", animSpeed, 0.0f, 3.0f)) {
            SetAnimationSpeed(animSpeed);
            changed = true;
        }

        float animTime = GetAnimationTime();
        ImGui::Text("時間: %.2f 秒", animTime);

        if (ImGui::Button("リセット##anim")) {
            ResetAnimation();
            changed = true;
        }
    }
    return changed;
}
#endif

void AnimatedCubeObject::SetAnimationSpeed(float /* speed */) {
    // 再生速度の変更はまだ実装されていない
    // TODO: Animatorに速度変更機能を追加する
}

float AnimatedCubeObject::GetAnimationSpeed() const {
    // 再生速度の取得はまだ実装されていない
    return 1.0f;
}

void AnimatedCubeObject::ResetAnimation() {
    if (model_) {
        model_->ResetAnimation();
    }
}

float AnimatedCubeObject::GetAnimationTime() const {
    if (model_) {
        return model_->GetAnimationTime();
    }
    return 0.0f;
}

bool AnimatedCubeObject::IsAnimationFinished() const {
    if (model_) {
        return model_->IsAnimationFinished();
    }
    return true;
}


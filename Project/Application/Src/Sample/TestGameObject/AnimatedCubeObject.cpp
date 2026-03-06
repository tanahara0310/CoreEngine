#include "AnimatedCubeObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Model/Animation/Animator.h"
#include "Camera/ICamera.h"

#ifdef _DEBUG
#include "externals/imgui/imgui.h"
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

    // アニメーションを事前に読み込む
    AnimationLoadInfo animLoadInfo{
        .directory = "SampleAssets/AnimatedCube",
        .modelFilename = "AnimatedCube.gltf",
        .animationName = "default"
    };
    modelManager->LoadAnimation(animLoadInfo);

    // キーフレームアニメーションモデルとして作成
    model_ = modelManager->CreateKeyframeModel(
        "SampleAssets/AnimatedCube/AnimatedCube.gltf",
        "default",  // アニメーション名
        true    // ループ再生
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
bool AnimatedCubeObject::DrawImGuiExtended() {
    bool changed = false;

    // アニメーション制御（拡張部分）
    if (model_ && model_->HasAnimationController()) {
        if (ImGui::TreeNode("アニメーション")) {
            // 再生速度
            float animSpeed = GetAnimationSpeed();
            if (ImGui::SliderFloat("速度", &animSpeed, 0.0f, 3.0f)) {
                SetAnimationSpeed(animSpeed);
                changed = true;
            }

            // アニメーション時間表示
            float animTime = GetAnimationTime();
            ImGui::Text("時間: %.2f 秒", animTime);

            // リセットボタン
            if (ImGui::Button("アニメーションをリセット")) {
                ResetAnimation();
                changed = true;
            }

            ImGui::TreePop();
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


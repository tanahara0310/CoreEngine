#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector3.h"
#include "Utility/CVar/CVar.h"

namespace GameComponents
{
    /// @brief 出現したものを、潰し・引き伸ばしを付けてポップに登場させる拡縮係数。
    ///
    /// 伸び上がる → 着地で潰れる → 跳ね戻って収まる、の3段で1回だけ再生する。
    /// 倍率は CVar を共有するので、使うものはすべて同じ動きで現れる。
    ///
    /// スケール自体はこのコンポーネントでは書かない。通常再生でワールド行列が
    /// 焼かれるのは `TransformComponent::Update()` の1回きりで、
    /// `GameObjectManager::SyncTransforms()` は停止中しか走らないため、
    /// それより後（LateUpdate など）に書いた値はそのフレームの描画へ届かない。
    /// 係数だけを持ち、スケールを書く側に毎フレーム掛けてもらう。
    /// 親に使えば、子は親のスケールを継いで一緒に潰れる。
    class SpawnPopComponent final : public CoreEngine::IComponent
    {
    public:
        static CoreEngine::CVar<float> SpawnScale;
        static CoreEngine::CVar<float> StretchDuration;
        static CoreEngine::CVar<float> StretchWidth;
        static CoreEngine::CVar<float> StretchHeight;
        static CoreEngine::CVar<float> SquashDuration;
        static CoreEngine::CVar<float> SquashWidth;
        static CoreEngine::CVar<float> SquashHeight;
        static CoreEngine::CVar<float> SettleDuration;

        // コンポーネントを識別する名前。必須
        const char* GetTypeName() const override {
            return "SpawnPop";
        }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "出現演出"; }
        bool DrawInspector() override;
#endif

        // アタッチされた時点で係数を縮めておく（最初のスケール計算から効かせる）
        void Awake() override;
        // 最初の更新直前に一度だけ呼ばれる
        void Start() override;

        /// @brief スケールへ掛ける拡縮係数。1 倍で通常の見た目になる。
        /// @note 寿命はこのコンポーネントと同じ。渡した先より長く生きること。
        const CoreEngine::Vector3* GetScaleMultiplier() const { return &popScale_; }

        // 出現演出を最初から再生する
        void PlayPopIn();

    private:
        // 幅（XZ）と高さ（Y）に別々の倍率を入れた係数を作る
        static CoreEngine::Vector3 Factor(float widthRate, float heightRate);

        CoreEngine::Vector3 popScale_{ 1.0f, 1.0f, 1.0f };
    };
}

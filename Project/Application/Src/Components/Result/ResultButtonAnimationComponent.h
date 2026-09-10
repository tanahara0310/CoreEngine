#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector4.h"
#include "Utility/CVar/CVar.h"

#include <functional>
#include <string>
#include <utility>

namespace CoreEngine
{
    class UIText;
}

namespace GameComponents
{
    /// @brief リザルト画面の選択肢に、選択・決定リアクションを付けるコンポーネント
    ///
    /// 選択中の文字だけをこのコンポーネントで制御することで、シーン側は
    /// 「どの項目を選んだか」と「どのシーンへ遷移するか」だけを担当する。
    class ResultButtonAnimationComponent final : public CoreEngine::IComponent
    {
    public:
        static CoreEngine::CVar<float> ReactionScale;
        static CoreEngine::CVar<float> ReactionDuration;
        static CoreEngine::CVar<float> SelectedScale;
        static CoreEngine::CVar<float> ConfirmScale;
        static CoreEngine::CVar<float> ConfirmDuration;
        static CoreEngine::CVar<float> UnselectedFadeScale;
        static CoreEngine::CVar<float> UnselectedFadeDuration;
        static CoreEngine::CVar<float> SelectedBobDistance;
        static CoreEngine::CVar<float> SelectedBobDuration;
        static CoreEngine::CVar<CoreEngine::Vector4> SelectedColor;

        explicit ResultButtonAnimationComponent(std::string tweenId)
            : tweenId_(std::move(tweenId)) {
        }

        const char* GetTypeName() const override { return "ResultButtonAnimation"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "リザルトボタン演出"; }
        const char* GetInspectorIcon() const override { return "scene.png"; }

        void GetInspectorIconColor(float* outRgba) const override
        {
            outRgba[0] = 0.30f;
            outRgba[1] = 0.70f;
            outRgba[2] = 1.00f;
            outRgba[3] = 1.00f;
        }

        bool DrawInspector() override;
#endif

        void Start() override;
        void OnDestroy() override;

        /// @brief 選択状態を反映する。演出の再生は PlaySelectionReaction() で行う。
        void SetSelected(bool selected);

        /// @brief 選択された瞬間の縮小 → 復帰 → 上下待機を再生する
        void PlaySelectionReaction();

        /// @brief 決定時の拡大 → 縮小・フェードアウトを再生してから完了通知を呼ぶ
        void PlayConfirmReaction(std::function<void()> onFinished = {});

        /// @brief 決定時に未選択ボタンを縮小しながらフェードアウトする
        void PlayUnselectedFade();

    private:
        void StopTweens();
        void StartSelectedIdle();
        void PlayScaleReaction(std::function<void()> onFinished);
        /// @brief 文字色とアウトラインへ同じアルファを反映する。
        /// @note アウトラインは文字と別の色として持っているため、文字だけフェードさせると
        ///       「輪郭だけ残って消えない」見え方になる。決定・非選択フェードは必ずこれを使うこと
        void ApplyColor(const CoreEngine::Vector4& color);

        CoreEngine::UIText* text_ = nullptr;
        std::string tweenId_;

        CoreEngine::Vector4 normalColor_{ 1.0f, 1.0f, 1.0f, 0.9f };
        float baseFontSize_ = 40.0f;
        float basePositionY_ = 0.0f;
        bool selected_ = false;
        bool started_ = false;
    };
}

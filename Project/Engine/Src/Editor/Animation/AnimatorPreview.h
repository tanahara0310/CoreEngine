#pragma once

#ifdef CORE_EDITOR

#include "Animation/AnimatorRuntime.h"
#include "GameObject/ObjectId.h"

#include <string>

namespace CoreEngine
{
    class AnimatorComponent;
    class SpriteAnimatorComponent;
    struct Skeleton;
}

namespace CoreEngine::Editor
{
    struct AnimatorDocument;

    /// @brief 停止中に、Hierarchy で選んだオブジェクトへ窓の状態の姿勢を出す（シーンで見る）
    /// @details 窓の今の中身（保存する前でも）から実行の状態を作り、選んだ状態の決まった進みの姿勢を渡す。
    ///          やめたとき・選ぶオブジェクトが変わったとき・再生を始めたときは、元の姿勢へ戻す。
    class AnimatorPreview
    {
    public:
        /// @brief 開いているタブの設定で毎フレーム呼ぶ
        void Update(AnimatorDocument& document);

        /// @brief 姿勢を出していれば元へ戻す
        void Stop();

        /// @brief シーンに触らずに、出している先を忘れる（窓を終えるとき）
        void Forget() { target_ = ObjectId{}; }

        /// @brief シーンで見る状態の名前（選んでいる状態か、最初の状態。無ければ空）
        static std::string PreviewStateOf(const AnimatorDocument& document);

        /// @brief Animator がこの設定を使っているか
        static bool UsesDocument(const AnimatorComponent& animator, const AnimatorDocument& document);

    private:
        /// @param sprite スプライトアニメを動かしているときはそのコンポーネント（骨の並びの代わりに使う）
        void Rebuild(const AnimatorDocument& document, const Skeleton* skeleton, const SpriteAnimatorComponent* sprite);

        /// @brief スプライトアニメのクリップの名前と尺を並べた文字（変わったら作り直す）
        static std::string SpriteClipsKey(const SpriteAnimatorComponent& sprite);

        ObjectId target_{};
        AnimatorRuntime runtime_;
        std::string builtText_;
        const Skeleton* builtSkeleton_ = nullptr;
        const SpriteAnimatorComponent* builtSprite_ = nullptr;
    };
}

#endif // CORE_EDITOR

#pragma once

#ifdef CORE_EDITOR

#include <string>

namespace CoreEngine
{
    class AnimatorComponent;
    class AnimatorRuntime;
}

namespace CoreEngine::Editor
{
    struct AnimatorDocument;

    /// @brief 再生中に映すもの（Hierarchy で選んだオブジェクトの Animator がこの設定で動いているとき）
    struct AnimatorLiveView
    {
        /// @brief 再生中に選んでいるオブジェクトが同じ設定で動いていれば、その実行の状態を集める
        /// @return 映すものが無ければ runtime が nullptr
        static AnimatorLiveView Find(const AnimatorDocument& document);

        AnimatorComponent* animator = nullptr;
        AnimatorRuntime* runtime = nullptr;
        std::string objectName;
        std::string currentState;
        std::string sourceState;   ///< 遷移の元（無ければ空）
        std::string transitionFrom; ///< 流れている遷移の元（表の遷移でなければ空）
        std::string transitionTo;
        float normalizedTime = 0.0f;
        float transitionProgress = 0.0f;
        bool inTransition = false;
    };
}

#endif // CORE_EDITOR

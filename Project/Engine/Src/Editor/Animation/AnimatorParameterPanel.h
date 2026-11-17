#pragma once

#ifdef CORE_EDITOR

namespace CoreEngine::Editor
{
    struct AnimatorDocument;
    struct AnimatorLiveView;

    /// @brief アニメーターの窓の左（パラメータとクリップの表）
    /// @details 再生中はパラメータの欄が、動いているオブジェクトの今の値になる。
    class AnimatorParameterPanel
    {
    public:
        void Draw(AnimatorDocument& document, const AnimatorLiveView& live);

    private:
        void DrawParameters(AnimatorDocument& document, const AnimatorLiveView& live);
        void DrawClips(AnimatorDocument& document);
    };
}

#endif // CORE_EDITOR

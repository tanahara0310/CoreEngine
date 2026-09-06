#pragma once

#include "ISceneFeature.h"

namespace CoreEngine
{
    /// @brief 再生中のトゥイーンをシーンの更新順の中で前進させる Feature
    /// @details TweenManager はプロセス寿命のシングルトンだが、「いつ進めるか」は
    ///          シーンの更新順に属する情報なので、他の更新タイミングと同じ
    ///          フェーズ + priority の仕組みで表現する。
    /// @note PreObjectUpdate の最後（kLateFeaturePriority）で回すこと。
    ///       GameObject の更新より前に進めることで、コンポーネントが同じフレームで
    ///       トゥイーン後の値を読める（追従処理が 1 フレーム遅れない）。
    class TweenFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "Tween"; }

        /// @brief PreObjectUpdate でトゥイーンを 1 フレーム分進める
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 再生途中のトゥイーンを畳む（GameObject の破棄後）
        /// @details 対象の GameObject はすでに解放されているので、値の書き戻しは
        ///          行わずに実体だけを捨てる。
        void PostSceneFinalize(SceneContext& ctx) override;
    };
}

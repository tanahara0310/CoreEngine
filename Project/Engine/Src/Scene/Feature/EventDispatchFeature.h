#pragma once

#include "ISceneFeature.h"

namespace CoreEngine
{
    /// @brief 積まれたイベントをシーンの更新順の中で一括配信する Feature
    /// @details EventBus はプロセス寿命のシングルトンだが、「いつ配信するか」は
    ///          シーンの更新順に属する情報なので、他の更新タイミングと同じ
    ///          フェーズ + priority の仕組みで表現する。
    /// @note PostObjectUpdate の最後（kLateFeaturePriority）で回すこと。
    ///       衝突判定より後・LateUpdate より前に置くことで、Queue されたイベントへの
    ///       反応が同じフレームの LateUpdate と描画に間に合う。
    ///       ここから先で Queue された分は次フレームに回る。
    class EventDispatchFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "EventDispatch"; }

        /// @brief PostObjectUpdate で Queue 済みイベントをまとめて配信する
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 取り残された購読と未配信イベントを畳む（GameObject の破棄後）
        /// @details GameObject / Feature が持つ Subscription は破棄で個別に解除済み。
        ///          シーン自身やその他が握っていた分をここで確実に断ち切り、
        ///          次のシーンへ前のシーンのハンドラを持ち越さない。
        void PostSceneFinalize(SceneContext& ctx) override;
    };
}
